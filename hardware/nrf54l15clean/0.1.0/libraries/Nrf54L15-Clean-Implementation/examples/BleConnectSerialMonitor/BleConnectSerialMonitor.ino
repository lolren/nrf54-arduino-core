#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_advEvents = 0;
static uint32_t g_linkEvents = 0;
static uint32_t g_lastLogMs = 0;
static bool g_initOk = false;
static bool g_wasConnected = false;

static constexpr int8_t kTxPowerDbm = 8;
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

  Serial.print("\r\nBleConnectSerialMonitor start\r\n");

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  bool ok = BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
  if (ok) {
    ok = g_ble.begin(kTxPowerDbm);
  }

  static const uint8_t kAddress[6] = {0x31, 0x00, 0x15, 0x54, 0xDE, 0xC0};
  static const uint8_t kAdvPayload[] = {
      2, 0x01, 0x06,
      13, 0x09, 'X', 'I', 'A', 'O', '-', 'C', 'O', 'N', 'S', 'O', 'L', 'E',
      3, 0x19, 0x80, 0x00,
  };
  static const uint8_t kScanRspPayload[] = {
      5, 0xFF, 0x34, 0x12, 0x54, 0x15,
  };

  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingChannelSelectionAlgorithm2(false) &&
         g_ble.setAdvertisingData(kAdvPayload, sizeof(kAdvPayload)) &&
         g_ble.setScanResponseData(kScanRspPayload, sizeof(kScanRspPayload)) &&
         g_ble.buildAdvertisingPacket() &&
         g_ble.setGattDeviceName("XIAO-CONSOLE") &&
         g_ble.setGattBatteryLevel(100U);
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print(" antenna=ceramic tx_dbm=");
  Serial.print(static_cast<int>(kTxPowerDbm));
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

  g_initOk = ok;
}

void loop() {
  const uint32_t now = millis();

  if (!g_ble.isConnected()) {
    BleAdvInteraction interaction{};
    const bool ok = g_ble.advertiseInteractEvent(&interaction, 350U, 350000UL, 700000UL);
    ++g_advEvents;

    if (g_wasConnected) {
      g_wasConnected = false;
      Gpio::write(kPinUserLed, true);
      Serial.print("disconnected\r\n");
    }

    if (interaction.receivedConnectInd) {
      Serial.print("connect_ind peer=");
      printAddress(interaction.peerAddress);
      Serial.print(" rssi=");
      Serial.print(interaction.rssiDbm);
      Serial.print("\r\n");
    }

    if ((now - g_lastLogMs) >= 1000UL) {
      g_lastLogMs = now;
      Serial.print("adv t=");
      Serial.print(now);
      Serial.print(" ev=");
      Serial.print(g_advEvents);
      Serial.print(" status=");
      Serial.print(ok ? "OK" : "FAIL");
      Serial.print(" init=");
      Serial.print(g_initOk ? "OK" : "FAIL");
      Serial.print("\r\n");
    }

    // Legacy advertising interval should stay >= 20 ms for broad interoperability.
    delay(kAdvIntervalMs);
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    Gpio::write(kPinUserLed, false);

    BleConnectionInfo info{};
    if (g_ble.getConnectionInfo(&info)) {
      Serial.print("connected peer=");
      printAddress(info.peerAddress);
      Serial.print(" aa=0x");
      Serial.print(info.accessAddress, HEX);
      Serial.print(" int=");
      Serial.print(info.intervalUnits);
      Serial.print("\r\n");
    } else {
      Serial.print("connected\r\n");
    }
  }

  BleConnectionEvent evt{};
  const bool ran = g_ble.pollConnectionEvent(&evt, 450000UL);
  if (ran && evt.eventStarted) {
    ++g_linkEvents;
    if (evt.packetReceived) {
      Serial.print("ce=");
      Serial.print(evt.eventCounter);
      Serial.print(" ch=");
      Serial.print(evt.dataChannel);
      Serial.print(" crc=");
      Serial.print(evt.crcOk ? "1" : "0");
      Serial.print(" llid=");
      Serial.print(evt.llid);
      Serial.print(" len=");
      Serial.print(evt.payloadLength);
      Serial.print(" rssi=");
      Serial.print(evt.rssiDbm);
      Serial.print("\r\n");
    }
    if (evt.terminateInd) {
      Serial.print("link terminated\r\n");
    }
  }

  if ((now - g_lastLogMs) >= 1000UL) {
    g_lastLogMs = now;
    Serial.print("connected t=");
    Serial.print(now);
    Serial.print(" link_ev=");
    Serial.print(g_linkEvents);
    Serial.print("\r\n");
  }

  delay(1);
}
