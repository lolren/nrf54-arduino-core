/*
 * BleBackgroundStressProbe
 *
 * Stress-tests the background connection service by intentionally hogging the
 * CPU foreground while a BLE connection is live. The background service runs
 * in interrupt context, so ATT/GATT responses must survive even when the main
 * loop is busy or sleeping for hundreds of milliseconds.
 *
 * What it does:
 *   - Advertises a custom GATT service (0xFFF0) with two characteristics:
 *       0xFFF1  readable + writable — write any bytes; the sketch echoes them
 *                                     back via a notification on 0xFFF2.
 *       0xFFF2  readable + notifiable — periodic heartbeat ("N" + counter)
 *                                       every kNotifyPeriodMs, plus echoes.
 *   - Also exposes the standard Battery Service (auto-decrements level).
 *   - After kConnectionWarmupMs, starts alternating between burning CPU
 *     (kForegroundBusyMs) and sleeping (kForegroundDelayMs) to verify that
 *     the background service keeps GATT alive without main-loop help.
 *   - Prints per-interval connection stats: event counts, timeouts, missed
 *     events, TX lag, etc. — useful for spotting timing regressions.
 *
 * Use with nRF Connect or any GATT browser. Enable notifications on both
 * Battery Level and 0xFFF2, then write bytes to 0xFFF1 and watch echoes.
 *
 * Tuning via compiler defines (override in boards.txt or build flags):
 *   NRF54L15_CLEAN_BG_STRESS_DELAY_MS  — sleep duration per cycle (default 900 ms)
 *   NRF54L15_CLEAN_BG_STRESS_BUSY_MS   — CPU-burn duration per cycle (default 250 ms)
 */

#include <Arduino.h>

#include <stdio.h>
#include <string.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static uint16_t g_customServiceHandle = 0U;
static uint16_t g_writeValueHandle = 0U;
static uint16_t g_notifyValueHandle = 0U;
static uint16_t g_notifyCccdHandle = 0U;

static bool g_initOk = false;
static bool g_connectionAnnounced = false;
static bool g_notifyEchoPending = false;
static bool g_wasConnected = false;
static bool g_stressArmingLogged = false;
static bool g_stressStartedLogged = false;

static uint8_t g_batteryLevel = 100U;
static uint8_t g_notifyCounter = 0U;
static uint8_t g_lastWriteLength = 0U;
static uint8_t g_lastWriteBytes[20];

static uint32_t g_lastStatsMs = 0U;
static uint32_t g_lastBatteryStepMs = 0U;
static uint32_t g_lastNotifyMs = 0U;
static uint32_t g_lastAdvLogMs = 0U;
static uint32_t g_connectionStartMs = 0U;
static uint32_t g_writeCount = 0U;
static uint32_t g_notifyQueueCount = 0U;
static uint32_t g_notifySentCount = 0U;
static uint32_t g_eventCount = 0U;
static uint32_t g_eventRxOkCount = 0U;
static uint32_t g_eventRxTimeoutCount = 0U;
static uint32_t g_eventTxTimeoutCount = 0U;
static uint32_t g_eventAttCount = 0U;
static uint32_t g_eventLlCount = 0U;
static volatile uint32_t g_spinSink = 0U;

// Lower power than normal so nearby devices are not overwhelmed during stress runs.
static constexpr int8_t kTxPowerDbm = -8;
#ifndef NRF54L15_CLEAN_BG_STRESS_DELAY_MS
#define NRF54L15_CLEAN_BG_STRESS_DELAY_MS 900UL  // ms to sleep between busy bursts
#endif

#ifndef NRF54L15_CLEAN_BG_STRESS_BUSY_MS
#define NRF54L15_CLEAN_BG_STRESS_BUSY_MS 250UL   // ms of CPU-burn per burst
#endif

// How long the foreground sleeps per stress cycle (overridable via compiler define).
static constexpr uint32_t kForegroundDelayMs =
    static_cast<uint32_t>(NRF54L15_CLEAN_BG_STRESS_DELAY_MS);
// How long the foreground burns CPU per stress cycle.
static constexpr uint32_t kForegroundBusyMs =
    static_cast<uint32_t>(NRF54L15_CLEAN_BG_STRESS_BUSY_MS);
// Grace period after connect before stress starts — gives Android time to finish
// service discovery and enable CCCDs while the main loop is still responsive.
static constexpr uint32_t kConnectionWarmupMs = 5000UL;
static constexpr uint32_t kStatsPeriodMs = 5000UL;   // How often to print event stats.
static constexpr uint32_t kBatteryStepMs = 4000UL;   // Battery level tick interval.
static constexpr uint32_t kNotifyPeriodMs = 1500UL;  // Heartbeat notification interval.

static void printAddress(const uint8_t* addr) {
  if (addr == nullptr) {
    return;
  }
  for (int i = 5; i >= 0; --i) {
    if (addr[i] < 16U) {
      Serial.print('0');
    }
    Serial.print(addr[i], HEX);
    if (i > 0) {
      Serial.print(':');
    }
  }
}

static void printBytes(const uint8_t* data, uint8_t length) {
  if (data == nullptr || length == 0U) {
    Serial.print("(empty)");
    return;
  }
  for (uint8_t i = 0U; i < length; ++i) {
    if (data[i] < 16U) {
      Serial.print('0');
    }
    Serial.print(data[i], HEX);
    if (i + 1U < length) {
      Serial.print(' ');
    }
  }
}

// Busy-waits for durationMs using a dummy LCG computation so the compiler
// cannot optimise the loop away. This monopolises the CPU to verify that the
// background GRTC interrupt still services BLE connection events on time.
static void burnForegroundCpu(uint32_t durationMs) {
  const uint32_t start = millis();
  while ((millis() - start) < durationMs) {
    for (uint32_t i = 0U; i < 8000U; ++i) {
      g_spinSink = (g_spinSink * 1664525UL) + 1013904223UL + i;
    }
  }
}

static void onCustomWrite(uint16_t valueHandle, const uint8_t* value,
                          uint8_t valueLength, bool withResponse,
                          void* context) {
  (void)context;
  if (valueHandle != g_writeValueHandle) {
    return;
  }

  g_lastWriteLength = (valueLength <= sizeof(g_lastWriteBytes))
                          ? valueLength
                          : static_cast<uint8_t>(sizeof(g_lastWriteBytes));
  if (g_lastWriteLength > 0U && value != nullptr) {
    memcpy(g_lastWriteBytes, value, g_lastWriteLength);
  }
  ++g_writeCount;
  g_notifyEchoPending = true;

  Serial.print("write handle=0x");
  Serial.print(valueHandle, HEX);
  Serial.print(" via=");
  Serial.print(withResponse ? "req" : "cmd");
  Serial.print(" len=");
  Serial.print(valueLength);
  Serial.print(" data=");
  printBytes(value, g_lastWriteLength);
  Serial.print("\r\n");
}

static void queueNotifyValue(const uint8_t* value, uint8_t length) {
  if (value == nullptr || length == 0U) {
    return;
  }
  if (!g_ble.setCustomGattCharacteristicValue(g_notifyValueHandle, value, length)) {
    Serial.print("notify_value_update_failed\r\n");
    return;
  }
  if (g_ble.notifyCustomGattCharacteristic(g_notifyValueHandle, false)) {
    ++g_notifyQueueCount;
  } else {
    Serial.print("notify_queue_skipped\r\n");
  }
}

static void queuePeriodicNotify() {
  uint8_t payload[2] = {'N', g_notifyCounter++};
  queueNotifyValue(payload, sizeof(payload));
}

static void queueEchoNotify() {
  uint8_t payload[20];
  uint8_t length = 0U;
  payload[length++] = 'W';
  payload[length++] = static_cast<uint8_t>(g_writeCount & 0xFFU);
  const uint8_t copyLength =
      (g_lastWriteLength <= static_cast<uint8_t>(sizeof(payload) - length))
          ? g_lastWriteLength
          : static_cast<uint8_t>(sizeof(payload) - length);
  if (copyLength > 0U) {
    memcpy(&payload[length], g_lastWriteBytes, copyLength);
    length = static_cast<uint8_t>(length + copyLength);
  }
  queueNotifyValue(payload, length);
}

static void printConnectionStats(uint32_t nowMs) {
  BleEncryptionDebugCounters dbg{};
  g_ble.getEncryptionDebugCounters(&dbg);

  char line[384];
  snprintf(
      line, sizeof(line),
      "stats dt_ms=%lu ev=%lu rx_ok=%lu rx_to=%lu tx_to=%lu att=%lu ll=%lu "
      "writes=%lu notify_q=%lu notify_tx=%lu late_poll=%lu miss_last=%lu "
      "miss_max=%lu rx_to_dbg=%lu tx_to_dbg=%lu follow_to=%lu "
      "tx_lag_last=%lu tx_lag_max=%lu tx_late=%lu defer_ovr=%lu "
      "cb_drop=%lu trace_drop=%lu disc_valid=%u disc_reason=0x%02X remote=%u\r\n",
      static_cast<unsigned long>(nowMs - g_lastStatsMs),
      static_cast<unsigned long>(g_eventCount),
      static_cast<unsigned long>(g_eventRxOkCount),
      static_cast<unsigned long>(g_eventRxTimeoutCount),
      static_cast<unsigned long>(g_eventTxTimeoutCount),
      static_cast<unsigned long>(g_eventAttCount),
      static_cast<unsigned long>(g_eventLlCount),
      static_cast<unsigned long>(g_writeCount),
      static_cast<unsigned long>(g_notifyQueueCount),
      static_cast<unsigned long>(g_notifySentCount),
      static_cast<unsigned long>(dbg.connLatePollCount),
      static_cast<unsigned long>(dbg.connMissedEventCountLast),
      static_cast<unsigned long>(dbg.connMissedEventCountMax),
      static_cast<unsigned long>(dbg.connRxTimeoutCount),
      static_cast<unsigned long>(dbg.connTxTimeoutCount),
      static_cast<unsigned long>(dbg.connFollowupRxTimeoutCount),
      static_cast<unsigned long>(dbg.txenLagLastUs),
      static_cast<unsigned long>(dbg.txenLagMaxUs),
      static_cast<unsigned long>(dbg.txenLateCount),
      static_cast<unsigned long>(dbg.connDeferredEventOverwriteCount),
      static_cast<unsigned long>(dbg.connDeferredCallbackDropCount),
      static_cast<unsigned long>(dbg.connDeferredTraceDropCount),
      static_cast<unsigned int>(dbg.connLastDisconnectValid),
      static_cast<unsigned int>(dbg.connLastDisconnectReason),
      static_cast<unsigned int>(dbg.connLastDisconnectRemote));
  Serial.print(line);

  g_eventCount = 0U;
  g_eventRxOkCount = 0U;
  g_eventRxTimeoutCount = 0U;
  g_eventTxTimeoutCount = 0U;
  g_eventAttCount = 0U;
  g_eventLlCount = 0U;
  g_notifySentCount = 0U;
  g_lastStatsMs = nowMs;
}

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleBackgroundStressProbe start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  // Unique address per sketch — avoids Android reusing a stale GATT cache from
  // a different sketch that happened to run on the same hardware.
  static const uint8_t kAddress[6] = {0x91, 0x00, 0x15, 0x54, 0xDE, 0xC0};

  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    // kLowPower: CPU scales down between events; compatible with the stress test
    // because the foreground burn itself provides the load, not the idle policy.
    // Set after begin() so the radio subsystem is already configured.
    g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  }
  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         // setAdvertisingName: puts short name in ad packet; true = also add flags.
         g_ble.setAdvertisingName("X54-BG-STRESS", true) &&
         // setScanResponseName: full name returned on active scan (not in every adv).
         g_ble.setScanResponseName("X54-BG-STRESS-SCAN") &&
         // GATT Device Name characteristic — readable by central via ATT.
         g_ble.setGattDeviceName("X54 Background Stress") &&
         // Built-in Battery Service (UUID 0x180F), value readable + notifiable.
         g_ble.setGattBatteryLevel(g_batteryLevel) &&
         // Remove any GATT services left from a previous sketch run.
         g_ble.clearCustomGatt();
  }
  if (ok) {
    // Register a custom Vendor-Specific service (UUID 0xFFF0).
    ok = g_ble.addCustomGattService(0xFFF0U, &g_customServiceHandle);
  }
  if (ok) {
    // 0xFFF1: read + write (with and without response). Central writes bytes here.
    const uint8_t props = static_cast<uint8_t>(kBleGattPropRead |
                                               kBleGattPropWrite |
                                               kBleGattPropWriteNoRsp);
    const uint8_t initial[] = {'O', 'K'};
    ok = g_ble.addCustomGattCharacteristic(g_customServiceHandle, 0xFFF1U, props,
                                           initial, sizeof(initial),
                                           &g_writeValueHandle, nullptr);
  }
  if (ok) {
    // 0xFFF2: read + notify. Sends heartbeat and echoes of 0xFFF1 writes.
    // nullptr for txCccdHandle: we track the CCCD via isCustomGattCccdEnabled().
    const uint8_t props = static_cast<uint8_t>(kBleGattPropRead |
                                               kBleGattPropNotify);
    const uint8_t initial[] = {'N', 0U};
    ok = g_ble.addCustomGattCharacteristic(g_customServiceHandle, 0xFFF2U, props,
                                           initial, sizeof(initial),
                                           &g_notifyValueHandle,
                                           &g_notifyCccdHandle);
  }
  if (ok) {
    // Single write callback for all writable characteristics; distinguish by handle.
    g_ble.setCustomGattWriteCallback(onCustomWrite, nullptr);
    g_ble.clearEncryptionDebugCounters();
  }

  Serial.print("init=");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print(" tx_dbm=");
  Serial.print(static_cast<int>(kTxPowerDbm));
  Serial.print(" delay_ms=");
  Serial.print(static_cast<unsigned long>(kForegroundDelayMs));
  Serial.print(" busy_ms=");
  Serial.print(static_cast<unsigned long>(kForegroundBusyMs));
  Serial.print("\r\n");
  Serial.print("custom service=0x");
  Serial.print(g_customServiceHandle, HEX);
  Serial.print(" write=0x");
  Serial.print(g_writeValueHandle, HEX);
  Serial.print(" notify=0x");
  Serial.print(g_notifyValueHandle, HEX);
  Serial.print(" cccd=0x");
  Serial.print(g_notifyCccdHandle, HEX);
  Serial.print("\r\n");
  Serial.print("Connect, enable Battery Level + 0xFFF2 notifications, and write to 0xFFF1.\r\n");

  g_initOk = ok;
  g_lastStatsMs = millis();
  g_lastAdvLogMs = g_lastStatsMs;
}

void loop() {
  const uint32_t nowMs = millis();
  if (!g_initOk) {
    delay(250);
    return;
  }

  if (!g_ble.isConnected()) {
    if (g_wasConnected) {
      g_wasConnected = false;
      g_connectionAnnounced = false;
      g_stressArmingLogged = false;
      g_stressStartedLogged = false;
      Serial.print("disconnected\r\n");
      Gpio::write(kPinUserLed, true);
    }

    BleAdvInteraction adv{};
    (void)g_ble.advertiseInteractEvent(&adv, 350U, 300000UL, 700000UL);
    if (adv.receivedConnectInd) {
      Serial.print("connect_ind peer=");
      printAddress(adv.peerAddress);
      Serial.print(" rssi=");
      Serial.print(adv.rssiDbm);
      Serial.print("\r\n");
    } else if ((nowMs - g_lastAdvLogMs) >= 2000UL) {
      g_lastAdvLogMs = nowMs;
      Serial.print("advertising init=");
      Serial.print(g_initOk ? "OK" : "FAIL");
      Serial.print("\r\n");
    }
    delay(25);
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    g_connectionAnnounced = false;
    g_lastBatteryStepMs = nowMs;
    g_lastNotifyMs = nowMs;
    g_lastStatsMs = nowMs;
    g_connectionStartMs = nowMs;
    g_stressArmingLogged = false;
    g_stressStartedLogged = false;
    g_ble.clearEncryptionDebugCounters();
  }

  if (!g_connectionAnnounced) {
    BleConnectionInfo info{};
    if (g_ble.getConnectionInfo(&info)) {
      Serial.print("connected peer=");
      printAddress(info.peerAddress);
      Serial.print(" int=");
      Serial.print(info.intervalUnits);
      Serial.print(" lat=");
      Serial.print(info.latency);
      Serial.print(" timeout=");
      Serial.print(info.supervisionTimeoutUnits);
      Serial.print("\r\n");
    } else {
      Serial.print("connected\r\n");
    }
    g_connectionAnnounced = true;
    Gpio::write(kPinUserLed, false);
  }

  const bool stressActive = (nowMs - g_connectionStartMs) >= kConnectionWarmupMs;
  if (!stressActive && ((nowMs - g_connectionStartMs) >= (kConnectionWarmupMs - 1000UL))) {
    if (!g_stressArmingLogged) {
      Serial.print("stress_arming\r\n");
      g_stressArmingLogged = true;
    }
  }
  if (stressActive) {
    if (!g_stressStartedLogged) {
      Serial.print("stress_active\r\n");
      g_stressStartedLogged = true;
    }
  } else {
    g_stressStartedLogged = false;
  }

  if ((nowMs - g_lastBatteryStepMs) >= kBatteryStepMs) {
    g_lastBatteryStepMs = nowMs;
    g_batteryLevel = (g_batteryLevel > 5U)
                         ? static_cast<uint8_t>(g_batteryLevel - 1U)
                         : 100U;
    (void)g_ble.setGattBatteryLevel(g_batteryLevel);
  }

  if (g_notifyEchoPending &&
      g_ble.isCustomGattCccdEnabled(g_notifyValueHandle, false)) {
    g_notifyEchoPending = false;
    queueEchoNotify();
  } else if ((nowMs - g_lastNotifyMs) >= kNotifyPeriodMs &&
             g_ble.isCustomGattCccdEnabled(g_notifyValueHandle, false)) {
    g_lastNotifyMs = nowMs;
    queuePeriodicNotify();
  }

  BleConnectionEvent evt{};
  if (g_ble.pollConnectionEvent(&evt, 1000UL) && evt.eventStarted) {
    ++g_eventCount;
    if (evt.packetReceived && evt.crcOk) {
      ++g_eventRxOkCount;
    } else if (!evt.packetReceived) {
      ++g_eventRxTimeoutCount;
    }
    if (!evt.txPacketSent) {
      ++g_eventTxTimeoutCount;
    }
    if (evt.attPacket) {
      ++g_eventAttCount;
    }
    if (evt.llControlPacket) {
      ++g_eventLlCount;
    }
    if (evt.txPayloadLength >= 8U && evt.txPayload != nullptr &&
        evt.txPayload[4] == 0x1BU) {
      ++g_notifySentCount;
    }
    if (evt.terminateInd) {
      Serial.print("link terminated\r\n");
      Gpio::write(kPinUserLed, true);
    }
  }

  if ((nowMs - g_lastStatsMs) >= kStatsPeriodMs) {
    printConnectionStats(nowMs);
  }

  if (stressActive) {
    burnForegroundCpu(kForegroundBusyMs);
    delay(kForegroundDelayMs);
  } else {
    delay(1);
  }
}
