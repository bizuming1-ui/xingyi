#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

// Copied to a bounded diagnostics-only queue; never contains credentials,
// UUIDs, network payloads or device identifiers. All time fields are
// monotonic milliseconds modulo 2^32; ticket is boot-local and wraps.
struct MonitorEvent {
  uint32_t ticket=0, endMs=0, durationMs=0;
  uint64_t total=0;
  int kind=0,result=0,barrier=0,http=0,stage=0,net=0,reused=0;
  int connectMs=-1,sentMs=-1,firstMs=-1;unsigned rxBytes=0;
};
inline int formatMonitorEvent(const MonitorEvent& e,unsigned part,char* out,size_t size) {
  if(part==0)return snprintf(out,size,"M1 i=%lu t=%lu k=%d d=%lu r=%d b=%d",static_cast<unsigned long>(e.ticket),static_cast<unsigned long>(e.endMs),e.kind,static_cast<unsigned long>(e.durationMs),e.result,e.barrier);
  if(part==1)return snprintf(out,size,"M2 i=%lu h=%d s=%d n=%d u=%d",static_cast<unsigned long>(e.ticket),e.http,e.stage,e.net,e.reused);
  if(part==2)return snprintf(out,size,"M3 i=%lu x=%llu",static_cast<unsigned long>(e.ticket),static_cast<unsigned long long>(e.total));
  if(part==3)return snprintf(out,size,"M4 i=%lu cn=%d tx=%d fb=%d rx=%u",static_cast<unsigned long>(e.ticket),e.connectMs,e.sentMs,e.firstMs,e.rxBytes);
  return -1;
}
