/*
 * BlePeriodicAdvertising — BLE 5.0+ Periodic Advertising
 *
 * Demonstrates periodic advertising using the raw RADIO peripheral.
 * Transmits periodic advertising PDU packets at a configurable interval.
 *
 * Periodic advertising is useful for:
 *   - Broadcasting sensor data to nearby BLE scanners
 *   - Channel sounding reference frames
 *   - LE Audio broadcasting foundation
 *
 * Hardware: XIAO nRF54L15
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

// Periodic advertising data: BLE device name + manufacturer data
static const uint8_t PERIODIC_ADV_DATA[] = {
  // Service UUID 128-bit (example: Nordic UARt)
  0x16, 0xFE, 0x95,  // Service UUID 128-bit: 0x95FE = Nordic UART
  0x2A, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
  0x80, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00,
  // Manufacturer specific data
  0xFF, 0x00, 0x59,  // Nordic Semiconductors
  0x01, 0x02, 0x03, 0x04, 0x05,
};

static const uint16_t PERIODIC_INTERVAL_MS = 30;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  BLE Periodic Advertising"));
  Serial.println(F("======================================"));
  Serial.println();

  BlePeriodicAdvertising adv;

  Serial.print(F("  RADIO base: 0x"));
  Serial.println(nrf54l15::RADIO_BASE, HEX);
  Serial.println();

  Serial.println(F("--- Periodic Advertising Configuration ---"));
  Serial.print(F("  Data length: "));
  Serial.println(sizeof(PERIODIC_ADV_DATA));
  Serial.print(F("  Interval: "));
  Serial.print(PERIODIC_INTERVAL_MS);
  Serial.println(F(" ms"));
  Serial.print(F("  TX power: "));
  Serial.println(adv.txPowerDbm());
  Serial.println();

  // Start periodic advertising
  bool ok = adv.begin(PERIODIC_ADV_DATA, sizeof(PERIODIC_ADV_DATA),
                      PERIODIC_INTERVAL_MS);
  Serial.print(F("  begin() result: "));
  Serial.println(ok ? F("OK") : F("FAIL"));
  Serial.println();

  if (ok) {
    Serial.print(F("  Packets transmitted: "));
    Serial.println(adv.packetCount());
    Serial.println();

    // Test changing interval
    adv.end();
    adv.setIntervalMs(100);
    Serial.print(F("  New interval: "));
    Serial.print(adv.intervalMs());
    Serial.println(F(" ms"));

    ok = adv.begin(PERIODIC_ADV_DATA, sizeof(PERIODIC_ADV_DATA),
                   adv.intervalMs());
    Serial.print(F("  begin(100ms) result: "));
    Serial.println(ok ? F("OK") : F("FAIL"));
    Serial.print(F("  Packets transmitted: "));
    Serial.println(adv.packetCount());
    Serial.println();

    // Test setting new data
    adv.end();
    uint8_t newData[] = { 0x16, 0xFE, 0x95, 0x01, 0x02, 0x03 };
    bool setOk = adv.setData(newData, sizeof(newData));
    Serial.print(F("  setData() result: "));
    Serial.println(setOk ? F("OK") : F("FAIL"));

    adv.setTxPowerDbm(8);
    Serial.print(F("  TX power: "));
    Serial.println(adv.txPowerDbm());
    Serial.println();

    ok = adv.begin(newData, sizeof(newData), adv.intervalMs());
    Serial.print(F("  begin(new data) result: "));
    Serial.println(ok ? F("OK") : F("FAIL"));
    Serial.print(F("  Total packets: "));
    Serial.println(adv.packetCount());
    Serial.println();

    adv.end();
  }

  Serial.println(F("======================================"));
  Serial.println(F("  BLE periodic advertising demo complete."));
  Serial.println(F("  Use a BLE scanner (nRF Connect) to observe packets."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
