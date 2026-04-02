#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

/*
 * BleExtendedAdv499
 *
 * Extended advertising example that forces exactly one AUX_CHAIN_IND follow-up
 * packet. A single AUX_ADV_IND PDU can carry ~253 bytes of AD data; this
 * sketch targets exactly 499 bytes, which requires the core to split the
 * payload into two secondary packets.
 *
 * Packet chain on-air:
 *   ADV_EXT_IND (ch37/38/39) → AUX_ADV_IND (~253 bytes) → AUX_CHAIN_IND (~246 bytes)
 *
 * Pair with BleExtendedScanner to verify the full chain is received.
 * The scanner will report chain_pkts=2 for a correctly received event.
 *
 * Power note:
 *   begin() / end() are called every advertising cycle so that end() clears
 *   the GRTC ACTIVE flag before the WFI sleep window. Without this, the GRTC
 *   keeps the CPU domain active and WFI only reaches ~90 µA instead of the
 *   2–3 µA achievable with the flag cleared.
 */

// Chained extended advertising example.
//
// This sketch exceeds the single AUX_ADV_IND payload budget and forces the
// core to emit one AUX_CHAIN_IND after the first auxiliary packet.

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_lastLogMs = 0;
static uint32_t g_extEvents = 0;
static uint8_t g_extData[kBleExtendedAdvDataMaxLength];
static size_t g_extDataLen = 0U;

static constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
static constexpr int8_t kTxPowerDbm = 0;
static constexpr uint8_t kAdvertisingSid = 4U;
static constexpr uint8_t kAuxChannel = 24U;
static constexpr uint32_t kAuxOffsetUs = 3000UL;
static constexpr uint32_t kInterPrimaryDelayUs = 350UL;
static constexpr uint32_t kAdvertisingIntervalMs = 120UL;
static constexpr uint32_t kSpinLimit = 900000UL;
static constexpr uint16_t kCompanyId = 0x3154U;
static constexpr char kName[] = "X54-EXT-499";
static constexpr uint8_t kAddress[6] = {0x52, 0x00, 0x15, 0x54, 0xDE, 0xC0};

static void collapseRfPathIdle() {
  BoardControl::collapseRfPathIdle();
}

static bool enableRfPath() {
  return BoardControl::enableRfPath(kAntennaPath);
}

static void configureBoardForBleLowPower() {
  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  collapseRfPathIdle();
}

static bool appendAdField(uint8_t type, const uint8_t* value, size_t valueLen) {
  if ((valueLen > 0U) && (value == nullptr)) {
    return false;
  }
  if (valueLen > 254U) {
    return false;
  }
  if ((g_extDataLen + valueLen + 2U) > sizeof(g_extData)) {
    return false;
  }

  g_extData[g_extDataLen++] = static_cast<uint8_t>(valueLen + 1U);
  g_extData[g_extDataLen++] = type;
  if (valueLen > 0U) {
    memcpy(&g_extData[g_extDataLen], value, valueLen);
    g_extDataLen += valueLen;
  }
  return true;
}

static bool appendPatternField(uint8_t type, size_t valueLen, uint8_t seed) {
  if ((valueLen < 2U) || (valueLen > 254U)) {
    return false;
  }

  uint8_t value[254];
  value[0] = static_cast<uint8_t>(kCompanyId & 0xFFU);
  value[1] = static_cast<uint8_t>((kCompanyId >> 8U) & 0xFFU);
  for (size_t i = 2U; i < valueLen; ++i) {
    value[i] = static_cast<uint8_t>(seed + ((i - 2U) % 26U));
  }
  return appendAdField(type, value, valueLen);
}

static bool buildExtendedPayload() {
  g_extDataLen = 0U;

  const uint8_t flags = 0x06U;
  if (!appendAdField(0x01U, &flags, 1U)) {
    return false;
  }

  if (!appendAdField(0x09U, reinterpret_cast<const uint8_t*>(kName),
                     strlen(kName))) {
    return false;
  }

  return appendPatternField(0xFFU, 239U, static_cast<uint8_t>('a')) &&
         appendPatternField(0xFFU, 240U, static_cast<uint8_t>('A')) &&
         (g_extDataLen == 499U);
}

static void printAddress(const uint8_t* addr) {
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

static bool advertiseOnce() {
  bool ok = enableRfPath();
  bool bleBegun = false;
  if (ok) {
    bleBegun = true;
    ok = g_ble.begin(kTxPowerDbm);
  }
  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic);
  }
  if (ok) {
    ok = g_ble.setExtendedAdvertisingSid(kAdvertisingSid);
  }
  if (ok) {
    ok = g_ble.setExtendedAdvertisingAuxChannel(kAuxChannel);
  }
  if (ok) {
    ok = g_ble.setExtendedAdvertisingData(g_extData, g_extDataLen);
  }
  if (ok) {
    ok = g_ble.advertiseExtendedEvent(kAuxOffsetUs, kInterPrimaryDelayUs,
                                      kSpinLimit);
  }
  if (bleBegun) {
    g_ble.end();  // clears GRTC ACTIVE → WFI reaches 2–3 µA
  }
  collapseRfPathIdle();
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nBleExtendedAdv499 start\r\n");
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  configureBoardForBleLowPower();
  g_power.setLatencyMode(PowerLatencyMode::kLowPower);

  if (!buildExtendedPayload()) {
    Serial.print("payload build failed\r\n");
    return;
  }

  Serial.print("addr=");
  printAddress(kAddress);
  Serial.print(" type=random\r\n");
  Serial.print("ext_adv_data_len=");
  Serial.print(g_extDataLen);
  Serial.print(" max=");
  Serial.print(kBleExtendedAdvDataMaxLength);
  Serial.print(" sid=");
  Serial.print(kAdvertisingSid);
  Serial.print(" aux_channel=");
  Serial.print(kAuxChannel);
  Serial.print(" chain_expected=yes interval_ms=");
  Serial.print(kAdvertisingIntervalMs);
  Serial.print("\r\n");
}

void loop() {
  const uint32_t cycleStart = millis();

  const bool ok = advertiseOnce();
  ++g_extEvents;

  Gpio::write(kPinUserLed, (g_extEvents & 0x1U) == 0U);

  if ((cycleStart - g_lastLogMs) >= 1000UL) {
    g_lastLogMs = cycleStart;

    char line[176];
    snprintf(line, sizeof(line),
             "t=%lu ext_events=%lu last=%s ext_len=%u sid=%u aux_ch=%u chain=yes\r\n",
             static_cast<unsigned long>(cycleStart),
             static_cast<unsigned long>(g_extEvents),
             ok ? "OK" : "FAIL",
             static_cast<unsigned>(g_extDataLen),
             static_cast<unsigned>(kAdvertisingSid),
             static_cast<unsigned>(kAuxChannel));
    Serial.print(line);
  }

  // Sleep until the next advertising window.
  // GRTC ACTIVE is cleared by end() above so WFI reaches 2–3 µA here.
  const uint32_t deadline = cycleStart + kAdvertisingIntervalMs;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    __asm volatile("wfi");
  }
}
