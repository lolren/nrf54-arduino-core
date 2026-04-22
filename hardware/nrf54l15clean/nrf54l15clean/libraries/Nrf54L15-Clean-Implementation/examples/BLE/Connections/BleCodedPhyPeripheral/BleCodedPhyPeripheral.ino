/*
 * BleCodedPhyPeripheral
 *
 * Peripheral-side coded PHY example for the XIAO nRF54L15 pair.
 *
 * Use it with BleCodedPhyCentral on the other board. This sketch requests
 * coded PHY with requestPHY(kBlePhyCoded), prints getPHY() in the status log,
 * and keeps 244-byte notifications flowing so the negotiated PHY is proven
 * under real traffic instead of idle state only.
 *
 * After coded is active it widens the local mask with
 * setPreferredPhyOptions(1M|CODED, 1M|CODED) so the companion sketch can drive
 * a fallback and recovery cycle without editing the example.
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static constexpr int8_t kTxPowerDbm = 0;
static constexpr uint32_t kAdvIntervalMs = 100;
static constexpr uint32_t kStatusIntervalMs = 1000;
static constexpr uint32_t kNotifyIntervalMs = 1500;
static constexpr uint32_t kPhyRequestRetryMs = 500;
static constexpr uint32_t kHold1MBeforeReturnMs = 3500;
static constexpr uint16_t kRequestedMtu = 247U;
static constexpr uint16_t kNotifyServiceUuid = 0xFFF0U;
static constexpr uint16_t kNotifyCharacteristicUuid = 0xFFF1U;
static constexpr char kAddressText[] = "C0:DE:54:15:20:41";

enum class PhyCyclePhase : uint8_t {
  kRequestInitialCoded = 0,
  kWaitForFallback1M = 1,
  kHold1MBeforeReturn = 2,
  kRequestReturnCoded = 3,
  kComplete = 4,
};

static uint16_t g_notifyServiceHandle = 0U;
static uint16_t g_notifyValueHandle = 0U;
static uint16_t g_notifyCccdHandle = 0U;
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
static PhyCyclePhase g_cyclePhase = PhyCyclePhase::kRequestInitialCoded;

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
    case PhyCyclePhase::kWaitForFallback1M:
      return "wait_1m";
    case PhyCyclePhase::kHold1MBeforeReturn:
      return "hold_1m";
    case PhyCyclePhase::kRequestReturnCoded:
      return "ret_coded";
    case PhyCyclePhase::kComplete:
      return "done";
    default:
      return "?";
  }
}

static void printConnectionState(const char* prefix, const BleConnectionInfo& info) {
  Serial.print(prefix);
  Serial.print(" phy=");
  Serial.print(phyText(g_ble.getPHY()));
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
        g_cyclePhase = PhyCyclePhase::kWaitForFallback1M;
        g_phaseSinceMs = nowMs;
        Serial.print("cycle phase: coded active\r\n");
        return;
      }
      if (initialCodedRequestReady()) {
        maybeQueuePhyRequest(kBlePhyCoded, kBlePhyCoded, "coded", nowMs);
      }
      return;

    case PhyCyclePhase::kWaitForFallback1M:
      if (!g_flexiblePreferenceSet &&
          g_ble.setPreferredPhyOptions(
              static_cast<uint8_t>(kBlePhy1M | kBlePhyCoded),
              static_cast<uint8_t>(kBlePhy1M | kBlePhyCoded))) {
        g_flexiblePreferenceSet = true;
        Serial.print("phy preference: flexible\r\n");
      }
      if ((info.txPhy == kBlePhy1M) && (info.rxPhy == kBlePhy1M)) {
        g_cyclePhase = PhyCyclePhase::kHold1MBeforeReturn;
        g_phaseSinceMs = nowMs;
        Serial.print("cycle phase: fallback 1M observed\r\n");
      }
      return;

    case PhyCyclePhase::kHold1MBeforeReturn:
      if ((info.txPhy == kBlePhyCoded) && (info.rxPhy == kBlePhyCoded)) {
        g_cyclePhase = PhyCyclePhase::kWaitForFallback1M;
        g_phaseSinceMs = nowMs;
        return;
      }
      if ((info.txPhy == kBlePhy1M) && (info.rxPhy == kBlePhy1M) &&
          ((nowMs - g_phaseSinceMs) >= kHold1MBeforeReturnMs)) {
        g_cyclePhase = PhyCyclePhase::kRequestReturnCoded;
        Serial.print("cycle phase: request coded return\r\n");
      }
      return;

    case PhyCyclePhase::kRequestReturnCoded:
      if ((info.txPhy == kBlePhyCoded) && (info.rxPhy == kBlePhyCoded)) {
        g_cyclePhase = PhyCyclePhase::kComplete;
        g_phaseSinceMs = nowMs;
        Serial.print("cycle phase: coded return complete\r\n");
        return;
      }
      maybeQueuePhyRequest(kBlePhyCoded, kBlePhyCoded, "coded_return", nowMs);
      return;

    case PhyCyclePhase::kComplete:
    default:
      return;
  }
}

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nBleCodedPhyPeripheral start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    g_power.setLatencyMode(PowerLatencyMode::kLowPower);
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

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
}

void loop() {
  if (!g_ble.isConnected()) {
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
      g_cyclePhase = PhyCyclePhase::kRequestInitialCoded;
      Serial.print("disconnected\r\n");
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
      Serial.print("link terminated\r\n");
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
