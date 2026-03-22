#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

/*
 * BleAdvertiser
 *
 * General-purpose legacy BLE advertiser. More explicit than minimal examples:
 *   - Custom static-random address so it does not clash with other sketches.
 *   - Raw advertising payload (Flags + Complete Name + Manufacturer Data).
 *   - Periodic Serial logging of event count.
 *   - Low-power latency mode between events.
 *
 * This sketch does not gate the RF switch and does not connect – it is a
 * pure TX-only non-connectable advertiser (no ADV_SCAN_IND, no CONNECT_IND).
 *
 * To receive and decode this sketch's packets, use BlePassiveScanner or
 * BleActiveScanner on a second board.
 *
 * Tip: the raw kAdvPayload layout is intentionally visible here so you can
 * learn the AD-structure format. Each field is: [length, type, data...].
 */

// General-purpose legacy advertiser example.
//
// This sketch is intentionally more explicit than BleBeaconMinimal:
// - custom static-random address
// - raw advertising payload
// - periodic serial logging
// - low-power latency mode enabled
//
// It does not duty-cycle the XIAO RF switch path. Use the dedicated low-power
// BLE examples if you care about average current.

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_lastLogMs = 0;
static uint32_t g_advEvents = 0;

// Custom advertising cadence for the raw advertiseEvent() loop.
// kTxPowerDbm: -8 dBm is a sensible indoor default. Range: -40 to +8 dBm.
static constexpr int8_t kTxPowerDbm = -8;
// kAdvertisingIntervalMs: delay() between advertising events (milliseconds).
static constexpr uint32_t kAdvertisingIntervalMs = 100UL;
// kInterChannelDelayUs: pause between ch37/38/39 transmissions (microseconds).
static constexpr uint32_t kInterChannelDelayUs = 350U;
// kAdvertisingSpinLimit: max time to wait for the radio to complete each
// channel transmission (microseconds). 700 000 us = 700 ms.
static constexpr uint32_t kAdvertisingSpinLimit = 700000UL;

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nBleAdvertiser start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);

  // The name is carried inside the raw advertising payload, not by
  // setAdvertisingName(...), so the payload layout is fully visible here.
  static const uint8_t kAdvPayload[] = {
      2, 0x01, 0x06,                                // Flags
      12, 0x09, 'X', 'I', 'A', 'O', '-', '5', '4',  // Complete name
      '-', 'C', 'L', 'N',
      5, 0xFF, 0x34, 0x12, 0x54, 0x15               // MFG data
  };
  static const uint8_t kAddress[6] = {0x01, 0x00, 0x15, 0x54, 0xDE, 0xC0};

  bool ok = g_ble.begin(kTxPowerDbm);
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
  const bool txOk = g_ble.advertiseEvent(kInterChannelDelayUs, kAdvertisingSpinLimit);
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

  // This is a plain System ON cadence, not a System OFF beacon pattern.
  delay(kAdvertisingIntervalMs);
  delay(1);
}
