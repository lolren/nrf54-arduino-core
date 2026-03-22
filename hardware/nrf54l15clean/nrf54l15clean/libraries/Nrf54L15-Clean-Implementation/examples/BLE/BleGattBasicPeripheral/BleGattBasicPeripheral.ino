/*
 * BleGattBasicPeripheral
 *
 * Minimal connectable BLE peripheral exposing two standard GATT services:
 *   - Generic Access (Device Name)
 *   - Battery Service (Battery Level, auto-decrements every 30 s as a demo)
 *
 * Useful as a baseline to verify that a central can discover services, read
 * characteristics, and write CCCDs — without any custom GATT or pairing logic
 * getting in the way.
 *
 * When kLogBatteryReadDebug = true, every Device Name read, service browse,
 * and Battery/Service-Changed CCCD write is printed with full per-event
 * BLE link-layer details (SN, NESN, fresh flag, etc.). Handy for tracing
 * Android GATT cache behaviour or timing regressions.
 *
 * When kLogConnectionPackets = true, every received ATT or LL-Control PDU
 * is also printed — very verbose, useful for low-level debugging.
 *
 * After disconnect, detailed channel-map and encryption counters are printed
 * to help diagnose any channel-map update or decryption issues.
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static uint8_t g_batteryLevel = 100U;
static uint32_t g_lastBatteryUpdateMs = 0U;
static uint32_t g_lastLogMs = 0U;
static bool g_wasConnected = false;
// -8 dBm: reduced range, suitable for bench testing without flooding the room.
// Valid range: -40 to +8 dBm (hardware-dependent; 0 is standard default).
static constexpr int8_t kTxPowerDbm = -8;
// Set true to log every received ATT or LL-Control PDU — very verbose.
static constexpr bool kLogConnectionPackets = false;
// Set true to print per-event detail for Device Name reads, service browse,
// and Battery/Service-Changed CCCD writes. Useful for GATT cache debugging.
static constexpr bool kLogBatteryReadDebug = true;

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleGattBasicPeripheral start\r\n");

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  // Unique address avoids Android reusing a stale GATT cache from other sketches.
  static const uint8_t kAddress[6] = {0x31, 0x00, 0x15, 0x54, 0xDE, 0xC0};
  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         // CSA#2 allows the central to use channel selection algorithm 2 for
         // better coexistence; no downside, leave this on for modern centrals.
         g_ble.setAdvertisingChannelSelectionAlgorithm2(true) &&
         // kAdvInd: connectable and scannable undirected advertising (standard mode).
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         // Short name in every advertising packet (true = include AD Flags).
         g_ble.setAdvertisingName("X54-GATT", true) &&
         // Full name in scan response — returned only when the central actively scans.
         g_ble.setScanResponseName("X54-GATT-SCAN") &&
         // GATT Device Name characteristic (UUID 0x2A00) — readable by the central.
         g_ble.setGattDeviceName("XIAO nRF54L15 Clean") &&
         // Battery Service (UUID 0x180F) with Battery Level (0x2A19) set to 100 %.
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
    if (g_wasConnected) {
      g_wasConnected = false;
      BleEncryptionDebugCounters dbg{};
      uint8_t lastReason = 0U;
      bool lastRemote = false;
      g_ble.getEncryptionDebugCounters(&dbg);
      const bool haveLastReason =
          g_ble.getLastDisconnectReason(&lastReason, &lastRemote);
      Serial.print("disc reason_valid=");
      Serial.print(haveLastReason ? "1" : "0");
      Serial.print(" reason=0x");
      if (lastReason < 16U) {
        Serial.print('0');
      }
      Serial.print(lastReason, HEX);
      Serial.print(" remote=");
      Serial.print(lastRemote ? "1" : "0");
      Serial.print(" chm_rx_ce=");
      Serial.print(dbg.connLastChannelMapRxEventCounter);
      Serial.print(" chm_inst=");
      Serial.print(dbg.connLastChannelMapInstant);
      Serial.print(" chm_apply_ce=");
      Serial.print(dbg.connLastChannelMapAppliedEventCounter);
      Serial.print(" chm_apply_ch=");
      Serial.print(dbg.connLastChannelMapAppliedDataChannel);
      Serial.print(" chm_pend=");
      Serial.print(dbg.connChannelMapPendingCount);
      Serial.print(" chm_apply=");
      Serial.print(dbg.connChannelMapAppliedCount);
      Serial.print(" chm_dup=");
      Serial.print(dbg.connChannelMapDuplicateCount);
      Serial.print(" chm_to_after_apply=");
      Serial.print(dbg.connChannelMapTimeoutAfterApplyCount);
      Serial.print(" chm_to_ce=");
      Serial.print(dbg.connLastChannelMapTimeoutEventCounter);
      Serial.print(" rx_to=");
      Serial.print(dbg.connRxTimeoutCount);
      Serial.print(" rx_crc=");
      Serial.print(dbg.connRxCrcErrorCount);
      Serial.print(" iatt_ack=");
      Serial.print(dbg.connImplicitAttProgressAckCount);
      Serial.print(" bg_thr=");
      Serial.print(dbg.connBgServiceThreadCount);
      Serial.print(" bg_isr=");
      Serial.print(dbg.connBgServiceIsrCount);
      Serial.print(" bg_due=");
      Serial.print(dbg.connBgDueCount);
      Serial.print(" bg_lag_last=");
      Serial.print(dbg.connBgWakeLagLastUs);
      Serial.print(" bg_lag_max=");
      Serial.print(dbg.connBgWakeLagMaxUs);
      Serial.print("\r\n");
    }
    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    Gpio::write(kPinUserLed, true);
    delay(1);
    return;
  }

  g_wasConnected = true;

  BleConnectionEvent evt{};
  const bool ran = g_ble.pollConnectionEvent(&evt, 450000UL);
  if (ran && evt.eventStarted) {
    Gpio::write(kPinUserLed, false);

    if (kLogBatteryReadDebug && evt.attPacket &&
        evt.payload != nullptr && evt.payloadLength >= 7U &&
        ((evt.attOpcode == 0x0AU) || (evt.attOpcode == 0x10U) ||
         (evt.attOpcode == 0x12U))) {
      const uint16_t attHandle =
          static_cast<uint16_t>(evt.payload[5]) |
          static_cast<uint16_t>(static_cast<uint16_t>(evt.payload[6]) << 8U);
      const bool logReadHandle =
          (evt.attOpcode == 0x0AU) &&
          ((attHandle == 0x0003U) || (attHandle == 0x0012U));
      const bool logPrimaryServiceBrowse = (evt.attOpcode == 0x10U) && (attHandle == 0x0001U);
      const bool logCccdWrite =
          (evt.attOpcode == 0x12U) &&
          ((attHandle == 0x000BU) || (attHandle == 0x0013U));
      if (logReadHandle || logPrimaryServiceBrowse || logCccdWrite) {
        if (evt.attOpcode == 0x0AU) {
          Serial.print(attHandle == 0x0003U ? "devname-read ce=" : "batt-read ce=");
        } else if (evt.attOpcode == 0x10U) {
          Serial.print("svc-browse ce=");
        } else {
          Serial.print(attHandle == 0x000BU ? "svcchg-cccd ce=" : "batt-cccd ce=");
        }
        Serial.print(evt.eventCounter);
        Serial.print(" ch=");
        Serial.print(evt.dataChannel);
        Serial.print(" new=");
        Serial.print(evt.packetIsNew ? 1 : 0);
        Serial.print(" ack=");
        Serial.print(evt.peerAckedLastTx ? 1 : 0);
        Serial.print(" fresh=");
        Serial.print(evt.freshTxAllowed ? 1 : 0);
        Serial.print(" iack=");
        Serial.print(evt.implicitEmptyAck ? 1 : 0);
        Serial.print(" rxsn=");
        Serial.print(evt.rxSn);
        Serial.print(" rxnesn=");
        Serial.print(evt.rxNesn);
        Serial.print(" txsn=");
        Serial.print(evt.txSn);
        Serial.print(" txnesn=");
        Serial.print(evt.txNesn);
        Serial.print(" tx_llid=");
        Serial.print(evt.txLlid);
        Serial.print(" tx_len=");
        Serial.print(evt.txPayloadLength);
        if (evt.txPayload != nullptr && evt.txPayloadLength >= 5U) {
          Serial.print(" tx_att=0x");
          if (evt.txPayload[4] < 16U) {
            Serial.print('0');
          }
          Serial.print(evt.txPayload[4], HEX);
          if (evt.txPayloadLength >= 7U) {
            const uint16_t txHandle =
                static_cast<uint16_t>(evt.txPayload[5]) |
                static_cast<uint16_t>(static_cast<uint16_t>(evt.txPayload[6]) << 8U);
            Serial.print(" tx_handle=0x");
            if (txHandle < 0x1000U) {
              Serial.print('0');
            }
            if (txHandle < 0x0100U) {
              Serial.print('0');
            }
            if (txHandle < 0x0010U) {
              Serial.print('0');
            }
            Serial.print(txHandle, HEX);
          }
        }
        Serial.print("\r\n");
      }
    }

    if (kLogConnectionPackets &&
        evt.packetReceived && evt.crcOk &&
        (evt.attPacket || evt.llControlPacket)) {
      Serial.print("ce=");
      Serial.print(evt.eventCounter);
      Serial.print(" ch=");
      Serial.print(evt.dataChannel);
      Serial.print(" llid=");
      Serial.print(evt.llid);
      Serial.print(" rx_len=");
      Serial.print(evt.payloadLength);
      Serial.print(" tx_len=");
      Serial.print(evt.txPayloadLength);
      Serial.print(" new=");
      Serial.print(evt.packetIsNew ? 1 : 0);
      Serial.print(" ack=");
      Serial.print(evt.peerAckedLastTx ? 1 : 0);
      Serial.print(" iack=");
      Serial.print(evt.implicitEmptyAck ? 1 : 0);
      Serial.print(" rxsn=");
      Serial.print(evt.rxSn);
      Serial.print(" rxnesn=");
      Serial.print(evt.rxNesn);
      Serial.print(" txsn=");
      Serial.print(evt.txSn);
      Serial.print(" txnesn=");
      Serial.print(evt.txNesn);
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
