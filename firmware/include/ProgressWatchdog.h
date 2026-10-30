#pragma once
#include <stdint.h>

// Policy only; caller supplies same boot-local modulo-2^32 monotonic clock.
// A progressing retry loop is healthy even when the network is unavailable.
// Inactive/fault states are never reset. The caller must run this separately
// from the watched tasks, perform resets, and enforce the restart budget.
class ProgressWatchdog {
 public:
  static constexpr uint32_t STALL_MS=90000;
  static constexpr uint32_t RELEASE_MS=1000;
  enum class Action { None, Restart };
  Action step(uint32_t now,uint32_t countProgress,uint32_t networkProgress,
              bool active,bool buttonPressed,bool budgetAvailable) {
    if(!active || buttonPressed || !budgetAvailable) {released_=false;return Action::None;}
    if(!released_) {released_=true;releasedAt_=now;}
    const bool staleCount=static_cast<uint32_t>(now-countProgress)>=STALL_MS;
    const bool staleNetwork=static_cast<uint32_t>(now-networkProgress)>=STALL_MS;
    if((staleCount || staleNetwork) && static_cast<uint32_t>(now-releasedAt_)>=RELEASE_MS)return Action::Restart;
    return Action::None;
  }
 private:
  bool released_=false;uint32_t releasedAt_=0;
};

// Bounded consecutive software-watchdog resets. Storage is supplied by caller;
// persistence over power failure/brownout is not guaranteed. Invalid or
// non-watchdog reset history is discarded and never mutates counter storage.
class WatchdogRestartBudget {
 public:
  static constexpr uint32_t MAX_RESTARTS=3;
  static constexpr uint32_t HEALTHY_MS=600000;
  struct Record {uint32_t magic;uint32_t attempts;uint32_t check;};
  explicit WatchdogRestartBudget(Record& record):record_(record){}
  void begin(bool softwareReset) {
    if(!softwareReset || record_.magic!=MAGIC || record_.attempts>MAX_RESTARTS ||
       record_.check!=(MAGIC^record_.attempts))write(0);
    healthy_=false;
  }
  bool available() const {return record_.attempts<MAX_RESTARTS;}
  uint32_t attempts() const {return record_.attempts;}
  bool reserve() {if(!available())return false;write(record_.attempts+1);return true;}
  void observe(uint32_t now,bool healthy) {
    if(!healthy){healthy_=false;return;}
    if(!healthy_){healthy_=true;healthySince_=now;return;}
    if(static_cast<uint32_t>(now-healthySince_)>=HEALTHY_MS){write(0);healthySince_=now;}
  }
 private:
  static constexpr uint32_t MAGIC=0x58495744;
  Record& record_;bool healthy_=false;uint32_t healthySince_=0;
  void write(uint32_t attempts){record_.magic=MAGIC;record_.attempts=attempts;record_.check=MAGIC^attempts;}
};
