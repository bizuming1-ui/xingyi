import assert from 'node:assert/strict';
import {chromium} from 'playwright-core';
import chromiumBinary from '@sparticuz/chromium';
const origin=process.env.TEST_BASE_URL;
const payload=Buffer.from(JSON.stringify({sub:'01234567-89ab-4cde-8fab-0123456789ab',role:'authenticated',exp:Math.floor(Date.now()/1000)+3600})).toString('base64url');
const session={access_token:`eyJhbGciOiJub25lIn0.${payload}.signature`,refresh_token:'fixture-refresh',expires_at:Math.floor(Date.now()/1000)+3600,expires_in:3600,token_type:'bearer',user:{id:'01234567-89ab-4cde-8fab-0123456789ab',email:'fixture@example.invalid',aud:'authenticated',role:'authenticated'}};
const order='d0000000-0000-4000-8000-000000000001',device='e0000000-0000-4000-8000-000000000001',secret='a'.repeat(64);
const browser=await chromium.launch({executablePath:await chromiumBinary.executablePath(),args:chromiumBinary.args.filter(x=>x!=='--single-process'),headless:true});
try {
 for(const {size,lost} of [{size:{width:390,height:844}},{size:{width:320,height:568}},{size:{width:568,height:320}},{size:{width:1280,height:720}},{size:{width:390,height:844},lost:true}]) {
  const context=await browser.newContext({viewport:size,isMobile:true,hasTouch:true});const page=await context.newPage();let created=false,calls=0;
  await page.route('**/*',r=>['127.0.0.1','localhost'].includes(new URL(r.request().url()).hostname)?r.continue():r.abort());await page.routeWebSocket('**/*',ws=>ws.close());
  await page.route('https://example.supabase.co/**',async route=>{
   const req=route.request(),path=new URL(req.url()).pathname,headers={'access-control-allow-origin':'*','content-type':'application/json'};
   if(req.method()==='OPTIONS')return route.fulfill({status:204,headers:{...headers,'access-control-allow-methods':'GET,POST,OPTIONS','access-control-allow-headers':'*'}});
   if(path==='/rest/v1/rpc/is_operator')return route.fulfill({status:200,headers,body:'true'});
   if(path==='/auth/v1/user')return route.fulfill({status:200,headers,body:JSON.stringify(session.user)});
   if(path==='/rest/v1/orders')return route.fulfill({status:200,headers,body:JSON.stringify([{id:order,order_no:'TEST',product:'fixture',total:100,current_count:0,archived:false,created_at:'2026-01-01T00:00:00Z'}])});
   if(path==='/rest/v1/rpc/device_list')return route.fulfill({status:200,headers,body:JSON.stringify(created?[{id:device,label:'ESP32-S3',desired_binding_id:null,desired_order_id:null,desired_binding_revision:null}]:[])});
   if(path==='/rest/v1/device_order_drafts')return route.fulfill({status:200,headers,body:'null'});
   if(path==='/rest/v1/rpc/device_create'){calls++;assert.deepEqual(req.postDataJSON(),{p_admin_password:'fixture-admin',p_label:'ESP32-S3'});created=true;if(lost)return route.fulfill({status:503,headers,body:'{}'});return route.fulfill({status:200,headers,body:JSON.stringify({id:device,secret})});}
   throw Error(`unexpected request ${path}`);
  });
  await page.addInitScript(s=>localStorage.setItem('sb-example-auth-token',JSON.stringify(s)),session);
  await page.goto(origin);await page.getByRole('button',{name:/TEST/}).click();await page.getByRole('button',{name:'设备预览'}).click();
  await page.getByRole('button',{name:'管理员创建设备'}).click();await page.getByLabel('管理员密码').fill('fixture-admin');
  await page.getByRole('button',{name:'创建设备',exact:true}).click();
  if(lost){await page.getByRole('alert').waitFor();assert.match(await page.getByRole('alert').textContent(),/结果未知/);assert.equal(await page.getByRole('button',{name:'创建设备',exact:true}).isDisabled(),true);assert.equal(calls,1);await context.close();continue;}
  await page.getByLabel('设备密钥').waitFor();
  assert.equal(await page.getByLabel('设备 ID').inputValue(),device);assert.equal(await page.getByLabel('设备密钥').inputValue(),secret);assert.equal(calls,1);
  assert.equal(await page.evaluate(s=>JSON.stringify({...localStorage}).includes(s),secret),false);
  assert.equal(await page.evaluate(s=>JSON.stringify({...sessionStorage}).includes(s),secret),false);
  const metrics=await page.evaluate(()=>({h:document.documentElement.scrollHeight,w:document.documentElement.scrollWidth,ih:innerHeight,iw:innerWidth}));
  assert.ok(metrics.h<=metrics.ih+1 && metrics.w<=metrics.iw+1,`overflow ${JSON.stringify({size,metrics})}`);
  page.once('dialog',d=>d.accept());await page.getByRole('button',{name:'← 返回设备计数'}).click();await page.getByText('设备待确认数').waitFor();assert.equal(await page.getByLabel('设备密钥').count(),0);
  await context.close();
 }
 console.log('device provisioning test preview passed');
} finally {await browser.close();}
