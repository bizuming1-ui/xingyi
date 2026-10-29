-- ADDITIVE / NOT DEPLOYED. Requires 202610290001_device_ingest.sql.
-- This is a server-side protocol foundation only. Do not deploy until the
-- device's durable freeze/queue implementation and full integration are tested.
-- A recent lease is a failure detector, not proof of present connectivity.

create table public.device_confirmation_barriers (
  id uuid primary key default gen_random_uuid(),
  request_id uuid not null unique,
  device_id uuid not null references public.device_endpoints(id),
  order_id uuid not null references public.orders(id),
  state text not null default 'requested' check (state in ('requested','ready','completed','cancelled')),
  freeze_binding uuid,
  freeze_epoch uuid,
  freeze_total bigint,
  frozen_count bigint,
  lease_until timestamptz,
  lease_nonce uuid,
  result jsonb,
  created_at timestamptz not null default clock_timestamp(),
  check (freeze_total is null or freeze_total between 0 and 9007199254740991),
  check (frozen_count is null or frozen_count between 0 and 9007199254740991),
  check (state <> 'ready' or (freeze_binding is not null and freeze_epoch is not null and freeze_total is not null and frozen_count is not null and lease_until is not null and lease_nonce is not null))
);
create unique index device_one_open_confirmation on public.device_confirmation_barriers(device_id) where state in ('requested','ready');
alter table public.device_confirmation_barriers enable row level security;
revoke all on public.device_confirmation_barriers from public, anon, authenticated;

-- Always lock device -> barrier -> stream/draft -> order, to avoid lock-order
-- reversals. A newer desired binding may coexist with late data from an old one.
create function public.device_confirmation_request(p_device uuid, p_order uuid, p_request uuid)
returns uuid language plpgsql security definer set search_path = public, pg_temp as $$
declare d public.device_endpoints; b public.device_confirmation_barriers;
begin
  if not public.is_operator() then raise exception '无访问权限'; end if;
  if p_request is null or p_device is null or p_order is null then raise exception '确认请求参数无效'; end if;
  perform pg_advisory_xact_lock(hashtextextended(p_request::text,0));
  select * into b from public.device_confirmation_barriers where request_id=p_request;
  if found then
    if b.device_id is distinct from p_device or b.order_id is distinct from p_order then raise exception '重复请求参数不一致'; end if;
    return b.id;
  end if;
  select * into d from public.device_endpoints where id=p_device and revoked_at is null for update;
  if not found then raise exception '设备不存在或已撤销'; end if;
  perform 1 from public.device_order_bindings where device_id=p_device and order_id=p_order;
  if not found then raise exception '设备没有此生产单的绑定'; end if;
  perform 1 from public.orders where id=p_order and not archived;
  if not found then raise exception '生产单不存在或已归档'; end if;
  insert into public.device_confirmation_barriers(request_id,device_id,order_id) values(p_request,p_device,p_order) returning * into b;
  return b.id;
end $$;

-- A device must persist its frozen local state and drain ALL its queued data
-- before making this call. The server verifies the supplied epoch is fully
-- acknowledged, but it cannot independently inspect unknown local queues.
create function public.device_confirmation_freeze(p_secret text, p_barrier uuid, p_binding uuid, p_epoch uuid, p_total bigint)
returns jsonb language plpgsql security definer set search_path = public, pg_temp as $$
declare device uuid; b public.device_confirmation_barriers; accepted bigint; draft bigint;
begin
  device:=public.device_authenticate(p_secret);
  if device is null then raise exception '设备认证失败' using errcode='28000'; end if;
  if p_binding is null or p_epoch is null or p_total is null or p_total<0 or p_total>9007199254740991 then raise exception '冻结快照无效'; end if;
  perform 1 from public.device_endpoints where id=device and revoked_at is null for update;
  if not found then raise exception '设备认证失败' using errcode='28000'; end if;
  select * into b from public.device_confirmation_barriers where id=p_barrier and device_id=device for update;
  if not found then raise exception '确认请求不存在'; end if;
  if b.state='ready' then
    if b.freeze_binding<>p_binding or b.freeze_epoch<>p_epoch or b.freeze_total<>p_total then raise exception '冻结快照不一致'; end if;
    -- A delayed duplicate must NOT renew the lease. Only a fresh heartbeat may.
    return jsonb_build_object('state',case when b.lease_until<=clock_timestamp() then 'expired' else b.state end,'lease_until',b.lease_until,'lease_nonce',b.lease_nonce);
  end if;
  if b.state<>'requested' then raise exception '确认请求已结束'; end if;
  perform 1 from public.device_order_bindings where id=p_binding and device_id=device;
  if not found then raise exception '设备绑定无效'; end if;
  select accepted_total into accepted from public.device_counter_streams where binding_id=p_binding and epoch=p_epoch for update;
  if not found or accepted<>p_total then raise exception '设备计数尚未同步'; end if;
  insert into public.device_order_drafts(order_id,count) values(b.order_id,0) on conflict do nothing;
  select count into draft from public.device_order_drafts where order_id=b.order_id for update;
  if draft<=0 then raise exception '没有需要确认的设备计数'; end if;
  update public.device_confirmation_barriers set state='ready', freeze_binding=p_binding, freeze_epoch=p_epoch,
    freeze_total=p_total, frozen_count=draft, lease_until=clock_timestamp()+interval '2 seconds', lease_nonce=gen_random_uuid() where id=b.id returning * into b;
  return jsonb_build_object('state',b.state,'lease_until',b.lease_until,'lease_nonce',b.lease_nonce);
end $$;

-- Heartbeats are request/response interactions, not queued event replays.
-- Once expired a barrier can never be resurrected: cancel and start again.
create function public.device_confirmation_renew(p_secret text, p_barrier uuid, p_binding uuid, p_epoch uuid, p_total bigint, p_nonce uuid)
returns jsonb language plpgsql security definer set search_path = public, pg_temp as $$
declare device uuid; b public.device_confirmation_barriers;
begin
  device:=public.device_authenticate(p_secret);
  if device is null then raise exception '设备认证失败' using errcode='28000'; end if;
  perform 1 from public.device_endpoints where id=device and revoked_at is null for update;
  if not found then raise exception '设备认证失败' using errcode='28000'; end if;
  select * into b from public.device_confirmation_barriers where id=p_barrier and device_id=device for update;
  if not found then raise exception '确认请求不存在'; end if;
  if b.state<>'ready' then return jsonb_build_object('state',b.state); end if;
  if b.lease_until<=clock_timestamp() then return jsonb_build_object('state','expired'); end if;
  if b.freeze_binding is distinct from p_binding or b.freeze_epoch is distinct from p_epoch or b.freeze_total is distinct from p_total then raise exception '冻结快照不一致'; end if;
  if b.lease_nonce is distinct from p_nonce then raise exception '续约令牌已使用或不一致'; end if;
  update public.device_confirmation_barriers set lease_until=clock_timestamp()+interval '2 seconds',lease_nonce=gen_random_uuid() where id=b.id returning * into b;
  return jsonb_build_object('state',b.state,'lease_until',b.lease_until,'lease_nonce',b.lease_nonce);
end $$;

create function public.device_confirmation_status(p_secret text, p_barrier uuid)
returns jsonb language plpgsql security definer set search_path = public, pg_temp as $$
declare device uuid; b public.device_confirmation_barriers;
begin
  device:=public.device_authenticate(p_secret);
  if device is null then raise exception '设备认证失败' using errcode='28000'; end if;
  select * into b from public.device_confirmation_barriers where id=p_barrier and device_id=device;
  if not found then raise exception '确认请求不存在'; end if;
  return jsonb_build_object('id',b.id,'order_id',b.order_id,'state',case when b.state='ready' and b.lease_until<=clock_timestamp() then 'expired' else b.state end,'lease_until',b.lease_until,'lease_nonce',b.lease_nonce);
end $$;

-- Do not change the desired binding while a freeze request is unresolved.
create or replace function public.device_request_binding(p_device uuid, p_order uuid)
returns uuid language plpgsql security definer set search_path = public, pg_temp as $$
declare d public.device_endpoints; b public.device_order_bindings; new_id uuid;
begin
  if not public.is_operator() then raise exception '无访问权限'; end if;
  select * into d from public.device_endpoints where id=p_device and revoked_at is null for update;
  if not found then raise exception '设备不存在或已撤销'; end if;
  if exists(select 1 from public.device_confirmation_barriers where device_id=d.id and state in ('requested','ready')) then
    raise exception '存在未结束的设备确认请求，请先确认或取消';
  end if;
  perform 1 from public.orders where id=p_order and not archived;
  if not found then raise exception '生产单不存在或已归档'; end if;
  if d.desired_binding_id is not null then
    select * into b from public.device_order_bindings where id=d.desired_binding_id;
    if b.order_id=p_order then return b.id; end if;
  end if;
  insert into public.device_order_bindings(device_id,order_id) values(d.id,p_order) returning id into new_id;
  update public.device_endpoints set desired_binding_id=new_id where id=d.id;
  return new_id;
end $$;

-- Until safe handover of local/offline counts is implemented, a bound device
-- cannot be revoked through the application. Manual credential recovery must
-- never silently discard unsynchronized device data.
create or replace function public.device_revoke(p_admin_password text, p_device uuid)
returns boolean language plpgsql security definer set search_path = public, pg_temp as $$
declare d public.device_endpoints;
begin
  if not public.check_admin(p_admin_password) then return false; end if;
  select * into d from public.device_endpoints where id=p_device and revoked_at is null for update;
  if not found then raise exception '设备不存在或已撤销'; end if;
  if exists(select 1 from public.device_order_bindings where device_id=d.id) then
    raise exception '已绑定设备不能直接撤销，需先安全处理离线计数';
  end if;
  update public.device_endpoints set revoked_at=now() where id=d.id;
  return true;
end $$;

-- The device polls this endpoint to discover the current confirmation request.
create or replace function public.device_status(p_secret text)
returns jsonb language plpgsql security definer set search_path = public, pg_temp as $$
declare v_device_id uuid; result jsonb;
begin
  v_device_id:=public.device_authenticate(p_secret);
  if v_device_id is null then raise exception '设备认证失败' using errcode='28000'; end if;
  select jsonb_build_object('device_id',d.id,'desired_binding_id',d.desired_binding_id,'desired_order_id',b.order_id,
    'barrier_id',c.id,'barrier_order_id',c.order_id,'barrier_state',case when c.state='ready' and c.lease_until<=clock_timestamp() then 'expired' else c.state end)
  into result from public.device_endpoints d left join public.device_order_bindings b on b.id=d.desired_binding_id
    left join public.device_confirmation_barriers c on c.device_id=d.id and c.state in ('requested','ready')
    where d.id=v_device_id and d.revoked_at is null;
  if result is null then raise exception '设备认证失败' using errcode='28000'; end if;
  return result;
end $$;

create function public.device_confirmation_view(p_barrier uuid)
returns jsonb language plpgsql security definer set search_path = public, pg_temp as $$
declare b public.device_confirmation_barriers;
begin
  if not public.is_operator() then raise exception '无访问权限'; end if;
  select * into b from public.device_confirmation_barriers where id=p_barrier;
  if not found then raise exception '确认请求不存在'; end if;
  return jsonb_build_object('id',b.id,'order_id',b.order_id,'state',case when b.state='ready' and b.lease_until<=clock_timestamp() then 'expired' else b.state end,
    'lease_until',b.lease_until,'frozen_count',b.frozen_count,'result',b.result);
end $$;

-- Only authenticated operators can commit; a retry returns the prior result.
create function public.device_confirmation_commit(p_barrier uuid)
returns jsonb language plpgsql security definer set search_path = public, pg_temp as $$
declare b public.device_confirmation_barriers; d public.device_endpoints; o public.orders; draft bigint; applied bigint; output jsonb;
begin
  if not public.is_operator() then raise exception '无访问权限'; end if;
  select * into b from public.device_confirmation_barriers where id=p_barrier;
  if not found then raise exception '确认请求不存在'; end if;
  select * into d from public.device_endpoints where id=b.device_id for update;
  select * into b from public.device_confirmation_barriers where id=p_barrier for update;
  if b.state='completed' then return b.result; end if;
  if d.revoked_at is not null then raise exception '设备已撤销'; end if;
  if b.state<>'ready' or b.lease_until<=clock_timestamp() then raise exception '设备未冻结同步或续约已过期'; end if;
  select count into draft from public.device_order_drafts where order_id=b.order_id for update;
  if not found or draft is distinct from b.frozen_count or draft<=0 then raise exception '设备计数已改变，请重新同步'; end if;
  select * into o from public.orders where id=b.order_id and not archived for update;
  if not found then raise exception '生产单不存在或已归档'; end if;
  -- Locks can take time: recheck after obtaining the order lock.
  if b.lease_until<=clock_timestamp() then raise exception '设备续约已过期'; end if;
  applied:=least(draft,o.total-o.current_count);
  update public.orders set current_count=current_count+applied where id=o.id returning * into o;
  insert into public.production_events(order_id,request_id,requested,applied,resulting_count) values(o.id,b.request_id,draft,applied,o.current_count);
  update public.device_order_drafts set count=0,updated_at=clock_timestamp() where order_id=o.id;
  output:=jsonb_build_object('requested',draft,'applied',applied,'current',o.current_count);
  update public.device_confirmation_barriers set state='completed',result=output where id=b.id;
  return output;
end $$;

create function public.device_confirmation_cancel(p_barrier uuid)
returns boolean language plpgsql security definer set search_path = public, pg_temp as $$
declare b public.device_confirmation_barriers;
begin
  if not public.is_operator() then raise exception '无访问权限'; end if;
  select * into b from public.device_confirmation_barriers where id=p_barrier;
  if not found then raise exception '确认请求不存在'; end if;
  perform 1 from public.device_endpoints where id=b.device_id for update;
  select * into b from public.device_confirmation_barriers where id=p_barrier for update;
  if b.state='cancelled' then return true; end if;
  if b.state='completed' then raise exception '已确认的计数不能取消'; end if;
  update public.device_confirmation_barriers set state='cancelled' where id=b.id;
  return true;
end $$;

-- Extend the NEW device endpoint only; PR1 manual confirmation remains untouched.
create or replace function public.device_ingest(p_secret text, p_binding uuid, p_epoch uuid, p_total bigint)
returns jsonb language plpgsql security definer set search_path = public, pg_temp as $$
declare device uuid; binding public.device_order_bindings; previous bigint; delta bigint;
begin
  device:=public.device_authenticate(p_secret);
  if device is null then raise exception '设备认证失败' using errcode='28000'; end if;
  if p_total is null or p_total<0 or p_total>9007199254740991 or p_epoch is null then raise exception '无效设备计数'; end if;
  perform 1 from public.device_endpoints where id=device and revoked_at is null for update;
  if not found then raise exception '设备认证失败' using errcode='28000'; end if;
  select * into binding from public.device_order_bindings where id=p_binding and device_id=device;
  if not found then raise exception '设备绑定无效'; end if;
  insert into public.device_counter_streams(binding_id,epoch) values(binding.id,p_epoch) on conflict do nothing;
  select accepted_total into previous from public.device_counter_streams where binding_id=binding.id and epoch=p_epoch for update;
  if p_total>previous then
    if exists(select 1 from public.device_confirmation_barriers where device_id=device and state='ready' and lease_until>clock_timestamp()) then raise exception '设备确认冻结中，请稍后重试'; end if;
    delta:=p_total-previous;
    insert into public.device_order_drafts(order_id,count) values(binding.order_id,0) on conflict do nothing;
    update public.device_order_drafts set count=count+delta,updated_at=now() where order_id=binding.order_id and count<=9007199254740991-delta;
    if not found then raise exception '设备计数已达到上限'; end if;
    update public.device_counter_streams set accepted_total=p_total where binding_id=binding.id and epoch=p_epoch;
    previous:=p_total;
  end if;
  return jsonb_build_object('accepted_total',previous,'binding_id',binding.id,'order_id',binding.order_id);
end $$;

revoke all on function public.device_confirmation_request(uuid,uuid,uuid),public.device_confirmation_freeze(text,uuid,uuid,uuid,bigint),public.device_confirmation_renew(text,uuid,uuid,uuid,bigint,uuid),public.device_confirmation_status(text,uuid),public.device_confirmation_commit(uuid),public.device_confirmation_cancel(uuid),public.device_confirmation_view(uuid) from public, anon, authenticated;
grant execute on function public.device_confirmation_request(uuid,uuid,uuid),public.device_confirmation_commit(uuid),public.device_confirmation_cancel(uuid),public.device_confirmation_view(uuid) to authenticated;
grant execute on function public.device_confirmation_freeze(text,uuid,uuid,uuid,bigint),public.device_confirmation_renew(text,uuid,uuid,uuid,bigint,uuid),public.device_confirmation_status(text,uuid) to anon, authenticated;
