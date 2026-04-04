/*
 * BleNordicUartCommandConsole
 *
 * A simple text command console accessible over BLE using the Nordic UART
 * Service (NUS). Connect with a NUS-capable app (e.g., nRF Toolbox → UART,
 * Serial Bluetooth Terminal) and type commands. Responses are sent back as
 * BLE notifications.
 *
 * Available commands (send from your phone):
 *   help          – list commands
 *   status        – print connection stats and LED state
 *   led on        – turn on the user LED
 *   led off       – turn off the user LED
 *   led toggle    – toggle the user LED
 *   echo <text>   – echo text back over BLE
 *
 * NUS architecture: RX characteristic (write-only from central) receives
 * commands; TX characteristic (notify) sends responses to the central.
 * Commands must end with CR or LF (\r or \n).
 *
 * Tip: this example keeps a short name in the primary ADV packet so stricter
 * phone scanners can see it without depending on a scan response.
 */

#include <Arduino.h>

#include <string.h>

#include "ble_nus.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static BleNordicUart g_nus(g_ble);  // NUS handles GATT service and ring buffers.
static PowerManager g_power;

static bool g_wasConnected = false;
static bool g_bannerSent = false;  // True once the welcome banner has been sent.
static bool g_ledOn = false;
static bool g_securityRequested = false;
static char g_lineBuffer[64];      // Accumulates incoming characters until newline.
static uint8_t g_lineLength = 0U;
static uint32_t g_connectedAtMs = 0U;
static constexpr bool kEnableSerialLogs = false;

// 0 dBm: good general-purpose TX power for console use.
static constexpr int8_t kTxPowerDbm = 0;
// Optional fixed test address. Leave it off by default because the board's
// factory-derived BLE address is more discoverable on some phones.
static const uint8_t kAddress[6] = {0x36, 0x00, 0x15, 0x54, 0xDE, 0xC0};
static constexpr bool kUseFixedAddress = false;
static constexpr char kDeviceName[] = "X54-CMD";
static const uint8_t kNusAdvPayload[] = {
    2, 0x01, 0x06,
    8, 0x09, 'X', '5', '4', '-', 'C', 'M', 'D',
    5, 0xFF, 0x34, 0x12, 0x54, 0x15,
};
static constexpr uint32_t kConnectionPollTimeoutUs = 450000UL;
static constexpr uint32_t kLinkWarmupMs = 500UL;
static constexpr bool kRequestLinkSecurity = false;

static void logSerial(const char* text) {
  if (!kEnableSerialLogs || text == nullptr) {
    return;
  }
  Serial.print(text);
}

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
        if (kEnableSerialLogs) {
          Serial.print("cmd: ");
          Serial.print(g_lineBuffer);
          Serial.print("\r\n");
        }
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

static void maybeRequestLinkSecurity(uint32_t nowMs) {
  if (!kRequestLinkSecurity || g_securityRequested || !g_ble.isConnected()) {
    return;
  }
  if ((nowMs - g_connectedAtMs) < kLinkWarmupMs) {
    return;
  }
  g_securityRequested = true;
  g_ble.sendSmpSecurityRequest();
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
    ok = g_ble.begin(kTxPowerDbm);
  }
  if (ok) {
    ok = (!kUseFixedAddress || g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic)) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingChannelSelectionAlgorithm2(true) &&
         g_ble.setGattDeviceName(kDeviceName) &&
         g_ble.setGattBatteryLevel(100U) &&
         g_ble.clearCustomGatt() &&
         g_nus.begin() &&
         g_ble.setAdvertisingData(kNusAdvPayload, sizeof(kNusAdvPayload)) &&
         g_ble.setScanResponseData(nullptr, 0U);
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
      g_securityRequested = false;
      g_lineLength = 0U;
      setLed(false);
      logSerial("BLE console disconnected\r\n");
    }

    if (!g_ble.isConnected()) {
      delay(100);
    }
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    g_bannerSent = false;
    g_securityRequested = false;
    g_connectedAtMs = millis();
    g_lineLength = 0U;
    logSerial("BLE console connected\r\n");
  }

  BleConnectionEvent evt{};
  const bool eventStarted =
      g_ble.pollConnectionEvent(&evt, kConnectionPollTimeoutUs) && evt.eventStarted;
  if (evt.terminateInd && g_ble.isConnected()) {
    evt.terminateInd = false;
  }
  if (eventStarted) {
    g_nus.service(&evt);
    if (evt.terminateInd) {
      logSerial("BLE link terminated\r\n");
    }
  } else {
    g_nus.service();
  }

  maybeRequestLinkSecurity(millis());

  // Only send the banner once per connection, and only after the central has
  // enabled NUS TX notifications (isNotifyEnabled() checks the CCCD value).
  if (!g_bannerSent && g_nus.isNotifyEnabled()) {
    g_bannerSent = true;
    g_nus.print("X54 Nordic UART command console\r\n");
    g_nus.print("Type help for commands\r\n");
    printPrompt();
  }

  processBleInput();
}
