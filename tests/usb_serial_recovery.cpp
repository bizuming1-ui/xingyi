#include "../firmware/include/UsbSerialRecovery.h"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
 UsbSerialRecovery r;
 assert(!r.due(249)); assert(r.due(250)); assert(!r.due(251));
 assert(r.due(500)); assert(!r.shouldKick(false,false,0));
 assert(!r.shouldKick(true,true,128)); assert(r.shouldKick(true,true,0));
 assert(r.shouldKick(true,true,-1)); assert(r.shouldKick(true,false,0));
 UsbSerialRecovery wrap; assert(wrap.due(UINT32_MAX-100));
 assert(!wrap.due(100)); assert(wrap.due(150));
 std::cout << "USB recovery throttle, no host, full buffer, wrap tests passed\n";
}
