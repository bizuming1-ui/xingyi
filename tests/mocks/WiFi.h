#pragma once
constexpr int WL_CONNECTED=3;
struct FakeWifi {
  int first=WL_CONNECTED, second=WL_CONNECTED, calls=0;
  int status() {return calls++==0?first:second;}
};
extern FakeWifi WiFi;
