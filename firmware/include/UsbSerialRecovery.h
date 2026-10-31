#pragma once
#include <stdint.h>

// A diagnostic-only kick request, not an assertion of USB host connectivity.
// The caller owns the port and performs any platform-specific register access.
class UsbSerialRecovery {
 public:
  bool due(uint32_t now) {
    if(static_cast<uint32_t>(now-lastCheck_)<250)return false;
    lastCheck_=now;return true;
  }
  static bool shouldKick(bool plugged,bool ready,int freeBytes) {
    return plugged && (!ready || freeBytes<128);
  }
 private:
  uint32_t lastCheck_=0;
};
