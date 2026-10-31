#pragma once
#include <stdint.h>
#include "TimeProofLimits.h"

// A boot-local monotonic counter that cannot wrap during the configured trust
// lifetime. Implementations must return false on any detected clock fault; a
// reset must reset this object and invalidate all prior signed time samples.
class MonotonicMillis64 {
 public:
  virtual ~MonotonicMillis64()=default;
  virtual bool read(uint64_t& millis)=0;
};

// Conservatively bounds actual UTC after a verified time proof. This class does
// NOT authenticate proofs or set the system clock.
// Only feed it an Ok result of TimeBootstrapExchange, measured from immediately
// before fetch to immediately after it on this SAME boot-local clock. The
// server signs floor(UTC seconds) during that interval. Invalid samples discard
// any old trust rather than retaining it. Network-task ownership is exclusive.
class TrustedTimeWindow {
 public:
  struct Bounds { uint64_t earliestMs=0,latestMs=0; };
  // Policy values must be based on real platform oscillator guarantees, not
  // optimistic measurement. At most 24 hours; drift is strictly below 1%.
  static constexpr uint32_t MAX_LIFETIME_MS=86400000;
  static constexpr uint32_t MAX_DRIFT_PPM=9999;
  static constexpr uint64_t MIN_EPOCH=1704067200ULL;
  static constexpr uint64_t MAX_EPOCH=4102444800ULL;
  TrustedTimeWindow(MonotonicMillis64& clock,uint32_t lifetimeMs,uint32_t driftPpm)
    :clock_(clock),lifetime_(lifetimeMs),driftPpm_(driftPpm){}
  TrustedTimeWindow(const TrustedTimeWindow&)=delete;
  TrustedTimeWindow& operator=(const TrustedTimeWindow&)=delete;
  bool usesClock(const MonotonicMillis64& clock) const {return &clock_==&clock;}
  void invalidate(){valid_=false;lastRead_=0;}
  bool acceptVerified(uint64_t serverSeconds,uint64_t requestStartedMs,uint64_t responseReceivedMs) {
    invalidate();
    if(!lifetime_ || lifetime_>MAX_LIFETIME_MS || driftPpm_>MAX_DRIFT_PPM ||
       serverSeconds<MIN_EPOCH || serverSeconds>MAX_EPOCH ||
       responseReceivedMs<requestStartedMs || responseReceivedMs-requestStartedMs>=TimeProofLimits::MAX_ROUND_TRIP_MS)return false;
    uint64_t now=0;
    if(!clock_.read(now) || now<responseReceivedMs || now-responseReceivedMs>=lifetime_)return false;
    const uint64_t trip=responseReceivedMs-requestStartedMs+1; // ms quantization can hide <1ms
    baseLo_=serverSeconds*1000;
    // floor(server_seconds) loses <1s; response time may be anywhere in the
    // measured round trip, including error in the local monotonic oscillator.
    baseHi_=baseLo_+1000+trip+driftAllowance(trip);
    received_=responseReceivedMs;lastRead_=now;valid_=true;
    return true;
  }
  // Refresh around the midpoint, without extending the original hard expiry.
  bool refreshDue() {
    Bounds ignored{};if(!bounds(ignored))return false;
    return lastRead_-received_ >= lifetime_/2;
  }
  // Replace atomically only with a fresh verified proof. Invalid timing or
  // clock faults fail closed. The caller owns proof authentication.
  bool replaceVerified(uint64_t serverSeconds,uint64_t requestStartedMs,uint64_t responseReceivedMs) {
    Bounds existing{};
    if(!bounds(existing))return false; // preserve mode requires an existing valid window
    TrustedTimeWindow candidate(clock_,lifetime_,driftPpm_);
    if(!candidate.acceptVerified(serverSeconds,requestStartedMs,responseReceivedMs)) {
      invalidate();return false;
    }
    if(candidate.lastRead_<lastRead_) {invalidate();return false;}
    baseLo_=candidate.baseLo_;baseHi_=candidate.baseHi_;received_=candidate.received_;
    lastRead_=candidate.lastRead_;valid_=true;return true;
  }
  bool bounds(Bounds& out) {
    if(!valid_)return false;
    uint64_t now=0;
    if(!clock_.read(now) || now<lastRead_ || now<received_ || now-received_>=lifetime_){invalidate();return false;}
    const uint64_t elapsed=now-received_;
    const uint64_t error=1+driftAllowance(elapsed+1); // bound millisecond quantization
    const uint64_t minimum=elapsed>error?elapsed-error:0;
    Bounds b;b.earliestMs=baseLo_+minimum;b.latestMs=baseHi_+elapsed+error;
    lastRead_=now;out=b;return true;
  }
 private:
  MonotonicMillis64& clock_;uint32_t lifetime_,driftPpm_;bool valid_=false;
  uint64_t baseLo_=0,baseHi_=0,received_=0,lastRead_=0;
  uint64_t driftAllowance(uint64_t measured) const {
    // ceil(measured * ppm / (1e6 - ppm)), conservative for a slow clock.
    const uint64_t denominator=1000000ULL-driftPpm_;
    return (measured*driftPpm_+denominator-1)/denominator;
  }
};
