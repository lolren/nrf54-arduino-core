/*
  AnalogWritePerPinFrequencySameFreqDual

  Diagnostic sketch for checking two custom-frequency pins at the same carrier.

  Hardware:
  - Probe D0 and D1

  Expected result:
  - D0 runs at 1000 Hz while duty ramps 0..255
  - D1 runs at 1000 Hz while duty ramps 0..255
*/

static const uint8_t kPinA = PIN_D0;
static const uint8_t kPinB = PIN_D1;
static const uint32_t kCarrierHz = 1000;
static const uint16_t kStepDelayMs = 2;

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(kPinA, OUTPUT);
  pinMode(kPinB, OUTPUT);

  analogWriteResolution(8);
  analogWritePinFrequency(kPinA, kCarrierHz);
  analogWritePinFrequency(kPinB, kCarrierHz);
  Serial.println("D0 and D1 both ramp at 1000 Hz");
}

void loop() {
  for (int duty = 0; duty <= 255; ++duty) {
    analogWrite(kPinA, duty);
    analogWrite(kPinB, duty);
    delay(kStepDelayMs);
  }
}
