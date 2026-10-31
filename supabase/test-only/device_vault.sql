-- TEST PROJECT ONLY: encrypted recovery envelope. Never deploy to production
-- without a security review, credential rotation, and a password-change plan.
create table public.test_device_secret_vault (
 device_id uuid primary key references public.device_endpoints(id),
 envelope jsonb not null,
 updated_at timestamptz not null default now()
);
alter table public.test_device_secret_vault enable row level security;
revoke all on public.test_device_secret_vault from public, anon, authenticated;
create function public.test_device_vault_save(p_password text,p_device uuid,p_secret text,p_envelope jsonb)
returns boolean language plpgsql security definer set search_path = public, extensions, pg_temp as $$
begin
 if not public.check_admin(p_password) then return null; end if;
 if p_secret is null or p_secret !~ '^[0-9a-f]{64}$' then return false; end if;
 if p_envelope is null or jsonb_typeof(p_envelope)<>'object' then return false; end if;
 if p_envelope->>'version' is distinct from '1'
   or coalesce(p_envelope->>'salt','') !~ '^[A-Za-z0-9+/]{22}==$'
   or coalesce(p_envelope->>'iv','') !~ '^[A-Za-z0-9+/]{16}$'
   or coalesce(p_envelope->>'ciphertext','') !~ '^[A-Za-z0-9+/]{107}=$'
   or (select count(*) from jsonb_object_keys(p_envelope))<>4 then return false; end if;
 perform 1 from public.device_endpoints where id=p_device and revoked_at is null and secret_hash=extensions.digest(p_secret,'sha256') for update;
 if not found then return false; end if;
 insert into public.test_device_secret_vault(device_id,envelope) values(p_device,p_envelope)
 on conflict(device_id) do update set envelope=excluded.envelope,updated_at=now();
 return true;
end $$;
create function public.test_device_vault_read(p_password text,p_device uuid)
returns jsonb language plpgsql security definer set search_path = public, pg_temp as $$
declare payload jsonb;
begin
 if not public.check_admin(p_password) then return null; end if;
 select v.envelope into payload from public.test_device_secret_vault v join public.device_endpoints d on d.id=v.device_id where d.id=p_device and d.revoked_at is null;
 if payload is null then return jsonb_build_object('state','missing'); end if;
 return jsonb_build_object('state','saved','envelope',payload);
end $$;
revoke all on function public.test_device_vault_save(text,uuid,text,jsonb),public.test_device_vault_read(text,uuid) from public,anon,authenticated;
grant execute on function public.test_device_vault_save(text,uuid,text,jsonb),public.test_device_vault_read(text,uuid) to authenticated;
