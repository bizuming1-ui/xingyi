#include "../firmware/include/DurableCounter.h"
#include <cassert>
#include <cstring>
#include <iostream>

struct MemoryStore : CounterStorage {
  DurableCounter::Snapshot slots[2]{};
  bool present[2]{};
  bool failWrite=false;
  int read(unsigned slot, void* out, size_t n) override {
    if (!present[slot]) return 0;
    assert(n==sizeof(slots[slot])); memcpy(out,&slots[slot],n); return 1;
  }
  bool write(unsigned slot, const void* in, size_t n) override {
    if (failWrite) return false;
    assert(n==sizeof(slots[slot])); memcpy(&slots[slot],in,n); present[slot]=true; return true;
  }
};
int main() {
  constexpr auto order="00000000-0000-4000-8000-000000000001";
  constexpr auto nextOrder="00000000-0000-4000-8000-000000000002";
  constexpr auto epoch="10000000-0000-4000-8000-000000000001";
  constexpr auto epoch2="10000000-0000-4000-8000-000000000002";
  using R=DurableCounter::Result;
  MemoryStore store; DurableCounter q(store);
  assert(q.add(1)==R::NotReady);
  assert(q.load()==R::Ok);
  assert(q.bind(order,1,epoch)==R::Ok);
  assert(q.add(10)==R::Ok && q.pending()==10);
  assert(q.bind(nextOrder,2,epoch2)==R::Pending);
  // A failed durable write must not advance the in-memory count.
  store.failWrite=true; assert(q.add(1)==R::StorageError && q.pending()==10); store.failWrite=false;
  assert(q.add(1)==R::NotReady); // uncertain write requires recovery before accepting new counts
  DurableCounter restarted(store); assert(restarted.load()==R::Ok && restarted.pending()==10);
  assert(restarted.acknowledge(epoch,8)==R::Ok && restarted.pending()==2);
  assert(restarted.acknowledge(epoch,8)==R::Ok && restarted.pending()==2);
  assert(restarted.acknowledge(epoch,11)==R::Invalid);
  assert(restarted.acknowledge(epoch2,10)==R::Invalid);
  assert(restarted.acknowledge(epoch,10)==R::Ok);
  assert(restarted.bind(nextOrder,1,epoch2)==R::Invalid);
  assert(restarted.bind(nextOrder,2,epoch2)==R::Ok && restarted.pending()==0);
  assert(restarted.add(7)==R::Ok);
  DurableCounter secondRestart(store); assert(secondRestart.load()==R::Ok && secondRestart.pending()==7);
  // Any corrupt slot blocks operation rather than silently resetting counts.
  store.slots[0].crc ^= 1;
  DurableCounter corrupt(store); assert(corrupt.load()==R::StorageError);
  std::cout << "durable counter tests passed\n";
}
