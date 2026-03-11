/*
  AnalogWritePerPinFrequencyScope

  Scope-oriented stress test for timer-backed per-pin PWM.

  Hardware:
  - Probe D0, D1, D2, and D3 as needed

  Expected result:
  - D0 = 1000 Hz, 50% duty
  - D1 = 1200 Hz, 50% duty
  - D2 = 2000 Hz, 50% duty
  - D3 = 4000 Hz, 50% duty

  Notes for XIAO nRF54L15:
  - analogWritePinFrequency(pin, hz) is intended for D0-D5.
  - It uses the timer-backed per-pin PWM path.
*/

static const uint8_t kPins[] = {PIN_D0, PIN_D1, PIN_D2, PIN_D3};
static const uint32_t kFrequencies[] = {1000, 1200, 2000, 4000};
static const uint8_t kDuty = 128;

void setup() {
  Serial.begin(115200);
  delay(200);

  analogWriteResolution(8);

  for (size_t i = 0; i < (sizeof(kPins) / sizeof(kPins[0])); ++i) {
    analogWritePinFrequency(kPins[i], kFrequencies[i]);
    analogWrite(kPins[i], kDuty);
  }

  Serial.println("D0=1000Hz D1=1200Hz D2=2000Hz D3=4000Hz");
}

void loop() {
  delay(1000);
}
