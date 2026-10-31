import assert from 'node:assert/strict';
import {chromium} from 'playwright-core';import chromiumBinary from '@sparticuz/chromium';
const origin=process.env.TEST_BASE_URL;
const payload=Buffer.from(JSON.stringify({sub:'01234567-89ab-4cde-8fab-0123456789ab',role:'authenticated',exp:Math.floor(Date.now()/1000)+3600})).toString('base64url');
const session={access_token:`eyJhbGciOiJub25lIn0.${payload}.signature`,refresh_token:'fixture-refresh',expires_at:Math.floor(Date.now()/1000)+3600,expires_in:3600,token_type:'bearer',user:{id:'01234567-89ab-4cde-8fab-0123456789ab',email:'fixture@example.invalid',aud:'authenticated',role:'authenticated'}};
const order='d0000000-0000-4000-8000-000000000001';const browser=await chromium.launch({executablePath:await chromiumBinary.executablePath(),args:chromiumBinary.args.filter(x=>x!=='--single-process'),headless:true});
try {
 const context=await browser.newContext({viewport:{width:1280,height:800}}),page=await context.newPage();let draft=7,closed=0,writes=0;
 await page.addInitScript(s=>{
   localStorage.setItem('sb-example-auth-token',JSON.stringify(s));let controller;
   window.emitSerial=line=>controller.enqueue(new TextEncoder().encode(line+'\r\n'));
   const port={readable:null,open:async function(){this.readable=new ReadableStream({start(c){controller=c;},cancel(){}});},close:async()=>{window.closeCount=(window.closeCount||0)+1;},writable:{getWriter(){window.writeCount=(window.writeCount||0)+1;throw Error('must not write');}}};
   Object.defineProperty(navigator,'serial',{configurable:true,value:{requestPort:async()=>port}});
 },session);
 await page.route('**/*',r=>['127.0.0.1','localhost'].includes(new URL(r.request().url()).hostname)?r.continue():r.abort());
 await page.routeWebSocket('**/*',ws=>ws.close());
 await page.route('https://example.supabase.co/**',async route=>{
  const req=route.request(),path=new URL(req.url()).pathname,headers={'access-control-allow-origin':'*','content-type':'application/json'};
  const reply=body=>route.fulfill({status:200,headers,body:JSON.stringify(body)});
  if(req.method()==='OPTIONS')return route.fulfill({status:204,headers:{...headers,'access-control-allow-methods':'GET,POST,OPTIONS','access-control-allow-headers':'*'}});
  if(path==='/rest/v1/rpc/is_operator')return reply(true);
  if(path==='/auth/v1/user')return reply(session.user);
  if(path==='/rest/v1/orders')return reply([{id:order,order_no:'TEST-ORDER',product:'测试',total:100,current_count:12,archived:false,created_at:'2026-01-01T00:00:00Z'}]);
  if(path==='/rest/v1/device_order_drafts')return reply({order_id:order,count:draft});
  throw new Error(`Unexpected ${path}`);
 });
 await page.goto(origin);await page.getByRole('button',{name:/TEST-ORDER/}).click();
 await page.getByRole('button',{name:'显示测速（测试）'}).click();await page.getByRole('button',{name:'连接 COM21'}).click();
 const rects=await page.evaluate(()=>{const a=document.querySelector('.latency-panel').getBoundingClientRect(),b=document.querySelector('[aria-label="现在（设备待确认数）"]').getBoundingClientRect();return {a:{left:a.left,right:a.right,top:a.top,bottom:a.bottom},b:{left:b.left,right:b.right,top:b.top,bottom:b.bottom}}});assert.ok(rects.a.right<=rects.b.left||rects.b.right<=rects.a.left||rects.a.bottom<=rects.b.top||rects.b.bottom<=rects.a.top,`measurement panel obscures count: ${JSON.stringify(rects)}`);
 await page.getByText('请短按一次作校准，等待上传完成。').waitFor();
 await page.evaluate(()=>{emitSerial('COUNT t=100 total=101 p=1');emitSerial('M1 i=5 t=300 k=2 d=100 r=0 b=0');emitSerial('M3 i=5 x=101');emitSerial('WIFI=1 TIME=1 BOUND=1 PENDING=0 t=400');emitSerial('FREEZE b=0 f=0 c=0 i=1');emitSerial('MON v=1 t=400 lost=0');});
 const arm=page.getByRole('button',{name:'准备测试一次'});await arm.waitFor();await page.waitForFunction(()=>!document.querySelector('.latency-panel button:disabled') || [...document.querySelectorAll('.latency-panel button')].some(x=>x.textContent==='准备测试一次'&&!x.disabled));await arm.click();
 await page.evaluate(()=>emitSerial('COUNT t=500 total=102 p=1'));await page.getByText('已收到串口计数，等待网页更新。').waitFor();draft=8;
 await page.getByText(/串口收到计数到网页首帧约/).waitFor();assert.equal(await page.getByLabel('现在（设备待确认数）').inputValue(),'8');
 await page.getByRole('button',{name:'断开串口'}).click();await page.waitForFunction(()=>window.closeCount===1);closed=await page.evaluate(()=>window.closeCount);writes=await page.evaluate(()=>window.writeCount||0);assert.equal(closed,1);assert.equal(writes,0);
 await context.close();console.log('isolated serial latency UI tests passed');
} finally {await browser.close();}
