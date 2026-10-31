#pragma once
#include "RandomEpochSource.h"
#include <WiFi.h>
#include <esp_system.h>

// esp_fill_random has a hardware entropy source while Wi-Fi is enabled. This
// adapter fails closed unless connected, and checks again after obtaining bytes.
// Runtime MUST serialize Wi-Fi enable/disable operations with fill(), so the
// radio cannot be stopped or restarted while the hardware RNG is being read.
// A connected-status check is not a substitute for that lifecycle lock.
class EspWifiEntropy final : public SecureEntropy {
 public:
  // Disconnection before/after the entropy read is temporary, not corruption.
  bool failureMayRetry() const override {return true;}
  bool fill(uint8_t* bytes,size_t length) override {
    if(!bytes || !length || WiFi.status()!=WL_CONNECTED)return false;
    esp_fill_random(bytes,length);
    if(WiFi.status()==WL_CONNECTED)return true;
    volatile uint8_t* p=bytes;for(size_t i=0;i<length;++i)p[i]=0;
    return false;
  }
};
