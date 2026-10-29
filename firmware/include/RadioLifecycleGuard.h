#pragma once
#include "WifiSupervisor.h"
#include "RandomEpochSource.h"

// Nonblocking lock shared by Wi-Fi lifecycle changes and Wi-Fi-backed entropy.
// A single instance outlives all users. Every radio start/stop/reconfiguration
// must use it; a connectivity check alone is not a lifecycle lock.
class RadioLifecycleLock {
 public:
  virtual ~RadioLifecycleLock()=default;
  virtual bool tryLock()=0;
  virtual void unlock()=0;
};

// Network task owns this wrapper and the supervisor. If the lock is busy, no
// Wi-Fi operation or state transition occurs; retry the step later. TLS/time
// requests and supervisor.step must also be serialized by the network task.
class GuardedWifiSupervisor {
 public:
  GuardedWifiSupervisor(RadioLifecycleLock& lock,WifiSupervisor& supervisor):lock_(lock),supervisor_(supervisor){}
  GuardedWifiSupervisor(const GuardedWifiSupervisor&)=delete;
  GuardedWifiSupervisor& operator=(const GuardedWifiSupervisor&)=delete;
  bool step(uint32_t now) {
    if(!lock_.tryLock())return false;
    struct Release {RadioLifecycleLock& lock;~Release(){lock.unlock();}} release{lock_};
    supervisor_.step(now);return true;
  }
 private:
  RadioLifecycleLock& lock_;WifiSupervisor& supervisor_;
};

// One wrapper per owning task (failureMayRetry is thread-local to the wrapper).
// All wrappers share the same lock; no waiting in the counting task. Never
// treat lock contention or a disconnected radio as secure random output.
class GuardedEntropy final : public SecureEntropy {
 public:
  GuardedEntropy(RadioLifecycleLock& lock,SecureEntropy& source):lock_(lock),source_(source){}
  GuardedEntropy(const GuardedEntropy&)=delete;
  GuardedEntropy& operator=(const GuardedEntropy&)=delete;
  bool fill(uint8_t* bytes,size_t length) override {
    retryable_=false;
    if(!bytes || !length)return false;
    if(!lock_.tryLock()){wipe(bytes,length);retryable_=true;return false;}
    struct Release {RadioLifecycleLock& lock;~Release(){lock.unlock();}} release{lock_};
    const bool ok=source_.fill(bytes,length);
    if(!ok){wipe(bytes,length);retryable_=source_.failureMayRetry();}
    return ok;
  }
  bool failureMayRetry() const override {return retryable_;}
 private:
  RadioLifecycleLock& lock_;SecureEntropy& source_;bool retryable_=false;
  static void wipe(uint8_t* bytes,size_t n){volatile uint8_t* p=bytes;for(size_t i=0;i<n;++i)p[i]=0;}
};
