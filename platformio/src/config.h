#pragma once

// ===== 1. 必改：WiFi（ESP32 只支持 2.4GHz，不支持 5GHz）=====
#define WIFI_SSID     "你的WiFi名称"
#define WIFI_PASSWORD "你的WiFi密码"

// ===== 2. 频道：必须和网站 src/lib/config.ts 里的 BOARD_CHANNEL 完全一致 =====
// 公共 MQTT 服务器是大家共用的，频道名请保持随机，不要告诉别人。
#define BOARD_CHANNEL "xy-7c3f9a1e5b42"

// ===== 3. 按键引脚：一端接 GND，另一端接 GPIO4 =====
#define BUTTON_PIN 4

// ===== 4. 按键手感（与网站 src/lib/config.ts 的“模拟按键”保持一致）=====
#define DEBOUNCE_MS      20    // 按下后的防抖锁定时间（按下瞬间立即计数，不增加延迟）
#define RELEASE_MS       15    // 松开需要稳定多久才算松开
#define HOLD_DELAY_MS    400   // 按住多久后开始连加
// 连加速度：按住不足 1 秒每 100ms 一次（10次/秒），1~2.5 秒每 50ms 一次（20次/秒），之后每 25ms 一次（40次/秒）
#define REPEAT_SLOW_MS   100
#define REPEAT_MID_MS    50
#define REPEAT_FAST_MS   25

// 数据上传最小间隔：按下后的第一包立即发出；长按连加时每 100ms 合并发送一次
// （实测公共服务器每秒超过约 10 条会限流丢包；上传的是“累计值”，合并发送不会丢数）
#define PUBLISH_MIN_MS   100
// 停止按键后，把最新累计值再补发 2 次（0.3 秒、1.5 秒后），防止最后一包被公共服务器丢掉
#define REPUBLISH_FIRST_MS  300
#define REPUBLISH_NEXT_MS   1200

// ===== 5. 板载 RGB 灯状态指示（没有或不想用就改成 0）=====
// 红=没连上WiFi  黄=WiFi已连、服务器未连  绿=在线  按键瞬间闪白
#define USE_RGB_LED 1
