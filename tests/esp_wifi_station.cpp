#include "../firmware/include/EspWifiStation.h"
#include <cassert>
#include <iostream>
FakeWifi WiFi;
int main() {
  EspWifiStation station;
  assert(station.start("network","password"));
  assert(!WiFi.persistentValue && !WiFi.autoReconnectValue && WiFi.modeCalls==1 && WiFi.beginCalls==1);
  assert(station.connected());station.disconnect();assert(WiFi.disconnectCalls==1);
  WiFi.modeResult=false;assert(!station.start("network","password") && WiFi.beginCalls==1);
  WiFi.modeResult=true;WiFi.reconnectResult=false;assert(!station.start("network","password") && WiFi.beginCalls==1);
  WiFi.reconnectResult=true;WiFi.beginResult=WL_CONNECT_FAILED;assert(!station.start("network","password") && WiFi.beginCalls==2);
  std::cout<<"ESP Wi-Fi station mock tests passed\n";
}
