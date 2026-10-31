#pragma once
#include "SyncPlanner.h"

// Implementations must obtain cryptographically secure random bytes, or fail.
// Never use timestamps, counters, Arduino random(), or a deterministic seed.
class SecureEntropy {
 public:
  virtual ~SecureEntropy()=default;
  virtual bool fill(uint8_t* bytes,size_t length)=0;
  virtual bool failureMayRetry() const {return false;}
};

class RandomEpochSource final : public EpochSource {
 public:
  explicit RandomEpochSource(SecureEntropy& entropy):entropy_(entropy) {}
  bool generate(char out[37]) override {
    retryable_=false;
    if(!out)return false;
    out[0]='\0';
    struct RandomBytes {
      uint8_t data[16]{};
      ~RandomBytes() { volatile uint8_t* p=data; for(size_t i=0;i<sizeof(data);++i)p[i]=0; }
    } bytes;
    if(!entropy_.fill(bytes.data,sizeof(bytes.data))) {retryable_=entropy_.failureMayRetry();return false;}
    bytes.data[6]=(bytes.data[6]&0x0f)|0x40;
    bytes.data[8]=(bytes.data[8]&0x3f)|0x80;
    static const char hex[]="0123456789abcdef";
    size_t pos=0;
    for(size_t i=0;i<sizeof(bytes.data);++i) {
      if(i==4 || i==6 || i==8 || i==10)out[pos++]='-';
      out[pos++]=hex[bytes.data[i]>>4];out[pos++]=hex[bytes.data[i]&15];
    }
    out[pos]='\0';return true;
  }
  bool failureMayRetry() const override {return retryable_;}
 private:
  SecureEntropy& entropy_;bool retryable_=false;
};
