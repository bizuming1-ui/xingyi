// Compile-only integration of the components; do not flash as production firmware.
#include <Arduino.h>
#include <Preferences.h> // Expose dependency to PlatformIO Library Dependency Finder.
#include "../../../firmware/include/ButtonCounter.h"
#include "../../../firmware/include/PreferencesCounterStorage.h"

ButtonCounter button;
PreferencesCounterStorage storage;
DurableCounter counter(storage);
volatile uint32_t observed = 0;
volatile bool compileOnlyExercise = false;

void setup() {
  pinMode(4, INPUT_PULLUP);
  if (storage.begin()) (void)counter.load();
}
void loop() {
  observed += button.tick(digitalRead(4) == LOW, millis());
  // Force real ESP32 compilation/linking of the unfinished component API.
  // This branch defaults to false; this project is still not a usable firmware.
  if (compileOnlyExercise) {
    (void)counter.bind("00000000-0000-4000-8000-000000000001", 1,
                       "20000000-0000-4000-8000-000000000001",
                       "10000000-0000-4000-8000-000000000001");
    (void)counter.add(1);
    (void)counter.acknowledge("10000000-0000-4000-8000-000000000001", 1);
    (void)counter.beginFreeze("30000000-0000-4000-8000-000000000001");
    observed += counter.canReportFrozen() ? 1 : 0;
    (void)counter.finishFreeze("30000000-0000-4000-8000-000000000001", DurableCounter::TerminalOutcome::Cancelled);
  }
  delay(1);
}
