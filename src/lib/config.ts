/**
 * 全站共用配置。
 * 注意：BOARD_CHANNEL 必须和 ESP32 固件 platformio/src/config.h 里的 BOARD_CHANNEL 完全一致，
 * 网页和传感器设备才会“配对”。公共 MQTT 服务器是大家共用的，所以频道名请保持随机、不要告诉别人。
 */
export const BOARD_CHANNEL = "xy-7c3f9a1e5b42";

export const TOPIC_PRESS = `xingyi-board/${BOARD_CHANNEL}/press`;
export const TOPIC_STATUS = `xingyi-board/${BOARD_CHANNEL}/status`;

/** 同时连接两个公共 MQTT 服务器（WebSocket + TLS），任意一个可用即可；计数数据是“累计值”，重复收到不会多算 */
export const BROKER_URLS = [
  "wss://broker.emqx.io:8084/mqtt",
  "wss://broker.hivemq.com:8884/mqtt",
];

/** 你的 GitHub 仓库与 GitHub Pages 正式网址 */
export const GITHUB_REPO = "bizuming1-ui/xingyi";
export const GITHUB_PAGES_URL = "https://bizuming1-ui.github.io/xingyi/";

/**
 * 两个光电对射传感器的计数规则（必须与固件 platformio/src/config.h 保持一致）：
 * A(GPIO4)、B(GPIO5) 同时遮光并稳定 CONFIRM_MS -> 现在 +1；
 * 之后必须 A、B 都通光并稳定 CLEAR_MS 才重新待命（一个物体只算 1 次）。
 */
export const SENSOR_A_PIN = 4;
export const SENSOR_B_PIN = 5;
export const CONFIRM_MS = 2;
export const CLEAR_MS = 3;
