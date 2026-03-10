/*
  AnalogWritePerPinFrequency

  Demonstrates timer-backed per-pin PWM frequency on D5.

  Notes for XIAO nRF54L15:
  - analogWritePinFrequency(pin, hz) is intended for D0-D5.
  - It uses a dedicated TIMER + GPIOTE + DPPIC path per active pin.
  - D6-D9 use software PWM fallback instead.
*/

static const uint8_t kPwmPin = PIN_D5;
static const uint8_t kDuty = 128;
static const uint32_t kSlowHz = 2;
static const uint32_t kFastHz = 8;
static const uint32_t kHoldMs = 4000;

static void applyFrequency(uint32_t hz) {
  analogWritePinFrequency(kPwmPin, hz);
  analogWrite(kPwmPin, kDuty);

  Serial.print("D5 per-pin PWM frequency: ");
  Serial.print(hz);
  Serial.println(" Hz");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  analogWriteResolution(8);
  applyFrequency(kSlowHz);
}

void loop() {
  static uint32_t lastChangeMs = millis();
  static bool fast = false;

  const uint32_t now = millis();
  if ((now - lastChangeMs) < kHoldMs) {
    return;
  }

  lastChangeMs = now;
  fast = !fast;
  applyFrequency(fast ? kFastHz : kSlowHz);
}
