import React, {useEffect, useState, useCallback} from 'react';
import {createRoot} from 'react-dom/client';
import {createClient} from '@supabase/supabase-js';
import QRCode from 'qrcode';
import {loginErrorMessage} from './auth-errors.js';
import {requestId} from './request-id.js';
import {publicAppUrl} from './public-url.js';
import {DevicePanel} from './DevicePanel.jsx';
import {DeviceDraftData,DeviceMonitor} from './device-data.js';
import {DeviceConfirmationPanel} from './DeviceConfirmationPanel.jsx';
import {DeviceProvisionPanel} from './DeviceProvisionPanel.jsx';
import {DeviceVaultPanel} from './DeviceVaultPanel.jsx';
import {TestSetup} from './TestSetup.jsx';
import './style.css';

const url=import.meta.env.VITE_SUPABASE_URL, key=import.meta.env.VITE_SUPABASE_ANON_KEY;
const isolatedTest=import.meta.env.VITE_ISOLATED_DEVICE_TEST==='true';
const devicePreview=import.meta.env.VITE_DEVICE_PREVIEW==='true';
const deviceConfirmPreview=devicePreview && import.meta.env.VITE_DEVICE_CONFIRM_PREVIEW==='true';
const db=url && key ? createClient(url,key) : null;
const whole = value => /^\d+$/.test(String(value)) && Number.isSafeInteger(Number(value));
function Pager({page, total, onChange}){
 const pages=Math.max(1,Math.ceil(total/3)), current=Math.min(page,pages-1);
 if(pages<=1)return null;
 return <nav className="pager" aria-label="翻页"><button type="button" disabled={current===0} onClick={()=>onChange(current-1)}>上一页</button><span>{current+1} / {pages}</span><button type="button" disabled={current>=pages-1} onClick={()=>onChange(current+1)}>下一页</button></nav>;
}
function App(){
 const [session,setSession]=useState(null),[email,setEmail]=useState(''),[password,setPassword]=useState('');
 const [orders,setOrders]=useState([]),[selected,setSelected]=useState(()=>{try{return JSON.parse(localStorage.getItem('xingyi_pending'))?.order||null}catch{return null}}),[amount,setAmount]=useState(()=>{try{return String(JSON.parse(localStorage.getItem('xingyi_pending'))?.amount||'')}catch{return ''}});
 const [page,setPage]=useState(()=>localStorage.getItem('xingyi_pending')?'order':'list'),[form,setForm]=useState({}),[admin,setAdmin]=useState(''),[history,setHistory]=useState(null);
 const [deviceId,setDeviceId]=useState(null);
 const [orderDeviceView,setOrderDeviceView]=useState({orderId:null,type:'loading'});
 const [message,setMessage]=useState(''),[busy,setBusy]=useState(false),[qr,setQr]=useState(''),[pending,setPending]=useState(()=>{try{return JSON.parse(localStorage.getItem('xingyi_pending'))}catch{return null}});
 const [listPage,setListPage]=useState(0),[archivePage,setArchivePage]=useState(0),[historyPage,setHistoryPage]=useState(0);
 const [sync,setSync]=useState('连接中'),[access,setAccess]=useState({userId:null,status:'checking'}),[accessRetry,setAccessRetry]=useState(0);
 const order=orders.find(o=>o.id===selected);
 const activeOrders=orders.filter(o=>!o.archived);
 const listIndex=Math.min(listPage,Math.max(0,Math.ceil(activeOrders.length/3)-1));
 const archiveIndex=Math.min(archivePage,Math.max(0,Math.ceil(orders.length/3)-1));
 const historyIndex=Math.min(historyPage,Math.max(0,Math.ceil((history?.items.length||0)/3)-1));
 const refresh=useCallback(async()=>{if(!db)return; const {data,error}=await db.from('orders').select('*').order('created_at',{ascending:false}); if(error){setSync('同步失败');return;} setOrders(data); setSync('已同步');},[]);
 useEffect(()=>{if(!db)return; db.auth.getSession().then(({data})=>setSession(data.session)); const {data:{subscription}}=db.auth.onAuthStateChange((_e,s)=>setSession(s)); return()=>subscription.unsubscribe();},[]);
 useEffect(()=>{if(!session?.user?.id)return; const userId=session.user.id; let active=true; setAccess({userId,status:'checking'}); db.rpc('is_operator').then(({data,error})=>{if(active)setAccess({userId,status:error?'error':data===true?'allowed':'denied'})}).catch(()=>{if(active)setAccess({userId,status:'error'})}); return()=>{active=false}},[session?.user?.id,accessRetry]);
 useEffect(()=>{if(!session || access.userId!==session.user.id || access.status!=='allowed')return; refresh(); const channel=db.channel('orders-live').on('postgres_changes',{event:'*',schema:'public',table:'orders'},refresh).subscribe(status=>{if(status==='SUBSCRIBED') {setSync('实时连接');refresh();} else if(status==='CHANNEL_ERROR'||status==='TIMED_OUT')setSync('连接中断，定时重试');}); const timer=setInterval(refresh,5000); const visible=()=>{if(!document.hidden)refresh()}; document.addEventListener('visibilitychange',visible); return()=>{clearInterval(timer);document.removeEventListener('visibilitychange',visible);db.removeChannel(channel)}},[session,access,refresh]);
 useEffect(()=>{
   if(!isolatedTest || !devicePreview || !session || access.userId!==session.user.id || access.status!=='allowed' || page!=='order' || !selected)return;
   const orderId=selected;setOrderDeviceView({orderId,type:'loading'});
   const monitor=new DeviceMonitor(new DeviceDraftData(db));
   monitor.start(orderId,event=>setOrderDeviceView({orderId,...event}));
   return ()=>monitor.stop();
 },[session,access,page,selected]);
 useEffect(()=>{ // Only permit document scrolling while the on-screen keyboard is open.
   const viewport=window.visualViewport; let baseline=viewport?.height||window.innerHeight;
   const update=()=>{const focused=/^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement?.tagName||''); const height=viewport?.height||window.innerHeight; const keyboard=focused && baseline-height>100; document.documentElement.classList.toggle('keyboard-open',keyboard); if(keyboard) setTimeout(()=>document.activeElement?.scrollIntoView({block:'nearest'}),80); if(!focused)baseline=height;};
   const focus=()=>{if(!document.documentElement.classList.contains('keyboard-open')) baseline=viewport?.height||window.innerHeight; setTimeout(update,300)};
   const blur=()=>setTimeout(update,150);
   viewport?.addEventListener('resize',update); window.addEventListener('resize',update); document.addEventListener('focusin',focus); document.addEventListener('focusout',blur);
   return()=>{viewport?.removeEventListener('resize',update);window.removeEventListener('resize',update);document.removeEventListener('focusin',focus);document.removeEventListener('focusout',blur);document.documentElement.classList.remove('keyboard-open')};
 },[]);
 const qrUrl=publicAppUrl(window.location,import.meta.env.BASE_URL,import.meta.env.VITE_PUBLIC_APP_URL);
 useEffect(()=>{if(isolatedTest && !import.meta.env.VITE_PUBLIC_APP_URL){setQr('');return;}QRCode.toDataURL(qrUrl,{width:240,margin:2,errorCorrectionLevel:'M'}).then(setQr).catch(()=>{});},[qrUrl]);
 async function login(e){e.preventDefault();setBusy(true);setMessage('');try{const {error}=await db.auth.signInWithPassword({email:email.trim(),password});if(error)setMessage(loginErrorMessage(error));}catch(error){setMessage(loginErrorMessage(error));}finally{setPassword('');setBusy(false)}}
 function showList(){if(pending){setMessage('存在未确认的提交，请先重试。');return}setPage('list');setAdmin('');setHistory(null);setMessage('');setPending(null);setAmount('');}
 function openOrder(o){setSelected(o.id);setPage('order');setMessage('');setAmount('');setPending(null)}
 function edit(o){setForm(o?{id:o.id,order_no:o.order_no,product:o.product,total:String(o.total),current:String(o.current_count),expected:o.current_count}:{order_no:'',product:'',total:'',current:'0'});setAdmin('');setPage('edit');setMessage('')}
 async function save(e){e.preventDefault();if(!whole(form.total)||!whole(form.current)||Number(form.current)>Number(form.total)){setMessage('数量必须为非负整数，当前生产数不能超过生产总数。');return} setBusy(true);setMessage('');const {data,error}=await db.rpc('admin_save_order',{p_password:admin,p_id:form.id||null,p_order_no:form.order_no,p_product:form.product,p_total:Number(form.total),p_current:Number(form.current),p_expected_current:form.id?form.expected:null});setBusy(false);if(error||!data){setMessage(error?.message||'管理员密码错误或暂时锁定，请稍后再试');return} await refresh();setSelected(data);setAdmin('');setPage('order');setMessage('保存成功')}
 async function submit(e){e.preventDefault();if(!whole(amount)||Number(amount)<=0){setMessage('请输入大于零的整数。');return} let request=pending; if(!request){try{request={id:requestId(),amount:Number(amount),order:selected}}catch(error){setMessage(error.message);return}}setPending(request);localStorage.setItem('xingyi_pending',JSON.stringify(request));setBusy(true);setMessage('');const {data,error}=await db.rpc('submit_production',{p_order:request.order,p_amount:request.amount,p_request:request.id});setBusy(false);if(error){setMessage('提交未确认：'+error.message+'。请重试同一次提交，避免重复累计。');return} setPending(null);localStorage.removeItem('xingyi_pending');setAmount('');await refresh();setMessage(data.applied<request.amount?`已累计 ${data.applied}，超过生产总数的部分未累计。`:`已累计 ${data.applied}。`)}
 async function archive(){if(!window.confirm('确认归档此生产单？历史记录会保留。'))return;setBusy(true);const {data,error}=await db.rpc('admin_archive_order',{p_password:admin,p_id:selected});setBusy(false);if(error||!data){setMessage(error?.message||'管理员密码错误或暂时锁定，请稍后再试');return}await refresh();showList()}
 async function loadHistory(o){if(!admin){setMessage('请输入管理员密码');return}setBusy(true);const {data,error}=await db.rpc('admin_history',{p_password:admin,p_id:o.id});setBusy(false);if(error||!data){setMessage(error?.message||'管理员密码错误或暂时锁定，请稍后再试');return}setHistory({order:o,items:data});setHistoryPage(0);setPage('history');setMessage('')}
 if(!db)return <main className="center"><h1>尚未配置数据库</h1><p>请按照 README 配置 Supabase 和部署环境变量。</p></main>;
 if(!session)return <main className="center"><form className="panel login" onSubmit={login}><h1>{isolatedTest?'界面1 · 隔离测试':'界面1'}</h1>{isolatedTest&&<p>当前连接独立测试数据库，不会显示或修改正式生产数据。</p>}<p>请使用管理员在 Supabase 中创建的系统共享账号登录。邮箱地址相同，并不代表可以使用 Gmail 等邮箱的密码登录。</p><label>邮箱<input type="email" value={email} onChange={e=>setEmail(e.target.value)} required /></label><label>密码<input type="password" value={password} onChange={e=>setPassword(e.target.value)} required /></label><button disabled={busy}>登录</button><p role="alert">{message}</p><p><a href="https://github.com/bizuming1-ui/xingyi/blob/arena/f7a2470b-xingyi/README.md#当前项目的首次配置" target="_blank" rel="noreferrer">首次使用或登录遇到问题？</a></p></form></main>;
 if(access.userId!==session.user.id || access.status==='checking')return <main className="center"><p role="status">正在验证共享账号权限…</p></main>;
 if(isolatedTest && access.status==='denied')return <TestSetup db={db} onDone={()=>setAccessRetry(x=>x+1)}/>;
 if(access.status!=='allowed')return <main className="center"><section className="panel login"><h1>共享账号尚未就绪</h1><p role="alert">{access.status==='error'?'无法检查账号权限，请检查网络并重试。':'账号已登录，但尚未被绑定为系统共享账号。请管理员按首次配置说明完成绑定。'}</p><p><a href="https://github.com/bizuming1-ui/xingyi/blob/arena/f7a2470b-xingyi/README.md#当前项目的首次配置" target="_blank" rel="noreferrer">查看首次配置说明</a></p><button onClick={()=>setAccessRetry(x=>x+1)}>重新检查</button>{' '}<button onClick={()=>db.auth.signOut()}>退出登录</button></section></main>;
 return <main><header><span>{isolatedTest?'界面1 · 隔离测试':'界面1 · 生产管理'}</span><div><span className="sync">● {sync}</span><button className="link" onClick={()=>db.auth.signOut()}>退出登录</button></div></header>
 {page==='list'&&<section className="list panel"><div className="listhead"><div><h1>生产单</h1><p>选择生产单开始操作</p></div><div className="list-actions"><button onClick={()=>edit(null)}>＋ 新建生产单</button>{qr&&<img className="qr-small" src={qr} alt={isolatedTest?'扫描进入隔离测试列表':'扫描进入正式生产单列表'} title={qrUrl}/>}</div></div><div className="orders">{activeOrders.slice(listIndex*3,listIndex*3+3).map(o=><button className="order-card" key={o.id} onClick={()=>openOrder(o)}><strong title={o.order_no}>{o.order_no}</strong><span title={o.product}>{o.product}</span><span>{o.current_count} / {o.total}</span><b>›</b></button>)}{!activeOrders.length&&<p>暂无生产单，请由管理员新建。</p>}</div><Pager page={listIndex} total={activeOrders.length} onChange={setListPage}/><button className="archive-link" onClick={()=>{setPage('archive');setArchivePage(0);setAdmin('');setMessage('')}}>管理员：查看归档和历史记录 →</button></section>}
 {page==='archive'&&<section className="panel list archive-page"><button className="back" onClick={showList}>← 返回生产单列表</button><h1>归档和历史记录</h1><label className="admin-label">管理员密码<input type="password" value={admin} onChange={e=>setAdmin(e.target.value)} /></label><div className="archive-rows">{orders.slice(archiveIndex*3,archiveIndex*3+3).map(o=><div className="history-row" key={o.id}><span title={o.order_no}>{o.order_no} {o.archived?'（已归档）':''}</span><button disabled={busy} onClick={()=>loadHistory(o)}>查看记录</button></div>)}{!orders.length&&<p>暂无生产单</p>}</div><Pager page={archiveIndex} total={orders.length} onChange={setArchivePage}/></section>}
 {page==='history'&&history&&<section className="panel list history-page"><button className="back" onClick={()=>{setPage('archive');setHistory(null)}}>← 返回归档列表</button><h1 title={history.order.order_no}>{history.order.order_no} 的确认记录</h1><div className="history-items">{history.items.length===0?<p>暂无确认记录</p>:history.items.slice(historyIndex*3,historyIndex*3+3).map((x,i)=><div className="event-row" key={i}><span>{new Date(x.created_at).toLocaleString()}</span><span>输入 {x.requested} · 实际 {x.applied} · 累计 {x.resulting_count}</span></div>)}</div><Pager page={historyIndex} total={history.items.length} onChange={setHistoryPage}/></section>}
 {page==='order'&&order&&<><button className="back" onClick={showList}>← 返回生产单列表</button><section className={isolatedTest&&devicePreview?"canvas test-device-canvas":"canvas"}><div className="qr-box">{qr&&<img src={qr} alt={isolatedTest?'扫描进入隔离测试列表':'扫描进入正式生产单列表'} title={qrUrl}/>}<small>扫码进入列表</small></div><div className="fields"><label className="field first"><span>生产单</span><input value={order.order_no} readOnly /></label><label className="field"><span>产品</span><input value={order.product} readOnly /></label><label className="field"><span>生产总数</span><input value={order.total} readOnly /></label><label className="field"><span>当前生产数</span><input value={order.current_count} readOnly /></label>{isolatedTest&&devicePreview&&<label className="field"><span>现在</span><input aria-label="现在（设备待确认数）" value={orderDeviceView.orderId!==order.id||orderDeviceView.type==='loading'?'读取中':orderDeviceView.type==='snapshot'?String(orderDeviceView.snapshot.draftCount):'刷新失败'} readOnly /></label>}<form id="submit" onSubmit={submit} className="field"><label htmlFor="amount">{isolatedTest&&devicePreview?'手动输入':'现在'}</label><input id="amount" type="number" inputMode="numeric" min="1" step="1" value={amount} disabled={!!pending||busy} onChange={e=>setAmount(e.target.value)} /></form></div><div className="canvas-footer">{devicePreview&&<button type="button" className="text-btn" disabled={!!pending} onClick={()=>{setMessage('');setPage('device')}}>设备预览</button>}{pending&&<button className="text-btn" type="button" onClick={()=>{if(window.confirm('这次提交可能已成功，请先核对当前生产数。放弃重试后再次提交可能造成重复累计，确定放弃吗？')){localStorage.removeItem('xingyi_pending');setPending(null);setAmount('');refresh()}}}>放弃重试</button>}<button className="text-btn" disabled={!!pending} onClick={()=>edit(order)}>管理员修改</button><button form="submit" disabled={busy}>{pending?'重试确认':isolatedTest&&devicePreview?'手动确认':'确认'}</button></div></section></>}
 {page==='device'&&order&&devicePreview&&<DevicePanel db={db} order={order} vaultEnabled={isolatedTest} confirmPreview={deviceConfirmPreview} onProvision={()=>setPage('device-create')} onVault={id=>{setDeviceId(id);setPage('device-vault')}} onConfirm={id=>{setDeviceId(id);setPage('device-confirm')}} onBack={()=>setPage('order')}/>}
 {page==='device-vault'&&order&&deviceId&&devicePreview&&isolatedTest&&<DeviceVaultPanel db={db} deviceId={deviceId} onBack={()=>setPage('device')}/>}
 {page==='device-create'&&order&&devicePreview&&<DeviceProvisionPanel db={db} autoBackup={isolatedTest} onBack={()=>setPage('device')}/>}
 {page==='device-confirm'&&order&&deviceId&&deviceConfirmPreview&&<DeviceConfirmationPanel db={db} deviceId={deviceId} order={order} onBack={()=>setPage('device')} onDone={()=>{refresh();setPage('device')}}/>}
 {page==='order'&&!order&&sync!=='连接中'&&<p>生产单不存在或已被归档。<button onClick={showList}>返回列表</button></p>}
 {page==='edit'&&<section className="panel edit"><button className="back" onClick={()=>{setAdmin('');setPage(form.id?'order':'list');setMessage('')}}>← 返回</button><h1>{form.id?'修改生产单':'新建生产单'}</h1><form onSubmit={save}><label>生产单<input required maxLength="100" value={form.order_no} onChange={e=>setForm({...form,order_no:e.target.value})}/></label><label>产品<input required maxLength="200" value={form.product} onChange={e=>setForm({...form,product:e.target.value})}/></label><label>生产总数<input required type="number" min="0" step="1" value={form.total} onChange={e=>setForm({...form,total:e.target.value})}/></label><label>当前生产数<input required type="number" min="0" step="1" value={form.current} onChange={e=>setForm({...form,current:e.target.value})}/></label><label>管理员密码<input required type="password" value={admin} onChange={e=>setAdmin(e.target.value)}/></label><div className="buttons"><button disabled={busy}>保存</button>{form.id&&<button type="button" className="danger" disabled={busy} onClick={archive}>归档生产单</button>}</div></form></section>}
 {message&&<div className="notice" role="status"><span>{message}</span><button type="button" aria-label="关闭提示" onClick={()=>setMessage('')}>×</button></div>}
 </main>
}
createRoot(document.getElementById('root')).render(<App/>);
