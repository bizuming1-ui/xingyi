#pragma once
#include <atomic>
#include <stdint.h>

// Passive observations of already issued, decoded RPCs; single writer,
// lock-free snapshots for diagnostics only. Counts wrap modulo 2^32.
class NetworkObservations {
 public:
  struct Snapshot {uint32_t ok=0,fail=0,timeout=0,yield=0,lastOk=0;bool everOk=false;};
  void record(bool attempted,bool validRequest,bool ok,bool timeout,bool yielded,uint32_t now) {
    if(!attempted || !validRequest)return;
    if(ok) {lastOk_.store(now,std::memory_order_relaxed);everOk_.store(true,std::memory_order_release);ok_.fetch_add(1,std::memory_order_relaxed);}
    else if(yielded)yield_.fetch_add(1,std::memory_order_relaxed);
    else {fail_.fetch_add(1,std::memory_order_relaxed);if(timeout)timeout_.fetch_add(1,std::memory_order_relaxed);}
  }
  Snapshot snapshot() const {
    Snapshot s{};s.ok=ok_.load(std::memory_order_relaxed);s.fail=fail_.load(std::memory_order_relaxed);
    s.timeout=timeout_.load(std::memory_order_relaxed);s.yield=yield_.load(std::memory_order_relaxed);
    s.everOk=everOk_.load(std::memory_order_acquire);s.lastOk=lastOk_.load(std::memory_order_relaxed);return s;
  }
 private:
  std::atomic<uint32_t> ok_{0},fail_{0},timeout_{0},yield_{0},lastOk_{0};std::atomic<bool> everOk_{false};
};
