#pragma once
#include "RandomEpochSource.h"
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Prototype signed-time verifier, NOT a complete clock or network bootstrap.
// A fresh 256-bit cryptographic nonce is single-use and bound to the device ID,
// signed server time, and a domain-separated message. Caller must immediately
// set the wall clock from accepted seconds and independently track whether it
// remains trustworthy. Never send secret, its hash, or this key in requests.
// Entropy and Wi-Fi lifecycle must be protected as for RandomEpochSource.
class TimeProof {
 public:
  static constexpr uint32_t MAX_ROUND_TRIP_MS=5000;
  explicit TimeProof(SecureEntropy& entropy):entropy_(entropy){}
  ~TimeProof(){clear();}
  TimeProof(const TimeProof&)=delete;
  TimeProof& operator=(const TimeProof&)=delete;
  bool begin(const char* deviceId,const char* secret,uint32_t now) {
    clear();
    if(!uuidValid(deviceId) || !lowerHex(secret,64))return false;
    if(mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(secret),64,key_,0)!=0){clear();return false;}
    uint8_t bytes[32]{};
    if(!entropy_.fill(bytes,sizeof(bytes))){wipe(bytes,sizeof(bytes));clear();return false;}
    static const char hex[]="0123456789abcdef";
    for(size_t i=0;i<sizeof(bytes);++i){nonce_[2*i]=hex[bytes[i]>>4];nonce_[2*i+1]=hex[bytes[i]&15];}
    nonce_[64]='\0';wipe(bytes,sizeof(bytes));memcpy(device_,deviceId,37);started_=now;pending_=true;return true;
  }
  const char* nonce() const {return pending_?nonce_:nullptr;}
  // Any verification attempt consumes the nonce, even on bad input or timeout.
  // unixSeconds is changed only on success. Actual elapsed time may be close
  // to the cap; the runtime must handle that clock uncertainty conservatively.
  bool verify(const char* unixText,const char* tag,uint32_t now,uint64_t& unixSeconds) {
    if(!pending_)return false;
    pending_=false;bool ok=false;uint8_t expected[32]{},supplied[32]{};char message[160]{};uint64_t value=0;
    do {
      if(static_cast<uint32_t>(now-started_)>=MAX_ROUND_TRIP_MS || !secondsValid(unixText,value) || !lowerHex(tag,64))break;
      const int n=snprintf(message,sizeof(message),"xingyi-time-v1|%s|%s|%s",device_,nonce_,unixText);
      if(n<0 || static_cast<size_t>(n)>=sizeof(message))break;
      const auto* md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);if(!md)break;
      if(mbedtls_md_hmac(md,key_,sizeof(key_),reinterpret_cast<const unsigned char*>(message),static_cast<size_t>(n),expected)!=0)break;
      for(size_t i=0;i<32;++i)supplied[i]=static_cast<uint8_t>((nibble(tag[2*i])<<4)|nibble(tag[2*i+1]));
      uint8_t diff=0;for(size_t i=0;i<32;++i)diff|=expected[i]^supplied[i];
      if(diff)break;
      unixSeconds=value;ok=true;
    } while(false);
    wipe(expected,sizeof(expected));wipe(supplied,sizeof(supplied));wipe(message,sizeof(message));clear();return ok;
  }
  void clear(){wipe(key_,sizeof(key_));wipe(nonce_,sizeof(nonce_));wipe(device_,sizeof(device_));pending_=false;started_=0;}
 private:
  SecureEntropy& entropy_;uint8_t key_[32]{};char nonce_[65]{},device_[37]{};uint32_t started_=0;bool pending_=false;
  static void wipe(void* p,size_t n){volatile uint8_t* b=static_cast<volatile uint8_t*>(p);for(size_t i=0;i<n;++i)b[i]=0;}
  static uint8_t nibble(char c){return static_cast<uint8_t>(c<='9'?c-'0':c-'a'+10);}
  static bool lowerHex(const char* s,size_t n){if(!s)return false;for(size_t i=0;i<n;++i)if(!s[i] || !((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;return s[n]=='\0';}
  static bool uuidValid(const char* s){
    if(!s)return false;
    for(size_t i=0;i<36;++i){if(!s[i])return false;if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;}
    return s[36]=='\0';
  }
  static bool secondsValid(const char* s,uint64_t& v) {
    if(!s || s[0]<'1' || s[0]>'9')return false;
    v=0;size_t i=0;for(;i<10 && s[i];++i){if(s[i]<'0'||s[i]>'9')return false;v=v*10+static_cast<unsigned>(s[i]-'0');}
    return i>0 && i<11 && s[i]=='\0' && v>=1704067200ULL && v<=4102444800ULL;
  }
};
