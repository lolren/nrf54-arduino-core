/*
  AnalogWritePerPinFrequencyRampDual

  Regression test for live duty updates on two custom-frequency pins.

  Hardware:
  - Probe D0 and D1

  Expected result:
  - D0 stays at 1000 Hz while duty ramps 0..255
  - D1 stays at 1200 Hz while duty ramps 0..255

  Notes for XIAO nRF54L15:
  - analogWritePinFrequency(pin, hz) is intended for D0-D5.
  - This sketch matches the dynamic-update case reported on issue #20.
*/

static const uint8_t kPinA = PIN_D0;
static const uint8_t kPinB = PIN_D1;
static const uint32_t kPinAHertz = 1000;
static const uint32_t kPinBHertz = 1200;
static const uint16_t kStepDelayMs = 2;

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(kPinA, OUTPUT);
  pinMode(kPinB, OUTPUT);

  analogWriteResolution(8);
  analogWritePinFrequency(kPinA, kPinAHertz);
  analogWritePinFrequency(kPinB, kPinBHertz);

  Serial.println("D0 ramps at 1000 Hz, D1 ramps at 1200 Hz");
}

void loop() {
  for (int duty = 0; duty <= 255; ++duty) {
    analogWrite(kPinA, duty);
    analogWrite(kPinB, duty);
    delay(kStepDelayMs);
  }
}
