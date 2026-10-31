#include "../firmware/include/EspAsyncDnsBackend.h"
#include <cassert>
#include <iostream>
err_t fakeDnsResult=ERR_OK;dns_found_callback fakeDnsCallback=nullptr;void* fakeDnsContext=nullptr;
bool fakeQueueFull=false;tcpip_callback_fn fakeTcpipCallback=nullptr;void* fakeTcpipContext=nullptr;
void dispatch(){assert(fakeTcpipCallback);auto f=fakeTcpipCallback;fakeTcpipCallback=nullptr;f(fakeTcpipContext);}
int main(){
 EspAsyncDnsBackend backend;AsyncDns dns(backend);AsyncDns::Handle handle{};uint8_t ip[4]{};
 fakeQueueFull=true;assert(dns.begin("project123.supabase.co",handle));assert(dns.poll(handle,ip)==AsyncDns::Result::Failed);
 fakeQueueFull=false;fakeDnsResult=ERR_OK;assert(dns.begin("project123.supabase.co",handle));
 assert(dns.poll(handle,ip)==AsyncDns::Result::Pending);dispatch();assert(dns.poll(handle,ip)==AsyncDns::Result::Success && ip[0]==1 && ip[3]==4);
 fakeDnsResult=ERR_VAL;assert(dns.begin("project123.supabase.co",handle));dispatch();assert(dns.poll(handle,ip)==AsyncDns::Result::Failed);
 fakeDnsResult=ERR_INPROGRESS;assert(dns.begin("project123.supabase.co",handle));dispatch();assert(dns.expire(handle));
 const ip_addr_t answer{{{9,8,7,6}},true};fakeDnsCallback("project123.supabase.co",&answer,fakeDnsContext);
 assert(dns.poll(handle,ip)==AsyncDns::Result::Invalid);
 assert(dns.begin("project123.supabase.co",handle));dispatch();fakeDnsCallback("project123.supabase.co",nullptr,fakeDnsContext);
 assert(dns.poll(handle,ip)==AsyncDns::Result::Failed);
 std::cout<<"ESP asynchronous DNS backend mock tests passed\n";
}
