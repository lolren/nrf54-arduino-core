/*
 * BleNordicUartLoopbackProbe
 *
 * Echo server over the Nordic UART Service (NUS): every byte received from
 * the central is immediately sent back via NUS TX notification. Useful for:
 *   - Testing NUS throughput and reliability.
 *   - Verifying that a NUS central (app or board) can both send and receive.
 *   - Measuring BLE link drop rates by comparing sent vs. echoed byte counts.
 *
 * On connect, the sketch:
 *   1. Enables the background connection service for ATT/GATT handling.
 *   2. Sends an SMP Security Request so the central can pair/encrypt.
 *   3. Sends a welcome banner once the NUS TX CCCD is enabled.
 *   4. Echoes received bytes back at up to one full notification per connection
 *      event, with a high-water check to prevent txBuffer overflow.
 *
 * Gotcha: sendSmpSecurityRequest() is needed for Android to initiate pairing
 * automatically. Without it, Android may connect without ever encrypting,
 * and some NUS-capable apps will refuse to interact with an unencrypted link.
 *
 * Back-pressure design: pumpLoopback() only reads from rxBuffer when txBuffer
 * holds fewer than 2 × kPollBudgetBytes (40 bytes). The threshold is a fixed
 * compile-time constant so it is not affected by ATT MTU negotiation; using
 * maxPayloadLength() instead would inflate the threshold to 244 after an MTU
 * exchange and lock reads permanently once txBuffer hit 242 bytes. Bytes not
 * yet read stay in rxBuffer (1 KB) until the pipeline clears.
 *
 * Serial output (every 2 s): notify state, echoed byte count, drop count.
 */

#include <Arduino.h>

#include "ble_nus.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static BleNordicUart g_nus(g_ble);
static PowerManager g_power;

static bool g_wasConnected = false;
static bool g_bannerSent = false;  // True once welcome banner has been sent.
static uint32_t g_lastStatusMs = 0U;
static uint32_t g_echoedBytes = 0U;   // Bytes echoed this connection.
static uint32_t g_droppedBytes = 0U;  // Bytes dropped this connection (TX full).

// 0 dBm: good for general-purpose probe work within a few metres.
static constexpr int8_t kTxPowerDbm = 0;
// Max bytes to read from rxBuffer per pumpLoopback() call.
// 20 = standard NUS MTU payload (ATT MTU 23 - 3 header bytes); matches one
// full notification so each event sends a maximally-packed PDU.
static constexpr uint8_t kPollBudgetBytes = 20U;
// Stop reading into txBuffer once it holds more than 2 × kPollBudgetBytes.
// Using kPollBudgetBytes (not maxPayloadLength()) as the unit keeps the
// threshold stable regardless of whether Android negotiates a large ATT MTU
// mid-connection — a large MTU would otherwise inflate the threshold to 244
// bytes, allow ~780 bytes into txBuffer, then block all reads permanently.
static constexpr int kTxFreeMin =
    static_cast<int>(BleNordicUart::kTxBufferSize) -
    2 * static_cast<int>(kPollBudgetBytes);
static constexpr uint32_t kStatusPeriodMs = 2000UL;
// Unique address prevents Android GATT cache collisions across sketches.
static constexpr uint8_t kAddress[6] = {0x37, 0x00, 0x15, 0x54, 0xDE, 0xC0};
static constexpr char kGattName[] = "X54 NUS Loopback";
static constexpr char kBanner[] = "X54 NUS loopback ready\r\n";
static const uint8_t kNusAdvPayload[] = {
    2, 0x01, 0x06,
    7, 0x09, 'X', '5', '4', '-', 'L', 'B',
    17, 0x07,
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E,
};

static void printAddress(const uint8_t* addr) {
  if (addr == nullptr) {
    return;
  }
  for (int i = 5; i >= 0; --i) {
    if (addr[i] < 16U) {
      Serial.print('0');
    }
    Serial.print(addr[i], HEX);
    if (i > 0) {
      Serial.print(':');
    }
  }
}

static void queueBanner() {
  if (g_bannerSent || !g_nus.isNotifyEnabled()) {
    return;
  }
  if (g_nus.availableForWrite() < static_cast<int>(sizeof(kBanner) - 1U)) {
    return;
  }
  g_nus.write(reinterpret_cast<const uint8_t*>(kBanner), sizeof(kBanner) - 1U);
  g_bannerSent = true;
}

static void pumpLoopback(uint8_t maxBytes) {
  // High-water gate: stop reading from rxBuffer once txBuffer holds more than
  // 2 × kPollBudgetBytes (40 bytes). kTxFreeMin is a fixed constant derived
  // from kPollBudgetBytes, not from the runtime maxPayloadLength(), so the
  // threshold is immune to ATT MTU negotiation enlarging maxPayloadLength()
  // to 244 bytes (which would have blocked all reads once txFree hit 242).
  // Bytes not yet read stay in rxBuffer (1 KB) and are retried once ACKs
  // drain the queued notifications.
  if (g_nus.availableForWrite() < kTxFreeMin || g_nus.available() <= 0) {
    return;
  }

  uint8_t buf[kPollBudgetBytes];
  uint8_t count = 0U;
  while (count < maxBytes && g_nus.available() > 0) {
    const int ch = g_nus.read();
    if (ch < 0) {
      break;
    }
    buf[count++] = static_cast<uint8_t>(ch);
  }
  if (count == 0U) {
    return;
  }
  const size_t written = g_nus.write(buf, count);
  g_echoedBytes += static_cast<uint32_t>(written);
  if (written < count) {
    g_droppedBytes += static_cast<uint32_t>(count - written);
  }
}

static void printTerminateStats(const BleConnectionEvent& evt) {
  Serial.print("[LB] Link terminated reason=0x");
  if (evt.disconnectReasonValid) {
    if (evt.disconnectReason < 0x10U) { Serial.print('0'); }
    Serial.print(evt.disconnectReason, HEX);
    Serial.print(evt.disconnectReasonRemote ? " (remote)" : " (local)");
  } else {
    Serial.print("??");
  }
  Serial.print(" echoed=");
  Serial.print(g_echoedBytes);
  Serial.print(" dropped=");
  Serial.print(g_droppedBytes);
  Serial.print(" nus_rx_drop=");
  Serial.print(g_nus.rxDroppedBytes());
  Serial.print(" nus_tx_drop=");
  Serial.print(g_nus.txDroppedBytes());
  Serial.print("\r\n");

  BleEncryptionDebugCounters dbg{};
  g_ble.getEncryptionDebugCounters(&dbg);
  Serial.print("[LB] mic_fail=");   Serial.print(dbg.encRxMicFailCount);
  Serial.print(" rx_to=");          Serial.print(dbg.connRxTimeoutCount);
  Serial.print(" tx_to=");          Serial.print(dbg.connTxTimeoutCount);
  Serial.print(" missed_last=");    Serial.print(dbg.connMissedEventCountLast);
  Serial.print(" missed_max=");     Serial.print(dbg.connMissedEventCountMax);
  Serial.print(" tx_lag_max=");     Serial.print(dbg.txenLagMaxUs);
  // cb_drop: deferred GATT write callbacks dropped when the 4-slot ISR queue
  //   overflowed (LL ACKed the packet, but the app callback was silently lost).
  // evt_overwrite: background connection events dropped when the 8-slot queue
  //   was full (main loop too slow to drain); each lost event may have carried
  //   freshTxAllowed=1, which can temporarily stall txNotificationInFlight_.
  Serial.print(" cb_drop=");        Serial.print(dbg.connDeferredCallbackDropCount);
  Serial.print(" evt_overwrite=");  Serial.print(dbg.connDeferredEventOverwriteCount);
  Serial.print("\r\n");
}

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleNordicUartLoopbackProbe start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  bool ok = BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
  if (ok) {
    ok = g_ble.begin(kTxPowerDbm) &&
         g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingData(kNusAdvPayload, sizeof(kNusAdvPayload)) &&
         g_ble.setScanResponseData(nullptr, 0U) &&
         g_ble.setGattDeviceName(kGattName) &&
         g_ble.clearCustomGatt() &&
         g_nus.begin();
  }
  if (ok) {
    g_power.setLatencyMode(PowerLatencyMode::kConstantLatency);
  }

  Serial.print("BLE NUS loopback init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (!ok) {
    while (true) {
      delay(1000);
    }
    return;
  }

  uint8_t addr[6] = {0};
  BleAddressType type = BleAddressType::kPublic;
  if (g_ble.getDeviceAddress(addr, &type)) {
    Serial.print("addr=");
    printAddress(addr);
    Serial.print(" type=");
    Serial.print((type == BleAddressType::kRandomStatic) ? "random" : "public");
    Serial.print("\r\n");
  }
  Serial.print("Advertised as X54-LB\r\n");
}

void loop() {
  if (!g_ble.isConnected()) {
    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    g_nus.service();

    if (g_wasConnected) {
      g_wasConnected = false;
      g_bannerSent = false;
      Gpio::write(kPinUserLed, true);
      Serial.print("BLE client disconnected\r\n");
    }

    if (!g_ble.isConnected()) {
      delay(20);
    }
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    g_bannerSent = false;
    g_echoedBytes = 0U;
    g_droppedBytes = 0U;
    Gpio::write(kPinUserLed, false);
    g_ble.setBackgroundConnectionServiceEnabled(true);
    g_ble.sendSmpSecurityRequest();
    Serial.print("BLE client connected\r\n");
  }

  BleConnectionEvent evt{};
  const bool eventStarted =
      g_ble.pollConnectionEvent(&evt, 300000UL) && evt.eventStarted;

  // The HAL deferred event queue is not flushed on disconnect. The first
  // pollConnectionEvent() of a new connection may return the previous
  // connection's terminateInd=true. Passing that stale event to
  // g_nus.service() would reset the NUS CCCD state, forcing Android to
  // re-discover and re-write it — causing the "serial not connected" symptom.
  // Guard: if terminateInd arrives while isConnected() is true, it is stale.
  if (evt.terminateInd && g_ble.isConnected()) {
    evt.terminateInd = false;
  }

  if (!eventStarted) {
    g_nus.service();
    queueBanner();
    pumpLoopback(2U);
    // terminateInd can arrive without a full event in the background-service
    // path; handle it here so the disconnect stats are never missed.
    if (evt.terminateInd) {
      printTerminateStats(evt);
    }
    return;
  }

  g_nus.service(&evt);
  queueBanner();
  pumpLoopback(kPollBudgetBytes);

  if (evt.terminateInd) {
    printTerminateStats(evt);
  }

  const uint32_t nowMs = millis();
  if ((nowMs - g_lastStatusMs) >= kStatusPeriodMs) {
    g_lastStatusMs = nowMs;
    Serial.print("notify=");
    Serial.print(g_nus.isNotifyEnabled() ? "on" : "off");
    Serial.print(" rx_pending=");
    Serial.print(g_nus.available());
    Serial.print(" tx_pending=");
    Serial.print(g_nus.availableForWrite());
    Serial.print(" echoed=");
    Serial.print(g_echoedBytes);
    Serial.print(" dropped=");
    Serial.print(g_droppedBytes);
    Serial.print("\r\n");
  }
}
