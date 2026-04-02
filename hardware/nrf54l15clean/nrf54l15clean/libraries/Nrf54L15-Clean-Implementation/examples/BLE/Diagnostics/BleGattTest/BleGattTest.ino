/*
 * BleGattTest
 *
 * Reliability stress-test for BLE GATT notifications over the Nordic UART
 * Service (NUS). After connecting, the sketch:
 *   - Sends a numbered test packet every 1 second via NUS TX notifications.
 *   - Echoes back "ACK" for every byte received from the central.
 *   - Tracks sent/received/dropped counters and prints them periodically.
 *
 * Use with nRF Connect or a custom central to verify:
 *   1. Notifications are sent reliably.
 *   2. Stalled notifications recover via timeout.
 *   3. Reconnection works correctly after a disconnect.
 *
 * Tip: enable notification on the NUS TX characteristic in the central app
 * before data will flow. NUS TX (peripheral→central) requires CCCD write 0x0001.
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"
#include "ble_nus.h"

using namespace xiao_nrf54l15;

// BLE GATT Reliability Test Sketch
//
// This sketch tests the BLE GATT notification reliability improvements:
// - Tests notification confirmation tracking
// - Tests timeout recovery for stalled notifications
// - Tests connection state handling
//
// Use with a BLE central device (e.g., nRF Connect app) to verify:
// 1. Notifications are sent reliably
// 2. Stalled notifications recover via timeout
// 3. Reconnection works correctly

static BleRadio g_ble;
static BleNordicUart g_nus(g_ble);
static PowerManager g_power;

static uint32_t g_packetsSent = 0;
static uint32_t g_packetsReceived = 0;
static uint32_t g_notificationsQueued = 0;
static uint32_t g_notificationsCompleted = 0;
static uint32_t g_timeoutsRecovered = 0;
static uint32_t g_lastTestTime = 0;
static bool g_initOk = false;
static bool g_wasConnected = false;

// kAntennaPath: kCeramic = on-board antenna; kExternal = external u.FL connector.
static constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
// kTxPowerDbm = 8: maximum power for reliable range. Reduce for bench testing.
static constexpr int8_t kTxPowerDbm = 8;
// kTestIntervalMs: how often to send a numbered test packet while connected.
static constexpr uint32_t kTestIntervalMs = 1000;
static constexpr uint32_t kQuickTestIntervalMs = 100;  // Unused in normal mode.

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.println("\r\n========================================");
  Serial.println("BLE GATT Reliability Test");
  Serial.println("========================================");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  // setAntennaPath() routes the radio to the selected antenna and powers the
  // RF switch. Must be called before begin() on boards with an RF switch.
  bool ok = BoardControl::setAntennaPath(kAntennaPath);
  if (ok) {
    ok = g_ble.begin(kTxPowerDbm);
  }

  static const uint8_t kAddress[6] = {0x11, 0x00, 0x15, 0x54, 0xDE, 0xC0};

  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         // Name ≤ 8 chars fits alongside the NUS UUID in the 31-byte ad payload.
         g_ble.setGattDeviceName("X54-GATT") &&
         // clearCustomGatt() removes any GATT services left by a previous sketch.
         g_ble.clearCustomGatt() &&
         // g_nus.begin() registers NUS TX/RX characteristics and sets the ad UUID.
         g_nus.begin();
  }
  if (ok) {
    // Set latency mode after BLE init so the radio subsystem is already configured.
    g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  }

  Serial.print("BLE init: ");
  Serial.println(ok ? "OK" : "FAIL");
  Serial.print("antenna=");
  Serial.println((BoardControl::antennaPath() == BoardAntennaPath::kExternal) ?
                 "external" : "ceramic");
  g_initOk = ok;
  g_lastTestTime = millis();
}

void loop() {
  const uint32_t now = millis();

  if (g_ble.isConnected()) {
    if (!g_wasConnected) {
      Serial.println("\r\n*** CONNECTED ***");
      g_wasConnected = true;
      // Background GRTC service handles ATT/SMP even when the main loop is
      // briefly delayed. Must be called after connect so the connection handle
      // is valid. sendSmpSecurityRequest() asks Android to initiate pairing so
      // it encrypts the link before writing the CCCD (required by NUS apps).
      g_ble.setBackgroundConnectionServiceEnabled(true);
      g_ble.sendSmpSecurityRequest();
    }

    // Service connection events
    BleConnectionEvent evt{};
    const bool eventProcessed = g_ble.pollConnectionEvent(&evt, 450000UL);

    // Guard against a stale terminateInd from the previous connection that the
    // HAL event queue may return on the first poll of a new connection.
    if (evt.terminateInd && g_ble.isConnected()) {
      evt.terminateInd = false;
    }

    // Service NUS (handles notifications)
    g_nus.service(&evt);

    if (eventProcessed && evt.eventStarted) {
      // Check if we just completed a notification
      if (!g_nus.hasPendingTx() && g_notificationsQueued > g_notificationsCompleted) {
        g_notificationsCompleted = g_notificationsQueued;
      }

      // Process incoming data
      while (g_nus.available() > 0) {
        const int c = g_nus.read();
        if (c >= 0) {
          g_packetsReceived++;
          // Echo back with ACK
          const char ack[] = "ACK";
          g_nus.write(reinterpret_cast<const uint8_t*>(ack), sizeof(ack) - 1);
        }
      }

      // Update LED based on connection state
      Gpio::write(kPinUserLed, ((now / 500) % 2) == 0);
    }

    // Send test data periodically
    if ((now - g_lastTestTime) >= kTestIntervalMs) {
      g_lastTestTime = now;

      // Send a test packet
      char testMsg[64];
      const int len = snprintf(testMsg, sizeof(testMsg),
                               "Test %lu t=%lu\r\n",
                               static_cast<unsigned long>(g_packetsSent),
                               static_cast<unsigned long>(now));

      if (len > 0 && len < static_cast<int>(sizeof(testMsg))) {
        const size_t written = g_nus.write(reinterpret_cast<const uint8_t*>(testMsg), len);
        if (written > 0) {
          g_packetsSent++;
          g_notificationsQueued++;
        }
      }

      // Print statistics
      printStats();
    }

    // Handle disconnect
    if (evt.terminateInd) {
      Serial.println("\r\n*** DISCONNECTED ***");
      g_wasConnected = false;
    }
  } else {
    // Not connected - advertise
    if (g_wasConnected) {
      Serial.println("\r\n*** DISCONNECTED (advertising) ***");
      g_wasConnected = false;
      g_nus.service(nullptr);  // Clear state
    }

    BleAdvInteraction interaction{};
    g_ble.advertiseInteractEvent(&interaction, 350U, 350000UL, 700000UL);

    if (interaction.receivedConnectInd) {
      // Connection initiated - will be handled in next iteration
    }

    Gpio::write(kPinUserLed, ((now / 200) % 2) == 0);
    delay(100);
  }
}

void printStats() {
  Serial.print("Stats: sent=");
  Serial.print(g_packetsSent);
  Serial.print(" recv=");
  Serial.print(g_packetsReceived);
  Serial.print(" queued=");
  Serial.print(g_notificationsQueued);
  Serial.print(" completed=");
  Serial.print(g_notificationsCompleted);
  Serial.print(" pending=");
  Serial.print(g_nus.hasPendingTx() ? "1" : "0");
  Serial.print(" notify=");
  Serial.print(g_nus.isNotifyEnabled() ? "1" : "0");
  Serial.print(" dropped_tx=");
  Serial.print(g_nus.txDroppedBytes());
  Serial.print(" dropped_rx=");
  Serial.println(g_nus.rxDroppedBytes());
}
