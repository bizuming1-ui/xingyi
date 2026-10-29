import assert from 'node:assert/strict';
import {chromium} from 'playwright-core';
import chromiumBinary from '@sparticuz/chromium';
const origin=process.env.TEST_BASE_URL;
const payload=Buffer.from(JSON.stringify({sub:'01234567-89ab-4cde-8fab-0123456789ab',role:'authenticated',exp:Math.floor(Date.now()/1000)+3600})).toString('base64url');
const session={access_token:`eyJhbGciOiJub25lIn0.${payload}.signature`,refresh_token:'fixture-refresh',expires_at:Math.floor(Date.now()/1000)+3600,expires_in:3600,token_type:'bearer',user:{id:'01234567-89ab-4cde-8fab-0123456789ab',email:'fixture@example.invalid',aud:'authenticated',role:'authenticated'}};
const order='d0000000-0000-4000-8000-000000000001',device='e0000000-0000-4000-8000-000000000001',secret='a'.repeat(64),password='fixture-admin';
const browser=await chromium.launch({executablePath:await chromiumBinary.executablePath(),args:chromiumBinary.args.filter(x=>x!=='--single-process'),headless:true});
try {
 for(const {size,outcome} of [{size:{width:390,height:844},outcome:'ok'},{size:{width:320,height:568},outcome:'ok'},{size:{width:568,height:320},outcome:'ok'},{size:{width:1280,height:720},outcome:'ok'},{size:{width:390,height:844},outcome:'save-lost'},{size:{width:390,height:844},outcome:'read-lost'},{size:{width:390,height:844},outcome:'create-lost'}]) {
  const context=await browser.newContext({viewport:size,isMobile:true,hasTouch:true});const page=await context.newPage();let created=false,calls=0,envelope=null;
  await page.route('**/*',r=>['127.0.0.1','localhost'].includes(new URL(r.request().url()).hostname)?r.continue():r.abort());await page.routeWebSocket('**/*',ws=>ws.close());
  await page.route('https://example.supabase.co/**',async route=>{
   const req=route.request(),path=new URL(req.url()).pathname,headers={'access-control-allow-origin':'*','content-type':'application/json'};
   if(req.method()==='OPTIONS')return route.fulfill({status:204,headers:{...headers,'access-control-allow-methods':'GET,POST,OPTIONS','access-control-allow-headers':'*'}});
   let value;
   if(path==='/rest/v1/rpc/is_operator')value=true;
   else if(path==='/auth/v1/user')value=session.user;
   else if(path==='/rest/v1/orders')value=[{id:order,order_no:'TEST',product:'fixture',total:100,current_count:0,archived:false,created_at:'2026-01-01T00:00:00Z'}];
   else if(path==='/rest/v1/rpc/device_list')value=created?[{id:device,label:'ESP32-S3',desired_binding_id:null,desired_order_id:null,desired_binding_revision:null}]:[];
   else if(path==='/rest/v1/device_order_drafts')value=null;
   else if(path==='/rest/v1/rpc/device_create'){calls++;created=true;if(outcome==='create-lost')return route.fulfill({status:503,headers,body:'{}'});value={id:device,secret};}
   else if(path==='/rest/v1/rpc/test_device_vault_save'){const p=req.postDataJSON();assert.equal(p.p_password,password);assert.equal(p.p_secret,secret);envelope=p.p_envelope;assert.ok(!JSON.stringify(envelope).includes(secret));if(outcome==='save-lost')return route.fulfill({status:503,headers,body:'{}'});value=true;}
   else if(path==='/rest/v1/rpc/test_device_vault_read'){if(outcome==='read-lost')return route.fulfill({status:503,headers,body:'{}'});value={state:'saved',envelope};}
   else throw Error(`unexpected ${path}`);
   return route.fulfill({status:200,headers,body:JSON.stringify(value)});
  });
  await page.addInitScript(s=>localStorage.setItem('sb-example-auth-token',JSON.stringify(s)),session);
  await page.goto(origin);await page.getByRole('button',{name:/TEST/}).click();await page.getByRole('button',{name:'设备预览'}).click();
  await page.getByRole('button',{name:'管理员创建设备'}).click();await page.getByLabel('管理员密码').fill(password);await page.getByRole('button',{name:'创建设备',exact:true}).click();
  if(outcome==='create-lost'){
   await page.getByRole('alert').waitFor();assert.match(await page.getByRole('alert').textContent(),/结果未知/);assert.equal(await page.getByRole('button',{name:'创建设备',exact:true}).isDisabled(),true);
  } else {
   await page.getByText(outcome==='ok'?'密钥已自动生成、加密保存并验证。':'设备已创建，但备份未确认。',{exact:false}).waitFor();
   assert.equal(await page.getByLabel('设备 ID').inputValue(),device);assert.equal(await page.getByLabel('设备密钥').inputValue(),secret);assert.equal(await page.getByLabel('设备密钥').getAttribute('type'),'password');
   assert.equal(await page.evaluate(s=>JSON.stringify({...localStorage,...sessionStorage}).includes(s),secret),false);
   const m=await page.evaluate(()=>({h:document.documentElement.scrollHeight,w:document.documentElement.scrollWidth,ih:innerHeight,iw:innerWidth}));assert.ok(m.h<=m.ih+1&&m.w<=m.iw+1,JSON.stringify({size,m}));
   page.once('dialog',d=>d.accept());await page.getByRole('button',{name:'← 返回设备计数'}).click();await page.getByText('设备待确认数').waitFor();
  }
  assert.equal(calls,1);await context.close();
 }
 console.log('automatic device creation and encrypted backup browser tests passed');
} finally {await browser.close();}
