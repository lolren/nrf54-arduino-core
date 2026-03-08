#include <nrf54l15_hal.h>

using xiao_nrf54l15::BleRadio;

BleRadio ble;

void setup() {
#if defined(NRF54L15_CLEAN_BLE_ENABLED) && (NRF54L15_CLEAN_BLE_ENABLED == 0)
  // Tools -> BLE Support is off, so blink fast to make the failure obvious.
  pinMode(LED_BUILTIN, OUTPUT);
  while (true) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(100);
  }
#else
  // Minimal example on purpose:
  // - no board-specific RF-switch duty cycling
  // - no custom address
  // - no scan response
  // - no low-power latency tuning
  //
  // Use the richer BLE examples if you need current optimization or explicit
  // antenna-path control.
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
  // One synchronous legacy advertising event, then a visible LED heartbeat.
  ble.advertiseEvent();
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  delay(100);
#endif
}
