#pragma once
#include <cstring>
constexpr int WL_CONNECTED=3;
constexpr int WL_CONNECT_FAILED=4;
constexpr int WIFI_STA=1;
struct FakeWifi {
  int first=WL_CONNECTED, second=WL_CONNECTED, calls=0;
  int modeCalls=0,beginCalls=0,disconnectCalls=0;bool persistentValue=true,autoReconnectValue=true;
  bool modeResult=true,reconnectResult=true;int beginResult=0;
  int status() {return calls++==0?first:second;}
  void persistent(bool value){persistentValue=value;}
  bool mode(int value){++modeCalls;return value==WIFI_STA && modeResult;}
  bool setAutoReconnect(bool value){autoReconnectValue=value;return reconnectResult;}
  int begin(const char* ssid,const char* pass){++beginCalls;return (ssid && pass)?beginResult:WL_CONNECT_FAILED;}
  bool disconnect(bool off,bool erase){++disconnectCalls;return !off && !erase;}
};
extern FakeWifi WiFi;
