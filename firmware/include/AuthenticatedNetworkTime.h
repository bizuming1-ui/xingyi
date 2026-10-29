#pragma once
#include "NetworkTaskCoordinator.h"
#include "TimeWindowSynchronizer.h"

// Network-task adapter. Credentials and CA remain owned by the caller, private,
// stable and alive. Do not log or expose the device secret. Refresh must only
// be called after Wi-Fi is connected and while radio lifecycle is serialized.
class AuthenticatedNetworkTime final : public NetworkTimeService {
 public:
  AuthenticatedNetworkTime(TrustedTimeWindow& window,TimeWindowSynchronizer& sync,const char* host,
                           const char* key,const char* ca,const char* deviceId,const char* secret,uint32_t timeoutMs)
    :window_(window),sync_(sync),host_(host),key_(key),ca_(ca),deviceId_(deviceId),secret_(secret),timeout_(timeoutMs){}
  bool trusted() override {TrustedTimeWindow::Bounds b{};return sync_.usesWindow(window_) && window_.bounds(b);}
  bool refresh() override {if(!sync_.usesWindow(window_))return false;return sync_.refresh(host_,key_,ca_,deviceId_,secret_,timeout_)==TimeBootstrapExchange::Result::Ok && trusted();}
  void invalidate() override {window_.invalidate();}
 private:
  TrustedTimeWindow& window_;TimeWindowSynchronizer& sync_;const char *host_,*key_,*ca_,*deviceId_,*secret_;uint32_t timeout_;
};
