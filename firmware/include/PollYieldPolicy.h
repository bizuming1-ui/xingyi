#pragma once
#include <atomic>
#include <stdint.h>

// Counting thread calls accepted(); all other operations are network-task-only.
// One status read may yield to new counts per *decoded successful* status read.
// This prevents sustained button input from indefinitely starving status and
// barrier discovery. It never affects uploads or confirmation RPCs.
class ReadInterrupt {
 public:
  virtual ~ReadInterrupt()=default;
  virtual bool interrupted() const=0;
};
class PollYieldPolicy final : public ReadInterrupt {
 public:
  void accepted() { generation_.fetch_add(1,std::memory_order_relaxed); }
  void beginPoll() { baseline_=generation_.load(std::memory_order_relaxed); }
  bool interrupted() const override {
    return permitted_ && generation_.load(std::memory_order_relaxed)!=baseline_;
  }
  void yielded() {permitted_=false;}
  void validPoll() {permitted_=true;}
 private:
  std::atomic<uint32_t> generation_{0};
  uint32_t baseline_=0;bool permitted_=false;
};
