/*
 * ESP32-S3 生产看板按键
 *   按一下按键      -> 看板“现在”+1
 *   长按按键        -> 看板“现在”快速连加
 *
 * 设计要点（为了“稳定、低延迟、不丢数”）：
 *  1. 按键检测单独跑在一个 FreeRTOS 任务里（1ms 轮询），WiFi/MQTT 断线重连时再慢也不会卡住按键。
 *  2. 按下的瞬间立即计数（前沿触发 + 防抖锁定），没有“等防抖结束再判定”的延迟。
 *  3. 上传的是“累计按键次数”而不是“+1 事件”：网络丢包、重发、合并发送都不会多算或少算；
 *     网页断线重连后还能自动补回漏掉的次数。
 *  4. WiFi 关闭省电模式、TCP_NODELAY、MQTT 保活 15 秒、断线自动重连，两个公共服务器自动切换。
 */
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#if __has_include(<esp_random.h>)
#include <esp_random.h>
#else
#include <esp_system.h>
#endif

#include "config.h"

// ---------- MQTT 服务器（公共，免注册）：连不上时自动切换到下一个 ----------
struct Broker {
  const char *host;
  uint16_t port;
};
static const Broker BROKERS[] = {
    {"broker.emqx.io", 1883},
    {"broker.hivemq.com", 1883},
};
static const size_t BROKER_COUNT = sizeof(BROKERS) / sizeof(BROKERS[0]);

static const char *TOPIC_PRESS = "xingyi-board/" BOARD_CHANNEL "/press";
static const char *TOPIC_STATUS = "xingyi-board/" BOARD_CHANNEL "/status";

// ---------- 状态 ----------
static volatile uint32_t g_count = 0;       // 开机以来的累计按键次数（只有按键任务会写）
static volatile uint32_t g_lastPressMs = 0; // 最近一次计数的时间（给 LED 闪烁用）
static char g_session[9];                   // 本次开机的随机编号，网页用它区分“重启过的设备”
static char g_clientId[32];

static WiFiClient net;
static PubSubClient mqtt(net);

static uint32_t g_published = 0;
static bool g_forcePublish = true;
static size_t g_broker = 0;
static uint8_t g_fail = 0;
static uint32_t g_lastTry = 0;
static uint32_t g_lastPub = 0;
static uint8_t g_resendLeft = 0;
static uint32_t g_resendAt = 0;

// ---------- 板载 RGB 灯（只在 loop 任务里调用）----------
static void led(uint8_t r, uint8_t g, uint8_t b) {
#if USE_RGB_LED && defined(RGB_BUILTIN)
  static uint32_t last = 0xFFFFFFFF;
  uint32_t v = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  if (v != last) {
    last = v;
    neopixelWrite(RGB_BUILTIN, r, g, b);
  }
#else
  (void)r;
  (void)g;
  (void)b;
#endif
}

// ---------- 按键任务：1ms 轮询，按下瞬间计数，长按连加 ----------
static void buttonTask(void *) {
  bool pressed = false;
  uint32_t lastEdge = 0, pressAt = 0, nextAt = 0, releaseSince = 0;

  for (;;) {
    const bool low = digitalRead(BUTTON_PIN) == LOW; // 接 GND：按下为低电平
    const uint32_t now = millis();

    if (!pressed) {
      if (low && (now - lastEdge) >= DEBOUNCE_MS) {
        pressed = true;
        lastEdge = now;
        pressAt = now;
        nextAt = now + HOLD_DELAY_MS;
        releaseSince = 0;
        g_count = g_count + 1; // 按一下 +1（立即）
        g_lastPressMs = now;
      }
    } else if (!low) {
      if (releaseSince == 0) releaseSince = now;
      if ((now - releaseSince) >= RELEASE_MS) {
        pressed = false;
        lastEdge = now;
        releaseSince = 0;
      }
    } else {
      releaseSince = 0;
      if ((int32_t)(now - nextAt) >= 0) { // 长按：到点就 +1
        g_count = g_count + 1;
        g_lastPressMs = now;
        const uint32_t held = now - pressAt;
        const uint32_t step = held < 1000 ? REPEAT_SLOW_MS : (held < 2500 ? REPEAT_MID_MS : REPEAT_FAST_MS);
        nextAt += step;
        if ((int32_t)(now - nextAt) >= 0) nextAt = now + step; // 落后太多就重新对齐，避免突发连加
      }
    }
    vTaskDelay(1);
  }
}

// ---------- WiFi：非阻塞，掉线自动重连 ----------
static void wifiTick(uint32_t now) {
  static bool started = false;
  static uint32_t lastBegin = 0;
  if (WiFi.status() == WL_CONNECTED) {
    started = true;
    return;
  }
  if (!started || (now - lastBegin) > 15000) {
    Serial.println("[wifi] connecting...");
    WiFi.disconnect(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    started = true;
    lastBegin = now;
  }
}

// ---------- MQTT：连接 / 重连 / 上传累计值 ----------
static void mqttTick(uint32_t now) {
  if (WiFi.status() != WL_CONNECTED) {
    if (net.connected()) net.stop();
    return;
  }

  if (!mqtt.connected()) {
    if ((now - g_lastTry) < 1500) return;
    g_lastTry = now;
    const Broker &b = BROKERS[g_broker];
    Serial.printf("[mqtt] connecting %s:%u ...\n", b.host, b.port);
    mqtt.setServer(b.host, b.port);
    // 遗嘱消息：设备断电/断网后，服务器会自动通知网页“设备离线”
    if (mqtt.connect(g_clientId, nullptr, nullptr, TOPIC_STATUS, 0, true, "offline")) {
      net.setNoDelay(true); // 关闭 Nagle，小包立刻发出，降低延迟
      g_fail = 0;
      mqtt.publish(TOPIC_STATUS, "online", true);
      g_forcePublish = true; // 连上后立刻上传一次当前累计值
      Serial.println("[mqtt] connected");
    } else {
      Serial.printf("[mqtt] failed rc=%d\n", mqtt.state());
      if (++g_fail >= 3) { // 连续失败 3 次，换另一个服务器
        g_fail = 0;
        g_broker = (g_broker + 1) % BROKER_COUNT;
      }
    }
    return;
  }

  mqtt.loop();

  const uint32_t c = g_count;
  const bool changed = (c != g_published) || g_forcePublish;
  // 累计值没变化时，补发两次同样的值（网页收到重复值会自动忽略，不会多算）
  const bool resend = !changed && g_resendLeft > 0 && (int32_t)(now - g_resendAt) >= 0;

  if ((changed || resend) && (now - g_lastPub) >= PUBLISH_MIN_MS) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"s\":\"%s\",\"c\":%lu}", g_session, (unsigned long)c);
    // retained=true：网页晚打开/断线重连，也能立刻拿到最新累计值
    if (mqtt.publish(TOPIC_PRESS, buf, true)) {
      g_lastPub = now;
      if (changed) {
        g_published = c;
        g_forcePublish = false;
        g_resendLeft = 2;
        g_resendAt = now + REPUBLISH_FIRST_MS;
        Serial.printf("[press] total=%lu\n", (unsigned long)c);
      } else {
        g_resendLeft--;
        g_resendAt = now + REPUBLISH_NEXT_MS;
      }
    }
  }
}

static void ledTick(uint32_t now) {
  if ((now - g_lastPressMs) < 60 && g_lastPressMs != 0) {
    led(40, 40, 40); // 按键瞬间闪白
  } else if (WiFi.status() != WL_CONNECTED) {
    led(40, 0, 0); // 红：没有 WiFi
  } else if (!mqtt.connected()) {
    led(30, 20, 0); // 黄：WiFi 已连，服务器未连
  } else {
    led(0, 20, 0); // 绿：在线
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  snprintf(g_session, sizeof(g_session), "%08x", (unsigned)esp_random());
  snprintf(g_clientId, sizeof(g_clientId), "xingyi-s3-%s", g_session);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false); // 关闭 WiFi 省电模式：延迟更低、更稳定

  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(3);

  // 按键任务固定在核心 1，优先级高于 loop，不受网络阻塞影响
  xTaskCreatePinnedToCore(buttonTask, "button", 3072, nullptr, 5, nullptr, 1);

  Serial.printf("\n[boot] session=%s channel=%s button=GPIO%d\n", g_session, BOARD_CHANNEL, BUTTON_PIN);
}

void loop() {
  const uint32_t now = millis();
  wifiTick(now);
  mqttTick(now);
  ledTick(now);
  delay(1);
}
