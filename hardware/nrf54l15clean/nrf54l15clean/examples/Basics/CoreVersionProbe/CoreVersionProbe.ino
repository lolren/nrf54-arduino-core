/*
  CoreVersionProbe

  Prints the version of the installed board package as seen by the sketch.
  The values come from the package version in platform.txt, so they update
  automatically when the core version changes.
*/

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(1);
  }

  Serial.print("Core version: ");
  Serial.println(ARDUINO_NRF54L15_CLEAN_VERSION_STRING);

  Serial.print("Major: ");
  Serial.println(ARDUINO_NRF54L15_CLEAN_VERSION_MAJOR);
  Serial.print("Minor: ");
  Serial.println(ARDUINO_NRF54L15_CLEAN_VERSION_MINOR);
  Serial.print("Patch: ");
  Serial.println(ARDUINO_NRF54L15_CLEAN_VERSION_PATCH);
  Serial.print("Encoded: ");
  Serial.println(arduino::nrf54l15clean::kCoreVersion);
}

void loop() {
  Serial.print("Core version heartbeat: ");
  Serial.println(ARDUINO_NRF54L15_CLEAN_VERSION_STRING);
  delay(1000);
}
