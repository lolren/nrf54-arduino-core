#include <Arduino.h>

#include "ble_nus.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static BleNordicUart g_nus(g_ble);
static PowerManager g_power;

static bool g_wasConnected = false;
static bool g_bannerSent = false;

static constexpr int8_t kTxPowerDbm = 0;
static const uint8_t kAddress[6] = {0x35, 0x00, 0x15, 0x54, 0xDE, 0xC0};
static const uint8_t kNusScanResponse[] = {
    17, 0x07,
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E};

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
  if (!g_nus.isNotifyEnabled()) {
    return;
  }

  int budget = g_nus.availableForWrite();
  if (budget > maxBytes) {
    budget = maxBytes;
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

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nBleNordicUartBridge start\r\n");

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  bool ok = BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
  if (ok) {
    ok = g_ble.begin(kTxPowerDbm) &&
         g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingName("X54-NUS", true) &&
         g_ble.setScanResponseData(kNusScanResponse, sizeof(kNusScanResponse)) &&
         g_ble.setGattDeviceName("X54 NUS Bridge") &&
         g_ble.clearCustomGatt() && g_nus.begin();
  }

  Serial.print("BLE NUS init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (ok) {
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

    delay(20);
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
    // Keep UART RX flowing between connection events without doing a long, blocking pump.
    pumpUsbToBle(2);
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
  pumpUsbToBle(16);
  pumpBleToUsb(16);

  if (!g_bannerSent && g_nus.isNotifyEnabled()) {
    g_bannerSent = true;
    g_nus.print("X54 Nordic UART bridge ready\r\n");
  }
}
