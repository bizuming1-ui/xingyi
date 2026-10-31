#include "../firmware/include/RadioLifecycleGuard.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
struct Lock : RadioLifecycleLock {std::atomic_flag flag=ATOMIC_FLAG_INIT;bool tryLock() override{return !flag.test_and_set(std::memory_order_acquire);}void unlock() override{flag.clear(std::memory_order_release);}};
struct FakeRadio {
 std::atomic<bool> busy{false};std::atomic<unsigned> overlaps{0};
 void operation(){if(busy.exchange(true))++overlaps;std::this_thread::yield();busy.store(false);}
};
struct Station : WifiStation {
 FakeRadio& radio;int starts=0,stops=0;bool online=false;
 explicit Station(FakeRadio& r):radio(r){}
 bool start(const char*,const char*) override {radio.operation();++starts;return true;}
 bool connected() override {radio.operation();return online;}
 void disconnect() override {radio.operation();++stops;}
};
struct Entropy : SecureEntropy {
 FakeRadio& radio;bool ready=true,retryable=true;explicit Entropy(FakeRadio& r):radio(r){}
 bool fill(uint8_t* out,size_t n) override {radio.operation();if(!ready){out[0]=5;return false;}for(size_t i=0;i<n;++i)out[i]=42;return true;}
 bool failureMayRetry() const override{return retryable;}
};
int main(){
 Lock lock;FakeRadio radio;Station station(radio);Entropy source(radio);
 WifiSupervisor supervisor(station,"network","password");GuardedWifiSupervisor wifi(lock,supervisor);
 GuardedEntropy counting(lock,source);uint8_t out[16]{};
 assert(lock.tryLock());assert(!wifi.step(0) && station.starts==0);
 assert(!counting.fill(out,sizeof(out)) && counting.failureMayRetry());for(auto c:out)assert(c==0);lock.unlock();
 assert(wifi.step(0) && station.starts==1);assert(counting.fill(out,sizeof(out)) && !counting.failureMayRetry());
 source.ready=false;assert(!counting.fill(out,sizeof(out)) && counting.failureMayRetry());for(auto c:out)assert(c==0);
 source.retryable=false;assert(!counting.fill(out,sizeof(out)) && !counting.failureMayRetry());
 assert(!counting.fill(nullptr,2) && !counting.failureMayRetry());
 source.ready=true;source.retryable=true;
 // Each task has its own wrapper, but both use the same lifecycle lock.
 GuardedEntropy network(lock,source);std::thread t([&]{uint8_t sample[16];for(int i=0;i<20000;++i)(void)network.fill(sample,sizeof(sample));});
 for(uint32_t i=0;i<20000;++i){(void)wifi.step(i*100);(void)counting.fill(out,sizeof(out));}
 t.join();assert(radio.overlaps.load()==0);
 std::cout<<"Wi-Fi lifecycle lock and entropy concurrency tests passed\n";
}
