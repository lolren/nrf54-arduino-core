#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_advEvents = 0U;
static uint32_t g_linkEvents = 0U;
static uint32_t g_rxPacketsOk = 0U;
static uint32_t g_rxCrcFail = 0U;
static uint32_t g_rxTimeoutEvents = 0U;
static uint32_t g_txTimeoutEvents = 0U;
static uint32_t g_llCtrlPackets = 0U;
static uint32_t g_attPackets = 0U;
static uint32_t g_encryptedEvents = 0U;
static uint32_t g_lastWindowMs = 0U;
static bool g_connectionAnnounced = false;
static constexpr int8_t kTxPowerDbm = -8;

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

static const char* timingProfileName() {
#if defined(NRF54L15_CLEAN_BLE_TIMING_AGGRESSIVE) && (NRF54L15_CLEAN_BLE_TIMING_AGGRESSIVE == 1)
  return "aggressive";
#elif defined(NRF54L15_CLEAN_BLE_TIMING_BALANCED) && (NRF54L15_CLEAN_BLE_TIMING_BALANCED == 1)
  return "balanced";
#else
  return "interop";
#endif
}

static void printWindowAndReset(uint32_t nowMs) {
  char line[256];
  snprintf(line, sizeof(line),
           "window_ms=%lu adv=%lu link=%lu rx_ok=%lu crc_fail=%lu rx_timeout=%lu tx_timeout=%lu ll=%lu att=%lu enc=%lu\r\n",
           static_cast<unsigned long>(nowMs - g_lastWindowMs),
           static_cast<unsigned long>(g_advEvents),
           static_cast<unsigned long>(g_linkEvents),
           static_cast<unsigned long>(g_rxPacketsOk),
           static_cast<unsigned long>(g_rxCrcFail),
           static_cast<unsigned long>(g_rxTimeoutEvents),
           static_cast<unsigned long>(g_txTimeoutEvents),
           static_cast<unsigned long>(g_llCtrlPackets),
           static_cast<unsigned long>(g_attPackets),
           static_cast<unsigned long>(g_encryptedEvents));
  Serial.print(line);

  g_advEvents = 0U;
  g_linkEvents = 0U;
  g_rxPacketsOk = 0U;
  g_rxCrcFail = 0U;
  g_rxTimeoutEvents = 0U;
  g_txTimeoutEvents = 0U;
  g_llCtrlPackets = 0U;
  g_attPackets = 0U;
  g_encryptedEvents = 0U;
  g_lastWindowMs = nowMs;
}

void setup() {
  Serial.begin(115200);
  delay(350);

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::write(kPinUserLed, true);

  static const uint8_t kAddress[6] = {0x71, 0x00, 0x15, 0x54, 0xDE, 0xC0};
  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic) &&
         g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingName("XIAO54-TIMING", true) &&
         g_ble.setScanResponseName("XIAO54-TIMING-SCAN") &&
         g_ble.setGattDeviceName("XIAO54-TIMING") &&
         g_ble.setGattBatteryLevel(100U);
  }

  Serial.print("\r\nBleConnectionTimingMetrics start profile=");
  Serial.print(timingProfileName());
  Serial.print(" tx_dbm=");
  Serial.print(static_cast<int>(kTxPowerDbm));
  Serial.print(" init=");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");

  g_lastWindowMs = millis();
}

void loop() {
  const uint32_t nowMs = millis();

  if (!g_ble.isConnected()) {
    g_connectionAnnounced = false;
    BleAdvInteraction adv{};
    (void)g_ble.advertiseInteractEvent(&adv, 350U, 300000UL, 700000UL);
    ++g_advEvents;

    if (adv.receivedConnectInd) {
      // Avoid extra logging work between CONNECT_IND and first data events.
      g_lastWindowMs = nowMs;
      (void)Gpio::write(kPinUserLed, true);
      delay(1);
      return;
    }

    if ((nowMs - g_lastWindowMs) >= 5000UL) {
      printWindowAndReset(nowMs);
    }
    (void)Gpio::write(kPinUserLed, true);
    delay(1);
    return;
  }

  BleConnectionEvent evt{};
  const bool ran = g_ble.pollConnectionEvent(&evt, 450000UL);
  if (ran && evt.eventStarted) {
    if (!g_connectionAnnounced) {
      BleConnectionInfo info{};
      if (g_ble.getConnectionInfo(&info)) {
        Serial.print("connected peer=");
        printAddress(info.peerAddress);
        Serial.print(" int=");
        Serial.print(info.intervalUnits);
        Serial.print(" lat=");
        Serial.print(info.latency);
        Serial.print(" timeout=");
        Serial.print(info.supervisionTimeoutUnits);
        Serial.print("\r\n");
      }
      g_connectionAnnounced = true;
    }

    ++g_linkEvents;
    if (g_ble.isConnectionEncrypted()) {
      ++g_encryptedEvents;
    }

    if (!evt.packetReceived) {
      ++g_rxTimeoutEvents;
    } else if (!evt.crcOk) {
      ++g_rxCrcFail;
    } else {
      ++g_rxPacketsOk;
      if (evt.llControlPacket) {
        ++g_llCtrlPackets;
      }
      if (evt.attPacket) {
        ++g_attPackets;
      }
    }

    if (!evt.txPacketSent) {
      ++g_txTimeoutEvents;
    }

    if (evt.terminateInd) {
      Serial.print("link terminated\r\n");
    }

    (void)Gpio::write(kPinUserLed, false);
  } else {
    (void)Gpio::write(kPinUserLed, true);
    delay(1);
  }

  if ((nowMs - g_lastWindowMs) >= 5000UL) {
    printWindowAndReset(nowMs);
  }
}
