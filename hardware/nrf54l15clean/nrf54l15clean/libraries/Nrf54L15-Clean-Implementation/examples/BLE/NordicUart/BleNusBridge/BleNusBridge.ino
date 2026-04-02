/*
 * BleNusBridge
 *
 * Simpler variant of BleNordicUartBridge: a transparent two-way bridge between
 * USB-CDC Serial and the Nordic UART Service (NUS). Receives bytes from the BLE
 * central via NUS RX and prints them to Serial; reads bytes from Serial and sends
 * them to the central via NUS TX notifications.
 *
 * Unlike BleNordicUartBridge, this version:
 *   - Always logs connect/disconnect and periodic stats to Serial.
 *   - Arms the background GRTC service on connect (same as BleNordicUartBridge)
 *     so ATT/SMP responses land even when the main loop is briefly delayed.
 *   - The NUS UUID is embedded in the ad packet by ble_nus.begin(); the short
 *     device name fits alongside it so passive scanners see the name directly.
 *
 * Use with any NUS-capable BLE app (nRF Toolbox, Serial Bluetooth Terminal, etc.).
 *
 * Steps:
 *   1. Flash and open a serial terminal at 115200 baud.
 *   2. Connect from a BLE app — device advertises as "X54-NUS".
 *   3. Enable notifications in the app and start typing on either side.
 *
 * Gotcha: Serial.print() inside loop() adds latency. If bytes are dropping,
 * reduce or remove the printStreamingStats() call, or switch to BleNordicUartBridge
 * which has a kEnableBridgeLogs flag to silence logs on the bridged port.
 */

#include <Arduino.h>

#include "ble_nus.h"

using namespace xiao_nrf54l15;

namespace {

BleRadio g_ble;
BleNordicUart g_nus(g_ble);
PowerManager g_power;

constexpr uint16_t kUsbToBleBufferSize = 1024U;   // Staging buffer for USB→BLE direction.
constexpr uint8_t kUsbToBleChunkBytes = 20U;       // Max bytes per NUS TX notification.
constexpr uint16_t kUsbStageBudgetBytes = 128U;    // Bytes drained from USB per event.
constexpr bool kEnableBleBgService = false;
// pollConnectionEvent spin limit in µs. Keep short so we don't miss 7.5 ms anchors.
constexpr uint32_t kConnectionPollTimeoutUs = 3000UL;
constexpr uint32_t kBridgeWarmupMs = 500UL;
constexpr bool kRequestLinkSecurity = false;
constexpr uint32_t kStatusPeriodMs = 2000UL;       // How often to print streaming stats.
constexpr uint8_t kNoBytes = 0U;

bool g_wasConnected = false;
bool g_bannerSent = false;
bool g_securityRequested = false;
uint32_t g_lastStatusMs = 0U;
uint32_t g_connectedAtMs = 0U;
uint32_t g_connSessionRxDropped = 0U;
uint32_t g_connSessionTxDropped = 0U;
uint16_t g_usbToBleHead = 0U;
uint16_t g_usbToBleTail = 0U;
uint16_t g_usbToBleCount = 0U;
uint8_t g_usbToBleBuffer[kUsbToBleBufferSize] = {0};

}  // namespace

// 0 dBm: standard output power, ~10 m indoor range. Range: -40 to +8 dBm.
constexpr int8_t kTxPowerDbm = 0;
// Unique address — avoids Android reusing a stale GATT cache from another sketch.
// Bump it when this sketch's advertised/GATT identity changes.
constexpr uint8_t kAddress[6] = {0x3A, 0x00, 0x15, 0x54, 0xDE, 0xC0};
// Device name ≤ 8 chars so it fits alongside the 128-bit NUS UUID in the 31-byte
// ad payload (3 flags + 18 UUID + 9 name = 30 bytes). Passive scanners (e.g.
// Windows) see the name without needing an active-scan SCAN_RSP exchange.
constexpr char kDeviceName[] = "X54-NUS";

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

static uint16_t advanceIndex(uint16_t index, uint16_t capacity) {
  ++index;
  if (index >= capacity) {
    index = 0U;
  }
  return index;
}

static void resetBridgeBuffers() {
  g_usbToBleHead = 0U;
  g_usbToBleTail = 0U;
  g_usbToBleCount = 0U;
}

static void stageUsbToBle(int maxBytes) {
  const int queueSpace = static_cast<int>(kUsbToBleBufferSize - g_usbToBleCount);
  if (queueSpace <= 0) {
    return;
  }
  int budget = maxBytes;
  if (budget > queueSpace) {
    budget = queueSpace;
  }
  while (budget > 0 && Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch < 0) {
      break;
    }

    g_usbToBleBuffer[g_usbToBleHead] = static_cast<uint8_t>(ch);
    g_usbToBleHead = advanceIndex(g_usbToBleHead, kUsbToBleBufferSize);
    ++g_usbToBleCount;
    --budget;
  }
}

static void pumpUsbToBle() {
  if (!g_nus.isNotifyEnabled() || g_usbToBleCount == 0U) {
    return;
  }

  const int payloadLimit = static_cast<int>(g_nus.maxPayloadLength());
  if (payloadLimit <= 0) {
    return;
  }
  int budget = g_nus.availableForWrite();
  if (budget > static_cast<int>(payloadLimit)) {
    budget = static_cast<int>(payloadLimit);
  }
  if (budget > g_usbToBleCount) {
    budget = static_cast<int>(g_usbToBleCount);
  }
  if (budget > static_cast<int>(kUsbToBleChunkBytes)) {
    budget = static_cast<int>(kUsbToBleChunkBytes);
  }

  if (budget <= 0) {
    return;
  }

  uint8_t chunk[kUsbToBleChunkBytes] = {kNoBytes};
  uint16_t index = g_usbToBleTail;
  for (int i = 0; i < budget; ++i) {
    chunk[i] = g_usbToBleBuffer[index];
    index = advanceIndex(index, kUsbToBleBufferSize);
  }
  const size_t written = g_nus.write(chunk, static_cast<size_t>(budget));
  for (size_t i = 0; i < written; ++i) {
    g_usbToBleTail = advanceIndex(g_usbToBleTail, kUsbToBleBufferSize);
    --g_usbToBleCount;
  }
}

static void printDropCounters() {
  const uint32_t txDropped = g_nus.txDroppedBytes();
  const uint32_t rxDropped = g_nus.rxDroppedBytes();
  if (txDropped <= g_connSessionTxDropped && rxDropped <= g_connSessionRxDropped) {
    return;
  }

  Serial.print("bridge_drops tx=");
  Serial.print(txDropped - g_connSessionTxDropped);
  Serial.print(" rx=");
  Serial.print(rxDropped - g_connSessionRxDropped);
  Serial.print(" queued=");
  Serial.print(g_usbToBleCount);
  Serial.print("\r\n");
}

static void printStreamingStats() {
  const uint32_t nowMs = millis();
  if ((nowMs - g_lastStatusMs) < kStatusPeriodMs) {
    return;
  }
  g_lastStatusMs = nowMs;
  Serial.print("stats pending_tx=");
  Serial.print(g_nus.availableForWrite());
  Serial.print(" usb_q=");
  Serial.print(g_usbToBleCount);
  Serial.print(" usb_avail=");
  Serial.print(Serial.available());
  Serial.print(" rxq=");
  Serial.print(g_nus.available());
  Serial.print(" mtu=");
  Serial.print(g_nus.maxPayloadLength());
  Serial.print(" notify=");
  Serial.print(g_nus.isNotifyEnabled() ? "on" : "off");
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

static void pumpUsbToBleWhenReady(int maxBytes) {
  stageUsbToBle(maxBytes);
  if (g_nus.isNotifyEnabled() && g_usbToBleCount > 0U) {
    pumpUsbToBle();
  }
}

static void pumpBleToUsb(int maxBytes) {
  int budget = maxBytes;
  while (budget > 0 && g_nus.available() > 0) {
    const int ch = g_nus.read();
    if (ch < 0) {
      break;
    }
    Serial.write(static_cast<uint8_t>(ch));
    --budget;
  }
}

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleNordicUartBridge start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  // kCeramic: route RF through the on-board ceramic chip antenna (default).
  // Use kExternal if a u.FL external antenna is connected.
  bool ok = BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
  if (ok) {
    ok = g_ble.begin(kTxPowerDbm) &&
         g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         // kAdvInd: connectable + scannable undirected (standard advertising mode).
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         // Name ≤ 8 chars embeds in the ad packet by setAdvertisingServiceUuid128
         // (called inside g_nus.begin()) alongside the NUS UUID, so passive
         // scanners see it without a SCAN_RSP exchange.
         g_ble.setGattDeviceName(kDeviceName) &&
         // Remove any leftover custom GATT services from a previous sketch.
         g_ble.clearCustomGatt() && g_nus.begin();
  }
  if (ok) {
    // kConstantLatency: keeps the CPU clock and radio wake timing deterministic.
    // Set after BLE init so the radio subsystem is already configured.
    g_power.setLatencyMode(PowerLatencyMode::kConstantLatency);
  }

  Serial.print("BLE NUS init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (ok) {
    Serial.print("Advertised as X54-NUS. Open a Nordic UART client and bridge bytes.\r\n");
  }
}

void loop() {
  if (!g_ble.isConnected()) {
    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    g_nus.service();

    if (g_wasConnected) {
      g_wasConnected = false;
      g_bannerSent = false;
      g_securityRequested = false;
      resetBridgeBuffers();
      printDropCounters();
      printStreamingStats();
      Gpio::write(kPinUserLed, true);
      Serial.print("\r\nBLE client disconnected\r\n");
    }

    // Only pace the advertising loop when still disconnected after
    // advertiseInteractEvent(). If a CONNECT_IND was just accepted, skip the
    // delay so the first pollConnectionEvent() reaches the anchor on time.
    if (!g_ble.isConnected()) {
      delay(20);
    }
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    g_bannerSent = false;
    g_securityRequested = false;
    g_connectedAtMs = millis();
    g_connSessionRxDropped = g_nus.rxDroppedBytes();
    g_connSessionTxDropped = g_nus.txDroppedBytes();
    resetBridgeBuffers();
    // Arm the background GRTC service so ATT/SMP responses are handled even if
    // the main loop is briefly delayed by Serial.print() calls.
    if (kEnableBleBgService) {
      g_ble.setBackgroundConnectionServiceEnabled(true);
    }
    Gpio::write(kPinUserLed, false);
    Serial.print("\r\nBLE client connected\r\n");
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
    const uint32_t nowMs = millis();
    maybeRequestLinkSecurity(nowMs);
    if (!bridgeWarmupElapsed(nowMs)) {
      return;
    }
    stageUsbToBle(2);
    pumpUsbToBleWhenReady(2);
    return;
  }

  g_nus.service(&evt);
  const uint32_t nowMs = millis();
  maybeRequestLinkSecurity(nowMs);
  if (evt.terminateInd) {
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
    printDropCounters();
  }

  if (!bridgeWarmupElapsed(nowMs)) {
    return;
  }

  // Only pump UART after a real connection event.
  pumpUsbToBleWhenReady(static_cast<int>(kUsbStageBudgetBytes));
  pumpBleToUsb(16);
  printStreamingStats();

  if (!g_bannerSent && g_nus.isNotifyEnabled()) {
    g_bannerSent = true;
    g_nus.print("X54 Nordic UART bridge ready\r\n");
  }
}
