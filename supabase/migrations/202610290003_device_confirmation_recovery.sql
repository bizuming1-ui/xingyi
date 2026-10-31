-- ADDITIVE / NOT DEPLOYED. Requires 202610290001 and 202610290002.
-- An operator can discover an open barrier after switching browsers or losing
-- local state. This does not reveal device secrets or the lease nonce, does not
-- create/commit/cancel a barrier, and does not infer present connectivity.
create function public.device_confirmation_open(p_device uuid)
returns jsonb language plpgsql security definer set search_path = public, pg_temp as $$
declare b public.device_confirmation_barriers;
begin
  if not public.is_operator() then raise exception '无访问权限'; end if;
  if p_device is null then raise exception '设备参数无效'; end if;
  perform 1 from public.device_endpoints where id=p_device and revoked_at is null;
  if not found then raise exception '设备不存在或已撤销'; end if;
  select * into b from public.device_confirmation_barriers
    where device_id=p_device and state in ('requested','ready');
  if not found then return null; end if;
  return jsonb_build_object(
    'id',b.id, 'request_id',b.request_id, 'device_id',b.device_id, 'order_id',b.order_id,
    'state',case when b.state='ready' and b.lease_until<=clock_timestamp() then 'expired' else b.state end
  );
end $$;
revoke all on function public.device_confirmation_open(uuid) from public, anon, authenticated;
grant execute on function public.device_confirmation_open(uuid) to authenticated;
