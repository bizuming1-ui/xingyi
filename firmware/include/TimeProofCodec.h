#pragma once
#include "TimeProof.h"

// Strict wire format for the time-only endpoint; neither direction contains
// credentials. This does NOT implement network transport or set the clock.
class TimeProofCodec {
 public:
  // Request fields come only from the active proof to prevent a caller from
  // requesting a proof for one device while validating against another.
  static bool request(const TimeProof& proof,char* out,size_t capacity) {
    if(out && capacity)out[0]='\0';
    if(!out || !capacity || !proof.deviceId() || !proof.nonce())return false;
    const int n=snprintf(out,capacity,"{\"p_device\":\"%s\",\"p_nonce\":\"%s\"}",proof.deviceId(),proof.nonce());
    if(n<0 || static_cast<size_t>(n)>=capacity){out[0]='\0';return false;}
    return true;
  }
  // Accept exactly one JSON object containing exactly the two expected string
  // fields. Reject escapes, duplicates, unknown fields, NULs and trailing data.
  // Failure consumes the pending nonce; retries need a new random challenge.
  static bool response(TimeProof& proof,const char* data,size_t length,uint32_t now,uint64_t& seconds) {
    char unixText[11]{},tag[65]{};bool haveUnix=false,haveTag=false;
    if(!data || !length || length>256){proof.clear();return false;}
    size_t i=0;space(data,length,i);
    if(!take(data,length,i,'{')){proof.clear();return false;}
    for(unsigned count=0;count<2;++count) {
      char key[20]{};char value[65]{};
      space(data,length,i);
      if(!token(data,length,i,key,sizeof(key))){proof.clear();return false;}
      space(data,length,i);
      if(!take(data,length,i,':')){proof.clear();return false;}
      space(data,length,i);
      if(!token(data,length,i,value,sizeof(value))){proof.clear();return false;}
      if(strcmp(key,"unix_seconds")==0 && !haveUnix && strlen(value)<sizeof(unixText)) {strcpy(unixText,value);haveUnix=true;}
      else if(strcmp(key,"tag")==0 && !haveTag) {strcpy(tag,value);haveTag=true;}
      else {proof.clear();return false;}
      space(data,length,i);
      if(count==0 && !take(data,length,i,',')){proof.clear();return false;}
    }
    space(data,length,i);
    if(!take(data,length,i,'}') || !haveUnix || !haveTag){proof.clear();return false;}
    space(data,length,i);
    if(i!=length){proof.clear();return false;}
    return proof.verify(unixText,tag,now,seconds);
  }
 private:
  static void space(const char* s,size_t n,size_t& i){while(i<n && (s[i]==' '||s[i]=='\n'||s[i]=='\r'||s[i]=='\t'))++i;}
  static bool take(const char* s,size_t n,size_t& i,char expected){if(i>=n || s[i]!=expected)return false;++i;return true;}
  static bool token(const char* s,size_t n,size_t& i,char* out,size_t cap) {
    if(!take(s,n,i,'"'))return false;
    size_t used=0;
    while(i<n && s[i]!='"') {
      const char c=s[i++];
      if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||c=='_') || used+1>=cap)return false;
      out[used++]=c;
    }
    if(!used || !take(s,n,i,'"'))return false;
    out[used]='\0';return true;
  }
};
