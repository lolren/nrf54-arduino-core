/*
 * BleNusBridge
 *
 * Simpler variant of BleNordicUartBridge: a transparent two-way bridge between
 * USB-CDC Serial and the Nordic UART Service (NUS). Receives bytes from the BLE
 * central via NUS RX and prints them to Serial; reads bytes from Serial and sends
 * them to the central via NUS TX notifications.
 *
 * Unlike BleNordicUartBridge, this version:
 *   - Keeps a smaller feature surface and less runtime debug state.
 *   - Can optionally log connect/disconnect and periodic stats to Serial.
 *   - Arms the background GRTC service on connect when requested.
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
 * Gotcha: any Serial logging while bridged data is flowing steals bandwidth from
 * the same USB CDC port. Keep kEnableBridgeLogs off in production.
 */

#include <Arduino.h>

#include "ble_nus.h"

using namespace xiao_nrf54l15;

namespace {

BleRadio g_ble;
BleNordicUart g_nus(g_ble);
PowerManager g_power;

constexpr uint16_t kUsbToBleBufferSize = 2048U;    // Staging buffer for USB→BLE direction.
constexpr uint8_t kUsbToBleChunkBytes = BleNordicUart::kMaxPayloadLength;
constexpr uint16_t kEventNoopStageBytes = 256U;    // Bytes staged between connection events.
constexpr uint16_t kEventStageBytes = 512U;        // Bytes staged on a real connection event.
constexpr uint16_t kEventPumpBytes = 512U;         // Bytes pumped per loop pass.
constexpr uint16_t kBleWriteChunkBytes =
    BleNordicUart::kMaxPayloadLength;              // One full notify-sized staging chunk.
constexpr bool kEnableBridgeLogs = false;
constexpr bool kEnableBleBgService = false;
// pollConnectionEvent spin limit in µs. Keep short so we don't miss 7.5 ms anchors.
constexpr uint32_t kConnectionPollTimeoutUs = 2000UL;
constexpr uint32_t kBridgeWarmupMs = 500UL;
constexpr bool kRequestLinkSecurity = false;
constexpr uint32_t kStatusPeriodMs = 2000UL;       // How often to print streaming stats.
constexpr uint8_t kNoBytes = 0U;

bool g_wasConnected = false;
bool g_bannerSent = false;
bool g_securityRequested = false;
uint32_t g_lastStatusMs = 0U;
uint32_t g_connectedAtMs = 0U;
uint32_t g_usbDroppedBytes = 0U;
uint32_t g_bleDroppedBytes = 0U;
uint32_t g_connSessionRxDropped = 0U;
uint32_t g_connSessionTxDropped = 0U;
uint16_t g_usbToBleHead = 0U;
uint16_t g_usbToBleTail = 0U;
uint16_t g_usbToBleCount = 0U;
uint16_t g_bleToUsbHead = 0U;
uint16_t g_bleToUsbTail = 0U;
uint16_t g_bleToUsbCount = 0U;
uint8_t g_usbToBleBuffer[kUsbToBleBufferSize] = {0};
uint8_t g_bleToUsbBuffer[kUsbToBleBufferSize] = {0};

}  // namespace

// 0 dBm: standard output power, ~10 m indoor range. Range: -40 to +8 dBm.
constexpr int8_t kTxPowerDbm = 0;
// Optional fixed test address. Leave it off by default because the board's
// factory-derived BLE address is more discoverable on some phones.
constexpr uint8_t kAddress[6] = {0x3A, 0x00, 0x15, 0x54, 0xDE, 0xC0};
static constexpr bool kUseFixedAddress = false;
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
  g_bleToUsbHead = 0U;
  g_bleToUsbTail = 0U;
  g_bleToUsbCount = 0U;
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
      g_usbToBleHead = advanceIndex(g_usbToBleHead, kUsbToBleBufferSize);
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

  uint8_t chunk[kBleWriteChunkBytes] = {kNoBytes};
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
      index = advanceIndex(index, kUsbToBleBufferSize);
    }

    const size_t written = g_nus.write(chunk, static_cast<size_t>(chunkBudget));
    if (written == 0U) {
      break;
    }
    for (size_t i = 0; i < written; ++i) {
      g_usbToBleTail = advanceIndex(g_usbToBleTail, kUsbToBleBufferSize);
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
    if (g_bleToUsbCount >= kUsbToBleBufferSize) {
      ++g_bleDroppedBytes;
    } else {
      g_bleToUsbBuffer[g_bleToUsbHead] = static_cast<uint8_t>(ch);
      g_bleToUsbHead = advanceIndex(g_bleToUsbHead, kUsbToBleBufferSize);
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
    g_bleToUsbTail = advanceIndex(g_bleToUsbTail, kUsbToBleBufferSize);
    --g_bleToUsbCount;
    --budget;
  }
}

static void printDropCounters() {
  if (!kEnableBridgeLogs) {
    return;
  }
  const uint32_t txDropped = g_nus.txDroppedBytes();
  const uint32_t rxDropped = g_nus.rxDroppedBytes();
  if (txDropped <= g_connSessionTxDropped && rxDropped <= g_connSessionRxDropped) {
    return;
  }

  Serial.print("bridge_drops tx=");
  Serial.print(txDropped - g_connSessionTxDropped);
  Serial.print(" rx=");
  Serial.print(rxDropped - g_connSessionRxDropped);
  Serial.print(" usb=");
  Serial.print(g_usbDroppedBytes);
  Serial.print(" ble=");
  Serial.print(g_bleDroppedBytes);
  Serial.print(" queued=");
  Serial.print(g_usbToBleCount);
  Serial.print("/");
  Serial.print(g_bleToUsbCount);
  Serial.print("\r\n");
}

static void printStreamingStats() {
  if (!kEnableBridgeLogs) {
    return;
  }
  const uint32_t nowMs = millis();
  if ((nowMs - g_lastStatusMs) < kStatusPeriodMs) {
    return;
  }
  g_lastStatusMs = nowMs;
  Serial.print("stats pending_tx=");
  Serial.print(g_nus.availableForWrite());
  Serial.print(" usb_q=");
  Serial.print(g_usbToBleCount);
  Serial.print(" ble_q=");
  Serial.print(g_bleToUsbCount);
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
         (!kUseFixedAddress || g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic)) &&
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
      g_usbDroppedBytes = 0U;
      g_bleDroppedBytes = 0U;
      resetBridgeBuffers();
      printDropCounters();
      printStreamingStats();
      Gpio::write(kPinUserLed, true);
      if (kEnableBridgeLogs) {
        Serial.print("\r\nBLE client disconnected\r\n");
      }
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
    g_usbDroppedBytes = 0U;
    g_bleDroppedBytes = 0U;
    g_connSessionRxDropped = g_nus.rxDroppedBytes();
    g_connSessionTxDropped = g_nus.txDroppedBytes();
    resetBridgeBuffers();
    // Arm the background GRTC service so ATT/SMP responses are handled even if
    // the main loop is briefly delayed by Serial.print() calls.
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
    const uint32_t nowMs = millis();
    maybeRequestLinkSecurity(nowMs);
    if (!bridgeWarmupElapsed(nowMs)) {
      return;
    }
    stageUsbToBle(static_cast<int>(kEventNoopStageBytes));
    stageBleToUsb(static_cast<int>(kEventNoopStageBytes));
    pumpUsbToBle(static_cast<int>(kEventPumpBytes));
    pumpBleToUsb(static_cast<int>(kEventPumpBytes));
    return;
  }

  g_nus.service(&evt);
  const uint32_t nowMs = millis();
  maybeRequestLinkSecurity(nowMs);
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
    printDropCounters();
  }

  if (!bridgeWarmupElapsed(nowMs)) {
    return;
  }

  // Only pump UART after a real connection event.
  stageUsbToBle(static_cast<int>(kEventStageBytes));
  stageBleToUsb(static_cast<int>(kEventStageBytes));
  pumpUsbToBle(static_cast<int>(kEventPumpBytes));
  pumpBleToUsb(static_cast<int>(kEventPumpBytes));
  printStreamingStats();

  if (!g_bannerSent && g_nus.isNotifyEnabled()) {
    g_bannerSent = true;
    g_nus.print("X54 Nordic UART bridge ready\r\n");
  }
}
