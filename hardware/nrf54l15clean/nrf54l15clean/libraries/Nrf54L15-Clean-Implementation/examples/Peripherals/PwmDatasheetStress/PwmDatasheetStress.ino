/*
 * PwmDatasheetStress
 *
 * Library-visible copy of the platform PWM stress sketch so Arduino IDE users
 * can find it under the core library examples as well as board examples.
 */

static const uint8_t kHardwarePwmPins[] = {
  PIN_D0, PIN_D1, PIN_D2, PIN_D3, PIN_D4,
  PIN_D5, PIN_D6, PIN_D7, PIN_D8, PIN_D9
};

static const uint8_t kTimerPwmPins[] = {
  PIN_D0, PIN_D1, PIN_D2, PIN_D3, PIN_D4, PIN_D5
};

static uint8_t g_duty = 0;

void setup()
{
  Serial.begin(115200);
  delay(200);

  analogWriteResolution(8);
  analogWriteFrequency(1000);

  for (uint8_t pin : kHardwarePwmPins) {
    pinMode(pin, OUTPUT);
  }

  Serial.println("PWM datasheet stress start");
  Serial.println("Shared 1 kHz hardware PWM on D0-D9 for 4 seconds");
  for (uint8_t i = 0; i < (sizeof(kHardwarePwmPins) / sizeof(kHardwarePwmPins[0])); ++i) {
    analogWrite(kHardwarePwmPins[i], static_cast<uint8_t>(20U + (i * 23U)));
  }
  delay(4000);

  Serial.println("Switching D0-D5 to grouped 1 kHz per-pin PWM and sweeping through 255 -> 0");
  for (uint8_t pin : kTimerPwmPins) {
    analogWritePinFrequency(pin, 1000);
  }
}

void loop()
{
  static uint32_t lastUpdateMs = 0;
  const uint32_t now = millis();
  if ((now - lastUpdateMs) < 5U) {
    return;
  }
  lastUpdateMs = now;

  for (uint8_t i = 0; i < (sizeof(kTimerPwmPins) / sizeof(kTimerPwmPins[0])); ++i) {
    analogWrite(kTimerPwmPins[i], static_cast<uint8_t>(g_duty + (i * 11U)));
  }

  analogWrite(PIN_D6, g_duty);
  analogWrite(PIN_D7, static_cast<uint8_t>(255U - g_duty));
  analogWrite(PIN_D8, static_cast<uint8_t>(g_duty * 3U));
  analogWrite(PIN_D9, static_cast<uint8_t>(g_duty * 5U));

  ++g_duty;
  if (g_duty == 0U) {
    Serial.println("Duty wrapped 255 -> 0");
  }
}
