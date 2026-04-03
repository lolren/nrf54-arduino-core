#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

/*
 * BleExtendedScannableAdv251
 *
 * BLE 5 extended advertising (Bluetooth 5.0 ADV_EXT_IND) with a scannable
 * payload. The scanner can ask for the AUX_SCAN_RSP which in this sketch
 * carries a ~251-byte manufacturer-specific payload – far beyond the 31-byte
 * legacy limit.
 *
 * Packet chain on-air:
 *   ADV_EXT_IND (ch 37/38/39)  →  AUX_ADV_IND (secondary channel)
 *                                         ↕ (if scanner sends AUX_SCAN_REQ)
 *                                  AUX_SCAN_RSP (same secondary channel)
 *
 * View the scan response data with BleExtendedActiveScanner or a phone app
 * that supports extended advertising (e.g., nRF Connect ≥ v4).
 *
 * Key constants:
 *   kAdvertisingSid – Set Identifier, 0-15, labels this advertising set.
 *   kAuxChannel     – The secondary channel number (0-36) for AUX packets.
 *   kAuxOffsetUs    – Timing offset from the last primary PDU to AUX_ADV_IND.
 *
 * Power note:
 *   begin() / end() are called every advertising cycle so that end() clears
 *   the GRTC ACTIVE flag before the WFI sleep window. Without this, the GRTC
 *   keeps the CPU domain active and WFI only reaches ~90 µA instead of the
 *   2–3 µA achievable with the flag cleared.
 */

// Minimal scannable extended advertising example.
//
// Current scope:
// - primary ADV_EXT_IND on channels 37/38/39
// - one AUX_ADV_IND on a fixed secondary channel
// - one AUX_SCAN_RSP payload on the same secondary channel
// - LE 1M only
// - non-connectable, scannable

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_lastLogMs = 0;
static uint32_t g_events = 0;
static uint8_t g_scanRspData[kBleExtendedAdvDataMaxLength];
static size_t g_scanRspDataLen = 0U;

static constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
// TX power in dBm (0 dBm is a good general-purpose value for desktop testing).
static constexpr int8_t kTxPowerDbm = 0;
// Advertising Set ID: a 4-bit value (0–15) that allows scanners to distinguish
// multiple simultaneous advertising sets from the same device.
static constexpr uint8_t kAdvertisingSid = 6U;
// Secondary channel for AUX packets. Valid data channels are 0–36.
// Choose a value that does not clash with other extended advertising examples
// running nearby (they use different channel numbers).
static constexpr uint8_t kAuxChannel = 21U;
// Time from last primary PDU to the AUX_ADV_IND transmission (microseconds).
// Must be long enough for the scanner to switch channels.
static constexpr uint32_t kAuxOffsetUs = 3000UL;
// Gap between primary PDU transmissions on ch37/38/39 (microseconds).
static constexpr uint32_t kInterPrimaryDelayUs = 350UL;
// How long to listen for an AUX_SCAN_REQ from a scanner (microseconds).
static constexpr uint32_t kRequestListenSpinLimit = 250000UL;
// Max wait for the radio to complete the full extended event (microseconds).
static constexpr uint32_t kSpinLimit = 900000UL;
// Advertising interval between events (milliseconds).
static constexpr uint32_t kAdvertisingIntervalMs = 120UL;
// Bluetooth company ID embedded in the manufacturer-specific AD field.
// 0x3154 is used as a placeholder here; use your own registered ID in products.
static constexpr uint16_t kCompanyId = 0x3154U;
static constexpr char kName[] = "X54-EXT-SCAN";
// Static random address. The two MSBs of the last byte must be 11b.
static constexpr uint8_t kAddress[6] = {0x61, 0x00, 0x15, 0x54, 0xDE, 0xC0};
  static constexpr bool kUseFixedAddress = false;  // Factory-derived BLE address is more reliable on phones.

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
  if ((g_scanRspDataLen + valueLen + 2U) > sizeof(g_scanRspData)) {
    return false;
  }

  g_scanRspData[g_scanRspDataLen++] = static_cast<uint8_t>(valueLen + 1U);
  g_scanRspData[g_scanRspDataLen++] = type;
  if (valueLen > 0U) {
    memcpy(&g_scanRspData[g_scanRspDataLen], value, valueLen);
    g_scanRspDataLen += valueLen;
  }
  return true;
}

static bool buildScanResponsePayload() {
  g_scanRspDataLen = 0U;

  if (!appendAdField(0x09U, reinterpret_cast<const uint8_t*>(kName), strlen(kName))) {
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

  return (g_scanRspDataLen > kBleLegacyAdDataMaxLength);
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

// Bring up BLE, fire one extended scannable advertising event, then shut down.
// end() clears the GRTC ACTIVE flag so subsequent WFI enters deep sleep.
static bool advertiseOnce() {
  bool ok = enableRfPath();
  bool bleBegun = false;
  if (ok) {
    bleBegun = true;
    ok = g_ble.begin(kTxPowerDbm);
  }
  if (ok) {
    ok = (!kUseFixedAddress || g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic));
  }
  if (ok) {
    // setExtendedAdvertisingSid() tags this set with a 4-bit SID so scanners
    // can correlate primary and secondary PDUs from the same set.
    ok = g_ble.setExtendedAdvertisingSid(kAdvertisingSid);
  }
  if (ok) {
    // setExtendedAdvertisingAuxChannel() tells the core which data channel
    // to use for AUX_ADV_IND and AUX_SCAN_RSP transmissions.
    ok = g_ble.setExtendedAdvertisingAuxChannel(kAuxChannel);
  }
  if (ok) {
    // setExtendedScanResponseData() provides the payload to transmit in
    // AUX_SCAN_RSP when a scanner sends an AUX_SCAN_REQ. This is the
    // mechanism that allows up to ~251 bytes in a "scan response" for
    // extended advertising (legacy allows only 31 bytes).
    ok = g_ble.setExtendedScanResponseData(g_scanRspData, g_scanRspDataLen);
  }
  if (ok) {
    ok = g_ble.advertiseExtendedScannableEvent(
        kAuxOffsetUs, kInterPrimaryDelayUs, kRequestListenSpinLimit,
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

  Serial.print("\r\nBleExtendedScannableAdv251 start\r\n");
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  configureBoardForBleLowPower();
  g_power.setLatencyMode(PowerLatencyMode::kLowPower);

  if (!buildScanResponsePayload()) {
    Serial.print("payload build failed\r\n");
    return;
  }

  Serial.print("scan_rsp_len=");
  Serial.print(g_scanRspDataLen);
  Serial.print(" sid=");
  Serial.print(kAdvertisingSid);
  Serial.print(" aux_channel=");
  Serial.print(kAuxChannel);
  Serial.print(" aux_offset_us=");
  Serial.print(kAuxOffsetUs);
  Serial.print(" interval_ms=");
  Serial.print(kAdvertisingIntervalMs);
  Serial.print("\r\n");

  Serial.print("addr=");
  printAddress(kAddress);
  Serial.print(" type=random\r\n");
}

void loop() {
  const uint32_t cycleStart = millis();

  const bool ok = advertiseOnce();
  ++g_events;

  Gpio::write(kPinUserLed, (g_events & 0x1U) == 0U);

  if ((cycleStart - g_lastLogMs) >= 1000UL) {
    g_lastLogMs = cycleStart;

    char line[160];
    snprintf(line, sizeof(line),
             "t=%lu ext_scannable_events=%lu last=%s scan_rsp_len=%u sid=%u aux_ch=%u\r\n",
             static_cast<unsigned long>(cycleStart),
             static_cast<unsigned long>(g_events),
             ok ? "OK" : "FAIL",
             static_cast<unsigned>(g_scanRspDataLen),
             static_cast<unsigned>(kAdvertisingSid),
             static_cast<unsigned>(kAuxChannel));
    Serial.print(line);
  }

  // GRTC ACTIVE cleared by end() above — WFI reaches 2–3 µA here
  const uint32_t deadline = cycleStart + kAdvertisingIntervalMs;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    __asm volatile("wfi");
  }
}
