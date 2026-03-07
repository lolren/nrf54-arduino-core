// For lowest current, select Tools -> Power Profile -> Low Power (WFI Idle).

unsigned long lastToggleMs = 0;
bool ledState = false;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  const unsigned long now = millis();
  if ((now - lastToggleMs) >= 1000UL) {
    lastToggleMs = now;
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
  }

  // Idle behavior is controlled by the Tools -> Power Profile menu.
  delay(5);
}
