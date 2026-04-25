/*
 * BleCodedPhyWriteProbe
 *
 * Peripheral-side coded PHY duplex probe with an extra write characteristic.
 * Once coded PHY, MTU 247 and data length 251 are active, it keeps streaming
 * 244-byte notifications so the companion central can verify long ATT writes
 * under continuous reverse traffic.
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

extern volatile uint8_t g_ble_periph_last_rx_llid;
extern volatile uint8_t g_ble_periph_last_rx_len;
extern volatile uint8_t g_ble_periph_last_rx_packet_is_new;
extern volatile uint8_t g_ble_periph_last_rx_peer_acked;
extern volatile uint16_t g_ble_periph_last_rx_l2cap_cid;
extern volatile uint8_t g_ble_periph_last_rx_att_opcode;
extern volatile uint8_t g_ble_periph_last_rx_ll_opcode;

static BleRadio g_ble;
static PowerManager g_power;

static constexpr int8_t kTxPowerDbm = 0;
static constexpr uint32_t kAdvIntervalMs = 100;
static constexpr uint32_t kStatusIntervalMs = 1000;
static constexpr uint32_t kNotifyIntervalMs = 500;
static constexpr uint32_t kPhyRequestRetryMs = 500;
static constexpr uint16_t kRequestedMtu = 247U;
static constexpr uint16_t kNotifyServiceUuid = 0xFFF0U;
static constexpr uint16_t kNotifyCharacteristicUuid = 0xFFF1U;
static constexpr uint16_t kWriteCharacteristicUuid = 0xFFF2U;
static constexpr char kAddressText[] = "C0:DE:54:15:20:41";

enum class PhyCyclePhase : uint8_t {
  kRequestInitialCoded = 0,
  kComplete = 1,
};

static uint16_t g_notifyServiceHandle = 0U;
static uint16_t g_notifyValueHandle = 0U;
static uint16_t g_notifyCccdHandle = 0U;
static uint16_t g_writeValueHandle = 0U;
static bool g_wasConnected = false;
static bool g_mtuRequestIssued = false;
static bool g_notifyPending = false;
static bool g_flexiblePreferenceSet = false;
static uint8_t g_lastTxPhy = kBlePhyNone;
static uint8_t g_lastRxPhy = kBlePhyNone;
static uint32_t g_lastReportMs = 0U;
static uint32_t g_lastNotifyMs = 0U;
static uint32_t g_lastPhyRequestMs = 0U;
static uint32_t g_phaseSinceMs = 0U;
static uint32_t g_notifySequence = 0U;
static uint32_t g_notifySentCount = 0U;
static uint32_t g_writeReceiveCount = 0U;
static uint32_t g_lastWriteLength = 0U;
static PhyCyclePhase g_cyclePhase = PhyCyclePhase::kRequestInitialCoded;
static BleDisconnectDebug g_lastDisconnectDebug{};
static bool g_hasDisconnectDebug = false;
static char g_lastTrace[48] = {0};

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

static const char* cyclePhaseText(PhyCyclePhase phase) {
  switch (phase) {
    case PhyCyclePhase::kRequestInitialCoded:
      return "boot";
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
  Serial.print(cyclePhaseText(g_cyclePhase));
  Serial.print(" notify=");
  Serial.print(g_notifySentCount);
  Serial.print(" writes=");
  Serial.print(g_writeReceiveCount);
  Serial.print(" last_write=");
  Serial.print(g_lastWriteLength);
  Serial.print("\r\n");
}

static void captureDisconnectDebug() {
  BleDisconnectDebug debug{};
  if (!g_ble.getDisconnectDebug(&debug) || debug.valid == 0U) {
    return;
  }
  memcpy(&g_lastDisconnectDebug, &debug, sizeof(debug));
  g_hasDisconnectDebug = true;
}

static void printDisconnectDebugIfAvailable() {
  if (!g_hasDisconnectDebug) {
    return;
  }
  Serial.print("disconnect dbg reason=");
  Serial.print(g_lastDisconnectDebug.reason);
  Serial.print(" err=0x");
  Serial.print(g_lastDisconnectDebug.errorCode, HEX);
  Serial.print(" ev=");
  Serial.print(g_lastDisconnectDebug.eventCounter);
  Serial.print(" miss=");
  Serial.print(g_lastDisconnectDebug.missedEventCount);
  Serial.print(" pend_len=");
  Serial.print(g_lastDisconnectDebug.pendingTxLength);
  Serial.print(" last_tx_op=0x");
  Serial.print(g_lastDisconnectDebug.lastTxOpcode, HEX);
  Serial.print(" last_tx_len=");
  Serial.print(g_lastDisconnectDebug.lastTxLength);
  Serial.print(" last_rx_op=0x");
  Serial.print(g_lastDisconnectDebug.lastRxOpcode, HEX);
  Serial.print(" last_rx_len=");
  Serial.print(g_lastDisconnectDebug.lastRxLength);
  Serial.print(" trace=");
  Serial.print(g_lastTrace[0] != '\0' ? g_lastTrace : "-");
  Serial.print(" obs_llid=");
  Serial.print(g_ble_periph_last_rx_llid);
  Serial.print(" obs_len=");
  Serial.print(g_ble_periph_last_rx_len);
  Serial.print(" obs_new=");
  Serial.print(g_ble_periph_last_rx_packet_is_new);
  Serial.print(" obs_ack=");
  Serial.print(g_ble_periph_last_rx_peer_acked);
  Serial.print(" obs_cid=0x");
  Serial.print(g_ble_periph_last_rx_l2cap_cid, HEX);
  Serial.print(" obs_att=0x");
  Serial.print(g_ble_periph_last_rx_att_opcode, HEX);
  Serial.print(" obs_ll=0x");
  Serial.print(g_ble_periph_last_rx_ll_opcode, HEX);
  Serial.print("\r\n");
  g_hasDisconnectDebug = false;
}

static void onBleTrace(const char* message, void* context) {
  (void)context;
  memset(g_lastTrace, 0, sizeof(g_lastTrace));
  if (message == nullptr) {
    return;
  }
  strncpy(g_lastTrace, message, sizeof(g_lastTrace) - 1U);
}

static void handleWriteValue(uint16_t valueHandle, const uint8_t* value,
                             uint8_t valueLength, bool withResponse,
                             void* context) {
  (void)valueHandle;
  (void)value;
  (void)context;

  g_lastWriteLength = valueLength;
  ++g_writeReceiveCount;

  Serial.print("write rx len=");
  Serial.print(valueLength);
  Serial.print(" with_response=");
  Serial.print(withResponse ? "yes" : "no");
  Serial.print(" total=");
  Serial.print(g_writeReceiveCount);
  Serial.print("\r\n");
}

static void buildNotifyValue(uint32_t sequence, uint8_t* value, uint8_t* valueLength) {
  if (value == nullptr || valueLength == nullptr) {
    return;
  }

  value[0] = 'C';
  value[1] = 'P';
  value[2] = 'H';
  value[3] = 'Y';
  value[4] = static_cast<uint8_t>(sequence & 0xFFU);
  value[5] = static_cast<uint8_t>((sequence >> 8U) & 0xFFU);
  value[6] = static_cast<uint8_t>((sequence >> 16U) & 0xFFU);
  value[7] = static_cast<uint8_t>((sequence >> 24U) & 0xFFU);
  for (uint16_t i = 8U; i < BleRadio::kCustomGattMaxValueLength; ++i) {
    value[i] = static_cast<uint8_t>('A' + ((i + sequence) % 26U));
  }
  *valueLength = BleRadio::kCustomGattMaxValueLength;
}

static bool longNotifyReady(const BleConnectionInfo& info) {
  return (g_cyclePhase != PhyCyclePhase::kRequestInitialCoded) &&
         g_ble.isCustomGattCccdEnabled(g_notifyValueHandle, false) &&
         (info.txPhy == info.rxPhy) &&
         (g_ble.currentAttMtu() >= kRequestedMtu) &&
         (g_ble.currentDataLength() >= 251U);
}

static bool initialCodedRequestReady() {
  return g_ble.isCustomGattCccdEnabled(g_notifyValueHandle, false) &&
         (g_ble.currentAttMtu() >= kRequestedMtu) &&
         (g_ble.currentDataLength() >= 251U);
}

static void queueLongNotify(const BleConnectionInfo& info) {
  uint8_t value[BleRadio::kCustomGattMaxValueLength];
  uint8_t valueLength = 0U;
  buildNotifyValue(g_notifySequence, value, &valueLength);

  if (!g_ble.setCustomGattCharacteristicValue(g_notifyValueHandle, value, valueLength)) {
    Serial.print("notify value update failed\r\n");
    return;
  }

  if (!g_ble.notifyCustomGattCharacteristic(g_notifyValueHandle, false)) {
    g_notifyPending = true;
    return;
  }

  Serial.print("queued notify seq=");
  Serial.print(g_notifySequence);
  Serial.print(" len=");
  Serial.print(valueLength);
  Serial.print(" phy=");
  Serial.print(phyText(info.txPhy));
  Serial.print("\r\n");
  ++g_notifySequence;
  g_notifyPending = false;
}

static bool maybeQueuePhyRequest(uint8_t txPhy, uint8_t rxPhy,
                                 const char* reason, uint32_t nowMs) {
  if ((nowMs - g_lastPhyRequestMs) < kPhyRequestRetryMs) {
    return false;
  }
  g_lastPhyRequestMs = nowMs;
  const bool queued = (txPhy == rxPhy) ? g_ble.requestPHY(txPhy)
                                       : g_ble.requestPreferredPhy(txPhy, rxPhy);
  if (queued) {
    Serial.print("request phy ");
    Serial.print(reason);
    Serial.print(": queued\r\n");
    return true;
  }
  return false;
}

static void maybeDrivePhyCycle(const BleConnectionInfo& info, uint32_t nowMs) {
  switch (g_cyclePhase) {
    case PhyCyclePhase::kRequestInitialCoded:
      if ((info.txPhy == kBlePhyCoded) && (info.rxPhy == kBlePhyCoded)) {
        g_cyclePhase = PhyCyclePhase::kComplete;
        g_phaseSinceMs = nowMs;
        Serial.print("cycle phase: coded active\r\n");
        return;
      }
      if (initialCodedRequestReady()) {
        maybeQueuePhyRequest(kBlePhyCoded, kBlePhyCoded, "coded", nowMs);
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

  Serial.print("\r\nBleCodedPhyWriteProbe start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    g_power.setLatencyMode(PowerLatencyMode::kLowPower);
    g_ble.setTraceCallback(onBleTrace, nullptr);
    ok = g_ble.setDeviceAddressString(kAddressText, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingName("XIAO54-CODED", true) &&
         g_ble.setGattDeviceName("XIAO54-CODED") &&
         g_ble.clearCustomGatt() &&
         g_ble.addCustomGattService(kNotifyServiceUuid, &g_notifyServiceHandle);
  }
  if (ok) {
    static const uint8_t kInitialValue[] = {'c', 'o', 'd', 'e', 'd'};
    const uint8_t props = static_cast<uint8_t>(kBleGattPropRead | kBleGattPropNotify);
    ok = g_ble.addCustomGattCharacteristic(g_notifyServiceHandle,
                                           kNotifyCharacteristicUuid,
                                           props,
                                           kInitialValue,
                                           sizeof(kInitialValue),
                                           &g_notifyValueHandle,
                                           &g_notifyCccdHandle);
  }
  if (ok) {
    static const uint8_t kInitialWriteValue[] = {'w', 'r', 'i', 't', 'e'};
    const uint8_t props = static_cast<uint8_t>(kBleGattPropRead |
                                               kBleGattPropWrite |
                                               kBleGattPropWriteNoRsp);
    ok = g_ble.addCustomGattCharacteristic(g_notifyServiceHandle,
                                           kWriteCharacteristicUuid,
                                           props,
                                           kInitialWriteValue,
                                           sizeof(kInitialWriteValue),
                                           &g_writeValueHandle,
                                           nullptr) &&
         g_ble.setCustomGattWriteHandler(g_writeValueHandle, handleWriteValue);
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (ok) {
    Serial.print("write handle=");
    Serial.print(g_writeValueHandle);
    Serial.print("\r\n");
  }
}

void loop() {
  if (!g_ble.isConnected()) {
    captureDisconnectDebug();
    BleAdvInteraction adv{};
    (void)g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    Gpio::write(kPinUserLed, true);
    if (g_wasConnected) {
      g_wasConnected = false;
      g_mtuRequestIssued = false;
      g_notifyPending = false;
      g_flexiblePreferenceSet = false;
      g_lastTxPhy = kBlePhyNone;
      g_lastRxPhy = kBlePhyNone;
      g_lastNotifyMs = 0U;
      g_lastPhyRequestMs = 0U;
      g_phaseSinceMs = 0U;
      g_notifySequence = 0U;
      g_notifySentCount = 0U;
      g_writeReceiveCount = 0U;
      g_lastWriteLength = 0U;
      g_cyclePhase = PhyCyclePhase::kRequestInitialCoded;
      Serial.print("disconnected\r\n");
      printDisconnectDebugIfAvailable();
    }
    delay(kAdvIntervalMs);
    return;
  }

  BleConnectionInfo info{};
  if (!g_ble.getConnectionInfo(&info)) {
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    g_lastNotifyMs = millis();
    g_phaseSinceMs = g_lastNotifyMs;
    Gpio::write(kPinUserLed, false);
    printConnectionState("connected", info);
  }

  if (!g_mtuRequestIssued && g_ble.currentAttMtu() < kRequestedMtu) {
    g_mtuRequestIssued = g_ble.requestAttMtuExchange(kRequestedMtu);
    if (g_mtuRequestIssued) {
      Serial.print("request mtu 247: queued\r\n");
    }
  }

  const uint32_t nowMs = millis();
  maybeDrivePhyCycle(info, nowMs);

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
    if (evt.txPacketSent &&
        evt.txPayload != nullptr &&
        evt.txPayloadLength == 251U &&
        evt.txPayload[4] == 0x1BU &&
        evt.txPayload[5] == static_cast<uint8_t>(g_notifyValueHandle & 0xFFU) &&
        evt.txPayload[6] == static_cast<uint8_t>((g_notifyValueHandle >> 8U) & 0xFFU)) {
      ++g_notifySentCount;
      Serial.print("notify tx ll=251 total=");
      Serial.print(g_notifySentCount);
      Serial.print(" phy=");
      Serial.print(phyText(evt.txPhy));
      Serial.print("\r\n");
    }
    if (evt.terminateInd) {
      captureDisconnectDebug();
      Serial.print("link terminated\r\n");
      printDisconnectDebugIfAvailable();
    }
  }

  if (longNotifyReady(info)) {
    if (g_notifyPending || ((nowMs - g_lastNotifyMs) >= kNotifyIntervalMs)) {
      g_lastNotifyMs = nowMs;
      queueLongNotify(info);
    }
  }

  if ((nowMs - g_lastReportMs) >= kStatusIntervalMs) {
    g_lastReportMs = nowMs;
    if (g_ble.getConnectionInfo(&info)) {
      printConnectionState("status", info);
    }
  }
}
