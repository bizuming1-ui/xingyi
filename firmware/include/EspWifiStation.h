#pragma once
#include "WifiSupervisor.h"
#include <WiFi.h>
#include <esp_wifi.h>

// No Wi-Fi credentials persisted by this adapter. The enclosing firmware must
// supply the radio lifecycle lock shared with EspWifiEntropy, and must ensure
// no TLS request is in flight when the radio is reset or stopped.
class EspWifiStation final : public WifiStation {
 public:
  bool start(const char* ssid,const char* password) override {
    WiFi.persistent(false);
    if(!WiFi.mode(WIFI_STA))return false;
    if(!WiFi.setAutoReconnect(false))return false;
    // Candidate for mains-powered iPhone-hotspot operation. Use the IDF API:
    // Arduino 2.0.17 setSleep(false) returns false even for a no-op, and its
    // cached value can diverge after an error. Never fail connection or change
    // count behavior on this optional optimization's failure.
    wifi_ps_type_t actual=WIFI_PS_MIN_MODEM;
    const auto applied=esp_wifi_set_ps(WIFI_PS_NONE);
    const auto readback=esp_wifi_get_ps(&actual);
    powerSaveDiagnostic_=(applied!=ESP_OK || readback!=ESP_OK)?-1:(actual==WIFI_PS_NONE?1:0);
    // start is asynchronous; a later status check determines connection.
    return WiFi.begin(ssid,password)!=WL_CONNECT_FAILED;
  }
  bool connected() override {return WiFi.status()==WL_CONNECTED;}
  void disconnect() override {WiFi.disconnect(false,false);}
  int powerSaveDiagnostic() const {return powerSaveDiagnostic_;}
 private:
  int powerSaveDiagnostic_=-1;
};
