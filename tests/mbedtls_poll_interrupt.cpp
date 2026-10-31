#include "../firmware/include/EspTcpOps.h"
#include "../firmware/include/MbedTlsChannel.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
struct Clock : ConnectionClock,MonotonicMillis64 {
 std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
 uint32_t nowMillis() override{return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()+10000);}
 bool read(uint64_t& out) override{out=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()+10000);return true;}
};
struct Yield : NetworkYield {void yield() override {std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
struct Dns : AsyncDnsBackend {int calls=0;bool start(Request* r) override {++calls;const uint8_t ip[]={127,0,0,1};r->complete(r->context,ip);return true;}};
// Test-only mapping avoids binding privileged port 443; production EspTcpOps
// connects to the port originally supplied to it without substitution.
struct MappedOps : TcpOps {
 EspTcpOps ops;uint16_t port;explicit MappedOps(uint16_t p):port(p){}
 int openNonblocking() override{return ops.openNonblocking();}
 Connect connect(int fd,const uint8_t ip[4],uint16_t requested) override {assert(requested==443);return ops.connect(fd,ip,port);}
 Connect connectionStatus(int fd) override{return ops.connectionStatus(fd);}
 int send(int fd,const uint8_t* d,size_t n) override{return ops.send(fd,d,n);}
 int receive(int fd,uint8_t* d,size_t n) override{return ops.receive(fd,d,n);}
 void close(int fd) override{ops.close(fd);}
};
static std::string file(const char* p){std::ifstream in(p);assert(in);return std::string(std::istreambuf_iterator<char>(in),{});}
int main(int argc,char** argv) {
 assert(argc==3);const auto ca=file(argv[2]);Clock clock;Yield scheduler;TrustedTimeWindow window(clock,60000,1000);
 Dns backend;AsyncDns dns(backend);MappedOps ops(static_cast<uint16_t>(std::stoi(argv[1])));
 NonblockingTcp tcp(ops);ResolvedTcpConnect resolved(dns,tcp);MbedTlsSession tls(tcp);VerifiedConnect connection(clock,tcp,resolved,tls);
 MbedTlsChannel channel(clock,scheduler,window,connection,tls,ca.c_str());
 uint64_t now=0;assert(clock.read(now));assert(window.acceptVerified(static_cast<uint64_t>(std::time(nullptr)),now,now));
 PollYieldPolicy priority;priority.validPoll();
 HttpRpcExchange exchange(channel,"project123.supabase.co","sb_publishable_test",5000,5000,true,&priority);char response[64]{};
 std::thread press([&]{std::this_thread::sleep_for(std::chrono::milliseconds(500));priority.accepted();});
 auto result=exchange.post("device_status","{}",2,response,sizeof(response));press.join();
 assert(result.status==RpcExchange::Status::Interrupted && exchange.lastPollYielded());
 assert(connection.state()==VerifiedConnect::State::Idle);
 std::cout<<"verified TLS stalled poll interruption passed\n";
}
