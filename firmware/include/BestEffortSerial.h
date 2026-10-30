#pragma once
#include <stddef.h>
#include <string.h>

// Serial must be configured for a zero TX timeout by its platform adapter.
// Checking readiness before free space allows USB CDC implementations to
// service reconnection; neither logging nor dropped output changes counting.
template <typename SerialPort>
bool bestEffortLine(SerialPort& serial,const char* text) {
  if(!text || !serial)return false;
  const size_t n=strlen(text);
  const int free=serial.availableForWrite();
  if(free<0 || static_cast<size_t>(free)<n+2)return false;
  return serial.println(text)==n+2;
}
