/*
 * BleNordicUartQuietBridgeProbe
 *
 * A bidirectional USB↔BLE bridge using the Nordic UART Service (NUS).
 * "Quiet" means the Serial port is reserved purely for structured session
 * summaries delimited by @@SUMMARY_BEGIN@@ / @@SUMMARY_END@@. During an
 * active session no runtime log lines are printed, making the output easy
 * to parse by a test harness.
 *
 * Architecture:
 *   USB Serial → g_usbToBleBuffer (ring) → NUS TX notifications → BLE central
 *   BLE central → NUS RX writes → g_bleToUsbBuffer (ring) → USB Serial
 *
 * FNV-1a hashes are computed on both sides for integrity verification.
 * Each session prints byte counts, hashes, and drop counters on disconnect.
 *
 * Tip: kBridgeWarmupMs delays data pumping after connection to let the phone
 * finish service discovery before the first NUS notification arrives.
 */

#include <Arduino.h>

#include "ble_nus.h"

using namespace xiao_nrf54l15;

namespace {

BleRadio g_ble;
BleNordicUart g_nus(g_ble);  // NUS handles the Nordic UART Service GATT table.
PowerManager g_power;

// TX power in dBm. 0 dBm is a good default for USB-tethered probe work.
constexpr int8_t kTxPowerDbm = 0;
// Ring buffer sizes for each direction. 1024 bytes handles burst traffic.
constexpr uint16_t kUsbToBleBufferSize = 1024U;
constexpr uint16_t kBleToUsbBufferSize = 1024U;
// NUS payload limit. Standard BLE 4.x MTU yields 20 bytes of usable payload.
// Larger MTU can be negotiated with BLE 5 centrals, but 20 is a safe default.
constexpr uint8_t kBleChunkBytes = 20U;
constexpr bool kEnableBleBgService = false;
// How long to wait for a BLE connection event anchor before giving up (us).
// 2000 us is very short; the main loop spins tightly to avoid missed anchors.
constexpr uint32_t kConnectionPollTimeoutUs = 2000UL;
// Delay after connection before pumping data. Gives the phone time to finish
// GATT service discovery before the first NUS notification is queued.
constexpr uint32_t kBridgeWarmupMs = 500UL;
constexpr bool kRequestLinkSecurity = false;
// Unique address per sketch to prevent Android GATT cache collisions.
constexpr uint8_t kAddress[6] = {0x39, 0x00, 0x15, 0x54, 0xDE, 0xC0};
constexpr char kGattName[] = "X54 Quiet Bridge";
// Delimiters that mark the session summary block printed on disconnect.
constexpr char kSummaryBegin[] = "@@SUMMARY_BEGIN@@";
constexpr char kSummaryEnd[] = "@@SUMMARY_END@@";

const uint8_t kNusAdvPayload[] = {
    2, 0x01, 0x06,
    8, 0x09, 'X', '5', '4', '-', 'Q', 'B', 'R',
    17, 0x07,
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E,
};

struct DirectionStats {
  uint32_t bytesIn;
  uint32_t bytesOut;
  uint32_t hashIn;
  uint32_t hashOut;
};

struct SessionStats {
  DirectionStats usbToBle;
  DirectionStats bleToUsb;
  uint32_t usbToBleStageDropped;
  uint32_t bleToUsbStageDropped;
  uint32_t bleRxDroppedBaseline;
  uint32_t bleTxDroppedBaseline;
  uint32_t sessionIndex;
  uint32_t connectedAtMs;
  uint8_t disconnectReason;
  bool disconnectReasonValid;
  bool disconnectReasonRemote;
};

bool g_wasConnected = false;
bool g_summaryPending = false;
bool g_securityRequested = false;
uint16_t g_usbToBleHead = 0U;
uint16_t g_usbToBleTail = 0U;
uint16_t g_usbToBleCount = 0U;
uint16_t g_bleToUsbHead = 0U;
uint16_t g_bleToUsbTail = 0U;
uint16_t g_bleToUsbCount = 0U;
uint8_t g_usbToBleBuffer[kUsbToBleBufferSize] = {0};
uint8_t g_bleToUsbBuffer[kBleToUsbBufferSize] = {0};
SessionStats g_stats{};

constexpr uint32_t kFnv1aOffset = 2166136261UL;
constexpr uint32_t kFnv1aPrime = 16777619UL;

uint32_t fnv1aAppend(uint32_t hash, uint8_t value) {
  return (hash ^ value) * kFnv1aPrime;
}

void resetDirection(DirectionStats* stats) {
  if (stats == nullptr) {
    return;
  }
  stats->bytesIn = 0U;
  stats->bytesOut = 0U;
  stats->hashIn = kFnv1aOffset;
  stats->hashOut = kFnv1aOffset;
}

void resetBridgeBuffers() {
  g_usbToBleHead = 0U;
  g_usbToBleTail = 0U;
  g_usbToBleCount = 0U;
  g_bleToUsbHead = 0U;
  g_bleToUsbTail = 0U;
  g_bleToUsbCount = 0U;
}

void beginSession() {
  resetBridgeBuffers();
  resetDirection(&g_stats.usbToBle);
  resetDirection(&g_stats.bleToUsb);
  g_stats.usbToBleStageDropped = 0U;
  g_stats.bleToUsbStageDropped = 0U;
  g_stats.bleRxDroppedBaseline = g_nus.rxDroppedBytes();
  g_stats.bleTxDroppedBaseline = g_nus.txDroppedBytes();
  ++g_stats.sessionIndex;
  g_stats.connectedAtMs = millis();
  g_stats.disconnectReason = 0U;
  g_stats.disconnectReasonValid = false;
  g_stats.disconnectReasonRemote = false;
  g_summaryPending = false;
}

void printHex32(uint32_t value) {
  Serial.print("0x");
  for (int shift = 28; shift >= 0; shift -= 4) {
    Serial.print(static_cast<uint8_t>((value >> shift) & 0x0FU), HEX);
  }
}

void printAddress(const uint8_t* addr) {
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

void printSummary() {
  Serial.print("\r\n");
  Serial.print(kSummaryBegin);
  Serial.print("\r\n");
  Serial.print("session=");
  Serial.print(g_stats.sessionIndex);
  Serial.print("\r\n");
  Serial.print("connected_ms=");
  Serial.print(millis() - g_stats.connectedAtMs);
  Serial.print("\r\n");
  Serial.print("disconnect_reason=");
  if (g_stats.disconnectReasonValid) {
    printHex32(g_stats.disconnectReason);
  } else {
    Serial.print("none");
  }
  Serial.print("\r\n");
  Serial.print("disconnect_remote=");
  Serial.print(g_stats.disconnectReasonRemote ? "1" : "0");
  Serial.print("\r\n");

  Serial.print("usb_to_ble_in=");
  Serial.print(g_stats.usbToBle.bytesIn);
  Serial.print("\r\n");
  Serial.print("usb_to_ble_out=");
  Serial.print(g_stats.usbToBle.bytesOut);
  Serial.print("\r\n");
  Serial.print("usb_to_ble_in_hash=");
  printHex32(g_stats.usbToBle.hashIn);
  Serial.print("\r\n");
  Serial.print("usb_to_ble_out_hash=");
  printHex32(g_stats.usbToBle.hashOut);
  Serial.print("\r\n");

  Serial.print("ble_to_usb_in=");
  Serial.print(g_stats.bleToUsb.bytesIn);
  Serial.print("\r\n");
  Serial.print("ble_to_usb_out=");
  Serial.print(g_stats.bleToUsb.bytesOut);
  Serial.print("\r\n");
  Serial.print("ble_to_usb_in_hash=");
  printHex32(g_stats.bleToUsb.hashIn);
  Serial.print("\r\n");
  Serial.print("ble_to_usb_out_hash=");
  printHex32(g_stats.bleToUsb.hashOut);
  Serial.print("\r\n");

  Serial.print("usb_to_ble_stage_drop=");
  Serial.print(g_stats.usbToBleStageDropped);
  Serial.print("\r\n");
  Serial.print("ble_to_usb_stage_drop=");
  Serial.print(g_stats.bleToUsbStageDropped);
  Serial.print("\r\n");
  Serial.print("ble_rx_drop=");
  Serial.print(g_nus.rxDroppedBytes() - g_stats.bleRxDroppedBaseline);
  Serial.print("\r\n");
  Serial.print("ble_tx_drop=");
  Serial.print(g_nus.txDroppedBytes() - g_stats.bleTxDroppedBaseline);
  Serial.print("\r\n");
  Serial.print("usb_to_ble_pending=");
  Serial.print(g_usbToBleCount);
  Serial.print("\r\n");
  Serial.print("ble_to_usb_pending=");
  Serial.print(g_bleToUsbCount);
  Serial.print("\r\n");
  Serial.print(kSummaryEnd);
  Serial.print("\r\n");
}

void maybeRequestLinkSecurity(uint32_t nowMs) {
  if (!kRequestLinkSecurity || g_securityRequested || !g_ble.isConnected()) {
    return;
  }
  if ((nowMs - g_stats.connectedAtMs) < kBridgeWarmupMs) {
    return;
  }
  g_securityRequested = true;
  g_ble.sendSmpSecurityRequest();
}

uint16_t advanceBridgeIndex(uint16_t index, uint16_t capacity) {
  ++index;
  if (index >= capacity) {
    index = 0U;
  }
  return index;
}

void stageUsbToBle(int maxBytes) {
  int budget = maxBytes;
  while (budget > 0 && Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch < 0) {
      break;
    }

    const uint8_t byte = static_cast<uint8_t>(ch);
    ++g_stats.usbToBle.bytesIn;
    g_stats.usbToBle.hashIn = fnv1aAppend(g_stats.usbToBle.hashIn, byte);

    if (g_usbToBleCount >= kUsbToBleBufferSize) {
      ++g_stats.usbToBleStageDropped;
    } else {
      g_usbToBleBuffer[g_usbToBleHead] = byte;
      g_usbToBleHead = advanceBridgeIndex(g_usbToBleHead, kUsbToBleBufferSize);
      ++g_usbToBleCount;
    }
    --budget;
  }
}

void pumpUsbToBle() {
  if (!g_nus.isNotifyEnabled() || g_usbToBleCount == 0U) {
    return;
  }

  int budget = g_nus.availableForWrite();
  const int payloadLimit = g_nus.maxPayloadLength();
  if (budget > payloadLimit) {
    budget = payloadLimit;
  }
  if (budget <= 0) {
    return;
  }
  if (budget > static_cast<int>(kBleChunkBytes)) {
    budget = static_cast<int>(kBleChunkBytes);
  }
  if (budget > g_usbToBleCount) {
    budget = g_usbToBleCount;
  }
  if (budget <= 0) {
    return;
  }

  uint8_t chunk[kBleChunkBytes] = {0};
  uint16_t index = g_usbToBleTail;
  for (int i = 0; i < budget; ++i) {
    chunk[i] = g_usbToBleBuffer[index];
    index = advanceBridgeIndex(index, kUsbToBleBufferSize);
  }

  const size_t written = g_nus.write(chunk, static_cast<size_t>(budget));
  for (size_t i = 0; i < written; ++i) {
    const uint8_t byte = g_usbToBleBuffer[g_usbToBleTail];
    g_stats.usbToBle.hashOut = fnv1aAppend(g_stats.usbToBle.hashOut, byte);
    ++g_stats.usbToBle.bytesOut;
    g_usbToBleTail = advanceBridgeIndex(g_usbToBleTail, kUsbToBleBufferSize);
    --g_usbToBleCount;
  }
}

void stageBleToUsb(int maxBytes) {
  int budget = maxBytes;
  while (budget > 0 && g_nus.available() > 0) {
    const int ch = g_nus.read();
    if (ch < 0) {
      break;
    }

    const uint8_t byte = static_cast<uint8_t>(ch);
    ++g_stats.bleToUsb.bytesIn;
    g_stats.bleToUsb.hashIn = fnv1aAppend(g_stats.bleToUsb.hashIn, byte);

    if (g_bleToUsbCount >= kBleToUsbBufferSize) {
      ++g_stats.bleToUsbStageDropped;
    } else {
      g_bleToUsbBuffer[g_bleToUsbHead] = byte;
      g_bleToUsbHead = advanceBridgeIndex(g_bleToUsbHead, kBleToUsbBufferSize);
      ++g_bleToUsbCount;
    }
    --budget;
  }
}

void pumpBleToUsb(int maxBytes) {
  int budget = maxBytes;
  while (budget > 0 && g_bleToUsbCount > 0U) {
    const uint8_t byte = g_bleToUsbBuffer[g_bleToUsbTail];
    const size_t written = Serial.write(byte);
    if (written != 1U) {
      break;
    }
    ++g_stats.bleToUsb.bytesOut;
    g_stats.bleToUsb.hashOut = fnv1aAppend(g_stats.bleToUsb.hashOut, byte);
    g_bleToUsbTail = advanceBridgeIndex(g_bleToUsbTail, kBleToUsbBufferSize);
    --g_bleToUsbCount;
    --budget;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleNordicUartQuietBridgeProbe start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  bool ok = BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
  if (ok) {
    ok = g_ble.begin(kTxPowerDbm) &&
         g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingData(kNusAdvPayload, sizeof(kNusAdvPayload)) &&
         g_ble.setScanResponseData(nullptr, 0U) &&
         g_ble.setGattDeviceName(kGattName) &&
         g_ble.clearCustomGatt() &&
         g_nus.begin();
  }
  if (ok) {
    // kConstantLatency keeps the CPU clocks running during WFI, ensuring
    // tight response to BLE connection-event interrupts. Preferred for
    // high-throughput bridges where missed anchors cause 0x08 timeouts.
    g_power.setLatencyMode(PowerLatencyMode::kConstantLatency);
  }

  Serial.print("BLE NUS quiet bridge init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (!ok) {
    while (true) {
      delay(1000);
    }
  }

  uint8_t addr[6] = {0};
  BleAddressType type = BleAddressType::kPublic;
  if (g_ble.getDeviceAddress(addr, &type)) {
    Serial.print("addr=");
    printAddress(addr);
    Serial.print(" type=");
    Serial.print((type == BleAddressType::kRandomStatic) ? "random" : "public");
    Serial.print("\r\n");
  }
  Serial.print("Advertised as X54-QBR\r\n");
}

void loop() {
  if (!g_ble.isConnected()) {
    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    g_nus.service();

    if (g_wasConnected) {
      g_wasConnected = false;
      Gpio::write(kPinUserLed, true);
      g_summaryPending = true;
    }

    if (g_summaryPending) {
      printSummary();
      resetBridgeBuffers();
      g_summaryPending = false;
    }

    if (!g_ble.isConnected()) {
      delay(20);
    }
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    Gpio::write(kPinUserLed, false);
    beginSession();
    g_securityRequested = false;
    // Background GRTC service handles ATT/SMP even when the main loop is briefly
    // delayed by bridge I/O.
    if (kEnableBleBgService) {
      g_ble.setBackgroundConnectionServiceEnabled(true);
    }
  }

  BleConnectionEvent evt{};
  const bool eventStarted =
      g_ble.pollConnectionEvent(&evt, kConnectionPollTimeoutUs) && evt.eventStarted;
  if (!eventStarted) {
    g_nus.service();
    maybeRequestLinkSecurity(millis());
    return;
  }

  // Guard against a stale terminateInd from the previous connection that the
  // HAL event queue may return on the first poll of a new connection.
  if (evt.terminateInd && g_ble.isConnected()) {
    evt.terminateInd = false;
  }

  g_nus.service(&evt);
  const uint32_t nowMs = millis();
  maybeRequestLinkSecurity(nowMs);
  if (evt.terminateInd) {
    g_stats.disconnectReasonValid = evt.disconnectReasonValid;
    g_stats.disconnectReason = evt.disconnectReason;
    g_stats.disconnectReasonRemote = evt.disconnectReasonRemote;
  }

  if ((nowMs - g_stats.connectedAtMs) < kBridgeWarmupMs) {
    return;
  }

  stageUsbToBle(128);
  stageBleToUsb(128);
  pumpUsbToBle();
  pumpBleToUsb(128);
}
