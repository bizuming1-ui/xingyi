#include "../firmware/include/NetworkTaskCoordinator.h"
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
struct Store : CounterStorage {
 DurableCounter::Snapshot data[2]{};bool present[2]{};
 int read(unsigned i,void* p,size_t n) override{if(!present[i])return 0;memcpy(p,&data[i],n);return 1;}
 bool write(unsigned i,const void* p,size_t n) override{memcpy(&data[i],p,n);present[i]=true;return true;}
};
struct Epochs : EpochSource {bool generate(char out[37]) override {memcpy(out,"10000000-0000-4000-8000-000000000001",37);return true;}};
struct Mailbox : SyncMailbox {
 SyncPlanner::Command command{};Completion result{};bool hasCommand=false,hasResult=false,blockPublish=false;
 bool submit(const SyncPlanner::Command& v) override{if(hasCommand)return false;command=v;hasCommand=true;return true;}
 bool take(SyncPlanner::Command& v) override{if(!hasCommand)return false;v=command;hasCommand=false;return true;}
 bool collect(Completion& v) override{if(!hasResult)return false;v=result;hasResult=false;return true;}
 bool publish(const Completion& v) override{if(hasResult||blockPublish)return false;result=v;hasResult=true;return true;}
};
struct Exchange : RpcExchange {
 int calls=0;std::atomic<int> closes{0};
 void close() override {++closes;}
 Response post(const char* endpoint,const char*,size_t,char* out,size_t cap) override {
  ++calls;
  const std::string body=strcmp(endpoint,"device_ingest")==0 ? "{\"binding_id\":\"20000000-0000-4000-8000-000000000001\",\"accepted_total\":7}" :"{\"desired_order_id\":\"00000000-0000-4000-8000-000000000001\",\"desired_binding_id\":\"20000000-0000-4000-8000-000000000001\",\"desired_binding_revision\":1}";
  assert(body.size()<=cap);memcpy(out,body.data(),body.size());Response r{};r.status=Status::Ok;r.httpStatus=200;r.length=body.size();return r;
 }
};
struct Lock : RadioLifecycleLock {bool busy=false;bool tryLock() override{if(busy)return false;busy=true;return true;}void unlock() override{busy=false;}};
struct Station : WifiStation {bool online=false;int starts=0;bool start(const char*,const char*) override{++starts;return true;}bool connected() override{return online;}void disconnect() override{}};
struct Clock : NetworkTaskClock {std::atomic<uint32_t> now{0};uint32_t nowMillis() override{return now.load();}};
struct Time : NetworkTimeService {
 std::atomic<bool> valid{false},block{false},entered{false},release{false};int refreshes=0,invalidations=0,preserves=0;bool success=false,due=false;
 bool trusted() override{return valid.load();}
 bool refresh() override{++refreshes;if(block.load()){entered=true;while(!release.load())std::this_thread::yield();}valid=success;return success;}
 bool refreshDue() override{return due;}
 bool refreshPreserving() override{++preserves;return success;}
 void invalidate() override{valid=false;++invalidations;}
};
int main(){
 Store store;DurableCounter counter(store);assert(counter.load()==DurableCounter::Result::Ok);
 assert(counter.bind("00000000-0000-4000-8000-000000000001",1,"20000000-0000-4000-8000-000000000001","10000000-0000-4000-8000-000000000001")==DurableCounter::Result::Ok);
 Epochs epochs;SyncPlanner planner(counter,epochs);assert(planner.count(5)==DurableCounter::Result::Ok);
 Mailbox mailbox;Exchange exchange;SyncController counting(planner,mailbox);
 SyncNetworkWorker worker(mailbox,exchange,"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
 Lock lock;Station station;WifiSupervisor supervisor(station,"network","password");GuardedWifiSupervisor wifi(lock,supervisor);Clock clock;Time time;
 NetworkTaskCoordinator network(clock,wifi,time,worker);
 counting.step(0);assert(mailbox.hasCommand);network.step();assert(exchange.closes==1 && mailbox.hasResult && !mailbox.result.reply.success && exchange.calls==0);
 counting.step(1);assert(counter.pending()==5 && !planner.faulted());
 counting.step(200);clock.now=200;station.online=true;network.step();assert(exchange.closes==2 && time.refreshes==1 && mailbox.hasResult && exchange.calls==0);
 counting.step(201);counting.step(400);clock.now=400;network.step();assert(time.refreshes==1 && mailbox.hasResult);counting.step(401);
 // A time refresh blocks only the network task, not the counter task.
 counting.step(5200);assert(mailbox.hasCommand);clock.now=5200;time.success=true;time.block=true;
 std::thread networkThread([&]{network.step();});
 while(!time.entered.load())std::this_thread::yield();
 assert(exchange.closes>=3); // Existing RPC socket must be closed before bootstrap owns the TCP transport.
 assert(planner.count(2)==DurableCounter::Result::Ok && counter.pending()==7);
 time.release=true;networkThread.join();assert(time.refreshes==2 && exchange.calls==1 && mailbox.hasResult);
 counting.step(5210);assert(counter.pending()==7 && !planner.faulted());
 // A successful response blocked by a full result queue remains the exact
 // response, even if Wi-Fi drops; it is not re-executed or replaced by failure.
 counting.step(5700);assert(mailbox.hasCommand && mailbox.command.kind==SyncPlanner::Kind::Upload);
 mailbox.blockPublish=true;clock.now=5700;network.step();assert(exchange.calls==2 && !mailbox.hasResult);
 mailbox.blockPublish=false;station.online=false;clock.now=5701;const int beforeOffline=exchange.closes;network.step();
 assert(exchange.closes==beforeOffline+1);
 assert(mailbox.hasResult && mailbox.result.reply.success && exchange.calls==2 && !time.trusted());
 counting.step(5702);assert(counter.pending()==0 && !planner.faulted());
 // Busy radio gate makes no Wi-Fi state transition, and no queued RPC is run.
 clock.now=6000;counting.step(6000);assert(mailbox.hasCommand);lock.busy=true;network.step();
 assert(mailbox.hasCommand && exchange.calls==2);lock.busy=false;network.step();
 assert(mailbox.hasResult && !mailbox.result.reply.success && counter.pending()==0);
 // Time retry backoff remains correct across the 32-bit clock wrap.
 Lock lock2;Station station2;station2.online=true;WifiSupervisor supervisor2(station2,"network","password");
 GuardedWifiSupervisor wifi2(lock2,supervisor2);Clock clock2;Time time2;Mailbox mailbox2;Exchange exchange2;
 SyncNetworkWorker worker2(mailbox2,exchange2,"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
 NetworkTaskCoordinator wrap(clock2,wifi2,time2,worker2);clock2.now=UINT32_MAX-100;wrap.step();
 clock2.now=UINT32_MAX-99;wrap.step();assert(time2.refreshes==1);
 clock2.now=4899;wrap.step();assert(time2.refreshes==1);
 clock2.now=4900;wrap.step();assert(time2.refreshes==2);
 // Proactive failure retains valid time and allows RPC work; retries are
 // rate-limited from completion. A pending barrier suppresses proactive work.
 Lock lock3;Station station3;station3.online=true;WifiSupervisor supervisor3(station3,"network","password");
 GuardedWifiSupervisor wifi3(lock3,supervisor3);Clock clock3;Time time3;Mailbox mailbox3;Exchange exchange3;
 SyncNetworkWorker worker3(mailbox3,exchange3,"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
 NetworkTaskCoordinator proactive(clock3,wifi3,time3,worker3);time3.valid=true;time3.due=true;
 proactive.step();time3.valid=true;clock3.now=1;proactive.step();assert(time3.preserves==1 && time3.trusted());
 clock3.now=4999;proactive.step();assert(time3.preserves==1);
 clock3.now=5001;proactive.step();assert(time3.preserves==2);
 SyncPlanner::Command freeze{};freeze.kind=SyncPlanner::Kind::Freeze;assert(mailbox3.submit(freeze));
 worker3.failUnavailable();assert(worker3.barrierActive());
 clock3.now=11000;proactive.step();assert(time3.preserves==2 && time3.trusted());
 // Loss of trust must bootstrap even when a barrier is being resolved.
 time3.valid=false;clock3.now=16000;proactive.step();assert(time3.refreshes==1);
 std::cout<<"network task coordination and offline count tests passed\n";
}
