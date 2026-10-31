import React, {useEffect, useMemo, useRef, useState} from 'react';
import {DeviceConfirmation} from './device-confirmation.js';

// Test-only UI. The server remains authoritative for synchronization/lease;
// nothing in this component should be interpreted as proof of connectivity.
export function DeviceConfirmationPanel({db,deviceId,order,onBack,onDone}) {
  const controller=useMemo(()=>new DeviceConfirmation({db,storage:window.localStorage}),[db]);
  const [state,setState]=useState({kind:'loading'}),[busy,setBusy]=useState(false),[message,setMessage]=useState('');
  const live=useRef(false),sequence=useRef(0),busyRef=useRef(false);
  async function readState() {
    const record=await controller.current();
    if(record) {
      if(record.deviceId!==deviceId || record.orderId!==order.id)return {kind:'other'};
      if(record.terminal)return {kind:record.terminal.state,result:record.terminal.result};
      if(!record.barrierId)return {kind:'resume'};
      const view=await controller.observe();
      return {kind:view.state,result:view.result,count:view.frozen_count};
    }
    const open=await controller.inspectOpen(deviceId);
    if(!open)return {kind:'new'};
    return open.order_id===order.id?{kind:'recover'}:{kind:'other'};
  }
  useEffect(()=>{
    live.current=true;let timer,cancelled=false;
    const poll=async()=>{
      const version=sequence.current;
      try {const next=await readState();if(!cancelled && live.current && !busyRef.current && sequence.current===version)setState(next);}
      catch(error) {if(!cancelled && live.current && !busyRef.current && sequence.current===version){setState({kind:'error'});if(error?.message?.includes('不支持安全的多标签页'))setMessage(error.message);}}
      if(!cancelled)timer=window.setTimeout(poll,600);
    };
    void poll();
    return ()=>{cancelled=true;live.current=false;++sequence.current;window.clearTimeout(timer);};
  },[controller,deviceId,order.id]);
  async function perform(action) {
    if(busyRef.current)return;
    busyRef.current=true;setBusy(true);setMessage('');++sequence.current;
    try {await action();const next=await readState();if(live.current)setState(next);}
    catch(error) {
      if(live.current){setState({kind:'error'});setMessage(error?.message||'操作未确认，请检查状态。');}
    } finally {busyRef.current=false;if(live.current)setBusy(false);}
  }
  function pausePrompt(){return window.confirm('请提醒操作员暂停按键，并在确认完成或取消前保持暂停。是否已经暂停？');}
  function begin(){if(pausePrompt())void perform(()=>controller.start({deviceId,orderId:order.id,operatorPaused:true}));}
  function recover(){if(pausePrompt())void perform(()=>controller.recoverOpen({deviceId,orderId:order.id,operatorPaused:true}));}
  function confirm(){if(pausePrompt())void perform(()=>controller.confirm({operatorPaused:true}));}
  function cancel(){if(window.confirm('确认取消本次设备确认？计数会保留。'))void perform(()=>controller.cancel());}
  function acknowledge(){void perform(async()=>{await controller.acknowledge();onDone();});}
  const labels={loading:'正在检查设备确认状态…',new:'请暂停按键；若上次结果未知，请先核对生产历史。',resume:'上次请求结果未知，请暂停按键后重试原请求。',recover:'发现未结束请求，请暂停按键后恢复。',other:'另一生产单有未处理的确认，请先返回原生产单处理。',requested:'等待设备冻结并同步，请保持暂停。',ready:'设备报告已冻结；请继续保持暂停。',expired:'设备续约已过期，请取消后重新发起。',cancelled:'本次确认已取消，设备计数保留。',completed:'设备确认已完成。',error:'状态未确认，请等待重试；不要重复新建。'};
  return <section className="panel device-panel confirmation-panel" aria-label="设备确认测试预览">
    <button type="button" className="back" disabled={busy} onClick={onBack}>← 返回设备计数</button>
    <h1>设备确认 <small>测试预览</small></h1>
    <p className="device-order">生产单：<strong title={order.order_no}>{order.order_no}</strong></p>
    <div className="device-summary" aria-live="polite">
      <p role={state.kind==='error'?'alert':'status'}>{labels[state.kind]||'设备状态异常，请勿确认。'}</p>
      {state.kind==='ready'&&<div className="device-count"><span>冻结计数</span><strong>{state.count}</strong></div>}
      {state.kind==='completed'&&state.result&&<p>请求 {state.result.requested}，实际累计 {state.result.applied}，当前 {state.result.current}。</p>}
      {message&&<p className="device-message">{message}</p>}
    </div>
    <div className="device-actions">
      {['new','resume'].includes(state.kind)&&<button type="button" disabled={busy} onClick={begin}>{state.kind==='resume'?'重试原请求':'开始设备确认'}</button>}
      {state.kind==='recover'&&<button type="button" disabled={busy} onClick={recover}>恢复未结束请求</button>}
      {state.kind==='ready'&&<button type="button" disabled={busy} onClick={confirm}>确认设备计数</button>}
      {['requested','ready','expired'].includes(state.kind)&&<button type="button" disabled={busy} onClick={cancel}>取消确认</button>}
      {['completed','cancelled'].includes(state.kind)&&<button type="button" disabled={busy} onClick={acknowledge}>知道了</button>}
    </div>
    <p className="device-note">仅限隔离测试，不能用于生产。短暂断网不能立即识别；确认由服务器重新检查。</p>
  </section>;
}
