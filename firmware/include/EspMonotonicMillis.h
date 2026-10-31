#pragma once
#include "TrustedTimeWindow.h"
#include <esp_timer.h>

// esp_timer_get_time is microseconds since startup. This does not survive a
// reboot; the time window must always be reconstructed from a fresh proof.
class EspMonotonicMillis final : public MonotonicMillis64 {
 public:
  bool read(uint64_t& millis) override {
    const int64_t us=esp_timer_get_time();if(us<0)return false;
    millis=static_cast<uint64_t>(us)/1000;return true;
  }
};
