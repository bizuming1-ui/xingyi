import React, {useMemo,useRef,useState} from 'react';
import {DeviceData} from './device-data.js';

// Test preview only. The secret remains in component memory: never storage,
// telemetry, URL or console. A failed request can still have created a device.
export function DeviceProvisionPanel({db,onBack}) {
 const data=useMemo(()=>new DeviceData(db),[db]);
 const [admin,setAdmin]=useState(''),[label,setLabel]=useState('ESP32-S3'),[result,setResult]=useState(null);
 const [busy,setBusy]=useState(false),[uncertain,setUncertain]=useState(false),[message,setMessage]=useState('');
 const inFlight=useRef(false);
 async function submit(e){
  e.preventDefault();if(inFlight.current || uncertain)return;
  inFlight.current=true;setBusy(true);setMessage('');const password=admin;setAdmin('');
  try {setResult(await data.provision(password,label));}
  catch(error) {const text=error?.message||'创建设备结果未知；不要重复创建，请先核对设备列表。';setMessage(text);if(!text.includes('未创建设备') && !text.includes('请输入管理员密码'))setUncertain(true);}
  finally {inFlight.current=false;setBusy(false);}
 }
 function leave(){
  if(result && !window.confirm('密钥只显示这一次。确认已在自己的电脑安全保存？'))return;
  if(uncertain && !window.confirm('创建结果未知。请先核对设备列表，不要盲目重试。确认返回？'))return;
  setAdmin('');setResult(null);onBack();
 }
 return <section className="panel device-panel provision-panel" aria-label="创建设备测试预览">
  <button type="button" className="back" disabled={busy} onClick={leave}>← 返回设备计数</button>
  <h1>创建设备 <small>测试预览</small></h1>
  {!result?<form onSubmit={submit} className="device-summary provision-form">
   <p>设备密钥仅显示一次，请在本人电脑上安全保存，不要发送给他人。</p>
   <label>设备名称<input value={label} onChange={e=>setLabel(e.target.value)} maxLength="100" disabled={busy||uncertain} required/></label>
   <label>管理员密码<input type="password" autoComplete="off" value={admin} onChange={e=>setAdmin(e.target.value)} disabled={busy||uncertain} required/></label>
   {message&&<p role="alert">{message}</p>}
   <button disabled={busy||uncertain}>{busy?'创建中…':'创建设备'}</button>
  </form>:<div className="device-summary provision-result">
   <p>请复制到自己电脑的固件配置。关闭页面后无法再次查看密钥。</p>
   <label>设备 ID<input readOnly value={result.id} onFocus={e=>e.target.select()}/></label>
   <label>设备密钥<input readOnly value={result.secret} onFocus={e=>e.target.select()}/></label>
  </div>}
  <p className="device-note">仅限隔离测试，不能用于生产。切勿将密钥、热点或管理员密码发送到聊天。</p>
 </section>;
}
