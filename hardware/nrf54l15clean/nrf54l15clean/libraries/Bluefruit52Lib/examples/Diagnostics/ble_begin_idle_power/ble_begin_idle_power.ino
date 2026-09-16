#include <Arduino.h>
#include <bluefruit.h>

// PPK2 sequence: 10 s baseline, 10 s BLE-initialized idle, 10 s advertising,
// then idle indefinitely. See README.md for capture windows and limitations.
static constexpr unsigned long kWindowMs = 10000UL;

static void failMeasurement() {
  pinMode(LED_BUILTIN, OUTPUT);
  while (true) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }
}

void setup() {
  Bluefruit.autoConnLed(false);
  delay(kWindowMs);

  if (!Bluefruit.begin()) {
    failMeasurement();
  }
  delay(kWindowMs);

  Bluefruit.setName("nRF54-IdlePower");
  Bluefruit.setTxPower(0);
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_ADV_NONCONN_IND);
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  if (!Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE) ||
      !Bluefruit.Advertising.addName()) {
    failMeasurement();
  }
  Bluefruit.Advertising.setInterval(1600, 1600);
  Bluefruit.Advertising.setFastTimeout(0);
  if (!Bluefruit.Advertising.start(0)) {
    failMeasurement();
  }
  delay(kWindowMs);
  if (!Bluefruit.Advertising.stop()) {
    failMeasurement();
  }
}

void loop() {
  delay(kWindowMs);
}
