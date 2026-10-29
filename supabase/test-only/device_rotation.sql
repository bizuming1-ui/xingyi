-- ISOLATED TEST PROJECT ONLY. Rotation changes the active device credential;
-- it does not prove an ESP32 has drained its offline queue or has been flashed.
create table public.test_device_credential_state (
 device_id uuid primary key references public.device_endpoints(id),
 revision bigint not null check(revision between 1 and 9007199254740991)
);
create table public.test_device_credential_operations (
 request_id uuid primary key,
 device_id uuid not null references public.device_endpoints(id),
 revision bigint not null,
 created_at timestamptz not null default now()
);
alter table public.test_device_credential_state enable row level security;
alter table public.test_device_credential_operations enable row level security;
revoke all on public.test_device_credential_state,public.test_device_credential_operations from public,anon,authenticated;

create function public.test_device_rotation_revision(p_password text,p_device uuid)
returns jsonb language plpgsql security definer set search_path=public,pg_temp as $$
declare rev bigint;
begin
 if not public.check_admin(p_password) then return null; end if;
 select coalesce(s.revision,0) into rev from public.device_endpoints d left join public.test_device_credential_state s on s.device_id=d.id
 where d.id=p_device and d.revoked_at is null;
 if not found then return jsonb_build_object('status','missing'); end if;
 return jsonb_build_object('status','ready','revision',rev);
end $$;

create function public.test_device_rotate(p_password text,p_device uuid,p_secret text,p_envelope jsonb,p_revision bigint,p_request uuid)
returns jsonb language plpgsql security definer set search_path=public,extensions,pg_temp as $$
declare current_revision bigint; prior public.test_device_credential_operations; device public.device_endpoints;
begin
 if not public.check_admin(p_password) then return null; end if;
 if p_device is null or p_request is null or p_revision is null or p_revision<0 or p_revision>=9007199254740991 or p_secret is null or p_secret !~ '^[0-9a-f]{64}$' then raise exception 'invalid rotation request'; end if;
 if p_envelope is null or jsonb_typeof(p_envelope)<>'object' then raise exception 'invalid envelope'; end if;
 if p_envelope->>'version' is distinct from '1' or coalesce(p_envelope->>'salt','') !~ '^[A-Za-z0-9+/]{22}==$' or coalesce(p_envelope->>'iv','') !~ '^[A-Za-z0-9+/]{16}$' or coalesce(p_envelope->>'ciphertext','') !~ '^[A-Za-z0-9+/]{107}=$' or (select count(*) from jsonb_object_keys(p_envelope))<>4 then raise exception 'invalid envelope'; end if;
 select * into device from public.device_endpoints d where d.id=p_device and d.revoked_at is null for update;
 if not found then return jsonb_build_object('status','missing'); end if;
 select * into prior from public.test_device_credential_operations where request_id=p_request;
 if found then
  if prior.device_id=p_device and prior.revision=p_revision+1 and device.secret_hash=extensions.digest(p_secret,'sha256') and exists(select 1 from public.test_device_secret_vault where device_id=p_device and envelope=p_envelope) then
   return jsonb_build_object('status','committed','revision',prior.revision);
  end if;
  return jsonb_build_object('status','conflict');
 end if;
 select coalesce((select s.revision from public.test_device_credential_state s where s.device_id=p_device),0) into current_revision;
 if current_revision<>p_revision then return jsonb_build_object('status','conflict'); end if;
 update public.device_endpoints set secret_hash=extensions.digest(p_secret,'sha256') where id=p_device;
 insert into public.test_device_secret_vault(device_id,envelope) values(p_device,p_envelope) on conflict(device_id) do update set envelope=excluded.envelope,updated_at=now();
 insert into public.test_device_credential_state(device_id,revision) values(p_device,p_revision+1) on conflict(device_id) do update set revision=excluded.revision;
 insert into public.test_device_credential_operations(request_id,device_id,revision) values(p_request,p_device,p_revision+1);
 return jsonb_build_object('status','committed','revision',p_revision+1);
end $$;
create function public.test_device_rotation_result(p_password text,p_device uuid,p_request uuid)
returns jsonb language plpgsql security definer set search_path=public,pg_temp as $$
declare op public.test_device_credential_operations; current_rev bigint;
begin
 if not public.check_admin(p_password) then return null; end if;
 select * into op from public.test_device_credential_operations where request_id=p_request and device_id=p_device;
 if not found then return jsonb_build_object('status','unknown'); end if;
 select s.revision into current_rev from public.test_device_credential_state s join public.device_endpoints d on d.id=s.device_id where s.device_id=p_device and d.revoked_at is null;
 if not found or current_rev<>op.revision then return jsonb_build_object('status','superseded'); end if;
 return jsonb_build_object('status','committed','revision',op.revision);
end $$;
revoke all on function public.test_device_rotation_revision(text,uuid),public.test_device_rotate(text,uuid,text,jsonb,bigint,uuid),public.test_device_rotation_result(text,uuid,uuid) from public,anon,authenticated;
grant execute on function public.test_device_rotation_revision(text,uuid),public.test_device_rotate(text,uuid,text,jsonb,bigint,uuid),public.test_device_rotation_result(text,uuid,uuid) to authenticated;
