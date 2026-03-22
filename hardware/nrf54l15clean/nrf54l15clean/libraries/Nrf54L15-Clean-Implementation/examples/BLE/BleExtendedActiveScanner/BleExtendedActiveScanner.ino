#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

/*
 * BleExtendedActiveScanner
 *
 * Scans for BLE 5 extended advertising packets and, when a scannable extended
 * advertiser is found, transmits an AUX_SCAN_REQ and waits for AUX_SCAN_RSP.
 * Received data (including the scan response payload) is printed to Serial.
 *
 * This is the active counterpart of BleExtendedScanner. Use it to test the
 * BleExtendedScannableAdv251 sketch on a second board.
 *
 * Key result fields printed per hit:
 *   sid        – Advertising Set ID (0–15) from the primary PDU.
 *   did        – Advertising Data Info; changes each time the payload changes.
 *   aux_ch     – Secondary channel where AUX packets were received.
 *   scan_rsp   – yes/no whether an AUX_SCAN_RSP was received.
 *   scan_rsp_len – Byte count of the assembled scan response payload.
 *   name       – Extracted from AD type 0x08/0x09 in the scan response.
 */

// Active extended advertising scanner example.
//
// Current scope:
// - primary ADV_EXT_IND on channels 37/38/39
// - one AUX_ADV_IND on a fixed secondary channel
// - one AUX_SCAN_RSP plus up to three AUX_CHAIN_IND follow-ups
// - LE 1M only
// - scannable, non-connectable extended advertisers

static BleRadio g_ble;
static bool g_bleReady = false;
static uint32_t g_hits = 0;
static uint32_t g_misses = 0;
static uint32_t g_lastStatusMs = 0;

// TX power used when transmitting AUX_SCAN_REQ to the advertiser.
static constexpr int8_t kTxPowerDbm = -8;
// How long to listen on each primary channel (37/38/39) for ADV_EXT_IND (us).
// Longer = more chances to catch an event; shorter = lower latency.
static constexpr uint32_t kPrimaryListenSpinPerChannel = 1200000UL;
// After seeing an ADV_EXT_IND, how long to wait for the AUX_ADV_IND (us).
static constexpr uint32_t kSecondaryListenSpin = 350000UL;
// After sending AUX_SCAN_REQ, how long to wait for AUX_SCAN_RSP (us).
static constexpr uint32_t kScanRspListenSpin = 350000UL;

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

  Serial.print("\r\nBleExtendedActiveScanner start\r\n");
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
      Serial.print("BLE init not ready; extended active scanner idle\r\n");
    }
    delay(50);
    return;
  }

  BleExtendedScanResult result{};
  // scanExtendedActiveCycle() listens on primary channels, follows any
  // ADV_EXT_IND pointer to the secondary channel, and if the advertiser is
  // scannable, sends AUX_SCAN_REQ and waits for AUX_SCAN_RSP.
  // Returns true if a complete extended advertising event was captured.
  const bool got = g_ble.scanExtendedActiveCycle(&result,
                                                 kPrimaryListenSpinPerChannel,
                                                 kSecondaryListenSpin,
                                                 kScanRspListenSpin);

  if (!got) {
    ++g_misses;
    const uint32_t now = millis();
    if ((now - g_lastStatusMs) >= 1000UL) {
      g_lastStatusMs = now;
      Serial.print("ext active scanning... hits=");
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
  (void)extractLocalNameFromAdData(result.scanRspData, result.scanRspDataLength,
                                   name, sizeof(name));

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
  Serial.print(" adv_mode=");
  Serial.print(result.advMode);
  Serial.print(" scan_rsp=");
  Serial.print(result.scanResponseReceived ? "yes" : "no");
  Serial.print(" scan_rsp_pkts=");
  Serial.print(result.scanRspSecondaryPacketCount);
  Serial.print(" scan_rsp_len=");
  Serial.print(result.scanRspDataLength);
  if (name[0] != '\0') {
    Serial.print(" name=");
    Serial.print(name);
  }
  Serial.print("\r\n");
}
