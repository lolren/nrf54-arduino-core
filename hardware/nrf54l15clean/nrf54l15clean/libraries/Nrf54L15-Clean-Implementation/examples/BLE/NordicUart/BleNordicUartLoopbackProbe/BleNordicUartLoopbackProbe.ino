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
 *   2. Optionally sends an SMP Security Request so the central can pair/encrypt.
 *   3. Sends a welcome banner once the NUS TX CCCD is enabled.
 *   4. Echoes received bytes back at up to one full notification per connection
 *      event, with a high-water check to prevent txBuffer overflow.
 *
 * Gotcha: peripheral-initiated SMP Security Request is intentionally disabled
 * by default here. Android NUS apps are primarily interested in getting the
 * TX CCCD enabled quickly; forcing a bond request during early discovery can
 * stall or time out CCCD writes. Use BlePairingEncryptionStatus.ino when you
 * want to validate pairing/encryption, or opt in below for loopback-specific
 * security testing.
 *
 * Back-pressure design: pumpLoopback() only reads from rxBuffer when txBuffer
 * holds fewer than 2 × kPollBudgetBytes (40 bytes). The threshold is a fixed
 * compile-time constant so it is not affected by ATT MTU negotiation; using
 * maxPayloadLength() instead would inflate the threshold to 244 after an MTU
 * exchange and lock reads permanently once txBuffer hit 242 bytes. Bytes not
 * yet read stay in rxBuffer (1 KB) until the pipeline clears.
 *
 * Default runtime is intentionally quiet while connected. Verbose periodic
 * status is available for debugging, but printing to USB CDC during an active
 * BLE session can delay connection-event polling enough to hurt Android
 * service discovery and CCCD writes.
 */

#include <Arduino.h>

#include "ble_nus.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static BleNordicUart g_nus(g_ble);
static PowerManager g_power;

static bool g_wasConnected = false;
static bool g_bannerSent = false;  // True once welcome banner has been sent.
static bool g_securityRequested = false;
static uint32_t g_lastStatusMs = 0U;
static uint32_t g_connectedAtMs = 0U;
static uint32_t g_echoedBytes = 0U;   // Bytes echoed this connection.
static uint32_t g_droppedBytes = 0U;  // Bytes dropped this connection (TX full).
volatile uint32_t g_connectCount = 0U;
volatile uint32_t g_disconnectCount = 0U;
volatile uint32_t g_pollEventCount = 0U;
volatile uint32_t g_pollNoEventCount = 0U;
volatile uint16_t g_lastEventCounter = 0U;
volatile uint8_t g_lastDataChannel = 0U;
volatile uint8_t g_lastDisconnectReason = 0U;
volatile uint8_t g_lastDisconnectReasonRemote = 0U;
volatile uint8_t g_initOk = 0U;
volatile uint8_t g_setupStage = 0U;
volatile uint8_t g_runtimeAddress[6] = {0U};
volatile uint8_t g_runtimeAddressType = 0xFFU;
volatile uint32_t g_unconnectedLoopCount = 0U;
volatile uint32_t g_advertiseCallCount = 0U;
volatile uint32_t g_advertiseOkCount = 0U;
volatile uint32_t g_advertiseFailCount = 0U;
volatile uint8_t g_lastAdvChannel = 0U;
volatile uint8_t g_lastAdvSawConnectInd = 0U;
volatile uint8_t g_lastAdvConnectIndChSel2 = 0U;
volatile uint8_t g_lastAdvSawScanReq = 0U;
volatile uint32_t g_advConnectIndCount = 0U;
volatile uint32_t g_advConnectedAfterEventCount = 0U;
volatile uint32_t g_advConnectIndLostBeforeLoopCount = 0U;
volatile uint8_t g_lastPostAdvConnected = 0U;
volatile uint8_t g_lastConnPeerAddress[6] = {0U};
volatile uint8_t g_lastConnPeerAddressRandom = 0U;
volatile uint32_t g_lastConnAccessAddress = 0U;
volatile uint32_t g_lastConnCrcInit = 0U;
volatile uint16_t g_lastConnIntervalUnits = 0U;
volatile uint16_t g_lastConnLatency = 0U;
volatile uint16_t g_lastConnTimeoutUnits = 0U;
volatile uint8_t g_lastConnChannelMap[5] = {0U};
volatile uint8_t g_lastConnChannelCount = 0U;
volatile uint8_t g_lastConnHopIncrement = 0U;
volatile uint8_t g_lastConnSleepClockAccuracy = 0U;
volatile uint32_t g_lastDisconnectSequence = 0U;
volatile uint32_t g_lastDisconnectNextEventUs = 0U;
volatile uint16_t g_lastDisconnectEventCounterDebug = 0U;
volatile uint16_t g_lastDisconnectMissedEventCount = 0U;
volatile uint8_t g_lastDisconnectRole = 0U;
volatile uint8_t g_lastDisconnectErrorCode = 0U;
volatile uint8_t g_lastDisconnectExpectedRxSn = 0U;
volatile uint8_t g_lastDisconnectTxSn = 0U;
volatile uint8_t g_lastDisconnectPendingTxValid = 0U;
volatile uint8_t g_lastDisconnectPendingTxLlid = 0U;
volatile uint8_t g_lastDisconnectPendingTxLength = 0U;
volatile uint8_t g_lastDisconnectLastTxOpcode = 0U;
volatile uint8_t g_lastDisconnectLastTxLength = 0U;
volatile uint8_t g_lastDisconnectLastRxOpcode = 0U;
volatile uint8_t g_lastDisconnectLastRxLength = 0U;
volatile uint8_t g_lastDisconnectValid = 0U;
volatile uint32_t g_dbgLatePollCount = 0U;
volatile uint32_t g_dbgConnRxTimeoutCount = 0U;
volatile uint32_t g_dbgConnMissedEventCountLast = 0U;
volatile uint32_t g_dbgConnMissedEventCountMax = 0U;

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
#ifndef NRF54L15_LOOPBACK_BG_SERVICE
#define NRF54L15_LOOPBACK_BG_SERVICE 1
#endif
static constexpr bool kEnableBleBgService =
    (NRF54L15_LOOPBACK_BG_SERVICE != 0);
static constexpr uint32_t kStatusPeriodMs = 2000UL;
#ifndef NRF54L15_LOOPBACK_VERBOSE_STATUS
#define NRF54L15_LOOPBACK_VERBOSE_STATUS 0
#endif
static constexpr bool kVerboseStatus =
    (NRF54L15_LOOPBACK_VERBOSE_STATUS != 0);
static constexpr uint32_t kConnectionPollTimeoutUs = 2000UL;
static constexpr uint32_t kConnectionWarmupMs = 500UL;
#ifndef NRF54L15_LOOPBACK_REQUEST_SECURITY
#define NRF54L15_LOOPBACK_REQUEST_SECURITY 0
#endif
static constexpr bool kRequestLinkSecurity =
    (NRF54L15_LOOPBACK_REQUEST_SECURITY != 0);
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

static void snapshotConnectionInfo() {
  BleConnectionInfo info{};
  if (!g_ble.getConnectionInfo(&info)) {
    return;
  }
  memcpy(const_cast<uint8_t*>(g_lastConnPeerAddress), info.peerAddress,
         sizeof(g_lastConnPeerAddress));
  g_lastConnPeerAddressRandom = info.peerAddressRandom ? 1U : 0U;
  g_lastConnAccessAddress = info.accessAddress;
  g_lastConnCrcInit = info.crcInit;
  g_lastConnIntervalUnits = info.intervalUnits;
  g_lastConnLatency = info.latency;
  g_lastConnTimeoutUnits = info.supervisionTimeoutUnits;
  memcpy(const_cast<uint8_t*>(g_lastConnChannelMap), info.channelMap,
         sizeof(g_lastConnChannelMap));
  g_lastConnChannelCount = info.channelCount;
  g_lastConnHopIncrement = info.hopIncrement;
  g_lastConnSleepClockAccuracy = info.sleepClockAccuracy;
}

static void snapshotDisconnectDebug() {
  BleDisconnectDebug dbg{};
  if (!g_ble.getDisconnectDebug(&dbg) || dbg.valid == 0U) {
    return;
  }
  g_lastDisconnectSequence = dbg.sequence;
  g_lastDisconnectNextEventUs = dbg.nextEventUs;
  g_lastDisconnectEventCounterDebug = dbg.eventCounter;
  g_lastDisconnectMissedEventCount = dbg.missedEventCount;
  g_lastDisconnectReason = dbg.reason;
  g_lastDisconnectRole = dbg.role;
  g_lastDisconnectErrorCode = dbg.errorCode;
  g_lastDisconnectExpectedRxSn = dbg.expectedRxSn;
  g_lastDisconnectTxSn = dbg.txSn;
  g_lastDisconnectReasonRemote =
      (dbg.reason == static_cast<uint8_t>(BleDisconnectReason::kPeerTerminate))
          ? 1U
          : 0U;
  g_lastDisconnectPendingTxValid = dbg.pendingTxValid;
  g_lastDisconnectPendingTxLlid = dbg.pendingTxLlid;
  g_lastDisconnectPendingTxLength = dbg.pendingTxLength;
  g_lastDisconnectLastTxOpcode = dbg.lastTxOpcode;
  g_lastDisconnectLastTxLength = dbg.lastTxLength;
  g_lastDisconnectLastRxOpcode = dbg.lastRxOpcode;
  g_lastDisconnectLastRxLength = dbg.lastRxLength;
  g_lastDisconnectValid = dbg.valid;
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

static bool connectionWarmupElapsed(uint32_t nowMs) {
  return (nowMs - g_connectedAtMs) >= kConnectionWarmupMs;
}

static void maybeRequestLinkSecurity(uint32_t nowMs) {
  if (!kRequestLinkSecurity || g_securityRequested || !g_ble.isConnected()) {
    return;
  }
  if (!connectionWarmupElapsed(nowMs)) {
    return;
  }
  g_securityRequested = true;
  g_ble.sendSmpSecurityRequest();
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
  Serial.print(" bg_isr=");         Serial.print(dbg.connBgServiceIsrCount);
  Serial.print(" bg_thr=");         Serial.print(dbg.connBgServiceThreadCount);
  Serial.print(" bg_due=");         Serial.print(dbg.connBgDueCount);
  Serial.print(" bg_wake_max=");    Serial.print(dbg.connBgWakeLagMaxUs);
  Serial.print(" bg_xo_pre=");      Serial.print(dbg.connBgHfxoPrewarmArmCount);
  Serial.print(" bg_xo_stop=");     Serial.print(dbg.connBgHfxoStopCount);
  Serial.print(" bg_xo_sw=");       Serial.print(dbg.connBgHfxoFallbackStartCount);
  Serial.print(" bg_rx_hw=");       Serial.print(dbg.connBgRxHardwareArmCount);
  Serial.print(" bg_rx_sw=");       Serial.print(dbg.connBgRxHardwareFallbackCount);
  Serial.print(" fast_att=");       Serial.print(dbg.connFastAttReadTurnaroundCount);
  Serial.print(" fast_ll=");        Serial.print(dbg.connFastLlControlTurnaroundCount);
  Serial.print(" fast_sig=");       Serial.print(dbg.connFastSignalingTurnaroundCount);
  Serial.print(" aar_try=");        Serial.print(dbg.connAarResolveAttemptCount);
  Serial.print(" aar_ok=");         Serial.print(dbg.connAarResolveSuccessCount);
  Serial.print(" aar_fail=");       Serial.print(dbg.connAarResolveFailureCount);
  Serial.print(" late_poll=");      Serial.print(dbg.connLatePollCount);
  Serial.print("\r\n");
}

void setup() {
  g_setupStage = 1U;
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleNordicUartLoopbackProbe start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  g_setupStage = 2U;
  bool ok = BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
  if (ok) {
    g_setupStage = 3U;
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
    g_setupStage = 4U;
    g_power.setLatencyMode(PowerLatencyMode::kConstantLatency);
  }

  Serial.print("BLE NUS loopback init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (!ok) {
    g_initOk = 0U;
    while (true) {
      delay(1000);
    }
    return;
  }
  g_initOk = 1U;
  g_setupStage = 5U;

  uint8_t addr[6] = {0};
  BleAddressType type = BleAddressType::kPublic;
  if (g_ble.getDeviceAddress(addr, &type)) {
    memcpy(const_cast<uint8_t*>(g_runtimeAddress), addr, sizeof(g_runtimeAddress));
    g_runtimeAddressType = static_cast<uint8_t>(type);
    Serial.print("addr=");
    printAddress(addr);
    Serial.print(" type=");
    Serial.print((type == BleAddressType::kRandomStatic) ? "random" : "public");
    Serial.print("\r\n");
  }
  Serial.print("Advertised as X54-LB\r\n");
}

void loop() {
  BleEncryptionDebugCounters dbg{};
  g_ble.getEncryptionDebugCounters(&dbg);
  g_dbgLatePollCount = dbg.connLatePollCount;
  g_dbgConnRxTimeoutCount = dbg.connRxTimeoutCount;
  g_dbgConnMissedEventCountLast = dbg.connMissedEventCountLast;
  g_dbgConnMissedEventCountMax = dbg.connMissedEventCountMax;

  if (!g_ble.isConnected()) {
    snapshotDisconnectDebug();
    ++g_unconnectedLoopCount;
    BleAdvInteraction adv{};
    ++g_advertiseCallCount;
    if (g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL)) {
      ++g_advertiseOkCount;
    } else {
      ++g_advertiseFailCount;
    }
    g_lastAdvChannel = static_cast<uint8_t>(adv.channel);
    g_lastAdvSawConnectInd = adv.receivedConnectInd ? 1U : 0U;
    g_lastAdvConnectIndChSel2 = adv.connectIndChSel2 ? 1U : 0U;
    g_lastAdvSawScanReq = adv.receivedScanRequest ? 1U : 0U;
    if (adv.receivedConnectInd) {
      ++g_advConnectIndCount;
    }
    g_lastPostAdvConnected = g_ble.isConnected() ? 1U : 0U;
    if (g_lastPostAdvConnected != 0U) {
      ++g_advConnectedAfterEventCount;
      snapshotConnectionInfo();
      if (kEnableBleBgService) {
        g_ble.setBackgroundConnectionServiceEnabled(true);
      }
    } else if (adv.receivedConnectInd) {
      ++g_advConnectIndLostBeforeLoopCount;
    }
    g_nus.service();
    snapshotDisconnectDebug();

    if (g_wasConnected) {
      g_wasConnected = false;
      g_bannerSent = false;
      g_securityRequested = false;
      ++g_disconnectCount;
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
    g_securityRequested = false;
    g_connectedAtMs = millis();
    g_echoedBytes = 0U;
    g_droppedBytes = 0U;
    ++g_connectCount;
    snapshotConnectionInfo();
    Gpio::write(kPinUserLed, false);
    if (kEnableBleBgService) {
      g_ble.setBackgroundConnectionServiceEnabled(true);
    }
    Serial.print("BLE client connected\r\n");
  }

  BleConnectionEvent evt{};
  const bool eventStarted =
      g_ble.pollConnectionEvent(&evt, kConnectionPollTimeoutUs) && evt.eventStarted;

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
    ++g_pollNoEventCount;
    g_nus.service();
    const uint32_t nowMs = millis();
    maybeRequestLinkSecurity(nowMs);
    if (!connectionWarmupElapsed(nowMs)) {
      return;
    }
    queueBanner();
    pumpLoopback(2U);
    // terminateInd can arrive without a full event in the background-service
    // path; handle it here so the disconnect stats are never missed.
    if (evt.terminateInd) {
      snapshotDisconnectDebug();
      printTerminateStats(evt);
    }
    return;
  }

  ++g_pollEventCount;
  g_lastEventCounter = evt.eventCounter;
  g_lastDataChannel = evt.dataChannel;
  g_nus.service(&evt);
  const uint32_t nowMs = millis();
  maybeRequestLinkSecurity(nowMs);
  if (!connectionWarmupElapsed(nowMs)) {
    if (evt.terminateInd) {
      printTerminateStats(evt);
    }
    return;
  }
  queueBanner();
  pumpLoopback(kPollBudgetBytes);

  if (evt.terminateInd) {
    if (evt.disconnectReasonValid) {
      g_lastDisconnectReason = evt.disconnectReason;
      g_lastDisconnectReasonRemote = evt.disconnectReasonRemote ? 1U : 0U;
    }
    snapshotDisconnectDebug();
    printTerminateStats(evt);
  }

  if (kVerboseStatus && ((nowMs - g_lastStatusMs) >= kStatusPeriodMs)) {
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
