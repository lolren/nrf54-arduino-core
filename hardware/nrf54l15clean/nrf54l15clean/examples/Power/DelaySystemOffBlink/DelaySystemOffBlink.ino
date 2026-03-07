// Briefly pulse the LED, then enter timed SYSTEM OFF.
//
// delaySystemOff(ms) preserves .noinit RAM by default.
// For the absolute lowest current, switch to delaySystemOffNoRetention(ms).

static constexpr unsigned long kBlinkOnMs = 12UL;
static constexpr unsigned long kSleepMs = 1000UL;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(kBlinkOnMs);
  digitalWrite(LED_BUILTIN, LOW);

  delaySystemOff(kSleepMs);
}
