import assert from 'node:assert/strict';import {chromium} from 'playwright-core';import chromiumBinary from '@sparticuz/chromium';
const live=process.env.PUBLIC_PAGES_LIVE==='1';
const origin=live?'https://bizuming1-ui.github.io/xingyi-isolated-test/':process.env.TEST_BASE_URL;
const api=live?'https://xypisxdlxcqyavixdiji.supabase.co/**':'https://example.supabase.co/**';
const payload=Buffer.from(JSON.stringify({sub:'01234567-89ab-4cde-8fab-0123456789ab',role:'authenticated',exp:Math.floor(Date.now()/1000)+3600})).toString('base64url');
const session={access_token:`eyJhbGciOiJub25lIn0.${payload}.signature`,refresh_token:'fixture-refresh',expires_at:Math.floor(Date.now()/1000)+3600,expires_in:3600,token_type:'bearer',user:{id:'01234567-89ab-4cde-8fab-0123456789ab',email:'fixture@example.invalid',aud:'authenticated',role:'authenticated'}};
const order='d0000000-0000-4000-8000-000000000001',device='e0000000-0000-4000-8000-000000000001',secret='a'.repeat(64),password='fixture-admin';
const browser=await chromium.launch({executablePath:await chromiumBinary.executablePath(),args:chromiumBinary.args.filter(x=>x!=='--single-process'),headless:true});
try{
 for(const {size,lost} of [{size:{width:390,height:844}},{size:{width:320,height:568}},{size:{width:568,height:320}},{size:{width:1280,height:720}},{size:{width:390,height:844},lost:true}]) {
  const context=await browser.newContext({viewport:size,isMobile:true,hasTouch:true});const page=await context.newPage();let envelope=null,rotated=null,manualRequest=null,manualCurrent=0,manualLost=false;
  await page.route('**/*',r=>(['127.0.0.1','localhost'].includes(new URL(r.request().url()).hostname)||(live&&r.request().url()===origin))?r.continue():r.abort());await page.routeWebSocket('**/*',ws=>ws.close());
  await page.route(api,async route=>{
   const req=route.request(),path=new URL(req.url()).pathname,headers={'access-control-allow-origin':'*','content-type':'application/json'};
   if(req.method()==='OPTIONS')return route.fulfill({status:204,headers:{...headers,'access-control-allow-methods':'GET,POST,OPTIONS','access-control-allow-headers':'*'}});
   let body;
   if(path==='/rest/v1/rpc/is_operator')body=true;
   else if(path==='/auth/v1/user')body=session.user;
   else if(path==='/rest/v1/orders')body=[{id:order,order_no:'TEST',product:'fixture',total:100,current_count:manualCurrent,archived:false,created_at:'2026-01-01T00:00:00Z'}];
   else if(path==='/rest/v1/rpc/device_list')body=[{id:device,label:'ESP32-S3',desired_binding_id:null,desired_order_id:null,desired_binding_revision:null}];
   else if(path==='/rest/v1/device_order_drafts')body={order_id:order,count:426};
   else if(path==='/rest/v1/rpc/test_admin_replace_count'){manualRequest=req.postDataJSON();if(manualRequest.p_password!==password)body=null;else if(manualRequest.p_expected!==manualCurrent)body={status:'conflict'};else {manualCurrent=manualRequest.p_target;if(manualLost){manualLost=false;return route.abort('failed');}body={status:'replaced',current:manualCurrent};}}
   else if(path==='/rest/v1/rpc/test_device_vault_save'){const p=req.postDataJSON();assert.equal(p.p_password,password);assert.equal(p.p_secret,secret);assert.equal(p.p_device,device);envelope=p.p_envelope;assert.ok(!JSON.stringify(envelope).includes(secret));body=true;}
   else if(path==='/rest/v1/rpc/test_device_rotation_revision'){body={status:'ready',revision:0};}
   else if(path==='/rest/v1/rpc/test_device_rotate'){const p=req.postDataJSON();assert.equal(p.p_password,password);assert.equal(p.p_device,device);assert.match(p.p_secret,/^[0-9a-f]{64}$/);assert.ok(!JSON.stringify(p.p_envelope).includes(p.p_secret));rotated=p.p_secret;envelope=p.p_envelope;if(lost)return route.fulfill({status:503,headers,body:'{}'});body={status:'committed',revision:1};}
   else if(path==='/rest/v1/rpc/test_device_rotation_result'){body={status:'committed',revision:1};}
   else if(path==='/rest/v1/rpc/test_device_vault_read'){const p=req.postDataJSON();body=p.p_password===password?(envelope?{state:'saved',envelope}:{state:'missing'}):null;}
   else throw Error(`unexpected request ${path}`);
   return route.fulfill({status:200,headers,body:JSON.stringify(body)});
  });
  await page.addInitScript(({s,live})=>localStorage.setItem(live?'sb-xypisxdlxcqyavixdiji-auth-token':'sb-example-auth-token',JSON.stringify(s)),{s:session,live});
  await page.goto(origin);await page.getByRole('button',{name:/TEST/}).click();
  if(!live) {
   const now=page.getByLabel('现在（设备待确认数）');await now.waitFor();await page.waitForFunction(()=>document.querySelector('[aria-label="现在（设备待确认数）"]')?.value==='426');
   assert.equal(await now.getAttribute('readonly'),'');await page.getByLabel('手动输入').fill('7');
   await page.waitForTimeout(700);assert.equal(await now.inputValue(),'426');assert.equal(await page.getByLabel('手动输入').inputValue(),'7');
   assert.equal(await page.getByRole('button',{name:'手动确认'}).count(),1);
   await page.getByRole('button',{name:'手动确认'}).click();
   await page.getByText('管理员替换当前生产数').waitFor();assert.equal(manualRequest,null);
   await page.getByLabel('管理员密码').fill('wrong');await page.getByRole('button',{name:'确认替换'}).click();await page.getByText('管理员密码错误或暂时锁定，请稍后再试。').waitFor();
   assert.equal(manualCurrent,0);await page.getByRole('button',{name:'关闭提示'}).click();
   await page.getByLabel('管理员密码').fill(password);await page.getByRole('button',{name:'确认替换'}).click();await page.getByText('当前生产数已替换。').waitFor();
   assert.equal(manualRequest.p_target,7);assert.equal(manualRequest.p_expected,0);assert.equal(manualRequest.p_order,order);assert.equal(manualCurrent,7);
   await page.waitForFunction(()=>document.querySelector('[aria-label="现在（设备待确认数）"]')?.value==='426');assert.equal(await now.inputValue(),'426');await page.getByRole('button',{name:'关闭提示'}).click();
   const fit=await page.evaluate(()=>{const selectors=['[aria-label="现在（设备待确认数）"]','#amount','button[form="submit"]'];return selectors.map(s=>{const r=document.querySelector(s).getBoundingClientRect();return {top:r.top,bottom:r.bottom,left:r.left,right:r.right,w:innerWidth,h:innerHeight}})});
   assert.ok(fit.every(r=>r.top>=0&&r.left>=0&&r.bottom<=r.h&&r.right<=r.w),JSON.stringify({size,fit}));
   if(size.width===390 && !lost) {
    await page.getByLabel('手动输入').fill('3');await page.getByRole('button',{name:'手动确认'}).click();
    manualCurrent=8;await page.getByLabel('管理员密码').fill(password);await page.getByRole('button',{name:'确认替换'}).click();
    await page.getByText('当前生产数已改变，本次未替换，请核对后重新输入。').waitFor();assert.equal(manualCurrent,8);
    await page.getByRole('button',{name:'关闭提示'}).click();await page.getByLabel('手动输入').fill('3');await page.getByRole('button',{name:'手动确认'}).click();
    manualLost=true;await page.getByLabel('管理员密码').fill(password);await page.getByRole('button',{name:'确认替换'}).click();
    await page.getByText('替换结果未知，请先核对当前生产数并联系管理员检查记录，不要直接重复提交。').waitFor();assert.equal(manualCurrent,3);
    await page.getByRole('button',{name:'关闭提示'}).click();
   }
  }
  await page.getByRole('button',{name:'设备预览'}).click();
  await page.getByRole('button',{name:'设备 ID / 密钥'}).click();assert.equal(await page.getByLabel('设备 ID').inputValue(),device);const menuMetrics=await page.evaluate(()=>({h:document.documentElement.scrollHeight,w:document.documentElement.scrollWidth,ih:innerHeight,iw:innerWidth}));assert.ok(menuMetrics.h<=menuMetrics.ih+1&&menuMetrics.w<=menuMetrics.iw+1,JSON.stringify({size,menuMetrics}));
  await page.getByRole('button',{name:'保存现有密钥'}).click();await page.getByLabel('现有设备密钥').fill(secret);await page.getByLabel('管理员密码').fill(password);await page.getByRole('button',{name:'加密并保存'}).click();
  await page.getByText('已加密保存。',{exact:false}).waitFor();
  await page.getByRole('button',{name:'找回密钥'}).click();await page.getByLabel('管理员密码').fill('wrong');await page.getByRole('button',{name:'解锁'}).click();await page.getByText('管理员密码错误或暂时锁定。').waitFor();
  await page.getByLabel('管理员密码').fill(password);await page.getByRole('button',{name:'解锁'}).click();await page.getByLabel('设备密钥',{exact:true}).waitFor();
  assert.equal(await page.getByLabel('设备密钥',{exact:true}).inputValue(),secret);assert.equal(await page.getByLabel('设备密钥',{exact:true}).getAttribute('type'),'password');
  assert.equal(await page.evaluate(s=>JSON.stringify({...localStorage,...sessionStorage}).includes(s),secret),false);
  const metrics=await page.evaluate(()=>({h:document.documentElement.scrollHeight,w:document.documentElement.scrollWidth,ih:innerHeight,iw:innerWidth}));assert.ok(metrics.h<=metrics.ih+1&&metrics.w<=metrics.iw+1,JSON.stringify({size,metrics}));
  await page.getByRole('button',{name:'← 返回设备凭据'}).click();assert.equal(await page.getByLabel('设备密钥',{exact:true}).count(),0);
  await page.getByRole('button',{name:'自动生成并更换密钥'}).click();const formMetrics=await page.evaluate(()=>({h:document.documentElement.scrollHeight,w:document.documentElement.scrollWidth,ih:innerHeight,iw:innerWidth}));assert.ok(formMetrics.h<=formMetrics.ih+1&&formMetrics.w<=formMetrics.iw+1,JSON.stringify({size,formMetrics}));assert.equal(await page.getByRole('button',{name:'确认更换'}).isDisabled(),true);
  await page.getByLabel('我已暂停按键，并准备更新开发板').check();await page.getByLabel('管理员密码').fill(password);await page.getByRole('button',{name:'确认更换'}).click();
  if(lost){
   await page.getByText('更换结果未知：',{exact:false}).waitFor();await page.reload();
   await page.getByRole('button',{name:/TEST/}).click();await page.getByRole('button',{name:'设备预览'}).click();await page.getByRole('button',{name:'设备 ID / 密钥'}).click();
   await page.getByRole('button',{name:'核查更换结果'}).click();await page.getByLabel('管理员密码').fill(password);await page.getByRole('button',{name:'核查',exact:true}).click();await page.getByText('已核实更换成功',{exact:false}).waitFor();
  } else await page.getByText('新密钥已生效并加密保存。',{exact:false}).waitFor();
  assert.equal(await page.getByLabel('设备密钥',{exact:true}).inputValue(),rotated);
  const rotateMetrics=await page.evaluate(()=>({h:document.documentElement.scrollHeight,w:document.documentElement.scrollWidth,ih:innerHeight,iw:innerWidth}));assert.ok(rotateMetrics.h<=rotateMetrics.ih+1&&rotateMetrics.w<=rotateMetrics.iw+1,JSON.stringify({size,rotateMetrics}));
  await context.close();
 }
 console.log('device vault UI save/unlock and mobile regression passed');
}finally{await browser.close();}
