#pragma once
#include "ResolvedTcpConnect.h"
#include "MbedTlsSession.h"

// Monotonic milliseconds, wrapping at 2^32. Must not jump backwards other
// than natural wrap; independent of the wall clock used for certificates.
class ConnectionClock {
 public:
  virtual ~ConnectionClock()=default;
  virtual uint32_t nowMillis()=0;
};

// Network-task-only connection setup. The DNS hostname is also the exact TLS
// verification hostname. A single deadline covers DNS, TCP and TLS setup.
// No application data is sent here. Caller owns all objects and guarantees
// their lifetimes, exclusive use and a stable CA buffer during setup. The DNS
// resolver/backend must outlive all late callbacks after timeout or close.
// Polling is cooperative: crypto may exceed a deadline before control returns.
// This is not yet an HTTP exchange or a full-request deadline.
class VerifiedConnect {
 public:
  enum class State { Idle, Connecting, Handshaking, Verified, Failed, Timeout };
  VerifiedConnect(ConnectionClock& clock,NonblockingTcp& tcp,ResolvedTcpConnect& resolved,MbedTlsSession& tls)
    :clock_(clock),tcp_(tcp),resolved_(resolved),tls_(tls) {}
  ~VerifiedConnect(){close();}
  VerifiedConnect(const VerifiedConnect&)=delete;
  VerifiedConnect& operator=(const VerifiedConnect&)=delete;
  // Verified means setup succeeded; later I/O must still check TLS status.
  State state() const {return state_;}
  bool begin(const char* host,uint16_t port,const char* ca,bool trustedTime,uint32_t timeoutMs) {
    return beginInternal(host,port,ca,trustedTime,nullptr,timeoutMs);
  }
  bool beginBounded(const char* host,uint16_t port,const char* ca,TrustedTimeWindow& window,uint32_t timeoutMs) {
    TrustedTimeWindow::Bounds bounds{};
    if(!window.bounds(bounds))return false;
    return beginInternal(host,port,ca,true,&window,timeoutMs);
  }
 private:
  bool beginInternal(const char* host,uint16_t port,const char* ca,bool trustedTime,TrustedTimeWindow* window,uint32_t timeoutMs) {
    if(state_!=State::Idle || !ca || !trustedTime || !timeoutMs || timeoutMs>30000 ||
       !resolved_.usesTransport(tcp_) || !tls_.usesTransport(tcp_) ||
       resolved_.state()!=ResolvedTcpConnect::State::Idle || tls_.state()!=MbedTlsSession::State::Idle ||
       tcp_.state()!=NonblockingTcp::State::Idle)return false;
    started_=clock_.nowMillis();timeout_=timeoutMs;ca_=ca;
    if(!resolved_.begin(host,port,started_,timeoutMs)){ca_=nullptr;return false;}
    active_=true;window_=window;state_=State::Connecting;
    if(expired()){finish(State::Timeout);return false;}
    return true;
  }
 public:
  State poll() {
    if(state_!=State::Connecting && state_!=State::Handshaking)return state_;
    if(expired()){finish(State::Timeout);return state_;}
    TrustedTimeWindow::Bounds bounds{};
    if(window_ && !window_->bounds(bounds)){finish(State::Failed);return state_;}
    if(state_==State::Connecting) {
      const auto result=resolved_.poll(clock_.nowMillis());
      if(expired()){finish(State::Timeout);return state_;}
      if(result==ResolvedTcpConnect::State::Timeout){finish(State::Timeout);return state_;}
      if(result==ResolvedTcpConnect::State::Failed || result==ResolvedTcpConnect::State::Idle){finish(State::Failed);return state_;}
      if(result!=ResolvedTcpConnect::State::Connected)return state_;
      // The caller cannot substitute the resolved numeric address or a second
      // host here: authenticate exactly the host that DNS was asked to resolve.
      const bool ok=window_?tls_.beginBounded(resolved_.hostname(),ca_,*window_):tls_.begin(resolved_.hostname(),ca_,true);
      ca_=nullptr;
      if(expired()){finish(State::Timeout);return state_;}
      if(!ok){finish(State::Failed);return state_;}
      state_=State::Handshaking;
      return state_;
    }
    const auto result=tls_.handshake();
    if(expired()){finish(State::Timeout);return state_;}
    if(result==MbedTlsSession::State::Verified)state_=State::Verified;
    else if(result!=MbedTlsSession::State::Handshaking)finish(State::Failed);
    return state_;
  }
  void close(){cleanup();state_=State::Idle;}
 private:
  ConnectionClock& clock_;NonblockingTcp& tcp_;ResolvedTcpConnect& resolved_;MbedTlsSession& tls_;
  State state_=State::Idle;uint32_t started_=0,timeout_=0;const char* ca_=nullptr;bool active_=false;TrustedTimeWindow* window_=nullptr;
  bool expired(){return static_cast<uint32_t>(clock_.nowMillis()-started_)>=timeout_;}
  void cleanup(){if(active_){tls_.reset();resolved_.close();active_=false;}ca_=nullptr;window_=nullptr;}
  void finish(State state){cleanup();state_=state;}
};
