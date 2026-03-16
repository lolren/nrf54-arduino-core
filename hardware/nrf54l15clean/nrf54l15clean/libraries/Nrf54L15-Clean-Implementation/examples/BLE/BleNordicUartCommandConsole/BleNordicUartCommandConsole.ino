#include <Arduino.h>

#include <string.h>

#include "ble_nus.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static BleNordicUart g_nus(g_ble);
static PowerManager g_power;

static bool g_wasConnected = false;
static bool g_bannerSent = false;
static bool g_ledOn = false;
static char g_lineBuffer[64];
static uint8_t g_lineLength = 0U;

static constexpr int8_t kTxPowerDbm = 0;
static const uint8_t kAddress[6] = {0x36, 0x00, 0x15, 0x54, 0xDE, 0xC0};
static const uint8_t kNusScanResponse[] = {
    17, 0x07,
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E};

static void setLed(bool on) {
  g_ledOn = on;
  Gpio::write(kPinUserLed, on ? false : true);
}

static void printPrompt() {
  g_nus.print("> ");
}

static void handleCommand(const char* line) {
  if (line == nullptr || line[0] == '\0') {
    printPrompt();
    return;
  }

  if (strcmp(line, "help") == 0) {
    g_nus.print("help, status, led on, led off, led toggle, echo <text>\r\n");
    printPrompt();
    return;
  }

  if (strcmp(line, "status") == 0) {
    g_nus.print("connected=yes notify=");
    g_nus.print(g_nus.isNotifyEnabled() ? "on" : "off");
    g_nus.print(" led=");
    g_nus.print(g_ledOn ? "on" : "off");
    g_nus.print(" rx_drop=");
    g_nus.print(g_nus.rxDroppedBytes());
    g_nus.print(" tx_drop=");
    g_nus.print(g_nus.txDroppedBytes());
    g_nus.print(" uptime_ms=");
    g_nus.print(millis());
    g_nus.print("\r\n");
    printPrompt();
    return;
  }

  if (strcmp(line, "led on") == 0) {
    setLed(true);
    g_nus.print("LED on\r\n");
    printPrompt();
    return;
  }

  if (strcmp(line, "led off") == 0) {
    setLed(false);
    g_nus.print("LED off\r\n");
    printPrompt();
    return;
  }

  if (strcmp(line, "led toggle") == 0) {
    setLed(!g_ledOn);
    g_nus.print("LED toggled\r\n");
    printPrompt();
    return;
  }

  if (strncmp(line, "echo ", 5) == 0) {
    g_nus.print(&line[5]);
    g_nus.print("\r\n");
    printPrompt();
    return;
  }

  g_nus.print("unknown command\r\n");
  printPrompt();
}

static void processBleInput() {
  while (g_nus.available() > 0) {
    const int ch = g_nus.read();
    if (ch < 0) {
      break;
    }

    if (ch == '\r' || ch == '\n') {
      if (g_lineLength > 0U) {
        g_lineBuffer[g_lineLength] = '\0';
        Serial.print("cmd: ");
        Serial.print(g_lineBuffer);
        Serial.print("\r\n");
        handleCommand(g_lineBuffer);
        g_lineLength = 0U;
      } else {
        printPrompt();
      }
      continue;
    }

    if (g_lineLength + 1U < sizeof(g_lineBuffer)) {
      g_lineBuffer[g_lineLength++] = static_cast<char>(ch);
    } else {
      g_lineLength = 0U;
      g_nus.print("line too long\r\n");
      printPrompt();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleNordicUartCommandConsole start\r\n");

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  setLed(false);

  bool ok = BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
  if (ok) {
    ok = g_ble.begin(kTxPowerDbm) &&
         g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingName("X54-NUS-CMD", true) &&
         g_ble.setScanResponseData(kNusScanResponse, sizeof(kNusScanResponse)) &&
         g_ble.setGattDeviceName("X54 NUS Console") &&
         g_ble.clearCustomGatt() && g_nus.begin();
  }

  Serial.print("BLE NUS console init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
}

void loop() {
  if (!g_ble.isConnected()) {
    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    g_nus.service();

    if (g_wasConnected) {
      g_wasConnected = false;
      g_bannerSent = false;
      g_lineLength = 0U;
      setLed(false);
      Serial.print("BLE console disconnected\r\n");
    }

    delay(20);
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    g_bannerSent = false;
    g_lineLength = 0U;
    Serial.print("BLE console connected\r\n");
  }

  BleConnectionEvent evt{};
  if (g_ble.pollConnectionEvent(&evt, 450000UL) && evt.eventStarted) {
    g_nus.service(&evt);
    if (evt.terminateInd) {
      Serial.print("BLE link terminated\r\n");
    }
  } else {
    g_nus.service();
  }

  if (!g_bannerSent && g_nus.isNotifyEnabled()) {
    g_bannerSent = true;
    g_nus.print("X54 Nordic UART command console\r\n");
    g_nus.print("Type help for commands\r\n");
    printPrompt();
  }

  processBleInput();
  delay(1);
}
