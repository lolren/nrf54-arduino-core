#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

/*
 * BleExtendedAdv251
 *
 * Minimal BLE 5 extended advertising example that fits within a single
 * AUX_ADV_IND packet (no chain). The payload is ~251 bytes including a
 * manufacturer-specific AD field – far beyond the 31-byte legacy limit.
 *
 * Packet chain on-air:
 *   ADV_EXT_IND (ch37/38/39) → AUX_ADV_IND (one secondary channel)
 *
 * Because no chaining is needed, this is the simplest extended advertising
 * configuration. Use it to verify your scanner supports extended advertising
 * before moving to BleExtendedAdv499 (2 packets) or BleExtendedAdv995 (4).
 *
 * Pair with BleExtendedScanner to receive and decode the payload.
 *
 * Power note:
 *   begin() / end() are called every advertising cycle so that end() clears
 *   the GRTC ACTIVE flag before the WFI sleep window. Without this, the GRTC
 *   keeps the CPU domain active and WFI only reaches ~90 µA instead of the
 *   2–3 µA achievable with the flag cleared.
 */

// Minimal extended advertising example.
//
// This stays within the single-AUX packet budget so the controller only emits:
// - primary ADV_EXT_IND on channels 37/38/39
// - one AUX_ADV_IND on a fixed secondary channel
// - LE 1M PHY only
// - non-connectable, non-scannable

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_lastLogMs = 0;
static uint32_t g_extEvents = 0;
static uint8_t g_extData[kBleExtendedAdvDataMaxLength];
static size_t g_extDataLen = 0U;

static constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
static constexpr int8_t kTxPowerDbm = 0;
static constexpr uint8_t kAdvertisingSid = 3U;
static constexpr uint8_t kAuxChannel = 20U;
static constexpr uint32_t kAuxOffsetUs = 3000UL;
static constexpr uint32_t kInterPrimaryDelayUs = 350UL;
static constexpr uint32_t kAdvertisingIntervalMs = 120UL;
static constexpr uint32_t kSpinLimit = 900000UL;
static constexpr uint16_t kCompanyId = 0x3154U;
static constexpr char kName[] = "X54-EXT-ADV";
// Human-readable BLE address. The helper converts this into the raw byte order
// used by the low-level radio code.
static constexpr char kAddressText[] = "C0:DE:54:15:00:51";

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

  uint8_t manufacturer[220];
  manufacturer[0] = static_cast<uint8_t>(kCompanyId & 0xFFU);
  manufacturer[1] = static_cast<uint8_t>((kCompanyId >> 8U) & 0xFFU);
  for (size_t i = 2U; i < sizeof(manufacturer); ++i) {
    manufacturer[i] = static_cast<uint8_t>('A' + ((i - 2U) % 26U));
  }

  if (!appendAdField(0xFFU, manufacturer, sizeof(manufacturer))) {
    return false;
  }

  return (g_extDataLen > kBleLegacyAdDataMaxLength);
}

// Bring up BLE, fire one extended advertising event, then shut down.
// end() clears the GRTC ACTIVE flag so subsequent WFI enters deep sleep.
static bool advertiseOnce() {
  bool ok = enableRfPath();
  bool bleBegun = false;
  if (ok) {
    bleBegun = true;
    ok = g_ble.begin(kTxPowerDbm);
  }
  if (ok) {
    ok = g_ble.setDeviceAddressString(kAddressText, BleAddressType::kRandomStatic);
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

  Serial.print("\r\nBleExtendedAdv251 start\r\n");
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  configureBoardForBleLowPower();
  g_power.setLatencyMode(PowerLatencyMode::kLowPower);

  if (!buildExtendedPayload()) {
    Serial.print("payload build failed\r\n");
    return;
  }

  Serial.print("ext_adv_data_len=");
  Serial.print(g_extDataLen);
  Serial.print(" max=");
  Serial.print(kBleExtendedAdvDataMaxLength);
  Serial.print(" sid=");
  Serial.print(kAdvertisingSid);
  Serial.print(" aux_channel=");
  Serial.print(kAuxChannel);
  Serial.print(" aux_offset_us=");
  Serial.print(kAuxOffsetUs);
  Serial.print(" interval_ms=");
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

    char line[160];
    snprintf(line, sizeof(line),
             "t=%lu ext_events=%lu last=%s ext_len=%u sid=%u aux_ch=%u\r\n",
             static_cast<unsigned long>(cycleStart),
             static_cast<unsigned long>(g_extEvents),
             ok ? "OK" : "FAIL",
             static_cast<unsigned>(g_extDataLen),
             static_cast<unsigned>(kAdvertisingSid),
             static_cast<unsigned>(kAuxChannel));
    Serial.print(line);
  }

  // Sleep until the next advertising window.
  // GRTC ACTIVE is cleared by end() above so WFI reaches 2–3 µA here
  // instead of the ~90 µA seen when BLE is left open between events.
  const uint32_t deadline = cycleStart + kAdvertisingIntervalMs;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    __asm volatile("wfi");
  }
}
