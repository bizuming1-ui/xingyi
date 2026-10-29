import React,{useMemo,useState} from 'react';
import {DeviceVault} from './device-vault.js';

// Never persist or log plaintext, credentials, or admin password in the browser.
export function DeviceVaultPanel({db,deviceId,onBack}) {
 const vault=useMemo(()=>new DeviceVault(db),[db]);
 const [mode,setMode]=useState('menu'),[admin,setAdmin]=useState(''),[input,setInput]=useState(''),[secret,setSecret]=useState(null),[shown,setShown]=useState(false),[busy,setBusy]=useState(false),[message,setMessage]=useState('');
 function choose(next){setAdmin('');setInput('');setSecret(null);setShown(false);setMessage('');setMode(next)}
 async function submit(e){
  e.preventDefault();if(busy)return;const password=admin,current=input;setAdmin('');setInput('');setMessage('');setBusy(true);
  try{
   if(mode==='save'){
    await vault.save(deviceId,current,password);
    setMode('menu');setMessage('已加密保存。请用“找回密钥”检查备份是否能解锁。');
   } else {
    const value=await vault.unlock(deviceId,password);
    if(value===null){setMessage('尚未保存密钥。请返回并选择“保存现有密钥”。');}
    else {setSecret(value);setShown(false);setMode('result');}
   }
  }catch(e){setMessage(e.message||'操作失败，请检查后重试。');}
  finally{setBusy(false);}
 }
 async function copy(value){try{await navigator.clipboard.writeText(value);setMessage('已复制，请勿发送给他人。');}catch{setMessage('复制失败，请手动选择复制。');}}
 return <section className="panel device-panel vault-panel" aria-label="设备凭据">
  <button className="back" disabled={busy} onClick={mode==='menu'?onBack:()=>choose('menu')}>← 返回{mode==='menu'?'设备计数':'设备凭据'}</button>
  <h1>设备凭据 <small>隔离测试</small></h1>
  <div className="vault-content">
   <label className="vault-field">设备 ID<input aria-label="设备 ID" readOnly value={deviceId}/></label>
   {mode==='menu'&&<><button onClick={()=>copy(deviceId)}>复制设备 ID</button><div className="vault-buttons"><button onClick={()=>choose('unlock')}>找回密钥</button><button onClick={()=>choose('save')}>保存现有密钥</button></div><p>密钥加密后保存到测试数据库，使用现有管理员密码解锁。首次使用需从本机配置导入。</p></>}
   {(mode==='save'||mode==='unlock')&&<form onSubmit={submit}>
    {mode==='save'&&<label className="vault-field">现有设备密钥<input type="password" autoComplete="off" spellCheck="false" required value={input} onChange={e=>setInput(e.target.value)} /></label>}
    <label className="vault-field">管理员密码<input type="password" autoComplete="off" required value={admin} onChange={e=>setAdmin(e.target.value)} /></label>
    <button disabled={busy}>{busy?'处理中…':mode==='save'?'加密并保存':'解锁'}</button>
   </form>}
   {mode==='result'&&<><label className="vault-field">设备密钥<input aria-label="设备密钥" readOnly type={shown?'text':'password'} value={secret||''}/></label><div className="vault-buttons"><button onClick={()=>setShown(!shown)}>{shown?'隐藏密钥':'显示密钥'}</button><button onClick={()=>copy(secret)}>复制密钥</button></div></>}
   {message&&<p role="status" className="vault-message">{message}</p>}
  </div>
  <p className="device-note">仅供测试。密钥曾泄露时必须更换；加密备份不能撤销泄露。请勿截图或发送凭据。管理员密码重置后旧备份可能无法解锁。</p>
 </section>;
}
