#pragma once
#include <stdint.h>

// Non-blocking, active-low button state machine. Times use Arduino millis().
// Call tick frequently, including while the network is unavailable. The caller
// must durably queue the returned increments; this class does not store them.
class ButtonCounter {
 public:
  static constexpr uint32_t DEBOUNCE_MS = 25;
  static constexpr uint32_t HOLD_DELAY_MS = 500;
  static constexpr uint32_t REPEAT_MS = 100;

  explicit ButtonCounter(bool initiallyPressed = false, uint32_t now = 0)
      : raw_(initiallyPressed), stable_(initiallyPressed), changedAt_(now),
        pressedAt_(now), emittedRepeats_(0) {}

  uint32_t tick(bool pressed, uint32_t now) {
    if (pressed != raw_) {
      raw_ = pressed;
      changedAt_ = now;
    }
    uint32_t increments = 0;
    if (raw_ != stable_ && elapsed(now, changedAt_) >= DEBOUNCE_MS) {
      stable_ = raw_;
      if (stable_) {
        pressedAt_ = now;
        emittedRepeats_ = 0;
        increments = 1;
      }
    }
    // While release is being debounced, do not emit additional repeats.
    if (stable_ && raw_) {
      const uint32_t duration = elapsed(now, pressedAt_);
      if (duration >= HOLD_DELAY_MS) {
        const uint32_t due = 1 + (duration - HOLD_DELAY_MS) / REPEAT_MS;
        if (due > emittedRepeats_) {
          increments += due - emittedRepeats_;
          emittedRepeats_ = due;
        }
      }
    }
    return increments;
  }

 private:
  static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
  bool raw_;
  bool stable_;
  uint32_t changedAt_;
  uint32_t pressedAt_;
  uint32_t emittedRepeats_;
};
