/*
  Regression sketch for the mixed timer/software analogWrite() wraparound case.

  It drives five D0-D4 pins through the timer-backed per-pin frequency path,
  and ten D6-D15 pins through the software PWM fallback, while all channels
  ramp from 0..255 together. This is intended for scope/LPF checks around the
  255 -> 0 wrap.
*/

static const uint8_t kTimerPins[] = {PIN_D0, PIN_D1, PIN_D2, PIN_D3, PIN_D4};
static const uint32_t kTimerHz[] = {1000, 1200, 1300, 1400, 2000};
static const uint8_t kSoftPins[] = {
  PIN_D6, PIN_D7, PIN_D8, PIN_D9, PIN_D10,
  PIN_D11, PIN_D12, PIN_D13, PIN_D14, PIN_D15
};
static const uint32_t kSoftHz = 1000;

void setup() {
  analogWriteResolution(8);
  analogWriteFrequency(kSoftHz);

  for (unsigned int i = 0; i < (sizeof(kTimerPins) / sizeof(kTimerPins[0])); ++i) {
    pinMode(kTimerPins[i], OUTPUT);
    analogWritePinFrequency(kTimerPins[i], kTimerHz[i]);
  }

  for (unsigned int i = 0; i < (sizeof(kSoftPins) / sizeof(kSoftPins[0])); ++i) {
    pinMode(kSoftPins[i], OUTPUT);
  }
}

void loop() {
  for (int duty = 0; duty <= 255; ++duty) {
    for (unsigned int i = 0; i < (sizeof(kTimerPins) / sizeof(kTimerPins[0])); ++i) {
      analogWrite(kTimerPins[i], duty);
    }
    for (unsigned int i = 0; i < (sizeof(kSoftPins) / sizeof(kSoftPins[0])); ++i) {
      analogWrite(kSoftPins[i], duty);
    }
    delay(1);
  }
}
