#include "../firmware/include/ProgressWatchdog.h"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
 using W=ProgressWatchdog;W w;
 assert(w.step(0,0,0,true,false,true)==W::Action::None);
 assert(w.step(89999,0,0,true,false,true)==W::Action::None);
 assert(w.step(90000,0,0,true,false,true)==W::Action::Restart);
 // Normal slow network operations and fresh retry loops do not trigger resets.
 assert(w.step(120000,119999,109000,true,false,true)==W::Action::None);
 assert(w.step(180000,179999,179999,true,false,true)==W::Action::None);
 // Either stuck worker, independently, can cause recovery; ordinary slow
 // RPCs and retries that report progress cannot be mistaken for a stall.
 W netStall;assert(netStall.step(0,0,0,true,false,true)==W::Action::None);
 assert(netStall.step(90000,89999,0,true,false,true)==W::Action::Restart);
 W countStall;assert(countStall.step(0,0,0,true,false,true)==W::Action::None);
 assert(countStall.step(90000,0,89999,true,false,true)==W::Action::Restart);
 W slow;assert(slow.step(0,0,0,true,false,true)==W::Action::None);
 for(uint32_t t=11000;t<300000;t+=11000)assert(slow.step(t,t,t,true,false,true)==W::Action::None);
 assert(w.step(180001,0,0,false,false,true)==W::Action::None);
 assert(w.step(180002,0,0,true,true,true)==W::Action::None);
 assert(w.step(180003,0,0,true,false,true)==W::Action::None);
 assert(w.step(181002,0,0,true,false,true)==W::Action::None);
 assert(w.step(181003,0,0,true,false,true)==W::Action::Restart);
 assert(w.step(181004,0,0,true,false,false)==W::Action::None);
 W wrap;uint32_t start=UINT32_MAX-999;assert(wrap.step(start,start,start,true,false,true)==W::Action::None);
 assert(wrap.step(start+W::STALL_MS,start,start,true,false,true)==W::Action::Restart);
 WatchdogRestartBudget::Record r{};WatchdogRestartBudget b(r);b.begin(false);
 assert(b.available() && b.attempts()==0);assert(b.reserve());assert(b.reserve());assert(b.reserve());assert(!b.available() && !b.reserve());
 WatchdogRestartBudget next(r);next.begin(true);assert(!next.available());
 next.observe(1,true);next.observe(300000,true);next.observe(300001,false);
 next.observe(300002,true);next.observe(900001,true);assert(!next.available());
 next.observe(900002,true);assert(next.available() && next.attempts()==0);
 assert(next.reserve());r.check^=1;WatchdogRestartBudget invalid(r);invalid.begin(true);assert(invalid.attempts()==0);
 assert(invalid.reserve());invalid.begin(false);assert(invalid.attempts()==0);
 std::cout<<"progress watchdog stall, held button, fault, wrap and restart-budget tests passed\n";
}
