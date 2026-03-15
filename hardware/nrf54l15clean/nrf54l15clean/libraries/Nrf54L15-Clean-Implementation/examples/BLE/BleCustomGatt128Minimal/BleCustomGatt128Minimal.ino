#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static uint16_t g_serviceHandle = 0U;
static uint16_t g_valueHandle = 0U;
static uint16_t g_cccdHandle = 0U;

static constexpr int8_t kTxPowerDbm = -8;
static const uint8_t kServiceUuid[16] = {
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0x4D, 0xEF,
    0x8A, 0xBC, 0xDE, 0xF0, 0x12, 0x34, 0x56, 0x78};
static const uint8_t kValueUuid[16] = {
    0x87, 0x65, 0x43, 0x21, 0xBA, 0x98, 0x4F, 0xED,
    0x8C, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10, 0xFE};

void setup() {
  Serial.begin(115200);
  delay(200);

  const uint8_t initialValue[] = {'O', 'K'};
  const uint8_t props = static_cast<uint8_t>(kBleGattPropRead | kBleGattPropNotify);

  bool ok = g_ble.begin(kTxPowerDbm);
  ok = ok && g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd);
  ok = ok && g_ble.setAdvertisingName("X54-UUID128", true);
  ok = ok && g_ble.setGattDeviceName("X54 UUID128");
  ok = ok && g_ble.clearCustomGatt();
  ok = ok && g_ble.addCustomGattService128(kServiceUuid, &g_serviceHandle);
  ok = ok && g_ble.addCustomGattCharacteristic128(
                 g_serviceHandle, kValueUuid, props, initialValue,
                 sizeof(initialValue), &g_valueHandle, &g_cccdHandle);

  Serial.print("BleCustomGatt128Minimal init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
}

void loop() {
  if (!g_ble.isConnected()) {
    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
  }
  delay(10);
}
