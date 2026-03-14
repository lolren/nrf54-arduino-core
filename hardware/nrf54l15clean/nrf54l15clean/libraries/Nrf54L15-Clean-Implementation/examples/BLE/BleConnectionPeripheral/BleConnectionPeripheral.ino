#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_lastLogMs = 0;
static uint32_t g_advEvents = 0;
static uint32_t g_linkEvents = 0;
static constexpr int8_t kTxPowerDbm = -8;
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

  Serial.print("\r\nBleConnectionPeripheral start\r\n");

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  static const uint8_t kAddress[6] = {0x21, 0x00, 0x15, 0x54, 0xDE, 0xC0};
  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingChannelSelectionAlgorithm2(false) &&
         g_ble.setAdvertisingName("XIAO54-LINK", true) &&
         g_ble.setScanResponseName("XIAO54-LINK-SCAN") &&
         g_ble.setGattDeviceName("XIAO54-LINK") &&
         g_ble.setGattBatteryLevel(100U);
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
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
  if (!g_ble.isConnected()) {
    BleAdvInteraction adv{};
    const bool advOk = g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    ++g_advEvents;

    if (adv.receivedConnectInd) {
      BleConnectionInfo info{};
      if (g_ble.getConnectionInfo(&info)) {
        Serial.print("connected peer=");
        printAddress(info.peerAddress);
        Serial.print(" aa=0x");
        Serial.print(info.accessAddress, HEX);
        Serial.print(" int=");
        Serial.print(info.intervalUnits);
        Serial.print(" hop=");
        Serial.print(info.hopIncrement);
        Serial.print(" chcnt=");
        Serial.print(info.channelCount);
        Serial.print("\r\n");
      }
    }

    const uint32_t now = millis();
    if ((now - g_lastLogMs) >= 1000UL) {
      g_lastLogMs = now;
      Serial.print("adv t=");
      Serial.print(now);
      Serial.print(" ev=");
      Serial.print(g_advEvents);
      Serial.print(" status=");
      Serial.print(advOk ? "OK" : "FAIL");
      Serial.print("\r\n");
    }

    Gpio::write(kPinUserLed, true);
    // Legacy advertising interval should stay >= 20 ms for broad interoperability.
    delay(kAdvIntervalMs);
    return;
  }

  BleConnectionEvent evt{};
  const bool ran = g_ble.pollConnectionEvent(&evt, 450000UL);
  if (ran && evt.eventStarted) {
    ++g_linkEvents;
    Gpio::write(kPinUserLed, false);

    if (evt.packetReceived && evt.crcOk) {
      Serial.print("ce=");
      Serial.print(evt.eventCounter);
      Serial.print(" ch=");
      Serial.print(evt.dataChannel);
      Serial.print(" llid=");
      Serial.print(evt.llid);
      Serial.print(" len=");
      Serial.print(evt.payloadLength);
      Serial.print(" new=");
      Serial.print(evt.packetIsNew ? "1" : "0");
      Serial.print(" ack=");
      Serial.print(evt.emptyAckTransmitted ? "1" : "0");
      Serial.print(" tx_llid=");
      Serial.print(evt.txLlid);
      Serial.print(" tx_len=");
      Serial.print(evt.txPayloadLength);
      Serial.print(" rssi=");
      Serial.print(evt.rssiDbm);
      Serial.print("\r\n");

      if (evt.llControlPacket) {
        Serial.print("  ll_ctrl_op=0x");
        if (evt.llControlOpcode < 16U) {
          Serial.print('0');
        }
        Serial.print(evt.llControlOpcode, HEX);
        Serial.print("\r\n");
      }
      if (evt.attPacket) {
        Serial.print("  att_op=0x");
        if (evt.attOpcode < 16U) {
          Serial.print('0');
        }
        Serial.print(evt.attOpcode, HEX);
        Serial.print("\r\n");
      }
    }

    if (evt.terminateInd) {
      Serial.print("link terminated\r\n");
    }
  } else {
    Gpio::write(kPinUserLed, true);
    delay(1);
  }
}
