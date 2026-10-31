#include "../firmware/include/EspWifiStation.h"
#include <cassert>
#include <iostream>
FakeWifi WiFi;
int mock_ps_set_result=ESP_OK,mock_ps_get_result=ESP_OK,mock_ps_set_calls=0;
wifi_ps_type_t mock_ps_value=WIFI_PS_MIN_MODEM;
int main() {
  EspWifiStation station;
  assert(station.start("network","password"));
  assert(!WiFi.persistentValue && !WiFi.autoReconnectValue && WiFi.modeCalls==1 && WiFi.beginCalls==1);
  assert(station.powerSaveDiagnostic()==1 && mock_ps_value==WIFI_PS_NONE && mock_ps_set_calls==1);
  assert(station.connected());station.disconnect();assert(WiFi.disconnectCalls==1);
  WiFi.modeResult=false;assert(!station.start("network","password") && WiFi.beginCalls==1);
  WiFi.modeResult=true;WiFi.reconnectResult=false;assert(!station.start("network","password") && WiFi.beginCalls==1);
  WiFi.reconnectResult=true;WiFi.beginResult=WL_CONNECT_FAILED;assert(!station.start("network","password") && WiFi.beginCalls==2);
  WiFi.beginResult=0;mock_ps_set_result=-1;mock_ps_value=WIFI_PS_MIN_MODEM;
  assert(station.start("network","password"));assert(station.powerSaveDiagnostic()==-1);
  mock_ps_set_result=ESP_OK;mock_ps_get_result=-1;
  assert(station.start("network","password"));assert(station.powerSaveDiagnostic()==-1);
  mock_ps_get_result=ESP_OK;assert(station.start("network","password"));assert(station.powerSaveDiagnostic()==1);
  std::cout<<"ESP Wi-Fi station mock tests passed\n";
}
