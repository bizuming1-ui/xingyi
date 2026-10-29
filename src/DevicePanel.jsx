import React, {useEffect, useMemo, useRef, useState} from 'react';
import {DeviceData,DeviceMonitor} from './device-data.js';

// Explicitly experimental: confirmation is intentionally disabled until the
// firmware, server migration and full device/browser handshake are integrated.
export function DevicePanel({db,order,onBack,onConfirm,confirmPreview=false}) {
  const data=useMemo(()=>new DeviceData(db),[db]);
  const [view,setView]=useState({type:'loading'}),[message,setMessage]=useState(''),[busy,setBusy]=useState(false);
  const generation=useRef(0);
  useEffect(()=>{
    ++generation.current;setView({type:'loading'});setMessage('');setBusy(false);
    const monitor=new DeviceMonitor(data);
    monitor.start(order.id,event=>setView(event));
    return ()=>{++generation.current;monitor.stop();};
  },[data,order.id]);
  const snapshot=view.type==='snapshot'?view.snapshot:null;
  const device=snapshot?.device;
  const desired=device?.desiredOrderId===order.id;
  async function bind() {
    if(busy||!device||desired)return;
    if(!window.confirm('确认请求将设备绑定到此生产单？离线计数仍归原生产单，设备联网后会先处理旧计数。'))return;
    const current=generation.current;setBusy(true);setMessage('');
    try {
      await data.requestBinding(device.id,order.id,true);
      if(generation.current===current)setMessage('绑定请求已发送；设备可能尚未完成切换。');
    } catch {
      if(generation.current===current)setMessage('绑定请求未确认，请刷新设备状态后重试。');
    } finally {if(generation.current===current)setBusy(false);}
  }
  return <section className="panel device-panel" aria-label="设备计数预览">
    <button type="button" className="back" disabled={busy} onClick={onBack}>← 返回生产单</button>
    <h1>设备计数 <small>测试预览</small></h1>
    <p className="device-order">生产单：<strong title={order.order_no}>{order.order_no}</strong></p>
    <div className="device-summary" aria-live="polite">
      {view.type==='loading'&&<p role="status">正在读取设备数据…</p>}
      {view.type==='error'&&<p role="alert">{view.message}</p>}
      {snapshot&&<>
        <div className="device-count"><span>设备待确认数</span><strong>{snapshot.draftCount}</strong></div>
        <p className="device-label">{device?<>设备：<strong title={device.label}>{device.label}</strong></>:'暂无已配置设备'}</p>
        {device&&<p>{desired?'已请求绑定此单':'尚未请求绑定此单'}，不代表设备已完成切换。</p>}
      </>}
    </div>
    <div className="device-actions">
      <button type="button" disabled={busy||!device||desired} onClick={bind}>请求绑定到此单</button>
      {confirmPreview?<button type="button" disabled={busy||!device} onClick={()=>onConfirm(device.id)}>设备确认（测试）</button>:<button type="button" disabled title="完整设备确认尚未接入">设备确认（未启用）</button>}
    </div>
    <p className="device-note">此为隔离测试预览，不能用于生产。设备是否在线及是否全部同步，不能仅凭此页面判断。</p>
    {message&&<p className="device-message" role="status">{message}</p>}
  </section>;
}
