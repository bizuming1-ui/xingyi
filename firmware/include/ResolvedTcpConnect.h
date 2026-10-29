#pragma once
#include "AsyncDns.h"
#include "NonblockingTcp.h"

// Network-task-only composition of DNS and numeric-IP TCP. This is NOT a
// verified channel: do not send secrets or RPCs. The resolver and its backend
// must outlive all callbacks, including callbacks after this object closes.
// This object exclusively owns use of tcp for its entire lifetime; neither the
// resolver handle nor tcp may be manipulated elsewhere while an attempt runs.
// One deadline covers DNS and TCP setup; it does not cover TLS or HTTP.
class ResolvedTcpConnect {
 public:
  enum class State { Idle, Resolving, Connecting, Connected, Failed, Timeout };
  ResolvedTcpConnect(AsyncDns& dns,NonblockingTcp& tcp):dns_(dns),tcp_(tcp) {}
  ~ResolvedTcpConnect(){close();}
  ResolvedTcpConnect(const ResolvedTcpConnect&)=delete;
  ResolvedTcpConnect& operator=(const ResolvedTcpConnect&)=delete;
  State state() const {return state_;}
  // Original hostname, not the resolved address; future TLS must authenticate it.
  const char* hostname() const {return hostname_;}
  bool begin(const char* host,uint16_t port,uint32_t now,uint32_t timeoutMs) {
    if(state_!=State::Idle || tcp_.state()!=NonblockingTcp::State::Idle || !port || !timeoutMs || timeoutMs>30000)return false;
    AsyncDns::Handle handle{};
    if(!dns_.begin(host,handle))return false;
    // dns.begin has already validated the string and its bounded length.
    memcpy(hostname_,host,strlen(host)+1);
    handle_=handle;pending_=true;port_=port;started_=now;timeout_=timeoutMs;state_=State::Resolving;
    return true;
  }
  State poll(uint32_t now) {
    if(state_!=State::Resolving && state_!=State::Connecting)return state_;
    const uint32_t elapsed=now-started_;
    if(elapsed>=timeout_){stopPending();tcp_.close();state_=State::Timeout;return state_;}
    if(state_==State::Resolving) {
      uint8_t address[4]{};
      const auto result=dns_.poll(handle_,address);
      if(result==AsyncDns::Result::Pending)return state_;
      // Success and Failed consume their slot; Invalid cannot safely be used.
      pending_=false;
      if(result!=AsyncDns::Result::Success){tcp_.close();state_=State::Failed;return state_;}
      if(!tcp_.start(address,port_,now,timeout_-elapsed)){tcp_.close();state_=State::Failed;return state_;}
      state_=tcp_.state()==NonblockingTcp::State::Connected?State::Connected:State::Connecting;
      return state_;
    }
    const auto result=tcp_.poll(now);
    if(result==NonblockingTcp::State::Connected)state_=State::Connected;
    else if(result==NonblockingTcp::State::Timeout){tcp_.close();state_=State::Timeout;}
    else if(result!=NonblockingTcp::State::Connecting){tcp_.close();state_=State::Failed;}
    return state_;
  }
  void close(){stopPending();tcp_.close();hostname_[0]='\0';state_=State::Idle;}
 private:
  AsyncDns& dns_;NonblockingTcp& tcp_;AsyncDns::Handle handle_{};
  char hostname_[80]{};uint16_t port_=0;uint32_t started_=0,timeout_=0;
  bool pending_=false;State state_=State::Idle;
  void stopPending(){if(pending_){dns_.expire(handle_);pending_=false;}}
};
