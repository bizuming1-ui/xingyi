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
 for(unsigned i=0;i<5;i++)assert(formatMonitorEvent(e,i,s,sizeof s)>0 && strlen(s)<64);
 assert(formatMonitorEvent(e,3,s,sizeof s)>0);assert(formatMonitorEvent(e,4,s,sizeof s)>0 && strstr(s,"dns=-1 tcp=-1"));assert(formatMonitorEvent(e,5,s,sizeof s)<0);
 MonitorEvent t{};t.ticket=7;t.endMs=300;t.durationMs=120;t.kind=0;t.barrier=1;t.result=1;t.stage=7;t.http=4500;t.net=0;t.connectMs=30;t.sentMs=80;t.firstMs=200;
 assert(formatMonitorEvent(t,0,s,sizeof s)>0 && strcmp(s,"T1 i=7 t=300 d=120 m=1 r=1 s=7")==0);
 assert(formatMonitorEvent(t,1,s,sizeof s)>0 && strcmp(s,"T2 i=7 code=0 budget=4500 hs=30 tx=80 rx=200")==0);
 assert(formatMonitorEvent(t,2,s,sizeof s)<0);
 std::cout<<"numeric monitor formatting tests passed\n";
}
