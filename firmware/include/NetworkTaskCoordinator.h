#pragma once
#include "SyncBridge.h"
#include "RadioLifecycleGuard.h"

// Single network-task ownership; the counting task must not call these methods.
class NetworkTimeService {
 public:
  virtual ~NetworkTimeService()=default;
  virtual bool trusted()=0;
  virtual bool refresh()=0; // may block within its bounded network deadline
  virtual void invalidate()=0;
};
class NetworkTaskClock {
 public:
  virtual ~NetworkTaskClock()=default;
  virtual uint32_t nowMillis()=0;
};

// Coordinates Wi-Fi lifecycle, authenticated time and mailbox RPC work.
// No count/storage/GPIO operations occur here. A pending result is never
// discarded or repeated, even when Wi-Fi disconnects. Offline or untrusted
// commands are returned as failures, never as acknowledgements. The clock must
// be monotonic modulo 2^32; refresh and RPC must each enforce their own limits.
// Runtime must call this on one dedicated network task and keep sampling the
// button on a separate task. No task creation or hardware configuration here.
class NetworkTaskCoordinator {
 public:
  static constexpr uint32_t TIME_RETRY_MS=5000;
  NetworkTaskCoordinator(NetworkTaskClock& clock,GuardedWifiSupervisor& wifi,NetworkTimeService& time,SyncNetworkWorker& worker)
   :clock_(clock),wifi_(wifi),time_(time),worker_(worker){}
  NetworkTaskCoordinator(const NetworkTaskCoordinator&)=delete;
  NetworkTaskCoordinator& operator=(const NetworkTaskCoordinator&)=delete;
  void step() {
    const uint32_t now=clock_.nowMillis();
    if(!wifi_.step(now)){worker_.flushPending();return;}
    if(wifi_.state()!=WifiSupervisor::State::Online) {
      worker_.closeConnection();time_.invalidate();attempted_=false;worker_.failUnavailable();return;
    }
    if(!time_.trusted()) {
      worker_.closeConnection(); // bootstrap shares the same TCP/TLS resources
      if(!attempted_ || static_cast<uint32_t>(now-lastAttempt_)>=TIME_RETRY_MS) {
        (void)time_.refresh();
        // Backoff is measured from completion, including timeout duration.
        attempted_=true;lastAttempt_=clock_.nowMillis();
      }
      if(!time_.trusted()){worker_.failUnavailable();return;}
    }
    worker_.step();
  }
 private:
  NetworkTaskClock& clock_;GuardedWifiSupervisor& wifi_;NetworkTimeService& time_;SyncNetworkWorker& worker_;
  bool attempted_=false;uint32_t lastAttempt_=0;
};
