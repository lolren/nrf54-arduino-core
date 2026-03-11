/*
  AnalogWritePerPinFrequencyDual

  Drives two hardware PWM pins at different custom frequencies at the same time.

  Hardware:
  - LED + resistor on D0
  - LED + resistor on D1

  Expected result:
  - D0 blinks at 2 Hz with a 50% duty cycle
  - D1 blinks at 5 Hz with a 50% duty cycle

  Notes for XIAO nRF54L15:
  - analogWritePinFrequency(pin, hz) is intended for D0-D5.
  - It uses the timer-backed per-pin PWM path.
*/

static const uint8_t kPinA = PIN_D0;
static const uint8_t kPinB = PIN_D1;
static const uint8_t kDuty = 128;
static const uint32_t kPinAHertz = 2;
static const uint32_t kPinBHertz = 5;

void setup() {
  Serial.begin(115200);
  delay(200);

  analogWriteResolution(8);

  analogWritePinFrequency(kPinA, kPinAHertz);
  analogWrite(kPinA, kDuty);

  analogWritePinFrequency(kPinB, kPinBHertz);
  analogWrite(kPinB, kDuty);

  Serial.println("D0 = 2 Hz, D1 = 5 Hz");
}

void loop() {
  delay(1000);
}
