-- ADDITIVE / NOT DEPLOYED: device ingest foundation only, NOT complete firmware integration.
-- Requires existing PR1 schema.sql. Do not deploy until pairing, snapshot-confirm
-- barrier, authorization, recovery and real-device end-to end tests are complete.
-- The existing orders and submit_production function are intentionally unchanged.

create table public.device_endpoints (
  id uuid primary key default gen_random_uuid(),
  label text not null check (length(trim(label)) between 1 and 100),
  secret_hash bytea not null check (octet_length(secret_hash) = 32),
  revoked_at timestamptz,
  desired_binding_id uuid,
  created_at timestamptz not null default now()
);
-- First release deliberately supports one device. Revocation permits replacement.
create unique index device_one_active on public.device_endpoints ((true)) where revoked_at is null;

create table public.device_order_bindings (
  id uuid primary key default gen_random_uuid(),
  device_id uuid not null references public.device_endpoints(id),
  order_id uuid not null references public.orders(id),
  revision bigint not null check (revision between 1 and 9007199254740991),
  created_at timestamptz not null default now(),
  unique (device_id, id),
  unique (device_id, revision)
);
alter table public.device_endpoints add constraint device_desired_binding_owned
  foreign key (id, desired_binding_id) references public.device_order_bindings(device_id, id);

-- One independently idempotent counter epoch per binding. Accepted totals are
-- never reset by confirmation, avoiding duplicate increments on response loss.
create table public.device_counter_streams (
  binding_id uuid not null references public.device_order_bindings(id),
  epoch uuid not null,
  accepted_total bigint not null default 0 check (accepted_total between 0 and 9007199254740991),
  primary key (binding_id, epoch)
);
create table public.device_order_drafts (
  order_id uuid primary key references public.orders(id),
  count bigint not null default 0 check (count between 0 and 9007199254740991),
  updated_at timestamptz not null default now()
);

alter table public.device_endpoints enable row level security;
alter table public.device_order_bindings enable row level security;
alter table public.device_counter_streams enable row level security;
alter table public.device_order_drafts enable row level security;
revoke all on public.device_endpoints, public.device_order_bindings, public.device_counter_streams, public.device_order_drafts from public, anon, authenticated;
grant select on public.device_order_drafts to authenticated;
create policy device_drafts_operator_read on public.device_order_drafts for select to authenticated using (public.is_operator());

-- Device secret is generated server-side, revealed once, and never stored raw.
-- Creating a device does not share the operator's login or administrator password.
create function public.device_create(p_admin_password text, p_label text)
returns jsonb language plpgsql security definer set search_path = public, extensions, pg_temp as $$
declare secret text; device_id uuid;
begin
  if not public.check_admin(p_admin_password) then return null; end if;
  if p_label is null or length(trim(p_label)) not between 1 and 100 then raise exception '请输入设备名称'; end if;
  secret := encode(extensions.gen_random_bytes(32), 'hex');
  insert into public.device_endpoints(label, secret_hash) values(trim(p_label), extensions.digest(secret, 'sha256')) returning id into device_id;
  return jsonb_build_object('id', device_id, 'secret', secret);
end $$;

create function public.device_revoke(p_admin_password text, p_device uuid)
returns boolean language plpgsql security definer set search_path = public, pg_temp as $$
begin
  if not public.check_admin(p_admin_password) then return false; end if;
  update public.device_endpoints set revoked_at=now() where id=p_device and revoked_at is null;
  if not found then raise exception '设备不存在或已撤销'; end if;
  return true;
end $$;

create function public.device_list()
returns table(id uuid, label text, desired_binding_id uuid, desired_order_id uuid, desired_binding_revision bigint, created_at timestamptz)
language plpgsql security definer set search_path = public, pg_temp as $$
begin
  if not public.is_operator() then raise exception '无访问权限'; end if;
  return query select d.id, d.label, d.desired_binding_id, b.order_id, b.revision, d.created_at
    from public.device_endpoints d left join public.device_order_bindings b on b.id=d.desired_binding_id
    where d.revoked_at is null order by d.created_at;
end $$;

-- The request changes only the desired binding. Old binding rows remain valid
-- for delayed offline uploads; the device must drain before applying a new one.
create function public.device_request_binding(p_device uuid, p_order uuid)
returns uuid language plpgsql security definer set search_path = public, pg_temp as $$
declare d public.device_endpoints; b public.device_order_bindings; new_id uuid; next_revision bigint;
begin
  if not public.is_operator() then raise exception '无访问权限'; end if;
  select * into d from public.device_endpoints where id=p_device and revoked_at is null for update;
  if not found then raise exception '设备不存在或已撤销'; end if;
  perform 1 from public.orders where id=p_order and not archived;
  if not found then raise exception '生产单不存在或已归档'; end if;
  if d.desired_binding_id is not null then
    select * into b from public.device_order_bindings where id=d.desired_binding_id;
    if b.order_id=p_order then return b.id; end if;
  end if;
  select coalesce(max(revision),0)+1 into next_revision from public.device_order_bindings where device_id=d.id;
  if next_revision>9007199254740991 then raise exception '设备绑定版本已达上限'; end if;
  insert into public.device_order_bindings(device_id,order_id,revision) values(d.id,p_order,next_revision) returning id into new_id;
  update public.device_endpoints set desired_binding_id=new_id where id=d.id;
  return new_id;
end $$;

-- Caller must use HTTPS and never place the secret in a URL or application log.
create function public.device_authenticate(p_secret text)
returns uuid language plpgsql stable security definer set search_path = public, extensions, pg_temp as $$
declare result uuid;
begin
  if p_secret is null or p_secret !~ '^[0-9a-f]{64}$' then return null; end if;
  select id into result from public.device_endpoints
    where secret_hash=extensions.digest(p_secret, 'sha256') and revoked_at is null;
  return result;
end $$;

create function public.device_status(p_secret text)
returns jsonb language plpgsql security definer set search_path = public, pg_temp as $$
declare v_device_id uuid; result jsonb;
begin
  v_device_id := public.device_authenticate(p_secret);
  if v_device_id is null then raise exception '设备认证失败' using errcode='28000'; end if;
  select jsonb_build_object('device_id',d.id,'desired_binding_id',d.desired_binding_id,'desired_order_id',b.order_id,'desired_binding_revision',b.revision)
    into result from public.device_endpoints d left join public.device_order_bindings b on b.id=d.desired_binding_id where d.id=v_device_id and d.revoked_at is null;
  if result is null then raise exception '设备认证失败' using errcode='28000'; end if;
  return result;
end $$;

-- The request carries a cumulative total, not a delta. The row lock makes
-- duplicates/out-of-order delivery safe and serializes concurrent updates.
-- This intentionally does NOT offer a device-draft confirmation endpoint:
-- a fresh online/synchronization barrier must be implemented first.
create function public.device_ingest(p_secret text, p_binding uuid, p_epoch uuid, p_total bigint)
returns jsonb language plpgsql security definer set search_path = public, pg_temp as $$
declare device uuid; binding public.device_order_bindings; previous bigint; delta bigint;
begin
  device := public.device_authenticate(p_secret);
  if device is null then raise exception '设备认证失败' using errcode='28000'; end if;
  if p_total is null or p_total < 0 or p_total > 9007199254740991 or p_epoch is null then raise exception '无效设备计数'; end if;
  -- Prevent credential revocation and ingestion racing one another.
  perform 1 from public.device_endpoints where id=device and revoked_at is null for update;
  if not found then raise exception '设备认证失败' using errcode='28000'; end if;
  select * into binding from public.device_order_bindings where id=p_binding and device_id=device;
  if not found then raise exception '设备绑定无效'; end if;
  insert into public.device_counter_streams(binding_id,epoch) values(binding.id,p_epoch) on conflict do nothing;
  select accepted_total into previous from public.device_counter_streams where binding_id=binding.id and epoch=p_epoch for update;
  if p_total > previous then
    delta := p_total - previous;
    insert into public.device_order_drafts(order_id,count) values(binding.order_id,0) on conflict do nothing;
    update public.device_order_drafts set count=count+delta, updated_at=now()
      where order_id=binding.order_id and count <= 9007199254740991-delta;
    if not found then raise exception '设备计数已达到上限'; end if;
    update public.device_counter_streams set accepted_total=p_total where binding_id=binding.id and epoch=p_epoch;
    previous := p_total;
  end if;
  return jsonb_build_object('accepted_total',previous,'binding_id',binding.id,'order_id',binding.order_id);
end $$;

revoke all on function public.device_authenticate(text) from public, anon, authenticated;
revoke all on function public.device_create(text,text), public.device_revoke(text,uuid), public.device_list(), public.device_request_binding(uuid,uuid), public.device_status(text), public.device_ingest(text,uuid,uuid,bigint) from public, anon, authenticated;
grant execute on function public.device_create(text,text), public.device_revoke(text,uuid), public.device_list(), public.device_request_binding(uuid,uuid) to authenticated;
grant execute on function public.device_status(text), public.device_ingest(text,uuid,uuid,bigint) to anon, authenticated;
