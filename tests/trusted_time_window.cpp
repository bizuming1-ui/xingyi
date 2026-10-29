#include "../firmware/include/TrustedTimeWindow.h"
#include <cassert>
#include <iostream>
#include <cmath>
#include <random>
struct Clock : MonotonicMillis64 {uint64_t now=0;bool good=true;bool read(uint64_t& out) override {if(!good)return false;out=now;return true;}};
int main(){
 Clock clock;using B=TrustedTimeWindow::Bounds;B bounds{7,9};
 TrustedTimeWindow t(clock,10000,1000);const uint64_t epoch=1800000000ULL;
 assert(!t.bounds(bounds) && bounds.earliestMs==7);
 clock.now=2100;assert(t.acceptVerified(epoch,100,2100));assert(t.bounds(bounds));
 assert(bounds.earliestMs==epoch*1000 && bounds.latestMs==epoch*1000+3006);
 clock.now=3100;assert(t.bounds(bounds));
 assert(bounds.earliestMs==epoch*1000+997 && bounds.latestMs==epoch*1000+4007);
 clock.now=12099;assert(t.bounds(bounds));clock.now=12100;assert(!t.bounds(bounds));
 clock.now=20000;assert(t.acceptVerified(epoch,19000,20000));clock.now=19999;assert(!t.bounds(bounds));
 clock.now=20000;assert(t.acceptVerified(epoch,19000,20000));clock.good=false;assert(!t.bounds(bounds));clock.good=true;assert(!t.bounds(bounds));
 assert(!t.acceptVerified(epoch,15000,20000));assert(!t.bounds(bounds));
 assert(!t.acceptVerified(epoch,20000,19999));assert(!t.acceptVerified(1,19000,20000));
 TrustedTimeWindow zero(clock,0,0),tooLong(clock,86400001,0),badDrift(clock,1000,10000);
 assert(!zero.acceptVerified(epoch,19000,20000) && !tooLong.acceptVerified(epoch,19000,20000) && !badDrift.acceptVerified(epoch,19000,20000));
 // A 64-bit monotonic clock continues through the legacy 32-bit millis wrap.
 clock.now=static_cast<uint64_t>(UINT32_MAX)+10;assert(t.acceptVerified(epoch,static_cast<uint64_t>(UINT32_MAX)-10,clock.now));
 clock.now+=100;assert(t.bounds(bounds));assert(bounds.earliestMs>=epoch*1000+98);
 t.invalidate();assert(!t.bounds(bounds));
 // Deterministic simulation of signed second rounding, ms quantization, path
 // delay and a range of fast/slow monotonic clocks under the declared bound.
 std::mt19937 rng(42);
 for(unsigned i=0;i<20000;++i){
  const double start=1000.0+(rng()%1000)/1000.0;
  const double trip=1.0+(rng()%4000000)/1000.0;
  const double server=start+trip*(rng()%1001)/1000.0;
  const double end=start+trip;
  const double observation=end+(rng()%9000000)/1000.0;
  const int ppm=static_cast<int>(rng()%2001)-1000;
  const double rate=1.0+ppm/1000000.0;
  Clock simulated;simulated.now=static_cast<uint64_t>(std::floor(observation*rate));
  TrustedTimeWindow trial(simulated,10000,1000);
  const uint64_t issued=static_cast<uint64_t>(std::floor((epoch*1000.0+server)/1000.0));
  assert(trial.acceptVerified(issued,static_cast<uint64_t>(std::floor(start*rate)),static_cast<uint64_t>(std::floor(end*rate))));
  B range{};assert(trial.bounds(range));
  const double actual=epoch*1000.0+observation;
  assert(static_cast<double>(range.earliestMs)<=actual && static_cast<double>(range.latestMs)>=actual);
 }
 std::cout<<"trusted time uncertainty and expiry tests passed\n";
}
