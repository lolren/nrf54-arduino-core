/*
 * BleNordicUartBridge
 *
 * Transparent two-way bridge between USB-CDC Serial and the Nordic UART Service
 * (NUS). Bytes typed in a serial terminal on the PC appear via NUS TX notification
 * on the BLE central; bytes received from the central over NUS RX are printed to
 * the PC terminal.
 *
 * Use with any NUS-capable BLE app (nRF Toolbox, Serial Bluetooth Terminal, etc.).
 *
 * Setup:
 *   1. Flash this sketch.
 *   2. Open a serial terminal at 115200 baud (the USB-CDC port).
 *   3. Connect from a BLE app — the device advertises as "X54-NUS".
 *   4. Enable notifications in the app — the bridge is now live.
 *   5. Anything typed in the serial terminal is forwarded to the BLE central,
 *      and vice versa.
 *
 * Tuning flags (change at the top of the file):
 *   kEnableBridgeLogs  — print connect/disconnect events and periodic stats
 *                        to Serial (corrupts bridged data, off by default).
 *   kUseFixedAddress   — opt into a hard-coded BLE address when you need a
 *                        stable test identity across reflashes.
 *   kEnableSelfTestTx  — ignore USB input; send a known ASCII pattern instead,
 *                        useful for isolating BLE TX corruption from USB issues.
 *   kEnableBleBgService — arm the background GRTC service on connect so ATT
 *                         responses survive a busy main loop.
 *
 * Gotcha: kEnableBridgeLogs corrupts the bridged byte stream because log lines
 * are interleaved with application data on the same Serial port. Keep it off
 * in production; enable only for debugging.
 */

#include <Arduino.h>

#include "ble_nus.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static BleNordicUart g_nus(g_ble);
static PowerManager g_power;

static bool g_wasConnected = false;
static bool g_bannerSent = false;
static bool g_securityRequested = false;
static uint32_t g_lastStatusMs = 0U;
static uint32_t g_lastSelfTestMs = 0U;
static uint32_t g_lastBleDbgMs = 0U;
static uint32_t g_connectedAtMs = 0U;
static uint32_t g_usbDroppedBytes = 0U;
static uint32_t g_bleDroppedBytes = 0U;
volatile uint32_t g_dbgLastEventCounter = 0U;
volatile uint8_t g_dbgLastPeerAcked = 0U;
volatile uint8_t g_dbgLastFreshTxAllowed = 0U;
volatile uint8_t g_dbgLastTxPacketSent = 0U;
volatile uint8_t g_dbgLastTxPayloadLength = 0U;
volatile uint8_t g_dbgLastTxLlid = 0U;
volatile uint8_t g_dbgLastTxPayload0 = 0U;
volatile uint8_t g_dbgLastTxPayload4 = 0U;
volatile uint8_t g_dbgLastNotifyEnabled = 0U;
volatile uint8_t g_dbgLastQueueResult = 0U;
volatile uint8_t g_dbgLastTxInFlight = 0U;
volatile uint8_t g_dbgLastTxAwaitingAck = 0U;
volatile uint16_t g_dbgLastTxCount = 0U;
volatile uint32_t g_dbgNusSentCount = 0U;
volatile uint32_t g_dbgNusRetiredCount = 0U;

static constexpr uint16_t kUsbToBleBufferSize = 2048U;
static constexpr uint16_t kBleToUsbBufferSize = 2048U;
static uint16_t g_usbToBleHead = 0U;
static uint16_t g_usbToBleTail = 0U;
static uint16_t g_usbToBleCount = 0U;
static uint16_t g_bleToUsbHead = 0U;
static uint16_t g_bleToUsbTail = 0U;
static uint16_t g_bleToUsbCount = 0U;
static uint8_t g_usbToBleBuffer[kUsbToBleBufferSize] = {0};
static uint8_t g_bleToUsbBuffer[kBleToUsbBufferSize] = {0};

static constexpr int8_t kTxPowerDbm = 0;
// Leave the bridged USB CDC stream clean during active sessions. Runtime logs on
// the same Serial port both corrupt application data and can delay BLE polling.
static constexpr bool kEnableBridgeLogs = false;
// Leave this off by default. Several phones, including Xperia 10 III, are more
// discoverable with the factory-derived BLE address than with a fixed example
// random-static address.
static constexpr bool kUseFixedAddress = false;
// Debug aid: when enabled, the sketch generates a known ASCII stream internally
// (no USB input) to help isolate BLE TX corruption vs. USB-UART bridge issues.
static constexpr bool kEnableSelfTestTx = false;
// Keep the background BLE service enabled on the live bridge. The quiet probe
// already relies on it, and it prevents USB CDC bursts from starving NUS TX
// scheduling on some hosts.
static constexpr bool kEnableBleBgService = true;
static constexpr uint32_t kBridgeWarmupMs = 500UL;
static constexpr bool kRequestLinkSecurity = false;
static constexpr uint32_t kConnectionPollTimeoutUs = 450000UL;
static constexpr uint16_t kEventNoopStageBytes = 256U;
static constexpr uint16_t kEventPumpBytes = 512U;
static constexpr uint16_t kEventStageBytes = 512U;
static constexpr uint16_t kPumpChunkBytes = 512U;
static constexpr uint16_t kBleWriteChunkBytes = BleNordicUart::kMaxPayloadLength;
static constexpr uint32_t kStatusPeriodMs = 2000UL;
static constexpr uint32_t kDebugPeriodMs = 1000UL;
static const uint8_t kAddress[6] = {0x38, 0x00, 0x15, 0x54, 0xDE, 0xC0};
// Keep the primary ADV payload short and self-contained so stricter phone
// scanners do not depend on a SCAN_RSP round-trip or a 128-bit UUID filter.
static constexpr char kDeviceName[] = "X54-NUS";
// Keep the startup banner within one notification so the live bridge path
// starts from a clean TX queue.
static constexpr char kReadyBanner[] = "X54 NUS ready\r\n";
static const uint8_t kNusAdvPayload[] = {
    2, 0x01, 0x06,
    8, 0x09, 'X', '5', '4', '-', 'N', 'U', 'S',
    5, 0xFF, 0x34, 0x12, 0x54, 0x15,
};

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

static const char* disconnectReasonLabel(uint8_t reason) {
  switch (reason) {
    case 0x08:
      return "timeout";
    case 0x13:
      return "remote user";
    case 0x16:
      return "local host";
    case 0x3D:
      return "mic failure";
    default:
      return "other";
  }
}

static uint16_t advanceBridgeIndex(uint16_t index, uint16_t capacity) {
  ++index;
  if (index >= capacity) {
    index = 0U;
  }
  return index;
}

static void clearBridgeBuffers() {
  g_usbToBleHead = 0U;
  g_usbToBleTail = 0U;
  g_usbToBleCount = 0U;
  g_bleToUsbHead = 0U;
  g_bleToUsbTail = 0U;
  g_bleToUsbCount = 0U;
}

static void printBleDebug(uint32_t nowMs) {
  if ((nowMs - g_lastBleDbgMs) < kDebugPeriodMs) {
    return;
  }
  g_lastBleDbgMs = nowMs;

  BleEncryptionDebugCounters dbg{};
  g_ble.getEncryptionDebugCounters(&dbg);
  Serial.print("ble_dbg late_poll=");
  Serial.print(dbg.connLatePollCount);
  Serial.print(" missed_last=");
  Serial.print(dbg.connMissedEventCountLast);
  Serial.print(" missed_max=");
  Serial.print(dbg.connMissedEventCountMax);
  Serial.print(" rx_timeout=");
  Serial.print(dbg.connRxTimeoutCount);
  Serial.print(" tx_timeout=");
  Serial.print(dbg.connTxTimeoutCount);
  Serial.print(" follow_timeout=");
  Serial.print(dbg.connFollowupRxTimeoutCount);
  Serial.print(" follow_crc_err=");
  Serial.print(dbg.connFollowupRxCrcErrorCount);
  Serial.print(" tx_lag_last=");
  Serial.print(dbg.txenLagLastUs);
  Serial.print(" tx_lag_max=");
  Serial.print(dbg.txenLagMaxUs);
  Serial.print(" tx_late=");
  Serial.print(dbg.txenLateCount);
  Serial.print(" q=");
  Serial.print(g_usbToBleCount);
  Serial.print("/");
  Serial.print(g_bleToUsbCount);
  Serial.print("\r\n");
}

static bool bridgeWarmupElapsed(uint32_t nowMs) {
  return (nowMs - g_connectedAtMs) >= kBridgeWarmupMs;
}

static void maybeRequestLinkSecurity(uint32_t nowMs) {
  if (!kRequestLinkSecurity || g_securityRequested || !g_ble.isConnected()) {
    return;
  }
  if (!bridgeWarmupElapsed(nowMs)) {
    return;
  }
  g_securityRequested = true;
  g_ble.sendSmpSecurityRequest();
}

static void stageUsbToBle(int maxBytes) {
  int budget = maxBytes;
  while (budget > 0 && Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch < 0) {
      break;
    }
    if (g_usbToBleCount >= kUsbToBleBufferSize) {
      ++g_usbDroppedBytes;
    } else {
      g_usbToBleBuffer[g_usbToBleHead] = static_cast<uint8_t>(ch);
      g_usbToBleHead = advanceBridgeIndex(g_usbToBleHead, kUsbToBleBufferSize);
      ++g_usbToBleCount;
    }
    --budget;
  }
}

static void pumpUsbToBle(int maxBytes) {
  if (!g_nus.isNotifyEnabled() || g_usbToBleCount == 0U) {
    return;
  }

  int budget = g_nus.availableForWrite();
  if (budget > maxBytes) {
    budget = maxBytes;
  }
  if (g_usbToBleCount < static_cast<uint16_t>(budget)) {
    budget = static_cast<int>(g_usbToBleCount);
  }
  if (budget <= 0) {
    return;
  }

  uint8_t chunk[kBleWriteChunkBytes] = {0};
  while (budget > 0 && g_usbToBleCount > 0U) {
    int chunkBudget = budget;
    if (chunkBudget > static_cast<int>(sizeof(chunk))) {
      chunkBudget = static_cast<int>(sizeof(chunk));
    }
    if (g_usbToBleCount < static_cast<uint16_t>(chunkBudget)) {
      chunkBudget = static_cast<int>(g_usbToBleCount);
    }
    if (chunkBudget <= 0) {
      break;
    }

    uint16_t index = g_usbToBleTail;
    for (int i = 0; i < chunkBudget; ++i) {
      chunk[i] = g_usbToBleBuffer[index];
      index = advanceBridgeIndex(index, kUsbToBleBufferSize);
    }

    const size_t written = g_nus.write(chunk, static_cast<size_t>(chunkBudget));
    if (written == 0U) {
      break;
    }
    for (size_t i = 0; i < written; ++i) {
      g_usbToBleTail = advanceBridgeIndex(g_usbToBleTail, kUsbToBleBufferSize);
      --g_usbToBleCount;
    }
    budget -= static_cast<int>(written);
    if (written < static_cast<size_t>(chunkBudget)) {
      break;
    }
  }
}

static void stageBleToUsb(int maxBytes) {
  int budget = maxBytes;
  while (budget > 0 && g_nus.available() > 0) {
    const int ch = g_nus.read();
    if (ch < 0) {
      break;
    }
    if (g_bleToUsbCount >= kBleToUsbBufferSize) {
      ++g_bleDroppedBytes;
    } else {
      g_bleToUsbBuffer[g_bleToUsbHead] = static_cast<uint8_t>(ch);
      g_bleToUsbHead = advanceBridgeIndex(g_bleToUsbHead, kBleToUsbBufferSize);
      ++g_bleToUsbCount;
    }
    --budget;
  }
}

static void pumpBleToUsb(int maxBytes) {
  int budget = maxBytes;
  while (budget > 0 && g_bleToUsbCount > 0U) {
    const size_t written = Serial.write(g_bleToUsbBuffer[g_bleToUsbTail]);
    if (written != 1U) {
      break;
    }
    g_bleToUsbTail = advanceBridgeIndex(g_bleToUsbTail, kBleToUsbBufferSize);
    --g_bleToUsbCount;
    --budget;
  }
}

static void selfTestTx() {
  if (!kEnableSelfTestTx || !g_nus.isNotifyEnabled()) {
    return;
  }

  const uint32_t nowMs = millis();
  if ((nowMs - g_lastSelfTestMs) < 100U) {
    return;
  }
  g_lastSelfTestMs = nowMs;

  static const uint8_t kPattern[] = "0123456789ABCDEF\r\n";
  if (g_nus.availableForWrite() >= static_cast<int>(sizeof(kPattern) - 1U)) {
    g_nus.write(kPattern, sizeof(kPattern) - 1U);
  }
}

static void snapshotBridgeDebug(const BleConnectionEvent* event) {
  g_dbgLastNotifyEnabled = g_nus.isNotifyEnabled() ? 1U : 0U;
  g_dbgLastQueueResult = g_nus.debugLastQueueResult();
  g_dbgLastTxInFlight = g_nus.debugTxNotificationInFlight() ? 1U : 0U;
  g_dbgLastTxAwaitingAck = g_nus.debugTxNotificationAwaitingAck() ? 1U : 0U;
  g_dbgLastTxCount = g_nus.debugTxCount();
  g_dbgNusSentCount = g_nus.debugNotificationSentCount();
  g_dbgNusRetiredCount = g_nus.debugNotificationRetiredCount();
  if (event == nullptr) {
    return;
  }
  g_dbgLastEventCounter = event->eventCounter;
  g_dbgLastPeerAcked = event->peerAckedLastTx ? 1U : 0U;
  g_dbgLastFreshTxAllowed = event->freshTxAllowed ? 1U : 0U;
  g_dbgLastTxPacketSent = event->txPacketSent ? 1U : 0U;
  g_dbgLastTxPayloadLength = event->txPayloadLength;
  g_dbgLastTxLlid = event->txLlid;
  g_dbgLastTxPayload0 =
      (event->txPayload != nullptr && event->txPayloadLength > 0U) ? event->txPayload[0] : 0U;
  g_dbgLastTxPayload4 =
      (event->txPayload != nullptr && event->txPayloadLength > 4U) ? event->txPayload[4] : 0U;
}

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleNordicUartBridge start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);
  Gpio::configure(kPinUserButton, GpioDirection::kInput, GpioPull::kPullUp);

  bool buttonPressed = true;
  if (!Gpio::read(kPinUserButton, &buttonPressed)) {
    buttonPressed = false;
  } else {
    buttonPressed = !buttonPressed;  // Active low.
  }
  if (buttonPressed) {
    g_ble.clearBondRecord(true);
    Serial.print("bond cleared (button held at boot)\r\n");
  }

  bool ok = BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
  if (ok) {
    ok = g_ble.begin(kTxPowerDbm);
  }

  if (ok) {
    // The user-facing bridge spends most of its life tethered over USB and
    // benefits more from deterministic serial/BLE polling than from shaving
    // idle current.
    g_power.setLatencyMode(PowerLatencyMode::kConstantLatency);
  }
  if (ok) {
    ok = (!kUseFixedAddress ||
          g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic)) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingChannelSelectionAlgorithm2(true) &&
         g_ble.setGattDeviceName(kDeviceName) &&
         g_ble.setGattBatteryLevel(100U) &&
         g_ble.clearCustomGatt() &&
         g_nus.begin() &&
         g_ble.setAdvertisingData(kNusAdvPayload, sizeof(kNusAdvPayload)) &&
         g_ble.setScanResponseData(nullptr, 0U);
  }

  Serial.print("BLE NUS init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (ok) {
    uint8_t addr[6] = {0};
    BleAddressType type = BleAddressType::kPublic;
    if (g_ble.getDeviceAddress(addr, &type)) {
      Serial.print("addr=");
      printAddress(addr);
      Serial.print(" type=");
      Serial.print((type == BleAddressType::kRandomStatic) ? "random" : "public");
      Serial.print("\r\n");
    }
    Serial.print("Advertised as X54-NUS. Open a Nordic UART client and bridge bytes.\r\n");
  }
}

void loop() {
  if (!g_ble.isConnected()) {
    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    g_nus.service();
    if (kEnableBridgeLogs && g_wasConnected) {
      printBleDebug(millis());
    }

    if (g_wasConnected) {
      g_wasConnected = false;
      g_bannerSent = false;
      g_securityRequested = false;
      clearBridgeBuffers();
      Gpio::write(kPinUserLed, true);
      g_lastBleDbgMs = 0U;
      if (kEnableBridgeLogs) {
        Serial.print("\r\nBLE client disconnected\r\n");
      }
    }

    // Only pace the advertising loop when we are still disconnected.
    // Sleeping here after advertiseInteractEvent() accepted a CONNECT_IND
    // delays the first pollConnectionEvent() by ~20 ms, causing Android
    // (which often uses a 7.5 ms connection interval) to miss several
    // connection events before synchronisation is established.
    if (!g_ble.isConnected()) {
      delay(100);
    }
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    g_bannerSent = false;
    g_securityRequested = false;
    g_connectedAtMs = millis();
    g_lastBleDbgMs = 0U;
    if (kEnableBleBgService) {
      g_ble.setBackgroundConnectionServiceEnabled(true);
    }
    Gpio::write(kPinUserLed, false);
    if (kEnableBridgeLogs) {
      Serial.print("\r\nBLE client connected\r\n");
    }
  }

  // Keep BLE connection-event polling tight; missing anchors leads to 0x08 timeouts.
  BleConnectionEvent evt{};
  const bool eventStarted =
      g_ble.pollConnectionEvent(&evt, kConnectionPollTimeoutUs) && evt.eventStarted;

  // The HAL deferred event queue is not flushed on disconnect. The first
  // pollConnectionEvent() of a new connection may return the previous
  // connection's terminateInd=true. Passing that stale event to
  // g_nus.service() resets the NUS CCCD state, forcing Android to re-discover
  // and re-write it — causing the "serial not connected" symptom.
  if (evt.terminateInd && g_ble.isConnected()) {
    evt.terminateInd = false;
  }

  if (!eventStarted) {
    g_nus.service();
    if (kEnableBridgeLogs) {
      snapshotBridgeDebug(nullptr);
    }
    const uint32_t nowMs = millis();
    maybeRequestLinkSecurity(nowMs);
    if (!bridgeWarmupElapsed(nowMs)) {
      return;
    }
    stageUsbToBle(kEventNoopStageBytes);
    stageBleToUsb(kEventNoopStageBytes);
    pumpUsbToBle(kEventPumpBytes);
    pumpBleToUsb(kEventPumpBytes);
    return;
  }

  g_nus.service(&evt);
  if (kEnableBridgeLogs) {
    snapshotBridgeDebug(&evt);
  }
  const uint32_t nowMs = millis();
  maybeRequestLinkSecurity(nowMs);
  if (kEnableBridgeLogs) {
    printBleDebug(nowMs);
  }
  if (evt.terminateInd) {
    if (kEnableBridgeLogs) {
      Serial.print("BLE link terminated");
      if (evt.disconnectReasonValid) {
        Serial.print(" reason=0x");
        if (evt.disconnectReason < 0x10U) {
          Serial.print('0');
        }
        Serial.print(evt.disconnectReason, HEX);
        Serial.print(" (");
        Serial.print(disconnectReasonLabel(evt.disconnectReason));
        Serial.print(", ");
        Serial.print(evt.disconnectReasonRemote ? "peer" : "local");
        Serial.print(")");
      }
      Serial.print("\r\n");
    }
  }

  if (!bridgeWarmupElapsed(nowMs)) {
    return;
  }

  // Only pump UART after a real connection event.
  selfTestTx();
  if (!kEnableSelfTestTx) {
    stageUsbToBle(kEventStageBytes);
  }
  stageBleToUsb(kEventStageBytes);
  pumpUsbToBle(kPumpChunkBytes);
  pumpBleToUsb(kPumpChunkBytes);

  if (!g_bannerSent && g_nus.isNotifyEnabled()) {
    g_bannerSent = true;
    g_nus.write(reinterpret_cast<const uint8_t*>(kReadyBanner), sizeof(kReadyBanner) - 1U);
  }

  if (kEnableBridgeLogs && (nowMs - g_lastStatusMs) >= kStatusPeriodMs) {
    g_lastStatusMs = nowMs;
    Serial.print("notify=");
    Serial.print(g_nus.isNotifyEnabled() ? "on" : "off");
    Serial.print(" rx_drop=");
    Serial.print(g_nus.rxDroppedBytes());
    Serial.print(" tx_drop=");
    Serial.print(g_nus.txDroppedBytes());
    Serial.print(" usb_drop=");
    Serial.print(g_usbDroppedBytes);
    Serial.print(" ble_drop=");
    Serial.print(g_bleDroppedBytes);
    Serial.print(" usb_q=");
    Serial.print(g_usbToBleCount);
    Serial.print(" ble_q=");
    Serial.print(g_bleToUsbCount);
    Serial.print("\r\n");
  }

  // No delay(): we want loop() to spin quickly so pollConnectionEvent() lands on anchors.
}
