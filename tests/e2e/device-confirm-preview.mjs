import assert from 'node:assert/strict';
import {chromium} from 'playwright-core';
import chromiumBinary from '@sparticuz/chromium';
const origin=process.env.TEST_BASE_URL;
const payload=Buffer.from(JSON.stringify({sub:'01234567-89ab-4cde-8fab-0123456789ab',role:'authenticated',exp:Math.floor(Date.now()/1000)+3600})).toString('base64url');
const session={access_token:`eyJhbGciOiJub25lIn0.${payload}.signature`,refresh_token:'fixture-refresh',expires_at:Math.floor(Date.now()/1000)+3600,expires_in:3600,token_type:'bearer',user:{id:'01234567-89ab-4cde-8fab-0123456789ab',email:'fixture@example.invalid',aud:'authenticated',role:'authenticated'}};
const order='d0000000-0000-4000-8000-000000000001',device='e0000000-0000-4000-8000-000000000001',binding='f0000000-0000-4000-8000-000000000001',barrier='a0000000-0000-4000-8000-000000000001';
const browser=await chromium.launch({executablePath:await chromiumBinary.executablePath(),args:chromiumBinary.args.filter(x=>x!=='--single-process'),headless:true});
async function checkVisible(page,label) {
 const state=await page.evaluate(()=>({height:innerHeight,width:innerWidth,scrollHeight:document.documentElement.scrollHeight,scrollWidth:document.documentElement.scrollWidth,
   elements:[...document.querySelectorAll('.confirmation-panel button,.confirmation-panel p,.confirmation-panel .device-summary')].map(el=>{const r=el.getBoundingClientRect();return {text:el.textContent,top:r.top,bottom:r.bottom,left:r.left,right:r.right}})}));
 assert.ok(state.scrollHeight<=state.height+1 && state.scrollWidth<=state.width+1,`${label} overflow ${JSON.stringify(state)}`);
 for(const e of state.elements)assert.ok(e.top>=-1 && e.bottom<=state.height+1 && e.left>=-1 && e.right<=state.width+1,`${label} clipped ${JSON.stringify(e)}`);
}
async function setup(size,{noLocks=false,existing=false}={}) {
 const context=await browser.newContext({viewport:size,isMobile:true,hasTouch:true}),page=await context.newPage();
 let phase=existing?'requested':'none',draft=7,current=12,requestId=existing?'70000000-0000-4000-8000-000000000001':null,requests=0,commits=0,events=0,abortCommit=true;
 let signalCommit;const committed=new Promise(resolve=>signalCommit=resolve);
 await page.route('**/*',r=>['127.0.0.1','localhost'].includes(new URL(r.request().url()).hostname)?r.continue():r.abort());
 await page.routeWebSocket('**/*',ws=>ws.close());
 await page.route('https://example.supabase.co/**',async route=>{
  const req=route.request(),path=new URL(req.url()).pathname,headers={'access-control-allow-origin':'*','content-type':'application/json'};
  const reply=body=>route.fulfill({status:200,headers,body:JSON.stringify(body)});
  if(req.method()==='OPTIONS')return route.fulfill({status:204,headers:{...headers,'access-control-allow-methods':'GET,POST,OPTIONS','access-control-allow-headers':'*'}});
  if(path==='/rest/v1/rpc/is_operator')return reply(true);
  if(path==='/auth/v1/user')return reply(session.user);
  if(path==='/rest/v1/orders')return reply([{id:order,order_no:'TEST-ORDER',product:'测试产品',total:100,current_count:current,archived:false,created_at:'2026-01-01T00:00:00Z'}]);
  if(path==='/rest/v1/rpc/device_list')return reply([{id:device,label:'测试设备',desired_binding_id:binding,desired_order_id:order,desired_binding_revision:1}]);
  if(path==='/rest/v1/device_order_drafts')return reply({order_id:order,count:draft});
  if(path==='/rest/v1/rpc/device_confirmation_open')return reply(['requested','ready','expired'].includes(phase)?{id:barrier,request_id:requestId,device_id:device,order_id:order,state:phase}:null);
  if(path==='/rest/v1/rpc/device_confirmation_request') {requests++;const args=req.postDataJSON();assert.equal(args.p_device,device);assert.equal(args.p_order,order);requestId=args.p_request;assert.match(requestId,/^[0-9a-f-]{36}$/);phase='requested';return reply(barrier);}
  if(path==='/rest/v1/rpc/device_confirmation_view')return reply({id:barrier,order_id:order,state:phase,frozen_count:7,result:phase==='completed'?{requested:7,applied:7,current:19}:null});
  if(path==='/rest/v1/rpc/device_confirmation_commit') {commits++;assert.equal(phase,'ready');phase='completed';draft=0;current=19;events++;signalCommit();if(abortCommit){abortCommit=false;return route.abort('failed');}return reply({requested:7,applied:7,current:19});}
  if(path==='/rest/v1/rpc/device_confirmation_cancel') {assert.notEqual(phase,'completed');phase='cancelled';return reply(true);}
  throw new Error(`Unexpected backend request ${path}`);
 });
 await page.addInitScript(s=>localStorage.setItem('sb-example-auth-token',JSON.stringify(s)),session);
 if(noLocks)await page.addInitScript(()=>Object.defineProperty(navigator,'locks',{value:undefined}));
 await page.goto(origin);await page.getByRole('button',{name:/TEST-ORDER/}).click();
 if(await page.getByLabel('现在（设备待确认数）').count()) {
  await page.waitForFunction(()=>document.querySelector('[aria-label="现在（设备待确认数）"]')?.value==='7');
  await page.getByRole('button',{name:'设备确认（测试）'}).click();
 } else {
  await page.getByRole('button',{name:'设备预览'}).click();await page.getByText('设备待确认数').waitFor();
  await page.getByRole('button',{name:'设备确认（测试）'}).click();
 }
 if(noLocks)await page.getByText(/不支持安全的多标签页协调/).waitFor();
 else if(existing)await page.getByRole('button',{name:'恢复未结束请求'}).waitFor();
 else await page.getByRole('button',{name:'开始设备确认'}).waitFor();
 return {context,page,committed,ready:()=>phase='ready',expire:()=>phase='expired',counts:()=>({commits,events}),requestCount:()=>requests};
}
try {
 const f=await setup({width:390,height:844}),p=f.page;
 await checkVisible(p,'initial');p.once('dialog',d=>d.dismiss());await p.getByRole('button',{name:'开始设备确认'}).click();assert.equal(f.requestCount(),0);
 p.once('dialog',d=>d.accept());await p.getByRole('button',{name:'开始设备确认'}).click();
 await p.getByText('等待设备冻结并同步，请保持暂停。').waitFor();assert.equal(await p.getByRole('button',{name:'确认设备计数'}).count(),0);
 f.ready();await p.getByRole('button',{name:'确认设备计数'}).waitFor();await checkVisible(p,'ready');
 p.once('dialog',d=>d.accept());await p.getByRole('button',{name:'确认设备计数'}).click();
 await f.committed;assert.equal(await p.evaluate(()=>JSON.parse(localStorage.getItem('xingyi_device_confirmation_v1')).commitAttempted),true);
 await p.reload();await p.getByRole('button',{name:/TEST-ORDER/}).click();await p.getByRole('button',{name:'设备预览'}).click();
 await p.getByRole('button',{name:'设备确认（测试）'}).click();await p.getByText('设备确认已完成。').waitFor();await p.getByText('请求 7，实际累计 7，当前 19。').waitFor();
 assert.deepEqual(f.counts(),{commits:1,events:1});await checkVisible(p,'completed');
 await p.getByRole('button',{name:'知道了'}).click();await p.getByText('设备待确认数').waitFor();
 await p.waitForFunction(()=>document.querySelector('.device-count strong')?.textContent==='0');
 await f.context.close();
 const g=await setup({width:568,height:320}),q=g.page;
 q.once('dialog',d=>d.accept());await q.getByRole('button',{name:'开始设备确认'}).click();await q.getByText('等待设备冻结并同步，请保持暂停。').waitFor();
 g.expire();await q.getByText('设备续约已过期，请取消后重新发起。').waitFor();await checkVisible(q,'expired landscape');
 q.once('dialog',d=>d.accept());await q.getByRole('button',{name:'取消确认'}).click();await q.getByText('本次确认已取消，设备计数保留。').waitFor();
 assert.deepEqual(g.counts(),{commits:0,events:0});await checkVisible(q,'cancelled landscape');await g.context.close();
 const h=await setup({width:320,height:568},{existing:true}),r=h.page;await checkVisible(r,'recover');
 r.once('dialog',d=>d.accept());await r.getByRole('button',{name:'恢复未结束请求'}).click();
 await r.getByText('等待设备冻结并同步，请保持暂停。').waitFor();
 r.once('dialog',d=>d.accept());await r.getByRole('button',{name:'取消确认'}).click();await r.getByText('本次确认已取消，设备计数保留。').waitFor();
 assert.deepEqual(h.counts(),{commits:0,events:0});assert.equal(h.requestCount(),0);await h.context.close();
 const unsupported=await setup({width:568,height:320},{noLocks:true});
 assert.equal(await unsupported.page.getByRole('button',{name:'开始设备确认'}).count(),0);
 await checkVisible(unsupported.page,'unsupported lock');await unsupported.context.close();
 console.log('device confirmation preview mocked browser tests passed');
} finally {await browser.close();}
