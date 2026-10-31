#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Only the network task may call begin/poll/expire. The backend may complete
// from the TCP/IP task. Request objects and this resolver must outlive every
// backend callback, including callbacks that arrive after a local timeout.
class AsyncDnsBackend {
 public:
  struct Request {
    char host[80]{};
    void* context=nullptr;
    void (*complete)(void*,const uint8_t*)=nullptr; // null address = failure
  };
  virtual ~AsyncDnsBackend()=default;
  // Return false only if no callback will be made. Accepted requests must invoke
  // complete exactly once, even if they ultimately time out or fail. The
  // backend must not access the Request after calling complete.
  virtual bool start(Request* request)=0;
};

// Bounded asynchronous IPv4 resolver. DNS does not authenticate a host; TLS
// must still verify the original hostname and trusted certificate. A timed-out
// request retains its slot until the backend's late callback is finished. If
// all slots are occupied, resolution fails closed rather than reusing memory.
class AsyncDns {
 public:
  static constexpr unsigned CAPACITY=4;
  enum class Result { Pending, Success, Failed, Invalid };
  struct Handle { uint32_t generation=0; uint8_t index=0; };
  explicit AsyncDns(AsyncDnsBackend& backend):backend_(backend) {}
  AsyncDns(const AsyncDns&)=delete;
  AsyncDns& operator=(const AsyncDns&)=delete;

  bool begin(const char* hostname,Handle& out) {
    out=Handle{};
    if(!validHost(hostname))return false;
    for(unsigned i=0;i<CAPACITY;++i) {
      Slot& slot=slots_[i];uint8_t expected=Idle;
      if(slot.generation==UINT32_MAX || !slot.state.compare_exchange_strong(expected,PendingState,std::memory_order_acq_rel))continue;
      ++slot.generation;out.generation=slot.generation;out.index=static_cast<uint8_t>(i);
      memcpy(slot.request.host,hostname,strlen(hostname)+1);slot.request.context=&slot;slot.request.complete=&complete;
      if(!backend_.start(&slot.request))complete(&slot,nullptr);
      return true;
    }
    return false;
  }
  Result poll(const Handle& handle,uint8_t out[4]) {
    Slot* slot=checked(handle);if(!slot)return Result::Invalid;
    const auto state=slot->state.load(std::memory_order_acquire);
    if(state==PendingState)return Result::Pending;
    if(state==Succeeded) {
      if(!out)return Result::Invalid;
      memcpy(out,slot->address,4);slot->state.store(Idle,std::memory_order_release);return Result::Success;
    }
    if(state==FailedState){slot->state.store(Idle,std::memory_order_release);return Result::Failed;}
    return Result::Invalid;
  }
  // Stops waiting; this cannot cancel a DNS packet already dispatched.
  // Late completion releases the reserved slot without delivering its result.
  bool expire(const Handle& handle) {
    Slot* slot=checked(handle);if(!slot)return false;
    uint8_t expected=PendingState;
    if(slot->state.compare_exchange_strong(expected,Abandoned,std::memory_order_acq_rel))return true;
    if(expected==Succeeded || expected==FailedState){slot->state.store(Idle,std::memory_order_release);return true;}
    return false;
  }
 private:
  enum : uint8_t {Idle,PendingState,Succeeded,FailedState,Abandoned};
  struct Slot {
    std::atomic<uint8_t> state{Idle};
    uint32_t generation=0;AsyncDnsBackend::Request request{};uint8_t address[4]{};
  };
  AsyncDnsBackend& backend_;Slot slots_[CAPACITY]{};
  Slot* checked(const Handle& h) {
    if(!h.generation || h.index>=CAPACITY)return nullptr;
    Slot& slot=slots_[h.index];return slot.generation==h.generation?&slot:nullptr;
  }
  static void complete(void* context,const uint8_t* address) {
    Slot* slot=static_cast<Slot*>(context);
    if(address)memcpy(slot->address,address,4);
    uint8_t expected=PendingState;
    if(slot->state.compare_exchange_strong(expected,address?Succeeded:FailedState,std::memory_order_acq_rel,std::memory_order_acquire))return;
    if(expected==Abandoned)slot->state.store(Idle,std::memory_order_release);
  }
  static bool validHost(const char* host) {
    if(!host)return false;
    size_t n=0;while(n<80 && host[n])++n;
    const char suffix[]=".supabase.co";const size_t suffixLength=sizeof(suffix)-1;
    if(n<=suffixLength || n>=80 || n-suffixLength>63 || memcmp(host+n-suffixLength,suffix,suffixLength)!=0)return false;
    for(size_t i=0;i<n-suffixLength;++i)if(!((host[i]>='a'&&host[i]<='z')||(host[i]>='0'&&host[i]<='9')))return false;
    return true;
  }
};
