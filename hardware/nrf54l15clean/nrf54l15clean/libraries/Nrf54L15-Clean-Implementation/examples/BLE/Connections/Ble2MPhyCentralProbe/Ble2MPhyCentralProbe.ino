/*
 * Ble2MPhyCentralProbe
 *
 * Central-side BLE 2M PHY transition probe. It connects to Ble2MPhyProbe,
 * requests LE 2M PHY + ATT MTU 247, subscribes to the notify characteristic,
 * validates received 244-byte notifications on 2M, requests a fallback to 1M,
 * then verifies long traffic again after the link returns to 2M.
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static constexpr int8_t kTxPowerDbm = 0;
static constexpr uint8_t kPeripheralAddress[6] = {0x4D, 0x22, 0x15, 0x54, 0xDE, 0xC0};
static constexpr uint32_t kStatusIntervalMs = 1000UL;
static constexpr uint32_t kDiscoveryDelayMs = 1000UL;
static constexpr uint32_t kPhyRequestRetryMs = 500UL;
static constexpr uint16_t kRequestedMtu = 247U;
static constexpr uint16_t kNotifyServiceUuid = 0xFFE0U;
static constexpr uint16_t kNotifyCharacteristicUuid = 0xFFE1U;
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
static constexpr uint32_t kInitial2MTrafficGoal = 3U;

enum class DiscoveryPhase : uint8_t {
  kFindService = 0,
  kFindCharacteristic = 1,
  kFindCccd = 2,
  kSubscribe = 3,
  kReady = 4,
  kFailed = 5,
};

enum class PhyCyclePhase : uint8_t {
  kWaitForInitial2MTraffic = 0,
  kWaitForFallback1MTraffic = 1,
  kWaitForReturn2MTraffic = 2,
  kComplete = 3,
};

static DiscoveryPhase g_phase = DiscoveryPhase::kFindService;
static PhyCyclePhase g_cyclePhase = PhyCyclePhase::kWaitForInitial2MTraffic;
static bool g_requestInFlight = false;
static bool g_wasConnected = false;
static bool g_phyRequestIssued = false;
static bool g_mtuRequestIssued = false;
static bool g_flexiblePreferenceSet = false;
static bool g_returnBaselineSet = false;
static uint16_t g_serviceStartHandle = 0U;
static uint16_t g_serviceEndHandle = 0U;
static uint16_t g_notifyValueHandle = 0U;
static uint16_t g_notifyCccdHandle = 0U;
static uint8_t g_lastTxPhy = kBlePhyNone;
static uint8_t g_lastRxPhy = kBlePhyNone;
static uint32_t g_connectedSinceMs = 0U;
static uint32_t g_lastReportMs = 0U;
static uint32_t g_lastPhyRequestMs = 0U;
static uint32_t g_longNotifyReceiveCount = 0U;
static uint32_t g_fallbackBaselineNotifyCount = 0U;
static uint32_t g_returnBaselineNotifyCount = 0U;
static uint32_t g_lastNotifySequence = 0U;

static const char* phyText(uint8_t phy) {
  switch (phy) {
    case kBlePhy1M:
      return "1M";
    case kBlePhy2M:
      return "2M";
    case kBlePhyCoded:
      return "CODED";
    default:
      return "NONE";
  }
}

static const char* phaseText() {
  switch (g_phase) {
    case DiscoveryPhase::kFindService:
      return "svc";
    case DiscoveryPhase::kFindCharacteristic:
      return "chr";
    case DiscoveryPhase::kFindCccd:
      return "cccd";
    case DiscoveryPhase::kSubscribe:
      return "sub";
    case DiscoveryPhase::kReady:
      return "ready";
    case DiscoveryPhase::kFailed:
      return "fail";
    default:
      return "?";
  }
}

static const char* cyclePhaseText() {
  switch (g_cyclePhase) {
    case PhyCyclePhase::kWaitForInitial2MTraffic:
      return "wait_2m";
    case PhyCyclePhase::kWaitForFallback1MTraffic:
      return "wait_1m";
    case PhyCyclePhase::kWaitForReturn2MTraffic:
      return "ret_2m";
    case PhyCyclePhase::kComplete:
      return "done";
    default:
      return "?";
  }
}

static void printConnectionState(const char* prefix, const BleConnectionInfo& info) {
  Serial.print(prefix);
  Serial.print(" tx_phy=");
  Serial.print(phyText(info.txPhy));
  Serial.print(" rx_phy=");
  Serial.print(phyText(info.rxPhy));
  Serial.print(" mtu=");
  Serial.print(g_ble.currentAttMtu());
  Serial.print(" data=");
  Serial.print(g_ble.currentDataLength());
  Serial.print(" phase=");
  Serial.print(phaseText());
  Serial.print("/");
  Serial.print(cyclePhaseText());
  Serial.print(" notify=");
  Serial.print(g_longNotifyReceiveCount);
  Serial.print("\r\n");
}

static uint16_t readLe16Local(const uint8_t* data) {
  if (data == nullptr) {
    return 0U;
  }
  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8U);
}

static uint32_t readLe32Local(const uint8_t* data) {
  if (data == nullptr) {
    return 0U;
  }
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) |
         (static_cast<uint32_t>(data[3]) << 24U);
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
  g_cyclePhase = PhyCyclePhase::kWaitForInitial2MTraffic;
  g_requestInFlight = false;
  g_phyRequestIssued = false;
  g_mtuRequestIssued = false;
  g_flexiblePreferenceSet = false;
  g_returnBaselineSet = false;
  g_serviceStartHandle = 0U;
  g_serviceEndHandle = 0U;
  g_notifyValueHandle = 0U;
  g_notifyCccdHandle = 0U;
  g_lastTxPhy = kBlePhyNone;
  g_lastRxPhy = kBlePhyNone;
  g_connectedSinceMs = 0U;
  g_lastPhyRequestMs = 0U;
  g_longNotifyReceiveCount = 0U;
  g_fallbackBaselineNotifyCount = 0U;
  g_returnBaselineNotifyCount = 0U;
  g_lastNotifySequence = 0U;
}

static bool initial2MPhyRequestReady() {
  return (g_phase == DiscoveryPhase::kReady) &&
         (g_ble.currentAttMtu() >= kRequestedMtu) &&
         (g_ble.currentDataLength() >= 251U);
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
  Serial.print("queued service discovery\r\n");
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
  Serial.print("queued characteristic discovery\r\n");
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
  Serial.print("queued CCCD discovery\r\n");
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
  Serial.print("queued notify subscribe\r\n");
  return true;
}

static void queueDiscoveryStep() {
  if (g_requestInFlight || g_phase == DiscoveryPhase::kReady ||
      g_phase == DiscoveryPhase::kFailed) {
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
  Serial.print("ATT error rsp opcode=0x");
  Serial.print(payload[5], HEX);
  Serial.print(" handle=");
  printHexHandle(readLe16Local(&payload[6]));
  Serial.print(" err=0x");
  Serial.print(payload[8], HEX);
  Serial.print("\r\n");
  g_phase = DiscoveryPhase::kFailed;
}

static void handleServiceDiscoveryResponse(const uint8_t* payload,
                                           uint8_t payloadLength) {
  if (payload == nullptr || payloadLength < 9U) {
    return;
  }
  g_requestInFlight = false;
  g_serviceStartHandle = readLe16Local(&payload[5]);
  g_serviceEndHandle = readLe16Local(&payload[7]);
  g_phase = DiscoveryPhase::kFindCharacteristic;
  Serial.print("service discovered start=");
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
  for (uint8_t offset = 6U;
       (offset + static_cast<uint8_t>(entryLength - 1U)) < payloadLength;
       offset = static_cast<uint8_t>(offset + entryLength)) {
    if (readLe16Local(&payload[offset + 5U]) != kNotifyCharacteristicUuid) {
      continue;
    }
    g_notifyValueHandle = readLe16Local(&payload[offset + 3U]);
    g_phase = DiscoveryPhase::kFindCccd;
    Serial.print("notify value handle=");
    printHexHandle(g_notifyValueHandle);
    Serial.print("\r\n");
    return;
  }
  g_phase = DiscoveryPhase::kFailed;
  Serial.print("notify characteristic missing\r\n");
}

static void handleFindInfoResponse(const uint8_t* payload, uint8_t payloadLength) {
  if (payload == nullptr || payloadLength < 10U) {
    return;
  }
  g_requestInFlight = false;
  if (payload[5] != 0x01U) {
    g_phase = DiscoveryPhase::kFailed;
    Serial.print("descriptor format unsupported\r\n");
    return;
  }
  for (uint8_t offset = 6U; (offset + 3U) < payloadLength;
       offset = static_cast<uint8_t>(offset + 4U)) {
    if (readLe16Local(&payload[offset + 2U]) != kUuidClientCharacteristicConfig) {
      continue;
    }
    g_notifyCccdHandle = readLe16Local(&payload[offset]);
    g_phase = DiscoveryPhase::kSubscribe;
    Serial.print("notify cccd handle=");
    printHexHandle(g_notifyCccdHandle);
    Serial.print("\r\n");
    return;
  }
  g_phase = DiscoveryPhase::kFailed;
  Serial.print("notify cccd missing\r\n");
}

static void handleWriteResponse() {
  g_requestInFlight = false;
  g_phase = DiscoveryPhase::kReady;
  Serial.print("notifications enabled\r\n");
}

static void handleNotification(const BleConnectionEvent& evt) {
  if (evt.payload == nullptr || evt.payloadLength < 11U) {
    return;
  }
  if (readLe16Local(&evt.payload[5]) != g_notifyValueHandle) {
    return;
  }
  const uint8_t valueLength = static_cast<uint8_t>(evt.payloadLength - 7U);
  if ((valueLength == BleRadio::kCustomGattMaxValueLength) &&
      (evt.payload[7] == '2') &&
      (evt.payload[8] == 'M') &&
      (evt.payload[9] == 'P') &&
      (evt.payload[10] == 'H')) {
    g_lastNotifySequence = readLe32Local(&evt.payload[11]);
    ++g_longNotifyReceiveCount;
    Serial.print("notify rx seq=");
    Serial.print(g_lastNotifySequence);
    Serial.print(" len=");
    Serial.print(valueLength);
    Serial.print(" ll=");
    Serial.print(evt.payloadLength);
    Serial.print(" phy=");
    Serial.print(phyText(evt.rxPhy));
    Serial.print("\r\n");
  }
}

static void maybeDrivePhyCycle(const BleConnectionInfo& info, uint32_t nowMs) {
  if (g_phase != DiscoveryPhase::kReady) {
    return;
  }

  switch (g_cyclePhase) {
    case PhyCyclePhase::kWaitForInitial2MTraffic:
      if ((info.txPhy == kBlePhy2M) &&
          (info.rxPhy == kBlePhy2M) &&
          (g_longNotifyReceiveCount >= kInitial2MTrafficGoal) &&
          ((nowMs - g_lastPhyRequestMs) >= kPhyRequestRetryMs)) {
        g_lastPhyRequestMs = nowMs;
        if (g_ble.requestPHY(kBlePhy1M)) {
          g_cyclePhase = PhyCyclePhase::kWaitForFallback1MTraffic;
          g_fallbackBaselineNotifyCount = g_longNotifyReceiveCount;
          Serial.print("request phy 1M fallback: queued\r\n");
        }
      }
      return;

    case PhyCyclePhase::kWaitForFallback1MTraffic:
      if ((info.txPhy == kBlePhy1M) &&
          (info.rxPhy == kBlePhy1M) &&
          (g_longNotifyReceiveCount > g_fallbackBaselineNotifyCount)) {
        if (!g_flexiblePreferenceSet &&
            g_ble.setPreferredPhyOptions(
                static_cast<uint8_t>(kBlePhy1M | kBlePhy2M),
                static_cast<uint8_t>(kBlePhy1M | kBlePhy2M))) {
          g_flexiblePreferenceSet = true;
          Serial.print("phy preference: flexible\r\n");
        }
        g_cyclePhase = PhyCyclePhase::kWaitForReturn2MTraffic;
        g_returnBaselineSet = false;
        Serial.print("cycle phase: 1M long notify confirmed\r\n");
      }
      return;

    case PhyCyclePhase::kWaitForReturn2MTraffic:
      if ((info.txPhy == kBlePhy2M) && (info.rxPhy == kBlePhy2M)) {
        if (!g_returnBaselineSet) {
          g_returnBaselineNotifyCount = g_longNotifyReceiveCount;
          g_returnBaselineSet = true;
          Serial.print("cycle phase: 2M returned\r\n");
        } else if (g_longNotifyReceiveCount > g_returnBaselineNotifyCount) {
          g_cyclePhase = PhyCyclePhase::kComplete;
          Serial.print("cycle phase: 2M long notify reconfirmed\r\n");
        }
      }
      return;

    case PhyCyclePhase::kComplete:
    default:
      return;
  }
}

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBle2MPhyCentralProbe start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  }
  resetDiscovery();

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
}

void loop() {
  if (!g_ble.isConnected()) {
    if (g_wasConnected) {
      g_wasConnected = false;
      resetDiscovery();
      Serial.print("disconnected\r\n");
    }

    const bool started = g_ble.initiateConnection(
        kPeripheralAddress, true, 24U, 200U, 9U, 300000UL);
    if (started) {
      Serial.print("connect attempt: sent\r\n");
    }
    Gpio::write(kPinUserLed, true);
    delay(20);
    return;
  }

  BleConnectionInfo info{};
  if (!g_ble.getConnectionInfo(&info)) {
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    g_connectedSinceMs = millis();
    Gpio::write(kPinUserLed, false);
    printConnectionState("connected", info);
  }

  if (!g_mtuRequestIssued && g_ble.currentAttMtu() < kRequestedMtu) {
    g_mtuRequestIssued = g_ble.requestAttMtuExchange(kRequestedMtu);
    if (g_mtuRequestIssued) {
      Serial.print("request mtu 247: queued\r\n");
    }
  }

  BleConnectionEvent evt{};
  if (g_ble.pollConnectionEvent(&evt, 450000UL) && evt.eventStarted) {
    if ((evt.txPhy != g_lastTxPhy) || (evt.rxPhy != g_lastRxPhy)) {
      Serial.print("event ce=");
      Serial.print(evt.eventCounter);
      Serial.print(" tx_phy=");
      Serial.print(phyText(evt.txPhy));
      Serial.print(" rx_phy=");
      Serial.print(phyText(evt.rxPhy));
      Serial.print("\r\n");
      g_lastTxPhy = evt.txPhy;
      g_lastRxPhy = evt.rxPhy;
    }
    if (evt.packetReceived && evt.crcOk && evt.attPacket && evt.payload != nullptr) {
      const uint8_t attOpcode = evt.payload[4];
      if (attOpcode == kAttOpErrorRsp) {
        handleErrorResponse(evt.payload, evt.payloadLength);
      } else if ((attOpcode == kAttOpFindByTypeValueRsp) &&
                 (g_phase == DiscoveryPhase::kFindService)) {
        handleServiceDiscoveryResponse(evt.payload, evt.payloadLength);
      } else if ((attOpcode == kAttOpReadByTypeRsp) &&
                 (g_phase == DiscoveryPhase::kFindCharacteristic)) {
        handleCharacteristicDiscoveryResponse(evt.payload, evt.payloadLength);
      } else if ((attOpcode == kAttOpFindInfoRsp) &&
                 (g_phase == DiscoveryPhase::kFindCccd)) {
        handleFindInfoResponse(evt.payload, evt.payloadLength);
      } else if ((attOpcode == kAttOpWriteRsp) &&
                 (g_phase == DiscoveryPhase::kSubscribe)) {
        handleWriteResponse();
      } else if (attOpcode == kAttOpHandleValueNtf) {
        handleNotification(evt);
      }
    }
  }

  const uint32_t nowMs = millis();
  (void)g_ble.getConnectionInfo(&info);
  queueDiscoveryStep();
  if (!g_phyRequestIssued && initial2MPhyRequestReady()) {
    g_phyRequestIssued = g_ble.requestPHY(kBlePhy2M);
    if (g_phyRequestIssued) {
      Serial.print("request 2M phy: queued\r\n");
    }
  }
  maybeDrivePhyCycle(info, nowMs);
  if ((nowMs - g_lastReportMs) >= kStatusIntervalMs) {
    g_lastReportMs = nowMs;
    printConnectionState("status", info);
  }
}
