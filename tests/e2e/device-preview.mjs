import assert from 'node:assert/strict';
import {chromium} from 'playwright-core';
import chromiumBinary from '@sparticuz/chromium';
const origin=process.env.TEST_BASE_URL;
const payload=Buffer.from(JSON.stringify({sub:'01234567-89ab-4cde-8fab-0123456789ab',role:'authenticated',exp:Math.floor(Date.now()/1000)+3600})).toString('base64url');
const session={access_token:`eyJhbGciOiJub25lIn0.${payload}.signature`,refresh_token:'fixture-refresh',expires_at:Math.floor(Date.now()/1000)+3600,expires_in:3600,token_type:'bearer',user:{id:'01234567-89ab-4cde-8fab-0123456789ab',email:'fixture@example.invalid',aud:'authenticated',role:'authenticated'}};
const order='d0000000-0000-4000-8000-000000000001',device='e0000000-0000-4000-8000-000000000001',binding='f0000000-0000-4000-8000-000000000001';
const browser=await chromium.launch({executablePath:await chromiumBinary.executablePath(),args:chromiumBinary.args.filter(x=>x!=='--single-process'),headless:true});
async function singleScreen(page,label) {
 const state=await page.evaluate(()=>({height:innerHeight,width:innerWidth,scrollHeight:document.documentElement.scrollHeight,scrollWidth:document.documentElement.scrollWidth,
  elements:[...document.querySelectorAll('.device-panel button,.device-panel .device-note,.device-panel .device-count')].map(el=>{const r=el.getBoundingClientRect();return {text:el.textContent,top:r.top,bottom:r.bottom,left:r.left,right:r.right}})}));
 assert.ok(state.scrollHeight<=state.height+1 && state.scrollWidth<=state.width+1,`${label}: overflow ${JSON.stringify(state)}`);
 for(const e of state.elements)assert.ok(e.top>=-1 && e.bottom<=state.height+1 && e.left>=-1 && e.right<=state.width+1,`${label}: clipped element ${JSON.stringify(e)}`);
}
try {
 for(const size of [{width:390,height:844},{width:320,height:568},{width:568,height:320},{width:1280,height:720}]) {
  const context=await browser.newContext({viewport:size,isMobile:true,hasTouch:true});const page=await context.newPage();let desired=null,draft=7,fail=false,binds=0;
  await page.route('**/*',r=>['127.0.0.1','localhost'].includes(new URL(r.request().url()).hostname)?r.continue():r.abort());
  await page.routeWebSocket('**/*',ws=>ws.close());
  await page.route('https://example.supabase.co/**',async route=>{
   const req=route.request(),path=new URL(req.url()).pathname,headers={'access-control-allow-origin':'*','content-type':'application/json'};
   if(req.method()==='OPTIONS')return route.fulfill({status:204,headers:{...headers,'access-control-allow-methods':'GET,POST,OPTIONS','access-control-allow-headers':'*'}});
   if(path==='/rest/v1/rpc/is_operator')return route.fulfill({status:200,headers,body:'true'});
   if(path==='/auth/v1/user')return route.fulfill({status:200,headers,body:JSON.stringify(session.user)});
   if(path==='/rest/v1/orders')return route.fulfill({status:200,headers,body:JSON.stringify([{id:order,order_no:'TEST-ORDER',product:'测试产品',total:100,current_count:12,archived:false,created_at:'2026-01-01T00:00:00Z'}])});
   if(path==='/rest/v1/rpc/device_list')return fail?route.fulfill({status:503,headers,body:'{}'}):route.fulfill({status:200,headers,body:JSON.stringify([{id:device,label:'设备'.repeat(50),desired_binding_id:desired?binding:null,desired_order_id:desired,desired_binding_revision:desired?1:null}])});
   if(path==='/rest/v1/device_order_drafts')return route.fulfill({status:200,headers,body:JSON.stringify({order_id:order,count:draft})});
   if(path==='/rest/v1/rpc/device_request_binding'){assert.deepEqual(req.postDataJSON(),{p_device:device,p_order:order});binds++;desired=order;return route.fulfill({status:200,headers,body:JSON.stringify(binding)});}
   throw new Error(`Unexpected request: ${path}`);
  });
  await page.addInitScript(s=>localStorage.setItem('sb-example-auth-token',JSON.stringify(s)),session);
  await page.goto(origin);await page.getByRole('button',{name:/TEST-ORDER/}).click();
  for(const button of await page.locator('.canvas-footer button').all()){const box=await button.boundingBox();assert.ok(box && box.x>=0 && box.y>=0 && box.x+box.width<=size.width+1 && box.y+box.height<=size.height+1,`order footer clipped: ${JSON.stringify({size,box})}`);}
  await page.getByRole('button',{name:'设备预览'}).click();
  await page.getByText('设备待确认数').waitFor();assert.equal(await page.locator('.device-count strong').textContent(),'7');
  assert.equal(await page.getByRole('button',{name:'设备确认（未启用）'}).isDisabled(),true);
  await singleScreen(page,JSON.stringify(size));
  page.once('dialog',dialog=>dialog.accept());await page.getByRole('button',{name:'请求绑定到此单'}).click();
  await page.getByText('绑定请求已发送；设备可能尚未完成切换。').waitFor();
  await page.getByText('已请求绑定此单，不代表设备已完成切换。').waitFor();assert.equal(binds,1);
  await singleScreen(page,`${JSON.stringify(size)} after binding`);
  draft=9007199254740991;await page.waitForFunction(()=>document.querySelector('.device-count strong')?.textContent==='9007199254740991');
  await singleScreen(page,`${JSON.stringify(size)} maximum count`);
  const countBox=await page.locator('.device-count strong').evaluate(el=>({scroll:el.scrollWidth,width:el.clientWidth}));assert.ok(countBox.scroll<=countBox.width+1,'full device count must be visible');
  fail=true;await page.getByRole('alert').waitFor();assert.equal(await page.locator('.device-count').count(),0,'network error must not display a stale count as current');assert.equal(await page.getByRole('button',{name:'请求绑定到此单'}).isDisabled(),true);
  fail=false;await page.getByText('设备待确认数').waitFor();
  await page.getByRole('button',{name:'← 返回生产单'}).click();assert.equal(await page.getByLabel('现在',{exact:true}).inputValue(),'');
  await context.close();
 }
 console.log('device preview mocked browser tests passed');
} finally {await browser.close();}
