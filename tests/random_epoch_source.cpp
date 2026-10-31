#include "../firmware/include/RandomEpochSource.h"
#include <cassert>
#include <cstring>
#include <iostream>
struct Entropy : SecureEntropy {
  bool success=true,retry=false;unsigned calls=0;
  bool failureMayRetry() const override {return retry;}
  bool fill(uint8_t* out,size_t n) override {
    ++calls;assert(n==16);for(size_t i=0;i<n;++i)out[i]=static_cast<uint8_t>(i);
    return success;
  }
};
int main() {
  Entropy source;RandomEpochSource epochs(source);char uuid[37]{};
  assert(!epochs.generate(nullptr) && source.calls==0);
  assert(epochs.generate(uuid) && strcmp(uuid,"00010203-0405-4607-8809-0a0b0c0d0e0f")==0);
  source.success=false;memset(uuid,'x',sizeof(uuid));
  assert(!epochs.generate(uuid) && uuid[0]=='\0' && !epochs.failureMayRetry());
  source.retry=true;assert(!epochs.generate(uuid) && epochs.failureMayRetry());
  source.success=true;assert(epochs.generate(uuid) && !epochs.failureMayRetry());
  std::cout<<"random epoch source tests passed\n";
}
