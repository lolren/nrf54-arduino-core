#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

// Active scanner example.
//
// This sketch scans for legacy advertisers and, when possible, requests and
// logs the scan response. It is useful for checking whether names live in the
// primary ADV payload or only in the SCAN_RSP payload.

static BleRadio g_ble;
static bool g_bleReady = false;
static uint32_t g_hits = 0;
static uint32_t g_misses = 0;
static uint32_t g_lastStatusMs = 0;

// Core-specific scan timing knobs:
// - ADV listen budget per advertising channel
// - extra scan-response listen budget after a matching ADV packet
static constexpr int8_t kTxPowerDbm = -8;
static constexpr uint32_t kAdvListenSpinPerChannel = 1200000UL;
static constexpr uint32_t kScanRspListenSpin = 250000UL;

static const char* pduTypeName(uint8_t type) {
  switch (type & 0x0FU) {
    case 0x00:
      return "ADV_IND";
    case 0x01:
      return "ADV_DIRECT";
    case 0x02:
      return "ADV_NONCONN";
    case 0x03:
      return "SCAN_REQ";
    case 0x04:
      return "SCAN_RSP";
    case 0x05:
      return "CONNECT_IND";
    case 0x06:
      return "ADV_SCAN";
    default:
      return "OTHER";
  }
}

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

static size_t extractLocalNameFromAdvPayload(const uint8_t* payloadWithAddress,
                                             uint8_t payloadLen,
                                             char* out,
                                             size_t outSize) {
  if (out == nullptr || outSize == 0U) {
    return 0U;
  }
  out[0] = '\0';
  if (payloadWithAddress == nullptr || payloadLen <= 6U) {
    return 0U;
  }

  const uint8_t* p = payloadWithAddress + 6U;
  uint8_t rem = static_cast<uint8_t>(payloadLen - 6U);
  while (rem > 1U) {
    const uint8_t fieldLen = p[0];
    if (fieldLen == 0U) {
      break;
    }
    if (fieldLen > rem - 1U) {
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

    const uint8_t step = static_cast<uint8_t>(fieldLen + 1U);
    p += step;
    rem = static_cast<uint8_t>(rem - step);
  }
  return 0U;
}

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nBleActiveScanner start\r\n");
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  // Scanner sensitivity and timing are raw-core choices here, not controller
  // defaults from a full BLE host stack.
  g_bleReady = g_ble.begin(kTxPowerDbm);
  Serial.print("BLE init: ");
  Serial.print(g_bleReady ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (!g_bleReady) {
    Serial.print("Hint: enable Tools -> BLE Support = Enabled\r\n");
  }
}

void loop() {
  if (!g_bleReady) {
    const uint32_t now = millis();
    if ((now - g_lastStatusMs) >= 2000UL) {
      g_lastStatusMs = now;
      Serial.print("BLE init not ready; scanner idle\r\n");
    }
    delay(50);
    return;
  }

  BleActiveScanResult result{};
  const bool got = g_ble.scanActiveCycle(&result, kAdvListenSpinPerChannel,
                                         kScanRspListenSpin);
  if (!got) {
    ++g_misses;
    const uint32_t now = millis();
    if ((now - g_lastStatusMs) >= 1000UL) {
      g_lastStatusMs = now;
      Serial.print("active scanning... hits=");
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

  const uint8_t advType = static_cast<uint8_t>(result.advHeader & 0x0FU);
  char advName[33] = {0};
  char scanRspName[33] = {0};
  (void)extractLocalNameFromAdvPayload(result.advPayload, result.advPayloadLength,
                                       advName, sizeof(advName));
  if (result.scanResponseReceived) {
    (void)extractLocalNameFromAdvPayload(result.scanRspPayload,
                                         result.scanRspPayloadLength,
                                         scanRspName, sizeof(scanRspName));
  }

  Serial.print("#");
  Serial.print(g_hits);
  Serial.print(" ch=");
  Serial.print(static_cast<uint8_t>(result.channel));
  Serial.print(" adv_rssi=");
  Serial.print(result.advRssiDbm);
  Serial.print(" type=");
  Serial.print(pduTypeName(advType));
  Serial.print(" advA=");
  printAddress(result.advertiserAddress);
  Serial.print(" adv_len=");
  Serial.print(result.advPayloadLength);
  if (advName[0] != '\0') {
    Serial.print(" adv_name=");
    Serial.print(advName);
  }

  if (result.scanResponseReceived) {
    Serial.print(" scan_rsp=1");
    Serial.print(" scan_rssi=");
    Serial.print(result.scanRspRssiDbm);
    Serial.print(" scan_len=");
    Serial.print(result.scanRspPayloadLength);
    if (scanRspName[0] != '\0') {
      Serial.print(" scan_name=");
      Serial.print(scanRspName);
    }
  } else {
    Serial.print(" scan_rsp=0");
  }
  Serial.print("\r\n");
}
