# 隔离数据库测试

`npm ci --prefix tests/database && npm test --prefix tests/database`

PGlite 在独立进程中创建临时 PostgreSQL 数据库及模拟角色，不连接 Supabase，不读取或更改生产数据。测试依次安装 PR1 基础 schema 与尚未部署的设备接收迁移，检查权限、设备密钥、重复/乱序请求、服务端递增绑定版本、改绑后的离线补传、溢出回滚、冻结/续约/过期、原子封顶、确认重试、请求 ID 冲突回滚，以及原有手动确认封顶。

PGlite 使用单进程数据库，不能证明真正的多连接并发、Supabase Realtime、网络传输、线上权限配置或真机表现。设备端冻结与确认屏障尚未实现，不可因此部署迁移。
