#include "../firmware/include/ResolvedTcpConnect.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
struct Backend : AsyncDnsBackend {
 std::vector<Request*> requests;
 bool start(Request* request) override {requests.push_back(request);return true;}
 void finish(size_t index,bool ok=true){auto* r=requests[index];const uint8_t ip[4]={1,2,3,4};r->complete(r->context,ok?ip:nullptr);}
};
struct Ops : TcpOps {
 int opened=0,closed=0;uint8_t address[4]{};uint16_t port=0;Connect next=Connect::Pending,status=Connect::Pending;
 int openNonblocking() override {++opened;return 4;}
 Connect connect(int,const uint8_t ip[4],uint16_t p) override {memcpy(address,ip,4);port=p;return next;}
 Connect connectionStatus(int) override {return status;}
 int send(int,const uint8_t*,size_t) override {assert(false);return -1;}
 int receive(int,uint8_t*,size_t) override {assert(false);return -1;}
 void close(int) override {++closed;}
};
int main(){
 Backend backend;AsyncDns dns(backend);Ops ops;NonblockingTcp tcp(ops);ResolvedTcpConnect c(dns,tcp);
 using S=ResolvedTcpConnect::State;
 assert(!c.begin("evil.example",443,0,100));assert(!c.begin("abc.supabase.co",0,0,100));
 assert(c.begin("abc.supabase.co",443,UINT32_MAX-49,100));
 assert(!strcmp(c.hostname(),"abc.supabase.co") && c.poll(20)==S::Resolving);
 backend.finish(0);assert(c.poll(29)==S::Connecting); // 79 ms elapsed; TCP gets only remaining time
 assert(ops.address[0]==1 && ops.address[3]==4 && ops.port==443);
 assert(c.poll(49)==S::Connecting && c.poll(50)==S::Timeout && ops.closed==1);
 c.close();assert(!*c.hostname());
 assert(c.begin("abc.supabase.co",443,0,10));assert(c.poll(10)==S::Timeout);
 backend.finish(1); // late callback after expiry; does not affect next request
 c.close();assert(c.begin("abc.supabase.co",443,20,100));backend.finish(2,false);
 assert(c.poll(21)==S::Failed && ops.opened==1);c.close();
 assert(c.begin("abc.supabase.co",443,30,100));backend.finish(3);
 assert(c.poll(31)==S::Connecting);ops.status=TcpOps::Connect::Ready;
 assert(c.poll(32)==S::Connected);assert(c.poll(999)==S::Connected);c.close();assert(ops.closed==2);
 ops.next=TcpOps::Connect::Error;assert(c.begin("abc.supabase.co",443,0,100));backend.finish(4);
 assert(c.poll(1)==S::Failed && ops.closed==3);c.close();
 // Destruction/close abandons DNS rather than freeing a slot still owned by callback.
 assert(c.begin("abc.supabase.co",443,0,100));c.close();backend.finish(5);
 assert(c.begin("abc.supabase.co",443,0,100));c.close();backend.finish(6);
 // Exhausted late-callback slots fail closed, then become available after callbacks.
 for(int i=0;i<4;++i){assert(c.begin("abc.supabase.co",443,0,1));assert(c.poll(1)==S::Timeout);c.close();}
 assert(!c.begin("abc.supabase.co",443,0,1));
 for(size_t i=7;i<11;++i)backend.finish(i);
 assert(c.begin("abc.supabase.co",443,0,1));c.close();backend.finish(11);
 std::cout<<"resolved TCP setup tests passed (no TLS)\n";
}
