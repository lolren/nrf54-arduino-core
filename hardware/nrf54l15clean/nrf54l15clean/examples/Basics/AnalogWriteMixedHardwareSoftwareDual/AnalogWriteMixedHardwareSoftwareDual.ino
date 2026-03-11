/*
  AnalogWriteMixedHardwareSoftwareDual

  Diagnostic sketch for mixed hardware/software PWM.

  Hardware:
  - Probe D0 (hardware PWM)
  - Probe D6 (software PWM fallback)

  Expected result:
  - D0 runs at 1000 Hz, 50% duty
  - D6 runs at 1000 Hz, 50% duty
*/

static const uint8_t kHardwarePin = PIN_D0;
static const uint8_t kSoftwarePin = PIN_D6;
static const uint32_t kFrequencyHz = 1000;
static const uint8_t kDuty = 128;

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(kHardwarePin, OUTPUT);
  pinMode(kSoftwarePin, OUTPUT);

  analogWriteResolution(8);
  analogWriteFrequency(kFrequencyHz);
  analogWrite(kHardwarePin, kDuty);
  analogWrite(kSoftwarePin, kDuty);

  Serial.println("D0 hardware PWM + D6 software PWM at 1000 Hz, 50% duty");
}

void loop() {
  delay(1000);
}
