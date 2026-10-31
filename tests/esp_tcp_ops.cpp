#include "../firmware/include/EspTcpOps.h"
#include <cassert>
#include <csignal>
#include <cstring>
#include <iostream>
int main(){
 std::signal(SIGPIPE,SIG_IGN);
 int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
 sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=0;
 assert(bind(listener,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==0 && listen(listener,1)==0);
 socklen_t n=sizeof(addr);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&addr),&n)==0);
 EspTcpOps ops;NonblockingTcp tcp(ops);const uint8_t loopback[]={127,0,0,1};
 assert(tcp.start(loopback,ntohs(addr.sin_port),0,1000));
 for(unsigned i=0;i<1000 && tcp.state()==NonblockingTcp::State::Connecting;++i)tcp.poll(i/10);
 assert(tcp.state()==NonblockingTcp::State::Connected);
 int peer=accept(listener,nullptr,nullptr);assert(peer>=0);
 uint8_t data[8]{};assert(tcp.read(data,sizeof(data)).status==NonblockingTcp::IoStatus::WouldBlock);
 const char sample[]="abc";assert(send(peer,sample,3,0)==3);
 NonblockingTcp::IoResult received;
 for(unsigned i=0;i<1000;++i){received=tcp.read(data,sizeof(data));if(received.status==NonblockingTcp::IoStatus::Progress)break;}
 assert(received.status==NonblockingTcp::IoStatus::Progress && received.count==3 && memcmp(data,sample,3)==0);
 auto sent=tcp.write(reinterpret_cast<const uint8_t*>(sample),3);assert(sent.status==NonblockingTcp::IoStatus::Progress && sent.count==3);
 char other[4]{};assert(recv(peer,other,3,0)==3 && memcmp(other,sample,3)==0);
 tcp.close();close(peer);close(listener);std::cout<<"ESP TCP adapter loopback tests passed (plaintext fixture only)\n";
}
