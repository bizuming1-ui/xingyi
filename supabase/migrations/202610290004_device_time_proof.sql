-- ADDITIVE / NOT DEPLOYED. Time proof only; not device authentication and not
-- permission to send credentials over an unverified connection. Apply only
-- after gateway rate limiting, bootstrap transport, and real-device security
-- review. Depends on device ingest migration and pgcrypto in extensions.
-- device_id and the random one-use nonce are not secrets. The device secret or
-- its hash must never be sent to this endpoint or placed in logs/URLs.
create function public.device_time_proof(p_device uuid, p_nonce text)
returns jsonb language plpgsql security definer
set search_path = public, extensions, pg_temp as $$
declare v_key bytea; v_seconds text; v_message text; v_tag text;
begin
  if p_device is null or p_nonce is null or p_nonce !~ '^[0-9a-f]{64}$' then
    raise exception '无效的时间请求' using errcode='22023';
  end if;
  select d.secret_hash into v_key from public.device_endpoints d
    where d.id=p_device and d.revoked_at is null;
  if not found then return null; end if;
  v_seconds := floor(extract(epoch from clock_timestamp()))::bigint::text;
  v_message := 'xingyi-time-v1|' || p_device::text || '|' || p_nonce || '|' || v_seconds;
  v_tag := encode(extensions.hmac(convert_to(v_message,'UTF8'),v_key,'sha256'),'hex');
  return jsonb_build_object('unix_seconds',v_seconds,'tag',v_tag);
end $$;
revoke all on function public.device_time_proof(uuid,text) from public, anon, authenticated;
grant execute on function public.device_time_proof(uuid,text) to anon, authenticated;
