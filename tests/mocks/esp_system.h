#pragma once
#include <cstddef>
#include <cstdint>
extern unsigned randomCalls;
inline void esp_fill_random(void* out,size_t n) {
  ++randomCalls;
  auto* p=static_cast<uint8_t*>(out);
  for(size_t i=0;i<n;++i)p[i]=static_cast<uint8_t>(i+1);
}
