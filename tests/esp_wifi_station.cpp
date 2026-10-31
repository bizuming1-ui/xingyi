#include "../firmware/include/EspWifiStation.h"
#include <cassert>
#include <iostream>
FakeWifi WiFi;
uint32_t mock_millis=0;
int mock_ps_set_result=ESP_OK,mock_ps_get_result=ESP_OK,mock_ps_set_calls=0;
bool mock_ps_set_ignored=false;
wifi_ps_type_t mock_ps_value=WIFI_PS_MIN_MODEM;
int main() {
  EspWifiStation station;
  assert(station.start("network","password"));
  assert(!WiFi.persistentValue && !WiFi.autoReconnectValue && WiFi.modeCalls==1 && WiFi.beginCalls==1 && WiFi.sleepCalls==1);
  assert(station.powerSaveDiagnostic()==1 && mock_ps_value==WIFI_PS_NONE && mock_ps_set_calls==1);
  // Model asynchronous Arduino STA_START overwriting the initial IDF setting.
  mock_ps_value=WIFI_PS_MIN_MODEM;
  assert(station.connected());assert(station.powerSaveDiagnostic()==1 && mock_ps_set_calls==2);
  mock_ps_value=WIFI_PS_MIN_MODEM;mock_millis=14999;assert(station.connected());assert(mock_ps_set_calls==2);
  mock_millis=15000;assert(station.connected());assert(mock_ps_set_calls==3 && station.powerSaveDiagnostic()==1);
  // Setting fails but no connection is blocked; reported readback remains real.
  mock_ps_value=WIFI_PS_MIN_MODEM;mock_ps_set_result=-1;mock_millis=30000;
  assert(station.connected());assert(station.powerSaveDiagnostic()==0 && station.powerSaveSetResult()==-1);
  mock_ps_set_result=ESP_OK;mock_ps_set_ignored=true;mock_millis=45000;
  assert(station.connected());assert(station.powerSaveDiagnostic()==0 && station.powerSaveSetResult()==0);
  mock_ps_set_ignored=false;mock_ps_get_result=-1;mock_millis=60000;
  assert(station.connected());assert(station.powerSaveDiagnostic()==-1);
  mock_ps_get_result=ESP_OK;mock_millis=75000;assert(station.connected());assert(station.powerSaveDiagnostic()==1);
  station.disconnect();assert(WiFi.disconnectCalls==1);
  WiFi.modeResult=false;assert(!station.start("network","password") && WiFi.beginCalls==1);
  WiFi.modeResult=true;WiFi.reconnectResult=false;assert(!station.start("network","password") && WiFi.beginCalls==1);
  WiFi.reconnectResult=true;WiFi.beginResult=WL_CONNECT_FAILED;assert(!station.start("network","password") && WiFi.beginCalls==2);
  std::cout<<"ESP Wi-Fi station mock tests passed\n";
}
