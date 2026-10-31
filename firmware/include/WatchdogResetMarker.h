#pragma once
#include <stdint.h>

// Best-effort RTC diagnostic only. The marker records an *attempt* immediately
// before esp_restart(), not proof that the restart completed or why. This is
// never used to make a counter, network, or restart-budget decision.
class WatchdogResetMarker {
 public:
  struct Record { uint32_t magic; uint32_t inverse; };
  explicit WatchdogResetMarker(Record& record):record_(record) {}
  void markAttempt() { record_.inverse=~MAGIC; record_.magic=MAGIC; }
  bool consume(bool softwareReset) {
    const bool attempted=softwareReset && record_.magic==MAGIC && record_.inverse==~MAGIC;
    record_.magic=0; record_.inverse=0;
    return attempted;
  }
 private:
  static constexpr uint32_t MAGIC=0x58575253;
  Record& record_;
};
