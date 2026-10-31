#pragma once
#include <stdint.h>

// The same strict round-trip bound must be used for nonce verification and
// for construction of the conservative UTC interval. Longer trips fail closed.
struct TimeProofLimits {
  static constexpr uint32_t MAX_ROUND_TRIP_MS=12000;
};
