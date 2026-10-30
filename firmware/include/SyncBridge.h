#pragma once
#include "RpcWorker.h"

// Two bounded, thread-safe one-item queues. Only the counting thread may call
// submit/collect; only the network thread may call take/publish. No credentials
// are included. Implementations must copy items and never wait in these calls.
class SyncMailbox {
 public:
  struct Completion { uint32_t ticket=0; SyncPlanner::Reply reply{}; };
  virtual ~SyncMailbox() = default;
  virtual bool submit(const SyncPlanner::Command& command)=0;
  virtual bool collect(Completion& completion)=0;
  virtual bool take(SyncPlanner::Command& command)=0;
  virtual bool publish(const Completion& completion)=0;
};

// Exclusively owned by the GPIO/counter thread. Never performs network I/O.
class SyncController {
 public:
  SyncController(SyncPlanner& planner,SyncMailbox& mailbox) : planner_(planner), mailbox_(mailbox) {}
  bool faulted() const { return fault_ || planner_.faulted(); }
  int diagnosticFault() const {return faultReason_;}
  void step(uint32_t now) {
    if (faulted()) return;
    SyncMailbox::Completion done{};
    if (mailbox_.collect(done) && !planner_.complete(done.ticket,done.reply,now)) {
      fault_=true;faultReason_=1; return; // unexpected/stale completion: stop safely
    }
    const auto command=planner_.next(now);
    if (command.kind!=SyncPlanner::Kind::None && !mailbox_.submit(command)) {
      // A full queue violates the one-in-flight invariant. Do not continue or
      // silently replace the command; require explicit operator recovery.
      SyncPlanner::Reply failed{};
      (void)planner_.complete(command.ticket,failed,now);
      fault_=true;faultReason_=2;
    }
  }
 private:
  SyncPlanner& planner_;
  SyncMailbox& mailbox_;
  bool fault_=false;int faultReason_=0;
};

// Exclusively owned by the network thread. step() can block in the transport;
// it must NEVER be called from the GPIO thread. The caller owns secret and must
// keep it alive, private and unchanged while this object exists.
class SyncNetworkWorker {
 public:
  SyncNetworkWorker(SyncMailbox& mailbox,RpcExchange& exchange,const char* secret)
    : mailbox_(mailbox),exchange_(exchange),secret_(secret) {}
  SyncPlanner::Kind lastKind() const {return lastKind_;}
  RpcWorker::Outcome lastOutcome() const {return lastOutcome_;}
  void closeConnection() {exchange_.close();}
  // Flush an already executed result without issuing or discarding an RPC.
  void flushPending() { if (pending_) publish(); }
  // When the network is unavailable, report a failed command so the counting
  // task retains all pending counts and can retry. Preserve any prior result.
  void failUnavailable() {
    if (pending_) { publish(); return; }
    SyncPlanner::Command command{};
    if (!mailbox_.take(command)) return;
    completion_={};completion_.ticket=command.ticket;pending_=true;publish();
  }
  void step() {
    if (pending_) { publish(); return; }
    SyncPlanner::Command command{};
    if (!mailbox_.take(command)) return;
    completion_={}; completion_.ticket=command.ticket;
    const auto outcome=RpcWorker::execute(exchange_,command,secret_,completion_.reply);
    lastKind_=command.kind;lastOutcome_=outcome;
    if (outcome.result!=RpcWorker::Result::Ok) completion_.reply={};
    pending_=true;
    publish();
  }
 private:
  SyncMailbox& mailbox_;
  RpcExchange& exchange_;
  const char* secret_;
  SyncMailbox::Completion completion_{};
  bool pending_=false;
  SyncPlanner::Kind lastKind_=SyncPlanner::Kind::None;RpcWorker::Outcome lastOutcome_{};
  void publish() {
    // If temporarily full, keep the exact result and retry; do not repeat RPC.
    if (mailbox_.publish(completion_)) { completion_={}; pending_=false; }
  }
};
