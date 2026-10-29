#pragma once
#include <cstdint>
using err_t=int;using u8_t=uint8_t;
constexpr err_t ERR_OK=0,ERR_INPROGRESS=-5,ERR_MEM=-1,ERR_VAL=-6;
constexpr u8_t LWIP_DNS_ADDRTYPE_IPV4=0;
struct ip4_addr_t {uint8_t bytes[4];};
struct ip_addr_t {ip4_addr_t v4;bool isV4=true;};
#define IP_IS_V4(x) ((x)->isV4)
#define ip_2_ip4(x) (&((x)->v4))
#define ip4_addr1(x) ((x)->bytes[0])
#define ip4_addr2(x) ((x)->bytes[1])
#define ip4_addr3(x) ((x)->bytes[2])
#define ip4_addr4(x) ((x)->bytes[3])
using dns_found_callback=void (*)(const char*,const ip_addr_t*,void*);
extern err_t fakeDnsResult;
extern dns_found_callback fakeDnsCallback;
extern void* fakeDnsContext;
inline err_t dns_gethostbyname_addrtype(const char*,ip_addr_t* out,dns_found_callback cb,void* ctx,u8_t type){
  if(type!=LWIP_DNS_ADDRTYPE_IPV4)return ERR_VAL;
  if(fakeDnsResult==ERR_OK){out->v4.bytes[0]=1;out->v4.bytes[1]=2;out->v4.bytes[2]=3;out->v4.bytes[3]=4;}
  if(fakeDnsResult==ERR_INPROGRESS){fakeDnsCallback=cb;fakeDnsContext=ctx;}
  return fakeDnsResult;
}
