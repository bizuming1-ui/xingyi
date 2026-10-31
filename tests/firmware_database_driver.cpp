// Test-only process: real firmware state machines, simulated storage, transport
// and clock. It never connects to a network and uses only fixture credentials.
#include "../firmware/include/DeviceInput.h"
#include "../firmware/include/SyncBridge.h"
#include <ArduinoJson.h>
#include <cstring>
#include <iostream>
#include <string>
struct Store : CounterStorage {
  DurableCounter::Snapshot slots[2]{};bool present[2]{};
  int read(unsigned i,void* p,size_t n) override {if(!present[i])return 0;memcpy(p,&slots[i],n);return 1;}
  bool write(unsigned i,const void* p,size_t n) override {memcpy(&slots[i],p,n);present[i]=true;return true;}
};
struct Epochs : EpochSource {
  unsigned n=0;
  bool generate(char out[37]) override {
    const char* ids[]={"10000000-0000-4000-8000-000000000001","10000000-0000-4000-8000-000000000002"};
    if(n>=2)return false;
    memcpy(out,ids[n++],37);return true;
  }
};
struct Mailbox : SyncMailbox {
  SyncPlanner::Command cmd{};Completion done{};bool haveCmd=false,haveDone=false;
  bool submit(const SyncPlanner::Command& c) override {if(haveCmd)return false;cmd=c;haveCmd=true;return true;}
  bool take(SyncPlanner::Command& c) override {if(!haveCmd)return false;c=cmd;haveCmd=false;return true;}
  bool publish(const Completion& c) override {if(haveDone)return false;done=c;haveDone=true;return true;}
  bool collect(Completion& c) override {if(!haveDone)return false;c=done;haveDone=false;return true;}
};
struct Exchange : RpcExchange {
  Response post(const char* endpoint,const char* body,size_t len,char* out,size_t capacity) override {
    StaticJsonDocument<2048> msg;msg["type"]="rpc";msg["endpoint"]=endpoint;
    msg["body"]=serialized(body,len);
    serializeJson(msg,std::cout);std::cout<<std::endl;
    std::string line;if(!std::getline(std::cin,line))return {};
    StaticJsonDocument<3072> reply;if(deserializeJson(reply,line))return {};
    Response result{};
    if(reply["timeout"]==true){result.status=Status::Timeout;return result;}
    result.httpStatus=reply["http"]|500;
    const char* content=reply["body"]|"";result.length=strlen(content);
    if(result.length>capacity){result.status=Status::ResponseTooLarge;return result;}
    memcpy(out,content,result.length);result.status=Status::Ok;return result;
  }
};
int main() {
  std::string line;if(!std::getline(std::cin,line))return 1;
  StaticJsonDocument<512> init;if(deserializeJson(init,line))return 2;
  const char* provided=init["secret"]|"";if(strlen(provided)!=64)return 3;
  char secret[65]{};memcpy(secret,provided,65);
  Store store;DurableCounter counter(store);if(counter.load()!=DurableCounter::Result::Ok)return 4;
  Epochs epochs;SyncPlanner planner(counter,epochs);Mailbox mailbox;Exchange exchange;
  SyncController controller(planner,mailbox);SyncNetworkWorker worker(mailbox,exchange,secret);DeviceInput input(counter,planner);
  while(std::getline(std::cin,line)) {
    StaticJsonDocument<512> cmd;if(deserializeJson(cmd,line))return 5;
    const char* action=cmd["action"]|"";uint32_t now=cmd["now"]|0;DeviceInput::Sample sample{};
    if(strcmp(action,"step")==0)controller.step(now);
    else if(strcmp(action,"work")==0)worker.step();
    else if(strcmp(action,"press")==0)sample=input.tick(cmd["pressed"]|false,now);
    else if(strcmp(action,"quit")==0)break;
    else return 6;
    StaticJsonDocument<512> report;report["type"]="result";report["ready"]=counter.isReady();
    report["order"]=counter.isReady()?counter.state().orderId:"";report["total"]=counter.state().total;
    report["pending"]=counter.pending();report["frozen"]=counter.isFrozen();report["fault"]=controller.faulted();
    report["accepted"]=sample.accepted;report["rejected"]=sample.rejected;report["queued"]=mailbox.haveCmd;
    serializeJson(report,std::cout);std::cout<<std::endl;
  }
  RpcCodec::wipe(secret,sizeof(secret));return 0;
}
