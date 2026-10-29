-- Run only against a disposable/test project. Everything is rolled back.
begin;
insert into auth.users(id, aud, role, email, encrypted_password, email_confirmed_at, created_at, updated_at) values ('f3694051-3202-4b82-833b-2158bd857e83', 'authenticated', 'authenticated', 'test-interface1@example.invalid', '', now(), now(), now());
insert into public.app_settings(id,shared_user_id,admin_password_hash) values (true,'f3694051-3202-4b82-833b-2158bd857e83',extensions.crypt('temporary-test-password',extensions.gen_salt('bf')));
select set_config('request.jwt.claim.sub','f3694051-3202-4b82-833b-2158bd857e83',true);
do $$ declare oid uuid; res jsonb; hist jsonb; begin
 oid:=public.admin_save_order('temporary-test-password',null,'test-001','test-product',100,95,null);
 if oid is null then raise exception 'admin save failed'; end if;
 res:=public.submit_production(oid,10,'ed1b9972-66cf-4f78-8486-ebf65e847a1d');
 if (res->>'applied')::int <> 5 or (res->>'current')::int <> 100 then raise exception 'cap failed %',res; end if;
 res:=public.submit_production(oid,10,'ed1b9972-66cf-4f78-8486-ebf65e847a1d');
 if (res->>'current')::int <> 100 or (select count(*) from public.production_events where order_id=oid) <> 1 then raise exception 'retry failed'; end if;
 hist:=public.admin_history('temporary-test-password',oid);
 if jsonb_array_length(hist) <> 1 then raise exception 'history failed'; end if;
 if public.admin_archive_order('incorrect',oid) then raise exception 'admin auth failed'; end if;
 if not public.admin_archive_order('temporary-test-password',oid) then raise exception 'archive failed'; end if;
end $$;
rollback;
