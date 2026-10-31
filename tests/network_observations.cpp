#include "../firmware/include/NetworkObservations.h"
#include <cassert>
int main(){
 NetworkObservations n;auto s=n.snapshot();assert(!s.everOk && s.ok==0);
 n.record(false,true,false,true,false,5);n.record(true,false,false,true,false,6);
 n.record(true,true,false,false,true,7);n.record(true,true,false,true,false,8);
 n.record(true,true,false,false,false,9);n.record(true,true,true,false,false,10);
 s=n.snapshot();assert(s.everOk && s.ok==1 && s.fail==2 && s.timeout==1 && s.yield==1 && s.lastOk==10);
}
