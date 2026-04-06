#include <Arduino.h>

#include <math.h>

#include "nrf54l15_hal.h"

#if defined(NRF54L15_CLEAN_ZIGBEE_ENABLED) && (NRF54L15_CLEAN_ZIGBEE_ENABLED == 0)
#error "Enable Tools > Zigbee Support to build ZigbeeRouter."
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_CHANNEL
#define NRF54L15_CLEAN_ZIGBEE_CHANNEL 15
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_PAN_ID
#define NRF54L15_CLEAN_ZIGBEE_PAN_ID 0x1234
#endif

using namespace xiao_nrf54l15;

// Router role demo for the raw Zigbee helper path.
//
// Flow:
// 1) boot with a temporary short address
// 2) send a join request to the coordinator
// 3) switch to the assigned short address on success
// 4) send periodic router heartbeat frames and wait for coordinator ACKs

static ZigbeeRadio g_zb;
static uint8_t g_sequence = 1U;
static uint8_t g_joinNonce = 1U;
static uint16_t g_localShort = 0x7E11U;
static bool g_joined = false;
static uint32_t g_lastJoinAttemptMs = 0U;
static uint32_t g_lastHeartbeatMs = 0U;
static uint32_t g_lastStatusMs = 0U;
static uint32_t g_heartbeatSent = 0U;
static uint32_t g_ackOk = 0U;
static uint32_t g_ackMiss = 0U;

// Network configuration comes from the Tools menu defaults unless overridden by
// the macros above.
static constexpr uint8_t kChannel =
    static_cast<uint8_t>(NRF54L15_CLEAN_ZIGBEE_CHANNEL);
static constexpr uint16_t kPanId =
    static_cast<uint16_t>(NRF54L15_CLEAN_ZIGBEE_PAN_ID);
static constexpr uint16_t kCoordinatorShort = 0x0000U;
static constexpr uint16_t kTempShort = 0x7E11U;
static constexpr uint8_t kJoinReqCmdId = 0xA1U;
static constexpr uint8_t kJoinRspCmdId = 0xA2U;
static constexpr uint8_t kRoleRouter = 1U;
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

static bool waitForJoinResponse(uint8_t expectedNonce, uint16_t* outAssignedShort) {
  if (outAssignedShort == nullptr) {
    return false;
  }

  const uint32_t waitStartUs = micros();
  while (static_cast<uint32_t>(micros() - waitStartUs) < 260000U) {
    ZigbeeFrame frame{};
    if (!g_zb.receive(&frame, 7000U, 900000UL)) {
      continue;
    }

    ZigbeeMacCommandView view{};
    if (!ZigbeeRadio::parseMacCommandFrameShort(frame.psdu, frame.length, &view) ||
        !view.valid) {
      continue;
    }
    if (view.panId != kPanId || view.sourceShort != kCoordinatorShort ||
        view.destinationShort != g_localShort || view.commandId != kJoinRspCmdId ||
        view.payloadLength < 5U) {
      continue;
    }

    const uint8_t status = view.payload[0];
    const uint16_t assigned =
        static_cast<uint16_t>(view.payload[1]) |
        (static_cast<uint16_t>(view.payload[2]) << 8U);
    const uint8_t nonce = view.payload[4];
    if (status != 0U || nonce != expectedNonce || assigned == 0U) {
      continue;
    }

    *outAssignedShort = assigned;
    return true;
  }

  return false;
}

static void attemptJoin() {
  uint8_t payload[2] = {kRoleRouter, g_joinNonce};
  uint8_t psdu[127] = {0};
  uint8_t psduLen = 0U;
  const bool built = ZigbeeRadio::buildMacCommandFrameShort(
      g_sequence++, kPanId, kCoordinatorShort, g_localShort, kJoinReqCmdId,
      payload, sizeof(payload), psdu, &psduLen, false);
  const bool txOk = built && g_zb.transmit(psdu, psduLen, false, 1200000UL);

  uint16_t assignedShort = 0U;
  const bool joined = txOk && waitForJoinResponse(g_joinNonce, &assignedShort);

  Serial.print("router_join nonce=");
  Serial.print(g_joinNonce);
  Serial.print(" tx=");
  Serial.print(txOk ? "OK" : "FAIL");
  Serial.print(" result=");
  Serial.print(joined ? "OK" : "MISS");
  if (joined) {
    g_joined = true;
    g_localShort = assignedShort;
    Serial.print(" short=0x");
    Serial.print(g_localShort, HEX);
  }
  Serial.print("\r\n");

  ++g_joinNonce;
}

static bool waitForAck(uint8_t heartbeatSeq, int8_t* outRssiDbm) {
  if (outRssiDbm == nullptr) {
    return false;
  }

  const uint32_t waitStartUs = micros();
  while (static_cast<uint32_t>(micros() - waitStartUs) < 180000U) {
    ZigbeeFrame frame{};
    if (!g_zb.receive(&frame, 7000U, 900000UL)) {
      continue;
    }

    ZigbeeDataFrameView view{};
    if (!ZigbeeRadio::parseDataFrameShort(frame.psdu, frame.length, &view) || !view.valid) {
      continue;
    }
    if (view.panId != kPanId || view.sourceShort != kCoordinatorShort ||
        view.destinationShort != g_localShort || view.payloadLength < 4U) {
      continue;
    }
    if (view.payload[0] != 'A' || view.payload[1] != 'C' ||
        view.payload[2] != 'K' || view.payload[3] != heartbeatSeq) {
      continue;
    }

    *outRssiDbm = frame.rssiDbm;
    return true;
  }

  return false;
}

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nZigbeeRouter start\r\n");
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  const bool ok = g_zb.begin(kChannel, 8);
  Serial.print("zigbee_phy_init=");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print(" channel=");
  Serial.print(kChannel);
  Serial.print(" pan=0x");
  Serial.print(kPanId, HEX);
  Serial.print(" temp_short=0x");
  Serial.print(kTempShort, HEX);
  Serial.print("\r\n");
}

void loop() {
  const uint32_t now = millis();

  if (!g_joined) {
    if ((now - g_lastJoinAttemptMs) >= 1500U) {
      g_lastJoinAttemptMs = now;
      attemptJoin();
    }
    delay(1);
    return;
  }

  if ((now - g_lastHeartbeatMs) >= 1100U) {
    g_lastHeartbeatMs = now;

    const uint8_t hbSeq = g_sequence;
    uint8_t payload[5] = {'R', 'T', 'R', hbSeq, static_cast<uint8_t>(g_heartbeatSent & 0xFFU)};
    uint8_t psdu[127] = {0};
    uint8_t psduLen = 0U;
    const bool built = ZigbeeRadio::buildDataFrameShort(
        hbSeq, kPanId, kCoordinatorShort, g_localShort, payload, sizeof(payload),
        psdu, &psduLen, false);
    const bool txOk = built && g_zb.transmit(psdu, psduLen, false, 1200000UL);
    ++g_sequence;
    ++g_heartbeatSent;

    int8_t ackRssiDbm = 0;
    const bool gotAck = txOk && waitForAck(hbSeq, &ackRssiDbm);
    if (gotAck) {
      ++g_ackOk;
      const int32_t mm = estimateDistanceMm(ackRssiDbm);
      Serial.print("router_hb seq=");
      Serial.print(hbSeq);
      Serial.print(" tx=OK ack=OK rssi=");
      Serial.print(ackRssiDbm);
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
      ++g_ackMiss;
      Serial.print("router_hb seq=");
      Serial.print(hbSeq);
      Serial.print(" tx=");
      Serial.print(txOk ? "OK" : "FAIL");
      Serial.print(" ack=MISS\r\n");
      Gpio::write(kPinUserLed, true);
    }
  }

  if ((now - g_lastStatusMs) >= 3000U) {
    g_lastStatusMs = now;
    Serial.print("t=");
    Serial.print(now);
    Serial.print(" joined=");
    Serial.print(g_joined ? "yes" : "no");
    Serial.print(" short=0x");
    Serial.print(g_localShort, HEX);
    Serial.print(" sent=");
    Serial.print(g_heartbeatSent);
    Serial.print(" ok=");
    Serial.print(g_ackOk);
    Serial.print(" miss=");
    Serial.print(g_ackMiss);
    Serial.print("\r\n");
  }

  delay(1);
}
