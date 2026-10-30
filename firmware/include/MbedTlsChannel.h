#pragma once
#include "HttpRpcExchange.h"
#include "VerifiedConnect.h"

// Must yield promptly in the network task without resetting Wi-Fi or touching
// transport/session objects. It must not indefinitely block or stop the clock.
class NetworkYield {
 public:
  virtual ~NetworkYield()=default;
  virtual void yield()=0;
};

// Blocking facade for RpcWorker's dedicated network task; the counter task must
// never call this. One operation polls nonblocking transport and yields between
// WANT_IO responses. DNS, TLS, clock, trust source, CA and Wi-Fi lifetime must
// be exclusively managed for the duration. CPU crypto cannot be preempted by
// these deadlines, and HTTPS still requires a full request deadline in caller.
class MbedTlsChannel final : public VerifiedTlsChannel {
 public:
  MbedTlsChannel(ConnectionClock& clock,NetworkYield& scheduler,TrustedTimeWindow& time,
                 VerifiedConnect& connection,MbedTlsSession& tls,const char* ca)
   :clock_(clock),scheduler_(scheduler),time_(time),connection_(connection),tls_(tls),ca_(ca) {}
  uint32_t nowMillis() override {return clock_.nowMillis();}
  Connect connectVerified(const char* host,uint16_t port,uint32_t timeoutMs) override {
    if(!timeoutMs || timeoutMs>30000 || !timeValid())return Connect::Error;
    const uint32_t start=clock_.nowMillis();
    if(!connection_.beginBounded(host,port,ca_,time_,timeoutMs))
      return connection_.state()==VerifiedConnect::State::Timeout?Connect::Timeout:Connect::Error;
    for(;;) {
      if(elapsed(start,timeoutMs)){close();return Connect::Timeout;}
      if(!timeValid()){close();return Connect::Error;}
      const auto state=connection_.poll();
      if(elapsed(start,timeoutMs)){close();return Connect::Timeout;}
      if(!timeValid()){close();return Connect::Error;}
      if(state==VerifiedConnect::State::Verified)return Connect::Ok;
      if(state==VerifiedConnect::State::Timeout){close();return Connect::Timeout;}
      if(state==VerifiedConnect::State::Failed || state==VerifiedConnect::State::Idle){close();return Connect::Error;}
      scheduler_.yield();
    }
  }
  int write(const char* data,size_t length,uint32_t timeoutMs) override {
    if(!data || !length || length>16384 || !timeoutMs || timeoutMs>30000)return -1;
    const uint32_t start=clock_.nowMillis();
    for(;;) {
      if(elapsed(start,timeoutMs)){close();return -2;}
      if(!canReuse()){close();return -1;}
      const auto r=tls_.write(reinterpret_cast<const uint8_t*>(data),length);
      if(elapsed(start,timeoutMs)){close();return -2;}
      if(!canReuse()){close();return -1;}
      if(r.status==MbedTlsSession::Status::Progress)return static_cast<int>(r.count);
      if(r.status!=MbedTlsSession::Status::WantIo){close();return -1;}
      scheduler_.yield();
    }
  }
  int read(char* data,size_t capacity,uint32_t timeoutMs) override {
    if(!data || !capacity || capacity>16384 || !timeoutMs || timeoutMs>30000)return -1;
    const uint32_t start=clock_.nowMillis();
    for(;;) {
      if(elapsed(start,timeoutMs)){close();return -2;}
      if(!canReuse()){close();return -1;}
      const auto r=tls_.read(reinterpret_cast<uint8_t*>(data),capacity);
      if(elapsed(start,timeoutMs)){close();return -2;}
      if(!canReuse()){close();return -1;}
      if(r.status==MbedTlsSession::Status::Progress)return static_cast<int>(r.count);
      if(r.status==MbedTlsSession::Status::Closed){close();return 0;}
      if(r.status!=MbedTlsSession::Status::WantIo){close();return -1;}
      scheduler_.yield();
    }
  }
  bool canReuse() override {return connection_.state()==VerifiedConnect::State::Verified && timeValid() && tls_.datesStillValid();}
  void close() override {connection_.close();}
 private:
  ConnectionClock& clock_;NetworkYield& scheduler_;TrustedTimeWindow& time_;VerifiedConnect& connection_;
  MbedTlsSession& tls_;const char* ca_;
  bool timeValid(){TrustedTimeWindow::Bounds b{};return time_.bounds(b);}
  bool elapsed(uint32_t start,uint32_t limit){return static_cast<uint32_t>(clock_.nowMillis()-start)>=limit;}
};
