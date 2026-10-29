#include "../firmware/include/DeviceInput.h"
#include <cassert>
#include <cstring>
#include <iostream>
struct Store : CounterStorage {
  DurableCounter::Snapshot slots[2]{};bool present[2]{},fail=false;
  int read(unsigned i,void* p,size_t n) override {if(!present[i])return 0;memcpy(p,&slots[i],n);return 1;}
  bool write(unsigned i,const void* p,size_t n) override {if(fail)return false;memcpy(&slots[i],p,n);present[i]=true;return true;}
};
uint32_t snapshotCrc(const DurableCounter::Snapshot& s) {
 const auto* data=reinterpret_cast<const uint8_t*>(&s);uint32_t crc=0xffffffffu;
 for(size_t i=0;i<offsetof(DurableCounter::Snapshot,crc);++i){crc^=data[i];for(unsigned j=0;j<8;++j)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
 return ~crc;
}
struct Epochs : EpochSource { bool generate(char*) override {return false;} };
using S=DeviceInput::Status;using R=DurableCounter::Result;
const char* order="00000000-0000-4000-8000-000000000001";
const char* binding="20000000-0000-4000-8000-000000000001";
const char* epoch="10000000-0000-4000-8000-000000000001";
const char* barrier="30000000-0000-4000-8000-000000000001";
int main() {
 Store store;DurableCounter counter(store);assert(counter.load()==R::Ok);Epochs e;SyncPlanner planner(counter,e);DeviceInput input(counter,planner);
 auto sample=input.tick(true,0);assert(sample.status==S::Unbound && sample.accepted==0);
 sample=input.tick(true,25);assert(sample.status==S::Unbound && sample.rejected==1 && !planner.faulted());
 assert(counter.bind(order,1,binding,epoch)==R::Ok);
 sample=input.tick(true,525);assert(sample.status==S::ReleaseRequired && sample.rejected==1 && counter.pending()==0);
 input.tick(false,526);input.tick(false,550);sample=input.tick(true,551);assert(sample.status==S::ReleaseRequired);
 input.tick(false,552);sample=input.tick(false,577);assert(sample.status==S::Ready);
 input.tick(true,578);sample=input.tick(true,603);assert(sample.accepted==1 && sample.rejected==0 && counter.pending()==1);
 sample=input.tick(true,1103);assert(sample.accepted==1 && counter.pending()==2);
 sample=input.tick(true,1403);assert(sample.accepted==3 && counter.pending()==5);
 input.tick(false,1404);input.tick(false,1429);
 // Frozen accidental presses are persisted but cannot be reported as frozen.
 assert(counter.acknowledge(epoch,5)==R::Ok && counter.beginFreeze(barrier)==R::Ok);
 input.tick(true,1430);sample=input.tick(true,1455);
 assert(sample.accepted==1 && counter.pending()==1 && !counter.canReportFrozen());
 // Storage errors stop further acceptance and are surfaced on subsequent ticks.
 input.tick(false,1456);input.tick(false,1481);store.fail=true;
 input.tick(true,1482);sample=input.tick(true,1507);
 assert(sample.status==S::StorageError && sample.rejected==1 && sample.accepted==0 && counter.pending()==1);
 sample=input.tick(true,2007);assert(sample.status==S::StorageError && sample.rejected==1 && counter.pending()==1);
 // Durable data remains intact after the failed write and restart.
 DurableCounter reload(store);assert(reload.load()==R::Ok && reload.pending()==1 && reload.isFrozen());
 // Capacity exhaustion rejects the press and latches a visible full state.
 Store full;DurableCounter initial(full);assert(initial.load()==R::Ok && initial.bind(order,1,binding,epoch)==R::Ok);
 full.slots[0].total=DurableCounter::MAX_COUNT;full.slots[0].acknowledged=DurableCounter::MAX_COUNT;full.slots[0].crc=snapshotCrc(full.slots[0]);
 DurableCounter maximum(full);assert(maximum.load()==R::Ok);Epochs e4;SyncPlanner p4(maximum,e4);DeviceInput fullInput(maximum,p4);
 fullInput.tick(true,0);sample=fullInput.tick(true,25);assert(sample.status==S::CapacityFull && sample.rejected==1 && sample.accepted==0);
 assert(maximum.state().total==DurableCounter::MAX_COUNT && fullInput.tick(true,525).status==S::CapacityFull);
 // A press that starts before binding is rejected even if binding arrives
 // before the initial debounce period finishes.
 Store s3;DurableCounter c3(s3);assert(c3.load()==R::Ok);Epochs e3;SyncPlanner p3(c3,e3);DeviceInput early(c3,p3);
 early.tick(true,0);assert(c3.bind(order,1,binding,epoch)==R::Ok);
 sample=early.tick(true,25);assert(sample.status==S::ReleaseRequired && sample.rejected==1 && c3.pending()==0);
 // A button already held at boot must be released before counting.
 DeviceInput bootHeld(c3,p3,true,0);sample=bootHeld.tick(true,500);assert(sample.status==S::ReleaseRequired && c3.pending()==0);
 // The release gate works across the millis wrap boundary.
 Store s2;DurableCounter c2(s2);assert(c2.load()==R::Ok);Epochs e2;SyncPlanner p2(c2,e2);DeviceInput wrapped(c2,p2);
 wrapped.tick(true,UINT32_MAX-40);wrapped.tick(true,UINT32_MAX-15);
 assert(c2.bind(order,1,binding,epoch)==R::Ok);
 wrapped.tick(false,UINT32_MAX-5);sample=wrapped.tick(false,19);assert(sample.status==S::Ready);
 wrapped.tick(true,20);sample=wrapped.tick(true,45);assert(sample.accepted==1);
 std::cout<<"device input integration tests passed\n";
}
