# PR1 网页回归测试

```sh
npm ci
npm ci --prefix tests/e2e
npm test --prefix tests/e2e
```

测试在本机启动 Vite，使用虚构的 Supabase 地址、账号、生产单和拦截的模拟响应；非本机和模拟服务的网页请求会被阻止，不会连接或修改正式生产数据。它在 Chromium 中覆盖桌面登录失败提示、手机单屏显示、二维码解码、生产单显示、数量封顶、确认请求响应丢失后的同一请求 ID 重试、管理员历史查看。

模拟服务不会证明真实 Supabase 的权限、并发、实时同步、跨设备行为，也不能代替实际安卓和 iOS 设备测试。`@sparticuz/chromium` 为测试独立依赖，不打包进网页或固件。
