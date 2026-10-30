-- Isolated-test only. Does not alter PR1 manual accumulation RPC.
create table public.test_admin_count_replacements (
  id bigint generated always as identity primary key,
  order_id uuid not null references public.orders(id),
  before_count bigint not null,
  after_count bigint not null,
  created_at timestamptz not null default now()
);
alter table public.test_admin_count_replacements enable row level security;
revoke all on public.test_admin_count_replacements from public, anon, authenticated;

create function public.test_admin_replace_count(p_password text,p_order uuid,p_expected bigint,p_target bigint)
returns jsonb language plpgsql security definer set search_path=public,pg_temp as $$
declare o public.orders;
begin
  if not public.check_admin(p_password) then return null; end if;
  if p_order is null or p_expected is null or p_target is null or p_expected<0 or p_target<0 or p_target>9007199254740991 then
    return jsonb_build_object('status','invalid');
  end if;
  select * into o from public.orders where id=p_order and not archived for update;
  if not found then return jsonb_build_object('status','missing'); end if;
  if o.current_count is distinct from p_expected then return jsonb_build_object('status','conflict'); end if;
  if p_target>o.total then return jsonb_build_object('status','invalid'); end if;
  update public.orders set current_count=p_target where id=o.id;
  insert into public.test_admin_count_replacements(order_id,before_count,after_count) values(o.id,o.current_count,p_target);
  return jsonb_build_object('status','replaced','current',p_target);
end $$;
revoke all on function public.test_admin_replace_count(text,uuid,bigint,bigint) from public,anon,authenticated;
grant execute on function public.test_admin_replace_count(text,uuid,bigint,bigint) to authenticated;
