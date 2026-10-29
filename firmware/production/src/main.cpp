// Candidate integration firmware: do not deploy to production until the real
// service, timing, NVS, freeze and hardware behavior have been validated.
#include <Arduino.h>
#include <ArduinoJson.h> // PlatformIO library discovery
#include <Preferences.h>
#include <WiFi.h>
#include <mbedtls/x509_crt.h>
#include <atomic>
#include "config.local.h"
#include "RuntimeConfig.h"
#include "PreferencesCounterStorage.h"
#include "FreeRtosSyncMailbox.h"
#include "DeviceInput.h"
#include "EspWifiEntropy.h"
#include "EspWifiStation.h"
#include "EspAsyncDnsBackend.h"
#include "EspTcpOps.h"
#include "ResolvedTcpConnect.h"
#include "MbedTlsSession.h"
#include "VerifiedConnect.h"
#include "MbedTlsChannel.h"
#include "EspMonotonicMillis.h"
#include "FreeRtosRadioLifecycleLock.h"
#include "AuthenticatedNetworkTime.h"

namespace {
constexpr int BUTTON_PIN=4;
constexpr uint32_t NETWORK_TIMEOUT_MS=3000;
constexpr uint32_t NETWORK_STACK_BYTES=24576;
RuntimeConfig config{WIFI_SSID,WIFI_PASSWORD,SUPABASE_HOST,SUPABASE_PUBLISHABLE_KEY,
 DEVICE_ID,DEVICE_SECRET,SUPABASE_CA_PEM,TIME_LIFETIME_MS,CLOCK_DRIFT_PPM};
class Clock final : public ConnectionClock, public NetworkTaskClock {
 public: uint32_t nowMillis() override {return millis();}
};
class Yield final : public NetworkYield {
 public: void yield() override {vTaskDelay(1);}
};
PreferencesCounterStorage storage;
DurableCounter counter(storage);
// Construct task and hardware-dependent objects in setup, after scheduler start.
SyncController* controller=nullptr;
DeviceInput* input=nullptr;
NetworkTaskCoordinator* network=nullptr;
std::atomic<bool> stopNetwork{false};
bool initialized=false;
const char* startupFault="FAULT: initialization incomplete; counting stopped";
DeviceInput::Status priorStatus=DeviceInput::Status::Ready;
uint32_t lastStatus=0;

// Console output is best-effort and never waits for an attached terminal. No
// secret, SSID, device ID, production-order UUID or request body is printed.
void status(const char* message) {
  if(Serial.availableForWrite()>=static_cast<int>(strlen(message)+2))Serial.println(message);
}
bool validCa(const char* pem) {
  mbedtls_x509_crt cert;mbedtls_x509_crt_init(&cert);
  const int result=mbedtls_x509_crt_parse(&cert,reinterpret_cast<const unsigned char*>(pem),strlen(pem)+1);
  mbedtls_x509_crt_free(&cert);return result==0;
}
void networkTask(void*) {
  while(!stopNetwork.load(std::memory_order_acquire)) {
    network->step();
    vTaskDelay(1);
  }
  vTaskDelete(nullptr);
}
const char* statusName(DeviceInput::Status s) {
  switch(s) {
    case DeviceInput::Status::Ready:return "STATUS: ready; local counts may still be pending";
    case DeviceInput::Status::Unbound:return "STATUS: no binding; presses rejected";
    case DeviceInput::Status::ReleaseRequired:return "STATUS: release button before counting";
    case DeviceInput::Status::StorageError:return "FAULT: storage failure; counting stopped; do not erase NVS";
    case DeviceInput::Status::CapacityFull:return "FAULT: counter full; counting stopped";
    case DeviceInput::Status::Fault:return "FAULT: synchronization state; counting stopped; do not erase NVS";
  }
  return "FAULT: unknown input state";
}
}
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN,INPUT_PULLUP);
  if(!config.valid() || !validCa(config.ca)) {startupFault="FAULT: incomplete or invalid local configuration; counting stopped";status(startupFault);return;}
  if(!storage.begin() || counter.load()!=DurableCounter::Result::Ok) {startupFault="FAULT: NVS load failed; counting stopped; do not erase NVS";status(startupFault);return;}
  static FreeRtosRadioLifecycleLock lock;
  static FreeRtosSyncMailbox mailbox;
  if(!lock.valid() || !mailbox.valid()) {startupFault="FAULT: task primitives unavailable; counting stopped";status(startupFault);return;}
  static EspWifiEntropy hardwareEntropy;
  static GuardedEntropy countEntropy(lock,hardwareEntropy), networkEntropy(lock,hardwareEntropy);
  static RandomEpochSource epochs(countEntropy);
  static SyncPlanner planner(counter,epochs);
  static SyncController countController(planner,mailbox);
  static DeviceInput button(counter,planner,digitalRead(BUTTON_PIN)==LOW,millis());
  static Clock clock;static Yield scheduler;static EspMonotonicMillis monotonic;
  static EspWifiStation station;static WifiSupervisor supervisor(station,config.ssid,config.password);
  static GuardedWifiSupervisor wifi(lock,supervisor);
  static EspAsyncDnsBackend dnsBackend;static AsyncDns dns(dnsBackend);
  static EspTcpOps ops;static NonblockingTcp tcp(ops);static ResolvedTcpConnect resolved(dns,tcp);
  static MbedTlsSession tls(tcp);static VerifiedConnect verified(clock,tcp,resolved,tls);
  static TrustedTimeWindow window(monotonic,config.timeLifetimeMs,config.clockDriftPpm);
  static MbedTlsChannel channel(clock,scheduler,window,verified,tls,config.ca);
  static HttpRpcExchange exchange(channel,config.host,config.key,NETWORK_TIMEOUT_MS);
  static SyncNetworkWorker worker(mailbox,exchange,config.secret);
  static TimeProof proof(networkEntropy);static TimeBootstrapExchange bootstrap(clock,scheduler,tcp,resolved,proof);
  static TimeWindowSynchronizer sync(monotonic,window,bootstrap);
  static AuthenticatedNetworkTime time(window,sync,config.host,config.key,config.ca,config.deviceId,config.secret,NETWORK_TIMEOUT_MS);
  static NetworkTaskCoordinator coordinator(clock,wifi,time,worker);
  controller=&countController;input=&button;network=&coordinator;
  if(controller->faulted()) {startupFault="FAULT: initial state invalid; counting stopped";status(startupFault);return;}
  TaskHandle_t handle=nullptr;
  if(xTaskCreatePinnedToCore(networkTask,"xingyi-net",NETWORK_STACK_BYTES,nullptr,1,&handle,0)!=pdPASS || !handle) {
    startupFault="FAULT: network task unavailable; counting stopped";status(startupFault);return;
  }
  initialized=true;
  status("Candidate firmware started; serial status is not production monitoring");
}
void loop() {
  if(!initialized){const uint32_t now=millis();if(now-lastStatus>=5000){lastStatus=now;status(startupFault);}delay(100);return;}
  const uint32_t now=millis();
  const auto sample=input->tick(digitalRead(BUTTON_PIN)==LOW,now);
  controller->step(now);
  if(controller->faulted() || sample.status==DeviceInput::Status::StorageError || sample.status==DeviceInput::Status::CapacityFull || sample.status==DeviceInput::Status::Fault)
    stopNetwork.store(true,std::memory_order_release);
  if(sample.status!=priorStatus || now-lastStatus>=5000) {
    priorStatus=sample.status;lastStatus=now;
    status(controller->faulted()?"FAULT: synchronization state; counting stopped; do not erase NVS":statusName(sample.status));
  }
  delay(1);
}
