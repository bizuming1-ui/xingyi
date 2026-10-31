#include "../firmware/include/MonitorEvent.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main(){
 MonitorEvent e{};e.ticket=42;e.endMs=1234;e.durationMs=398;e.kind=2;e.result=0;e.barrier=0;e.http=200;e.stage=5;e.net=0;e.reused=1;e.total=55;
 char s[80];assert(formatMonitorEvent(e,0,s,sizeof s)>0 && strcmp(s,"M1 i=42 t=1234 k=2 d=398 r=0 b=0")==0);
 assert(formatMonitorEvent(e,1,s,sizeof s)>0 && strcmp(s,"M2 i=42 h=200 s=5 n=0 u=1")==0);
 assert(formatMonitorEvent(e,2,s,sizeof s)>0 && strcmp(s,"M3 i=42 x=55")==0);
 e.ticket=UINT32_MAX;e.endMs=UINT32_MAX;e.durationMs=UINT32_MAX;e.total=UINT64_MAX;
 for(unsigned i=0;i<4;i++)assert(formatMonitorEvent(e,i,s,sizeof s)>0 && strlen(s)<64);
 assert(formatMonitorEvent(e,3,s,sizeof s)>0);assert(formatMonitorEvent(e,4,s,sizeof s)<0);
 std::cout<<"numeric monitor formatting tests passed\n";
}
