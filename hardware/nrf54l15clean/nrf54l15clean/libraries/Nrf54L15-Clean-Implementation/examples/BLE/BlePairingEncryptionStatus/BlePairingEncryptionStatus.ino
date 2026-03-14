#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;
static bool g_bleReady = false;
static constexpr bool kEnableBleTraceLogging = false;
static constexpr bool kLogEveryConnectionEvent = false;

static bool g_prevConnected = false;
static bool g_prevEncrypted = false;
static bool g_connectionAnnounced = false;
static uint32_t g_lastAdvLogMs = 0U;
static uint32_t g_lastInitErrorLogMs = 0U;
static constexpr int8_t kTxPowerDbm = 0;

static void printHexBytes(const uint8_t* data, size_t len) {
  if (data == nullptr) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    if (data[i] < 16U) {
      Serial.print('0');
    }
    Serial.print(data[i], HEX);
  }
}

static void printEncDebug() {
  BleEncryptionDebugCounters dbg{};
  g_ble.getEncryptionDebugCounters(&dbg);
  Serial.print("enc_dbg followup_armed=");
  Serial.print(dbg.followupArmed);
  Serial.print(" end_seen=");
  Serial.print(dbg.followupEndSeen);
  Serial.print(" crc_ok=");
  Serial.print(dbg.followupCrcOk);
  Serial.print(" start_req=");
  Serial.print(dbg.followupStartEncReqSeen);
  Serial.print(" start_rsp_tx_ok=");
  Serial.print(dbg.followupStartEncRspTxOk);
  Serial.print(" rx_llid1=");
  Serial.print(dbg.followupRxLlid1);
  Serial.print(" rx_llid2=");
  Serial.print(dbg.followupRxLlid2);
  Serial.print(" rx_llid3=");
  Serial.print(dbg.followupRxLlid3);
  Serial.print(" last_hdr=0x");
  if (dbg.lastFollowHdr < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.lastFollowHdr, HEX);
  Serial.print(" last_llid=");
  Serial.print(dbg.lastFollowLlid);
  Serial.print(" last_len=");
  Serial.print(dbg.lastFollowLen);
  Serial.print(" last_b0=0x");
  if (dbg.lastFollowByte0 < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.lastFollowByte0, HEX);

  Serial.print(" main_enc_req=");
  Serial.print(dbg.mainEncReqSeen);
  Serial.print(" main_enc_rsp_tx_ok=");
  Serial.print(dbg.mainEncRspTxOk);
  Serial.print(" main_start_req=");
  Serial.print(dbg.mainStartEncReqSeen);
  Serial.print(" main_start_req_dec=");
  Serial.print(dbg.mainStartEncReqSeenDecrypted);
  Serial.print(" main_start_rsp_tx_ok=");
  Serial.print(dbg.mainStartEncRspTxOk);

  Serial.print(" pend_ctrl_rx=");
  Serial.print(dbg.startPendingControlRxSeen);
  Serial.print(" pend_last_hdr=0x");
  if (dbg.startPendingLastHdr < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.startPendingLastHdr, HEX);
  Serial.print(" pend_last_len_raw=");
  Serial.print(dbg.startPendingLastLenRaw);
  Serial.print(" pend_last_b0=0x");
  if (dbg.startPendingLastByte0 < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.startPendingLastByte0, HEX);
  Serial.print(" pend_last_dec=");
  Serial.print(dbg.startPendingLastDecrypted);
  Serial.print(" enc_rsp_txen_lag_last_us=");
  Serial.print(dbg.encRspTxenLagLastUs);
  Serial.print(" enc_rsp_txen_lag_max_us=");
  Serial.print(dbg.encRspTxenLagMaxUs);
  Serial.print(" mic_fail=");
  Serial.print(dbg.encRxMicFailCount);
  Serial.print(" short_pdu=");
  Serial.print(dbg.encRxShortPduCount);
  Serial.print(" mic_ctr_lo=");
  Serial.print(dbg.encRxLastMicFailCounterLo);
  Serial.print(" mic_hdr=0x");
  if (dbg.encRxLastMicFailHdr < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.encRxLastMicFailHdr, HEX);
  Serial.print(" mic_len_raw=");
  Serial.print(dbg.encRxLastMicFailLenRaw);
  Serial.print(" mic_dir=");
  Serial.print(dbg.encRxLastMicFailDir);
  Serial.print(" mic_state=0x");
  if (dbg.encRxLastMicFailState < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.encRxLastMicFailState, HEX);
  Serial.print(" mic_b=0x");
  if (dbg.encRxLastMicFailData0 < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.encRxLastMicFailData0, HEX);
  Serial.print(",");
  if (dbg.encRxLastMicFailData1 < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.encRxLastMicFailData1, HEX);
  Serial.print(",");
  if (dbg.encRxLastMicFailData2 < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.encRxLastMicFailData2, HEX);
  Serial.print(",");
  if (dbg.encRxLastMicFailData3 < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.encRxLastMicFailData3, HEX);
  Serial.print(",");
  if (dbg.encRxLastMicFailData4 < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.encRxLastMicFailData4, HEX);
  Serial.print(" start_rsp_rx=");
  Serial.print(dbg.encStartRspRxCount);
  Serial.print(" start_rsp_raw_len=");
  Serial.print(dbg.encStartRspLastRawLen);
  Serial.print(" start_rsp_dec=");
  Serial.print(dbg.encStartRspLastDecrypted);
  Serial.print(" start_rsp_hdr=0x");
  if (dbg.encStartRspLastHdr < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.encStartRspLastHdr, HEX);
  Serial.print(" pause_req=");
  Serial.print(dbg.encPauseReqAcceptedCount);
  Serial.print(" pause_rsp=");
  Serial.print(dbg.encPauseRspRxCount);
  Serial.print(" enc_clear=");
  Serial.print(dbg.encClearCount);
  Serial.print(" clear_reason=0x");
  if (dbg.encLastClearReason < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.encLastClearReason, HEX);
  Serial.print(" tx_lag_last=");
  Serial.print(dbg.txenLagLastUs);
  Serial.print(" tx_lag_max=");
  Serial.print(dbg.txenLagMaxUs);
  Serial.print(" enc_tx_cnt=");
  Serial.print(dbg.encTxPacketCount);
  Serial.print(" enc_tx_lag_last=");
  Serial.print(dbg.encTxenLagLastUs);
  Serial.print(" enc_tx_lag_max=");
  Serial.print(dbg.encTxenLagMaxUs);
  Serial.print(" tx_ctr_lo=");
  Serial.print(dbg.encLastTxCounterLo);
  Serial.print(" tx_hdr=0x");
  if (dbg.encLastTxHdr < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.encLastTxHdr, HEX);
  Serial.print(" tx_plen=");
  Serial.print(dbg.encLastTxPlainLen);
  Serial.print(" tx_alen=");
  Serial.print(dbg.encLastTxAirLen);
  Serial.print(" tx_fresh=");
  Serial.print(dbg.encLastTxWasFresh);
  Serial.print(" tx_enc=");
  Serial.print(dbg.encLastTxWasEncrypted);
  Serial.print(" rx_ctr_lo=");
  Serial.print(dbg.encLastRxCounterLo);
  Serial.print(" rx_hdr=0x");
  if (dbg.encLastRxHdr < 16U) {
    Serial.print('0');
  }
  Serial.print(dbg.encLastRxHdr, HEX);
  Serial.print(" rx_len_raw=");
  Serial.print(dbg.encLastRxLenRaw);
  Serial.print(" rx_new=");
  Serial.print(dbg.encLastRxWasNew);
  Serial.print(" rx_dec=");
  Serial.print(dbg.encLastRxWasDecrypted);
  Serial.print(" skdm=");
  printHexBytes(dbg.encLastSkdm, sizeof(dbg.encLastSkdm));
  Serial.print(" ivm=");
  printHexBytes(dbg.encLastIvm, sizeof(dbg.encLastIvm));
  Serial.print(" skds=");
  printHexBytes(dbg.encLastSkds, sizeof(dbg.encLastSkds));
  Serial.print(" ivs=");
  printHexBytes(dbg.encLastIvs, sizeof(dbg.encLastIvs));
  Serial.print(" sk=");
  printHexBytes(dbg.encLastSessionKey, sizeof(dbg.encLastSessionKey));
  Serial.print(" sk_alt=");
  printHexBytes(dbg.encLastSessionAltKey, sizeof(dbg.encLastSessionAltKey));
  Serial.print(" sk_ok=");
  Serial.print(dbg.encLastSessionKeyValid);
  Serial.print(" sk_alt_ok=");
  Serial.print(dbg.encLastSessionAltKeyValid);
  Serial.print(" rx_dir=");
  Serial.print(dbg.encLastRxDir);
  Serial.print(" tx_dir=");
  Serial.print(dbg.encLastTxDir);
  Serial.print("\r\n");
}

static void onBleTrace(const char* message, void* context) {
  (void)context;
  if (message == nullptr) {
    return;
  }
  Serial.print("[trace] ");
  Serial.print(message);
  Serial.print("\r\n");
}

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

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nBlePairingEncryptionStatus start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);
  if (kEnableBleTraceLogging) {
    g_ble.setTraceCallback(onBleTrace, nullptr);
  } else {
    g_ble.setTraceCallback(nullptr, nullptr);
  }

  static const uint8_t kAddress[6] = {0x51, 0x00, 0x15, 0x54, 0xDE, 0xC0};
  bool ok = g_ble.begin(kTxPowerDbm);
  if (!ok) {
    Serial.print("BLE step failed: begin\r\n");
  }
  if (ok && !g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic)) {
    ok = false;
    Serial.print("BLE step failed: setDeviceAddress\r\n");
  }
  if (ok && !g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd)) {
    ok = false;
    Serial.print("BLE step failed: setAdvertisingPduType\r\n");
  }
  if (ok && !g_ble.setAdvertisingName("X54-PAIR", true)) {
    ok = false;
    Serial.print("BLE step failed: setAdvertisingName\r\n");
  }
  if (ok && !g_ble.setScanResponseName("X54-PAIR-SCAN")) {
    ok = false;
    Serial.print("BLE step failed: setScanResponseName\r\n");
  }
  if (ok && !g_ble.setGattDeviceName("X54 Pairing Probe")) {
    ok = false;
    Serial.print("BLE step failed: setGattDeviceName\r\n");
  }
  if (ok && !g_ble.setGattBatteryLevel(96U)) {
    ok = false;
    Serial.print("BLE step failed: setGattBatteryLevel\r\n");
  }
  g_bleReady = ok;
  if (g_bleReady) {
    g_power.setLatencyMode(PowerLatencyMode::kConstantLatency);
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  Serial.print("Pair from phone and watch encryption state transitions.\r\n");
}

void loop() {
  if (!g_bleReady) {
    const uint32_t now = millis();
    if ((now - g_lastInitErrorLogMs) >= 2000UL) {
      g_lastInitErrorLogMs = now;
      Serial.print("BLE not ready; skipping advertise/poll\r\n");
    }
    delay(10);
    return;
  }

  if (!g_ble.isConnected()) {
    if (g_prevConnected) {
      g_prevConnected = false;
      g_prevEncrypted = false;
      g_connectionAnnounced = false;
      Serial.print("disconnected\r\n");
      printEncDebug();
    }

    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    if (adv.receivedConnectInd) {
      g_prevConnected = true;
      g_connectionAnnounced = false;
    } else {
      const uint32_t now = millis();
      if ((now - g_lastAdvLogMs) >= 2000UL) {
        g_lastAdvLogMs = now;
        Serial.print("advertising\r\n");
      }
    }

    Gpio::write(kPinUserLed, true);
    delay(1);
    return;
  }

  BleConnectionEvent evt{};
  const bool ran = g_ble.pollConnectionEvent(&evt, 450000UL);
  if (!g_connectionAnnounced && ran && evt.eventStarted) {
    BleConnectionInfo info{};
    if (g_ble.getConnectionInfo(&info)) {
      Serial.print("connected peer=");
      printAddress(info.peerAddress);
      Serial.print(" int=");
      Serial.print(info.intervalUnits);
      Serial.print("\r\n");
    } else {
      Serial.print("connected\r\n");
    }
    g_ble.clearEncryptionDebugCounters();
    g_connectionAnnounced = true;
  }
  const bool encrypted = g_ble.isConnectionEncrypted();
  if (encrypted != g_prevEncrypted) {
    g_prevEncrypted = encrypted;
    Serial.print("encryption=");
    Serial.print(encrypted ? "ON" : "OFF");
    if (ran && evt.eventStarted) {
      Serial.print(" ce=");
      Serial.print(evt.eventCounter);
    }
    Serial.print("\r\n");
  }

  if (kLogEveryConnectionEvent && ran && evt.eventStarted) {
    Serial.print("ce=");
    Serial.print(evt.eventCounter);
    Serial.print(" rx=");
    Serial.print(evt.packetReceived ? 1 : 0);
    Serial.print(" crc=");
    Serial.print(evt.crcOk ? 1 : 0);
    Serial.print(" new=");
    Serial.print(evt.packetIsNew ? 1 : 0);
    Serial.print(" sn=");
    Serial.print(evt.rxSn);
    Serial.print(" nesn=");
    Serial.print(evt.rxNesn);
    Serial.print(" ack=");
    Serial.print(evt.peerAckedLastTx ? 1 : 0);
    Serial.print(" fresh=");
    Serial.print(evt.freshTxAllowed ? 1 : 0);
    Serial.print(" iack=");
    Serial.print(evt.implicitEmptyAck ? 1 : 0);
    Serial.print(" llid=");
    Serial.print(evt.llid);
    Serial.print(" len=");
    Serial.print(evt.payloadLength);
    Serial.print(" tx=");
    Serial.print(evt.txPacketSent ? 1 : 0);
    Serial.print(" txllid=");
    Serial.print(evt.txLlid);
    Serial.print(" txsn=");
    Serial.print(evt.txSn);
    Serial.print(" txnesn=");
    Serial.print(evt.txNesn);
    Serial.print(" txlen=");
    Serial.print(evt.txPayloadLength);
    if (evt.llControlPacket) {
      Serial.print(" rxop=0x");
      if (evt.llControlOpcode < 16U) {
        Serial.print('0');
      }
      Serial.print(evt.llControlOpcode, HEX);
    }
    if ((evt.txLlid == 0x03U) &&
        (evt.txPayloadLength >= 1U) &&
        (evt.txPayload != nullptr)) {
      Serial.print(" txop=0x");
      if (evt.txPayload[0] < 16U) {
        Serial.print('0');
      }
      Serial.print(evt.txPayload[0], HEX);
      const uint8_t txDump = (evt.txPayloadLength < 4U) ? evt.txPayloadLength : 4U;
      if (txDump > 1U) {
        Serial.print(" txb=");
        for (uint8_t i = 1U; i < txDump; ++i) {
          if (evt.txPayload[i] < 16U) {
            Serial.print('0');
          }
          Serial.print(evt.txPayload[i], HEX);
        }
      }
    }
    Serial.print(" term=");
    Serial.print(evt.terminateInd ? 1 : 0);
    Serial.print("\r\n");
  }

  if (ran && evt.terminateInd) {
    Serial.print("link terminated\r\n");
    printEncDebug();
    g_prevConnected = false;
    g_prevEncrypted = false;
    g_connectionAnnounced = false;
  }

  Gpio::write(kPinUserLed, encrypted ? false : true);
  if (!ran) {
    delay(1);
  }
}
