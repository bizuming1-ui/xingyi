#include "../firmware/include/DurableCounter.h"
#include <cassert>
#include <cstring>
#include <iostream>
struct Store : CounterStorage {
  DurableCounter::Snapshot data[2]{}; bool present[2]{};
  enum class Mode {Normal, FailBeforeWrite, CommitThenError} mode=Mode::Normal;
  int read(unsigned i,void* dest,size_t size) override {if(!present[i])return 0; assert(size==sizeof(data[i])); memcpy(dest,&data[i],size); return 1;}
  bool write(unsigned i,const void* src,size_t size) override {if(mode==Mode::FailBeforeWrite)return false; memcpy(&data[i],src,size);present[i]=true;return mode!=Mode::CommitThenError;}
};
int main(){
  using R=DurableCounter::Result; using O=DurableCounter::TerminalOutcome;
  const char* order="00000000-0000-4000-8000-000000000001";
  const char* other="00000000-0000-4000-8000-000000000002";
  const char* binding="20000000-0000-4000-8000-000000000001";
  const char* binding2="20000000-0000-4000-8000-000000000002";
  const char* epoch="10000000-0000-4000-8000-000000000001";
  const char* epoch2="10000000-0000-4000-8000-000000000002";
  const char* barrier="30000000-0000-4000-8000-000000000001";
  const char* wrong="30000000-0000-4000-8000-000000000002";
  Store store; DurableCounter c(store);
  assert(c.load()==R::Ok); assert(c.bind(order,1,binding,epoch)==R::Ok);
  assert(c.canUpload()); assert(c.add(10)==R::Ok); assert(c.beginFreeze(barrier)==R::Pending);
  assert(c.acknowledge(epoch,10)==R::Ok); assert(c.beginFreeze(barrier)==R::Ok);
  assert(c.beginFreeze(barrier)==R::Ok); assert(!c.canUpload()); assert(c.canReportFrozen()); assert(c.uploadableTotal()==10);
  assert(c.beginFreeze(wrong)==R::Frozen); assert(c.bind(other,2,binding2,epoch2)==R::Frozen);
  DurableCounter afterFreeze(store); assert(afterFreeze.load()==R::Ok && afterFreeze.isFrozen() && afterFreeze.canReportFrozen());
  // An accidental press during the barrier is durable but is not uploadable.
  assert(afterFreeze.add(3)==R::Ok); assert(!afterFreeze.canReportFrozen());
  assert(afterFreeze.pending()==3 && afterFreeze.uploadableTotal()==10);
  assert(afterFreeze.acknowledge(epoch,13)==R::Frozen);
  DurableCounter reboot(store); assert(reboot.load()==R::Ok && reboot.isFrozen());
  assert(reboot.pending()==3 && reboot.uploadableTotal()==10 && !reboot.canReportFrozen());
  assert(reboot.finishFreeze(wrong,O::Cancelled)==R::Invalid);
  assert(reboot.finishFreeze(barrier,static_cast<O>(99))==R::Invalid);
  // Local error/timeout alone does not clear freeze; only a confirmed terminal result may do so.
  assert(reboot.finishFreeze(barrier,O::Completed)==R::Ok && !reboot.isFrozen());
  assert(reboot.pending()==3 && reboot.uploadableTotal()==13);
  assert(reboot.bind(other,2,binding2,epoch2)==R::Pending);
  assert(reboot.acknowledge(epoch,13)==R::Ok);
  assert(reboot.bind(other,1,binding2,epoch2)==R::Invalid);
  assert(reboot.bind(other,2,binding2,epoch2)==R::Ok);
  assert(reboot.beginFreeze(barrier)==R::Ok);
  store.mode=Store::Mode::CommitThenError;
  assert(reboot.add(1)==R::StorageError); assert(!reboot.canUpload() && !reboot.isReady()); assert(reboot.add(1)==R::NotReady);
  store.mode=Store::Mode::Normal;
  DurableCounter uncertain(store); assert(uncertain.load()==R::Ok);
  assert(uncertain.isFrozen() && uncertain.pending()==1 && uncertain.uploadableTotal()==0);
  assert(uncertain.finishFreeze(barrier,O::Expired)==R::Ok && uncertain.pending()==1);
  std::cout<<"freeze persistence tests passed\n";
}
