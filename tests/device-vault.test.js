import {test} from 'node:test';import assert from 'node:assert/strict';
import {encryptDeviceSecret,decryptDeviceSecret,DeviceVault} from '../src/device-vault.js';
const id='e0000000-0000-4000-8000-000000000001',secret='a'.repeat(64),password='example-strong-admin';
test('encrypted secret is bound to device, password and ciphertext',async()=>{
 const e=await encryptDeviceSecret(id,secret,password);assert.ok(!JSON.stringify(e).includes(secret));assert.equal(await decryptDeviceSecret(id,e,password),secret);
 await assert.rejects(()=>decryptDeviceSecret(id,e,'wrong'));await assert.rejects(()=>decryptDeviceSecret('e0000000-0000-4000-8000-000000000002',e,password));
 const corrupted={...e,ciphertext:'A'+e.ciphertext.slice(1)};await assert.rejects(()=>decryptDeviceSecret(id,corrupted,password));
 assert.notDeepEqual(e,await encryptDeviceSecret(id,secret,password));
});
test('vault adapter validates results and never exposes saved envelope as plaintext',async()=>{
 let envelope;const db={rpc:async(name,args)=>{if(name==='test_device_vault_save'){envelope=args.p_envelope;return {data:true,error:null}}return {data:{state:'saved',envelope},error:null}}};
 const vault=new DeviceVault(db);await vault.save(id,secret,password);assert.equal(await vault.unlock(id,password),secret);
 db.rpc=async()=>({data:null,error:null});await assert.rejects(()=>vault.unlock(id,password),/密码/);
});
test('rotation generates a fresh credential, encrypts it and never retries uncertain writes',async()=>{
 const {rotateDeviceSecret,pendingRotation,resolveDeviceRotation}=await import('../src/device-vault.js');let args,calls=0;
 const m=new Map(),storage={getItem:k=>m.get(k)??null,setItem:(k,v)=>m.set(k,v),removeItem:k=>m.delete(k)};const locks={request:async(_name,_opts,cb)=>cb()};const options={storage,locks};
 const db={rpc:async(name,p)=>{calls++;if(name==='test_device_rotation_revision')return {data:{status:'ready',revision:7}};args=p;return {data:{status:'committed',revision:8}};}};
 const result=await rotateDeviceSecret(db,id,password,options);assert.equal(result.status,'committed');assert.match(result.secret,/^[0-9a-f]{64}$/);assert.equal(args.p_secret,result.secret);assert.equal(await decryptDeviceSecret(id,args.p_envelope,password),result.secret);assert.equal(calls,2);
 calls=0;db.rpc=async(name)=>{calls++;if(name==='test_device_rotation_revision')return {data:{status:'ready',revision:7}};throw Error('response lost');};
 assert.deepEqual(await rotateDeviceSecret(db,id,password,options),{status:'unknown'});assert.equal(calls,2);assert.ok(pendingRotation(id,storage));await assert.rejects(()=>rotateDeviceSecret(db,id,password,options),/未确认/);
 const saved=await encryptDeviceSecret(id,secret,password);db.rpc=async(name)=>name==='test_device_rotation_result'?{data:{status:'unknown'}}:{data:{state:'saved',envelope:saved}};
 await assert.rejects(()=>resolveDeviceRotation(db,id,password,options),/尚未确认/);assert.ok(pendingRotation(id,storage));
 db.rpc=async(name)=>name==='test_device_rotation_result'?{data:{status:'committed',revision:8}}:{data:{state:'saved',envelope:saved}};
 assert.equal(await resolveDeviceRotation(db,id,password,options),secret);assert.equal(pendingRotation(id,storage),null);
 calls=0;db.rpc=async()=>{calls++;return {data:null}};await assert.rejects(()=>rotateDeviceSecret(db,id,password,options),/密码/);assert.equal(calls,1);
});
