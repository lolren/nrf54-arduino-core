#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_lastLogMs = 0;
static uint32_t g_advEvents = 0;

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nBleAdvertiser start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);

  static const uint8_t kAdvPayload[] = {
      2, 0x01, 0x06,                                // Flags
      12, 0x09, 'X', 'I', 'A', 'O', '-', '5', '4',  // Complete name
      '-', 'C', 'L', 'N',
      5, 0xFF, 0x34, 0x12, 0x54, 0x15               // MFG data
  };
  static const uint8_t kAddress[6] = {0x01, 0x00, 0x15, 0x54, 0xDE, 0xC0};

  bool ok = g_ble.begin();
  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic);
  }
  if (ok) {
    ok = g_ble.setAdvertisingData(kAdvPayload, sizeof(kAdvPayload));
  }
  if (ok) {
    ok = g_ble.buildAdvertisingPacket();
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
}

void loop() {
  const bool txOk = g_ble.advertiseEvent(350U, 700000UL);
  ++g_advEvents;

  // User LED is active-low on XIAO.
  Gpio::write(kPinUserLed, (g_advEvents & 0x1U) == 0U);

  const uint32_t now = millis();
  if ((now - g_lastLogMs) >= 1000UL) {
    g_lastLogMs = now;

    char line[96];
    snprintf(line, sizeof(line),
             "t=%lu adv_events=%lu last=%s\r\n",
             static_cast<unsigned long>(now),
             static_cast<unsigned long>(g_advEvents),
             txOk ? "OK" : "FAIL");
    Serial.print(line);
  }

  // 100 ms advertising interval. Keep the loop active unless an explicit wake
  // source is armed for WFI.
  delay(100);
  delay(1);
}
