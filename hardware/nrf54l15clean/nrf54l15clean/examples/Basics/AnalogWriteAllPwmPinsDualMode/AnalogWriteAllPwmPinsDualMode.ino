/*
  AnalogWriteAllPwmPinsDualMode

  Diagnostic sketch for driving all analogWrite() PWM-capable pins at once.

  Expected result on XIAO nRF54L15:
  - D0-D3 use shared hardware PWM channels first
  - D4-D15 fall back to software PWM once shared hardware channels are exhausted
  - All pins output 1000 Hz, 50% duty
*/

static const uint8_t kPins[] = {
  PIN_D0, PIN_D1, PIN_D2, PIN_D3, PIN_D4,
  PIN_D5, PIN_D6, PIN_D7, PIN_D8, PIN_D9,
  PIN_D10, PIN_D11, PIN_D12, PIN_D13, PIN_D14,
  PIN_D15
};

static const uint32_t kFrequencyHz = 1000;
static const uint8_t kDuty = 128;

void setup() {
  Serial.begin(115200);
  delay(200);

  analogWriteResolution(8);
  analogWriteFrequency(kFrequencyHz);

  for (uint8_t i = 0; i < sizeof(kPins); ++i) {
    pinMode(kPins[i], OUTPUT);
    analogWrite(kPins[i], kDuty);
  }

  Serial.println("D0-D15 active at 1000 Hz, 50% duty");
}

void loop() {
  delay(1000);
}
