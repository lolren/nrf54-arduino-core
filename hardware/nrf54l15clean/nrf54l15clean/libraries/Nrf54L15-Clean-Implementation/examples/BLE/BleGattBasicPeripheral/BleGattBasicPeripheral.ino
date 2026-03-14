#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static uint8_t g_batteryLevel = 100U;
static uint32_t g_lastBatteryUpdateMs = 0U;
static uint32_t g_lastLogMs = 0U;
static constexpr int8_t kTxPowerDbm = -8;

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleGattBasicPeripheral start\r\n");

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  static const uint8_t kAddress[6] = {0x31, 0x00, 0x15, 0x54, 0xDE, 0xC0};
  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingName("X54-GATT", true) &&
         g_ble.setScanResponseName("X54-GATT-SCAN") &&
         g_ble.setGattDeviceName("XIAO nRF54L15 Clean") &&
         g_ble.setGattBatteryLevel(g_batteryLevel);
  }

  Serial.print("BLE GATT init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
}

void loop() {
  const uint32_t nowMs = millis();
  if ((nowMs - g_lastBatteryUpdateMs) >= 30000UL) {
    g_lastBatteryUpdateMs = nowMs;
    g_batteryLevel = (g_batteryLevel > 5U) ? static_cast<uint8_t>(g_batteryLevel - 1U)
                                            : 100U;
    g_ble.setGattBatteryLevel(g_batteryLevel);
  }

  if (!g_ble.isConnected()) {
    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    Gpio::write(kPinUserLed, true);
    delay(1);
    return;
  }

  BleConnectionEvent evt{};
  const bool ran = g_ble.pollConnectionEvent(&evt, 450000UL);
  if (ran && evt.eventStarted) {
    Gpio::write(kPinUserLed, false);

    if (evt.packetReceived && evt.crcOk && (evt.attPacket || evt.llControlPacket)) {
      Serial.print("ce=");
      Serial.print(evt.eventCounter);
      Serial.print(" llid=");
      Serial.print(evt.llid);
      Serial.print(" rx_len=");
      Serial.print(evt.payloadLength);
      Serial.print(" tx_len=");
      Serial.print(evt.txPayloadLength);
      if (evt.llControlPacket) {
        Serial.print(" ll_op=0x");
        if (evt.llControlOpcode < 16U) {
          Serial.print('0');
        }
        Serial.print(evt.llControlOpcode, HEX);
      }
      if (evt.attPacket) {
        Serial.print(" att_op=0x");
        if (evt.attOpcode < 16U) {
          Serial.print('0');
        }
        Serial.print(evt.attOpcode, HEX);
      }
      Serial.print("\r\n");
    }

    if (evt.terminateInd) {
      Serial.print("link terminated\r\n");
      Gpio::write(kPinUserLed, true);
    }
    return;
  }

  if ((nowMs - g_lastLogMs) >= 2000UL) {
    g_lastLogMs = nowMs;
    Serial.print("connected batt=");
    Serial.print(g_batteryLevel);
    Serial.print("%\r\n");
  }

  Gpio::write(kPinUserLed, true);
  delay(1);
}
