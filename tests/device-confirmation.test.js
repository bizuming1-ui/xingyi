import {test} from 'node:test';
import assert from 'node:assert/strict';
import {DeviceConfirmation,DEVICE_CONFIRMATION_KEY} from '../src/device-confirmation.js';
const device='10000000-0000-4000-8000-000000000001',order='20000000-0000-4000-8000-000000000001';
const request='30000000-0000-4000-8000-000000000001',barrier='40000000-0000-4000-8000-000000000001';
class Storage {
  data=new Map(); fail=false;
  getItem(key){return this.data.has(key)?this.data.get(key):null;}
  setItem(key,value){if(this.fail)throw Error('quota');this.data.set(key,value);}
  removeItem(key){this.data.delete(key);}
}
// Simulate a cross-tab lock, with a shared promise queue.
class Locks {
  tail=Promise.resolve();
  request(_name,_options,callback){const next=this.tail.then(callback);this.tail=next.catch(()=>{});return next;}
}
function fixture() {
  const storage=new Storage(),locks=new Locks();let state='requested',openBarrier=false,lostRequest=false,lostCommit=false,failAfterCommit=false;
  const calls=[];let commits=0,requests=0;
  const result={requested:8,applied:5,current:100};
  const db={async rpc(name,args){
    calls.push([name,args]);
    if(name==='device_confirmation_open')return {data:openBarrier && state!=='completed' && state!=='cancelled'?{id:barrier,request_id:request,device_id:device,order_id:order,state}:null};
    if(name==='device_confirmation_request') {requests++;openBarrier=true;assert.equal(args.p_request,request);if(lostRequest){lostRequest=false;return {error:{}};}return {data:barrier};}
    if(name==='device_confirmation_view')return {data:{id:barrier,order_id:order,state,frozen_count:8,result:state==='completed'?result:null}};
    if(name==='device_confirmation_commit') {commits++;state='completed';if(failAfterCommit)storage.fail=true;if(lostCommit){lostCommit=false;return {error:{}};}return {data:result};}
    if(name==='device_confirmation_cancel') {if(state==='completed')return {error:{}};state='cancelled';return {data:true};}
    throw Error('unknown');
  }};
  const controller=()=>new DeviceConfirmation({db,storage,locks,newId:()=>request});
  return {storage,controller,calls,db,open:()=>openBarrier=true,setState:s=>state=s,loseRequest:()=>lostRequest=true,loseCommit:()=>lostCommit=true,failAfterCommit:()=>failAfterCommit=true,
    counts:()=>({requests,commits}),result};
}
test('lost request is recovered with same UUID and without duplicate local requests',async()=>{
  const f=fixture(),c=f.controller();f.loseRequest();
  await assert.rejects(()=>c.start({deviceId:device,orderId:order,operatorPaused:true}));
  assert.equal((await c.current()).requestId,request);assert.equal((await c.current()).barrierId,null);
  await assert.rejects(()=>c.acknowledge(),/尚未结束/);
  assert.equal((await c.start({deviceId:device,orderId:order,operatorPaused:true})).barrierId,barrier);
  assert.equal(f.counts().requests,2);
  // A second tab sees the existing record rather than inventing a new request.
  await f.controller().start({deviceId:device,orderId:order,operatorPaused:true});assert.equal(f.counts().requests,2);
});
test('lost commit response is resolved by querying the same barrier',async()=>{
  const f=fixture(),c=f.controller();await c.start({deviceId:device,orderId:order,operatorPaused:true});
  await assert.rejects(()=>c.confirm({operatorPaused:true}),/尚未完成冻结/);
  f.setState('ready');f.loseCommit();await assert.rejects(()=>c.confirm({operatorPaused:true}));
  assert.equal((await c.current()).commitAttempted,true);
  assert.deepEqual((await f.controller().observe()).result,f.result);
  assert.equal(f.counts().commits,1);
  await assert.rejects(()=>c.cancel(),/不能取消/);
  await c.acknowledge();assert.equal(await c.current(),null);
});
test('cancel and expired states do not silently clear unresolved records',async()=>{
  const f=fixture(),c=f.controller();await c.start({deviceId:device,orderId:order,operatorPaused:true});f.setState('expired');
  assert.equal((await c.observe()).state,'expired');await assert.rejects(()=>c.acknowledge(),/尚未结束/);
  assert.equal(await c.cancel(),true);await c.acknowledge();assert.equal(await c.current(),null);
});
test('storage, pause and cross-tab safeguards fail closed',async()=>{
  const f=fixture(),c=f.controller();
  await assert.rejects(()=>c.start({deviceId:device,orderId:order,operatorPaused:false}),/暂停/);
  f.storage.fail=true;await assert.rejects(()=>c.start({deviceId:device,orderId:order,operatorPaused:true}),/quota/);assert.equal(f.counts().requests,0);
  f.storage.fail=false;f.storage.setItem(DEVICE_CONFIRMATION_KEY,'broken');
  await assert.rejects(()=>c.start({deviceId:device,orderId:order,operatorPaused:true}),/损坏/);assert.equal(f.counts().requests,0);
  const noLocks=new DeviceConfirmation({db:{},storage:f.storage,locks:null});
  await assert.rejects(()=>noLocks.current(),/多标签页/);
});
test('two tabs serialize creation and reject mismatched order',async()=>{
  const f=fixture(),a=f.controller(),b=f.controller();
  const results=await Promise.allSettled([
    a.start({deviceId:device,orderId:order,operatorPaused:true}),
    b.start({deviceId:device,orderId:'50000000-0000-4000-8000-000000000001',operatorPaused:true})
  ]);
  assert.equal(results[0].status,'fulfilled');assert.equal(results[1].status,'rejected');assert.equal(f.counts().requests,1);
});

test('commit winning a cancellation race keeps the recovery record',async()=>{
  const f=fixture(),c=f.controller();await c.start({deviceId:device,orderId:order,operatorPaused:true});
  f.setState('ready');f.loseCommit();await assert.rejects(()=>c.confirm({operatorPaused:true}));
  await assert.rejects(()=>c.cancel());assert.equal((await c.current()).terminal,null);
  assert.equal((await c.observe()).state,'completed');assert.equal((await c.current()).terminal.state,'completed');
});
test('storage failure after a successful commit retains the attempted record',async()=>{
  const f=fixture(),c=f.controller();await c.start({deviceId:device,orderId:order,operatorPaused:true});
  f.setState('ready');f.failAfterCommit();await assert.rejects(()=>c.confirm({operatorPaused:true}),/quota/);
  f.storage.fail=false;assert.equal((await c.current()).commitAttempted,true);
  assert.equal((await c.observe()).state,'completed');assert.equal(f.counts().commits,1);
});
test('untrusted status and confirmation results are rejected',async()=>{
  const f=fixture(),c=f.controller();await c.start({deviceId:device,orderId:order,operatorPaused:true});
  const original=f.db.rpc;f.db.rpc=async(name,args)=>name==='device_confirmation_view'?{data:{id:barrier,order_id:'50000000-0000-4000-8000-000000000001',state:'ready',frozen_count:8}}:original(name,args);
  await assert.rejects(()=>c.confirm({operatorPaused:true}),/状态无效/);assert.equal(f.counts().commits,0);
  assert.equal((await c.current()).terminal,null);
});

test('a new browser discovers and safely adopts an existing open barrier',async()=>{
  const f=fixture(),c=f.controller();f.open();
  await assert.rejects(()=>c.start({deviceId:device,orderId:order,operatorPaused:true}),/先恢复/);
  assert.equal(f.counts().requests,0);assert.equal(await c.current(),null);
  assert.equal((await c.inspectOpen(device)).id,barrier);
  await assert.rejects(()=>c.recoverOpen({deviceId:device,orderId:order,operatorPaused:false}),/暂停/);
  await assert.rejects(()=>c.recoverOpen({deviceId:device,orderId:'50000000-0000-4000-8000-000000000001',operatorPaused:true}),/其他生产单/);
  const adopted=await c.recoverOpen({deviceId:device,orderId:order,operatorPaused:true});
  assert.equal(adopted.requestId,request);assert.equal(adopted.barrierId,barrier);assert.equal(adopted.commitAttempted,true);
  await assert.rejects(()=>c.recoverOpen({deviceId:device,orderId:order,operatorPaused:true}),/不能覆盖/);
  f.setState('expired');assert.equal((await c.observe()).state,'expired');
  await c.cancel();await c.acknowledge();assert.equal(await c.current(),null);
});
test('invalid open-barrier response cannot be mistaken for an empty state',async()=>{
  const f=fixture(),c=f.controller();f.db.rpc=async()=>({data:undefined});
  await assert.rejects(()=>c.start({deviceId:device,orderId:order,operatorPaused:true}),/状态无效/);
  assert.equal(await c.current(),null);
});

test('a competing phone after preflight cannot erase an uncertain local request',async()=>{
  const f=fixture(),c=f.controller();
  const original=f.db.rpc;
  f.db.rpc=async(name,args)=>name==='device_confirmation_request'?{error:{message:'open barrier conflict'}}:original(name,args);
  await assert.rejects(()=>c.start({deviceId:device,orderId:order,operatorPaused:true}));
  const saved=await c.current();assert.equal(saved.requestId,request);assert.equal(saved.barrierId,null);
  f.open();await assert.rejects(()=>c.recoverOpen({deviceId:device,orderId:order,operatorPaused:true}),/不能覆盖/);
  assert.deepEqual(await c.current(),saved);
});
