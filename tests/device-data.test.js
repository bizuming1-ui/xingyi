import {test} from 'node:test';
import assert from 'node:assert/strict';
import {DeviceData,DeviceDraftData,DeviceMonitor} from '../src/device-data.js';
const order='00000000-0000-4000-8000-000000000001',other='00000000-0000-4000-8000-000000000002';
const device='10000000-0000-4000-8000-000000000001',binding='20000000-0000-4000-8000-000000000001';
function fixture() {
  const state={devices:[{id:device,label:'设备一',desired_binding_id:binding,desired_order_id:order,desired_binding_revision:1}],draft:{order_id:order,count:5},listError:null,draftError:null,bindError:null,bindId:binding,calls:[]};
  const db={rpc:async(name,args)=>{state.calls.push([name,args]);if(name==='device_list')return {data:state.devices,error:state.listError};if(name==='device_request_binding')return {data:state.bindId,error:state.bindError};throw Error(name);},
    from(table){assert.equal(table,'device_order_drafts');return {select(fields){assert.equal(fields,'order_id,count');return {eq(key,value){assert.equal(key,'order_id');assert.equal(value,order);return {maybeSingle:async()=>({data:state.draft,error:state.draftError})};}};}};}};
  return {state,db,data:new DeviceData(db)};
}
test('server draft and requested binding are validated independently of online state',async()=>{
  const {state,data}=fixture();assert.deepEqual(await data.load(order),{orderId:order,device:{id:device,label:'设备一',desiredBindingId:binding,desiredOrderId:order,desiredRevision:1},draftCount:5});
  state.draft=null;assert.equal((await data.load(order)).draftCount,0);
  state.devices=[];assert.equal((await data.load(order)).device,null);
});
test('failure or malformed data is not silently converted to zero',async()=>{
  const {state,data}=fixture();state.draftError={message:'network'};await assert.rejects(()=>data.load(order),/刷新失败/);
  state.draftError=null;state.draft=undefined;await assert.rejects(()=>data.load(order),/计数数据无效/);
  state.draft={order_id:order,count:9007199254740992};await assert.rejects(()=>data.load(order),/计数数据无效/);
  state.draft={order_id:other,count:2};await assert.rejects(()=>data.load(order),/计数数据无效/);
  state.draft=null;state.devices[0].desired_binding_revision=null;await assert.rejects(()=>data.load(order),/绑定状态无效/);
  state.devices=[state.devices[0],state.devices[0]];await assert.rejects(()=>data.load(order),/设备列表数据无效/);
});
test('order draft-only polling avoids device list and rejects stale or malformed values',async()=>{
 const {state,db}=fixture();const data=new DeviceDraftData(db);
 assert.deepEqual(await data.load(order),{orderId:order,draftCount:5});assert.equal(state.calls.length,0);
 state.draft=null;assert.equal((await data.load(order)).draftCount,0);
 state.draft={order_id:other,count:5};await assert.rejects(()=>data.load(order),/刷新失败/);
 state.draft={order_id:order,count:-1};await assert.rejects(()=>data.load(order),/刷新失败/);
 state.draftError={message:'offline'};await assert.rejects(()=>data.load(order),/刷新失败/);
});
test('binding requires explicit confirmation and returns only a desired binding',async()=>{
  const {state,data}=fixture();await assert.rejects(()=>data.requestBinding(device,order,false),/确认/);assert.equal(state.calls.length,0);
  assert.deepEqual(await data.requestBinding(device,order,true),{requestedBindingId:binding});
  assert.deepEqual(state.calls[0][1],{p_device:device,p_order:order});
  state.bindError={message:'timeout'};await assert.rejects(()=>data.requestBinding(device,order,true),/未确认/);
  state.bindError=null;state.bindId='bad';await assert.rejects(()=>data.requestBinding(device,order,true),/回复无效/);
});
function deferred(){let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no});return {promise,resolve,reject};}
const flush=()=>new Promise(resolve=>setImmediate(resolve));
test('monitor ignores old replies after switching or stopping',async()=>{
  const first=deferred(),second=deferred(),updates=[],scheduled=[];
  let calls=0;const data={load:id=>{calls++;return id===order?first.promise:second.promise;}};
  const m=new DeviceMonitor(data,{setTimer:fn=>{scheduled.push(fn);return scheduled.length;},clearTimer:()=>{}});
  m.start(order,event=>updates.push(event));m.start(other,event=>updates.push(event));
  first.resolve({orderId:order,draftCount:1});await flush();assert.equal(updates.length,0);
  second.resolve({orderId:other,draftCount:2});await flush();assert.equal(updates.length,1);assert.equal(updates[0].snapshot.orderId,other);
  assert.equal(scheduled.length,1);m.stop();
  // Even a stale timer callback that was already queued cannot notify.
  scheduled[0]();await flush();assert.equal(updates.length,1);assert.equal(calls,2);
});
test('monitor reports failures and only retries after the prior read completes',async()=>{
  const pending=deferred(),updates=[],scheduled=[];let calls=0;
  const m=new DeviceMonitor({load(){calls++;return pending.promise;}},{setTimer:fn=>{scheduled.push(fn);return 1;},clearTimer:()=>{}});
  m.start(order,event=>updates.push(event));await flush();assert.equal(calls,1);assert.equal(scheduled.length,0);
  pending.reject(new Error('network'));await flush();assert.equal(updates[0].type,'error');assert.equal(scheduled.length,1);
  m.stop();assert.throws(()=>new DeviceMonitor({}, {interval:0}),/刷新间隔/);
});

test('fast draft polling remains serialized and backs off on errors',async()=>{
  const pending=deferred(),timers=[],updates=[];let calls=0;
  const monitor=new DeviceMonitor({load(){calls++;return calls===1?pending.promise:Promise.reject(new Error('offline'));}},
    {interval:250,setTimer:(fn,ms)=>{timers.push({fn,ms});return timers.length;},clearTimer:()=>{}});
  monitor.start(order,e=>updates.push(e));await flush();assert.equal(calls,1);assert.equal(timers.length,0);
  pending.resolve({orderId:order,draftCount:3});await flush();assert.equal(timers[0].ms,250);
  timers[0].fn();await flush();assert.equal(calls,2);assert.equal(updates[1].type,'error');assert.equal(timers[1].ms,1000);
  monitor.stop();timers[1].fn();await flush();assert.equal(calls,2);
});

test('a rejected read waits for the other read before failing',async()=>{
  const wait=deferred();let done=false;
  const db={rpc:()=>Promise.reject(new Error('offline')),from:()=>({select:()=>({eq:()=>({maybeSingle:()=>wait.promise})})})};
  const request=new DeviceData(db).load(order).catch(()=>{done=true;});
  await flush();assert.equal(done,false);wait.resolve({data:null,error:null});await request;assert.equal(done,true);
});
test('provisioning returns one-time credentials without retrying uncertain requests',async()=>{
 const id=device,secret='a'.repeat(64);let calls=0;
 const db={rpc:async(name,args)=>{calls++;assert.equal(name,'device_create');assert.deepEqual(args,{p_admin_password:'fixture',p_label:'测试'});return {data:{id,secret},error:null}}};
 const data=new DeviceData(db);assert.deepEqual(await data.provision('fixture',' 测试 '),{id,secret});assert.equal(calls,1);
 db.rpc=async()=>{calls++;throw Error('response lost')};await assert.rejects(()=>data.provision('fixture','测试'),/结果未知/);assert.equal(calls,2);
 db.rpc=async()=>({data:null,error:null});await assert.rejects(()=>data.provision('fixture','测试'),/未创建设备/);
 db.rpc=async()=>({data:{id,secret:'not-a-secret'},error:null});await assert.rejects(()=>data.provision('fixture','测试'),/回复无效/);
});

test('realtime hints coalesce and re-read after an in-flight request without trusting events',async()=>{
 const a=deferred(),b=deferred(),updates=[],timers=[];let calls=0;
 const m=new DeviceMonitor({load(){return ++calls===1?a.promise:b.promise;}},{interval:250,setTimer:(fn,ms)=>{timers.push({fn,ms});return timers.length;},clearTimer:()=>{}});
 m.start(order,e=>updates.push(e));m.refreshSoon();m.refreshSoon();assert.equal(calls,1);
 a.resolve({orderId:order,draftCount:1});await flush();assert.equal(timers[0].ms,100);assert.equal(calls,1);
 timers[0].fn();await flush();assert.equal(calls,2);
 b.resolve({orderId:order,draftCount:2});await flush();assert.equal(updates[1].snapshot.draftCount,2);
 m.refreshSoon();const kick=timers.at(-1);assert.equal(kick.ms,100);m.refreshSoon();assert.equal(timers.at(-1),kick);
 m.stop();kick.fn();await flush();assert.equal(calls,2);
});

test('realtime hints do not defeat error backoff or execute cancelled timer callbacks',async()=>{
 const timers=[];let calls=0;const pending=deferred();
 const m=new DeviceMonitor({load(){calls++;return calls===1?Promise.resolve({orderId:order,draftCount:0}):pending.promise;}},{interval:250,setTimer:(fn,ms)=>{timers.push({fn,ms});return timers.length;},clearTimer:()=>{}});
 m.start(order,()=>{});await flush();const stale=timers[0];m.refreshSoon();stale.fn();await flush();assert.equal(calls,1);
 timers.at(-1).fn();assert.equal(calls,2);m.refreshSoon();pending.reject(Error('offline'));await flush();
 assert.equal(timers.at(-1).ms,1000);const count=timers.length;m.refreshSoon();assert.equal(timers.length,count);m.stop();
});
