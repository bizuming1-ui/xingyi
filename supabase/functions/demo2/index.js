// TEST PROJECT ONLY. Serves only the pinned public ESP32 download page.
const commit='d9d46e36243f4be230df442a6dc3c374488e5a95';
const expectedHash='62694294bd254cd3873df17e77e5b246c2f29aa3f923541d39bbdcb73e205708';
const expectedSize=299534;
const source=`https://api.github.com/repos/bizuming1-ui/xingyi/contents/public-device/index.html?ref=${commit}`;
const headers={
 'content-type':'text/html; charset=utf-8',
 'cache-control':'public, max-age=300',
 'x-content-type-options':'nosniff',
 'referrer-policy':'no-referrer',
 'content-security-policy':"default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'"
};
export function makeHandler({fetcher=fetch,hash=expectedHash,size=expectedSize,url=source}={}) {
 let cached=null,inFlight=null;
 async function load() {
  if(cached)return cached;
  if(inFlight)return inFlight;
  inFlight=(async()=>{
   const response=await fetcher(url,{headers:{Accept:'application/vnd.github+json','User-Agent':'xingyi-isolated-demo'},redirect:'error',signal:AbortSignal.timeout(8000)});
   if(!response.ok)throw Error('artifact unavailable');
   const length=Number(response.headers.get('content-length')||0);if(length>900000)throw Error('artifact response too large');
   const item=await response.json();
   if(item.encoding!=='base64' || item.size!==size || typeof item.content!=='string' || item.content.length>700000)throw Error('unexpected artifact');
   const raw=atob(item.content.replace(/\s/g,''));if(raw.length!==size)throw Error('unexpected artifact length');
   const bytes=Uint8Array.from(raw,c=>c.charCodeAt(0));
   const digest=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),x=>x.toString(16).padStart(2,'0')).join('');
   if(digest!==hash)throw Error('artifact integrity mismatch');
   cached=bytes;return bytes;
  })();
  try{return await inFlight;}finally{inFlight=null;}
 }
 return async request=>{
  const path=new URL(request.url).pathname;
  if(!['/demo2','/demo2/','/functions/v1/demo2','/functions/v1/demo2/'].includes(path))return new Response('Not found',{status:404});
  if(request.method!=='GET' && request.method!=='HEAD')return new Response('Method not allowed',{status:405,headers:{allow:'GET, HEAD'}});
  try {const bytes=await load();return new Response(request.method==='HEAD'?null:bytes,{status:200,headers});}
  catch {return new Response('Test page temporarily unavailable. Please retry.',{status:503,headers:{'cache-control':'no-store'}});}
 };
}
if(typeof Deno!=='undefined')Deno.serve(makeHandler());
