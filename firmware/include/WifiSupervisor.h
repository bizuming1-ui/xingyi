#pragma once
#include <stddef.h>
#include <stdint.h>

// Operations may invoke a platform networking stack and must run outside the
// GPIO/counting task. The caller owns and protects the credential buffers.
class WifiStation {
 public:
  virtual ~WifiStation()=default;
  virtual bool start(const char* ssid,const char* password)=0;
  virtual bool connected()=0;
  virtual void disconnect()=0;
};

// Single-owner Wi-Fi connection policy; never writes or logs credentials. A
// platform start/disconnect may block, so step() belongs in the network task.
// Runtime must serialize radio start/stop against hardware entropy reads.
class WifiSupervisor {
 public:
  enum class State { InvalidConfiguration, Idle, Connecting, Online, Backoff };
  static constexpr uint32_t CONNECT_WINDOW_MS=20000;
  static constexpr uint32_t RETRY_INTERVAL_MS=5000;
  WifiSupervisor(WifiStation& station,const char* ssid,const char* password)
    : station_(station),ssid_(ssid),password_(password),state_(valid(ssid,password)?State::Idle:State::InvalidConfiguration) {}
  State state() const {return state_;}
  void step(uint32_t now) {
    switch(state_) {
      case State::InvalidConfiguration: return;
      case State::Idle: attempt(now);return;
      case State::Connecting:
        if(station_.connected()){state_=State::Online;return;}
        if(elapsed(now,since_)>=CONNECT_WINDOW_MS){station_.disconnect();since_=now;state_=State::Backoff;}
        return;
      case State::Online:
        if(!station_.connected()){station_.disconnect();since_=now;state_=State::Backoff;}
        return;
      case State::Backoff:
        if(elapsed(now,since_)>=RETRY_INTERVAL_MS)attempt(now);
        return;
    }
  }
 private:
  WifiStation& station_;const char* ssid_;const char* password_;State state_;uint32_t since_=0;
  static uint32_t elapsed(uint32_t now,uint32_t then){return now-then;}
  void attempt(uint32_t now) {
    since_=now;
    if(station_.start(ssid_,password_))state_=State::Connecting;
    else {station_.disconnect();state_=State::Backoff;}
  }
  static size_t boundedLength(const char* value,size_t cap) {
    size_t n=0;while(n<cap && value[n])++n;return n;
  }
  static bool valid(const char* ssid,const char* password) {
    if(!ssid||!password)return false;
    const size_t ssidLength=boundedLength(ssid,33),passLength=boundedLength(password,65);
    if(ssidLength==0 || ssidLength>32 || passLength>64 || (passLength>0 && passLength<8))return false;
    if(passLength==64)for(size_t i=0;i<64;++i) {
      const char c=password[i];
      if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;
    }
    return true;
  }
};
