#include "../firmware/include/EspTcpOps.h"
#include "../firmware/include/MbedTlsChannel.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
struct Clock : ConnectionClock {
 std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
 uint32_t nowMillis() override{return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());}
};
struct Yield : NetworkYield {void yield() override {std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
struct Time : TrustedTime {bool good=true;bool trusted() override {return good;}};
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
 assert(argc==4);const auto ca=file(argv[2]),wrongCa=file(argv[3]);
 Clock clock;Yield scheduler;Time time;Dns backend;AsyncDns dns(backend);MappedOps ops(static_cast<uint16_t>(std::stoi(argv[1])));
 NonblockingTcp tcp(ops);ResolvedTcpConnect resolved(dns,tcp);MbedTlsSession tls(tcp);VerifiedConnect connection(clock,tcp,resolved,tls);
 MbedTlsChannel channel(clock,scheduler,time,connection,tls,ca.c_str());
 HttpRpcExchange exchange(channel,"project123.supabase.co","sb_publishable_test",3000);
 SyncPlanner::Command cmd{};cmd.kind=SyncPlanner::Kind::Poll;SyncPlanner::Reply reply{};
 auto out=RpcWorker::execute(exchange,cmd,"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",reply);
 assert(out.result==RpcWorker::Result::Ok && reply.success && connection.state()==VerifiedConnect::State::Idle);
 int count=backend.calls;time.good=false;char response[256]{};
 assert(exchange.post("device_status","{}",2,response,sizeof(response)).status==RpcExchange::Status::NetworkError && backend.calls==count);
 time.good=true;
 assert(channel.connectVerified("project123.supabase.co",443,3000)==VerifiedTlsChannel::Connect::Ok);
 time.good=false;assert(channel.write("x",1,1000)==-1 && connection.state()==VerifiedConnect::State::Idle);
 time.good=true;
 HttpRpcExchange wrongHost(channel,"wrong.supabase.co","sb_publishable_test",3000);
 assert(wrongHost.post("device_status","{}",2,response,sizeof(response)).status==RpcExchange::Status::NetworkError);
 MbedTlsChannel badChannel(clock,scheduler,time,connection,tls,wrongCa.c_str());
 HttpRpcExchange badCa(badChannel,"project123.supabase.co","sb_publishable_test",3000);
 assert(badCa.post("device_status","{}",2,response,sizeof(response)).status==RpcExchange::Status::NetworkError);
 HttpRpcExchange timeout(channel,"project123.supabase.co","sb_publishable_test",200);
 assert(timeout.post("device_status","{\"stall\":true}",14,response,sizeof(response)).status==RpcExchange::Status::Timeout);
 assert(connection.state()==VerifiedConnect::State::Idle);
 std::cout<<"verified local HTTPS RPC integration and timeout tests passed\n";
}
