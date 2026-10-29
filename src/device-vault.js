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
