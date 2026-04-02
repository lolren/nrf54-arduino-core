#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static const uint8_t kPeripheralAddress[6] = {0x42, 0x00, 0x15, 0x54, 0xDE, 0xC0};
static constexpr uint16_t kNotifyServiceUuid = 0xFFF0U;
static constexpr uint16_t kNotifyCharacteristicUuid = 0xFFF1U;
static constexpr uint16_t kUuidPrimaryService = 0x2800U;
static constexpr uint16_t kUuidCharacteristic = 0x2803U;
static constexpr uint16_t kUuidClientCharacteristicConfig = 0x2902U;

static constexpr uint8_t kAttOpErrorRsp = 0x01U;
static constexpr uint8_t kAttOpFindInfoReq = 0x04U;
static constexpr uint8_t kAttOpFindInfoRsp = 0x05U;
static constexpr uint8_t kAttOpFindByTypeValueReq = 0x06U;
static constexpr uint8_t kAttOpFindByTypeValueRsp = 0x07U;
static constexpr uint8_t kAttOpReadByTypeReq = 0x08U;
static constexpr uint8_t kAttOpReadByTypeRsp = 0x09U;
static constexpr uint8_t kAttOpWriteRsp = 0x13U;
static constexpr uint8_t kAttOpHandleValueNtf = 0x1BU;

enum class DiscoveryPhase : uint8_t {
  kFindService = 0,
  kFindCharacteristic = 1,
  kFindCccd = 2,
  kSubscribe = 3,
  kReady = 4,
  kFailed = 5,
};

static DiscoveryPhase g_phase = DiscoveryPhase::kFindService;
static bool g_requestInFlight = false;
static bool g_connectedLogged = false;
static uint16_t g_serviceStartHandle = 0U;
static uint16_t g_serviceEndHandle = 0U;
static uint16_t g_notifyValueHandle = 0U;
static uint16_t g_notifyCccdHandle = 0U;
static uint32_t g_notifyReceiveCount = 0U;
static uint32_t g_connectedSinceMs = 0U;
static uint32_t g_traceCount = 0U;
static char g_lastTrace[24] = {0};
static volatile BleDisconnectDebug g_lastDisconnectDebug{};
static volatile bool g_hasDisconnectDebug = false;
static constexpr uint32_t kDiscoveryDelayMs = 1000UL;
static constexpr bool kEnableDiscoveryTraffic = true;
struct LinkTraceEntry {
  uint16_t eventCounter;
  uint8_t dataChannel;
  uint8_t llid;
  uint8_t rxNesn;
  uint8_t rxSn;
  uint8_t txLlid;
  uint8_t txNesn;
  uint8_t txSn;
  uint8_t payloadLength;
  uint8_t txPayloadLength;
  uint8_t flags;
};
static volatile LinkTraceEntry g_linkTrace[16]{};
static volatile uint32_t g_linkTraceCount = 0U;

static void recordLinkTrace(const BleConnectionEvent& evt) {
  const uint32_t index = g_linkTraceCount & 0x0FU;
  LinkTraceEntry trace{};
  trace.eventCounter = evt.eventCounter;
  trace.dataChannel = evt.dataChannel;
  trace.llid = evt.llid;
  trace.rxNesn = evt.rxNesn;
  trace.rxSn = evt.rxSn;
  trace.txLlid = evt.txLlid;
  trace.txNesn = evt.txNesn;
  trace.txSn = evt.txSn;
  trace.payloadLength = evt.payloadLength;
  trace.txPayloadLength = evt.txPayloadLength;
  trace.flags = 0U;
  if (evt.packetReceived) trace.flags |= 0x01U;
  if (evt.crcOk) trace.flags |= 0x02U;
  if (evt.packetIsNew) trace.flags |= 0x04U;
  if (evt.peerAckedLastTx) trace.flags |= 0x08U;
  if (evt.freshTxAllowed) trace.flags |= 0x10U;
  if (evt.emptyAckTransmitted) trace.flags |= 0x20U;
  if (evt.txPacketSent) trace.flags |= 0x40U;
  if (evt.terminateInd) trace.flags |= 0x80U;
  memcpy(const_cast<LinkTraceEntry*>(&g_linkTrace[index]), &trace, sizeof(trace));
  ++g_linkTraceCount;
}

static void onBleTrace(const char* message, void* context) {
  (void)context;
  ++g_traceCount;
  memset(g_lastTrace, 0, sizeof(g_lastTrace));
  if (message == nullptr) {
    return;
  }
  strncpy(g_lastTrace, message, sizeof(g_lastTrace) - 1U);
}

static uint16_t readLe16Local(const uint8_t* data) {
  if (data == nullptr) {
    return 0U;
  }
  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8U);
}

static void writeLe16Local(uint8_t* data, uint16_t value) {
  if (data == nullptr) {
    return;
  }
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

static void printHexHandle(uint16_t value) {
  Serial.print("0x");
  if (value < 0x1000U) {
    Serial.print('0');
  }
  if (value < 0x100U) {
    Serial.print('0');
  }
  if (value < 0x10U) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

static void resetDiscovery() {
  g_phase = DiscoveryPhase::kFindService;
  g_requestInFlight = false;
  g_connectedLogged = false;
  g_connectedSinceMs = 0U;
  g_serviceStartHandle = 0U;
  g_serviceEndHandle = 0U;
  g_notifyValueHandle = 0U;
  g_notifyCccdHandle = 0U;
}

static void captureDisconnectDebug() {
  BleDisconnectDebug debug{};
  if (g_ble.getDisconnectDebug(&debug)) {
    memcpy(const_cast<BleDisconnectDebug*>(&g_lastDisconnectDebug), &debug,
           sizeof(debug));
    g_hasDisconnectDebug = true;
  }
}

static bool queueServiceDiscovery() {
  uint8_t request[9] = {kAttOpFindByTypeValueReq, 0U, 0U, 0U, 0U,
                        0U, 0U, 0U, 0U};
  writeLe16Local(&request[1], 0x0001U);
  writeLe16Local(&request[3], 0xFFFFU);
  writeLe16Local(&request[5], kUuidPrimaryService);
  writeLe16Local(&request[7], kNotifyServiceUuid);
  if (!g_ble.queueAttRequest(request, sizeof(request))) {
    return false;
  }
  g_requestInFlight = true;
  Serial.print("queued primary service discovery for UUID 0x");
  Serial.print(kNotifyServiceUuid, HEX);
  Serial.print("\r\n");
  return true;
}

static bool queueCharacteristicDiscovery() {
  if (g_serviceStartHandle == 0U || g_serviceEndHandle == 0U) {
    return false;
  }

  uint8_t request[7] = {kAttOpReadByTypeReq, 0U, 0U, 0U, 0U, 0U, 0U};
  writeLe16Local(&request[1], g_serviceStartHandle);
  writeLe16Local(&request[3], g_serviceEndHandle);
  writeLe16Local(&request[5], kUuidCharacteristic);
  if (!g_ble.queueAttRequest(request, sizeof(request))) {
    return false;
  }
  g_requestInFlight = true;
  Serial.print("queued characteristic discovery in range ");
  printHexHandle(g_serviceStartHandle);
  Serial.print("..");
  printHexHandle(g_serviceEndHandle);
  Serial.print("\r\n");
  return true;
}

static bool queueCccdDiscovery() {
  if (g_notifyValueHandle == 0U || g_serviceEndHandle <= g_notifyValueHandle) {
    return false;
  }

  uint8_t request[5] = {kAttOpFindInfoReq, 0U, 0U, 0U, 0U};
  writeLe16Local(&request[1], static_cast<uint16_t>(g_notifyValueHandle + 1U));
  writeLe16Local(&request[3], g_serviceEndHandle);
  if (!g_ble.queueAttRequest(request, sizeof(request))) {
    return false;
  }
  g_requestInFlight = true;
  Serial.print("queued descriptor discovery after value handle ");
  printHexHandle(g_notifyValueHandle);
  Serial.print("\r\n");
  return true;
}

static bool queueSubscribe() {
  if (g_notifyCccdHandle == 0U) {
    return false;
  }
  if (!g_ble.queueAttCccdWrite(g_notifyCccdHandle, true, false, true)) {
    return false;
  }
  g_requestInFlight = true;
  Serial.print("queued CCCD write for handle ");
  printHexHandle(g_notifyCccdHandle);
  Serial.print("\r\n");
  return true;
}

static void queueDiscoveryStep() {
  if (g_requestInFlight) {
    return;
  }
  if (g_connectedSinceMs == 0U ||
      (millis() - g_connectedSinceMs) < kDiscoveryDelayMs) {
    return;
  }

  switch (g_phase) {
    case DiscoveryPhase::kFindService:
      (void)queueServiceDiscovery();
      break;
    case DiscoveryPhase::kFindCharacteristic:
      (void)queueCharacteristicDiscovery();
      break;
    case DiscoveryPhase::kFindCccd:
      (void)queueCccdDiscovery();
      break;
    case DiscoveryPhase::kSubscribe:
      (void)queueSubscribe();
      break;
    case DiscoveryPhase::kReady:
    case DiscoveryPhase::kFailed:
    default:
      break;
  }
}

static void handleErrorResponse(const uint8_t* payload, uint8_t payloadLength) {
  if (payload == nullptr || payloadLength < 9U) {
    return;
  }

  g_requestInFlight = false;
  const uint8_t requestOpcode = payload[5];
  const uint16_t handle = readLe16Local(&payload[6]);
  const uint8_t errorCode = payload[8];

  Serial.print("ATT error rsp opcode=0x");
  Serial.print(requestOpcode, HEX);
  Serial.print(" handle=");
  printHexHandle(handle);
  Serial.print(" err=0x");
  Serial.print(errorCode, HEX);
  Serial.print("\r\n");

  if (requestOpcode == kAttOpFindByTypeValueReq ||
      requestOpcode == kAttOpReadByTypeReq ||
      requestOpcode == kAttOpFindInfoReq) {
    g_phase = DiscoveryPhase::kFailed;
  }
}

static void handleServiceDiscoveryResponse(const uint8_t* payload,
                                           uint8_t payloadLength) {
  if (payload == nullptr || payloadLength < 9U) {
    return;
  }

  g_requestInFlight = false;
  bool found = false;
  for (uint8_t offset = 5U; (offset + 3U) < payloadLength; offset = static_cast<uint8_t>(offset + 4U)) {
    g_serviceStartHandle = readLe16Local(&payload[offset]);
    g_serviceEndHandle = readLe16Local(&payload[offset + 2U]);
    found = true;
    break;
  }

  if (!found) {
    g_phase = DiscoveryPhase::kFailed;
    Serial.print("service discovery returned no handle records\r\n");
    return;
  }

  g_phase = DiscoveryPhase::kFindCharacteristic;
  Serial.print("service discovered: start=");
  printHexHandle(g_serviceStartHandle);
  Serial.print(" end=");
  printHexHandle(g_serviceEndHandle);
  Serial.print("\r\n");
}

static void handleCharacteristicDiscoveryResponse(const uint8_t* payload,
                                                  uint8_t payloadLength) {
  if (payload == nullptr || payloadLength < 13U) {
    return;
  }

  g_requestInFlight = false;
  const uint8_t entryLength = payload[5];
  if (entryLength < 7U) {
    g_phase = DiscoveryPhase::kFailed;
    Serial.print("characteristic discovery entry too short\r\n");
    return;
  }

  bool found = false;
  for (uint8_t offset = 6U;
       (offset + static_cast<uint8_t>(entryLength - 1U)) < payloadLength;
       offset = static_cast<uint8_t>(offset + entryLength)) {
    const uint16_t uuid16 = readLe16Local(&payload[offset + 5U]);
    if (uuid16 != kNotifyCharacteristicUuid) {
      continue;
    }
    g_notifyValueHandle = readLe16Local(&payload[offset + 3U]);
    found = true;
    break;
  }

  if (!found) {
    g_phase = DiscoveryPhase::kFailed;
    Serial.print("notify characteristic UUID not found\r\n");
    return;
  }

  g_phase = DiscoveryPhase::kFindCccd;
  Serial.print("notify characteristic discovered: value=");
  printHexHandle(g_notifyValueHandle);
  Serial.print("\r\n");
}

static void handleFindInfoResponse(const uint8_t* payload, uint8_t payloadLength) {
  if (payload == nullptr || payloadLength < 10U) {
    return;
  }

  g_requestInFlight = false;
  const uint8_t format = payload[5];
  if (format != 0x01U) {
    g_phase = DiscoveryPhase::kFailed;
    Serial.print("descriptor discovery returned unsupported UUID format\r\n");
    return;
  }

  bool found = false;
  for (uint8_t offset = 6U; (offset + 3U) < payloadLength; offset = static_cast<uint8_t>(offset + 4U)) {
    const uint16_t handle = readLe16Local(&payload[offset]);
    const uint16_t uuid16 = readLe16Local(&payload[offset + 2U]);
    if (uuid16 == kUuidClientCharacteristicConfig) {
      g_notifyCccdHandle = handle;
      found = true;
      break;
    }
  }

  if (!found) {
    g_phase = DiscoveryPhase::kFailed;
    Serial.print("CCCD descriptor not found\r\n");
    return;
  }

  g_phase = DiscoveryPhase::kSubscribe;
  Serial.print("notify CCCD discovered: handle=");
  printHexHandle(g_notifyCccdHandle);
  Serial.print("\r\n");
}

static void handleWriteResponse() {
  g_requestInFlight = false;
  g_phase = DiscoveryPhase::kReady;
  Serial.print("notifications enabled\r\n");
}

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleNotifyCentral start (ATT discovery)\r\n");

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);
  static const uint8_t kAddress[6] = {0x43, 0x00, 0x15, 0x54, 0xDE, 0xC0};
  bool ok = g_ble.begin(-4);
  if (ok) {
    g_ble.setTraceCallback(onBleTrace, nullptr);
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic);
  }

  resetDiscovery();

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  Serial.print("Target peripheral address: C0:DE:54:15:00:42 (random static)\r\n");
}

void loop() {
  if (!g_ble.isConnected()) {
    captureDisconnectDebug();
    resetDiscovery();
    Gpio::write(kPinUserLed, true);

    if (g_ble.initiateConnection(kPeripheralAddress, true, 24U, 200U, 9U,
                                 250000UL)) {
      Serial.print("connection initiated\r\n");
    }
    delay(1);
    return;
  }

  BleConnectionEvent evt{};
  if (!g_ble.pollConnectionEvent(&evt, 450000UL)) {
    return;
  }
  recordLinkTrace(evt);

  Gpio::write(kPinUserLed, false);

  if (!g_connectedLogged) {
    g_connectedLogged = true;
    g_connectedSinceMs = millis();
    Serial.print("connected; waiting before ATT discovery\r\n");
  }

  if (evt.terminateInd) {
    captureDisconnectDebug();
    Serial.print("link terminated\r\n");
    Gpio::write(kPinUserLed, true);
    return;
  }

  if (kEnableDiscoveryTraffic &&
      evt.packetReceived && evt.crcOk && evt.attPacket && evt.payload != nullptr) {
    const uint8_t* payload = evt.payload;
    const uint8_t attOpcode = payload[4];
    if (attOpcode == kAttOpErrorRsp) {
      handleErrorResponse(payload, evt.payloadLength);
    } else if ((attOpcode == kAttOpFindByTypeValueRsp) &&
               (g_phase == DiscoveryPhase::kFindService)) {
      handleServiceDiscoveryResponse(payload, evt.payloadLength);
    } else if ((attOpcode == kAttOpReadByTypeRsp) &&
               (g_phase == DiscoveryPhase::kFindCharacteristic)) {
      handleCharacteristicDiscoveryResponse(payload, evt.payloadLength);
    } else if ((attOpcode == kAttOpFindInfoRsp) &&
               (g_phase == DiscoveryPhase::kFindCccd)) {
      handleFindInfoResponse(payload, evt.payloadLength);
    } else if ((attOpcode == kAttOpWriteRsp) &&
               (g_phase == DiscoveryPhase::kSubscribe)) {
      handleWriteResponse();
    } else if (attOpcode == kAttOpHandleValueNtf && evt.payloadLength >= 7U) {
      const uint16_t valueHandle = readLe16Local(&payload[5]);
      if (valueHandle == g_notifyValueHandle) {
        ++g_notifyReceiveCount;
      }
    }
  }

  if (kEnableDiscoveryTraffic) {
    queueDiscoveryStep();
  }
}
