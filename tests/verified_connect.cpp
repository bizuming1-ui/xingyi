#include "../firmware/include/EspTcpOps.h"
#include "../firmware/include/VerifiedConnect.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
struct Clock : ConnectionClock {uint32_t now=0;uint32_t nowMillis() override{return now;}};
struct Dns : AsyncDnsBackend {
 std::vector<Request*> requests;
 bool start(Request* r) override {requests.push_back(r);return true;}
 void finish(size_t n,bool ok=true) {const uint8_t ip[]={127,0,0,1};auto* r=requests.at(n);r->complete(r->context,ok?ip:nullptr);}
};
static std::string file(const char* p){std::ifstream in(p);assert(in);return std::string(std::istreambuf_iterator<char>(in),{});}
static void progress(VerifiedConnect& c,Clock& clock) {
 for(unsigned i=0;i<4999 && (c.state()==VerifiedConnect::State::Connecting || c.state()==VerifiedConnect::State::Handshaking);++i){
  c.poll();++clock.now;std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
}
int main(int argc,char** argv){
 assert(argc==3);auto ca=file(argv[2]);int port=std::stoi(argv[1]);
 Dns backend;AsyncDns dns(backend);EspTcpOps ops;NonblockingTcp tcp(ops);
 ResolvedTcpConnect resolved(dns,tcp);MbedTlsSession tls(tcp);Clock clock;
 VerifiedConnect c(clock,tcp,resolved,tls);using S=VerifiedConnect::State;
 assert(!c.begin("project123.supabase.co",port,ca.c_str(),false,5000));
 assert(!c.begin("bad.example",port,ca.c_str(),true,5000));assert(backend.requests.empty());
 // A wrong TCP pairing fails without disturbing either transport.
 NonblockingTcp otherTcp(ops);MbedTlsSession otherTls(otherTcp);
 {VerifiedConnect invalid(clock,tcp,resolved,otherTls);assert(!invalid.begin("project123.supabase.co",port,ca.c_str(),true,5000));}
 assert(c.begin("project123.supabase.co",port,ca.c_str(),true,5000));
 assert(c.poll()==S::Connecting && tls.state()==MbedTlsSession::State::Idle);
 backend.finish(0);progress(c,clock);assert(c.state()==S::Verified && tls.state()==MbedTlsSession::State::Verified);
 const std::string request="GET / HTTP/1.0\r\n\r\n";size_t sent=0;
 for(int i=0;i<1000 && sent<request.size();++i){auto r=tls.write(reinterpret_cast<const uint8_t*>(request.data()+sent),request.size()-sent);assert(r.status==MbedTlsSession::Status::Progress || r.status==MbedTlsSession::Status::WantIo);sent+=r.count;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(sent==request.size());uint8_t buf[512];bool received=false;
 for(int i=0;i<1000 && !received;++i){auto r=tls.read(buf,sizeof(buf));assert(r.status==MbedTlsSession::Status::Progress || r.status==MbedTlsSession::Status::WantIo);received=r.count>0;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(received);c.close();
 assert(c.begin("wrong.supabase.co",port,ca.c_str(),true,5000));backend.finish(1);progress(c,clock);assert(c.state()==S::Failed);c.close();
 // Timeout across unsigned clock wrap abandons the unresolved DNS slot safely.
 clock.now=UINT32_MAX-4;assert(c.begin("project123.supabase.co",port,ca.c_str(),true,10));
 clock.now=4;assert(c.poll()==S::Connecting);clock.now=5;assert(c.poll()==S::Timeout);
 backend.finish(2);c.close();
 // DNS time uses the same budget as the later TLS handshake.
 clock.now=100;assert(c.begin("project123.supabase.co",port,ca.c_str(),true,10));
 backend.finish(3);clock.now=110;assert(c.poll()==S::Timeout);c.close();
 assert(c.begin("project123.supabase.co",port,ca.c_str(),true,100));backend.finish(4,false);assert(c.poll()==S::Failed);c.close();
 // Reaching TCP does not restart the overall setup deadline for TLS.
 clock.now=1000;assert(c.begin("project123.supabase.co",port,ca.c_str(),true,100));backend.finish(5);
 for(int i=0;i<1000 && c.state()==S::Connecting;++i){c.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(c.state()==S::Handshaking);clock.now=1100;assert(c.poll()==S::Timeout);
 assert(tcp.state()==NonblockingTcp::State::Idle && tls.state()==MbedTlsSession::State::Idle);c.close();
 std::cout<<"DNS/TCP/TLS unified setup tests passed\n";
}
