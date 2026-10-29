#include "../firmware/include/PreferencesCounterStorage.h"
#include <cassert>
#include <iostream>
int main() {
  PreferencesCounterStorage storage; assert(storage.begin());
  DurableCounter c(storage); assert(c.load()==DurableCounter::Result::Ok);
  assert(c.bind("00000000-0000-4000-8000-000000000001",1,"10000000-0000-4000-8000-000000000001")==DurableCounter::Result::Ok);
  assert(c.add(10)==DurableCounter::Result::Ok);
  DurableCounter d(storage); assert(d.load()==DurableCounter::Result::Ok);
  assert(d.pending()==10); storage.end();
  std::cout << "preferences adapter mock tests passed\n";
}
