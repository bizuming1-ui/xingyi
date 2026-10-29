import assert from 'node:assert/strict';
import { chromium } from 'playwright-core';
import chromiumBinary from '@sparticuz/chromium';
import jsQR from 'jsqr';
import {PNG} from 'pngjs';

const origin = process.env.TEST_BASE_URL || 'http://127.0.0.1:5174';
const browser = await chromium.launch({executablePath: await chromiumBinary.executablePath(), args: chromiumBinary.args.filter(x=>x!=='--single-process'), headless:true});
const payload = Buffer.from(JSON.stringify({sub:'01234567-89ab-4cde-8fab-0123456789ab',role:'authenticated',exp:Math.floor(Date.now()/1000)+3600})).toString('base64url');
const session = {access_token:`eyJhbGciOiJub25lIn0.${payload}.signature`,refresh_token:'fixture-refresh',expires_at:Math.floor(Date.now()/1000)+3600,expires_in:3600,token_type:'bearer',user:{id:'01234567-89ab-4cde-8fab-0123456789ab',email:'fixture@example.invalid',aud:'authenticated',role:'authenticated'}};
const mockId='d0000000-0000-4000-8000-000000000001';
const fixtureOrder = {id:mockId,order_no:'TEST-ORDER',product:'测试产品',total:100,current_count:95,archived:false,created_at:'2026-01-01T00:00:00Z'};
let current=95, calls=0, firstRequestId=null, appliedEvents=0;

async function installMocks(page) {
 await page.route('**/*',route=>{const host=new URL(route.request().url()).hostname; return ['127.0.0.1','localhost'].includes(host) ? route.continue() : route.abort();});
 await page.routeWebSocket('**/*', ws=>ws.close());
 await page.route('https://example.supabase.co/**',async route=>{
   const request=route.request(), path=new URL(request.url()).pathname;
   const headers={'access-control-allow-origin':'*','content-type':'application/json'};
   if(request.method()==='OPTIONS') return route.fulfill({status:204,headers:{...headers,'access-control-allow-methods':'GET,POST,OPTIONS','access-control-allow-headers':'*'}});
   if(path==='/rest/v1/rpc/is_operator') return route.fulfill({status:200,headers,body:'true'});
   if(path==='/rest/v1/orders') return route.fulfill({status:200,headers,body:JSON.stringify([{...fixtureOrder,current_count:current}])});
   if(path==='/rest/v1/rpc/submit_production') {
     const body=request.postDataJSON(); assert.equal(body.p_order,mockId); assert.equal(body.p_amount,10); assert.match(body.p_request,/^[0-9a-f-]{36}$/);
     calls++;
     if(!firstRequestId){firstRequestId=body.p_request; appliedEvents++; current=100; return route.abort('failed');}
     assert.equal(body.p_request,firstRequestId,'retry must use the original idempotency key');
     return route.fulfill({status:200,headers,body:JSON.stringify({applied:5,current:100})});
   }
   if(path==='/rest/v1/rpc/admin_history') return route.fulfill({status:200,headers,body:JSON.stringify([{created_at:'2026-01-01T00:00:00Z',requested:10,applied:5,resulting_count:100}])});
   if(path==='/auth/v1/user') return route.fulfill({status:200,headers,body:JSON.stringify(session.user)});
   if(path==='/auth/v1/token') return route.fulfill({status:400,headers,body:JSON.stringify({code:'invalid_credentials',message:'Invalid login credentials'})});
   console.error('Unexpected backend request:',request.method(),path);
   return route.fulfill({status:404,headers,body:'{}'});
 });
}
async function assertSingleScreen(page, label) {
 const dimensions=await page.evaluate(()=>({viewport:innerHeight, content:document.documentElement.scrollHeight, width:innerWidth, contentWidth:document.documentElement.scrollWidth}));
 assert.ok(dimensions.content<=dimensions.viewport+1,`${label}: vertical overflow ${JSON.stringify(dimensions)}`);
 assert.ok(dimensions.contentWidth<=dimensions.width+1,`${label}: horizontal overflow ${JSON.stringify(dimensions)}`);
}
try {
 const context=await browser.newContext({viewport:{width:390,height:844},isMobile:true,hasTouch:true});
 const page=await context.newPage(); await installMocks(page);
 await page.addInitScript(session=>localStorage.setItem('sb-example-auth-token',JSON.stringify(session)),session);
 await page.goto(origin); await page.getByRole('button',{name:/TEST-ORDER/}).waitFor();
 await assertSingleScreen(page,'list');
 const qrData=await page.locator('img[alt="扫描进入正式生产单列表"]').getAttribute('src');
 const png=PNG.sync.read(Buffer.from(qrData.split(',')[1],'base64'));
 const decoded=jsQR(new Uint8ClampedArray(png.data),png.width,png.height);
 assert.equal(decoded?.data,`${origin}/`,'QR must encode the actual application URL');
 await page.getByRole('button',{name:/TEST-ORDER/}).click();
 await page.getByLabel('现在',{exact:true}).waitFor();
 assert.equal(await page.locator('.field').filter({hasText:'当前生产数'}).locator('input').inputValue(),'95');
 await assertSingleScreen(page,'order');
 await page.getByLabel('现在',{exact:true}).fill('10');
 await page.getByRole('button',{name:'确认',exact:true}).click();
 await page.getByText(/提交未确认/).waitFor();
 await page.reload();
 await page.getByRole('button',{name:'重试确认'}).click();
 await page.getByText('已累计 5，超过生产总数的部分未累计。').waitFor();
 assert.equal(await page.getByLabel('现在',{exact:true}).inputValue(),'');
 assert.equal(await page.locator('.field').filter({hasText:'当前生产数'}).locator('input').inputValue(),'100');
 assert.equal(calls,2); assert.equal(appliedEvents,1);
 await page.getByRole('button',{name:'关闭提示'}).click();
 await page.getByRole('button',{name:/返回生产单列表/}).click();
 await page.getByRole('button',{name:/管理员：查看归档/}).click();
 await assertSingleScreen(page,'archive');
 await page.getByLabel('管理员密码').fill('fixture-only-password');
 await page.getByRole('button',{name:'查看记录'}).click();
 await page.getByText(/输入 10 · 实际 5 · 累计 100/).waitFor();
 await assertSingleScreen(page,'history');
 const loginContext=await browser.newContext({viewport:{width:1280,height:720}});
 const login=await loginContext.newPage(); await installMocks(login);
 await login.goto(origin);
 await login.getByLabel('邮箱').fill('fixture@example.invalid'); await login.getByLabel('密码').fill('bad-password');
 await login.getByRole('button',{name:'登录',exact:true}).click();
 await login.getByText(/登录失败：账号尚未创建/).waitFor();
 await assertSingleScreen(login,'login');
 await loginContext.close();
 await context.close();
 console.log('PR1 mocked browser regression passed (mobile & desktop)');
} finally {await browser.close()}
