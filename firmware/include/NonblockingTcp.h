#pragma once
#include <stddef.h>
#include <stdint.h>

// Raw transport plumbing only. Never send device credentials or RPC data over
// this connection without an authenticated TLS layer. All operations in Ops
// must return immediately; the object is exclusively owned by the network task.
class TcpOps {
 public:
  enum class Connect { Ready, Pending, Error };
  virtual ~TcpOps()=default;
  virtual int openNonblocking()=0;
  virtual Connect connect(int fd,const uint8_t address[4],uint16_t port)=0;
  virtual Connect connectionStatus(int fd)=0;
  // Positive = bytes, zero = peer closed, -1 = error, -2 = would block.
  virtual int send(int fd,const uint8_t* data,size_t length)=0;
  virtual int receive(int fd,uint8_t* data,size_t capacity)=0;
  virtual void close(int fd)=0;
};

class NonblockingTcp {
 public:
  enum class State { Idle, Connecting, Connected, Error, Timeout };
  enum class IoStatus { Progress, WouldBlock, Closed, Error };
  struct IoResult {
    IoStatus status;size_t count;
    IoResult(IoStatus s=IoStatus::Error,size_t n=0):status(s),count(n){}
  };
  explicit NonblockingTcp(TcpOps& ops):ops_(ops) {}
  ~NonblockingTcp(){close();}
  NonblockingTcp(const NonblockingTcp&)=delete;
  NonblockingTcp& operator=(const NonblockingTcp&)=delete;
  State state() const {return state_;}
  bool start(const uint8_t ip[4],uint16_t port,uint32_t now,uint32_t timeoutMs) {
    if(state_!=State::Idle || !ip || !(ip[0]||ip[1]||ip[2]||ip[3]) || !port || !timeoutMs || timeoutMs>30000)return false;
    fd_=ops_.openNonblocking();
    if(fd_<0){state_=State::Error;return false;}
    start_=now;timeout_=timeoutMs;
    const auto result=ops_.connect(fd_,ip,port);
    if(result==TcpOps::Connect::Error){fail(State::Error);return false;}
    state_=result==TcpOps::Connect::Ready?State::Connected:State::Connecting;return true;
  }
  State poll(uint32_t now) {
    if(state_!=State::Connecting)return state_;
    if(static_cast<uint32_t>(now-start_)>=timeout_){fail(State::Timeout);return state_;}
    const auto result=ops_.connectionStatus(fd_);
    if(result==TcpOps::Connect::Error)fail(State::Error);
    else if(result==TcpOps::Connect::Ready)state_=State::Connected;
    return state_;
  }
  IoResult write(const uint8_t* data,size_t length) {
    if(state_!=State::Connected || !data || !length || length>16384)return {};
    return interpret(ops_.send(fd_,data,length),length);
  }
  IoResult read(uint8_t* data,size_t capacity) {
    if(state_!=State::Connected || !data || !capacity || capacity>16384)return {};
    return interpret(ops_.receive(fd_,data,capacity),capacity);
  }
  void close(){if(fd_>=0){ops_.close(fd_);fd_=-1;}state_=State::Idle;}
 private:
  TcpOps& ops_;int fd_=-1;State state_=State::Idle;uint32_t start_=0,timeout_=0;
  void fail(State state){if(fd_>=0){ops_.close(fd_);fd_=-1;}state_=state;}
  IoResult interpret(int n,size_t capacity) {
    if(n==-2)return {IoStatus::WouldBlock,0};
    if(n==0){fail(State::Error);return {IoStatus::Closed,0};}
    if(n<0 || static_cast<size_t>(n)>capacity){fail(State::Error);return {};}
    return {IoStatus::Progress,static_cast<size_t>(n)};
  }
};
