#pragma once
#include "TimeBootstrapExchange.h"
#include "TrustedTimeWindow.h"

// Connects authenticated time bootstrap to a boot-local uncertainty window.
// The caller must supply the same monotonic clock used by the window and a
// lifetime/drift policy backed by hardware guarantees. All operations run on
// the network task; this does not set the system wall clock or make TLS use
// the resulting bounds. A failed refresh explicitly invalidates prior trust.
class TimeWindowSynchronizer {
 public:
  TimeWindowSynchronizer(MonotonicMillis64& clock,TrustedTimeWindow& window,TimeBootstrapExchange& bootstrap)
    :clock_(clock),window_(window),bootstrap_(bootstrap) {}
  TimeWindowSynchronizer(const TimeWindowSynchronizer&)=delete;
  TimeWindowSynchronizer& operator=(const TimeWindowSynchronizer&)=delete;
  bool usesWindow(const TrustedTimeWindow& window) const {return &window_==&window;}
  TimeBootstrapExchange::Result refresh(const char* host,const char* key,const char* ca,
                                       const char* deviceId,const char* secret,uint32_t timeoutMs) {
    window_.invalidate();uint64_t before=0,after=0,seconds=0;
    if(!window_.usesClock(clock_) || !clock_.read(before))return TimeBootstrapExchange::Result::Unavailable;
    const auto result=bootstrap_.fetch(host,key,ca,deviceId,secret,timeoutMs,seconds);
    if(result!=TimeBootstrapExchange::Result::Ok)return result;
    if(!clock_.read(after) || !window_.acceptVerified(seconds,before,after))return TimeBootstrapExchange::Result::Unavailable;
    return TimeBootstrapExchange::Result::Ok;
  }
 private:
  MonotonicMillis64& clock_;TrustedTimeWindow& window_;TimeBootstrapExchange& bootstrap_;
};
