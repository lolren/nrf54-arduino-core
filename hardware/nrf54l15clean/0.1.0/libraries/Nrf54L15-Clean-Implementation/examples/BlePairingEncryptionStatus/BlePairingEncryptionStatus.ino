#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static bool g_prevConnected = false;
static bool g_prevEncrypted = false;
static uint32_t g_lastAdvLogMs = 0U;

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

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);
  g_ble.setTraceCallback(onBleTrace, nullptr);

  bool ok = g_ble.begin(-8);
  if (ok) {
    ok = g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingName("X54-PAIR", true) &&
         g_ble.setScanResponseName("X54-PAIR-SCAN") &&
         g_ble.setGattDeviceName("X54 Pairing Probe") &&
         g_ble.setGattBatteryLevel(96U);
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  Serial.print("Pair from phone and watch encryption state transitions.\r\n");
}

void loop() {
  if (!g_ble.isConnected()) {
    if (g_prevConnected) {
      g_prevConnected = false;
      g_prevEncrypted = false;
      Serial.print("disconnected\r\n");
    }

    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    if (adv.receivedConnectInd) {
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
      g_prevConnected = true;
    } else {
      const uint32_t now = millis();
      if ((now - g_lastAdvLogMs) >= 2000UL) {
        g_lastAdvLogMs = now;
        Serial.print("advertising\r\n");
      }
    }

    Gpio::write(kPinUserLed, true);
    __asm volatile("wfi");
    return;
  }

  BleConnectionEvent evt{};
  const bool ran = g_ble.pollConnectionEvent(&evt, 450000UL);
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

  if (ran && evt.eventStarted && evt.packetReceived && evt.crcOk && evt.llControlPacket) {
    Serial.print("ll_ctrl op=0x");
    if (evt.llControlOpcode < 16U) {
      Serial.print('0');
    }
    Serial.print(evt.llControlOpcode, HEX);
    Serial.print(" ce=");
    Serial.print(evt.eventCounter);
    Serial.print("\r\n");
  }

  if (ran && evt.terminateInd) {
    Serial.print("link terminated\r\n");
    g_prevConnected = false;
    g_prevEncrypted = false;
  }

  Gpio::write(kPinUserLed, encrypted ? false : true);
  if (!ran) {
    __asm volatile("wfi");
  }
}
