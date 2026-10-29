#include "../firmware/include/EspTcpOps.h"
#include "../firmware/include/TimeBootstrapExchange.h"
#include "../firmware/include/TimeWindowSynchronizer.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
struct Clock : ConnectionClock,MonotonicMillis64 {std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();uint32_t nowMillis() override {return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());} bool read(uint64_t& out) override {out=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());return true;}};
struct Yield : NetworkYield {void yield() override {std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
struct Entropy : SecureEntropy {uint8_t pattern=1;bool fill(uint8_t* b,size_t n) override {for(size_t i=0;i<n;++i)b[i]=static_cast<uint8_t>(pattern+i);return true;}};
struct Dns : AsyncDnsBackend {int calls=0;bool start(Request* r) override {++calls;const uint8_t ip[]={127,0,0,1};r->complete(r->context,ip);return true;}};
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
 assert(argc==4);const auto ca=file(argv[2]),wrongCa=file(argv[3]);
 const char* id="11111111-1111-4111-8111-111111111111";
 const char* secret="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
 Clock clock;Yield scheduler;Entropy entropy;TimeProof proof(entropy);Dns backend;AsyncDns dns(backend);
 MappedOps ops(static_cast<uint16_t>(std::stoi(argv[1])));NonblockingTcp tcp(ops);ResolvedTcpConnect resolved(dns,tcp);
 TimeBootstrapExchange bootstrap(clock,scheduler,tcp,resolved,proof);using R=TimeBootstrapExchange::Result;
 uint64_t seconds=42;
 assert(bootstrap.fetch("project123.supabase.co","sb_publishable_test",ca.c_str(),id,secret,3000,seconds)==R::Ok);
 assert(seconds>=1704067200ULL && seconds<=4102444800ULL && tcp.state()==NonblockingTcp::State::Idle && !proof.nonce());
 TrustedTimeWindow window(clock,10000,1000);TimeWindowSynchronizer sync(clock,window,bootstrap);
 assert(sync.refresh("project123.supabase.co","sb_publishable_test",ca.c_str(),id,secret,3000)==R::Ok);
 TrustedTimeWindow::Bounds bounds{};assert(window.bounds(bounds));
 assert(bounds.earliestMs>=1704067200000ULL && bounds.latestMs>bounds.earliestMs);
 assert(sync.refresh("project123.supabase.co","sb_publishable_test",ca.c_str(),id,"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",3000)==R::InvalidProof);
 assert(!window.bounds(bounds));
 seconds=42;
 assert(bootstrap.fetch("wrong.supabase.co","sb_publishable_test",ca.c_str(),id,secret,3000,seconds)==R::Unavailable && seconds==42);
 assert(bootstrap.fetch("project123.supabase.co","sb_publishable_test",wrongCa.c_str(),id,secret,3000,seconds)==R::Unavailable && seconds==42);
 const int calls=backend.calls;
 assert(bootstrap.fetch("project123.supabase.co","sb_secret_test",ca.c_str(),id,secret,3000,seconds)==R::Unavailable && backend.calls==calls);
 assert(bootstrap.fetch("project123.supabase.co","sb_publishable_test",ca.c_str(),id,"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",3000,seconds)==R::InvalidProof && seconds==42);
 entropy.pattern=0x11;
 assert(bootstrap.fetch("project123.supabase.co","sb_publishable_test",ca.c_str(),id,secret,3000,seconds)==R::InvalidProof && seconds==42);
 entropy.pattern=0x10;
 assert(bootstrap.fetch("project123.supabase.co","sb_publishable_test",ca.c_str(),id,secret,200,seconds)==R::Timeout && seconds==42);
 assert(tcp.state()==NonblockingTcp::State::Idle && !proof.nonce());
 std::cout<<"time-only bootstrap: date exception, CA/host checks, proof and timeout passed\n";
}
