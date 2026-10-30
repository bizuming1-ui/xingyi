#pragma once
#include "DurableCounter.h"
#include <stdint.h>
#include <string.h>

// Pure, single-threaded scheduling core. It does NOT implement Wi-Fi, TLS,
// JSON parsing, timers on another thread, or the device secret. A transport
// must validate TLS and run asynchronously, returning results by ticket.
class EpochSource {
 public:
  virtual ~EpochSource() = default;
  virtual bool generate(char out[37]) = 0; // cryptographically random UUID v4
  // Only meaningful immediately after failure, on the same owning thread.
  virtual bool failureMayRetry() const {return false;}
};

class SyncPlanner {
 public:
  enum class Kind { None, Poll, Upload, Freeze, BarrierStatus, Renew, Abort };
  enum class BarrierState { None, Requested, Ready, Completed, Cancelled, Expired };
  struct Command {
    Kind kind=Kind::None;
    uint32_t ticket=0;
    char binding[37]{};
    char epoch[37]{};
    char barrier[37]{};
    char nonce[37]{};
    uint64_t total=0;
  };
  struct Reply {
    bool success=false;
    // Poll response: an absent desired binding uses empty strings and revision 0.
    char desiredOrder[37]{};
    char desiredBinding[37]{};
    uint64_t desiredRevision=0;
    char barrier[37]{};
    BarrierState barrierState=BarrierState::None;
    // Upload response, or freeze/renew/status lease nonce.
    uint64_t acceptedTotal=0;
    char nonce[37]{};
  };

  SyncPlanner(DurableCounter& counter, EpochSource& epochs)
      : counter_(counter), epochs_(epochs), streamRegistered_(!counter.isReady()) {
    if (!counter.isLoaded()) fault_=true;
    // On reboot we do not know whether the server has seen this epoch, even
    // when its local total is zero. Frozen state must be resolved first.
    if (counter.isReady()) streamRegistered_=false;
    if (counter.isFrozen()) { copy(barrier_,counter.state().barrierId); needBarrierStatus_=true; }
  }

  DurableCounter::Result count(uint32_t amount) {
    if (fault_) return DurableCounter::Result::NotReady;
    const auto r=counter_.add(amount);
    if (r==DurableCounter::Result::StorageError || r==DurableCounter::Result::Full || r==DurableCounter::Result::NotReady) fault_=true;
    return r;
  }
  bool faulted() const { return fault_; }
  bool hasInFlight() const { return inFlight_.kind!=Kind::None; }
  Kind diagnosticInFlight() const {return inFlight_.kind;}
  BarrierState diagnosticBarrier() const {return remoteBarrier_;}

  // Commands execute outside this class. The caller must keep sampling GPIO
  // during network delays, serialize access to the counter, and enforce a
  // bounded request timeout. Commands do not contain the device credential.
  Command next(uint32_t now) {
    if (fault_ || hasInFlight()) return {};
    // A single global minimum interval also bounds retry rate after failures.
    if (sentAny_ && elapsed(now,lastSent_)<200) return {};
    if (counter_.isFrozen()) {
      if (!counter_.canReportFrozen()) return issue(Kind::Abort,now);
      if (needBarrierStatus_) return issue(Kind::BarrierStatus,now);
      if (remoteBarrier_==BarrierState::Requested) return issue(Kind::Freeze,now);
      if (remoteBarrier_==BarrierState::Ready && uuid(nonce_) && (!renewed_ || elapsed(now,lastRenew_)>=750)) return issue(Kind::Renew,now);
      return {};
    }
    // A slow poll can take longer than the polling interval. Give its result
    // one scheduling opportunity before polling again, or binding/uploads may
    // be permanently starved on a real network.
    const bool justPolled=pollJustCompleted_;pollJustCompleted_=false;
    if (!polled_ || (!justPolled && elapsed(now,lastPoll_)>=500)) return issue(Kind::Poll,now);
    if (counter_.isReady() && (!streamRegistered_ || counter_.pending()>0)) return issue(Kind::Upload,now);
    if (uuid(desiredBinding_) && uuid(desiredOrder_) && desiredRevision_>0) {
      const auto& s=counter_.state();
      const bool same=counter_.isReady() && s.bindingRevision==desiredRevision_ && strcmp(s.bindingId,desiredBinding_)==0 && strcmp(s.orderId,desiredOrder_)==0;
      if (!same) {
        // A stale server response is never allowed to move the device back.
        if (counter_.isReady() && desiredRevision_<=s.bindingRevision) { fault_=true; return {}; }
        if (epochAttempted_ && elapsed(now,lastEpochAttempt_)<200) return {};
        epochAttempted_=true;lastEpochAttempt_=now;
        char epoch[37]{};
        if (!epochs_.generate(epoch)) { if(!epochs_.failureMayRetry())fault_=true; return {}; }
        const auto r=counter_.bind(desiredOrder_,desiredRevision_,desiredBinding_,epoch);
        if (r==DurableCounter::Result::Pending || r==DurableCounter::Result::Frozen) return {};
        if (r!=DurableCounter::Result::Ok) { fault_=true; return {}; }
        streamRegistered_=false;
        return issue(Kind::Upload,now); // register even a zero-count epoch
      }
    }
    if (remoteBarrier_==BarrierState::Requested && uuid(barrier_) && counter_.isReady() && streamRegistered_ && counter_.pending()==0) {
      const auto r=counter_.beginFreeze(barrier_);
      if (r!=DurableCounter::Result::Ok) { fault_=true; return {}; }
      return issue(Kind::Freeze,now);
    }
    return {};
  }

  // A failed/timed-out response is never treated as an acknowledgement. For
  // frozen operations, query the server's authoritative barrier state before
  // deciding whether to resume or retry. Unknown/stale tickets are ignored.
  bool complete(uint32_t ticket, const Reply& reply, uint32_t now) {
    if (!hasInFlight() || ticket!=inFlight_.ticket) return false;
    const Command sent=inFlight_; inFlight_={};
    if (!reply.success) {
      if (counter_.isFrozen()) needBarrierStatus_=true;
      return true;
    }
    switch(sent.kind) {
      case Kind::Poll: {
        if (reply.desiredRevision>DurableCounter::MAX_COUNT ||
            (reply.desiredRevision==0 && (reply.desiredOrder[0] || reply.desiredBinding[0] || counter_.isReady())) ||
            (reply.desiredRevision>0 && (!uuid(reply.desiredOrder) || !uuid(reply.desiredBinding)))) { fault_=true; break; }
        desiredRevision_=reply.desiredRevision; copy(desiredOrder_,reply.desiredOrder); copy(desiredBinding_,reply.desiredBinding);
        if (reply.barrierState==BarrierState::None) { barrier_[0]='\0'; remoteBarrier_=BarrierState::None; }
        else if (!uuid(reply.barrier) || (reply.barrierState!=BarrierState::Requested && reply.barrierState!=BarrierState::Ready && reply.barrierState!=BarrierState::Expired)) { fault_=true; }
        else if (reply.barrierState==BarrierState::Ready || reply.barrierState==BarrierState::Expired) {
          // Server says it is frozen, but local journal does not: never guess.
          fault_=true;
        } else { copy(barrier_,reply.barrier); remoteBarrier_=reply.barrierState; }
        polled_=true;pollJustCompleted_=true; break;
      }
      case Kind::Upload: {
        if (reply.acceptedTotal!=sent.total || strcmp(sent.epoch,counter_.state().epochId)!=0 || strcmp(sent.binding,counter_.state().bindingId)!=0) { fault_=true; break; }
        if (counter_.acknowledge(sent.epoch,reply.acceptedTotal)!=DurableCounter::Result::Ok) { fault_=true; break; }
        streamRegistered_=true; break;
      }
      case Kind::Freeze: case Kind::Renew: case Kind::BarrierStatus: case Kind::Abort: {
        if (!counter_.isFrozen() || strcmp(sent.barrier,counter_.state().barrierId)!=0) { fault_=true; break; }
        if (reply.barrierState==BarrierState::Completed || reply.barrierState==BarrierState::Cancelled || reply.barrierState==BarrierState::Expired) {
          const auto outcome=reply.barrierState==BarrierState::Completed?DurableCounter::TerminalOutcome::Completed:
            reply.barrierState==BarrierState::Cancelled?DurableCounter::TerminalOutcome::Cancelled:DurableCounter::TerminalOutcome::Expired;
          if(counter_.finishFreeze(sent.barrier,outcome)!=DurableCounter::Result::Ok) { fault_=true; break; }
          barrier_[0]='\0'; nonce_[0]='\0'; remoteBarrier_=BarrierState::None; needBarrierStatus_=false; renewed_=false; polled_=false;
        } else if (reply.barrierState==BarrierState::Ready) {
          if (!uuid(reply.nonce)) { fault_=true; break; }
          copy(nonce_,reply.nonce); remoteBarrier_=BarrierState::Ready; needBarrierStatus_=false;
          renewed_=true; lastRenew_=now;
        } else if (reply.barrierState==BarrierState::Requested && sent.kind==Kind::BarrierStatus) {
          remoteBarrier_=BarrierState::Requested; needBarrierStatus_=false;
        } else { fault_=true; }
        break;
      }
      default: fault_=true; break;
    }
    return true;
  }

 private:
  DurableCounter& counter_; EpochSource& epochs_;
  bool fault_=false, sentAny_=false, polled_=false, pollJustCompleted_=false, streamRegistered_=false, needBarrierStatus_=false, renewed_=false, epochAttempted_=false;
  uint32_t lastSent_=0,lastPoll_=0,lastRenew_=0,lastEpochAttempt_=0,nextTicket_=0;
  uint64_t desiredRevision_=0;
  char desiredOrder_[37]{},desiredBinding_[37]{},barrier_[37]{},nonce_[37]{};
  BarrierState remoteBarrier_=BarrierState::None;
  Command inFlight_{};
  static uint32_t elapsed(uint32_t now,uint32_t then) {return now-then;}
  static void copy(char* dst,const char* src) {memcpy(dst,src,37);}
  static bool uuid(const char* s) {
    if(!s)return false;
    for(unsigned i=0;i<36;++i){const char c=s[i];if(!c)return false;
      if(i==8||i==13||i==18||i==23){if(c!='-')return false;}
      else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;}
    return s[36]=='\0';
  }
  Command issue(Kind kind,uint32_t now) {
    Command cmd{};cmd.kind=kind; if(++nextTicket_==0)++nextTicket_;cmd.ticket=nextTicket_;
    if(kind==Kind::Poll)lastPoll_=now;
    if(kind==Kind::Upload || kind==Kind::Freeze || kind==Kind::Renew) {
      copy(cmd.binding,counter_.state().bindingId);copy(cmd.epoch,counter_.state().epochId);
      cmd.total=kind==Kind::Upload?counter_.state().total:counter_.state().frozenTotal;
    }
    if(kind==Kind::Freeze || kind==Kind::Renew || kind==Kind::Abort || kind==Kind::BarrierStatus)copy(cmd.barrier,counter_.state().barrierId);
    if(kind==Kind::Renew)copy(cmd.nonce,nonce_);
    sentAny_=true;lastSent_=now;inFlight_=cmd;return cmd;
  }
};
