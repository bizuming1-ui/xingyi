## 生产看板（含 ESP32 双光电对射传感器 + 浏览器模拟器 + 复制代码界面）

### 功能

* 页面版式与参考图一致：生产单、产品、生产总数、当前生产数、现在、确认、右上角二维码。
* **整个看板固定在窗口内，不需要鼠标滚动**：窗口大小变化时整体等比缩放（手机竖屏自动上下排列并压缩高度）。
* **两个光电对射（槽型）传感器代替按键**：传感器 A 接 GPIO4、传感器 B 接 GPIO5，**A、B 同时遮光一次 → “现在”+1**。
  一个物体通过只算 1 次（A、B 都通光后才重新待命）；单个传感器触发、毛刺、抖动都不会误计数。
* 生产单 / 产品 / 生产总数 / 现在：也可以手动输入。
* 当前生产数：**不可手动填写**。点击“确认”后，“现在”里的数字累加到“当前生产数”，然后“现在”清零。
* 二维码：编码的是网站地址（`…/#/site`），手机扫码即可打开纯网站页面。
* 浏览器模拟器：地址栏、正式网址、“模拟传感器 A / B”（没有硬件也能测试，判定逻辑和固件完全一致）、“复制代码”按钮（分成「GitHub 代码」和「PlatformIO 代码」两类）。

### 接线（两个传感器相同）

| 传感器引脚 | 接到 ESP32 |
| --- | --- |
| VCC | **3V3**（不要接 5V，5V 会让 OUT 输出 5V，损坏 ESP32） |
| GND | GND |
| OUT | 传感器 A → GPIO4；传感器 B → GPIO5 |

遮光时的输出电平：常见 LM393 槽型模块是“遮光=高电平”，固件默认 `SENSOR_BLOCKED_LEVEL HIGH`。
如果槽里什么都没有却在计数（或串口提示一直遮光），把 `platformio/src/config.h` 里的 `HIGH` 改成 `LOW`。

### ESP32 如何把数据送到网页（无需自己的服务器）

```
传感器A(GPIO4) ┐
               ├→ ESP32-S3 --WiFi/MQTT(两个服务器同时发)--> 公共 MQTT 服务器 --WSS--> 网页（GitHub Pages）
传感器B(GPIO5) ┘
```

* 固件上传的是“累计计数次数”，网页自己算增量：丢包、重发、两个服务器同时送达都不会多算少算。
* **超低延迟**：传感器用硬件中断唤醒检测任务（微秒级）；计数后立刻通知上传任务；两个公共服务器**同时**上传，网页谁先到用谁。
* **稳定**：检测任务与网络完全独立；每个服务器一个独立任务，一个断了不影响另一个；WiFi 省电关闭、最大发射功率、DNS 缓存、断线自动重连。
* 网页和固件通过“频道名”配对：`src/lib/config.ts` 的 `BOARD_CHANNEL` 必须和固件 `src/config.h` 的 `BOARD_CHANNEL` 一致。
* 看板右上角的状态灯：绿=传感器设备在线，黄=已连服务器等待设备，红=未连上服务器。

### 本地运行

```
npm install
npm run dev      # 开发
npm run build    # 构建，产物是 dist/index.html（单文件）
```

### 发布到 GitHub Pages

1. 把本仓库所有文件（含 `.github/workflows/deploy.yml`）提交到 `main` 分支。
2. 仓库 **Settings → Pages → Build and deployment → Source** 选择 **GitHub Actions**。
3. 到 **Actions** 页面等待 “Deploy to GitHub Pages” 变成绿色（约 1 分钟）。
4. 网址：
   * 完整页面（含浏览器模拟器）：`https://bizuming1-ui.github.io/xingyi/`
   * 纯网站页面（二维码指向这里）：`https://bizuming1-ui.github.io/xingyi/#/site`

### 固件（PlatformIO）

固件在 `platformio/` 文件夹：`platformio.ini`、`src/config.h`、`src/main.cpp`。
把它们也提交到仓库后，线上网站的“复制代码”界面就能显示 PlatformIO 代码。
