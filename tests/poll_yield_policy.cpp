#include "../firmware/include/PollYieldPolicy.h"
#include <cassert>
#include <iostream>
int main() {
 PollYieldPolicy p;
 p.beginPoll();p.accepted();assert(!p.interrupted()); // no valid status yet
 p.validPoll();p.beginPoll();assert(!p.interrupted());p.accepted();assert(p.interrupted());
 p.yielded();p.beginPoll();p.accepted();assert(!p.interrupted()); // sustained presses do not starve status
 p.validPoll();p.beginPoll();p.accepted();assert(p.interrupted());
 std::cout<<"poll yield fairness tests passed\n";
}
