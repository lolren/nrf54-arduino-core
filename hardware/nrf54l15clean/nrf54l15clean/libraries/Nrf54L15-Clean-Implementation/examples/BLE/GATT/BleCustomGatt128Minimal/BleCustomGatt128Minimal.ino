/*
 * BleCustomGatt128Minimal
 *
 * Minimal example of a custom GATT service using 128-bit (vendor-specific)
 * UUIDs. A 128-bit UUID uniquely identifies a proprietary service that is not
 * registered with the Bluetooth SIG, allowing any application-specific protocol.
 *
 * Only one characteristic is added:
 *   Read + Notify, initial value = "OK"
 *
 * To test:
 *   1. Connect with nRF Connect.
 *   2. Browse to the custom service UUID.
 *   3. Enable notifications on the characteristic.
 *   4. Read the value to see "OK".
 *
 * Tip: 128-bit UUIDs are stored in little-endian order in the byte array.
 * The UUID displayed by a phone will show bytes in reverse order compared
 * to the array declaration here.
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static uint16_t g_serviceHandle = 0U;  // Handle of the custom 128-bit service.
static uint16_t g_valueHandle = 0U;    // Handle of the characteristic value.
static uint16_t g_cccdHandle = 0U;     // Handle of the CCCD (notification enable).

// TX power in dBm.
static constexpr int8_t kTxPowerDbm = -8;
// 128-bit service UUID in little-endian byte order.
// Change these to your own unique UUID before shipping a product.
static const uint8_t kServiceUuid[16] = {
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0x4D, 0xEF,
    0x8A, 0xBC, 0xDE, 0xF0, 0x12, 0x34, 0x56, 0x78};
// 128-bit characteristic UUID in little-endian byte order.
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
  // clearCustomGatt() resets the custom GATT table. Must be called before
  // adding services so handles are assigned from a known starting offset.
  ok = ok && g_ble.clearCustomGatt();
  // addCustomGattService128() registers a service identified by a 128-bit UUID.
  // The returned g_serviceHandle is used when adding characteristics.
  ok = ok && g_ble.addCustomGattService128(kServiceUuid, &g_serviceHandle);
  // addCustomGattCharacteristic128() uses a 128-bit UUID for the characteristic.
  // g_cccdHandle is non-null so a CCCD descriptor is created, enabling
  // notifications (the central writes 0x0001 to enable them).
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
