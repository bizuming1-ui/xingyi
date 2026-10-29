// Compile-only integration of the components; do not flash as production firmware.
#include <Arduino.h>
#include <Preferences.h> // Expose dependency to PlatformIO Library Dependency Finder.
#include "../../../firmware/include/ButtonCounter.h"
#include "../../../firmware/include/PreferencesCounterStorage.h"

ButtonCounter button;
PreferencesCounterStorage storage;
DurableCounter counter(storage);
volatile uint32_t observed = 0;

void setup() {
  pinMode(4, INPUT_PULLUP);
  if (storage.begin()) (void)counter.load();
}
void loop() {
  observed += button.tick(digitalRead(4) == LOW, millis());
  delay(1);
}
