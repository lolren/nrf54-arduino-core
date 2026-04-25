#if !defined(ARDUINO_NRF54L15DK_PCA10156)
#error "Select Tools > Board > Nordic PCA10156 nRF54L15 DK for this example."
#endif

namespace {

constexpr uint8_t kRawLedPin = _PINNUM(2, 9);
constexpr uint8_t kRawButtonPin = _PINNUM(1, 13);
constexpr uint8_t kHeaderProbePin = _PINNUM(1, 4);

void setLed(uint8_t pin, bool on) {
  digitalWrite(pin, on ? LED_STATE_ON : !LED_STATE_ON);
}

}  // namespace

void setup() {
#if !defined(NRF54L15_CLEAN_SERIAL_DISABLED)
  Serial.begin(115200);
  delay(50);
  Serial.println("DK linear GPIO demo");
  Serial.println("LED0 = _PINNUM(2, 9), SW0 = _PINNUM(1, 13), D0/header-4 = _PINNUM(1, 4)");
#endif

  pinMode(kRawLedPin, OUTPUT);
  pinMode(PIN_LED1, OUTPUT);
  pinMode(kRawButtonPin, INPUT_PULLUP);
  pinMode(kHeaderProbePin, OUTPUT);

  setLed(kRawLedPin, false);
  setLed(PIN_LED1, false);
  digitalWrite(kHeaderProbePin, LOW);
}

void loop() {
  const bool pressed = digitalRead(kRawButtonPin) == BUTTON_STATE_ON;

  setLed(kRawLedPin, pressed);
  setLed(PIN_LED1, !pressed);
  digitalWrite(kHeaderProbePin, pressed ? HIGH : LOW);

#if !defined(NRF54L15_CLEAN_SERIAL_DISABLED)
  static bool previousPressed = false;
  if (pressed != previousPressed) {
    previousPressed = pressed;
    Serial.print("SW0 ");
    Serial.println(pressed ? "pressed" : "released");
  }
#endif

  delay(20);
}
