#include "../firmware/include/BestEffortSerial.h"
#include <cassert>
#include <string>
#include <iostream>
struct Port {
  bool connected=false; int space=0; size_t written=0; int readiness=0,checks=0,calls=0;
  explicit operator bool(){++readiness;return connected;}
  int availableForWrite(){++checks;return space;}
  size_t println(const char* value){++calls;written=strlen(value)+2;return written;}
};
int main(){
 Port port;assert(!bestEffortLine(port,"status") && port.readiness==1 && port.checks==0);
 port.connected=true;port.space=7;assert(!bestEffortLine(port,"status") && port.calls==0);
 port.space=8;assert(bestEffortLine(port,"status") && port.calls==1 && port.written==8);
 port.space=-1;assert(!bestEffortLine(port,"status") && port.calls==1);
 assert(!bestEffortLine(port,nullptr));
 std::cout<<"best-effort serial readiness and full buffer tests passed\n";
}
