#include <Arduino.h>

#include <math.h>

#include "nrf54l15_hal.h"

#if defined(NRF54L15_CLEAN_ZIGBEE_ENABLED) && (NRF54L15_CLEAN_ZIGBEE_ENABLED == 0)
#error "Enable Tools > Zigbee Support to build ZigbeePingInitiator."
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_CHANNEL
#define NRF54L15_CLEAN_ZIGBEE_CHANNEL 15
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_PAN_ID
#define NRF54L15_CLEAN_ZIGBEE_PAN_ID 0x1234
#endif

using namespace xiao_nrf54l15;

// Point-to-point Zigbee ping initiator demo.
//
// This is the simplest two-node raw Zigbee data-frame exchange in the repo:
// send PING, wait for PONG, log RSSI and rough distance heuristic.

static ZigbeeRadio g_zb;
static uint8_t g_sequence = 1U;
static uint32_t g_lastPingMs = 0U;
static uint32_t g_pingSent = 0U;
static uint32_t g_pingReply = 0U;
static uint32_t g_pingMiss = 0U;
static uint32_t g_lastStatusMs = 0U;

// Network configuration comes from the Tools menu defaults unless overridden by
// the macros above.
static constexpr uint8_t kChannel =
    static_cast<uint8_t>(NRF54L15_CLEAN_ZIGBEE_CHANNEL);
static constexpr uint16_t kPanId =
    static_cast<uint16_t>(NRF54L15_CLEAN_ZIGBEE_PAN_ID);
static constexpr uint16_t kLocalShort = 0x0001U;
static constexpr uint16_t kPeerShort = 0x0002U;
static constexpr uint32_t kPingIntervalMs = 1000U;
static constexpr uint32_t kReplyWindowUs = 180000U;
static constexpr float kRefRssiAtOneMeterDbm = -59.0f;
static constexpr float kPathLossExponent = 2.0f;

static int32_t estimateDistanceMm(int8_t rssiDbm) {
  if (rssiDbm >= 0) {
    return -1;
  }
  const float exponent =
      (kRefRssiAtOneMeterDbm - static_cast<float>(rssiDbm)) /
      (10.0f * kPathLossExponent);
  const float meters = powf(10.0f, exponent);
  const int32_t mm = static_cast<int32_t>(meters * 1000.0f + 0.5f);
  if (mm < 0) {
    return -1;
  }
  return mm;
}

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nZigbeePingInitiator start\r\n");
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  bool ok = g_zb.begin(kChannel, 8);
  Serial.print("zigbee_phy_init=");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print(" channel=");
  Serial.print(kChannel);
  Serial.print(" pan=0x");
  Serial.print(kPanId, HEX);
  Serial.print(" local=0x");
  Serial.print(kLocalShort, HEX);
  Serial.print(" peer=0x");
  Serial.print(kPeerShort, HEX);
  Serial.print("\r\n");
}

void loop() {
  const uint32_t nowMs = millis();

  if ((nowMs - g_lastPingMs) >= kPingIntervalMs) {
    g_lastPingMs = nowMs;
    ++g_pingSent;

    uint8_t payload[5] = {'P', 'I', 'N', 'G', g_sequence};
    uint8_t psdu[127] = {0};
    uint8_t psduLen = 0U;
    bool frameOk = ZigbeeRadio::buildDataFrameShort(
        g_sequence, kPanId, kPeerShort, kLocalShort, payload, sizeof(payload),
        psdu, &psduLen, false);
    bool txOk = frameOk && g_zb.transmit(psdu, psduLen, false, 1400000UL);

    bool matchedReply = false;
    int8_t matchedRssi = 0;
    const uint32_t waitStartUs = micros();

    while (static_cast<uint32_t>(micros() - waitStartUs) < kReplyWindowUs) {
      ZigbeeFrame frame{};
      if (!g_zb.receive(&frame, 7000U, 900000UL)) {
        continue;
      }

      ZigbeeDataFrameView view{};
      if (!ZigbeeRadio::parseDataFrameShort(frame.psdu, frame.length, &view) ||
          !view.valid) {
        continue;
      }
      if (view.panId != kPanId || view.sourceShort != kPeerShort ||
          view.destinationShort != kLocalShort || view.payloadLength < 5U) {
        continue;
      }
      if (view.payload[0] != 'P' || view.payload[1] != 'O' ||
          view.payload[2] != 'N' || view.payload[3] != 'G') {
        continue;
      }
      if (view.payload[4] != g_sequence) {
        continue;
      }

      matchedReply = true;
      matchedRssi = frame.rssiDbm;
      break;
    }

    if (matchedReply) {
      ++g_pingReply;
      const int32_t mm = estimateDistanceMm(matchedRssi);
      Serial.print("ping seq=");
      Serial.print(g_sequence);
      Serial.print(" tx=");
      Serial.print(txOk ? "OK" : "FAIL");
      Serial.print(" reply=OK rssi=");
      Serial.print(matchedRssi);
      Serial.print("dBm");
      if (mm > 0) {
        Serial.print(" dist_cm=");
        Serial.print(mm / 10);
        Serial.print(" dist_mm=");
        Serial.print(mm);
      }
      Serial.print("\r\n");
      Gpio::write(kPinUserLed, false);
    } else {
      ++g_pingMiss;
      Serial.print("ping seq=");
      Serial.print(g_sequence);
      Serial.print(" tx=");
      Serial.print(txOk ? "OK" : "FAIL");
      Serial.print(" reply=MISS\r\n");
      Gpio::write(kPinUserLed, true);
    }

    ++g_sequence;
  }

  if ((nowMs - g_lastStatusMs) >= 2000U) {
    g_lastStatusMs = nowMs;
    Serial.print("t=");
    Serial.print(nowMs);
    Serial.print(" sent=");
    Serial.print(g_pingSent);
    Serial.print(" ok=");
    Serial.print(g_pingReply);
    Serial.print(" miss=");
    Serial.print(g_pingMiss);
    Serial.print("\r\n");
  }

  delay(1);
}
