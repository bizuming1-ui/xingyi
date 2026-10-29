// 硬件验证版：仅测试 GPIO 4 按键和 2.4 GHz Wi-Fi。
// 不连接生产数据库，不保存计数，重启后归零；不可用于生产计数。
#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

// 只在您自己电脑的 PlatformIO 文件中填写，不要发给任何人。
const char WIFI_SSID[] = "YOUR_2_4GHZ_SSID";
const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

constexpr int BUTTON_PIN = 4; // 按键另一端接 GND，不能接 3.3V
constexpr uint32_t DEBOUNCE_MS = 25;
constexpr uint32_t HOLD_DELAY_MS = 500;
constexpr uint32_t REPEAT_MS = 100;

class ButtonCounter {
 public:
  ButtonCounter(bool initial, uint32_t now)
      : raw_(initial), stable_(initial), changedAt_(now), pressedAt_(now) {}

  uint32_t tick(bool pressed, uint32_t now) {
    if (pressed != raw_) { raw_ = pressed; changedAt_ = now; }
    uint32_t increments = 0;
    if (raw_ != stable_ && now - changedAt_ >= DEBOUNCE_MS) {
      stable_ = raw_;
      if (stable_) { pressedAt_ = now; repeats_ = 0; increments = 1; }
    }
    if (stable_ && raw_) {
      const uint32_t duration = now - pressedAt_;
      if (duration >= HOLD_DELAY_MS) {
        const uint32_t due = 1 + (duration - HOLD_DELAY_MS) / REPEAT_MS;
        if (due > repeats_) { increments += due - repeats_; repeats_ = due; }
      }
    }
    return increments;
  }
 private:
  bool raw_, stable_;
  uint32_t changedAt_, pressedAt_, repeats_ = 0;
};

ButtonCounter* button = nullptr;
uint64_t count = 0;
bool wifiConfigured = false;
bool wifiConnected = false;
uint32_t lastWifiCheck = 0;
uint32_t lastReconnect = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  static ButtonCounter counter(digitalRead(BUTTON_PIN) == LOW, millis());
  button = &counter;
  Serial.println("\nHARDWARE TEST ONLY: count is RAM-only; no production data.");
  Serial.println("GPIO 4 to GND. Release the button after startup.");
  wifiConfigured = WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "YOUR_2_4GHZ_SSID") != 0 &&
                   strcmp(WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") != 0;
  if (!wifiConfigured) {
    Serial.println("Wi-Fi: not configured. Button test is still available.");
    return;
  }
  WiFi.persistent(false); // Do not write network credentials to Wi-Fi NVS.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastReconnect = millis();
  Serial.println("Wi-Fi: connecting (2.4 GHz required)...");
}

void loop() {
  const uint32_t now = millis();
  const uint32_t added = button->tick(digitalRead(BUTTON_PIN) == LOW, now);
  if (added) {
    count += added;
    Serial.printf("Added: %lu  RAM count: %llu\n", static_cast<unsigned long>(added),
                  static_cast<unsigned long long>(count));
  }
  if (wifiConfigured && now - lastWifiCheck >= 500) {
    lastWifiCheck = now;
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected != wifiConnected) {
      wifiConnected = connected;
      if (connected) { Serial.print("Wi-Fi: connected, IP: "); Serial.println(WiFi.localIP()); }
      else Serial.println("Wi-Fi: disconnected. Button counting continues.");
    }
    if (!connected && now - lastReconnect >= 10000) {
      lastReconnect = now;
      WiFi.reconnect();
      Serial.println("Wi-Fi: retrying...");
    }
  }
  delay(1); // Yield to the ESP32 scheduler; button sampling remains independent of Wi-Fi status.
}
