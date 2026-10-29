#include "../firmware/include/EspTcpOps.h"
#include "../firmware/include/MbedTlsSession.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
using Clock=std::chrono::steady_clock;
static std::string file(const char* path){std::ifstream in(path);assert(in);return std::string(std::istreambuf_iterator<char>(in),{});}
static void connect(NonblockingTcp& tcp,int port) {
 const uint8_t ip[]={127,0,0,1};assert(tcp.start(ip,static_cast<uint16_t>(port),0,5000));
 for(unsigned i=0;i<5000 && tcp.state()==NonblockingTcp::State::Connecting;++i){tcp.poll(i);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(tcp.state()==NonblockingTcp::State::Connected);
}
static bool handshake(MbedTlsSession& tls) {
 auto deadline=Clock::now()+std::chrono::seconds(5);
 while(tls.state()==MbedTlsSession::State::Handshaking && Clock::now()<deadline){tls.handshake();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 return tls.state()==MbedTlsSession::State::Verified;
}
int main(int argc,char** argv){
 assert(argc==4 || argc==5);const int port=std::stoi(argv[1]);const auto ca=file(argv[2]),wrongCa=file(argv[3]);
 EspTcpOps ops;NonblockingTcp tcp(ops);MbedTlsSession tls(tcp);
 using S=MbedTlsSession::Status;
 if(argc==5){connect(tcp,port);assert(tls.begin("project123.supabase.co",ca.c_str(),true));assert(!handshake(tls));std::cout<<"expired certificate rejected\n";return 0;}
 uint8_t byte=0;
 connect(tcp,port);assert(!tls.begin("project123.supabase.co",ca.c_str(),false));
 assert(tls.state()==MbedTlsSession::State::Failed && tls.write(&byte,1).status==S::Error);tls.reset();
 connect(tcp,port);assert(!tls.begin("project123.supabase.co.evil",ca.c_str(),true));tls.reset();
 connect(tcp,port);assert(tls.begin("wrong.supabase.co",ca.c_str(),true));
 assert(tls.write(&byte,1).status==S::Error && !handshake(tls));tls.reset();
 connect(tcp,port);assert(tls.begin("project123.supabase.co",wrongCa.c_str(),true));assert(!handshake(tls));tls.reset();
 connect(tcp,port);assert(tls.begin("project123.supabase.co",ca.c_str(),true));assert(handshake(tls));
 const std::string request="GET / HTTP/1.0\r\n\r\n";size_t sent=0;
 auto deadline=Clock::now()+std::chrono::seconds(5);
 while(sent<request.size() && Clock::now()<deadline){auto r=tls.write(reinterpret_cast<const uint8_t*>(request.data()+sent),request.size()-sent);assert(r.status==S::Progress || r.status==S::WantIo);sent+=r.count;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(sent==request.size());std::string response;uint8_t buf[1024];
 while(response.find("HTTP/1.0 200") == std::string::npos && Clock::now()<deadline){auto r=tls.read(buf,sizeof(buf));assert(r.status==S::Progress || r.status==S::WantIo);response.append(reinterpret_cast<char*>(buf),r.count);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 assert(response.find("HTTP/1.0 200")!=std::string::npos);tls.reset();
 std::cout<<"mbedTLS local verified handshake, rejection and encrypted HTTP tests passed\n";
}
