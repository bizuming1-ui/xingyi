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
  TimeBootstrapExchange::Stage lastStage() const {return stage_;}
  int lastDetail() const {return detail_;}
  uint32_t lastHandshakeStartMs() const {return bootstrap_.lastHandshakeStartMs();}
  uint32_t lastTxBytes() const {return bootstrap_.lastTxBytes();}
  uint32_t lastRxBytes() const {return bootstrap_.lastRxBytes();}
  TimeBootstrapExchange::Result refresh(const char* host,const char* key,const char* ca,
                                       const char* deviceId,const char* secret,uint32_t timeoutMs) {
    return refreshImpl(host,key,ca,deviceId,secret,timeoutMs,false);
  }
  TimeBootstrapExchange::Result refreshPreserving(const char* host,const char* key,const char* ca,
                                       const char* deviceId,const char* secret,uint32_t timeoutMs) {
    return refreshImpl(host,key,ca,deviceId,secret,timeoutMs,true);
  }
 private:
  TimeBootstrapExchange::Result refreshImpl(const char* host,const char* key,const char* ca,
                                       const char* deviceId,const char* secret,uint32_t timeoutMs,bool preserve) {
    stage_=TimeBootstrapExchange::Stage::Clock;detail_=0;
    if(!preserve)window_.invalidate();
    else {TrustedTimeWindow::Bounds b{};if(!window_.bounds(b))return TimeBootstrapExchange::Result::Unavailable;}
    uint64_t before=0,after=0,seconds=0;
    if(!window_.usesClock(clock_) || !clock_.read(before)) {window_.invalidate();return TimeBootstrapExchange::Result::Unavailable;}
    const auto result=bootstrap_.fetch(host,key,ca,deviceId,secret,timeoutMs,seconds);
    stage_=bootstrap_.lastStage();detail_=bootstrap_.lastDetail();
    if(result!=TimeBootstrapExchange::Result::Ok)return result;
    stage_=TimeBootstrapExchange::Stage::Clock;detail_=0;
    if(!clock_.read(after)) {window_.invalidate();return TimeBootstrapExchange::Result::Unavailable;}
    if(!(preserve?window_.replaceVerified(seconds,before,after):window_.acceptVerified(seconds,before,after)))return TimeBootstrapExchange::Result::Unavailable;
    stage_=TimeBootstrapExchange::Stage::Complete;return TimeBootstrapExchange::Result::Ok;
  }
  MonotonicMillis64& clock_;TrustedTimeWindow& window_;TimeBootstrapExchange& bootstrap_;
  TimeBootstrapExchange::Stage stage_=TimeBootstrapExchange::Stage::Idle;int detail_=0;
};
