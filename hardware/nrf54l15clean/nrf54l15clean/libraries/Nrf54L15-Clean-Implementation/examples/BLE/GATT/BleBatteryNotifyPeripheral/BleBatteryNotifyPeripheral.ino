/*
 * BleBatteryNotifyPeripheral
 *
 * Demonstrates the standard BLE Battery Service (UUID 0x180F) with
 * Battery Level notifications (characteristic UUID 0x2A19).
 *
 * After connecting and enabling notifications on Battery Level CCCD (0x2A19),
 * the peripheral sends a notification every 5 seconds with a slowly decrementing
 * battery percentage (100% → 5% → wraps back to 100%).
 *
 * This is a good starting point for any sensor node that needs to report
 * battery state to a phone app.
 *
 * Tip: setGattBatteryLevel() updates the in-memory value but does NOT
 * automatically push a notification. The BLE core sends a notification
 * implicitly on the next connection event if the central has enabled the CCCD.
 * You can verify the notification was sent by watching the tx payload check
 * at the bottom of loop().
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static uint8_t g_batteryLevel = 100U;  // Starts at 100% and steps down 1% every 5 s.
static uint32_t g_lastBatteryStepMs = 0U;
static bool g_wasConnected = false;
// -8 dBm: conservative default; good for office-distance testing.
static constexpr int8_t kTxPowerDbm = -8;

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleBatteryNotifyPeripheral start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  static const uint8_t kAddress[6] = {0x41, 0x00, 0x15, 0x54, 0xDE, 0xC0};
  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    // Set after begin() so the radio subsystem is already configured.
    g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  }
  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingName("X54-BATT", true) &&
         g_ble.setScanResponseName("X54-BATT-SCAN") &&
         g_ble.setGattDeviceName("XIAO nRF54L15 Battery") &&
         g_ble.setGattBatteryLevel(g_batteryLevel);
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  Serial.print("Enable notifications on Battery Level (0x2A19) CCCD to receive updates.\r\n");
}

void loop() {
  if (!g_ble.isConnected()) {
    g_wasConnected = false;
    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 300000UL, 700000UL);
    Gpio::write(kPinUserLed, true);
    delay(1);
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    g_lastBatteryStepMs = millis();
  }

  BleConnectionEvent evt{};
  if (!g_ble.pollConnectionEvent(&evt, 450000UL)) {
    delay(1);
    return;
  }

  if (evt.terminateInd) {
    Serial.print("link terminated\r\n");
    Gpio::write(kPinUserLed, true);
    return;
  }

  if (evt.packetReceived && evt.crcOk) {
    Gpio::write(kPinUserLed, false);
  } else {
    Gpio::write(kPinUserLed, true);
  }

  const uint32_t now = millis();
  if ((now - g_lastBatteryStepMs) >= 5000UL) {
    g_lastBatteryStepMs = now;
    g_batteryLevel = (g_batteryLevel > 5U) ? static_cast<uint8_t>(g_batteryLevel - 1U)
                                            : 100U;
    g_ble.setGattBatteryLevel(g_batteryLevel);

    Serial.print("battery level updated to ");
    Serial.print(g_batteryLevel);
    Serial.print("%\r\n");
  }

  // Inspect the outgoing L2CAP/ATT packet to confirm a Battery Level notify
  // was transmitted. ATT notification payload layout:
  //   [0..1] = L2CAP length (little-endian)
  //   [2..3] = L2CAP channel ID (ATT = 0x0004)
  //   [4]    = ATT opcode (0x1B = Handle Value Notification)
  //   [5..6] = attribute handle (little-endian); 0x0012 = Battery Level in this GATT layout
  //   [7]    = battery level value (0–100)
  if (evt.txPayloadLength >= 8U && evt.txPayload != nullptr &&
      evt.txPayload[4] == 0x1BU && evt.txPayload[5] == 0x12U && evt.txPayload[6] == 0x00U) {
    Serial.print("battery notify sent: ");
    Serial.print(evt.txPayload[7]);
    Serial.print("%\r\n");
  }

  delay(1);
}
