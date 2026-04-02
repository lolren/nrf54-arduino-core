/*
 * BleCustomGattRuntime
 *
 * Demonstrates how to create a custom GATT service and characteristics at
 * runtime using the BleRadio API. Two characteristics are added:
 *
 *   0xFFF1 – Read/Write/WriteNoRsp (a settable value)
 *   0xFFF2 – Read/Notify (a counter that the peripheral pushes to the central)
 *
 * After connecting with a BLE central (e.g., nRF Connect app):
 *   - Enable notifications on 0xFFF2 to receive periodic updates.
 *   - Write to 0xFFF1 to see the write callback fire on Serial.
 *   - Type "notify", "state", or "set <text>" in the Arduino Serial Monitor.
 *
 * Key concepts:
 *   - Handles: GATT attributes are referenced by 16-bit handles assigned
 *     during addCustomGattService / addCustomGattCharacteristic.
 *   - CCCD: Client Characteristic Configuration Descriptor. The central writes
 *     this to enable or disable notifications on 0xFFF2.
 *   - Notifications are queued; if the CCCD is not enabled they are silently
 *     dropped (the sketch prints a message in that case).
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

// GATT attribute handles assigned at setup. These are needed to update values
// or check CCCD state later in loop().
static uint16_t g_customServiceHandle = 0U;
static uint16_t g_rwValueHandle = 0U;     // Handle of the read/write characteristic.
static uint16_t g_notifyValueHandle = 0U; // Handle of the notify characteristic value.
static uint16_t g_notifyCccdHandle = 0U;  // Handle of the notify characteristic's CCCD.

static uint8_t g_notifyCounter = 0U;  // Wrapping counter embedded in each notification.
static uint32_t g_lastNotifyMs = 0U;

static char g_cmdBuffer[48];
static uint8_t g_cmdLength = 0U;
// TX power: -40 to +8 dBm. -8 is a safe indoor default.
static constexpr int8_t kTxPowerDbm = -8;

static void printBytes(const uint8_t* value, uint8_t length) {
  if (value == nullptr || length == 0U) {
    Serial.print("(empty)");
    return;
  }
  for (uint8_t i = 0U; i < length; ++i) {
    if (value[i] < 16U) {
      Serial.print('0');
    }
    Serial.print(value[i], HEX);
    if (i + 1U < length) {
      Serial.print(' ');
    }
  }
}

static void onCustomWrite(uint16_t valueHandle, const uint8_t* value,
                          uint8_t valueLength, bool withResponse,
                          void* context) {
  (void)context;
  Serial.print("custom write handle=0x");
  Serial.print(valueHandle, HEX);
  Serial.print(" via=");
  Serial.print(withResponse ? "req" : "cmd");
  Serial.print(" len=");
  Serial.print(valueLength);
  Serial.print(" data=");
  printBytes(value, valueLength);
  Serial.print("\r\n");
}

static void queueNotifyPacket() {
  uint8_t value[2] = {'N', g_notifyCounter++};
  if (!g_ble.setCustomGattCharacteristicValue(g_notifyValueHandle, value,
                                              sizeof(value))) {
    Serial.print("notify update failed\r\n");
    return;
  }
  if (!g_ble.notifyCustomGattCharacteristic(g_notifyValueHandle, false)) {
    Serial.print("notify queue skipped (CCCD disabled or busy)\r\n");
  }
}

static void handleLine(const char* line) {
  if (line == nullptr) {
    return;
  }

  if (strcmp(line, "notify") == 0) {
    queueNotifyPacket();
    return;
  }
  if (strcmp(line, "state") == 0) {
    const bool notifyEnabled =
        g_ble.isCustomGattCccdEnabled(g_notifyValueHandle, false);
    Serial.print("state connected=");
    Serial.print(g_ble.isConnected() ? "yes" : "no");
    Serial.print(" notify_cccd=");
    Serial.print(notifyEnabled ? "on" : "off");
    Serial.print("\r\n");
    return;
  }
  if (strncmp(line, "set ", 4) == 0) {
    const char* text = &line[4];
    const size_t length = strlen(text);
    if (length > BleRadio::kCustomGattMaxValueLength) {
      Serial.print("set too long\r\n");
      return;
    }
    if (!g_ble.setCustomGattCharacteristicValue(
            g_rwValueHandle, reinterpret_cast<const uint8_t*>(text),
            static_cast<uint8_t>(length))) {
      Serial.print("set failed\r\n");
      return;
    }
    Serial.print("set ok\r\n");
    return;
  }
  Serial.print("commands: notify | state | set <text>\r\n");
}

static void pollSerialCommands() {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch < 0) {
      break;
    }
    if (ch == '\r' || ch == '\n') {
      if (g_cmdLength > 0U) {
        g_cmdBuffer[g_cmdLength] = '\0';
        handleLine(g_cmdBuffer);
        g_cmdLength = 0U;
      }
      continue;
    }
    if (g_cmdLength + 1U < sizeof(g_cmdBuffer)) {
      g_cmdBuffer[g_cmdLength++] = static_cast<char>(ch);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleCustomGattRuntime start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  static const uint8_t kAddress[6] = {0x44, 0x00, 0x15, 0x54, 0xDE, 0xC0};

  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    // Set after begin() so the radio subsystem is already configured.
    g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  }
  if (ok) {
    // clearCustomGatt() must be called before adding services so the GATT
    // table is empty and handles start from a predictable offset.
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingName("X54-CUSTOM", true) &&
         g_ble.setScanResponseName("X54-CUSTOM-SCAN") &&
         g_ble.setGattDeviceName("X54 Custom Runtime") &&
         g_ble.setGattBatteryLevel(92U) && g_ble.clearCustomGatt();
  }
  if (ok) {
    // addCustomGattService() registers a 16-bit UUID service. The handle is
    // written into g_customServiceHandle and is needed when adding
    // characteristics that belong to this service.
    ok = g_ble.addCustomGattService(0xFFF0U, &g_customServiceHandle);
  }
  if (ok) {
    const uint8_t initialValue[] = {'O', 'K'};
    // Property flags are ORed together:
    //   kBleGattPropRead       – central can read the value.
    //   kBleGattPropWrite      – central can write with response (ATT Write Request).
    //   kBleGattPropWriteNoRsp – central can write without response (ATT Write Command).
    const uint8_t props = static_cast<uint8_t>(kBleGattPropRead |
                                               kBleGattPropWrite |
                                               kBleGattPropWriteNoRsp);
    // Passing nullptr as the last argument means no CCCD is created (no notifications).
    ok = g_ble.addCustomGattCharacteristic(g_customServiceHandle, 0xFFF1U, props,
                                           initialValue, sizeof(initialValue),
                                           &g_rwValueHandle, nullptr);
  }
  if (ok) {
    const uint8_t notifyValue[] = {'N', 0U};
    // kBleGattPropNotify lets the peripheral push updates without being asked.
    // The last parameter receives the CCCD handle so we can check if the
    // central has enabled notifications.
    const uint8_t props = static_cast<uint8_t>(kBleGattPropRead |
                                               kBleGattPropNotify);
    ok = g_ble.addCustomGattCharacteristic(g_customServiceHandle, 0xFFF2U, props,
                                           notifyValue, sizeof(notifyValue),
                                           &g_notifyValueHandle,
                                           &g_notifyCccdHandle);
  }
  if (ok) {
    // Register a callback that fires whenever the central writes to any custom
    // characteristic. The callback runs in the main-loop context here.
    g_ble.setCustomGattWriteCallback(onCustomWrite, nullptr);
  }

  Serial.print("BLE custom GATT init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (ok) {
    Serial.print("service=0x");
    Serial.print(g_customServiceHandle, HEX);
    Serial.print(" rw=0x");
    Serial.print(g_rwValueHandle, HEX);
    Serial.print(" notify=0x");
    Serial.print(g_notifyValueHandle, HEX);
    Serial.print(" cccd=0x");
    Serial.print(g_notifyCccdHandle, HEX);
    Serial.print("\r\n");
  }
  Serial.print("commands: notify | state | set <text>\r\n");
}

void loop() {
  pollSerialCommands();

  if (!g_ble.isConnected()) {
    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    Gpio::write(kPinUserLed, true);
    delay(1);
    return;
  }

  BleConnectionEvent evt{};
  if (g_ble.pollConnectionEvent(&evt, 450000UL) && evt.eventStarted) {
    Gpio::write(kPinUserLed, false);
    if (evt.packetReceived && evt.crcOk && evt.attPacket) {
      Serial.print("att op=0x");
      Serial.print(evt.attOpcode, HEX);
      Serial.print(" ce=");
      Serial.print(evt.eventCounter);
      Serial.print("\r\n");
    }
    if (evt.terminateInd) {
      Serial.print("link terminated\r\n");
      Gpio::write(kPinUserLed, true);
    }
  }

  const uint32_t nowMs = millis();
  // Only send notifications every 1500 ms and only if the central has enabled
  // them by writing 0x0001 to the CCCD. isCustomGattCccdEnabled() checks that.
  if ((nowMs - g_lastNotifyMs) >= 1500UL &&
      g_ble.isCustomGattCccdEnabled(g_notifyValueHandle, false)) {
    g_lastNotifyMs = nowMs;
    queueNotifyPacket();
  }

  delay(1);
}
