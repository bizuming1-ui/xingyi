#pragma once
#include "WifiSupervisor.h"
#include <WiFi.h>
#include <esp_wifi.h>

// No credentials persisted. Caller serializes radio lifecycle against entropy
// and owns the station on the network task; no TLS operation is in flight here.
class EspWifiStation final : public WifiStation {
 public:
  bool start(const char* ssid,const char* password) override {
    wasConnected_=false;powerSaveDiagnostic_=-1;powerSaveSetResult_=-2;
    WiFi.persistent(false);
    if(!WiFi.mode(WIFI_STA))return false;
    if(!WiFi.setAutoReconnect(false))return false;
    // Arduino 2.0.17's asynchronous STA_START handler reapplies its cached
    // sleep mode. Align that cache too; its boolean return does not indicate
    // the current driver state. Recheck after connection and periodically.
    (void)WiFi.setSleep(false);
    checkPowerSave();
    return WiFi.begin(ssid,password)!=WL_CONNECT_FAILED;
  }
  bool connected() override {
    const bool online=WiFi.status()==WL_CONNECTED;
    if(!online){wasConnected_=false;return false;}
    const uint32_t now=millis();
    if(!wasConnected_ || static_cast<uint32_t>(now-lastCheck_)>=POWER_SAVE_CHECK_MS) {
      wasConnected_=true;lastCheck_=now;checkPowerSave();
    }
    return true;
  }
  void disconnect() override {wasConnected_=false;WiFi.disconnect(false,false);}
  // Most recent driver read, not proof of the current RF behavior. setResult:
  // -2 no IDF correction yet, -1 failed, 0 succeeded.
  int powerSaveDiagnostic() const {return powerSaveDiagnostic_;}
  int powerSaveSetResult() const {return powerSaveSetResult_;}
 private:
  static constexpr uint32_t POWER_SAVE_CHECK_MS=15000;
  bool wasConnected_=false;uint32_t lastCheck_=0;
  int powerSaveDiagnostic_=-1,powerSaveSetResult_=-2;
  void checkPowerSave() {
    wifi_ps_type_t actual=WIFI_PS_MIN_MODEM;
    if(esp_wifi_get_ps(&actual)!=ESP_OK){powerSaveDiagnostic_=-1;return;}
    if(actual!=WIFI_PS_NONE) {
      powerSaveSetResult_=esp_wifi_set_ps(WIFI_PS_NONE)==ESP_OK?0:-1;
      if(esp_wifi_get_ps(&actual)!=ESP_OK){powerSaveDiagnostic_=-1;return;}
    }
    powerSaveDiagnostic_=actual==WIFI_PS_NONE?1:0;
  }
};
