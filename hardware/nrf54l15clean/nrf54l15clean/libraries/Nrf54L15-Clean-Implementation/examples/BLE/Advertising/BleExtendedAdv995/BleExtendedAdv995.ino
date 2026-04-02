#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

/*
 * BleExtendedAdv995
 *
 * Extended advertising stress test: fills all 995 bytes of the current
 * controller's extended advertising data limit. The payload is split across
 * one AUX_ADV_IND and three AUX_CHAIN_IND follow-up packets.
 *
 * Packet chain on-air:
 *   ADV_EXT_IND (ch37/38/39)
 *     → AUX_ADV_IND       (~243 bytes)
 *     → AUX_CHAIN_IND #1  (~243 bytes)
 *     → AUX_CHAIN_IND #2  (~243 bytes)
 *     → AUX_CHAIN_IND #3  (~242 bytes + name + flags)
 *
 * This sketch is designed to be received by BleExtendedScanner which reports
 * the total data length and number of chain packets.
 *
 * Note: non-scannable means no SCAN_RSP is possible; all data is in the chain.
 *
 * Power note:
 *   begin() / end() are called every advertising cycle so that end() clears
 *   the GRTC ACTIVE flag before the WFI sleep window. Without this, the GRTC
 *   keeps the CPU domain active and WFI only reaches ~90 µA instead of the
 *   2–3 µA achievable with the flag cleared.
 */

// Multi-chain extended advertising example.
//
// This fills the current controller limit:
// - primary ADV_EXT_IND on channels 37/38/39
// - one AUX_ADV_IND
// - three AUX_CHAIN_IND payload follow-ups
// - LE 1M PHY only
// - non-connectable, non-scannable

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_lastLogMs = 0;
static uint32_t g_extEvents = 0;
static uint8_t g_extData[kBleExtendedAdvDataMaxLength];
static size_t g_extDataLen = 0U;

static constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
// 0 dBm gives good range for lab testing without saturating a nearby receiver.
static constexpr int8_t kTxPowerDbm = 0;
// SID 5 distinguishes this set from BleExtendedAdv251 (SID 6) and others.
static constexpr uint8_t kAdvertisingSid = 5U;
// Secondary channel 28 avoids collision with other extended advertising sketches.
static constexpr uint8_t kAuxChannel = 28U;
// Timing from last primary PDU to AUX_ADV_IND start (microseconds).
static constexpr uint32_t kAuxOffsetUs = 3000UL;
// Gap between primary PDUs on channels 37, 38, 39 (microseconds).
static constexpr uint32_t kInterPrimaryDelayUs = 350UL;
// Time between complete advertising events (milliseconds).
static constexpr uint32_t kAdvertisingIntervalMs = 120UL;
// Maximum time the driver will spin waiting for the radio to complete (us).
static constexpr uint32_t kSpinLimit = 900000UL;
// Placeholder company ID in manufacturer-specific AD fields. Use your own.
static constexpr uint16_t kCompanyId = 0x3154U;
static constexpr char kName[] = "X54-EXT-995";
static constexpr uint8_t kAddress[6] = {0x53, 0x00, 0x15, 0x54, 0xDE, 0xC0};

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

  return appendPatternField(0xFFU, 243U, static_cast<uint8_t>('A')) &&
         appendPatternField(0xFFU, 243U, static_cast<uint8_t>('a')) &&
         appendPatternField(0xFFU, 243U, static_cast<uint8_t>('0')) &&
         appendPatternField(0xFFU, 242U, static_cast<uint8_t>('k')) &&
         (g_extDataLen == kBleExtendedAdvDataMaxLength);
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

  Serial.print("\r\nBleExtendedAdv995 start\r\n");
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
  Serial.print(" chain_packets=4 interval_ms=");
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

    char line[184];
    snprintf(line, sizeof(line),
             "t=%lu ext_events=%lu last=%s ext_len=%u sid=%u aux_ch=%u chain_packets=4\r\n",
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
