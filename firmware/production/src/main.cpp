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
// Experimental longer budget for status and count upload only.
constexpr uint32_t DATA_RPC_TIMEOUT_MS=8000;
// Only for initial authenticated time; remains below the 5s proof limit.
constexpr uint32_t TIME_BOOTSTRAP_TIMEOUT_MS=4500;
static_assert(TIME_BOOTSTRAP_TIMEOUT_MS < TimeProof::MAX_ROUND_TRIP_MS,"proof timeout must remain bounded");
constexpr uint32_t NETWORK_STACK_BYTES=24576;
constexpr uint32_t STATUS_INTERVAL_MS=1000;
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
SyncPlanner* countPlanner=nullptr;
DeviceInput* input=nullptr;
NetworkTaskCoordinator* network=nullptr;
GuardedWifiSupervisor* networkWifi=nullptr;
AuthenticatedNetworkTime* networkTime=nullptr;
SyncNetworkWorker* networkWorker=nullptr;
HttpRpcExchange* networkExchange=nullptr;
std::atomic<bool> stopNetwork{false};
std::atomic<bool> radioOnline{false},timeTrusted{false};
std::atomic<int> timeStage{0},timeResult{2},timeDetail{0};
std::atomic<uint32_t> timeStart{0},timeTx{0},timeRx{0};
std::atomic<int> rpcKind{0},rpcResult{1},rpcStage{0},rpcNet{1},rpcHttp{0};
std::atomic<int> barrierRpcKind{0},barrierRpcResult{1},barrierRpcStage{0},barrierRpcNet{1},barrierRpcHttp{0};
bool initialized=false;
const char* startupFault="FAULT: initialization incomplete; counting stopped";
DeviceInput::Status priorStatus=DeviceInput::Status::Ready;
uint32_t lastStatus=0,lastDiagnostic=0,lastTlsIo=0,lastRpcDiag=0,lastFreezeDiag=0,lastBarrierRpcDiag=0;

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
    // Updates are informational only; they cannot prove a live freeze lease.
    const bool online=networkWifi->state()==WifiSupervisor::State::Online;
    radioOnline.store(online,std::memory_order_relaxed);
    timeTrusted.store(online && networkTime->trusted(),std::memory_order_relaxed);
    timeStage.store(networkTime->lastStage(),std::memory_order_relaxed);
    timeResult.store(networkTime->lastResult(),std::memory_order_relaxed);
    timeDetail.store(networkTime->lastDetail(),std::memory_order_relaxed);
    timeStart.store(networkTime->lastHandshakeStartMs(),std::memory_order_relaxed);
    timeTx.store(networkTime->lastTxBytes(),std::memory_order_relaxed);
    timeRx.store(networkTime->lastRxBytes(),std::memory_order_relaxed);
    rpcKind.store(static_cast<int>(networkWorker->lastKind()),std::memory_order_relaxed);
    rpcResult.store(static_cast<int>(networkWorker->lastOutcome().result),std::memory_order_relaxed);
    rpcStage.store(static_cast<int>(networkExchange->lastStage()),std::memory_order_relaxed);
    rpcNet.store(static_cast<int>(networkExchange->lastStatus()),std::memory_order_relaxed);
    rpcHttp.store(networkExchange->lastHttpStatus(),std::memory_order_relaxed);
    if(static_cast<int>(networkWorker->lastKind())>=static_cast<int>(SyncPlanner::Kind::Freeze)) {
      barrierRpcKind.store(static_cast<int>(networkWorker->lastKind()),std::memory_order_relaxed);
      barrierRpcResult.store(static_cast<int>(networkWorker->lastOutcome().result),std::memory_order_relaxed);
      barrierRpcStage.store(static_cast<int>(networkExchange->lastStage()),std::memory_order_relaxed);
      barrierRpcNet.store(static_cast<int>(networkExchange->lastStatus()),std::memory_order_relaxed);
      barrierRpcHttp.store(networkExchange->lastHttpStatus(),std::memory_order_relaxed);
    }
    vTaskDelay(1);
  }
  vTaskDelete(nullptr);
}
const char* statusName(DeviceInput::Status s) {
  switch(s) {
    case DeviceInput::Status::Ready:return "STATUS: ready; counts may be pending";
    case DeviceInput::Status::Unbound:return "STATUS: unbound; presses rejected";
    case DeviceInput::Status::ReleaseRequired:return "STATUS: release button before counting";
    case DeviceInput::Status::StorageError:return "FAULT: NVS error; do not erase";
    case DeviceInput::Status::CapacityFull:return "FAULT: counter full; stopped";
    case DeviceInput::Status::Fault:return "FAULT: sync stopped; do not erase NVS";
  }
  return "FAULT: unknown input state";
}
}
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN,INPUT_PULLUP);
  if(!config.valid() || !validCa(config.ca)) {startupFault="FAULT: check local config or CA";status(startupFault);return;}
  if(!storage.begin() || counter.load()!=DurableCounter::Result::Ok) {startupFault="FAULT: NVS load failed; do not erase";status(startupFault);return;}
  static FreeRtosRadioLifecycleLock lock;
  static FreeRtosSyncMailbox mailbox;
  if(!lock.valid() || !mailbox.valid()) {startupFault="FAULT: task primitives unavailable";status(startupFault);return;}
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
  static HttpRpcExchange exchange(channel,config.host,config.key,NETWORK_TIMEOUT_MS,DATA_RPC_TIMEOUT_MS);
  static SyncNetworkWorker worker(mailbox,exchange,config.secret);
  static TimeProof proof(networkEntropy);static TimeBootstrapExchange bootstrap(clock,scheduler,tcp,resolved,proof);
  static TimeWindowSynchronizer sync(monotonic,window,bootstrap);
  static AuthenticatedNetworkTime time(window,sync,config.host,config.key,config.ca,config.deviceId,config.secret,TIME_BOOTSTRAP_TIMEOUT_MS);
  static NetworkTaskCoordinator coordinator(clock,wifi,time,worker);
  controller=&countController;countPlanner=&planner;input=&button;network=&coordinator;networkWifi=&wifi;networkTime=&time;networkWorker=&worker;networkExchange=&exchange;
  if(controller->faulted()) {startupFault="FAULT: initial state invalid";status(startupFault);return;}
  TaskHandle_t handle=nullptr;
  if(xTaskCreatePinnedToCore(networkTask,"xingyi-net",NETWORK_STACK_BYTES,nullptr,1,&handle,0)!=pdPASS || !handle) {
    startupFault="FAULT: network task unavailable";status(startupFault);return;
  }
  initialized=true;
  status("Test candidate started; do not use in production");
}
void loop() {
  if(!initialized){const uint32_t now=millis();if(now-lastStatus>=5000){lastStatus=now;status(startupFault);}delay(100);return;}
  const uint32_t now=millis();
  const auto sample=input->tick(digitalRead(BUTTON_PIN)==LOW,now);
  controller->step(now);
  if(controller->faulted() || sample.status==DeviceInput::Status::StorageError || sample.status==DeviceInput::Status::CapacityFull || sample.status==DeviceInput::Status::Fault)
    stopNetwork.store(true,std::memory_order_release);
  const bool changed=sample.status!=priorStatus;
  if(changed) {
    priorStatus=sample.status;lastStatus=now;
    status(controller->faulted()?"FAULT: sync stopped; do not erase NVS":statusName(sample.status));
  } else if(now-lastStatus>=STATUS_INTERVAL_MS) {
    lastStatus=now;
    if(controller->faulted()) status("FAULT: sync stopped; do not erase NVS");
    else {
      char info[64];
      snprintf(info,sizeof(info),"WIFI=%d TIME=%d BOUND=%d PENDING=%llu",radioOnline.load()?1:0,timeTrusted.load()?1:0,counter.isReady()?1:0,static_cast<unsigned long long>(counter.pending()));
      status(info);
    }
  }
  // Emit only numeric failure stage/result/detail, never credentials or data.
  // Offset from the main status line so a small serial buffer can drain.
  if(radioOnline.load() && !timeTrusted.load() && now-lastDiagnostic>=5000 && now-lastStatus>=400 && now-lastStatus<600) {
    lastDiagnostic=now;char diag[64];
    snprintf(diag,sizeof(diag),"TIME_DIAG stage=%d result=%d code=%d",timeStage.load(),timeResult.load(),timeDetail.load());
    status(diag);
  }
  if(radioOnline.load() && !timeTrusted.load() && now-lastTlsIo>=5000 && now-lastStatus>=700 && now-lastStatus<900) {
    lastTlsIo=now;char info[64];
    snprintf(info,sizeof(info),"TLS_IO start=%lu tx=%lu rx=%lu",static_cast<unsigned long>(timeStart.load()),static_cast<unsigned long>(timeTx.load()),static_cast<unsigned long>(timeRx.load()));
    status(info);
  }
  if(radioOnline.load() && now-lastRpcDiag>=5000 && now-lastStatus>=150 && now-lastStatus<300) {
    lastRpcDiag=now;char info[64];
    snprintf(info,sizeof(info),"RPC_DIAG k=%d r=%d s=%d n=%d h=%d",rpcKind.load(),rpcResult.load(),rpcStage.load(),rpcNet.load(),rpcHttp.load());
    status(info);
  }
  if(radioOnline.load() && now-lastFreezeDiag>=5000 && now-lastStatus>=570 && now-lastStatus<670) {
    lastFreezeDiag=now;char info[64];
    snprintf(info,sizeof(info),"FREEZE b=%d f=%d c=%d i=%d",static_cast<int>(countPlanner->diagnosticBarrier()),counter.isFrozen()?1:0,counter.canReportFrozen()?1:0,static_cast<int>(countPlanner->diagnosticInFlight()));
    status(info);
  }
  if(radioOnline.load() && barrierRpcKind.load()!=0 && now-lastBarrierRpcDiag>=5000 && now-lastStatus>=850 && now-lastStatus<950) {
    lastBarrierRpcDiag=now;char info[64];
    snprintf(info,sizeof(info),"BAR_RPC k=%d r=%d s=%d n=%d h=%d",barrierRpcKind.load(),barrierRpcResult.load(),barrierRpcStage.load(),barrierRpcNet.load(),barrierRpcHttp.load());
    status(info);
  }
  delay(1);
}
