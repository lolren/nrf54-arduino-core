#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

// Full legacy advertising interaction example.
//
// This sketch exists to expose the raw advertiser's interaction path:
// - SCAN_REQ / SCAN_RSP
// - CONNECT_IND
// - connection-event polling after a connection is established
//
// This is a BLE feature-demo sketch, not a low-power example.

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_advEvents = 0;
static uint32_t g_scanReqCount = 0;
static uint32_t g_scanRspCount = 0;
static uint32_t g_connectIndCount = 0;
static uint32_t g_linkEvents = 0;
static uint32_t g_anyRxCount = 0;
static uint32_t g_connRxAny = 0;
static uint32_t g_connRxCrcOk = 0;
static uint32_t g_lastLogMs = 0;
static bool g_initOk = false;
static bool g_lastConnChSel2 = false;
static constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
static constexpr int8_t kAdvertiserTxPowerDbm = 8;
static constexpr uint32_t kAdvIntervalMs = 100;

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

  Serial.print("\r\nBleConnectableScannableAdvertiser start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);

  // Make the antenna path explicit in the example. Use kExternal only if an
  // external antenna is attached.
  bool ok = BoardControl::setAntennaPath(kAntennaPath);
  if (ok) {
    ok = g_ble.begin(kAdvertiserTxPowerDbm);
  }
  static const uint8_t kAddress[6] = {0x11, 0x00, 0x15, 0x54, 0xDE, 0xC0};

  static const uint8_t kAdvPayload[] = {
      2, 0x01, 0x06,                                   // Flags
      18, 0x09, 'X', 'I', 'A', 'O', '-', '5', '4', '-', 'S', 'C', 'A',
      'N', '-', 'D', 'E', 'M', 'O',                    // Name
      3, 0x19, 0x80, 0x00                              // Appearance (generic)
  };
  static const uint8_t kScanRspPayload[] = {
      5, 0xFF, 0x34, 0x12, 0x54, 0x15
  };

  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic);
  }
  if (ok) {
    // ADV_IND keeps the example both connectable and scannable.
    ok = g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd);
  }
  if (ok) {
    ok = g_ble.setAdvertisingChannelSelectionAlgorithm2(false);
  }
  if (ok) {
    ok = g_ble.setAdvertisingData(kAdvPayload, sizeof(kAdvPayload));
  }
  if (ok) {
    ok = g_ble.setScanResponseData(kScanRspPayload, sizeof(kScanRspPayload));
  }
  if (ok) {
    ok = g_ble.buildAdvertisingPacket();
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  Serial.print("antenna=");
  const BoardAntennaPath path = BoardControl::antennaPath();
  if (path == BoardAntennaPath::kExternal) {
    Serial.print("external");
  } else if (path == BoardAntennaPath::kControlHighImpedance) {
    Serial.print("hi-z");
  } else {
    Serial.print("ceramic");
  }
  Serial.print(" tx_dbm=");
  Serial.print(static_cast<int>(kAdvertiserTxPowerDbm));
  Serial.print("\r\n");
  g_initOk = ok;
  if (ok) {
    uint8_t addr[6];
    BleAddressType type = BleAddressType::kPublic;
    if (g_ble.getDeviceAddress(addr, &type)) {
      Serial.print("addr=");
      printAddress(addr);
      Serial.print(" type=");
      Serial.print((type == BleAddressType::kRandomStatic) ? "random" : "public");
      Serial.print("\r\n");
    }
  }
}

void loop() {
  const uint32_t now = millis();
  if (g_ble.isConnected()) {
    // Once connected, stop advertising and inspect the connection-event path.
    BleConnectionEvent evt{};
    const bool ran = g_ble.pollConnectionEvent(&evt, 450000UL);
    if (ran && evt.eventStarted) {
      ++g_linkEvents;
      if (evt.packetReceived) {
        ++g_connRxAny;
      }
      if (evt.packetReceived && evt.crcOk) {
        ++g_connRxCrcOk;
      }
      Serial.print("ev ce=");
      Serial.print(evt.eventCounter);
      Serial.print(" ch=");
      Serial.print(evt.dataChannel);
      Serial.print(" rx=");
      Serial.print(evt.packetReceived ? "1" : "0");
      Serial.print(" crc=");
      Serial.print(evt.crcOk ? "1" : "0");
      Serial.print(" tx=");
      Serial.print(evt.txPacketSent ? "1" : "0");
      Serial.print(" new=");
      Serial.print(evt.packetIsNew ? "1" : "0");
      Serial.print(" llid=");
      Serial.print(evt.llid);
      Serial.print(" len=");
      Serial.print(evt.payloadLength);
      Serial.print(" tx_llid=");
      Serial.print(evt.txLlid);
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
      if (g_linkEvents <= 3U) {
        BleConnectionInfo info{};
        if (g_ble.getConnectionInfo(&info)) {
          Serial.print("  map_now=");
          for (uint8_t i = 0; i < 5; ++i) {
            if (info.channelMap[i] < 16U) {
              Serial.print('0');
            }
            Serial.print(info.channelMap[i], HEX);
          }
          Serial.print(" hop=");
          Serial.print(info.hopIncrement);
          Serial.print("\r\n");
        }
      }
      Gpio::write(kPinUserLed, false);
      if (evt.terminateInd) {
        Serial.print("link terminated\r\n");
        Serial.print("conn_rx=");
        Serial.print(g_connRxAny);
        Serial.print(" conn_crc=");
        Serial.print(g_connRxCrcOk);
        Serial.print("\r\n");
      }
    } else {
      // Stay lit while connected.
      Gpio::write(kPinUserLed, false);
      delay(1);
    }

    if ((now - g_lastLogMs) >= 1000UL) {
      g_lastLogMs = now;
      char line[160];
      snprintf(line, sizeof(line),
               "t=%lu connected link_ev=%lu conn_rx=%lu conn_crc=%lu scan_req=%lu scan_rsp=%lu conn_ind=%lu any_rx=%lu init=%s\r\n",
               static_cast<unsigned long>(now),
               static_cast<unsigned long>(g_linkEvents),
               static_cast<unsigned long>(g_connRxAny),
               static_cast<unsigned long>(g_connRxCrcOk),
               static_cast<unsigned long>(g_scanReqCount),
               static_cast<unsigned long>(g_scanRspCount),
               static_cast<unsigned long>(g_connectIndCount),
               static_cast<unsigned long>(g_anyRxCount),
               g_initOk ? "OK" : "FAIL");
      Serial.print(line);
      Serial.print("last_conn_chsel2=");
      Serial.print(g_lastConnChSel2 ? "1" : "0");
      Serial.print("\r\n");
    }
    return;
  }

  // In the not-connected state, keep running one interactive advertising event
  // at a time so the sketch can report SCAN_REQ and CONNECT_IND details.
  BleAdvInteraction interaction{};
  const bool ok = g_ble.advertiseInteractEvent(&interaction, 350U, 350000UL, 700000UL);
  ++g_advEvents;

  if (interaction.receivedScanRequest) {
    ++g_scanReqCount;
    if (interaction.scanResponseTransmitted) {
      ++g_scanRspCount;
    }

    Serial.print("scan_req ch=");
    Serial.print(static_cast<uint8_t>(interaction.channel));
    Serial.print(" peer=");
    printAddress(interaction.peerAddress);
    Serial.print(" rssi=");
    Serial.print(interaction.rssiDbm);
    Serial.print(" rsp=");
    Serial.print(interaction.scanResponseTransmitted ? "TX" : "miss");
    Serial.print("\r\n");
  }

  if (interaction.receivedConnectInd) {
    ++g_connectIndCount;
    g_lastConnChSel2 = interaction.connectIndChSel2;

    Serial.print("connect_ind ch=");
    Serial.print(static_cast<uint8_t>(interaction.channel));
    Serial.print(" initA=");
    printAddress(interaction.peerAddress);
    Serial.print(" rssi=");
    Serial.print(interaction.rssiDbm);
    Serial.print(" chsel2=");
    Serial.print(interaction.connectIndChSel2 ? "1" : "0");
    Serial.print("\r\n");

    BleConnectionInfo info{};
    if (g_ble.getConnectionInfo(&info)) {
      Serial.print("conn aa=0x");
      Serial.print(info.accessAddress, HEX);
      Serial.print(" int=");
      Serial.print(info.intervalUnits);
      Serial.print(" hop=");
      Serial.print(info.hopIncrement);
      Serial.print(" chcnt=");
      Serial.print(info.channelCount);
      Serial.print(" chm=");
      for (uint8_t i = 0; i < 5; ++i) {
        if (info.channelMap[i] < 16U) {
          Serial.print('0');
        }
        Serial.print(info.channelMap[i], HEX);
      }
      Serial.print("\r\n");
    }
  }

  if (interaction.rssiDbm != 0) {
    ++g_anyRxCount;
  }

  // Active-low LED: pulse on interactions.
  const bool interactionSeen = interaction.receivedScanRequest || interaction.receivedConnectInd;
  Gpio::write(kPinUserLed, interactionSeen ? false : true);

  if ((now - g_lastLogMs) >= 1000UL) {
    g_lastLogMs = now;
    char line[160];
    snprintf(line, sizeof(line),
             "t=%lu adv=%lu scan_req=%lu scan_rsp=%lu conn_ind=%lu any_rx=%lu status=%s init=%s\r\n",
             static_cast<unsigned long>(now),
             static_cast<unsigned long>(g_advEvents),
             static_cast<unsigned long>(g_scanReqCount),
             static_cast<unsigned long>(g_scanRspCount),
             static_cast<unsigned long>(g_connectIndCount),
             static_cast<unsigned long>(g_anyRxCount),
             ok ? "OK" : "FAIL",
             g_initOk ? "OK" : "FAIL");
    Serial.print(line);
    Serial.print("last_conn_chsel2=");
    Serial.print(g_lastConnChSel2 ? "1" : "0");
    Serial.print("\r\n");
  }

  // Legacy advertising interval should stay >= 20 ms for broad interoperability.
  delay(kAdvIntervalMs);
}
