#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

/*
 * BleAdvertiser
 *
 * General-purpose legacy BLE advertiser. More explicit than minimal examples:
 *   - Raw advertising payload (Flags + Complete Name + Manufacturer Data).
 *   - Periodic Serial logging of event count.
 *   - Low-power latency mode between events.
 *
 * This sketch uses the per-event enable/collapse RF pattern which is required
 * for phone discoverability on XIAO nRF54L15. The RF switch is powered only
 * during each advertiseEvent() call and collapsed during the idle delay.
 *
 * Note: setDeviceAddress() is intentionally omitted. Validation on this HAL
 * showed that calling setDeviceAddress() before advertiseEvent() causes the
 * device to not be discoverable by phones/scanners. Use the default
 * FICR-derived address for reliable discoverability.
 *
 * To receive and decode this sketch's packets, use BlePassiveScanner or
 * BleActiveScanner on a second board.
 *
 * Tip: the raw kAdvPayload layout is intentionally visible here so you can
 * learn the AD-structure format. Each field is: [length, type, data...].
 *
 * Power note:
 *   begin() / end() are called every advertising cycle so that end() clears
 *   the GRTC ACTIVE flag before the WFI sleep window. Without this, the GRTC
 *   keeps the CPU domain active and WFI only reaches ~90 µA instead of the
 *   2–3 µA achievable with the flag cleared.
 */

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_lastLogMs = 0;
static uint32_t g_advEvents = 0;

// kTxPowerDbm: 0 dBm for reliable phone discoverability.
static constexpr int8_t kTxPowerDbm = 0;
// kAdvertisingIntervalMs: delay() between advertising events (milliseconds).
static constexpr uint32_t kAdvertisingIntervalMs = 100UL;
// kInterChannelDelayUs: pause between ch37/38/39 transmissions (microseconds).
static constexpr uint32_t kInterChannelDelayUs = 350U;
// kAdvertisingSpinLimit: max time to wait for the radio to complete each
// channel transmission (microseconds). 700 000 us = 700 ms.
static constexpr uint32_t kAdvertisingSpinLimit = 700000UL;

// The name is carried inside the raw advertising payload, not by
// setAdvertisingName(...), so the payload layout is fully visible here.
static const uint8_t kAdvPayload[] = {
    2, 0x01, 0x06,                                 // Flags
    12, 0x09, 'X', 'I', 'A', 'O', '-', '5', '4',  // Complete name
    '-', 'C', 'L', 'N',
    5, 0xFF, 0x34, 0x12, 0x54, 0x15                // MFG data
};

// Bring up BLE, fire one advertising event, then shut down.
// end() clears the GRTC ACTIVE flag so subsequent WFI enters deep sleep.
static bool advertiseOnce() {
  // Per-event enable/collapse required for phone discoverability on XIAO nRF54L15.
  bool ok = BoardControl::enableRfPath(BoardAntennaPath::kCeramic);
  bool bleBegun = false;
  if (ok) {
    bleBegun = true;
    ok = g_ble.begin(kTxPowerDbm);
  }
  if (ok) {
    // advertiseEvent() is TX-only on this HAL, so use a non-connectable PDU.
    ok = g_ble.setAdvertisingPduType(BleAdvPduType::kAdvNonConnInd);
  }
  if (ok) {
    ok = g_ble.setAdvertisingData(kAdvPayload, sizeof(kAdvPayload));
  }
  if (ok) {
    ok = g_ble.advertiseEvent(kInterChannelDelayUs, kAdvertisingSpinLimit);
  }
  if (bleBegun) {
    g_ble.end();  // clears GRTC ACTIVE → WFI reaches 2–3 µA
  }
  BoardControl::collapseRfPathIdle();
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nBleAdvertiser start\r\n");

  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  // Set after hardware init so the radio subsystem can be configured per-cycle.
  g_power.setLatencyMode(PowerLatencyMode::kLowPower);

  Serial.print("BleAdvertiser ready\r\n");
}

void loop() {
  const uint32_t cycleStart = millis();

  const bool txOk = advertiseOnce();
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

  // GRTC ACTIVE cleared by end() above — WFI reaches 2–3 µA here
  const uint32_t deadline = cycleStart + kAdvertisingIntervalMs;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    __asm volatile("wfi");
  }
}
