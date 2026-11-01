/**
 * 全站共用配置。
 * 注意：BOARD_CHANNEL 必须和 ESP32 固件 firmware/src/config.h 里的 BOARD_CHANNEL 完全一致，
 * 网页和按键才会“配对”。公共 MQTT 服务器是大家共用的，所以频道名请保持随机、不要告诉别人。
 */
export const BOARD_CHANNEL = "xy-7c3f9a1e5b42";

export const TOPIC_PRESS = `xingyi-board/${BOARD_CHANNEL}/press`;
export const TOPIC_STATUS = `xingyi-board/${BOARD_CHANNEL}/status`;

/** 同时连接两个公共 MQTT 服务器（WebSocket + TLS），任意一个可用即可；按键数据是“累计值”，重复收到不会多算 */
export const BROKER_URLS = [
  "wss://broker.emqx.io:8084/mqtt",
  "wss://broker.hivemq.com:8884/mqtt",
];

/** 你的 GitHub 仓库与 GitHub Pages 正式网址 */
export const GITHUB_REPO = "bizuming1-ui/xingyi";
export const GITHUB_PAGES_URL = "https://bizuming1-ui.github.io/xingyi/";

/** 长按连加节奏（必须与固件 config.h 保持一致）：按下立即 +1；按住 400ms 后开始连加，越按越快 */
export const HOLD_DELAY_MS = 400;
export function holdInterval(heldMs: number): number {
  if (heldMs < 1000) return 100; // 每秒 10 次
  if (heldMs < 2500) return 50; // 每秒 20 次
  return 25; // 每秒 40 次
}
