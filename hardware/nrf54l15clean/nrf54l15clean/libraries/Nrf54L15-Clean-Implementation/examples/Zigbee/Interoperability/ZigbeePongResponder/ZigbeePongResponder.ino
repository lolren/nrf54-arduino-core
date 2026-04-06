#include <Arduino.h>

#include <math.h>

#include "nrf54l15_hal.h"

#if defined(NRF54L15_CLEAN_ZIGBEE_ENABLED) && (NRF54L15_CLEAN_ZIGBEE_ENABLED == 0)
#error "Enable Tools > Zigbee Support to build ZigbeePongResponder."
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_CHANNEL
#define NRF54L15_CLEAN_ZIGBEE_CHANNEL 15
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_PAN_ID
#define NRF54L15_CLEAN_ZIGBEE_PAN_ID 0x1234
#endif

using namespace xiao_nrf54l15;

// Point-to-point Zigbee pong responder demo.
//
// Pair with ZigbeePingInitiator. This listens for PING frames and immediately
// returns a PONG carrying the same sequence byte.

static ZigbeeRadio g_zb;
static uint32_t g_rxFrames = 0U;
static uint32_t g_pingRx = 0U;
static uint32_t g_pongTx = 0U;
static uint32_t g_lastStatusMs = 0U;

// Network configuration comes from the Tools menu defaults unless overridden by
// the macros above.
static constexpr uint8_t kChannel =
    static_cast<uint8_t>(NRF54L15_CLEAN_ZIGBEE_CHANNEL);
static constexpr uint16_t kPanId =
    static_cast<uint16_t>(NRF54L15_CLEAN_ZIGBEE_PAN_ID);
static constexpr uint16_t kLocalShort = 0x0002U;
static constexpr uint16_t kPeerShort = 0x0001U;
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

  Serial.print("\r\nZigbeePongResponder start\r\n");
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
  ZigbeeFrame frame{};
  const bool got = g_zb.receive(&frame, 10000U, 1000000UL);
  if (got) {
    ++g_rxFrames;
    ZigbeeDataFrameView view{};
    const bool parsed = ZigbeeRadio::parseDataFrameShort(frame.psdu, frame.length, &view);
    if (parsed && view.valid && view.panId == kPanId &&
        view.destinationShort == kLocalShort &&
        view.sourceShort == kPeerShort &&
        view.payloadLength >= 5U &&
        view.payload[0] == 'P' &&
        view.payload[1] == 'I' &&
        view.payload[2] == 'N' &&
        view.payload[3] == 'G') {
      ++g_pingRx;
      const uint8_t seq = view.payload[4];
      const int32_t mm = estimateDistanceMm(frame.rssiDbm);

      uint8_t payload[5] = {'P', 'O', 'N', 'G', seq};
      uint8_t psdu[127] = {0};
      uint8_t psduLen = 0U;
      const bool built = ZigbeeRadio::buildDataFrameShort(
          seq, kPanId, kPeerShort, kLocalShort, payload, sizeof(payload),
          psdu, &psduLen, false);
      const bool txOk = built && g_zb.transmit(psdu, psduLen, false, 1200000UL);
      if (txOk) {
        ++g_pongTx;
        Gpio::write(kPinUserLed, false);
      } else {
        Gpio::write(kPinUserLed, true);
      }

      Serial.print("ping seq=");
      Serial.print(seq);
      Serial.print(" from=0x");
      Serial.print(view.sourceShort, HEX);
      Serial.print(" rssi=");
      Serial.print(frame.rssiDbm);
      Serial.print("dBm");
      if (mm > 0) {
        Serial.print(" dist_cm=");
        Serial.print(mm / 10);
        Serial.print(" dist_mm=");
        Serial.print(mm);
      }
      Serial.print(" pong_tx=");
      Serial.print(txOk ? "OK" : "FAIL");
      Serial.print("\r\n");
    }
  }

  const uint32_t now = millis();
  if ((now - g_lastStatusMs) >= 2000U) {
    g_lastStatusMs = now;
    Serial.print("t=");
    Serial.print(now);
    Serial.print(" rx=");
    Serial.print(g_rxFrames);
    Serial.print(" ping_rx=");
    Serial.print(g_pingRx);
    Serial.print(" pong_tx=");
    Serial.print(g_pongTx);
    Serial.print("\r\n");
  }

  delay(1);
}
