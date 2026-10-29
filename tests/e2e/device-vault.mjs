import assert from 'node:assert/strict';import {chromium} from 'playwright-core';import chromiumBinary from '@sparticuz/chromium';
const live=process.env.PUBLIC_PAGES_LIVE==='1';
const origin=live?'https://bizuming1-ui.github.io/xingyi-isolated-test/':process.env.TEST_BASE_URL;
const api=live?'https://xypisxdlxcqyavixdiji.supabase.co/**':'https://example.supabase.co/**';
const payload=Buffer.from(JSON.stringify({sub:'01234567-89ab-4cde-8fab-0123456789ab',role:'authenticated',exp:Math.floor(Date.now()/1000)+3600})).toString('base64url');
const session={access_token:`eyJhbGciOiJub25lIn0.${payload}.signature`,refresh_token:'fixture-refresh',expires_at:Math.floor(Date.now()/1000)+3600,expires_in:3600,token_type:'bearer',user:{id:'01234567-89ab-4cde-8fab-0123456789ab',email:'fixture@example.invalid',aud:'authenticated',role:'authenticated'}};
const order='d0000000-0000-4000-8000-000000000001',device='e0000000-0000-4000-8000-000000000001',secret='a'.repeat(64),password='fixture-admin';
const browser=await chromium.launch({executablePath:await chromiumBinary.executablePath(),args:chromiumBinary.args.filter(x=>x!=='--single-process'),headless:true});
try{
 for(const size of [{width:390,height:844},{width:320,height:568},{width:568,height:320},{width:1280,height:720}]) {
  const context=await browser.newContext({viewport:size,isMobile:true,hasTouch:true});const page=await context.newPage();let envelope=null;
  await page.route('**/*',r=>(['127.0.0.1','localhost'].includes(new URL(r.request().url()).hostname)||(live&&r.request().url()===origin))?r.continue():r.abort());await page.routeWebSocket('**/*',ws=>ws.close());
  await page.route(api,async route=>{
   const req=route.request(),path=new URL(req.url()).pathname,headers={'access-control-allow-origin':'*','content-type':'application/json'};
   if(req.method()==='OPTIONS')return route.fulfill({status:204,headers:{...headers,'access-control-allow-methods':'GET,POST,OPTIONS','access-control-allow-headers':'*'}});
   let body;
   if(path==='/rest/v1/rpc/is_operator')body=true;
   else if(path==='/auth/v1/user')body=session.user;
   else if(path==='/rest/v1/orders')body=[{id:order,order_no:'TEST',product:'fixture',total:100,current_count:0,archived:false,created_at:'2026-01-01T00:00:00Z'}];
   else if(path==='/rest/v1/rpc/device_list')body=[{id:device,label:'ESP32-S3',desired_binding_id:null,desired_order_id:null,desired_binding_revision:null}];
   else if(path==='/rest/v1/device_order_drafts')body=null;
   else if(path==='/rest/v1/rpc/test_device_vault_save'){const p=req.postDataJSON();assert.equal(p.p_password,password);assert.equal(p.p_secret,secret);assert.equal(p.p_device,device);envelope=p.p_envelope;assert.ok(!JSON.stringify(envelope).includes(secret));body=true;}
   else if(path==='/rest/v1/rpc/test_device_vault_read'){const p=req.postDataJSON();body=p.p_password===password?(envelope?{state:'saved',envelope}:{state:'missing'}):null;}
   else throw Error(`unexpected request ${path}`);
   return route.fulfill({status:200,headers,body:JSON.stringify(body)});
  });
  await page.addInitScript(({s,live})=>localStorage.setItem(live?'sb-xypisxdlxcqyavixdiji-auth-token':'sb-example-auth-token',JSON.stringify(s)),{s:session,live});
  await page.goto(origin);await page.getByRole('button',{name:/TEST/}).click();await page.getByRole('button',{name:'设备预览'}).click();
  await page.getByRole('button',{name:'设备 ID / 密钥'}).click();assert.equal(await page.getByLabel('设备 ID').inputValue(),device);
  await page.getByRole('button',{name:'保存现有密钥'}).click();await page.getByLabel('现有设备密钥').fill(secret);await page.getByLabel('管理员密码').fill(password);await page.getByRole('button',{name:'加密并保存'}).click();
  await page.getByText('已加密保存。',{exact:false}).waitFor();
  await page.getByRole('button',{name:'找回密钥'}).click();await page.getByLabel('管理员密码').fill('wrong');await page.getByRole('button',{name:'解锁'}).click();await page.getByText('管理员密码错误或暂时锁定。').waitFor();
  await page.getByLabel('管理员密码').fill(password);await page.getByRole('button',{name:'解锁'}).click();await page.getByLabel('设备密钥',{exact:true}).waitFor();
  assert.equal(await page.getByLabel('设备密钥',{exact:true}).inputValue(),secret);assert.equal(await page.getByLabel('设备密钥',{exact:true}).getAttribute('type'),'password');
  assert.equal(await page.evaluate(s=>JSON.stringify({...localStorage,...sessionStorage}).includes(s),secret),false);
  const metrics=await page.evaluate(()=>({h:document.documentElement.scrollHeight,w:document.documentElement.scrollWidth,ih:innerHeight,iw:innerWidth}));assert.ok(metrics.h<=metrics.ih+1&&metrics.w<=metrics.iw+1,JSON.stringify({size,metrics}));
  await page.getByRole('button',{name:'← 返回设备凭据'}).click();assert.equal(await page.getByLabel('设备密钥',{exact:true}).count(),0);await context.close();
 }
 console.log('device vault UI save/unlock and mobile regression passed');
}finally{await browser.close();}
