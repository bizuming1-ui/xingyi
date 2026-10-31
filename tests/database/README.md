# 隔离数据库测试

`npm ci --prefix tests/database && npm test --prefix tests/database`

PGlite 在独立进程中创建临时 PostgreSQL 数据库及模拟角色，不连接 Supabase，不读取或更改生产数据。测试依次安装 PR1 基础 schema 与尚未部署的三条设备迁移，检查权限、设备密钥、重复/乱序请求、服务端递增绑定版本、改绑后的离线补传、溢出回滚、冻结/续约/过期、原子封顶、确认重试、请求 ID 冲突回滚、跨浏览器未结束确认查询，以及原有手动确认封顶。

PGlite 使用单进程数据库，不能证明真正的多连接并发、Supabase Realtime、网络传输、线上权限配置或真机表现。设备端冻结与确认屏障尚未实现，不可因此部署迁移。

## 固件与隔离数据库组件联测

设置 `ARDUINOJSON_INCLUDE` 指向 ArduinoJson 6.21.5 的 `src` 目录后运行 `npm run firmware-test --prefix tests/database`。测试通过本地模拟传输，把实际 C++ 按键、计数、RPC 和同步调度组件接到临时 PGlite 数据库：覆盖响应丢失后的幂等重试、改绑前旧数归属、冻结误按取消、后续上传和最终封顶。设备凭据、账号和数据均为测试临时值。

这不测试实际 TLS、Wi-Fi、NVS、断电、双任务调度、实时延迟、多连接数据库并发或真实 Supabase，更不能替代真机测试。
