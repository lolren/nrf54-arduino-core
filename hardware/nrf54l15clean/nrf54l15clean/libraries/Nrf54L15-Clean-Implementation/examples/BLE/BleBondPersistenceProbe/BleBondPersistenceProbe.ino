#include <Arduino.h>

#include <string.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;
static bool g_bleReady = false;
static constexpr bool kEnableBleTraceLogging = false;

static bool g_prevConnected = false;
static bool g_prevEncrypted = false;
static bool g_connectionAnnounced = false;
static uint32_t g_lastAdvLogMs = 0U;
static uint32_t g_lastBondLogMs = 0U;
static uint32_t g_lastInitErrorLogMs = 0U;
static constexpr int8_t kTxPowerDbm = 0;

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

static void printBondSummary() {
  BleBondRecord bond{};
  if (!g_ble.getBondRecord(&bond)) {
    Serial.print("bond: none\r\n");
    return;
  }
  Serial.print("bond: peer=");
  printAddress(bond.peerAddress);
  Serial.print(" keySize=");
  Serial.print(bond.keySize);
  Serial.print(" ediv=0x");
  if (bond.ediv < 0x1000U) {
    Serial.print('0');
  }
  if (bond.ediv < 0x0100U) {
    Serial.print('0');
  }
  if (bond.ediv < 0x0010U) {
    Serial.print('0');
  }
  Serial.print(bond.ediv, HEX);
  Serial.print("\r\n");
}

static void handleSerialCommands() {
  static char command[48] = {0};
  static size_t commandLen = 0U;

  while (Serial.available() > 0) {
    const int readValue = Serial.read();
    if (readValue < 0) {
      break;
    }
    const char ch = static_cast<char>(readValue);
    if (ch == '\r' || ch == '\n') {
      if (commandLen == 0U) {
        continue;
      }
      command[commandLen] = '\0';
      if (strcmp(command, "clear-bond") == 0) {
        g_ble.clearBondRecord(true);
        Serial.print("bond cleared (serial command)\r\n");
        printBondSummary();
      } else if (strcmp(command, "show-bond") == 0) {
        printBondSummary();
      } else {
        Serial.print("unknown command: ");
        Serial.print(command);
        Serial.print("\r\n");
      }
      commandLen = 0U;
      continue;
    }
    if (commandLen + 1U < sizeof(command)) {
      command[commandLen] = ch;
      ++commandLen;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nBleBondPersistenceProbe start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);
  Gpio::configure(kPinUserButton, GpioDirection::kInput, GpioPull::kPullUp);
  if (kEnableBleTraceLogging) {
    g_ble.setTraceCallback(onBleTrace, nullptr);
  } else {
    g_ble.setTraceCallback(nullptr, nullptr);
  }

  bool buttonPressed = true;
  if (!Gpio::read(kPinUserButton, &buttonPressed)) {
    buttonPressed = false;
  } else {
    buttonPressed = !buttonPressed;  // Active low.
  }
  if (buttonPressed) {
    g_ble.clearBondRecord(true);
    Serial.print("bond cleared (button held at boot)\r\n");
  }

  static const uint8_t kAddress[6] = {0x61, 0x00, 0x15, 0x54, 0xDE, 0xC0};
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
  if (ok && !g_ble.setAdvertisingName("X54-BOND", true)) {
    ok = false;
    Serial.print("BLE step failed: setAdvertisingName\r\n");
  }
  if (ok && !g_ble.setScanResponseName("X54-BOND-SCAN")) {
    ok = false;
    Serial.print("BLE step failed: setScanResponseName\r\n");
  }
  if (ok && !g_ble.setGattDeviceName("X54 Bond Probe")) {
    ok = false;
    Serial.print("BLE step failed: setGattDeviceName\r\n");
  }
  if (ok && !g_ble.setGattBatteryLevel(95U)) {
    ok = false;
    Serial.print("BLE step failed: setGattBatteryLevel\r\n");
  }
  g_bleReady = ok;
  if (g_bleReady) {
    // Keep BLE timing margins deterministic during SMP/encryption transitions.
    g_power.setLatencyMode(PowerLatencyMode::kConstantLatency);
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  Serial.print("Pair once, disconnect, reconnect, then observe encryption and bond reuse.\r\n");
  Serial.print("Hold user button while rebooting to clear bond.\r\n");
  Serial.print("Serial commands: clear-bond | show-bond\r\n");
  printBondSummary();
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
    handleSerialCommands();

    if (g_prevConnected) {
      g_prevConnected = false;
      g_prevEncrypted = false;
      g_connectionAnnounced = false;
      Serial.print("disconnected\r\n");
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
      if ((now - g_lastBondLogMs) >= 10000UL) {
        g_lastBondLogMs = now;
        printBondSummary();
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
      Serial.print("\r\n");
    } else {
      Serial.print("connected\r\n");
    }
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
    if (encrypted) {
      printBondSummary();
    }
  }

  if (ran && evt.terminateInd) {
    Serial.print("link terminated\r\n");
    g_prevConnected = false;
    g_prevEncrypted = false;
    g_connectionAnnounced = false;
  }

  Gpio::write(kPinUserLed, encrypted ? false : true);
  if (!ran) {
    delay(1);
  }
}
