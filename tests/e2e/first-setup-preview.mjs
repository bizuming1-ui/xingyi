import assert from 'node:assert/strict';
import {chromium} from 'playwright-core';
import chromiumBinary from '@sparticuz/chromium';
const payload=Buffer.from(JSON.stringify({sub:'01234567-89ab-4cde-8fab-0123456789ab',role:'authenticated',exp:Math.floor(Date.now()/1000)+3600})).toString('base64url');
const session={access_token:`eyJhbGciOiJub25lIn0.${payload}.signature`,refresh_token:'fixture-refresh',expires_at:Math.floor(Date.now()/1000)+3600,expires_in:3600,token_type:'bearer',user:{id:'01234567-89ab-4cde-8fab-0123456789ab',email:'fixture@example.invalid',aud:'authenticated',role:'authenticated'}};
const browser=await chromium.launch({executablePath:await chromiumBinary.executablePath(),args:chromiumBinary.args.filter(x=>x!=='--single-process'),headless:true});
try {
 const context=await browser.newContext({viewport:{width:390,height:844}});const page=await context.newPage();let allowed=false,setupCalls=0;
 await page.route('**/*',r=>['127.0.0.1','localhost'].includes(new URL(r.request().url()).hostname)?r.continue():r.abort());await page.routeWebSocket('**/*',ws=>ws.close());
 await page.route('https://example.supabase.co/**',async route=>{
  const req=route.request(),path=new URL(req.url()).pathname,headers={'access-control-allow-origin':'*','content-type':'application/json'};
  if(req.method()==='OPTIONS')return route.fulfill({status:204,headers:{...headers,'access-control-allow-methods':'GET,POST,OPTIONS','access-control-allow-headers':'*'}});
  if(path==='/auth/v1/user')return route.fulfill({status:200,headers,body:JSON.stringify(session.user)});
  if(path==='/rest/v1/rpc/is_operator')return route.fulfill({status:200,headers,body:JSON.stringify(allowed)});
  if(path==='/rest/v1/rpc/test_initial_setup'){setupCalls++;assert.deepEqual(req.postDataJSON(),{p_password:'fixture-test-password'});allowed=true;return route.fulfill({status:200,headers,body:'true'});}
  if(path==='/rest/v1/orders')return route.fulfill({status:200,headers,body:'[]'});
  throw Error(`unexpected ${path}`);
 });
 await page.addInitScript(s=>localStorage.setItem('sb-example-auth-token',JSON.stringify(s)),session);
 await page.goto(process.env.TEST_BASE_URL);await page.getByText('隔离测试 · 首次设置').waitFor();
 await page.getByLabel('测试管理员密码').fill('fixture-test-password');await page.getByLabel('再次输入').fill('fixture-test-password');
 await page.getByRole('button',{name:'完成设置'}).click();await page.getByText('暂无生产单，请由管理员新建。').waitFor();
 assert.equal(setupCalls,1);assert.equal(await page.getByText('界面1 · 隔离测试').count(),1);
 assert.equal(await page.locator('img[alt^="扫描进入正式"]').count(),0);
 assert.equal(await page.evaluate(()=>JSON.stringify({...localStorage}).includes('fixture-test-password')),false);
 await context.close();console.log('test-only first setup UI passed');
} finally {await browser.close();}
