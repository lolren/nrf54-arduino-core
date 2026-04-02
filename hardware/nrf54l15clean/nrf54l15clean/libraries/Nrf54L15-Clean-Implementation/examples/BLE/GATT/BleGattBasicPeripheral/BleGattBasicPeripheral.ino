/*
 * BleGattBasicPeripheral
 *
 * Minimal connectable BLE peripheral exposing two standard GATT services:
 *   - Generic Access (Device Name)
 *   - Battery Service (Battery Level, auto-decrements every 30 s as a demo)
 *
 * Useful as a baseline to verify that a central can discover services, read
 * characteristics, and write CCCDs — without any custom GATT or pairing logic
 * getting in the way.
 *
 * When kLogBatteryReadDebug = true, every Device Name read, service browse,
 * and Battery/Service-Changed CCCD write is printed with full per-event
 * BLE link-layer details (SN, NESN, fresh flag, etc.). Handy for tracing
 * Android GATT cache behaviour or timing regressions.
 *
 * When kLogConnectionPackets = true, every received ATT or LL-Control PDU
 * is also printed — very verbose, useful for low-level debugging.
 *
 * After disconnect, detailed channel-map and encryption counters are printed
 * to help diagnose any channel-map update or decryption issues.
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static bool g_wasConnected = false;
static uint8_t g_batteryLevel = 100U;
static uint32_t g_lastBatteryUpdateMs = 0U;
static uint32_t g_traceCount = 0U;
static char g_lastTrace[24] = {0};
static constexpr int8_t kTxPowerDbm = 0;

static void onBleTrace(const char* message, void* context) {
  (void)context;
  ++g_traceCount;
  memset(g_lastTrace, 0, sizeof(g_lastTrace));
  if (message == nullptr) {
    return;
  }
  strncpy(g_lastTrace, message, sizeof(g_lastTrace) - 1U);
}

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleGattBasicPeripheral start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  // Unique address avoids Android reusing a stale GATT cache from other sketches.
  static const uint8_t kAddress[6] = {0x31, 0x00, 0x15, 0x54, 0xDE, 0xC0};
  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    // Set after begin() so the radio subsystem is already configured.
    g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  }
  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         // CSA#2 allows the central to use channel selection algorithm 2 for
         // better coexistence; no downside, leave this on for modern centrals.
         g_ble.setAdvertisingChannelSelectionAlgorithm2(true) &&
         // kAdvInd: connectable and scannable undirected advertising (standard mode).
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         // Short name in every advertising packet (true = include AD Flags).
         g_ble.setAdvertisingName("X54-GATT", true) &&
         // Full name in scan response — returned only when the central actively scans.
         g_ble.setScanResponseName("X54-GATT-SCAN") &&
         // GATT Device Name characteristic (UUID 0x2A00) — readable by the central.
         g_ble.setGattDeviceName("XIAO nRF54L15 Clean") &&
         // Battery Service (UUID 0x180F) with Battery Level (0x2A19) set to 100 %.
         g_ble.setGattBatteryLevel(g_batteryLevel);
  }
  if (ok) {
    g_ble.setTraceCallback(onBleTrace, nullptr);
  }

  Serial.print("BLE GATT init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
}

void loop() {
  const uint32_t nowMs = millis();
  if ((nowMs - g_lastBatteryUpdateMs) >= 30000UL) {
    g_lastBatteryUpdateMs = nowMs;
    g_batteryLevel = (g_batteryLevel > 5U) ? static_cast<uint8_t>(g_batteryLevel - 1U)
                                            : 100U;
    g_ble.setGattBatteryLevel(g_batteryLevel);
  }

  if (!g_ble.isConnected()) {
    if (g_wasConnected) {
      g_wasConnected = false;
      BleEncryptionDebugCounters dbg{};
      uint8_t lastReason = 0U;
      bool lastRemote = false;
      g_ble.getEncryptionDebugCounters(&dbg);
      const bool haveLastReason =
          g_ble.getLastDisconnectReason(&lastReason, &lastRemote);
      Serial.print("disc reason_valid=");
      Serial.print(haveLastReason ? "1" : "0");
      Serial.print(" reason=0x");
      if (lastReason < 16U) {
        Serial.print('0');
      }
      Serial.print(lastReason, HEX);
      Serial.print(" remote=");
      Serial.print(lastRemote ? "1" : "0");
      Serial.print(" chm_rx_ce=");
      Serial.print(dbg.connLastChannelMapRxEventCounter);
      Serial.print(" chm_inst=");
      Serial.print(dbg.connLastChannelMapInstant);
      Serial.print(" chm_apply_ce=");
      Serial.print(dbg.connLastChannelMapAppliedEventCounter);
      Serial.print(" chm_apply_ch=");
      Serial.print(dbg.connLastChannelMapAppliedDataChannel);
      Serial.print(" chm_pend=");
      Serial.print(dbg.connChannelMapPendingCount);
      Serial.print(" chm_apply=");
      Serial.print(dbg.connChannelMapAppliedCount);
      Serial.print(" chm_dup=");
      Serial.print(dbg.connChannelMapDuplicateCount);
      Serial.print(" chm_to_after_apply=");
      Serial.print(dbg.connChannelMapTimeoutAfterApplyCount);
      Serial.print(" chm_to_ce=");
      Serial.print(dbg.connLastChannelMapTimeoutEventCounter);
      Serial.print(" rx_to=");
      Serial.print(dbg.connRxTimeoutCount);
      Serial.print(" rx_crc=");
      Serial.print(dbg.connRxCrcErrorCount);
      Serial.print(" iatt_ack=");
      Serial.print(dbg.connImplicitAttProgressAckCount);
      Serial.print(" bg_thr=");
      Serial.print(dbg.connBgServiceThreadCount);
      Serial.print(" bg_isr=");
      Serial.print(dbg.connBgServiceIsrCount);
      Serial.print(" bg_due=");
      Serial.print(dbg.connBgDueCount);
      Serial.print(" bg_lag_last=");
      Serial.print(dbg.connBgWakeLagLastUs);
      Serial.print(" bg_lag_max=");
      Serial.print(dbg.connBgWakeLagMaxUs);
      Serial.print("\r\n");
    }
    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    Gpio::write(kPinUserLed, true);
    delay(1);
    return;
  }

  g_wasConnected = true;

  BleConnectionEvent evt{};
  const bool ran = g_ble.pollConnectionEvent(&evt, 450000UL);
  if (ran && evt.eventStarted) {
    Gpio::write(kPinUserLed, false);

    if (evt.terminateInd) {
      Serial.print("link terminated\r\n");
      Gpio::write(kPinUserLed, true);
    }
    return;
  }

  Gpio::write(kPinUserLed, true);
}
