#pragma once
#include "WifiSupervisor.h"
#include <WiFi.h>

// No Wi-Fi credentials persisted by this adapter. The enclosing firmware must
// supply the radio lifecycle lock shared with EspWifiEntropy, and must ensure
// no TLS request is in flight when the radio is reset or stopped.
class EspWifiStation final : public WifiStation {
 public:
  bool start(const char* ssid,const char* password) override {
    WiFi.persistent(false);
    if(!WiFi.mode(WIFI_STA))return false;
    if(!WiFi.setAutoReconnect(false))return false;
    // start is asynchronous; a later status check determines connection.
    return WiFi.begin(ssid,password)!=WL_CONNECT_FAILED;
  }
  bool connected() override {return WiFi.status()==WL_CONNECTED;}
  void disconnect() override {WiFi.disconnect(false,false);}
};
