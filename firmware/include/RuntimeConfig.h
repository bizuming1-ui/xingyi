#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Pure validation: rejects incomplete local settings before any radio or RPC.
// CA parsing and certification are the caller's responsibility. No secrets
// are logged. The trusted time policy must be justified for the real hardware.
struct RuntimeConfig {
  const char *ssid,*password,*host,*key,*deviceId,*secret,*ca;
  uint32_t timeLifetimeMs,clockDriftPpm;
  bool valid() const {
    if(!ssid || !password || !host || !key || !deviceId || !secret || !ca) return false;
    const size_t sn=length(ssid,33),pn=length(password,65);
    if(!sn || sn>32 || pn>64 || (pn>0 && pn<8))return false;
    if(pn==64)for(size_t i=0;i<pn;++i)if(!hexAny(password[i]))return false;
    const size_t hn=length(host,80);const char suffix[]=".supabase.co";const size_t sl=sizeof(suffix)-1;
    if(hn<=sl || hn>=80 || hn-sl>63 || memcmp(host+hn-sl,suffix,sl)!=0)return false;
    for(size_t i=0;i<hn-sl;++i)if(!((host[i]>='a'&&host[i]<='z')||(host[i]>='0'&&host[i]<='9')))return false;
    const size_t kn=length(key,128);
    if(kn<=15 || kn>=128 || strncmp(key,"sb_publishable_",15)!=0)return false;
    for(size_t i=0;i<kn;++i){const char c=key[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;}
    if(length(deviceId,37)!=36 || length(secret,65)!=64)return false;
    for(size_t i=0;i<36;++i){const char c=deviceId[i];if(i==8||i==13||i==18||i==23){if(c!='-')return false;}else if(!lowerHex(c))return false;}
    for(size_t i=0;i<64;++i)if(!lowerHex(secret[i]))return false;
    if(strncmp(ca,"-----BEGIN CERTIFICATE-----",27)!=0 || length(ca,16385)>=16385)return false;
    return timeLifetimeMs>0 && timeLifetimeMs<=86400000 && clockDriftPpm<=9999;
  }
 private:
  static size_t length(const char* s,size_t cap){size_t n=0;while(n<cap && s[n])++n;return n;}
  static bool lowerHex(char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}
  static bool hexAny(char c){return lowerHex(c)||(c>='A'&&c<='F');}
};
