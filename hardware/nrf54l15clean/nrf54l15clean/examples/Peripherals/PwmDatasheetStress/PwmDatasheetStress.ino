/*
 * PwmDatasheetStress
 *
 * Exercises the nRF54L15 PWM20/PWM21/PWM22 hardware slices used by
 * shared-frequency analogWrite(), then runs the D0-D5 custom-frequency
 * path that is useful for checking 255 -> 0 duty wrap behavior.
 *
 * Probe pins directly when checking the waveform. An RC filter will show the
 * expected capacitor discharge/charge step at the 255 -> 0 transition.
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
    analogWrite(kHardwarePwmPins[i], (uint8_t)(20U + (i * 23U)));
  }
  delay(4000);

  Serial.println("Switching D0-D5 to grouped 1 kHz per-pin PWM and sweeping through 255 -> 0");
  for (uint8_t pin : kTimerPwmPins) {
    analogWritePinFrequency(pin, 1000);
  }
}

void loop()
{
  static uint32_t last_update_ms = 0;
  const uint32_t now = millis();
  if ((now - last_update_ms) < 5U) {
    return;
  }
  last_update_ms = now;

  for (uint8_t i = 0; i < (sizeof(kTimerPwmPins) / sizeof(kTimerPwmPins[0])); ++i) {
    analogWrite(kTimerPwmPins[i], (uint8_t)(g_duty + (i * 11U)));
  }

  analogWrite(PIN_D6, g_duty);
  analogWrite(PIN_D7, (uint8_t)(255U - g_duty));
  analogWrite(PIN_D8, (uint8_t)(g_duty * 3U));
  analogWrite(PIN_D9, (uint8_t)(g_duty * 5U));

  ++g_duty;
  if (g_duty == 0U) {
    Serial.println("Duty wrapped 255 -> 0");
  }
}
