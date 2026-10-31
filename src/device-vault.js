// Test-only recovery envelope: encryption happens in the browser, not SQL.
const iterations=600000;
const encoder=new TextEncoder(),decoder=new TextDecoder();
const uuid=s=>typeof s==='string'&&/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(s);
const secretOk=s=>typeof s==='string'&&/^[0-9a-f]{64}$/.test(s);
const encode=bytes=>btoa(String.fromCharCode(...bytes));
function decode(s,length){if(typeof s!=='string'||!/^[A-Za-z0-9+/]+={0,2}$/.test(s))throw Error('设备密钥备份格式无效。');const raw=atob(s);if(raw.length!==length||btoa(raw)!==s)throw Error('设备密钥备份格式无效。');return Uint8Array.from(raw,c=>c.charCodeAt(0));}
async function keyFor(password,salt){if(typeof password!=='string'||!password)throw Error('请输入管理员密码。');const material=await crypto.subtle.importKey('raw',encoder.encode(password),'PBKDF2',false,['deriveKey']);return crypto.subtle.deriveKey({name:'PBKDF2',hash:'SHA-256',salt,iterations},material,{name:'AES-GCM',length:256},false,['encrypt','decrypt']);}
export async function encryptDeviceSecret(id,secret,password){
 if(!uuid(id)||!secretOk(secret))throw Error('设备 ID 或密钥格式无效。');
 const salt=crypto.getRandomValues(new Uint8Array(16)),iv=crypto.getRandomValues(new Uint8Array(12));
 const cipher=await crypto.subtle.encrypt({name:'AES-GCM',iv,additionalData:encoder.encode('xingyi-device-vault-v1|'+id)},await keyFor(password,salt),encoder.encode(secret));
 return {version:1,salt:encode(salt),iv:encode(iv),ciphertext:encode(new Uint8Array(cipher))};
}
export async function decryptDeviceSecret(id,envelope,password){
 if(!uuid(id)||!envelope||envelope.version!==1)throw Error('设备密钥备份格式无效。');
 const salt=decode(envelope.salt,16),iv=decode(envelope.iv,12),cipher=decode(envelope.ciphertext,80);
 let plaintext;try{plaintext=await crypto.subtle.decrypt({name:'AES-GCM',iv,additionalData:encoder.encode('xingyi-device-vault-v1|'+id)},await keyFor(password,salt),cipher);}catch{throw Error('无法解锁：密码可能已更改或备份已损坏。');}
 const secret=decoder.decode(plaintext);if(!secretOk(secret))throw Error('设备密钥备份格式无效。');return secret;
}
export class DeviceVault {
 constructor(db){this.db=db;}
 async save(id,secret,password){
  const envelope=await encryptDeviceSecret(id,secret,password);
  const {data,error}=await this.db.rpc('test_device_vault_save',{p_password:password,p_device:id,p_secret:secret,p_envelope:envelope});
  if(error)throw Error('保存结果未确认，请重新查看；不要更换设备密钥。');
  if(data===null)throw Error('管理员密码错误或暂时锁定。');
  if(data!==true)throw Error('设备密钥不匹配，或该设备已停用。');
 }
 async unlock(id,password){
  if(!uuid(id)||!password)throw Error('请输入管理员密码。');
  const {data,error}=await this.db.rpc('test_device_vault_read',{p_password:password,p_device:id});
  if(error)throw Error('读取失败，请检查网络。');if(data===null)throw Error('管理员密码错误或暂时锁定。');
  if(data.state==='missing')return null;
  if(data.state!=='saved')throw Error('设备密钥备份状态无效。');
  return decryptDeviceSecret(id,data.envelope,password);
 }
}

// A non-secret request receipt is persisted BEFORE sending the mutation. It
// blocks another rotation after a lost response, even across reloads/tabs.
const markerKey=id=>'xingyi-test-rotation-pending:'+id;
export function pendingRotation(id,storage=globalThis.localStorage){
 if(!uuid(id)||!storage)throw Error('本机安全存储不可用，不能更换密钥。');
 let value;try{value=storage.getItem(markerKey(id));}catch{throw Error('本机安全存储不可用，不能更换密钥。');}
 if(value===null)return null;
 if(!uuid(value))throw Error('更换记录异常，不能继续操作。');return value;
}
function clearReceipt(id,request,storage){
 try{if(storage.getItem(markerKey(id))===request)storage.removeItem(markerKey(id));}catch{/* leaving the receipt blocks further mutation */}
}
export async function rotateDeviceSecret(db,id,password,{storage=globalThis.localStorage,locks=globalThis.navigator?.locks}={}) {
 if(!uuid(id)||!password)throw Error('请输入管理员密码。');
 if(!locks?.request)throw Error('浏览器不支持安全的跨标签页锁，不能更换密钥。');
 return locks.request('xingyi-test-rotate:'+id,{mode:'exclusive'},async()=>{
  if(pendingRotation(id,storage))throw Error('上次更换结果未确认，请先核查更换结果。');
  const info=await db.rpc('test_device_rotation_revision',{p_password:password,p_device:id});
  if(info.error)throw Error('未能读取设备状态，尚未更换密钥。');
  if(info.data===null)throw Error('管理员密码错误或暂时锁定，尚未更换密钥。');
  if(info.data?.status!=='ready'||!Number.isSafeInteger(info.data.revision)||info.data.revision<0)throw Error('设备不存在或状态无效，尚未更换密钥。');
  const secret=Array.from(crypto.getRandomValues(new Uint8Array(32)),b=>b.toString(16).padStart(2,'0')).join('');
  const envelope=await encryptDeviceSecret(id,secret,password);
  if(await decryptDeviceSecret(id,envelope,password)!==secret)throw Error('加密验证失败，尚未更换密钥。');
  const {requestId}=await import('./request-id.js');const request=requestId();
  try{storage.setItem(markerKey(id),request);if(storage.getItem(markerKey(id))!==request)throw Error();}catch{throw Error('本机安全存储不可用，尚未更换密钥。');}
  const req={p_password:password,p_device:id,p_secret:secret,p_envelope:envelope,p_revision:info.data.revision,p_request:request};
  let result;try{result=await db.rpc('test_device_rotate',req);}catch{return {status:'unknown'};}
  if(!result||result.error)return {status:'unknown'};
  if(result.data===null){clearReceipt(id,request,storage);return {status:'denied'};}
  if(result.data?.status==='conflict'||result.data?.status==='missing'){clearReceipt(id,request,storage);return {status:'conflict'};}
  if(result.data?.status!=='committed'||result.data.revision!==info.data.revision+1)return {status:'unknown'};
  clearReceipt(id,request,storage);return {status:'committed',secret};
 });
}
export async function resolveDeviceRotation(db,id,password,{storage=globalThis.localStorage,locks=globalThis.navigator?.locks}={}) {
 if(!uuid(id)||!password)throw Error('请输入管理员密码。');
 if(!locks?.request)throw Error('浏览器不支持安全的跨标签页锁，不能核查。');
 return locks.request('xingyi-test-rotate:'+id,{mode:'exclusive'},async()=>{
  const request=pendingRotation(id,storage);if(!request)throw Error('本机没有待核查的更换记录。');
  const {data,error}=await db.rpc('test_device_rotation_result',{p_password:password,p_device:id,p_request:request});
  if(error)throw Error('无法核查，请检查网络。');if(data===null)throw Error('管理员密码错误或暂时锁定。');
  if(data?.status!=='committed')throw Error('更换尚未确认或已被其他操作替代，请勿继续更换或烧录，请联系管理员核查。');
  const secret=await new DeviceVault(db).unlock(id,password);if(!secret)throw Error('未找到密钥备份，请联系管理员核查。');
  clearReceipt(id,request,storage);return secret;
 });
}
