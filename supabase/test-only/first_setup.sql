-- TEST PROJECT ONLY: with public signups disabled, the first and only manually
-- created Auth user may set the isolated test project's administrator password.
-- Not a production migration. Password is sent over HTTPS, hashed server-side,
-- and must never be included in scripts, logs, URLs or Git.
create function public.test_initial_setup(p_password text)
returns boolean language plpgsql security definer set search_path=public,extensions,pg_temp as $setup$
declare uid uuid:=auth.uid();
begin
 if uid is null or p_password is null or length(p_password)<12 or length(p_password)>128 then return false; end if;
 perform pg_catalog.pg_advisory_xact_lock(pg_catalog.hashtextextended('xingyi-test-first-operator'::text,0::bigint));
 if exists(select 1 from public.app_settings) or (select count(*) from auth.users)<>1 or not exists(select 1 from auth.users where id=uid) then return false; end if;
 insert into public.app_settings(id,shared_user_id,admin_password_hash) values(true,uid,extensions.crypt(p_password,extensions.gen_salt('bf')));
 return true;
end $setup$;
revoke all on function public.test_initial_setup(text) from public,anon;
grant execute on function public.test_initial_setup(text) to authenticated;
