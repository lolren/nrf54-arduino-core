#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static uint16_t g_notifyServiceHandle = 0U;
static uint16_t g_notifyValueHandle = 0U;
static uint16_t g_notifyCccdHandle = 0U;
static uint32_t g_lastNotifyMs = 0U;
static uint32_t g_notifyCounter = 0U;
static uint32_t g_notifySentCount = 0U;
static constexpr bool kEnableNotifyTraffic = true;
static constexpr uint32_t kNotifyIntervalMs = 1000UL;
static bool g_wasConnected = false;
static uint32_t g_traceCount = 0U;
static char g_lastTrace[24] = {0};
static volatile BleDisconnectDebug g_lastDisconnectDebug{};
static volatile bool g_hasDisconnectDebug = false;
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

static void queueNotify() {
  char value[20] = {0};
  const int written = snprintf(value, sizeof(value), "pkt-%lu",
                               static_cast<unsigned long>(g_notifyCounter++));
  if (written <= 0) {
    return;
  }

  uint8_t valueLength = static_cast<uint8_t>(written);
  if (valueLength >= sizeof(value)) {
    valueLength = static_cast<uint8_t>(sizeof(value) - 1U);
  }

  if (!g_ble.setCustomGattCharacteristicValue(
          g_notifyValueHandle, reinterpret_cast<const uint8_t*>(value),
          valueLength)) {
    Serial.print("notify value update failed\r\n");
    return;
  }

  if (!g_ble.notifyCustomGattCharacteristic(g_notifyValueHandle, false)) {
    return;
  }
}

static void captureDisconnectDebug() {
  BleDisconnectDebug debug{};
  if (g_ble.getDisconnectDebug(&debug)) {
    memcpy(const_cast<BleDisconnectDebug*>(&g_lastDisconnectDebug), &debug,
           sizeof(debug));
    g_hasDisconnectDebug = true;
  }
}

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleNotifyPeripheral start\r\n");

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);
  static const uint8_t kAddress[6] = {0x42, 0x00, 0x15, 0x54, 0xDE, 0xC0};
  static const uint8_t kInitialValue[] = {'p', 'k', 't', '-', '0'};

  bool ok = g_ble.begin(-4);
  if (ok) {
    g_ble.setTraceCallback(onBleTrace, nullptr);
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingName("X54-NOTIFY", true) &&
         g_ble.setScanResponseName("X54-NOTIFY-SCAN") &&
         g_ble.setGattDeviceName("X54 Notify Peripheral") &&
         g_ble.clearCustomGatt();
  }
  if (ok) {
    ok = g_ble.addCustomGattService(0xFFF0U, &g_notifyServiceHandle);
  }
  if (ok) {
    const uint8_t props = static_cast<uint8_t>(kBleGattPropRead |
                                               kBleGattPropNotify);
    ok = g_ble.addCustomGattCharacteristic(
        g_notifyServiceHandle, 0xFFF1U, props, kInitialValue,
        sizeof(kInitialValue), &g_notifyValueHandle, &g_notifyCccdHandle);
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (ok) {
    Serial.print("notify value handle=0x");
    Serial.print(g_notifyValueHandle, HEX);
    Serial.print(" cccd handle=0x");
    Serial.print(g_notifyCccdHandle, HEX);
    Serial.print("\r\n");
  }
}

void loop() {
  if (!g_ble.isConnected()) {
    captureDisconnectDebug();
    g_wasConnected = false;
    BleAdvInteraction adv{};
    (void)g_ble.advertiseInteractEvent(&adv, 350U, 300000UL, 700000UL);
    Gpio::write(kPinUserLed, true);
    delay(1);
    return;
  }

  BleConnectionEvent evt{};
  if (!g_ble.pollConnectionEvent(&evt, 450000UL)) {
    return;
  }
  recordLinkTrace(evt);

  if (evt.terminateInd) {
    captureDisconnectDebug();
    Serial.print("link terminated\r\n");
    Gpio::write(kPinUserLed, true);
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    g_lastNotifyMs = millis();
    Serial.print("central connected\r\n");
  }

  Gpio::write(kPinUserLed, false);

  const uint32_t now = millis();
  if (kEnableNotifyTraffic &&
      (now - g_lastNotifyMs) >= kNotifyIntervalMs &&
      g_ble.isCustomGattCccdEnabled(g_notifyValueHandle, false)) {
    g_lastNotifyMs = now;
    queueNotify();
  }

  if (evt.txPayloadLength >= 7U && evt.txPayload != nullptr &&
      evt.txPayload[4] == 0x1BU &&
      evt.txPayload[5] == static_cast<uint8_t>(g_notifyValueHandle & 0xFFU) &&
      evt.txPayload[6] == static_cast<uint8_t>((g_notifyValueHandle >> 8U) & 0xFFU)) {
    ++g_notifySentCount;
  }

}
