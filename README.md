## 生产看板（含 ESP32 按键 + 浏览器模拟器 + 复制代码界面）

### 功能

* 页面版式与参考图一致：生产单、产品、生产总数、当前生产数、现在、确认、右上角二维码。
* **整个看板固定在窗口内，不需要鼠标滚动**：窗口大小变化时整体等比缩放（手机竖屏自动上下排列并压缩高度）。
* **ESP32-S3 按键**：按一下 → “现在”+1；长按 → “现在”快速连加（按住 0.4 秒后开始，越按越快）。
* 生产单 / 产品 / 生产总数 / 现在：也可以手动输入。
* 当前生产数：**不可手动填写**。点击“确认”后，“现在”里的数字累加到“当前生产数”，然后“现在”清零。
* 二维码：编码的是网站地址（`…/#/site`），手机扫码即可打开纯网站页面。
* 浏览器模拟器：地址栏、正式网址、“模拟按键”（没有硬件也能测试）、“复制代码”按钮（分成「GitHub 代码」和「PlatformIO 代码」两类）。

### ESP32 按键如何把数据送到网页（无需自己的服务器）

```
按键(GPIO4-GND) → ESP32-S3 --WiFi/MQTT--> 公共 MQTT 服务器 --WSS--> 网页（GitHub Pages）
```

* 固件上传的是“累计按键次数”，网页自己算增量：丢包、重发、两个服务器同时送达都不会多算少算。
* 网页同时连接 `broker.emqx.io` 和 `broker.hivemq.com`，固件连不上其中一个会自动切换。
* 网页和固件通过“频道名”配对：`src/lib/config.ts` 的 `BOARD_CHANNEL` 必须和固件 `src/config.h` 的 `BOARD_CHANNEL` 一致。
* 看板右上角的状态灯：绿=按键设备在线，黄=已连服务器等待设备，红=未连上服务器。

### 本地运行

    npm install
    npm run dev      # 开发
    npm run build    # 构建，产物是 dist/index.html（单文件）

### 发布到 GitHub Pages

1. 把本仓库所有文件（含 `.github/workflows/deploy.yml`）提交到 `main` 分支。
2. 仓库 **Settings → Pages → Build and deployment → Source** 选择 **GitHub Actions**。
3. 到 **Actions** 页面等待 “Deploy to GitHub Pages” 变成绿色（约 1 分钟）。
4. 网址：
   * 完整页面（含浏览器模拟器）：`https://bizuming1-ui.github.io/xingyi/`
   * 纯网站页面（二维码指向这里）：`https://bizuming1-ui.github.io/xingyi/#/site`

### 固件（PlatformIO）

固件在 `firmware/` 文件夹（可选上传到仓库，只用于让线上网站的“复制代码”也能显示 PlatformIO 代码）：
`platformio.ini`、`src/config.h`、`src/main.cpp`。接线：按键一端 GND，另一端 GPIO4。
