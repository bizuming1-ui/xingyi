# 界面1 · 生产管理

参考原图制作的网页应用：扫码进入生产单列表，多设备登录共享账号后可选择生产单操作。生产数实时同步（Supabase Realtime，断线时每 5 秒重新读取）；数量确认由数据库事务串行累计、封顶并记录。每台设备的“现在”独立；请求结果不确定时保留请求 ID，重试不会重复累计。管理员使用单独密码维护及归档生产单、查询确认历史。

## 当前项目的首次配置

已建立新加坡区域 Supabase 项目 `cdaplcswcdjfbkdbabji`，并初始化 `supabase/schema.sql` 和关闭公开注册。请在 Supabase 控制台完成以下操作（不要把密码发到聊天、提交 GitHub 或放入网页）：

1. 在 Authentication → Users 中 **Add user** 创建一个共用的邮箱、强密码，勾选确认邮箱。操作人员使用该账号登录。
2. 在 SQL Editor 中执行下方语句，将邮箱和管理员密码替换成自己的值。管理员密码应是独立的高强度密码。执行后请清理 SQL 编辑器中可能保存的查询历史；不要共享这条语句。语句必须影响 1 行，如果为 0 行请检查用户邮箱。

```sql
insert into public.app_settings (id, shared_user_id, admin_password_hash)
select true, id, extensions.crypt('替换为管理员强密码', extensions.gen_salt('bf'))
from auth.users where email = '替换为共用账号邮箱'
on conflict (id) do update set shared_user_id=excluded.shared_user_id,
  admin_password_hash=excluded.admin_password_hash;
```

管理员连续输错 5 次后，所有操作人员的管理员操作会锁定 15 分钟；普通数量提交不受影响。共享账号不区分个人，确认记录不含个人身份；需要个人审计时应改为独立账号和权限模型。密码有单引号时需按 SQL 规则转义。数据库项目创建时的初始数据库密码曾出现在工具执行记录中，请在 Supabase 项目设置中重置数据库密码；网页不需要数据库密码。

3. GitHub 仓库 Settings → Pages → Build and deployment 选择 **GitHub Actions**。工作流默认连接本项目；可通过仓库 Actions Variables `VITE_SUPABASE_URL` 和 `VITE_SUPABASE_ANON_KEY` 覆盖项目 URL 和 **publishable/anon** 公钥。默认值是可公开的客户端配置，权限仍由数据库 RLS 控制。禁止使用 service_role 或 secret key。代码合并到 main 后工作流构建并部署，访问 `https://bizuming1-ui.github.io/xingyi/`（成功部署前该地址不能使用）。如果 Pages 未启用或 Actions 失败，检查仓库 Pages 设置和工作流日志。
4. 在生产环境使用前，使用两台真实设备核对扫码、登录、并发提交、自动同步、封顶、断网重试、管理员权限和归档。

## 本地运行

```bash
npm ci
cp .env.example .env
# 填写项目 URL 和 publishable/anon key；切勿使用管理员密钥
npm run dev
npm run build
```

多设备访问需要公开 HTTPS 部署；本地开发 URL 或预览环境不会自动变成永久公网服务。浏览器必须允许 JavaScript、localStorage 和 WebSocket。若网络请求超时，页面会保留待确认请求供重试；放弃重试前必须核对当前数值，避免重复提交。浏览器清理站点数据可能丢失待重试请求。管理员密码仅发送给 Supabase RPC，不能直接存入前端。

## 自行建立新项目

在全新 Supabase 项目的 SQL Editor 中运行 `supabase/schema.sql`，关闭公开注册，再按照上方首次配置步骤创建用户和设置密码。数据库服务端规则是权限的最终依据，不要放宽 RLS 或给客户端写入表的权限。

## 手机扫码及单屏操作

二维码指向正式网站的生产单列表，不包含账号或密码。Arena 预览中生成的二维码指向 GitHub Pages 正式网址，而非无法直接通过手机访问的 Arena 预览地址；必须先完成正式部署。其他部署可通过 `VITE_PUBLIC_APP_URL` 指定公开网址。手机需要能访问二维码里的 HTTPS 地址，扫码后使用同一个共享账号登录；安卓 Chrome 和 iPhone Safari 使用相同功能。电脑和手机应打开同一个已经部署的网页地址：Arena 临时预览、localhost 及局域网地址不保证能从其他设备访问，GitHub Pages 未部署前二维码不等于永久公网入口。界面不改变原有权限规则：管理员操作仍需管理员密码。

正常使用时各页面适配屏幕，不依靠页面滚动；列表和历史记录使用上一页、下一页。手机输入时若屏幕键盘遮挡内容，浏览器可临时滚动。极小屏幕、浏览器大字号及系统辅助功能可能需要调整缩放或横竖屏，实际设备兼容性仍需使用真实安卓和 iPhone 测试。
