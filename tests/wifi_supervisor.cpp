#include "../firmware/include/WifiSupervisor.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
struct Station : WifiStation {
  int starts=0,disconnects=0;bool online=false,allow=true;
  bool start(const char* ssid,const char* password) override {++starts;assert(strcmp(ssid,"network")==0);assert(strcmp(password,"password")==0);return allow;}
  bool connected() override {return online;}
  void disconnect() override {++disconnects;}
};
using S=WifiSupervisor::State;
int main() {
  Station st;WifiSupervisor w(st,"network","password");assert(w.state()==S::Idle);
  w.step(0);assert(st.starts==1 && w.state()==S::Connecting);
  w.step(19999);assert(st.disconnects==0);w.step(20000);assert(st.disconnects==1 && w.state()==S::Backoff);
  w.step(24999);assert(st.starts==1);w.step(25000);assert(st.starts==2 && w.state()==S::Connecting);
  st.online=true;w.step(25001);assert(w.state()==S::Online);
  st.online=false;w.step(25002);assert(w.state()==S::Backoff && st.disconnects==2);
  st.allow=false;w.step(30002);assert(st.starts==3 && st.disconnects==3 && w.state()==S::Backoff);
  w.step(35001);assert(st.starts==3);w.step(35002);assert(st.starts==4);
  Station unused;
  WifiSupervisor noSsid(unused,"","password"), noPass(unused,"network","short"), missing(unused,nullptr,"password");
  std::string longName(33,'x');WifiSupervisor longSsid(unused,longName.c_str(),"password");
  std::string bad64(64,'g');WifiSupervisor badKey(unused,"network",bad64.c_str());
  assert(noSsid.state()==S::InvalidConfiguration && noPass.state()==S::InvalidConfiguration && missing.state()==S::InvalidConfiguration);
  assert(longSsid.state()==S::InvalidConfiguration && badKey.state()==S::InvalidConfiguration);
  noSsid.step(0);assert(unused.starts==0);
  WifiSupervisor open(unused,"network","");assert(open.state()==S::Idle);
  std::string good64(64,'A');WifiSupervisor psk(unused,"network",good64.c_str());assert(psk.state()==S::Idle);
  Station rollover;WifiSupervisor wrap(rollover,"network","password");wrap.step(UINT32_MAX-10);
  wrap.step(19988);assert(wrap.state()==S::Connecting);wrap.step(19989);assert(wrap.state()==S::Backoff);
  std::cout<<"Wi-Fi supervisor tests passed\n";
}
