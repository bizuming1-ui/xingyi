#pragma once
#include "NetworkTaskCoordinator.h"
#include "TimeWindowSynchronizer.h"

// Network-task adapter. Credentials and CA remain owned by the caller, private,
// stable and alive. Do not log or expose the device secret. Refresh must only
// be called after Wi-Fi is connected and while radio lifecycle is serialized.
class AuthenticatedNetworkTime final : public NetworkTimeService {
 public:
  AuthenticatedNetworkTime(TrustedTimeWindow& window,TimeWindowSynchronizer& sync,const char* host,
                           const char* key,const char* ca,const char* deviceId,const char* secret,uint32_t timeoutMs,uint32_t recoveryTimeoutMs=0)
    :window_(window),sync_(sync),host_(host),key_(key),ca_(ca),deviceId_(deviceId),secret_(secret),timeout_(timeoutMs),recoveryTimeout_(recoveryTimeoutMs?recoveryTimeoutMs:timeoutMs){}
  bool trusted() override {TrustedTimeWindow::Bounds b{};return sync_.usesWindow(window_) && window_.bounds(b);}
  bool refresh() override {
    if(!sync_.usesWindow(window_))return false;
    // Escalate only after a timed-out attempt and only on the hard recovery
    // path. Proactive refresh stays short while normal traffic may be active.
    const uint32_t budget=lastResult_==TimeBootstrapExchange::Result::Timeout?recoveryTimeout_:timeout_;
    lastBudget_=budget;
    lastResult_=sync_.refresh(host_,key_,ca_,deviceId_,secret_,budget);
    return lastResult_==TimeBootstrapExchange::Result::Ok && trusted();
  }
  bool refreshDue() override {return sync_.usesWindow(window_) && window_.refreshDue();}
  bool refreshPreserving() override {
    if(!sync_.usesWindow(window_))return false;
    lastBudget_=timeout_;
    lastResult_=sync_.refreshPreserving(host_,key_,ca_,deviceId_,secret_,timeout_);
    return lastResult_==TimeBootstrapExchange::Result::Ok && trusted();
  }
  uint32_t lastBudget() const {return lastBudget_;}
  int lastStage() const {return static_cast<int>(sync_.lastStage());}
  int lastDetail() const {return sync_.lastDetail();}
  int lastResult() const {return static_cast<int>(lastResult_);}
  uint32_t lastHandshakeStartMs() const {return sync_.lastHandshakeStartMs();}
  uint32_t lastTxBytes() const {return sync_.lastTxBytes();}
  uint32_t lastRxBytes() const {return sync_.lastRxBytes();}
  void invalidate() override {window_.invalidate();}
 private:
  TrustedTimeWindow& window_;TimeWindowSynchronizer& sync_;const char *host_,*key_,*ca_,*deviceId_,*secret_;uint32_t timeout_,recoveryTimeout_;
  uint32_t lastBudget_=0;
  TimeBootstrapExchange::Result lastResult_=TimeBootstrapExchange::Result::Unavailable;
};
