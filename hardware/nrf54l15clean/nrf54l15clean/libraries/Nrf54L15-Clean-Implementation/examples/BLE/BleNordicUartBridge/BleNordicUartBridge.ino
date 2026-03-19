#include <Arduino.h>

#include "ble_nus.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static BleNordicUart g_nus(g_ble);
static PowerManager g_power;

static bool g_wasConnected = false;
static bool g_bannerSent = false;
static uint32_t g_lastStatusMs = 0U;
static uint32_t g_lastSelfTestMs = 0U;
static uint32_t g_usbDroppedBytes = 0U;

static constexpr int8_t kTxPowerDbm = 0;
// Keep a sketch-specific address so mobile centrals do not reuse a stale GATT
// cache after switching BLE sketches or reflashing NUS changes on the same board.
static constexpr bool kUseFixedAddress = true;
// Debug aid: when enabled, the sketch generates a known ASCII stream internally
// (no USB input) to help isolate BLE TX corruption vs. USB-UART bridge issues.
static constexpr bool kEnableSelfTestTx = false;
static const uint8_t kAddress[6] = {0x35, 0x00, 0x15, 0x54, 0xDE, 0xC0};
static const uint8_t kNusAdvPayload[] = {
    2, 0x01, 0x06,  // Flags: LE General Discoverable + BR/EDR not supported.
    8, 0x09, 'X', '5', '4', '-', 'N', 'U', 'S',  // Complete local name.
    // Complete list of 128-bit Service UUIDs (NUS) in little-endian.
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

static const char* disconnectReasonLabel(uint8_t reason) {
  switch (reason) {
    case 0x08:
      return "timeout";
    case 0x13:
      return "remote user";
    case 0x16:
      return "local host";
    case 0x3D:
      return "mic failure";
    default:
      return "other";
  }
}

static void pumpUsbToBle(int maxBytes) {
  // Without hardware flow control, the USB-UART bridge can overrun if the BLE
  // link can't drain fast enough (or if notifications are temporarily off
  // during Android discovery). Prefer dropping input bytes early rather than
  // letting the UART receiver overflow and inject garbage into the stream.

  int budget = g_nus.availableForWrite();
  if (budget > maxBytes) {
    budget = maxBytes;
  }
  if (!g_nus.isNotifyEnabled() || budget <= 0) {
    // Drop aggressively to keep the UART from overflowing and corrupting
    // subsequent bytes. Bound work per call so we still service BLE anchors.
    int dropBudget = maxBytes * 64;
    if (dropBudget < 64) {
      dropBudget = 64;
    }
    while (dropBudget > 0) {
      const int ch = Serial.read();
      if (ch < 0) {
        break;
      }
      ++g_usbDroppedBytes;
      --dropBudget;
    }
    return;
  }
  while (budget > 0 && Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch < 0) {
      break;
    }
    g_nus.write(static_cast<uint8_t>(ch));
    --budget;
  }
}

static void pumpBleToUsb(int maxBytes) {
  int budget = maxBytes;
  while (budget > 0 && g_nus.available() > 0) {
    const int ch = g_nus.read();
    if (ch < 0) {
      break;
    }
    Serial.write(static_cast<uint8_t>(ch));
    --budget;
  }
}

static void selfTestTx() {
  if (!kEnableSelfTestTx || !g_nus.isNotifyEnabled()) {
    return;
  }

  const uint32_t nowMs = millis();
  if ((nowMs - g_lastSelfTestMs) < 100U) {
    return;
  }
  g_lastSelfTestMs = nowMs;

  static const uint8_t kPattern[] = "0123456789ABCDEF\r\n";
  if (g_nus.availableForWrite() >= static_cast<int>(sizeof(kPattern) - 1U)) {
    g_nus.write(kPattern, sizeof(kPattern) - 1U);
  }
}

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleNordicUartBridge start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);
  Gpio::configure(kPinUserButton, GpioDirection::kInput, GpioPull::kPullUp);

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

  bool ok = BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
  if (ok) {
    ok = g_ble.begin(kTxPowerDbm) &&
         (!kUseFixedAddress ||
          g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic)) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingData(kNusAdvPayload, sizeof(kNusAdvPayload)) &&
         g_ble.setScanResponseData(nullptr, 0U) &&
         g_ble.setGattDeviceName("X54 NUS Bridge") &&
         g_ble.clearCustomGatt() && g_nus.begin();
  }

  if (ok) {
    // Keep BLE timing margins deterministic under load and during SMP/encryption
    // transitions initiated by some centrals.
    g_power.setLatencyMode(PowerLatencyMode::kConstantLatency);
  }

  Serial.print("BLE NUS init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (ok) {
    uint8_t addr[6] = {0};
    BleAddressType type = BleAddressType::kPublic;
    if (g_ble.getDeviceAddress(addr, &type)) {
      Serial.print("addr=");
      printAddress(addr);
      Serial.print(" type=");
      Serial.print((type == BleAddressType::kRandomStatic) ? "random" : "public");
      Serial.print("\r\n");
    }
    Serial.print("Advertised as X54-NUS. Open a Nordic UART client and bridge bytes.\r\n");
  }
}

void loop() {
  if (!g_ble.isConnected()) {
    BleAdvInteraction adv{};
    g_ble.advertiseInteractEvent(&adv, 350U, 350000UL, 700000UL);
    g_nus.service();

    if (g_wasConnected) {
      g_wasConnected = false;
      g_bannerSent = false;
      Gpio::write(kPinUserLed, true);
      Serial.print("\r\nBLE client disconnected\r\n");
    }

    // Only pace the advertising loop when we are still disconnected.
    // Sleeping here after advertiseInteractEvent() accepted a CONNECT_IND
    // delays the first pollConnectionEvent() by ~20 ms, causing Android
    // (which often uses a 7.5 ms connection interval) to miss several
    // connection events before synchronisation is established.
    if (!g_ble.isConnected()) {
      delay(20);
    }
    return;
  }

  if (!g_wasConnected) {
    g_wasConnected = true;
    g_bannerSent = false;
    Gpio::write(kPinUserLed, false);
    Serial.print("\r\nBLE client connected\r\n");
  }

  // Keep BLE connection-event polling tight; missing anchors leads to 0x08 timeouts.
  BleConnectionEvent evt{};
  const bool eventStarted = g_ble.pollConnectionEvent(&evt, 300000UL) && evt.eventStarted;
  if (!eventStarted) {
    g_nus.service();
    // Keep UART flowing between connection events without doing a long, blocking pump.
    pumpUsbToBle(2);
    pumpBleToUsb(2);
    return;
  }

  g_nus.service(&evt);
  if (evt.terminateInd) {
    Serial.print("BLE link terminated");
    if (evt.disconnectReasonValid) {
      Serial.print(" reason=0x");
      if (evt.disconnectReason < 0x10U) {
        Serial.print('0');
      }
      Serial.print(evt.disconnectReason, HEX);
      Serial.print(" (");
      Serial.print(disconnectReasonLabel(evt.disconnectReason));
      Serial.print(", ");
      Serial.print(evt.disconnectReasonRemote ? "peer" : "local");
      Serial.print(")");
    }
    Serial.print("\r\n");
  }

  // Only pump UART after a real connection event.
  selfTestTx();
  if (!kEnableSelfTestTx) {
    pumpUsbToBle(16);
  }
  pumpBleToUsb(16);

  if (!g_bannerSent && g_nus.isNotifyEnabled()) {
    g_bannerSent = true;
    g_nus.print("X54 Nordic UART bridge ready\r\n");
  }

  const uint32_t nowMs = millis();
  if ((nowMs - g_lastStatusMs) >= 2000UL) {
    g_lastStatusMs = nowMs;
    Serial.print("notify=");
    Serial.print(g_nus.isNotifyEnabled() ? "on" : "off");
    Serial.print(" rx_drop=");
    Serial.print(g_nus.rxDroppedBytes());
    Serial.print(" tx_drop=");
    Serial.print(g_nus.txDroppedBytes());
    Serial.print(" usb_drop=");
    Serial.print(g_usbDroppedBytes);
    Serial.print("\r\n");
  }

  // No delay(): we want loop() to spin quickly so pollConnectionEvent() lands on anchors.
}
