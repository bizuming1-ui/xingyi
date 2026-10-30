#include "../firmware/include/EspTcpOps.h"
#include "../firmware/include/TimeBootstrapExchange.h"
#include "../firmware/include/TimeWindowSynchronizer.h"
#include "../firmware/include/AuthenticatedNetworkTime.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
struct Clock : ConnectionClock,MonotonicMillis64,NetworkTaskClock {std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();uint32_t nowMillis() override {return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());} bool read(uint64_t& out) override {out=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());return true;}};
struct Yield : NetworkYield {void yield() override {std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
struct Entropy : SecureEntropy {uint8_t pattern=1;bool fill(uint8_t* b,size_t n) override {for(size_t i=0;i<n;++i)b[i]=static_cast<uint8_t>(pattern+i);return true;}};
struct Dns : AsyncDnsBackend {int calls=0;bool start(Request* r) override {++calls;const uint8_t ip[]={127,0,0,1};r->complete(r->context,ip);return true;}};
struct RadioLock : RadioLifecycleLock {bool held=false;bool tryLock() override{if(held)return false;held=true;return true;}void unlock() override{held=false;}};
struct Station : WifiStation {bool online=false;bool start(const char*,const char*) override{return true;}bool connected() override{return online;}void disconnect() override{}};
struct Mailbox : SyncMailbox {
 SyncPlanner::Command command{};Completion result{};bool hasCommand=false,hasResult=false;
 bool submit(const SyncPlanner::Command& c) override{if(hasCommand)return false;command=c;hasCommand=true;return true;}
 bool take(SyncPlanner::Command& c) override{if(!hasCommand)return false;c=command;hasCommand=false;return true;}
 bool collect(Completion& r) override{if(!hasResult)return false;r=result;hasResult=false;return true;}
 bool publish(const Completion& r) override{if(hasResult)return false;result=r;hasResult=true;return true;}
};
struct MappedOps : TcpOps {
 EspTcpOps ops;uint16_t port;explicit MappedOps(uint16_t p):port(p){}
 int openNonblocking() override {return ops.openNonblocking();}
 Connect connect(int fd,const uint8_t ip[4],uint16_t requested) override {assert(requested==443);return ops.connect(fd,ip,port);}
 Connect connectionStatus(int fd) override {return ops.connectionStatus(fd);}
 int send(int fd,const uint8_t* data,size_t n) override {return ops.send(fd,data,n);}
 int receive(int fd,uint8_t* data,size_t n) override {return ops.receive(fd,data,n);}
 void close(int fd) override {ops.close(fd);}
};
static std::string file(const char* path){std::ifstream in(path);assert(in);return std::string(std::istreambuf_iterator<char>(in),{});}
int main(int argc,char** argv){
 assert(argc==4 || argc==5);const auto ca=file(argv[2]),wrongCa=file(argv[3]);
 const char* id="11111111-1111-4111-8111-111111111111";
 const char* secret="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
 Clock clock;Yield scheduler;Entropy entropy;TimeProof proof(entropy);Dns backend;AsyncDns dns(backend);
 MappedOps ops(static_cast<uint16_t>(std::stoi(argv[1])));NonblockingTcp tcp(ops);ResolvedTcpConnect resolved(dns,tcp);
 TimeBootstrapExchange bootstrap(clock,scheduler,tcp,resolved,proof);using R=TimeBootstrapExchange::Result;
 uint64_t seconds=42;
 if(argc==5) {
  TrustedTimeWindow window(clock,10000,1000);TimeWindowSynchronizer sync(clock,window,bootstrap);
  MbedTlsSession tls(tcp);VerifiedConnect verified(clock,tcp,resolved,tls);
  MbedTlsChannel channel(clock,scheduler,window,verified,tls,ca.c_str());
  HttpRpcExchange exchange(channel,"project123.supabase.co","sb_publishable_test",3000);
  Mailbox mailbox;SyncNetworkWorker worker(mailbox,exchange,secret);
  RadioLock lock;Station station;WifiSupervisor supervisor(station,"network","password");GuardedWifiSupervisor wifi(lock,supervisor);
  AuthenticatedNetworkTime time(window,sync,"project123.supabase.co","sb_publishable_test",ca.c_str(),id,secret,3000);
  NetworkTaskCoordinator coordinator(clock,wifi,time,worker);
  SyncPlanner::Command cmd{};cmd.kind=SyncPlanner::Kind::Poll;cmd.ticket=7;assert(mailbox.submit(cmd));
  coordinator.step();SyncMailbox::Completion result{};assert(mailbox.collect(result) && !result.reply.success);
  station.online=true;assert(mailbox.submit(cmd));coordinator.step();
  assert(mailbox.collect(result) && result.ticket==7 && result.reply.success);
  std::cout<<"network coordinator: authenticated time bootstrap through verified HTTPS RPC passed\n";return 0;
 }
 assert(bootstrap.fetch("project123.supabase.co","sb_publishable_test",ca.c_str(),id,secret,3000,seconds)==R::Ok);
 assert(seconds>=1704067200ULL && seconds<=4102444800ULL && tcp.state()==NonblockingTcp::State::Idle && !proof.nonce());
 assert(bootstrap.lastStage()==TimeBootstrapExchange::Stage::Complete && bootstrap.lastDetail()==0);
 assert(bootstrap.lastTxBytes()>0 && bootstrap.lastRxBytes()>0 && bootstrap.lastHandshakeStartMs()<3000);
 TrustedTimeWindow window(clock,10000,1000);TimeWindowSynchronizer sync(clock,window,bootstrap);
 assert(sync.refresh("project123.supabase.co","sb_publishable_test",ca.c_str(),id,secret,3000)==R::Ok);
 TrustedTimeWindow::Bounds bounds{};assert(window.bounds(bounds));
 assert(bounds.earliestMs>=1704067200000ULL && bounds.latestMs>bounds.earliestMs);
 // Bad new proof must not extend or destroy a still-valid old proof.
 assert(sync.refreshPreserving("project123.supabase.co","sb_publishable_test",ca.c_str(),id,"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",3000)==R::InvalidProof);
 assert(window.bounds(bounds));
 assert(sync.refreshPreserving("project123.supabase.co","sb_publishable_test",ca.c_str(),id,secret,3000)==R::Ok);
 assert(window.bounds(bounds));
 assert(sync.refresh("project123.supabase.co","sb_publishable_test",ca.c_str(),id,"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",3000)==R::InvalidProof);
 assert(!window.bounds(bounds));assert(sync.lastStage()==TimeBootstrapExchange::Stage::Proof);
 seconds=42;
 assert(bootstrap.fetch("wrong.supabase.co","sb_publishable_test",ca.c_str(),id,secret,3000,seconds)==R::Unavailable && seconds==42);
 assert(bootstrap.fetch("project123.supabase.co","sb_publishable_test",wrongCa.c_str(),id,secret,3000,seconds)==R::Unavailable && seconds==42);
 assert(bootstrap.lastStage()==TimeBootstrapExchange::Stage::TlsHandshake && bootstrap.lastDetail()!=0);
 const int calls=backend.calls;
 assert(bootstrap.fetch("project123.supabase.co","sb_secret_test",ca.c_str(),id,secret,3000,seconds)==R::Unavailable && backend.calls==calls);
 assert(bootstrap.lastStage()==TimeBootstrapExchange::Stage::Validate);
 assert(bootstrap.fetch("project123.supabase.co","sb_publishable_test",ca.c_str(),id,"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",3000,seconds)==R::InvalidProof && seconds==42);
 entropy.pattern=0x11;
 assert(bootstrap.fetch("project123.supabase.co","sb_publishable_test",ca.c_str(),id,secret,3000,seconds)==R::InvalidProof && seconds==42);
 entropy.pattern=0x10;
 assert(bootstrap.fetch("project123.supabase.co","sb_publishable_test",ca.c_str(),id,secret,200,seconds)==R::Timeout && seconds==42);
 assert(bootstrap.lastStage()==TimeBootstrapExchange::Stage::Receive);
 assert(tcp.state()==NonblockingTcp::State::Idle && !proof.nonce());
 // Same delayed response: 3s total rejects, 4.5s accepts with a validated window.
 entropy.pattern=0x12;
 assert(bootstrap.fetch("project123.supabase.co","sb_publishable_test",ca.c_str(),id,secret,3000,seconds)==R::Timeout);
 assert(bootstrap.lastStage()==TimeBootstrapExchange::Stage::Receive);
 assert(bootstrap.lastTxBytes()>0 && bootstrap.lastRxBytes()>0);
 assert(sync.refresh("project123.supabase.co","sb_publishable_test",ca.c_str(),id,secret,4500)==R::Ok);
 assert(window.bounds(bounds) && sync.lastStage()==TimeBootstrapExchange::Stage::Complete);
 // Simulate a link whose time response takes over 5 seconds: the first hard
 // recovery times out, then the longer retry accepts only a valid signed proof.
 // Its uncertainty interval includes the full duration, not just server time.
 entropy.pattern=0x13;
 AuthenticatedNetworkTime recovery(window,sync,"project123.supabase.co","sb_publishable_test",ca.c_str(),id,secret,4500,10000);
 assert(!recovery.refresh() && recovery.lastResult()==static_cast<int>(R::Timeout));
 assert(!recovery.trusted() && recovery.lastBudget()==4500);
 assert(recovery.refresh() && recovery.trusted() && recovery.lastBudget()==10000);
 assert(window.bounds(bounds) && bounds.latestMs-bounds.earliestMs>=6200);
 std::cout<<"time-only bootstrap: date exception, CA/host checks, proof and timeout passed\n";
}
