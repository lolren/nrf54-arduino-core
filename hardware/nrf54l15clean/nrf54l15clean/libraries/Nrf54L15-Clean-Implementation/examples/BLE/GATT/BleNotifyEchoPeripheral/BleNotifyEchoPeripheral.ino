#include <Arduino.h>

#include <stdio.h>
#include <string.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

BleRadio g_ble;
PowerManager g_power;

uint16_t g_serviceHandle = 0U;
uint16_t g_writeValueHandle = 0U;
uint16_t g_notifyValueHandle = 0U;
uint16_t g_notifyCccdHandle = 0U;

char g_lastWrite[BleRadio::kCustomGattMaxValueLength + 1U] = {0};
bool g_writePending = false;
bool g_wasConnected = false;

uint16_t g_tickCounter = 0U;
uint32_t g_lastTickMs = 0U;

constexpr int8_t kTxPowerDbm = -8;
constexpr uint32_t kTickIntervalMs = 2000UL;

void copyAsciiValue(char* outBuffer, size_t outBufferSize, const uint8_t* value,
                    uint8_t valueLength) {
  if (outBuffer == nullptr || outBufferSize == 0U) {
    return;
  }

  const size_t maxCopy = outBufferSize - 1U;
  const size_t copyLength = (valueLength < maxCopy) ? valueLength : maxCopy;
  if (value != nullptr && copyLength > 0U) {
    memcpy(outBuffer, value, copyLength);
  }
  outBuffer[copyLength] = '\0';
}

bool queueNotifyMessage(const char* text) {
  if (text == nullptr) {
    return false;
  }

  const size_t messageLength = strlen(text);
  const uint8_t valueLength = static_cast<uint8_t>(
      (messageLength < BleRadio::kCustomGattMaxValueLength)
          ? messageLength
          : BleRadio::kCustomGattMaxValueLength);

  if (!g_ble.setCustomGattCharacteristicValue(
          g_notifyValueHandle, reinterpret_cast<const uint8_t*>(text),
          valueLength)) {
    Serial.print("notify value update failed\r\n");
    return false;
  }

  if (!g_ble.notifyCustomGattCharacteristic(g_notifyValueHandle, false)) {
    Serial.print("notify skipped (CCCD disabled or controller busy)\r\n");
    return false;
  }

  Serial.print("notify: ");
  Serial.print(text);
  Serial.print("\r\n");
  return true;
}

void onWrite(uint16_t valueHandle, const uint8_t* value, uint8_t valueLength,
             bool withResponse, void* context) {
  (void)context;

  if (valueHandle != g_writeValueHandle) {
    return;
  }

  copyAsciiValue(g_lastWrite, sizeof(g_lastWrite), value, valueLength);
  g_writePending = true;

  Serial.print("write ");
  Serial.print(withResponse ? "req" : "cmd");
  Serial.print(": ");
  Serial.print(g_lastWrite);
  Serial.print("\r\n");
}

void printUsage() {
  Serial.print("Service  0xFFF0\r\n");
  Serial.print("Write    0xFFF1 (UTF-8 text from central)\r\n");
  Serial.print("Notify   0xFFF2 (tick:<n> and rx:<text>)\r\n");
  Serial.print("Subscribe to 0xFFF2 notifications, then write text to 0xFFF1.\r\n");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleNotifyEchoPeripheral start\r\n");

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  static const uint8_t kAddress[6] = {0x46, 0x00, 0x15, 0x54, 0xDE, 0xC0};
  static const uint8_t kWriteInitialValue[] = {'h', 'e', 'l', 'l', 'o'};
  static const uint8_t kNotifyInitialValue[] = {'r', 'e', 'a', 'd', 'y'};

  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingName("X54-NOTIFY", true) &&
         g_ble.setScanResponseName("X54-NOTIFY-SCAN") &&
         g_ble.setGattDeviceName("XIAO nRF54L15 Notify") &&
         g_ble.setGattBatteryLevel(100U) && g_ble.clearCustomGatt();
  }
  if (ok) {
    ok = g_ble.addCustomGattService(0xFFF0U, &g_serviceHandle);
  }
  if (ok) {
    const uint8_t writeProps = static_cast<uint8_t>(
        kBleGattPropRead | kBleGattPropWrite | kBleGattPropWriteNoRsp);
    ok = g_ble.addCustomGattCharacteristic(
        g_serviceHandle, 0xFFF1U, writeProps, kWriteInitialValue,
        sizeof(kWriteInitialValue), &g_writeValueHandle, nullptr);
  }
  if (ok) {
    const uint8_t notifyProps =
        static_cast<uint8_t>(kBleGattPropRead | kBleGattPropNotify);
    ok = g_ble.addCustomGattCharacteristic(
        g_serviceHandle, 0xFFF2U, notifyProps, kNotifyInitialValue,
        sizeof(kNotifyInitialValue), &g_notifyValueHandle, &g_notifyCccdHandle);
  }
  if (ok) {
    g_ble.setCustomGattWriteCallback(onWrite, nullptr);
  }

  Serial.print("BLE notify init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (ok) {
    Serial.print("service=0x");
    Serial.print(g_serviceHandle, HEX);
    Serial.print(" write=0x");
    Serial.print(g_writeValueHandle, HEX);
    Serial.print(" notify=0x");
    Serial.print(g_notifyValueHandle, HEX);
    Serial.print(" cccd=0x");
    Serial.print(g_notifyCccdHandle, HEX);
    Serial.print("\r\n");
    printUsage();
  }
}

void loop() {
  if (!g_ble.isConnected()) {
    g_wasConnected = false;
    g_writePending = false;

    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    Gpio::write(kPinUserLed, true);
    delay(1);
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    g_tickCounter = 0U;
    g_lastTickMs = millis();
    Serial.print("central connected\r\n");
  }

  BleConnectionEvent evt{};
  if (g_ble.pollConnectionEvent(&evt, 450000UL) && evt.eventStarted) {
    Gpio::write(kPinUserLed, false);
    if (evt.terminateInd) {
      Serial.print("link terminated\r\n");
      Gpio::write(kPinUserLed, true);
      delay(1);
      return;
    }
  }

  if (g_writePending) {
    char notifyText[BleRadio::kCustomGattMaxValueLength + 1U] = {0};
    snprintf(notifyText, sizeof(notifyText), "rx:%.*s",
             static_cast<int>(BleRadio::kCustomGattMaxValueLength - 3U),
             g_lastWrite);
    queueNotifyMessage(notifyText);
    g_writePending = false;
  }

  const uint32_t nowMs = millis();
  if ((nowMs - g_lastTickMs) >= kTickIntervalMs &&
      g_ble.isCustomGattCccdEnabled(g_notifyValueHandle, false)) {
    g_lastTickMs = nowMs;

    char tickText[BleRadio::kCustomGattMaxValueLength + 1U] = {0};
    snprintf(tickText, sizeof(tickText), "tick:%u",
             static_cast<unsigned>(++g_tickCounter));
    queueNotifyMessage(tickText);
  }

  delay(1);
}
