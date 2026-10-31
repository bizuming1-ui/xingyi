import React,{useCallback,useEffect,useRef,useState} from 'react';
import {LatencyTracker} from './latency-tracker.js';

// Optional, isolated-site-only; receives no credentials and makes no writes to
// the serial port or database. Raw serial lines are never stored or exported.
export function SerialLatencyPanel({event}) {
 const tracker=useRef(new LatencyTracker()),portRef=useRef(null),readerRef=useRef(null),readDoneRef=useRef(null),generation=useRef(0),raf=useRef(0);
 const [open,setOpen]=useState(false),[connected,setConnected]=useState(false),[error,setError]=useState(''),[version,setVersion]=useState(0),[results,setResults]=useState([]);
 const bump=()=>setVersion(v=>v+1);
 const stop=useCallback(async()=>{
   ++generation.current;const reader=readerRef.current,port=portRef.current;readerRef.current=null;portRef.current=null;
   try {if(reader)await reader.cancel();}catch{};
   try {if(readDoneRef.current)await readDoneRef.current;}catch{};
   try {if(port)await port.close();}catch{};
 },[]);
 useEffect(()=>()=>{cancelAnimationFrame(raf.current);void stop();},[stop]);
 useEffect(()=>{
   if(!connected)return;
   const visibility=()=>{if(document.hidden){tracker.current.fail('网页进入后台，测速无效。');bump();}};
   document.addEventListener('visibilitychange',visibility);return()=>document.removeEventListener('visibilitychange',visibility);
 },[connected]);
 useEffect(()=>{
   if(!connected || !event)return;
   const tr=tracker.current,now=performance.now();
   if(event.type==='snapshot') {
     const value=event.snapshot.draftCount;tr.snapshot(value,now,event.startedAt);bump();
     if(tr.phase==='waiting' && value===tr.pending.baseline+1) {
       cancelAnimationFrame(raf.current);raf.current=requestAnimationFrame(()=>{
         const box=document.querySelector('[aria-label="现在（设备待确认数）"]');
         if(document.hidden||!box||box.value!==String(value)){tr.fail('画面不可见或已变化，测速无效。');bump();return;}
         const result=tr.painted(value,performance.now());if(result)setResults(old=>[...old.slice(-49),result]);bump();
       });
     }
   } else if(event.type==='error') {tr.fail('网页读取失败，测速无效。');bump();}
 },[event,connected]);
 useEffect(()=>{if(!connected)return;const id=setInterval(()=>{const t=tracker.current,phase=t.phase;t.expire(performance.now());if(t.phase!==phase)bump();},500);return()=>clearInterval(id);},[connected]);
 async function connect(){
   if(!navigator.serial){setError('请在电脑 Chrome 或 Edge 中打开，并允许串口权限。');return;}
   setError('');let port;const token=++generation.current;
   try {
     port=await navigator.serial.requestPort();if(token!==generation.current)return;
     await port.open({baudRate:115200});if(token!==generation.current){await port.close();return;}
     portRef.current=port;tracker.current.reset();setConnected(true);
     const decoder=new TextDecoder(),reader=port.readable.getReader();readerRef.current=reader;let buffer='',discard=false;
     let markReadDone;readDoneRef.current=new Promise(resolve=>{markReadDone=resolve;});
     try {
       while(token===generation.current){
         const {value,done}=await reader.read();if(done)break;
         const text=decoder.decode(value,{stream:true});
         // Bound memory even when a disconnected or untrusted serial device
         // sends arbitrarily long lines. Never copy raw text to UI or logs.
         for(const c of text){if(c==='\n'){if(!discard){tracker.current.line(buffer.replace(/\r$/,''),performance.now());bump();}buffer='';discard=false;}else if(!discard){if(buffer.length>=200){buffer='';discard=true;}else buffer+=c;}}
       }
     } finally {try{reader.releaseLock();}catch{}if(readerRef.current===reader)readerRef.current=null;markReadDone();}
     if(token===generation.current){tracker.current.fail('串口断开，测速无效。');setConnected(false);portRef.current=null;try{await port.close();}catch{};bump();}
   } catch {
     if(port){if(portRef.current===port)portRef.current=null;try{await port.close();}catch{}};
     if(token===generation.current){setConnected(false);setError('串口连接失败：请关闭 VS Code 串口监视器后重试。');}
   }
 }
 async function disconnect(){await stop();setConnected(false);tracker.current.reset();bump();}
 function restart(){tracker.current.reset();setError('');bump();}
 function download(){const body=JSON.stringify({version:1,description:'串口行到网页首帧近似时间；不代表精确按键到显示时间',samples:results},null,2);const a=document.createElement('a');const url=URL.createObjectURL(new Blob([body],{type:'application/json'}));a.href=url;a.download='xingyi-latency-log.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
 const tr=tracker.current;void version;
 return <div className="latency-root">
  <button type="button" className="text-btn latency-toggle" onClick={()=>{if(open)void disconnect();setOpen(v=>!v);}}>{open?'收起测速':'显示测速（测试）'}</button>
  {open&&<section className="latency-panel" aria-label="设备显示测速">
    <strong>设备显示测速 · 仅测试</strong>
    <p>电脑 Chrome/Edge；先关闭 VS Code 串口监视器。连接可能重启设备。</p>
    {!connected?<button type="button" onClick={connect}>连接 COM21</button>:<button type="button" onClick={disconnect}>断开串口</button>}
    {connected&&<>
     <p role="status">{tr.reason}</p>
     {tr.phase==='calibrate'&&<button type="button" disabled={!tr.canArm(performance.now())} onClick={()=>{tr.arm(performance.now());bump();}}>准备测试一次</button>}
     {tr.phase==='done'&&<p>串口收到计数到网页首帧约 <b>{tr.result.milliseconds} 毫秒</b>（非精确按键到显示时间）。</p>}
     {['done','invalid'].includes(tr.phase)&&<button type="button" onClick={restart}>重新校准</button>}
     <p>仅限单设备、单订单、一次短按；不要确认或让其他人操作。否则结果可能无效。</p>
     <button type="button" disabled={!results.length} onClick={download}>下载测速结果</button>
    </>}
    {error&&<p role="alert">{error}</p>}
  </section>}
 </div>;
}
