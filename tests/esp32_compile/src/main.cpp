// Compile-only integration of the components; do not flash as production firmware.
#include <Arduino.h>
#include <ArduinoJson.h> // Expose dependency to PlatformIO Library Dependency Finder.
#include <Preferences.h> // Expose dependency to PlatformIO Library Dependency Finder.
#include "../../../firmware/include/ButtonCounter.h"
#include "../../../firmware/include/PreferencesCounterStorage.h"
#include "../../../firmware/include/SyncPlanner.h"
#include "../../../firmware/include/RpcCodec.h"
#include "../../../firmware/include/RpcWorker.h"
#include "../../../firmware/include/FreeRtosSyncMailbox.h"

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
FixtureExchange fixtureExchange;
FreeRtosSyncMailbox mailbox;
SyncController* controller = nullptr;
SyncNetworkWorker networkWorker(mailbox,fixtureExchange,"0000000000000000000000000000000000000000000000000000000000000000");
FixtureEpochs epochs;
SyncPlanner* planner = nullptr;
volatile uint32_t observed = 0;
volatile bool compileOnlyExercise = false;

void setup() {
  pinMode(4, INPUT_PULLUP);
  if (storage.begin() && counter.load() == DurableCounter::Result::Ok) { planner = new SyncPlanner(counter,epochs); if (mailbox.valid()) controller = new SyncController(*planner,mailbox); }
}
void loop() {
  observed += button.tick(digitalRead(4) == LOW, millis());
  // Force real ESP32 compilation/linking of the unfinished component API.
  // This branch defaults to false; this project is still not a usable firmware.
  if (compileOnlyExercise && planner && controller) {
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
