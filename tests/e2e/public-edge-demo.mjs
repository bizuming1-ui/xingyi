import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {chromium} from 'playwright-core';
import chromiumBinary from '@sparticuz/chromium';
import jsQR from 'jsqr';
import {PNG} from 'pngjs';
const origin='https://xypisxdlxcqyavixdiji.supabase.co',url=origin+'/functions/v1/demo1/';
const html=await readFile(resolve('public-demo/index.html'),'utf8');
const payload=Buffer.from(JSON.stringify({sub:'01234567-89ab-4cde-8fab-0123456789ab',role:'authenticated',exp:Math.floor(Date.now()/1000)+3600})).toString('base64url');
const session={access_token:`eyJhbGciOiJub25lIn0.${payload}.signature`,refresh_token:'fixture-refresh',expires_at:Math.floor(Date.now()/1000)+3600,expires_in:3600,token_type:'bearer',user:{id:'01234567-89ab-4cde-8fab-0123456789ab',email:'fixture@example.invalid',aud:'authenticated',role:'authenticated'}};
const browser=await chromium.launch({executablePath:await chromiumBinary.executablePath(),args:chromiumBinary.args.filter(x=>x!=='--single-process'),headless:true});
try {
 for(const viewport of [{width:390,height:844},{width:320,height:568},{width:568,height:320}]) {
  const context=await browser.newContext({viewport,isMobile:true,hasTouch:true});const page=await context.newPage();const errors=[];
  page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error' && m.text().includes('Content Security Policy'))errors.push(m.text());});
  await page.route('**/*',async route=>{
   const req=route.request(),u=new URL(req.url());if(u.origin!==origin)return route.abort();
   const headers={'content-type':'application/json'};
   if(u.pathname==='/functions/v1/demo1/' && process.env.PUBLIC_EDGE_LIVE==='1')return route.continue();
   if(u.pathname==='/functions/v1/demo1/')return route.fulfill({status:200,headers:{'content-type':'text/html; charset=utf-8','content-security-policy':"default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self' wss://xypisxdlxcqyavixdiji.supabase.co; object-src 'none'; base-uri 'none'; frame-ancestors 'none'"},body:html});
   if(u.pathname==='/auth/v1/user')return route.fulfill({status:200,headers,body:JSON.stringify(session.user)});
   if(u.pathname==='/rest/v1/rpc/is_operator')return route.fulfill({status:200,headers,body:'true'});
   if(u.pathname==='/rest/v1/orders')return route.fulfill({status:200,headers,body:JSON.stringify([{id:'d0000000-0000-4000-8000-000000000001',order_no:'TEST',product:'测试',total:100,current_count:0,archived:false,created_at:'2026-01-01T00:00:00Z'}])});
   throw Error(`unexpected ${u.pathname}`);
  });
  await page.routeWebSocket('**/*',ws=>ws.close());
  await page.addInitScript(s=>localStorage.setItem('sb-xypisxdlxcqyavixdiji-auth-token',JSON.stringify(s)),session);
  await page.goto(url);await page.getByText('界面1 · 隔离测试').waitFor();
  const img=page.getByAltText('扫描进入隔离测试列表');await img.waitFor();const src=await img.getAttribute('src');
  const png=PNG.sync.read(Buffer.from(src.split(',')[1],'base64'));assert.equal(jsQR(new Uint8ClampedArray(png.data),png.width,png.height)?.data,url);
  await page.getByRole('button',{name:/TEST/}).click();await page.getByAltText('扫描进入隔离测试列表').waitFor();
  const m=await page.evaluate(()=>({h:document.documentElement.scrollHeight,w:document.documentElement.scrollWidth,ih:innerHeight,iw:innerWidth}));
  assert.ok(m.h<=m.ih+1 && m.w<=m.iw+1,`overflow ${JSON.stringify({viewport,m})}`);assert.deepEqual(errors,[]);
  await context.close();
 }
 console.log('public demo artifact, CSP and mobile QR regression passed');
} finally {await browser.close();}
