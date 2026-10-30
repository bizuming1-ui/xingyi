#include "../firmware/include/SyncBridge.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

struct Store : CounterStorage {
  DurableCounter::Snapshot data[2]{}; bool present[2]{};
  int read(unsigned i,void* p,size_t n) override {if(!present[i]) return 0; memcpy(p,&data[i],n);return 1;}
  bool write(unsigned i,const void* p,size_t n) override {memcpy(&data[i],p,n);present[i]=true;return true;}
};
struct Epochs : EpochSource {
  bool generate(char out[37]) override {memcpy(out,"10000000-0000-4000-8000-000000000001",37);return true;}
};
struct Mailbox : SyncMailbox {
  SyncPlanner::Command command{}; Completion result{};
  bool hasCommand=false,hasResult=false, blockSubmit=false,blockPublish=false;
  bool submit(const SyncPlanner::Command& v) override {if(hasCommand||blockSubmit)return false;command=v;hasCommand=true;return true;}
  bool collect(Completion& v) override {if(!hasResult)return false;v=result;hasResult=false;return true;}
  bool take(SyncPlanner::Command& v) override {if(!hasCommand)return false;v=command;hasCommand=false;return true;}
  bool publish(const Completion& v) override {if(hasResult||blockPublish)return false;result=v;hasResult=true;return true;}
};
const char* secret="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
struct Exchange : RpcExchange {
  int calls=0; bool fail=false;
  Response post(const char* endpoint,const char*,size_t,char* out,size_t capacity) override {
    ++calls; Response r{}; if(fail){r.status=Status::Timeout;return r;}
    std::string body;
    if(strcmp(endpoint,"device_status")==0)
      body="{\"desired_order_id\":\"00000000-0000-4000-8000-000000000001\",\"desired_binding_id\":\"20000000-0000-4000-8000-000000000001\",\"desired_binding_revision\":1}";
    else {assert(strcmp(endpoint,"device_ingest")==0);body="{\"binding_id\":\"20000000-0000-4000-8000-000000000001\",\"accepted_total\":0}";}
    assert(body.size()<=capacity);memcpy(out,body.data(),body.size());r.status=Status::Ok;r.httpStatus=200;r.length=body.size();return r;
  }
};
struct Observer : RpcObserver {
 int begins=0,ends=0,offline=0;SyncPlanner::Kind kind=SyncPlanner::Kind::None;uint32_t ticket=0;uint64_t total=0;RpcWorker::Result result=RpcWorker::Result::InvalidRequest;
 void begin(SyncPlanner::Kind k,uint32_t t,uint64_t v) override {++begins;kind=k;ticket=t;total=v;}
 void end(const RpcWorker::Outcome& o,SyncPlanner::BarrierState,bool attempted) override {++ends;result=o.result;if(!attempted)++offline;}
};
int main() {
  Store s;DurableCounter c(s);assert(c.load()==DurableCounter::Result::Ok);Epochs e;SyncPlanner p(c,e);
  Mailbox m;Exchange x;Observer obs;SyncController controller(p,m);SyncNetworkWorker worker(m,x,secret,&obs);
  controller.step(0);assert(m.hasCommand && p.hasInFlight());
  controller.step(100);assert(x.calls==0 && m.hasCommand); // network never runs on controller
  worker.step();assert(x.calls==1 && m.hasResult);assert(obs.begins==1 && obs.ends==1 && obs.kind==SyncPlanner::Kind::Poll && obs.result==RpcWorker::Result::Ok);controller.step(100);
  controller.step(200);assert(m.hasCommand && m.command.kind==SyncPlanner::Kind::Upload);
  assert(p.count(3)==DurableCounter::Result::Ok); // count while upload is in flight
  m.blockPublish=true;worker.step();assert(x.calls==2 && !m.hasResult);
  worker.step();assert(x.calls==2 && obs.begins==2 && obs.ends==2); // full result queue must not repeat the RPC
  m.blockPublish=false;worker.step();assert(m.hasResult && x.calls==2);
  controller.step(250);assert(c.pending()==3 && !controller.faulted());
  controller.step(400);assert(m.hasCommand && m.command.total==3);
  x.fail=true;worker.step();assert(obs.kind==SyncPlanner::Kind::Upload && obs.total==3 && obs.result==RpcWorker::Result::TransportError);controller.step(410);assert(c.pending()==3 && !p.hasInFlight());
  // Unexpected response cannot complete another ticket.
  m.result.ticket=123456;m.result.reply.success=true;m.hasResult=true;
  controller.step(500);assert(controller.faulted() && c.pending()==3);
  Store s2;DurableCounter c2(s2);assert(c2.load()==DurableCounter::Result::Ok);Epochs e2;SyncPlanner p2(c2,e2);
  Mailbox m2;m2.blockSubmit=true;SyncController controller2(p2,m2);
  controller2.step(0);assert(controller2.faulted() && !p2.hasInFlight());
  controller2.step(1000);assert(!m2.hasCommand);
  Mailbox m3;Exchange x3;Observer o3;SyncNetworkWorker offline(m3,x3,secret,&o3);
  SyncPlanner::Command sent{};sent.kind=SyncPlanner::Kind::Upload;sent.ticket=12;sent.total=9;assert(m3.submit(sent));
  offline.failUnavailable();assert(o3.begins==1 && o3.ends==1 && o3.offline==1 && o3.total==9 && x3.calls==0 && m3.hasResult && !m3.result.reply.success);
  std::cout<<"sync bridge tests passed\n";
}
