import {PGlite} from '@electric-sql/pglite';
import {pgcrypto} from '@electric-sql/pglite/contrib/pgcrypto';
import {readFile} from 'node:fs/promises';
import assert from 'node:assert/strict';
const db=await PGlite.create({extensions:{pgcrypto}});
try {
 await db.exec(`create role anon;create role authenticated;create schema auth;create schema extensions;create table auth.users(id uuid primary key);
 create function auth.uid() returns uuid language sql stable as $$ select nullif(current_setting('request.jwt.claim.sub',true),'')::uuid $$;create publication supabase_realtime;`);
 await db.exec(await readFile(new URL('../../supabase/schema.sql',import.meta.url),'utf8'));
 await db.exec(await readFile(new URL('../../supabase/test-only/first_setup.sql',import.meta.url),'utf8'));
 const a='aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa',b='bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb';
 const one=async(sql,args=[])=> (await db.query(sql,args)).rows[0];
 const call=async(value)=> (await one('select public.test_initial_setup($1) as x',[value])).x;
 await db.query('insert into auth.users values ($1),($2)',[a,b]);await db.query("select set_config('request.jwt.claim.sub',$1,false)",[a]);
 await db.exec('set role authenticated');assert.equal(await call('fixture-only-password'),false);await db.exec('reset role');
 await db.query('delete from auth.users where id=$1',[b]);await db.exec('set role authenticated');
 assert.equal(await call('short'),false);assert.equal(await call('fixture-only-password'),true);assert.equal(await call('second-fixture-password'),false);
 await db.exec('reset role');assert.equal((await one('select shared_user_id from public.app_settings')).shared_user_id,a);
 assert.equal((await one("select has_function_privilege('anon','public.test_initial_setup(text)','EXECUTE') as allowed")).allowed,false);
 console.log('isolated first-operator setup tests passed');
} finally {await db.close();}
