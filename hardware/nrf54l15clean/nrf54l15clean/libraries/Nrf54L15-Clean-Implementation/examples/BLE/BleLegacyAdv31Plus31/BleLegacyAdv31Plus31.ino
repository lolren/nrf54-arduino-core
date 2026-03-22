#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

/*
 * BleLegacyAdv31Plus31
 *
 * Demonstrates the full legacy BLE advertising data budget:
 *   31 bytes of AdvData   (in ADV_SCAN_IND)
 *   31 bytes of ScanRsp   (in SCAN_RSP, sent on request)
 *
 * The PDU body is slightly larger on-air (AdvData + 6-byte AdvA = up to 37
 * bytes), but the AD-data area visible to the application is capped at 31.
 * This sketch fills both budgets so you can verify that a scanner sees all
 * 62 bytes of application data total.
 *
 * ADV_SCAN_IND is used instead of ADV_IND because it is the canonical type
 * for devices that want to be scannable but not connectable. A phone or the
 * BleActiveScanner example can request the scan response.
 *
 * Gotcha: every distinct sketch that uses custom raw payloads should have a
 * unique kAddress to prevent Android from returning a cached GATT service
 * table from a previous sketch flashed to the same board.
 */

// Legacy advertising budget example.
//
// Legacy BLE gives you:
// - up to 31 bytes of AdvData
// - up to 31 bytes of ScanRspData
//
// The on-air PDU body is larger (up to 37 bytes) because it also carries the
// advertiser address (AdvA, 6 bytes). This sketch fills both 31-byte AD-data
// budgets and listens for SCAN_REQ so the full 31 + 31 pattern can be tested
// with a phone scanner or the BleActiveScanner example.

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_advEvents = 0;
static uint32_t g_scanReqCount = 0;
static uint32_t g_scanRspCount = 0;
static uint32_t g_lastLogMs = 0;

// Antenna selection: kCeramic = on-board patch; kExternal = u.FL connector.
static constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
// 0 dBm: full power for reliable scan-response delivery at room distance.
static constexpr int8_t kTxPowerDbm = 0;
// 100 ms is a common advertising interval for interactive demo purposes.
static constexpr uint32_t kAdvIntervalMs = 100UL;
// Gap between primary PDU transmissions on ch37/38/39 (microseconds).
static constexpr uint32_t kInterChannelDelayUs = 350U;
// How long to wait for a SCAN_REQ from a scanner after the last primary PDU.
static constexpr uint32_t kRequestListenSpinLimit = 250000UL;
// Total spin limit for the full advertiseInteractEvent() call.
static constexpr uint32_t kSpinLimit = 900000UL;

static const uint8_t kAddress[6] = {0x31, 0x00, 0x15, 0x54, 0xDE, 0xC0};

static const uint8_t kAdvPayload[] = {
    2, 0x01, 0x06,
    9, 0x09, 'X', '5', '4', '-', '3', '1', '-', 'A',
    17, 0xFF,
    0x31, 0x31, 0x54, 0x15, 0xA0, 0xA1, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB,
};

static const uint8_t kScanRspPayload[] = {
    12, 0x09, 'X', '5', '4', '-', '3', '1', '-', 'S', 'C', 'A', 'N',
    17, 0x07,
    0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
};

static_assert(sizeof(kAdvPayload) <= kBleLegacyAdDataMaxLength,
              "Legacy AdvData budget must stay within 31 bytes");
static_assert(sizeof(kScanRspPayload) <= kBleLegacyAdDataMaxLength,
              "Legacy ScanRspData budget must stay within 31 bytes");

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

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nBleLegacyAdv31Plus31 start\r\n");
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  g_power.setLatencyMode(PowerLatencyMode::kLowPower);

  bool ok = BoardControl::setAntennaPath(kAntennaPath);
  if (ok) {
    ok = g_ble.begin(kTxPowerDbm);
  }
  if (ok) {
    ok = g_ble.setDeviceAddress(kAddress, BleAddressType::kRandomStatic);
  }
  if (ok) {
    // kAdvScanInd = ADV_SCAN_IND: scannable but NOT connectable.
    // A scanner can request the SCAN_RSP but cannot initiate a connection.
    // Use kAdvInd if you also want the device to be connectable.
    ok = g_ble.setAdvertisingPduType(BleAdvPduType::kAdvScanInd);
  }
  if (ok) {
    // setAdvertisingData() sets the raw AD-structure bytes for the primary
    // ADV_SCAN_IND payload. Up to 31 bytes.
    ok = g_ble.setAdvertisingData(kAdvPayload, sizeof(kAdvPayload));
  }
  if (ok) {
    // setScanResponseData() sets the raw AD bytes returned in SCAN_RSP.
    // Up to 31 bytes. Only transmitted when a scanner sends SCAN_REQ.
    ok = g_ble.setScanResponseData(kScanRspPayload, sizeof(kScanRspPayload));
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (!ok) {
    return;
  }

  Serial.print("addr=");
  printAddress(kAddress);
  Serial.print(" type=random\r\n");
  Serial.print("adv_data_len=");
  Serial.print(sizeof(kAdvPayload));
  Serial.print(" scan_rsp_data_len=");
  Serial.print(sizeof(kScanRspPayload));
  Serial.print(" raw_legacy_payload_len=");
  Serial.print(kBleLegacyRawPayloadMaxLength);
  Serial.print("\r\n");
}

void loop() {
  BleAdvInteraction interaction{};
  const bool ok = g_ble.advertiseInteractEvent(&interaction, kInterChannelDelayUs,
                                               kRequestListenSpinLimit,
                                               kSpinLimit);
  ++g_advEvents;
  if (interaction.receivedScanRequest) {
    ++g_scanReqCount;
  }
  if (interaction.scanResponseTransmitted) {
    ++g_scanRspCount;
  }

  Gpio::write(kPinUserLed, (g_advEvents & 0x1U) == 0U);

  const uint32_t now = millis();
  if ((now - g_lastLogMs) >= 1000UL) {
    g_lastLogMs = now;

    char line[192];
    snprintf(line, sizeof(line),
             "t=%lu adv_events=%lu scan_req=%lu scan_rsp=%lu last=%s type=ADV_SCAN_IND adv_len=%u scan_len=%u raw_pdu=%u\r\n",
             static_cast<unsigned long>(now),
             static_cast<unsigned long>(g_advEvents),
             static_cast<unsigned long>(g_scanReqCount),
             static_cast<unsigned long>(g_scanRspCount),
             ok ? "OK" : "FAIL",
             static_cast<unsigned>(sizeof(kAdvPayload)),
             static_cast<unsigned>(sizeof(kScanRspPayload)),
             static_cast<unsigned>(kBleLegacyRawPayloadMaxLength));
    Serial.print(line);

    if (interaction.receivedScanRequest) {
      Serial.print("last_scan_req peer=");
      printAddress(interaction.peerAddress);
      Serial.print(" addr_type=");
      Serial.print(interaction.peerAddressRandom ? "random" : "public");
      Serial.print("\r\n");
    }
  }

  delay(kAdvIntervalMs);
}
