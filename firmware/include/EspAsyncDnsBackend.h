#pragma once
#include "AsyncDns.h"
#include <lwip/dns.h>
#include <lwip/tcpip.h>

// Queue DNS work without blocking; lwIP DNS APIs run on the TCP/IP thread. The
// Request must stay alive until completion even after the caller times out.
class EspAsyncDnsBackend final : public AsyncDnsBackend {
 public:
  bool start(Request* request) override {return request && tcpip_try_callback(&startOnTcpip,request)==ERR_OK;}
 private:
  static void completeAddress(Request* request,const ip_addr_t* addr) {
    if(!addr || !IP_IS_V4(addr)) {request->complete(request->context,nullptr);return;}
    const ip4_addr_t* ip=ip_2_ip4(addr);
    const uint8_t bytes[4]={ip4_addr1(ip),ip4_addr2(ip),ip4_addr3(ip),ip4_addr4(ip)};
    if(!(bytes[0]||bytes[1]||bytes[2]||bytes[3])) {request->complete(request->context,nullptr);return;}
    request->complete(request->context,bytes);
  }
  static void found(const char*,const ip_addr_t* addr,void* arg) {completeAddress(static_cast<Request*>(arg),addr);}
  static void startOnTcpip(void* arg) {
    Request* request=static_cast<Request*>(arg);ip_addr_t address{};
    const err_t result=dns_gethostbyname_addrtype(request->host,&address,&found,request,LWIP_DNS_ADDRTYPE_IPV4);
    if(result==ERR_OK)completeAddress(request,&address);
    else if(result!=ERR_INPROGRESS)request->complete(request->context,nullptr);
  }
};
