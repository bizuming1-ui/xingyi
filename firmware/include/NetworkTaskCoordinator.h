#pragma once
#include "SyncBridge.h"
#include "RadioLifecycleGuard.h"

// Single network-task ownership; the counting task must not call these methods.
class NetworkTimeService {
 public:
  virtual ~NetworkTimeService()=default;
  virtual bool trusted()=0;
  virtual bool refresh()=0; // may block within its bounded network deadline
  virtual bool refreshDue() {return false;}
  virtual bool refreshPreserving() {return false;}
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
  uint32_t timeAttemptSequence() const {return timeSeq_;}
  uint32_t timeAttemptStart() const {return timeStart_;}
  uint32_t timeAttemptEnd() const {return timeEnd_;}
  bool timeAttemptPreserving() const {return timePreserving_;}
  void step() {
    const uint32_t now=clock_.nowMillis();
    if(!wifi_.step(now)){worker_.flushPending();return;}
    if(wifi_.state()!=WifiSupervisor::State::Online) {
      worker_.closeConnection();time_.invalidate();attempted_=false;deferred_=false;worker_.failUnavailable();return;
    }
    if(!time_.trusted()) {
      deferred_=false;worker_.closeConnection(); // bootstrap shares the same TCP/TLS resources
      if(!attempted_ || static_cast<uint32_t>(now-lastAttempt_)>=TIME_RETRY_MS) {
        timeStart_=clock_.nowMillis();timePreserving_=false;
        (void)time_.refresh();timeEnd_=clock_.nowMillis();++timeSeq_;
        // Backoff is measured from completion, including timeout duration.
        attempted_=true;lastAttempt_=clock_.nowMillis();
      }
      if(!time_.trusted()){worker_.failUnavailable();return;}
    }
    // A proactive attempt never lengthens an old proof's expiry. Skip it
    // while a barrier is being resolved so a heartbeat is not preempted by a
    // bootstrap on the shared TCP transport. Hard expiry still fails closed.
    if(time_.trusted() && time_.refreshDue() && !worker_.barrierActive() &&
       (!attempted_ || static_cast<uint32_t>(now-lastAttempt_)>=TIME_RETRY_MS)) {
      // Serve at most one already-queued non-poll command before this refresh.
      // This cannot extend trust, and does not defer hard-expiry recovery.
      if(!deferred_ && worker_.hasPriorityCommand()) {
        deferred_=true;worker_.step();return;
      }
      deferred_=false;worker_.closeConnection();timeStart_=clock_.nowMillis();timePreserving_=true;
      (void)time_.refreshPreserving();timeEnd_=clock_.nowMillis();++timeSeq_;
      attempted_=true;lastAttempt_=clock_.nowMillis();
      // Proactive backoff must not add another five seconds once the old
      // proof has expired. Give hard recovery one immediate opportunity;
      // subsequent failed hard attempts still use their normal backoff.
      if(!time_.trusted()){attempted_=false;worker_.failUnavailable();return;}
    }
    worker_.step();
  }
 private:
  NetworkTaskClock& clock_;GuardedWifiSupervisor& wifi_;NetworkTimeService& time_;SyncNetworkWorker& worker_;
  bool attempted_=false,deferred_=false;uint32_t lastAttempt_=0;
  uint32_t timeSeq_=0,timeStart_=0,timeEnd_=0;bool timePreserving_=false;
};
