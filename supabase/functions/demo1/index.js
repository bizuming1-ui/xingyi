// TEST PROJECT ONLY. Public, immutable-by-hash HTML; no project secret, device
// credentials or user data are read or logged. The public GitHub API is the
// source of a pinned, independently SHA-256-verified build artifact.
const commit='b6287bd463d56b8fa5845dddc027eaebd0fdf3b3';
const expectedHash='0b61902c0b9d04236299a9b8401068896d90e994677ed4ea386f18c3bd93d986';
const expectedSize=432765;
const source=`https://api.github.com/repos/bizuming1-ui/xingyi/contents/public-demo/index.html?ref=${commit}`;
const headers={
 'content-type':'text/html; charset=utf-8',
 'cache-control':'public, max-age=300',
 'x-content-type-options':'nosniff',
 'referrer-policy':'no-referrer',
 'content-security-policy':"default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self' wss://xypisxdlxcqyavixdiji.supabase.co; object-src 'none'; base-uri 'none'; frame-ancestors 'none'"
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
  if(!['/demo1','/demo1/','/functions/v1/demo1','/functions/v1/demo1/'].includes(path))return new Response('Not found',{status:404});
  if(request.method!=='GET' && request.method!=='HEAD')return new Response('Method not allowed',{status:405,headers:{allow:'GET, HEAD'}});
  try {const bytes=await load();return new Response(request.method==='HEAD'?null:bytes,{status:200,headers});}
  catch {return new Response('Test page temporarily unavailable. Please retry.',{status:503,headers:{'cache-control':'no-store'}});}
 };
}
if(typeof Deno!=='undefined')Deno.serve(makeHandler());
