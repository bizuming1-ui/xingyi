import {PGlite} from '@electric-sql/pglite';import {pgcrypto} from '@electric-sql/pglite/contrib/pgcrypto';import {readFile} from 'node:fs/promises';import assert from 'node:assert/strict';
const db=await PGlite.create({extensions:{pgcrypto}}),user='aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa',other='bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb';
try{
 await db.exec(`create role anon; create role authenticated; create schema auth; create schema extensions; create table auth.users(id uuid primary key); create function auth.uid() returns uuid language sql stable as $$ select nullif(current_setting('request.jwt.claim.sub',true),'')::uuid $$; create publication supabase_realtime;`);
 for(const path of ['../../supabase/schema.sql','../../supabase/migrations/202610290001_device_ingest.sql','../../supabase/test-only/device_vault.sql'])await db.exec(await readFile(new URL(path,import.meta.url),'utf8'));
 await db.query('insert into auth.users values($1),($2)',[user,other]);await db.query("insert into public.app_settings(id,shared_user_id,admin_password_hash) values(true,$1,extensions.crypt('fixture-admin',extensions.gen_salt('bf')))",[user]);
 await db.query("select set_config('request.jwt.claim.sub',$1,false)",[user]);await db.exec('set role authenticated');
 const one=async(sql,args=[])=> (await db.query(sql,args)).rows[0].x;
 const dev=await one('select public.device_create($1,$2) x',['fixture-admin','vault']);
 const envelope={version:1,salt:'A'.repeat(22)+'==',iv:'A'.repeat(16),ciphertext:'A'.repeat(107)+'='};
 const save=(password,secret,e=envelope)=>one('select public.test_device_vault_save($1,$2,$3,$4) x',[password,dev.id,secret,e]);
 const read=password=>one('select public.test_device_vault_read($1,$2) x',[password,dev.id]);
 assert.equal(await save('wrong',dev.secret),null);assert.equal(await save('fixture-admin','a'.repeat(64)),false);
 assert.equal(await save('fixture-admin',dev.secret,{...envelope,ciphertext:'bad'}),false);
 assert.deepEqual(await read('fixture-admin'),{state:'missing'});assert.equal(await save('fixture-admin',dev.secret),true);
 assert.deepEqual(await read('fixture-admin'),{state:'saved',envelope});assert.equal(await read('wrong'),null);
 await assert.rejects(()=>db.query('select * from public.test_device_secret_vault'),/permission denied/);
 await db.exec('reset role');await db.query("select set_config('request.jwt.claim.sub',$1,false)",[other]);await db.exec('set role authenticated');assert.equal(await read('fixture-admin'),null);
 await db.exec('reset role');await db.exec('set role anon');await assert.rejects(()=>read('fixture-admin'),/permission denied/);
 console.log('isolated vault authorization, matching and persistence tests passed');
}finally{await db.close();}
