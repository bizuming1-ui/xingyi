#pragma once
#include <stdint.h>

// Diagnostics only. Repeated poll-not-sent events can fill the serial queue
// when Wi-Fi/time is unavailable; retain one per second and never suppress
// attempted requests or uploads/barrier operations. Network-task ownership.
class MonitorThrottle {
 public:
  bool emit(bool statusPoll,bool attempted,uint32_t now) {
    if(!statusPoll || attempted) {if(statusPoll)seen_=false;return true;}
    if(!seen_ || static_cast<uint32_t>(now-last_)>=1000) {seen_=true;last_=now;return true;}
    return false;
  }
 private:
  uint32_t last_=0;bool seen_=false;
};

// Counting-task-only; coalesces successive totals, never blocks. A failed
// serial write is retried at the next interval instead of losing final state.
class CountLineThrottle {
 public:
  void begin(uint64_t total) {lastTotal_=total;lastAttempt_=0;}
  bool due(uint64_t total,uint32_t now) const {return total!=lastTotal_ && static_cast<uint32_t>(now-lastAttempt_)>=100;}
  void attempted(uint64_t total,uint32_t now,bool written) {lastAttempt_=now;if(written)lastTotal_=total;}
 private:
  uint64_t lastTotal_=0;uint32_t lastAttempt_=0;
};
