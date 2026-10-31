// Isolated component integration: no real Wi-Fi, TLS, hardware, or Supabase.
import {PGlite} from '@electric-sql/pglite';
import {pgcrypto} from '@electric-sql/pglite/contrib/pgcrypto';
import {spawn,execFileSync} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdtempSync,rmSync} from 'node:fs';
import {readFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import assert from 'node:assert/strict';
const root=resolve(fileURLToPath(new URL('../..',import.meta.url))),temp=mkdtempSync(join(tmpdir(),'xingyi-firmware-db-'));
const include=process.env.ARDUINOJSON_INCLUDE;if(!include)throw Error('ARDUINOJSON_INCLUDE is required');
const db=await PGlite.create({extensions:{pgcrypto}});let child;
const user='aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa',a='11111111-1111-4111-8111-111111111111',b='22222222-2222-4222-8222-222222222222';
const one=async(sql,args=[]) => (await db.query(sql,args)).rows[0];
async function role(name){await db.exec('reset role');await db.query("select set_config('request.jwt.claim.sub',$1,false)",[user]);await db.exec(`set role ${name}`);}
try {
 execFileSync('g++',['-std=c++11','-Wall','-Wextra','-Werror','-I',include,join(root,'tests/firmware_database_driver.cpp'),'-o',join(temp,'driver')]);
 await db.exec(`create role anon;create role authenticated;create schema auth;create schema extensions;create table auth.users(id uuid primary key);
 create function auth.uid() returns uuid language sql stable as $$ select nullif(current_setting('request.jwt.claim.sub',true),'')::uuid $$;create publication supabase_realtime;`);
 for(const path of ['supabase/schema.sql','supabase/migrations/202610290001_device_ingest.sql','supabase/migrations/202610290002_device_confirmation.sql','supabase/migrations/202610290003_device_confirmation_recovery.sql'])await db.exec(await readFile(join(root,path),'utf8'));
 await db.query('insert into auth.users(id) values($1)',[user]);
 await db.query("insert into public.app_settings(id,shared_user_id,admin_password_hash) values(true,$1,extensions.crypt('fixture-only',extensions.gen_salt('bf')))",[user]);
 await db.query("insert into public.orders(id,order_no,product,total,current_count) values($1,'A','fixture',100,0),($2,'B','fixture',1,0)",[a,b]);
 await role('authenticated');const created=(await one('select public.device_create($1,$2) as x',['fixture-only','integration fixture'])).x;
 const bindA=(await one('select public.device_request_binding($1,$2) as x',[created.id,a])).x;
 child=spawn(join(temp,'driver'),[],{stdio:['pipe','pipe','inherit']});const lines=createInterface({input:child.stdout})[Symbol.asyncIterator]();
 child.stdin.write(JSON.stringify({secret:created.secret})+'\n');
 const methods={
  device_status:['p_secret'],device_ingest:['p_secret','p_binding','p_epoch','p_total'],
  device_confirmation_freeze:['p_secret','p_barrier','p_binding','p_epoch','p_total'],
  device_confirmation_status:['p_secret','p_barrier'],device_confirmation_renew:['p_secret','p_barrier','p_binding','p_epoch','p_total','p_nonce'],
  device_confirmation_abort:['p_secret','p_barrier']
 };
 let loseNextIngest=false;
 async function nextLine(){let timer;try{return await Promise.race([lines.next(),new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('driver timed out')),15000);})]);}finally{clearTimeout(timer);}}
 async function command(action,now=0,pressed=false) {
  child.stdin.write(JSON.stringify({action,now,pressed})+'\n');
  for(;;){const item=await nextLine();if(item.done)throw Error('driver exited unexpectedly');const message=JSON.parse(item.value);
   if(message.type==='result'){assert.equal(message.fault,false,'device state machine faulted');return message;}
   assert.equal(message.type,'rpc');const params=methods[message.endpoint];assert.ok(params,'unexpected RPC');
   const values=params.map(k=>message.body[k]);assert.ok(values.every(v=>v!==undefined));
   await role('anon');
   let result;try {const args=params.map((_,i)=>`$${i+1}`).join(',');result=(await one(`select public.${message.endpoint}(${args}) as x`,values)).x;}
   catch(error){throw new Error(`fixture RPC ${message.endpoint} failed: ${error.message}`);}
   const timeout=loseNextIngest && message.endpoint==='device_ingest';if(timeout)loseNextIngest=false;
   child.stdin.write(JSON.stringify(timeout?{timeout:true}:{http:200,body:JSON.stringify(result)})+'\n');
  }
 }
 async function step(now){return command('step',now);}
 async function exchange(now){assert.equal((await step(now)).queued,true);await command('work');return step(now+20);}
 async function press(now,hold=false){await command('press',now,true);let result=await command('press',now+25,true);assert.equal(result.accepted,1);if(hold){result=await command('press',now+525,true);assert.equal(result.accepted,1);}await command('press',now+(hold?530:50),false);await command('press',now+(hold?555:75),false);}
 async function draft(id){await role('authenticated');return (await one('select count from public.device_order_drafts where order_id=$1',[id]))?.count??0;}
 // Poll binding A and register a zero epoch.
 await exchange(0);let state=await exchange(200);assert.equal(state.order,a);assert.equal(state.pending,0);
 // Offline presses and a desired rebind to B: older counts remain on A.
 await press(300,true);await role('authenticated');const bindB=(await one('select public.device_request_binding($1,$2) as x',[created.id,b])).x;assert.notEqual(bindA,bindB);
 await exchange(900);loseNextIngest=true;state=await exchange(1100);assert.equal(state.pending,2);assert.equal(await draft(a),2);
 state=await exchange(1300);assert.equal(state.pending,0);assert.equal(await draft(a),2); // idempotent retry after server applied but reply was lost
 await exchange(1500);state=await exchange(1700);assert.equal(state.order,b);assert.equal(await draft(b),0);
 await press(1750);state=await exchange(1900);assert.equal(state.pending,0);assert.equal(await draft(b),1);
 // During freeze, an accidental press is retained and makes the device abort.
 await role('authenticated');const barrier1=(await one('select public.device_confirmation_request($1,$2,$3) as x',[created.id,b,'30000000-0000-4000-8000-000000000001'])).x;
 await exchange(2100);state=await exchange(2300);assert.equal(state.frozen,true);
 await press(2350);state=await exchange(2500);assert.equal(state.frozen,false);assert.equal(state.pending,1);
 await role('authenticated');assert.equal((await one('select public.device_confirmation_view($1) as x',[barrier1])).x.state,'cancelled');assert.equal(await draft(b),1);
 await exchange(2700);state=await exchange(2900);assert.equal(state.pending,0);assert.equal(await draft(b),2);
 // A clean freeze and server commit cap the count while clearing excess.
 await role('authenticated');const barrier2=(await one('select public.device_confirmation_request($1,$2,$3) as x',[created.id,b,'30000000-0000-4000-8000-000000000002'])).x;
 await exchange(3200);state=await exchange(3400);assert.equal(state.frozen,true);
 await role('authenticated');const result=(await one('select public.device_confirmation_commit($1) as x',[barrier2])).x;
 assert.deepEqual(result,{requested:2,applied:1,current:1});assert.equal(await draft(b),0);assert.equal(await draft(a),2);
 state=await exchange(4200);assert.equal(state.frozen,false);assert.equal(state.pending,0);
 const exited=new Promise(resolve=>child.once('exit',code=>resolve(code)));
 child.stdin.write(JSON.stringify({action:'quit'})+'\n');child.stdin.end();
 const exit=await exited;assert.equal(exit,0);child=null;
 console.log('firmware/database component integration passed (simulated transport)');
} finally {if(child)child.kill();await db.close();rmSync(temp,{recursive:true,force:true});}
