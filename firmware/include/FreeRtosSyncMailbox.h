#pragma once
#include "SyncBridge.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <type_traits>

static_assert(std::is_trivially_copyable<SyncPlanner::Command>::value, "queue commands must be copyable as bytes");
static_assert(std::is_trivially_copyable<SyncMailbox::Completion>::value, "queue completions must be copyable as bytes");

// Static one-item FreeRTOS queues; no dynamic allocations and no waits. Object
// must outlive both tasks and must never be copied or destroyed while in use.
class FreeRtosSyncMailbox final : public SyncMailbox {
 public:
  FreeRtosSyncMailbox() {
    commands_=xQueueCreateStatic(1,sizeof(SyncPlanner::Command),commandStorage_,&commandQueue_);
    completions_=xQueueCreateStatic(1,sizeof(Completion),completionStorage_,&completionQueue_);
  }
  FreeRtosSyncMailbox(const FreeRtosSyncMailbox&)=delete;
  FreeRtosSyncMailbox& operator=(const FreeRtosSyncMailbox&)=delete;
  bool valid() const { return commands_ && completions_; }
  bool submit(const SyncPlanner::Command& item) override { return commands_ && xQueueSend(commands_,&item,0)==pdTRUE; }
  bool collect(Completion& item) override { return completions_ && xQueueReceive(completions_,&item,0)==pdTRUE; }
  bool take(SyncPlanner::Command& item) override { return commands_ && xQueueReceive(commands_,&item,0)==pdTRUE; }
  bool publish(const Completion& item) override { return completions_ && xQueueSend(completions_,&item,0)==pdTRUE; }
 private:
  StaticQueue_t commandQueue_{},completionQueue_{};
  uint8_t commandStorage_[sizeof(SyncPlanner::Command)]{};
  uint8_t completionStorage_[sizeof(Completion)]{};
  QueueHandle_t commands_=nullptr,completions_=nullptr;
};
