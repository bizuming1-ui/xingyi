#include "../firmware/include/SyncPlanner.h"
#include <cassert>
#include <cstring>
#include <iostream>

struct Store : CounterStorage {
  DurableCounter::Snapshot data[2]{}; bool present[2]{}; bool fail=false;
  int read(unsigned i,void* p,size_t n) override { if(!present[i])return 0;assert(n==sizeof(data[i]));memcpy(p,&data[i],n);return 1; }
  bool write(unsigned i,const void* p,size_t n) override { if(fail)return false;memcpy(&data[i],p,n);present[i]=true;return true; }
};
struct Epochs : EpochSource {
  unsigned n=0;
  bool generate(char out[37]) override { const char* values[]={"10000000-0000-4000-8000-000000000001","10000000-0000-4000-8000-000000000002"}; if(n>=2)return false;memcpy(out,values[n++],37);return true; }
};
using K=SyncPlanner::Kind; using B=SyncPlanner::BarrierState; using R=DurableCounter::Result;
const char* order1="00000000-0000-4000-8000-000000000001";
const char* order2="00000000-0000-4000-8000-000000000002";
const char* binding1="20000000-0000-4000-8000-000000000001";
const char* binding2="20000000-0000-4000-8000-000000000002";
const char* barrier="30000000-0000-4000-8000-000000000001";
const char* nonce="40000000-0000-4000-8000-000000000001";
void copy(char* out,const char* in){memcpy(out,in,37);}
SyncPlanner::Reply status(const char* order,const char* binding,uint64_t revision,B state=B::None) {
  SyncPlanner::Reply r{};r.success=true;copy(r.desiredOrder,order);copy(r.desiredBinding,binding);r.desiredRevision=revision;r.barrierState=state;if(state!=B::None)copy(r.barrier,barrier);return r;
}
SyncPlanner::Reply accepted(uint64_t total) {SyncPlanner::Reply r{};r.success=true;r.acceptedTotal=total;return r;}
SyncPlanner::Reply frozen(B state){SyncPlanner::Reply r{};r.success=true;r.barrierState=state;if(state==B::Ready)copy(r.nonce,nonce);return r;}
int main(){
 Store coldStore; DurableCounter unloaded(coldStore); Epochs coldEpochs; SyncPlanner beforeLoad(unloaded,coldEpochs);
 assert(beforeLoad.faulted() && beforeLoad.next(0).kind==K::None);
 Store store;DurableCounter c(store);assert(c.load()==R::Ok);Epochs epochs;SyncPlanner p(c,epochs);
 auto cmd=p.next(0);assert(cmd.kind==K::Poll);assert(p.complete(cmd.ticket,status(order1,binding1,1),0));
 assert(p.next(199).kind==K::None);
 cmd=p.next(200);assert(cmd.kind==K::Upload && cmd.total==0 && strcmp(cmd.binding,binding1)==0);
 // Press while request is in flight: ACK of the old total cannot clear it.
 assert(p.count(5)==R::Ok);assert(p.complete(cmd.ticket,accepted(0),250));assert(c.pending()==5);
 cmd=p.next(400);assert(cmd.kind==K::Upload && cmd.total==5);
 SyncPlanner::Reply lost{};assert(p.complete(cmd.ticket,lost,450));assert(c.pending()==5);
 cmd=p.next(600);assert(cmd.kind==K::Poll);assert(p.complete(cmd.ticket,status(order1,binding1,1),620));
 cmd=p.next(800);assert(cmd.kind==K::Upload && cmd.total==5);assert(p.complete(cmd.ticket,accepted(5),810));assert(c.pending()==0);
 // A newly requested order must not steal counts created before the change.
 assert(p.count(2)==R::Ok);
 cmd=p.next(1100);assert(cmd.kind==K::Poll);assert(p.complete(cmd.ticket,status(order2,binding2,2),1110));
 cmd=p.next(1300);assert(cmd.kind==K::Upload && cmd.total==7 && strcmp(cmd.binding,binding1)==0);
 assert(p.complete(cmd.ticket,accepted(7),1310));
 cmd=p.next(1500);assert(cmd.kind==K::Upload && cmd.total==0 && strcmp(cmd.binding,binding2)==0);
 assert(p.complete(cmd.ticket,accepted(0),1510));assert(c.state().bindingRevision==2);
 // Freeze only after the current epoch is registered and all counts are sent.
 cmd=p.next(1700);assert(cmd.kind==K::Poll);assert(p.complete(cmd.ticket,status(order2,binding2,2,B::Requested),1710));
 cmd=p.next(1900);assert(cmd.kind==K::Freeze && cmd.total==0 && c.isFrozen());
 // An accidental press while freeze is in flight remains stored, and forces abort.
 assert(p.count(3)==R::Ok);assert(p.complete(cmd.ticket,frozen(B::Ready),1910));
 cmd=p.next(2100);assert(cmd.kind==K::Abort);assert(p.complete(cmd.ticket,lost,2110));assert(c.isFrozen() && c.pending()==3);
 // Reboot with dirty freeze: only abort, never a new upload or renewal.
 DurableCounter restored(store);assert(restored.load()==R::Ok);SyncPlanner restarted(restored,epochs);
 cmd=restarted.next(0);assert(cmd.kind==K::Abort);
 assert(restarted.complete(cmd.ticket,frozen(B::Completed),10)); // server won the race
 assert(!restored.isFrozen() && restored.pending()==3);
 cmd=restarted.next(200);assert(cmd.kind==K::Poll);assert(restarted.complete(cmd.ticket,status(order2,binding2,2),210));
 cmd=restarted.next(400);assert(cmd.kind==K::Upload && cmd.total==3);assert(restarted.complete(cmd.ticket,accepted(3),410));
 // A new clean freeze survives an ambiguous response and is recovered by status.
 cmd=restarted.next(700);assert(cmd.kind==K::Poll);assert(restarted.complete(cmd.ticket,status(order2,binding2,2,B::Requested),710));
 cmd=restarted.next(900);assert(cmd.kind==K::Freeze && cmd.total==3);assert(restarted.complete(cmd.ticket,lost,910));
 cmd=restarted.next(1100);assert(cmd.kind==K::BarrierStatus);assert(restarted.complete(cmd.ticket,frozen(B::Ready),1110));
 cmd=restarted.next(1800);assert(cmd.kind==K::None);
 cmd=restarted.next(1900);assert(cmd.kind==K::Renew);assert(strcmp(cmd.nonce,nonce)==0);
 assert(!restarted.complete(cmd.ticket+1,frozen(B::Expired),1910));assert(restarted.hasInFlight());
 assert(restarted.complete(cmd.ticket,frozen(B::Expired),1920));assert(!restored.isFrozen());
 // Storage failure is fail-closed rather than reporting a successful press.
 store.fail=true;assert(restarted.count(1)==R::StorageError);assert(restarted.faulted());assert(restarted.next(3000).kind==K::None);

 // Fail closed if server claims an impossible acknowledgement.
 Store store2; DurableCounter c2(store2); assert(c2.load()==R::Ok); Epochs epochs2; SyncPlanner invalid(c2,epochs2);
 auto step=invalid.next(0); assert(invalid.complete(step.ticket,status(order1,binding1,1),0));
 step=invalid.next(200); assert(step.kind==K::Upload && step.total==0);
 assert(invalid.complete(step.ticket,accepted(1),210)); assert(invalid.faulted());
 assert(c2.pending()==0 && invalid.next(1000).kind==K::None);
 // A stale binding revision never replaces a newer locally persisted binding.
 Store store3; DurableCounter c3(store3); assert(c3.load()==R::Ok);
 assert(c3.bind(order2,2,binding2,"10000000-0000-4000-8000-000000000002")==R::Ok);
 Epochs epochs3; SyncPlanner stale(c3,epochs3); step=stale.next(0);
 assert(stale.complete(step.ticket,status(order1,binding1,1),0));
 step=stale.next(200); assert(step.kind==K::Upload && step.total==0);
 assert(stale.complete(step.ticket,accepted(0),210));
 assert(stale.next(400).kind==K::None && stale.faulted());
 assert(strcmp(c3.state().bindingId,binding2)==0);
 // Request throttling remains correct across Arduino millis rollover.
 Store store4; DurableCounter c4(store4); assert(c4.load()==R::Ok); Epochs epochs4; SyncPlanner rollover(c4,epochs4);
 step=rollover.next(UINT32_MAX-50); assert(step.kind==K::Poll);
 assert(rollover.complete(step.ticket,status(order1,binding1,1),UINT32_MAX-50));
 assert(rollover.next(148).kind==K::None);
 assert(rollover.next(149).kind==K::Upload);
 std::cout<<"synchronization planner tests passed\n";
}
