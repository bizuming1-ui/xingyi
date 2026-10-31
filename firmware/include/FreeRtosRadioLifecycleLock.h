#pragma once
#include "RadioLifecycleGuard.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Static FreeRTOS mutex; both tryLock and unlock must run in task context,
// never in an ISR. The owner must check valid() and never destroy while used.
class FreeRtosRadioLifecycleLock final : public RadioLifecycleLock {
 public:
  FreeRtosRadioLifecycleLock():mutex_(xSemaphoreCreateMutexStatic(&storage_)){}
  FreeRtosRadioLifecycleLock(const FreeRtosRadioLifecycleLock&)=delete;
  FreeRtosRadioLifecycleLock& operator=(const FreeRtosRadioLifecycleLock&)=delete;
  bool valid() const {return mutex_!=nullptr;}
  bool tryLock() override {return mutex_ && xSemaphoreTake(mutex_,0)==pdTRUE;}
  void unlock() override {if(mutex_)(void)xSemaphoreGive(mutex_);}
 private:
  StaticSemaphore_t storage_{};SemaphoreHandle_t mutex_=nullptr;
};
