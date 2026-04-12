#if !defined(ARDUINO_HOLYIOT_25008_NRF54L15)
#error "Select Tools > Board > HOLYIOT-25008 nRF54L15 Module for this example."
#endif

namespace {

constexpr uint32_t kStepMs = 180U;

void setChannel(uint8_t pin, bool on) {
  digitalWrite(pin, on ? LED_STATE_ON : !LED_STATE_ON);
}

void setRgb(bool red, bool green, bool blue) {
  setChannel(LED_RED, red);
  setChannel(LED_GREEN, green);
  setChannel(LED_BLUE, blue);
}

bool buttonPressed() {
  return digitalRead(PIN_BUTTON) == LOW;
}

void flashWhite(uint8_t count, uint16_t onMs, uint16_t offMs) {
  for (uint8_t i = 0; i < count; ++i) {
    setRgb(true, true, true);
    delay(onMs);
    setRgb(false, false, false);
    if (i + 1U < count) {
      delay(offMs);
    }
  }
}

}  // namespace

void setup() {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  setRgb(false, false, false);
  flashWhite(2, 90, 70);
}

void loop() {
  if (buttonPressed()) {
    flashWhite(3, 45, 45);
    while (buttonPressed()) {
      setRgb(true, true, true);
      delay(10);
    }
    setRgb(false, false, false);
    delay(120);
    return;
  }

  setRgb(true, false, false);
  delay(kStepMs);
  setRgb(false, true, false);
  delay(kStepMs);
  setRgb(false, false, true);
  delay(kStepMs);
}
