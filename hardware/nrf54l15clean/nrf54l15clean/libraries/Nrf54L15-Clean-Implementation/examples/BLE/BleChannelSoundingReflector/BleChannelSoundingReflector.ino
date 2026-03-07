#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static bool g_ready = false;
static uint32_t g_lastLogMs = 0;
static uint32_t g_advEvents = 0;
static uint32_t g_scanReqCount = 0;
static uint32_t g_scanRspCount = 0;
static uint32_t g_chHits[3] = {0, 0, 0};
static int32_t g_chRssiSum[3] = {0, 0, 0};

static constexpr int8_t kTxPowerDbm = 8;
static constexpr uint32_t kInterChannelDelayUs = 350U;
static constexpr uint32_t kRequestListenSpin = 350000UL;
static constexpr uint32_t kRadioSpin = 700000UL;

// Random static address (MSB byte has top bits set).
static const uint8_t kReflectorAddress[6] = {0x5A, 0x15, 0x54, 0x15, 0xDE, 0xC0};

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

static int channelIndex(BleAdvertisingChannel channel) {
  switch (channel) {
    case BleAdvertisingChannel::k37:
      return 0;
    case BleAdvertisingChannel::k38:
      return 1;
    case BleAdvertisingChannel::k39:
      return 2;
    default:
      return -1;
  }
}

static void printChannelStats(uint8_t channelNum, uint32_t hits, int32_t rssiSum) {
  Serial.print(" ch");
  Serial.print(channelNum);
  Serial.print("=");
  Serial.print(hits);
  Serial.print("@");
  if (hits == 0U) {
    Serial.print("na");
    return;
  }
  const int32_t avg = rssiSum / static_cast<int32_t>(hits);
  Serial.print(avg);
  Serial.print("dBm");
}

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nBleChannelSoundingReflector start\r\n");
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    ok = g_ble.setDeviceAddress(kReflectorAddress, BleAddressType::kRandomStatic);
  }
  if (ok) {
    ok = g_ble.setAdvertisingPduType(BleAdvPduType::kAdvScanInd);
  }
  if (ok) {
    static const uint8_t kAdvPayload[] = {
        2, 0x01, 0x06,  // Flags
        13, 0x09, 'X', 'I', 'A', 'O', '-', 'C', 'S', '-', 'R', 'E', 'F', 'L',
        5, 0xFF, 0x34, 0x12, 0x43, 0x53  // MFG marker "CS"
    };
    ok = g_ble.setAdvertisingData(kAdvPayload, sizeof(kAdvPayload));
  }
  if (ok) {
    static const uint8_t kScanRspPayload[] = {
        11, 0x09, 'C', 'S', '-', 'R', 'E', 'F', 'L', 'E', 'C', 'T',
        4, 0x16, 0x15, 0x54, 0x01
    };
    ok = g_ble.setScanResponseData(kScanRspPayload, sizeof(kScanRspPayload));
  }
  if (ok) {
    ok = g_ble.buildAdvertisingPacket();
  }

  g_ready = ok;
  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  Serial.print("reflector_addr=");
  printAddress(kReflectorAddress);
  Serial.print(" tx_dbm=");
  Serial.print(static_cast<int>(kTxPowerDbm));
  Serial.print("\r\n");
  if (!ok) {
    Serial.print("Hint: enable Tools -> BLE Support = Enabled\r\n");
  }
}

void loop() {
  if (!g_ready) {
    delay(200);
    return;
  }

  BleAdvInteraction interaction{};
  (void)g_ble.advertiseInteractEvent(&interaction, kInterChannelDelayUs,
                                     kRequestListenSpin, kRadioSpin);
  ++g_advEvents;

  if (interaction.receivedScanRequest) {
    ++g_scanReqCount;
    if (interaction.scanResponseTransmitted) {
      ++g_scanRspCount;
    }

    const int idx = channelIndex(interaction.channel);
    if (idx >= 0 && idx < 3) {
      ++g_chHits[idx];
      g_chRssiSum[idx] += static_cast<int32_t>(interaction.rssiDbm);
    }

    Gpio::write(kPinUserLed, (g_scanReqCount & 0x1U) != 0U);
  }

  const uint32_t now = millis();
  if ((now - g_lastLogMs) >= 1000UL) {
    g_lastLogMs = now;
    Serial.print("t=");
    Serial.print(now);
    Serial.print(" adv=");
    Serial.print(g_advEvents);
    Serial.print(" scan_req=");
    Serial.print(g_scanReqCount);
    Serial.print(" scan_rsp=");
    Serial.print(g_scanRspCount);
    printChannelStats(37, g_chHits[0], g_chRssiSum[0]);
    printChannelStats(38, g_chHits[1], g_chRssiSum[1]);
    printChannelStats(39, g_chHits[2], g_chRssiSum[2]);
    Serial.print("\r\n");
  }

  delay(4);
}
