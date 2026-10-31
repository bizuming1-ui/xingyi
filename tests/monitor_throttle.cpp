#include "../firmware/include/MonitorThrottle.h"
#include <cassert>
#include <iostream>
int main() {
 MonitorThrottle m;
 assert(m.emit(true,false,0));assert(!m.emit(true,false,200));assert(!m.emit(true,false,999));assert(m.emit(true,false,1000));
 assert(m.emit(false,false,1001));assert(m.emit(true,true,1002));assert(m.emit(true,false,1003));
 MonitorThrottle wrap;assert(wrap.emit(true,false,UINT32_MAX-100));assert(!wrap.emit(true,false,50));assert(wrap.emit(true,false,1000));
 CountLineThrottle c;c.begin(5);assert(!c.due(5,100));assert(!c.due(6,99));assert(c.due(6,100));
 c.attempted(6,100,false);assert(!c.due(6,199));assert(c.due(6,200));c.attempted(7,200,true);
 assert(!c.due(7,300));assert(c.due(8,300));c.attempted(8,UINT32_MAX-30,false);assert(!c.due(8,50));assert(c.due(8,70));
 std::cout<<"diagnostic throttling and retry tests passed\n";
}
