// Test-only: actual firmware components over local TLS, simulated GPIO/NVS,
// Wi-Fi and clock. Only ephemeral fixture credentials and test CA are used.
#include "../firmware/include/DeviceInput.h"
#include "../firmware/include/AuthenticatedNetworkTime.h"
#include "../firmware/include/EspTcpOps.h"
#include <ArduinoJson.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
struct Store : CounterStorage {
 DurableCounter::Snapshot slots[2]{};bool present[2]{};
 int read(unsigned i,void* p,size_t n) override{if(!present[i])return 0;memcpy(p,&slots[i],n);return 1;}
 bool write(unsigned i,const void* p,size_t n) override{memcpy(&slots[i],p,n);present[i]=true;return true;}
};
struct Epochs : EpochSource {unsigned n=0;bool generate(char out[37]) override{
 const char* ids[]={"10000000-0000-4000-8000-000000000001","10000000-0000-4000-8000-000000000002"};
 if(n>=2)return false;
 memcpy(out,ids[n++],37);return true;}};
struct Mailbox : SyncMailbox {
 SyncPlanner::Command cmd{};Completion done{};bool haveCmd=false,haveDone=false;
 bool submit(const SyncPlanner::Command& c) override{if(haveCmd)return false;cmd=c;haveCmd=true;return true;}
 bool take(SyncPlanner::Command& c) override{if(!haveCmd)return false;c=cmd;haveCmd=false;return true;}
 bool publish(const Completion& c) override{if(haveDone)return false;done=c;haveDone=true;return true;}
 bool collect(Completion& c) override{if(!haveDone)return false;c=done;haveDone=false;return true;}
};
struct Clock : ConnectionClock,MonotonicMillis64,NetworkTaskClock {
 std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();uint64_t offset=10000;
 bool read(uint64_t& ms) override{ms=offset+static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());return true;}
 uint32_t nowMillis() override{uint64_t ms=0;read(ms);return static_cast<uint32_t>(ms);}
};
struct Yield : NetworkYield {void yield() override{std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
struct Entropy : SecureEntropy {uint8_t seq=0;bool fill(uint8_t* b,size_t n) override{++seq;for(size_t i=0;i<n;++i)b[i]=static_cast<uint8_t>(seq+i);return true;}};
struct Dns : AsyncDnsBackend {bool start(Request* r) override{const uint8_t ip[]={127,0,0,1};r->complete(r->context,ip);return true;}};
struct Lock : RadioLifecycleLock {bool held=false;bool tryLock() override{if(held)return false;held=true;return true;}void unlock() override{held=false;}};
struct Station : WifiStation {bool online=true;bool start(const char*,const char*) override{return true;}bool connected() override{return online;}void disconnect() override{}};
struct MappedOps : TcpOps {
 EspTcpOps ops;uint16_t port;explicit MappedOps(uint16_t p):port(p){}
 int openNonblocking() override{return ops.openNonblocking();}
 Connect connect(int f,const uint8_t ip[4],uint16_t p) override{if(p!=443)return Connect::Error;return ops.connect(f,ip,port);}
 Connect connectionStatus(int f) override{return ops.connectionStatus(f);}
 int send(int f,const uint8_t* p,size_t n) override{return ops.send(f,p,n);}
 int receive(int f,uint8_t* p,size_t n) override{return ops.receive(f,p,n);}
 void close(int f) override{ops.close(f);}
};
int main(){
 std::string line;if(!std::getline(std::cin,line))return 1;
 StaticJsonDocument<1024> init;if(deserializeJson(init,line))return 2;
 const char* provided=init["secret"]|"";const char* id=init["device_id"]|"";const char* caPath=init["ca"]|"";const int port=init["port"]|0;
 if(strlen(provided)!=64 || strlen(id)!=36 || port<1 || port>65535)return 3;
 char secret[65]{},device[37]{};memcpy(secret,provided,65);memcpy(device,id,37);
 std::ifstream f(caPath);if(!f)return 4;const std::string ca((std::istreambuf_iterator<char>(f)),{});
 Store store;DurableCounter counter(store);if(counter.load()!=DurableCounter::Result::Ok)return 5;
 Epochs epochs;SyncPlanner planner(counter,epochs);Mailbox mailbox;SyncController controller(planner,mailbox);DeviceInput input(counter,planner);
 Clock clock;Yield scheduler;Entropy entropy;TimeProof proof(entropy);Dns backend;AsyncDns dns(backend);
 MappedOps ops(static_cast<uint16_t>(port));NonblockingTcp tcp(ops);ResolvedTcpConnect resolved(dns,tcp);
 TimeBootstrapExchange bootstrap(clock,scheduler,tcp,resolved,proof);TrustedTimeWindow window(clock,60000,1000);
 TimeWindowSynchronizer sync(clock,window,bootstrap);MbedTlsSession tls(tcp);VerifiedConnect verified(clock,tcp,resolved,tls);
 MbedTlsChannel channel(clock,scheduler,window,verified,tls,ca.c_str());HttpRpcExchange exchange(channel,"project123.supabase.co","sb_publishable_test",3000);
 SyncNetworkWorker worker(mailbox,exchange,secret);Lock lock;Station station;WifiSupervisor supervisor(station,"network","password");GuardedWifiSupervisor wifi(lock,supervisor);
 AuthenticatedNetworkTime time(window,sync,"project123.supabase.co","sb_publishable_test",ca.c_str(),device,secret,3000);
 NetworkTaskCoordinator network(clock,wifi,time,worker);
 network.step();network.step(); // fixture Wi-Fi and signed-time setup
 while(std::getline(std::cin,line)) {
  StaticJsonDocument<512> cmd;if(deserializeJson(cmd,line))return 6;
  const char* action=cmd["action"]|"";uint32_t now=cmd["now"]|0;DeviceInput::Sample sample{};
  if(strcmp(action,"step")==0)controller.step(now);
  else if(strcmp(action,"work")==0)network.step();
  else if(strcmp(action,"offline")==0){station.online=false;network.step();}
  else if(strcmp(action,"online")==0){station.online=true;clock.offset+=5000;network.step();network.step();}
  else if(strcmp(action,"press")==0)sample=input.tick(cmd["pressed"]|false,now);
  else if(strcmp(action,"quit")==0)break;
  else return 7;
  StaticJsonDocument<512> report;report["type"]="result";report["ready"]=counter.isReady();report["order"]=counter.isReady()?counter.state().orderId:"";
  report["total"]=counter.state().total;report["pending"]=counter.pending();report["frozen"]=counter.isFrozen();report["fault"]=controller.faulted();
  report["accepted"]=sample.accepted;report["rejected"]=sample.rejected;report["queued"]=mailbox.haveCmd;
  serializeJson(report,std::cout);std::cout<<std::endl;
 }
 RpcCodec::wipe(secret,sizeof(secret));return 0;
}
