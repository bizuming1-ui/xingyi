-- Run in the Supabase SQL editor on a fresh project. Requires pgcrypto.
create extension if not exists pgcrypto with schema extensions;
create table if not exists public.app_settings (
  id boolean primary key default true check (id),
  shared_user_id uuid not null references auth.users(id),
  admin_password_hash text not null
);
create table if not exists public.admin_attempts (
  id boolean primary key default true check (id),
  failures integer not null default 0,
  locked_until timestamptz
);
insert into public.admin_attempts(id) values (true) on conflict do nothing;
alter table public.admin_attempts enable row level security;
revoke all on public.admin_attempts from anon, authenticated;
create table if not exists public.orders (
  id uuid primary key default gen_random_uuid(),
  order_no text not null unique check (length(trim(order_no)) between 1 and 100),
  product text not null check (length(trim(product)) between 1 and 200),
  total bigint not null check (total between 0 and 9007199254740991),
  current_count bigint not null default 0 check (current_count >= 0 and current_count <= total),
  archived boolean not null default false,
  created_at timestamptz not null default now()
);
create table if not exists public.production_events (
  id uuid primary key default gen_random_uuid(),
  order_id uuid not null references public.orders(id),
  request_id uuid not null unique,
  requested bigint not null,
  applied bigint not null,
  resulting_count bigint not null,
  created_at timestamptz not null default now()
);
alter table public.app_settings enable row level security;
alter table public.orders enable row level security;
alter table public.production_events enable row level security;
revoke all on public.app_settings, public.orders, public.production_events from anon, authenticated;
grant select on public.orders to authenticated;
create or replace function public.is_operator() returns boolean language sql stable security definer set search_path = public, pg_temp as $$
  select exists(select 1 from public.app_settings where id = true and shared_user_id = auth.uid());
$$;
revoke all on function public.is_operator() from public, anon;
grant execute on function public.is_operator() to authenticated;
drop policy if exists operator_read_orders on public.orders;
create policy operator_read_orders on public.orders for select to authenticated using (public.is_operator());
-- All updates are server-side functions so browser clients cannot bypass rules.
create or replace function public.submit_production(p_order uuid, p_amount bigint, p_request uuid)
returns jsonb language plpgsql security definer set search_path = public, pg_temp as $$
declare o public.orders; e public.production_events; applied_amount bigint;
begin
 if not public.is_operator() then raise exception '无访问权限'; end if;
 if p_amount is null or p_amount <= 0 or p_amount > 9007199254740991 or p_request is null then raise exception '请输入大于零的整数'; end if;
 -- A transaction-scoped lock also makes retries with the same request ID safe.
 perform pg_advisory_xact_lock(hashtextextended(p_request::text, 0));
 select * into e from public.production_events where request_id=p_request;
 if found then
   if e.order_id <> p_order or e.requested <> p_amount then raise exception '重复请求参数不一致'; end if;
   return jsonb_build_object('applied',e.applied,'current',e.resulting_count);
 end if;
 select * into o from public.orders where id=p_order and not archived for update;
 if not found then raise exception '生产单不存在或已归档'; end if;
 applied_amount := least(p_amount, o.total-o.current_count);
 update public.orders set current_count=current_count+applied_amount where id=o.id returning * into o;
 insert into public.production_events(order_id,request_id,requested,applied,resulting_count) values(o.id,p_request,p_amount,applied_amount,o.current_count);
 return jsonb_build_object('applied',applied_amount,'current',o.current_count);
end $$;
create or replace function public.check_admin(p_password text) returns boolean language plpgsql security definer set search_path = public, extensions, pg_temp as $$
declare a public.admin_attempts; ok boolean;
begin
 if not public.is_operator() then return false; end if;
 select * into a from public.admin_attempts where id for update;
 if a.locked_until > now() then return false; end if;
 select exists(select 1 from public.app_settings where id and p_password is not null and admin_password_hash = extensions.crypt(p_password, admin_password_hash)) into ok;
 if ok then
   update public.admin_attempts set failures=0, locked_until=null where id;
 else
   update public.admin_attempts set failures=case when a.locked_until is not null and a.locked_until <= now() then 1 else a.failures+1 end,
     locked_until=case when (case when a.locked_until is not null and a.locked_until <= now() then 1 else a.failures+1 end) >= 5 then now()+interval '15 minutes' else null end where id;
 end if;
 return ok;
end $$;
create or replace function public.admin_save_order(p_password text, p_id uuid, p_order_no text, p_product text, p_total bigint, p_current bigint, p_expected_current bigint)
returns uuid language plpgsql security definer set search_path = public, pg_temp as $$
declare result_id uuid; old_current bigint;
begin
 if not public.check_admin(p_password) then return null; end if;
 if p_total is null or p_current is null or p_total < 0 or p_total > 9007199254740991 or p_current < 0 or p_current > p_total then raise exception '数量必须为非负整数，且当前生产数不能超过生产总数'; end if;
 if p_id is null then
   insert into public.orders(order_no,product,total,current_count) values(trim(p_order_no),trim(p_product),p_total,p_current) returning id into result_id;
 else
   select current_count into old_current from public.orders where id=p_id and not archived for update;
   if not found then raise exception '生产单不存在或已归档'; end if;
   if old_current is distinct from p_expected_current then raise exception '数量已被其他设备更新，请刷新后重试'; end if;
   if p_total < old_current then raise exception '生产总数不能小于原有当前生产数，请先修正当前生产数'; end if;
   update public.orders set order_no=trim(p_order_no), product=trim(p_product),total=p_total,current_count=p_current where id=p_id and not archived returning id into result_id;
   if result_id is null then raise exception '生产单不存在或已归档'; end if;
 end if;
 return result_id;
end $$;
create or replace function public.admin_archive_order(p_password text,p_id uuid) returns boolean language plpgsql security definer set search_path = public, pg_temp as $$
begin
 if not public.check_admin(p_password) then return false; end if;
 update public.orders set archived=true where id=p_id;
 if not found then raise exception '生产单不存在'; end if;
 return true;
end $$;
create or replace function public.admin_history(p_password text,p_id uuid)
returns jsonb language plpgsql security definer set search_path = public, pg_temp as $$
declare result jsonb;
begin
 if not public.check_admin(p_password) then return null; end if;
 select coalesce(jsonb_agg(to_jsonb(t) order by t.created_at desc), '[]'::jsonb) into result from (select e.created_at,e.requested,e.applied,e.resulting_count from public.production_events e where e.order_id=p_id) t;
 return result;
end $$;
revoke all on function public.check_admin(text) from public, anon, authenticated;
revoke all on function public.submit_production(uuid,bigint,uuid), public.admin_save_order(text,uuid,text,text,bigint,bigint,bigint), public.admin_archive_order(text,uuid), public.admin_history(text,uuid) from public, anon;
grant execute on function public.submit_production(uuid,bigint,uuid), public.admin_save_order(text,uuid,text,text,bigint,bigint,bigint), public.admin_archive_order(text,uuid), public.admin_history(text,uuid) to authenticated;
do $$ begin
 if not exists (select 1 from pg_publication_tables where pubname='supabase_realtime' and schemaname='public' and tablename='orders') then
   alter publication supabase_realtime add table public.orders;
 end if;
end $$;
