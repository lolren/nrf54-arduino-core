/*
  AnalogWriteHardwarePwmFade

  Fades an LED on D5 using the shared hardware PWM path.

  Notes for XIAO nRF54L15:
  - Real analogWrite() hardware PWM is available on D0-D5.
  - analogWriteFrequency(hz) sets the shared PWM frequency for that path.
  - Move the LED to another D0-D5 pin if D5 is in use for I2C.
*/

static const uint8_t kPwmPin = PIN_D5;
static const uint16_t kStepDelayMs = 4;

void setup() {
  Serial.begin(115200);
  delay(200);

  analogWriteResolution(8);
  analogWriteFrequency(1000);

  Serial.println("AnalogWriteHardwarePwmFade on D5 at 1 kHz");
}

void loop() {
  for (int duty = 0; duty <= 255; ++duty) {
    analogWrite(kPwmPin, duty);
    delay(kStepDelayMs);
  }

  for (int duty = 255; duty >= 0; --duty) {
    analogWrite(kPwmPin, duty);
    delay(kStepDelayMs);
  }
}
