import {PGlite} from '@electric-sql/pglite';import {pgcrypto} from '@electric-sql/pglite/contrib/pgcrypto';import {readFile} from 'node:fs/promises';import assert from 'node:assert/strict';
const db=await PGlite.create({extensions:{pgcrypto}}),user='aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa',order='11111111-1111-4111-8111-111111111111';
try {
 await db.exec(`create role anon;create role authenticated;create schema auth;create schema extensions;create table auth.users(id uuid primary key);create function auth.uid() returns uuid language sql stable as $$select nullif(current_setting('request.jwt.claim.sub',true),'')::uuid$$;create publication supabase_realtime;`);
 for(const path of ['../../supabase/schema.sql','../../supabase/test-only/admin_replace_count.sql'])await db.exec(await readFile(new URL(path,import.meta.url),'utf8'));
 await db.query('insert into auth.users values($1)',[user]);await db.query("insert into public.app_settings values(true,$1,extensions.crypt('fixture-admin',extensions.gen_salt('bf')))",[user]);
 await db.query("insert into public.orders(id,order_no,product,total,current_count) values($1,'fixture','item',2000,119)",[order]);await db.query("select set_config('request.jwt.claim.sub',$1,false)",[user]);await db.exec('set role authenticated');
 const call=async(password,expected,target)=>(await db.query('select public.test_admin_replace_count($1,$2,$3,$4) result',[password,order,expected,target])).rows[0].result;
 assert.equal(await call('wrong',119,4),null);assert.equal((await call('fixture-admin',118,4)).status,'conflict');
 assert.equal((await call('fixture-admin',119,2001)).status,'invalid');assert.equal((await call('fixture-admin',119,4)).current,4);
 assert.equal((await call('fixture-admin',119,5)).status,'conflict');assert.equal((await call('fixture-admin',4,0)).current,0);
 await assert.rejects(()=>db.query('select * from public.test_admin_count_replacements'),/permission denied/);
 await db.exec('reset role');const audits=await db.query('select before_count,after_count from public.test_admin_count_replacements order by id');assert.deepEqual(audits.rows.map(r=>[Number(r.before_count),Number(r.after_count)]),[[119,4],[4,0]]);
 await db.exec('set role anon');await assert.rejects(()=>call('fixture-admin',0,7),/permission denied/);
 console.log('isolated admin replacement: authorization, conflict, limits and audit passed');
}finally{await db.close();}
