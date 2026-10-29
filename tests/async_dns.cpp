#include "../firmware/include/AsyncDns.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
struct Backend : AsyncDnsBackend {
  std::vector<Request*> pending;bool reject=false,immediate=false;
  bool start(Request* req) override {
    if(reject)return false;
    if(immediate){const uint8_t ip[]={1,2,3,4};req->complete(req->context,ip);}
    else pending.push_back(req);
    return true;
  }
  void finish(size_t i,bool ok=true) {auto* r=pending[i];const uint8_t ip[]={1,2,3,4};r->complete(r->context,ok?ip:nullptr);}
};
int main(){
 Backend backend;AsyncDns dns(backend);AsyncDns::Handle h{};uint8_t ip[4]{};
 assert(!dns.begin("bad.example",h) && !dns.begin("project.supabase.co.evil",h));
 assert(!dns.begin("BAD.supabase.co",h) && !dns.begin("x\r.supabase.co",h));
 assert(dns.begin("project123.supabase.co",h));assert(dns.poll(h,ip)==AsyncDns::Result::Pending);
 backend.finish(0);assert(dns.poll(h,ip)==AsyncDns::Result::Success && ip[0]==1 && ip[3]==4);
 assert(dns.poll(h,ip)==AsyncDns::Result::Invalid);
 const auto stale=h;backend.immediate=true;assert(dns.begin("project123.supabase.co",h));
 assert(dns.poll(stale,ip)==AsyncDns::Result::Invalid);assert(dns.poll(h,ip)==AsyncDns::Result::Success);
 backend.immediate=false;backend.reject=true;assert(dns.begin("project123.supabase.co",h));assert(dns.poll(h,ip)==AsyncDns::Result::Failed);backend.reject=false;
 backend.pending.clear();AsyncDns::Handle handles[AsyncDns::CAPACITY];
 for(auto& item:handles)assert(dns.begin("project123.supabase.co",item));
 for(auto item:handles)assert(dns.expire(item));
 assert(!dns.begin("project123.supabase.co",h)); // timed-out callbacks still own their slots
 backend.finish(0);assert(dns.begin("project123.supabase.co",h));
 assert(dns.poll(handles[0],ip)==AsyncDns::Result::Invalid);
 backend.finish(1);backend.finish(2,false);backend.finish(3);backend.finish(4);
 assert(dns.poll(h,ip)==AsyncDns::Result::Success);
 // Callback and local timeout can race; after joining there is no stale writer.
 for(unsigned i=0;i<500;++i){
  backend.pending.clear();assert(dns.begin("project123.supabase.co",h));
  std::thread callback([&]{backend.finish(0);});
  assert(dns.expire(h));callback.join();
  assert(dns.poll(h,ip)==AsyncDns::Result::Invalid);
 }
 std::cout<<"asynchronous DNS state tests passed\n";
}
