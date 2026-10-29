import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {makeHandler} from '../supabase/functions/demo1/index.js';
const bytes=Buffer.from('<!doctype html><h1>fixture</h1>');
const hash=createHash('sha256').update(bytes).digest('hex');
const item={encoding:'base64',size:bytes.length,content:bytes.toString('base64')};
const req=(path='/functions/v1/demo1/',method='GET')=>new Request('https://example.supabase.co'+path,{method});
test('demo handler serves only pinned, verified static HTML and caches it',async()=>{
 let calls=0;const handler=makeHandler({url:'https://api.github.com/fixture',hash,size:bytes.length,fetcher:async(url,opts)=>{calls++;assert.equal(url,'https://api.github.com/fixture');assert.equal(opts.redirect,'error');return Response.json(item);}});
 assert.equal((await handler(req('/private'))).status,404);assert.equal((await handler(req('/functions/v1/demo1/','POST'))).status,405);assert.equal(calls,0);
 const response=await handler(req());assert.equal(response.status,200);assert.equal(await response.text(),bytes.toString());assert.match(response.headers.get('content-security-policy'),/frame-ancestors 'none'/);
 assert.equal((await handler(req('/demo1','HEAD'))).status,200);assert.equal(calls,1);
});
test('invalid or unavailable artifacts never reach the browser',async()=>{
 for(const data of [{...item,content:Buffer.from('bad').toString('base64')},{...item,encoding:'none'},item]) {
  let calls=0;const handler=makeHandler({hash:data===item?'0'.repeat(64):hash,size:bytes.length,fetcher:async()=>{calls++;return Response.json(data);}});
  assert.equal((await handler(req())).status,503);assert.equal((await handler(req())).status,503);assert.equal(calls,2);
 }
 const handler=makeHandler({hash,size:bytes.length,fetcher:async()=>{throw Error('offline')}});assert.equal((await handler(req())).status,503);
});
