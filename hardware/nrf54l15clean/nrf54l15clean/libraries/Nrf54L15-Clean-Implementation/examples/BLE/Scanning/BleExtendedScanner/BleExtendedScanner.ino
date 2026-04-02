#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

/*
 * BleExtendedScanner
 *
 * Passive scanner for BLE 5 extended advertising packets (ADV_EXT_IND +
 * AUX_ADV_IND + up to three AUX_CHAIN_IND follow-ups). Only LE 1M is
 * scanned; no SCAN_REQ is transmitted (passive).
 *
 * Pair with BleExtendedAdv251, BleExtendedAdv499, or BleExtendedAdv995
 * on a second board. The scanner reports:
 *   - sid, did: Set ID and Data Info from the primary PDU.
 *   - aux_ch, aux_off_us: secondary channel and offset in microseconds.
 *   - chain_pkts: number of AUX_CHAIN_IND packets in the received chain.
 *   - data_len: total assembled payload length in bytes.
 *   - name: extracted from AD type 0x08/0x09 if present.
 *
 * Note: unlike BleExtendedActiveScanner, this sketch never sends a
 * SCAN_REQ, so it cannot receive AUX_SCAN_RSP from scannable advertisers.
 */

// Passive extended advertising scanner example.
//
// Current scope intentionally matches the TX side:
// - primary ADV_EXT_IND on channels 37/38/39
// - AUX_ADV_IND plus up to three AUX_CHAIN_IND follow-ups
// - LE 1M only
// - non-connectable, non-scannable extended payloads

static BleRadio g_ble;
static bool g_bleReady = false;
static uint32_t g_hits = 0;
static uint32_t g_misses = 0;
static uint32_t g_lastStatusMs = 0;

// TX power programmed into the radio hardware at init (scanner is RX-only here).
static constexpr int8_t kTxPowerDbm = -8;
// How long to listen on each primary channel (37/38/39) for ADV_EXT_IND (us).
static constexpr uint32_t kPrimaryListenSpinPerChannel = 1200000UL;
// After seeing ADV_EXT_IND, how long to wait for AUX_ADV_IND and chain (us).
static constexpr uint32_t kSecondaryListenSpin = 350000UL;

static void printAddress(const uint8_t* addr) {
  char addressText[kBleAddressStringLength] = {0};
  if (formatBleAddressString(addr, addressText, sizeof(addressText)) == 0U) {
    return;
  }
  Serial.print(addressText);
}

static size_t extractLocalNameFromAdData(const uint8_t* payload,
                                         uint16_t payloadLen,
                                         char* out,
                                         size_t outSize) {
  if (out == nullptr || outSize == 0U) {
    return 0U;
  }
  out[0] = '\0';
  if (payload == nullptr || payloadLen == 0U) {
    return 0U;
  }

  const uint8_t* p = payload;
  uint16_t rem = payloadLen;
  while (rem > 1U) {
    const uint8_t fieldLen = p[0];
    if (fieldLen == 0U) {
      break;
    }
    if (fieldLen > static_cast<uint8_t>(rem - 1U)) {
      break;
    }

    const uint8_t type = p[1];
    if (type == 0x08U || type == 0x09U) {
      const uint8_t valueLen = static_cast<uint8_t>(fieldLen - 1U);
      const size_t copyLen =
          (valueLen < static_cast<uint8_t>(outSize - 1U))
              ? static_cast<size_t>(valueLen)
              : (outSize - 1U);
      if (copyLen > 0U) {
        memcpy(out, &p[2], copyLen);
      }
      out[copyLen] = '\0';
      return copyLen;
    }

    const uint16_t step = static_cast<uint16_t>(fieldLen + 1U);
    p += step;
    rem = static_cast<uint16_t>(rem - step);
  }
  return 0U;
}

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nBleExtendedScanner start\r\n");
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  const bool ok = g_ble.begin(kTxPowerDbm);
  g_bleReady = ok;
  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (!ok) {
    Serial.print("Hint: enable Tools -> BLE Support = Enabled\r\n");
  }
}

void loop() {
  if (!g_bleReady) {
    const uint32_t now = millis();
    if ((now - g_lastStatusMs) >= 2000UL) {
      g_lastStatusMs = now;
      Serial.print("BLE init not ready; extended scanner idle\r\n");
    }
    delay(50);
    return;
  }

  BleExtendedScanResult result{};
  // scanExtendedCycle() listens passively on channels 37/38/39 for
  // ADV_EXT_IND, then follows the AuxPtr to receive AUX_ADV_IND and any
  // AUX_CHAIN_IND packets. Returns true if a complete event was captured.
  const bool got = g_ble.scanExtendedCycle(&result, kPrimaryListenSpinPerChannel,
                                           kSecondaryListenSpin);

  if (!got) {
    ++g_misses;
    const uint32_t now = millis();
    if ((now - g_lastStatusMs) >= 1000UL) {
      g_lastStatusMs = now;
      Serial.print("ext scanning... hits=");
      Serial.print(g_hits);
      Serial.print(" misses=");
      Serial.print(g_misses);
      Serial.print("\r\n");
    }
    delay(1);
    return;
  }

  ++g_hits;
  Gpio::write(kPinUserLed, (g_hits & 0x1U) == 0U);

  char name[33] = {0};
  (void)extractLocalNameFromAdData(result.data, result.dataLength, name, sizeof(name));

  Serial.print("#");
  Serial.print(g_hits);
  Serial.print(" ch=");
  Serial.print(static_cast<uint8_t>(result.primaryChannel));
  Serial.print(" rssi=");
  Serial.print(result.primaryRssiDbm);
  Serial.print(" advA=");
  printAddress(result.advertiserAddress);
  Serial.print(" sid=");
  Serial.print(result.sid);
  Serial.print(" did=0x");
  Serial.print(result.did, HEX);
  Serial.print(" aux_ch=");
  Serial.print(result.auxChannel);
  Serial.print(" aux_off_us=");
  Serial.print(result.auxOffsetUs);
  Serial.print(" chain_pkts=");
  Serial.print(result.secondaryPacketCount);
  Serial.print(" data_len=");
  Serial.print(result.dataLength);
  if (name[0] != '\0') {
    Serial.print(" name=");
    Serial.print(name);
  }
  Serial.print("\r\n");
}
