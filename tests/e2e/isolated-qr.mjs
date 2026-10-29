import assert from 'node:assert/strict';
import {chromium} from 'playwright-core';
import chromiumBinary from '@sparticuz/chromium';
import jsQR from 'jsqr';
import {PNG} from 'pngjs';
const payload=Buffer.from(JSON.stringify({sub:'01234567-89ab-4cde-8fab-0123456789ab',role:'authenticated',exp:Math.floor(Date.now()/1000)+3600})).toString('base64url');
const session={access_token:`eyJhbGciOiJub25lIn0.${payload}.signature`,refresh_token:'fixture-refresh',expires_at:Math.floor(Date.now()/1000)+3600,expires_in:3600,token_type:'bearer',user:{id:'01234567-89ab-4cde-8fab-0123456789ab',email:'fixture@example.invalid',aud:'authenticated',role:'authenticated'}};
const browser=await chromium.launch({executablePath:await chromiumBinary.executablePath(),args:chromiumBinary.args.filter(x=>x!=='--single-process'),headless:true});
try {
 const page=await browser.newPage({viewport:{width:390,height:844}});
 const artifact=process.env.DEMO_ARTIFACT==='true';
 const apiHost=artifact?'https://xypisxdlxcqyavixdiji.supabase.co/**':'https://example.supabase.co/**';
 await page.route('**/*',r=>['127.0.0.1','localhost'].includes(new URL(r.request().url()).hostname)?r.continue():r.abort());await page.routeWebSocket('**/*',ws=>ws.close());
 await page.route(apiHost,async route=>{
  const req=route.request(),path=new URL(req.url()).pathname,headers={'access-control-allow-origin':'*','content-type':'application/json'};
  if(req.method()==='OPTIONS')return route.fulfill({status:204,headers:{...headers,'access-control-allow-methods':'GET,POST,OPTIONS','access-control-allow-headers':'*'}});
  if(path==='/auth/v1/user')return route.fulfill({status:200,headers,body:JSON.stringify(session.user)});
  if(path==='/rest/v1/rpc/is_operator')return route.fulfill({status:200,headers,body:'true'});
  if(path==='/rest/v1/orders')return route.fulfill({status:200,headers,body:JSON.stringify([{id:'d0000000-0000-4000-8000-000000000001',order_no:'TEST',product:'测试',total:100,current_count:0,archived:false,created_at:'2026-01-01T00:00:00Z'}])});
  throw Error(`unexpected ${path}`);
 });
 await page.addInitScript(s=>localStorage.setItem(location.pathname.includes('public-demo')?'sb-xypisxdlxcqyavixdiji-auth-token':'sb-example-auth-token',JSON.stringify(s)),session);await page.goto(process.env.TEST_BASE_URL+(artifact?'/public-demo/index.html':''));
 const img=page.getByAltText('扫描进入隔离测试列表');await img.waitFor();
 const src=await img.getAttribute('src');const png=PNG.sync.read(Buffer.from(src.split(',')[1],'base64'));const decoded=jsQR(new Uint8ClampedArray(png.data),png.width,png.height);
 assert.equal(decoded?.data,'https://bizuming1-ui.github.io/xingyi-isolated-test/');
 await page.getByRole('button',{name:/TEST/}).click();await page.getByAltText('扫描进入隔离测试列表').waitFor();
 console.log(artifact?'public demo artifact QR passed':'isolated public QR encodes isolated demo URL');
} finally {await browser.close();}
