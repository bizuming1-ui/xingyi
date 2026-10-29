#pragma once
#include "DurableCounter.h"
#include <Preferences.h>

// Two independently committed NVS keys. Never include Wi-Fi or device secrets
// in these count blobs. The caller must stop counting on any storage error.
class PreferencesCounterStorage : public CounterStorage {
 public:
  bool begin() { return preferences_.begin("xingyi-count", false); }
  void end() { preferences_.end(); }
  int read(unsigned slot, void* data, size_t size) override {
    if (slot > 1) return -1;
    const char* key = slot ? "slot1" : "slot0";
    if (!preferences_.isKey(key)) return 0;
    if (preferences_.getBytesLength(key) != size) return -1;
    return preferences_.getBytes(key, data, size) == size ? 1 : -1;
  }
  bool write(unsigned slot, const void* data, size_t size) override {
    if (slot > 1) return false;
    return preferences_.putBytes(slot ? "slot1" : "slot0", data, size) == size;
  }
 private:
  Preferences preferences_;
};
