#include "../firmware/include/WatchdogResetMarker.h"
#include <cassert>
#include <iostream>
int main() {
 WatchdogResetMarker::Record record{}; WatchdogResetMarker marker(record);
 assert(!marker.consume(true));
 marker.markAttempt(); assert(marker.consume(true)); assert(!marker.consume(true));
 marker.markAttempt(); assert(!marker.consume(false)); assert(!marker.consume(true));
 marker.markAttempt(); record.inverse^=1; assert(!marker.consume(true));
 marker.markAttempt(); record.magic^=1; assert(!marker.consume(true));
 std::cout << "watchdog RTC attempt marker consume and invalid record tests passed\n";
}
