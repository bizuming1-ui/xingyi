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
