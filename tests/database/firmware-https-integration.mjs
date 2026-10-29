// End-to-end component test: isolated PGlite, local TLS, real firmware network
// and count logic, simulated Wi-Fi/GPIO/NVS. No real Supabase or device.
import {PGlite} from '@electric-sql/pglite';
import {pgcrypto} from '@electric-sql/pglite/contrib/pgcrypto';
import https from 'node:https';
import {spawn,execFileSync} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdtempSync,rmSync,readFileSync,writeFileSync} from 'node:fs';
import {readFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import assert from 'node:assert/strict';
const root=resolve(fileURLToPath(new URL('../..',import.meta.url)));
const mbed=process.env.MBEDTLS_DIR, include=process.env.ARDUINOJSON_INCLUDE;
if(!mbed || !include)throw Error('MBEDTLS_DIR and ARDUINOJSON_INCLUDE required');
const temp=mkdtempSync(join(tmpdir(),'xingyi-https-db-'));
const db=await PGlite.create({extensions:{pgcrypto}});let child,server;
const user='aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa',a='11111111-1111-4111-8111-111111111111',b='22222222-2222-4222-8222-222222222222';
const one=async(sql,args=[]) => (await db.query(sql,args)).rows[0];
async function role(name){await db.exec('reset role');await db.query("select set_config('request.jwt.claim.sub',$1,false)",[user]);await db.exec(`set role ${name}`);}
const openssl=(args)=>execFileSync('openssl',args,{stdio:'ignore'});
try {
 execFileSync('g++',['-std=c++17','-Wall','-Wextra','-Werror','-pthread','-I',join(root,'tests/mocks'),'-I',include,'-I',join(mbed,'include'),join(root,'tests/firmware_https_database_driver.cpp'),'-L',join(mbed,'library'),'-lmbedtls','-lmbedx509','-lmbedcrypto','-o',join(temp,'driver')]);
 openssl(['req','-x509','-newkey','rsa:2048','-nodes','-days','2','-subj','/CN=Local test CA','-keyout',join(temp,'ca.key'),'-out',join(temp,'ca.crt')]);
 openssl(['req','-newkey','rsa:2048','-nodes','-subj','/CN=project123.supabase.co','-keyout',join(temp,'server.key'),'-out',join(temp,'server.csr')]);
 writeFileSync(join(temp,'ext'),'subjectAltName=DNS:project123.supabase.co\nextendedKeyUsage=serverAuth\n');
 openssl(['x509','-req','-in',join(temp,'server.csr'),'-CA',join(temp,'ca.crt'),'-CAkey',join(temp,'ca.key'),'-CAcreateserial','-days','1','-extfile',join(temp,'ext'),'-out',join(temp,'server.crt')]);
 await db.exec(`create role anon;create role authenticated;create schema auth;create schema extensions;create table auth.users(id uuid primary key);
 create function auth.uid() returns uuid language sql stable as $$ select nullif(current_setting('request.jwt.claim.sub',true),'')::uuid $$;create publication supabase_realtime;`);
 for(const path of ['supabase/schema.sql','supabase/migrations/202610290001_device_ingest.sql','supabase/migrations/202610290002_device_confirmation.sql','supabase/migrations/202610290003_device_confirmation_recovery.sql','supabase/migrations/202610290004_device_time_proof.sql'])await db.exec(await readFile(join(root,path),'utf8'));
 await db.query('insert into auth.users(id) values($1)',[user]);
 await db.query("insert into public.app_settings(id,shared_user_id,admin_password_hash) values(true,$1,extensions.crypt('fixture-only',extensions.gen_salt('bf')))",[user]);
 await db.query("insert into public.orders(id,order_no,product,total,current_count) values($1,'A','fixture',100,0),($2,'B','fixture',1,0)",[a,b]);
 await role('authenticated');const created=(await one('select public.device_create($1,$2) as x',['fixture-only','integration fixture'])).x;
 const bindA=(await one('select public.device_request_binding($1,$2) as x',[created.id,a])).x;
 const methods={device_time_proof:['p_device','p_nonce'],device_status:['p_secret'],device_ingest:['p_secret','p_binding','p_epoch','p_total'],
  device_confirmation_freeze:['p_secret','p_barrier','p_binding','p_epoch','p_total'],device_confirmation_status:['p_secret','p_barrier'],
  device_confirmation_renew:['p_secret','p_barrier','p_binding','p_epoch','p_total','p_nonce'],device_confirmation_abort:['p_secret','p_barrier']};
 let loseNextIngest=false,proofCalls=0;const requestErrors=[];
 server=https.createServer({key:readFileSync(join(temp,'server.key')),cert:readFileSync(join(temp,'server.crt'))},async(req,res)=>{
  try {
   assert.equal(req.method,'POST');assert.equal(req.headers.host,'project123.supabase.co');assert.equal(req.headers.apikey,'sb_publishable_test');
   const match=/^\/rest\/v1\/rpc\/([a-z_]+)$/.exec(req.url);assert.ok(match);const endpoint=match[1];const params=methods[endpoint];assert.ok(params);
   let raw='';for await(const chunk of req){raw+=chunk;if(raw.length>2048)throw Error('fixture request too large');}
   const body=JSON.parse(raw);assert.deepEqual(Object.keys(body).sort(),params.slice().sort());
   if(endpoint==='device_time_proof'){assert.equal(raw.includes(created.secret),false);proofCalls++;}
   await role('anon');const result=(await one(`select public.${endpoint}(${params.map((_,i)=>`$${i+1}`).join(',')}) as x`,params.map(k=>body[k]))).x;
   if(loseNextIngest && endpoint==='device_ingest'){loseNextIngest=false;req.socket.destroy();return;}
   const content=JSON.stringify(result);res.writeHead(200,{'Content-Type':'application/json','Content-Length':Buffer.byteLength(content)});res.end(content);
  } catch(e){requestErrors.push(e.message);if(!res.headersSent)res.writeHead(500,{'Content-Length':0});res.end();}
 });
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 child=spawn(join(temp,'driver'),[],{stdio:['pipe','pipe','inherit']});const lines=createInterface({input:child.stdout})[Symbol.asyncIterator]();
 child.stdin.write(JSON.stringify({secret:created.secret,device_id:created.id,ca:join(temp,'ca.crt'),port:server.address().port})+'\n');
 async function command(action,now=0,pressed=false){
  child.stdin.write(JSON.stringify({action,now,pressed})+'\n');let timer;
  try {const item=await Promise.race([lines.next(),new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('driver timed out')),20000);})]);
   if(item.done)throw Error('driver exited unexpectedly');const result=JSON.parse(item.value);assert.equal(result.fault,false);return result;
  }finally{clearTimeout(timer);}
 }
 async function step(now){return command('step',now);}
 async function exchange(now){assert.equal((await step(now)).queued,true);await command('work');return step(now+20);}
 async function press(now,hold=false){await command('press',now,true);let result=await command('press',now+25,true);assert.equal(result.accepted,1);if(hold){result=await command('press',now+525,true);assert.equal(result.accepted,1);}await command('press',now+(hold?530:50),false);await command('press',now+(hold?555:75),false);}
 async function draft(id){await role('authenticated');return (await one('select count from public.device_order_drafts where order_id=$1',[id]))?.count??0;}
 await exchange(0);let state=await exchange(200);assert.equal(state.order,a);assert.equal(state.pending,0);assert.ok(proofCalls>=1);
 await command('offline');await press(300,true);await role('authenticated');
 const offlineBarrier=(await one('select public.device_confirmation_request($1,$2,$3) as x',[created.id,a,'30000000-0000-4000-8000-000000000003'])).x;
 await assert.rejects(()=>db.query('select public.device_confirmation_commit($1)',[offlineBarrier]),/设备未冻结同步或续约已过期/);
 assert.equal((await one('select public.device_confirmation_cancel($1) as x',[offlineBarrier])).x,true);
 const manual=(await one('select public.submit_production($1,$2,$3) as x',[a,1,'c0000000-0000-4000-8000-000000000001'])).x;
 assert.equal(manual.applied,1);assert.equal(await draft(a),0); // manual is independent of offline device counts
 const bindB=(await one('select public.device_request_binding($1,$2) as x',[created.id,b])).x;assert.notEqual(bindA,bindB);
 state=await exchange(900);assert.equal(state.pending,2);assert.equal(state.order,a);assert.equal(await draft(a),0);
 await command('online');assert.ok(proofCalls>=2);
 loseNextIngest=true;state=await exchange(1100);assert.equal(state.pending,2);assert.equal(await draft(a),2);
 state=await exchange(1300);assert.equal(state.pending,0);assert.equal(await draft(a),2);
 await exchange(1500);state=await exchange(1700);assert.equal(state.order,b);assert.equal(await draft(b),0);
 await press(1750);state=await exchange(1900);assert.equal(state.pending,0);assert.equal(await draft(b),1);
 await role('authenticated');const barrier1=(await one('select public.device_confirmation_request($1,$2,$3) as x',[created.id,b,'30000000-0000-4000-8000-000000000001'])).x;
 await exchange(2100);state=await exchange(2300);assert.equal(state.frozen,true);
 await press(2350);state=await exchange(2500);assert.equal(state.frozen,false);assert.equal(state.pending,1);
 await role('authenticated');assert.equal((await one('select public.device_confirmation_view($1) as x',[barrier1])).x.state,'cancelled');assert.equal(await draft(b),1);
 await exchange(2700);state=await exchange(2900);assert.equal(state.pending,0);assert.equal(await draft(b),2);
 await role('authenticated');const barrier2=(await one('select public.device_confirmation_request($1,$2,$3) as x',[created.id,b,'30000000-0000-4000-8000-000000000002'])).x;
 await exchange(3200);state=await exchange(3400);assert.equal(state.frozen,true);
 await role('authenticated');const result=(await one('select public.device_confirmation_commit($1) as x',[barrier2])).x;
 assert.deepEqual(result,{requested:2,applied:1,current:1});assert.equal(await draft(b),0);assert.equal(await draft(a),2);
 state=await exchange(4200);assert.equal(state.frozen,false);assert.equal(state.pending,0);assert.deepEqual(requestErrors,[]);
 const exited=new Promise(resolve=>child.once('exit',resolve));child.stdin.write(JSON.stringify({action:'quit'})+'\n');child.stdin.end();assert.equal(await exited,0);child=null;
 console.log('firmware/local HTTPS/isolated database integration passed (simulated hardware)');
} finally {if(child)child.kill();if(server)await new Promise(resolve=>server.close(resolve));await db.close();rmSync(temp,{recursive:true,force:true});}
