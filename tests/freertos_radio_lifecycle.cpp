#include "../firmware/include/FreeRtosRadioLifecycleLock.h"
#include <cassert>
#include <iostream>
bool mock_mutex_creation_fails=false;
int main(){
 FreeRtosRadioLifecycleLock lock;assert(lock.valid() && lock.tryLock() && !lock.tryLock());lock.unlock();assert(lock.tryLock());lock.unlock();
 mock_mutex_creation_fails=true;FreeRtosRadioLifecycleLock bad;assert(!bad.valid() && !bad.tryLock());bad.unlock();
 std::cout<<"FreeRTOS radio mutex mock tests passed\n";
}
