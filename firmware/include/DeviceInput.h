#pragma once
#include "ButtonCounter.h"
#include "SyncPlanner.h"

// Counting-thread-only component. Poll frequently; never call from the network
// task. DurableCounter's NVS writes may still take time and need real testing.
// Rejected presses are explicitly reported, never claimed as accepted.
class DeviceInput {
 public:
  enum class Status { Ready, Unbound, ReleaseRequired, StorageError, CapacityFull, Fault };
  struct Sample { Status status=Status::Ready; uint32_t accepted=0; uint32_t rejected=0; };
  DeviceInput(DurableCounter& counter,SyncPlanner& planner,bool initialPressed=false,uint32_t now=0)
    : counter_(counter),planner_(planner),button_(initialPressed,now),releaseRequired_(initialPressed) {}

  Sample tick(bool pressed,uint32_t now) {
    const uint32_t events=button_.tick(pressed,now);
    Sample result{};
    if (stopped_!=Status::Ready) {result.status=stopped_;result.rejected=events;return result;}
    if (planner_.faulted()) {stopped_=Status::Fault;result.status=stopped_;result.rejected=events;return result;}
    // Also gate a raw press begun while unbound, even if binding appears
    // during the debounce interval.
    if (!counter_.isReady() && pressed) releaseRequired_=true;
    if (releaseRequired_) {
      // An unbound hold must not begin counting halfway through when an order
      // becomes available. Require a continuously released/debounced button.
      if (pressed) releaseTiming_=false;
      else if (!releaseTiming_) {releaseTiming_=true;releasedAt_=now;}
      else if (static_cast<uint32_t>(now-releasedAt_)>=ButtonCounter::DEBOUNCE_MS) {releaseRequired_=false;releaseTiming_=false;}
      result.status=!counter_.isReady()?Status::Unbound:(releaseRequired_?Status::ReleaseRequired:Status::Ready);
      result.rejected=events;return result;
    }
    if (!counter_.isReady()) {
      result.status=Status::Unbound;result.rejected=events;
      return result;
    }
    if (!events) return result;
    const auto outcome=planner_.count(events);
    if(outcome==DurableCounter::Result::Ok) {result.accepted=events;return result;}
    stopped_=outcome==DurableCounter::Result::StorageError?Status::StorageError:
      outcome==DurableCounter::Result::Full?Status::CapacityFull:Status::Fault;
    result.status=stopped_;result.rejected=events;return result;
  }
 private:
  DurableCounter& counter_;SyncPlanner& planner_;ButtonCounter button_;
  Status stopped_=Status::Ready;
  bool releaseRequired_=false,releaseTiming_=false;
  uint32_t releasedAt_=0;
};
