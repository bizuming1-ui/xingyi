// Compile-only integration of the components; do not flash as production firmware.
#include <Arduino.h>
#include <ArduinoJson.h> // Expose dependency to PlatformIO Library Dependency Finder.
#include <Preferences.h> // Expose dependency to PlatformIO Library Dependency Finder.
#include <WiFi.h> // Expose Wi-Fi dependency to PlatformIO Library Dependency Finder.
#include "../../../firmware/include/ButtonCounter.h"
#include "../../../firmware/include/PreferencesCounterStorage.h"
#include "../../../firmware/include/SyncPlanner.h"
#include "../../../firmware/include/RpcCodec.h"
#include "../../../firmware/include/RpcWorker.h"
#include "../../../firmware/include/FreeRtosSyncMailbox.h"
#include "../../../firmware/include/HttpResponseDecoder.h"
#include "../../../firmware/include/HttpRpcExchange.h"
#include "../../../firmware/include/EspWifiEntropy.h"
#include "../../../firmware/include/EspWifiStation.h"
#include "../../../firmware/include/DeviceInput.h"
#include "../../../firmware/include/EspAsyncDnsBackend.h"
#include "../../../firmware/include/EspTcpOps.h"
#include "../../../firmware/include/ResolvedTcpConnect.h"
#include "../../../firmware/include/MbedTlsSession.h"
#include "../../../firmware/include/VerifiedConnect.h"
#include "../../../firmware/include/MbedTlsChannel.h"
#include "../../../firmware/include/TimeProof.h"
#include "../../../firmware/include/TimeProofCodec.h"
#include "../../../firmware/include/TimeBootstrapExchange.h"
#include "../../../firmware/include/EspMonotonicMillis.h"
#include "../../../firmware/include/TimeWindowSynchronizer.h"

ButtonCounter button;
PreferencesCounterStorage storage;
DurableCounter counter(storage);
class FixtureEpochs : public EpochSource {
 public:
  bool generate(char out[37]) override { memcpy(out,"10000000-0000-4000-8000-000000000001",37); return true; }
};
class FixtureExchange : public RpcExchange {
 public:
  Response post(const char*,const char*,size_t,char*,size_t) override { return {}; }
};
class FixtureTls : public VerifiedTlsChannel {
 public:
  uint32_t nowMillis() override { return millis(); }
  Connect connectVerified(const char*,uint16_t,uint32_t) override { return Connect::Error; }
  int write(const char*,size_t,uint32_t) override { return -1; }
  int read(char*,size_t,uint32_t) override { return -1; }
  void close() override {}
};
FixtureTls fixtureTls;
HttpRpcExchange fixtureHttp(fixtureTls,"project123.supabase.co","sb_publishable_test",1000);
FixtureExchange fixtureExchange;
FreeRtosSyncMailbox mailbox;
SyncController* controller = nullptr;
DeviceInput* deviceInput = nullptr;
SyncNetworkWorker networkWorker(mailbox,fixtureExchange,"0000000000000000000000000000000000000000000000000000000000000000");
FixtureEpochs epochs;
EspTcpOps tcpOps;
NonblockingTcp rawTcp(tcpOps);
EspAsyncDnsBackend dnsBackend;
AsyncDns dns(dnsBackend);
ResolvedTcpConnect resolvedTcp(dns,rawTcp);
MbedTlsSession tlsSession(rawTcp);
class FixtureClock : public ConnectionClock {public: uint32_t nowMillis() override {return millis();}};
FixtureClock fixtureClock;
VerifiedConnect verifiedConnect(fixtureClock,rawTcp,resolvedTcp,tlsSession);
class FixtureNetworkYield : public NetworkYield {public: void yield() override {delay(1);}};
FixtureNetworkYield fixtureNetworkYield;
EspMonotonicMillis monotonicClock;
TrustedTimeWindow timeWindow(monotonicClock,1000,9999); // fixture only, not a deployed policy
MbedTlsChannel tlsChannel(fixtureClock,fixtureNetworkYield,timeWindow,verifiedConnect,tlsSession,"fixture CA");
HttpRpcExchange actualFixtureHttp(tlsChannel,"project123.supabase.co","sb_publishable_test",1000);
EspWifiStation wifiStation;
WifiSupervisor wifiSupervisor(wifiStation,"fixture-network","fixture-password");
EspWifiEntropy hardwareEntropy;
TimeProof timeProof(hardwareEntropy);
TimeBootstrapExchange timeBootstrap(fixtureClock,fixtureNetworkYield,rawTcp,resolvedTcp,timeProof);
TimeWindowSynchronizer timeSynchronizer(monotonicClock,timeWindow,timeBootstrap);
RandomEpochSource hardwareEpochs(hardwareEntropy);
SyncPlanner* planner = nullptr;
volatile uint32_t observed = 0;
volatile bool compileOnlyExercise = false;

void setup() {
  pinMode(4, INPUT_PULLUP);
  if (storage.begin() && counter.load() == DurableCounter::Result::Ok) { planner = new SyncPlanner(counter,epochs); if (mailbox.valid()) controller = new SyncController(*planner,mailbox); deviceInput = new DeviceInput(counter,*planner); }
}
void loop() {
  observed += button.tick(digitalRead(4) == LOW, millis());
  // Force real ESP32 compilation/linking of the unfinished component API.
  // This branch defaults to false; this project is still not a usable firmware.
  if (compileOnlyExercise && planner && controller && deviceInput) {
    (void)deviceInput->tick(digitalRead(4)==LOW,millis());
    AsyncDns::Handle dnsHandle{};uint8_t dnsAddress[4]{};
    if(dns.begin("project123.supabase.co",dnsHandle)){(void)dns.poll(dnsHandle,dnsAddress);(void)dns.expire(dnsHandle);}
    const uint8_t fixtureIp[]={127,0,0,1};
    if(rawTcp.start(fixtureIp,443,millis(),1000)){(void)rawTcp.poll(millis());rawTcp.close();}
    if(resolvedTcp.begin("project123.supabase.co",443,millis(),1000)){(void)resolvedTcp.poll(millis());resolvedTcp.close();}
    (void)tlsSession.begin("project123.supabase.co","fixture CA",false);
    (void)tlsSession.handshake();
    tlsSession.reset();
    if(verifiedConnect.begin("project123.supabase.co",443,"fixture CA",false,1000)){(void)verifiedConnect.poll();verifiedConnect.close();}
    wifiSupervisor.step(millis());
    uint64_t signedTime=0;
    if(timeProof.begin("00000000-0000-4000-8000-000000000001","0000000000000000000000000000000000000000000000000000000000000000",millis())) {
      char timeRequest[160]{};(void)TimeProofCodec::request(timeProof,timeRequest,sizeof(timeRequest));
      const char timeResponse[]="{\"unix_seconds\":\"1704067200\",\"tag\":\"0000000000000000000000000000000000000000000000000000000000000000\"}";
      (void)TimeProofCodec::response(timeProof,timeResponse,sizeof(timeResponse)-1,millis(),signedTime);
    }
    (void)timeBootstrap.fetch("project123.supabase.co","sb_publishable_test","fixture CA",
      "00000000-0000-4000-8000-000000000001","0000000000000000000000000000000000000000000000000000000000000000",1000,signedTime);
    (void)timeSynchronizer.refresh("project123.supabase.co","sb_publishable_test","fixture CA",
      "00000000-0000-4000-8000-000000000001","0000000000000000000000000000000000000000000000000000000000000000",1000);
    TrustedTimeWindow::Bounds bounds{};(void)timeWindow.bounds(bounds);
    char randomEpoch[37]{};
    (void)hardwareEpochs.generate(randomEpoch);
    char responseBuffer[32]{};
    HttpResponseDecoder httpResponse(responseBuffer,sizeof(responseBuffer));
    const char sample[]="HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
    (void)httpResponse.feed(sample,sizeof(sample)-1);
    (void)httpResponse.finish();
    (void)fixtureHttp.post("device_status","{}",2,responseBuffer,sizeof(responseBuffer));
    (void)actualFixtureHttp.post("device_status","{}",2,responseBuffer,sizeof(responseBuffer));
    controller->step(millis());
    networkWorker.step();
    (void)counter.bind("00000000-0000-4000-8000-000000000001", 1,
                       "20000000-0000-4000-8000-000000000001",
                       "10000000-0000-4000-8000-000000000001");
    auto command = planner->next(millis());
    SyncPlanner::Reply reply{};
    char fixtureBody[512]{};
    (void)RpcCodec::encode(command,"0000000000000000000000000000000000000000000000000000000000000000",fixtureBody,sizeof(fixtureBody));
    (void)RpcCodec::decode(command,"{}",2,reply);
    RpcCodec::wipe(fixtureBody,sizeof(fixtureBody));
    (void)RpcWorker::execute(fixtureExchange,command,"0000000000000000000000000000000000000000000000000000000000000000",reply);
    (void)planner->complete(command.ticket, reply, millis());
    (void)planner->count(1);
    (void)counter.add(1);
    (void)counter.acknowledge("10000000-0000-4000-8000-000000000001", 1);
    (void)counter.beginFreeze("30000000-0000-4000-8000-000000000001");
    observed += counter.canReportFrozen() ? 1 : 0;
    (void)counter.finishFreeze("30000000-0000-4000-8000-000000000001", DurableCounter::TerminalOutcome::Cancelled);
  }
  delay(1);
}
