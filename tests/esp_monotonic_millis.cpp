#include "../firmware/include/EspMonotonicMillis.h"
#include <cassert>
#include <iostream>
int64_t mock_esp_timer_us=0;
int main(){EspMonotonicMillis c;uint64_t ms=44;assert(c.read(ms)&&ms==0);mock_esp_timer_us=1234567;assert(c.read(ms)&&ms==1234);mock_esp_timer_us=-1;assert(!c.read(ms)&&ms==1234);std::cout<<"ESP monotonic clock adapter tests passed\n";}
