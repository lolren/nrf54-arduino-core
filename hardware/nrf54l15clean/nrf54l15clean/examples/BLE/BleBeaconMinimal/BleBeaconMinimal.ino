#include <nrf54l15_hal.h>

using xiao_nrf54l15::BleRadio;

BleRadio ble;

void setup() {
#if defined(NRF54L15_CLEAN_BLE_ENABLED) && (NRF54L15_CLEAN_BLE_ENABLED == 0)
  pinMode(LED_BUILTIN, OUTPUT);
  while (true) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(100);
  }
#else
  pinMode(LED_BUILTIN, OUTPUT);
  ble.begin(0);
  ble.setAdvertisingName("XIAO54-Clean");
  ble.buildAdvertisingPacket();
#endif
}

void loop() {
#if defined(NRF54L15_CLEAN_BLE_ENABLED) && (NRF54L15_CLEAN_BLE_ENABLED == 0)
  delay(1000);
#else
  ble.advertiseEvent();
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  delay(100);
#endif
}
