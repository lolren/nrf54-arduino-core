#include <Arduino.h>

#include "nrf54l15_hal.h"

using xiao_nrf54l15::ZigbeeFrame;
using xiao_nrf54l15::ZigbeeMacCommandView;
using xiao_nrf54l15::ZigbeeRadio;
using xiao_nrf54l15::ZigbeeTransmitDebug;

extern "C" __attribute__((used)) volatile uint32_t g_ot_srcmatch_peer_results[16] = {0};

namespace {

constexpr uint8_t kChannel = 15U;
constexpr uint16_t kPanId = 0x1234U;
constexpr uint16_t kResponderShort = 0x1111U;
constexpr uint16_t kMatchedRequesterShort = 0x2222U;
constexpr uint16_t kUnmatchedRequesterShort = 0x4444U;
constexpr uint16_t kPeerShort = 0x3333U;
constexpr uint8_t kDataRequestCommandId = 0x04U;

ZigbeeRadio gRadio;
uint8_t gSequence = 1U;
uint32_t gBootMs = 0U;
bool gMatchedPollDone = false;
bool gUnmatchedPollDone = false;
bool gPeerRequestSeen = false;
uint32_t gCallbackSetCount = 0U;
uint32_t gCallbackClearCount = 0U;
uint8_t gMatchedPollAttempts = 0U;
uint8_t gUnmatchedPollAttempts = 0U;
bool gMatchedPollTxOk = false;
bool gMatchedPollAck = false;
bool gMatchedPollFramePending = false;
uint8_t gMatchedPollSequence = 0U;
bool gUnmatchedPollTxOk = false;
bool gUnmatchedPollAck = false;
bool gUnmatchedPollFramePending = false;
uint8_t gUnmatchedPollSequence = 0U;
uint16_t gPeerRequestSource = 0U;
uint16_t gPeerRequestDestination = 0U;
uint8_t gPeerRequestSequence = 0U;

void printHex16(uint16_t value) {
  if (value < 0x1000U) {
    Serial.print('0');
  }
  if (value < 0x0100U) {
    Serial.print('0');
  }
  if (value < 0x0010U) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

bool pendingCallback(const uint8_t* psdu, uint8_t length, void*) {
  ZigbeeMacCommandView view = {};
  const bool match = ZigbeeRadio::parseMacCommandFrameShort(psdu, length, &view) &&
                     view.valid && view.commandId == kDataRequestCommandId &&
                     view.sourceShort == kResponderShort;
  if (match) {
    ++gCallbackSetCount;
    return true;
  }
  ++gCallbackClearCount;
  return false;
}

void sendPoll(const char* label, uint16_t sourceShort) {
  uint8_t psdu[127] = {0};
  uint8_t length = 0U;
  if (!ZigbeeRadio::buildMacCommandFrameShort(
          gSequence++, kPanId, kResponderShort, sourceShort,
          kDataRequestCommandId, nullptr, 0U, psdu, &length)) {
    Serial.print("poll ");
    Serial.print(label);
    Serial.println(" build=0");
    return;
  }

  const bool txOk = gRadio.transmit(psdu, length, false, 900000UL);
  const ZigbeeTransmitDebug debug = gRadio.lastTransmitDebug();
  if (sourceShort == kMatchedRequesterShort) {
    gMatchedPollTxOk = txOk;
    gMatchedPollAck = debug.ackReceived;
    gMatchedPollFramePending = debug.ackFramePending;
    gMatchedPollSequence = debug.ackSequence;
  } else if (sourceShort == kUnmatchedRequesterShort) {
    gUnmatchedPollTxOk = txOk;
    gUnmatchedPollAck = debug.ackReceived;
    gUnmatchedPollFramePending = debug.ackFramePending;
    gUnmatchedPollSequence = debug.ackSequence;
  }

  Serial.print("poll ");
  Serial.print(label);
  Serial.print(" tx=");
  Serial.print(txOk ? 1 : 0);
  Serial.print(" ack=");
  Serial.print(debug.ackReceived ? 1 : 0);
  Serial.print(" fp=");
  Serial.print(debug.ackFramePending ? 1 : 0);
  Serial.print(" seq=");
  Serial.print(debug.ackSequence);
  Serial.print(" rxseq=");
  Serial.print(debug.rxSequence);
  Serial.println();
}

void pollForPeerDataRequest() {
  ZigbeeFrame frame = {};
  if (!gRadio.receive(&frame, 250000U, 900000UL)) {
    return;
  }

  ZigbeeMacCommandView view = {};
  if (!ZigbeeRadio::parseMacCommandFrameShort(frame.psdu, frame.length, &view) ||
      !view.valid || view.commandId != kDataRequestCommandId) {
    return;
  }

  if (view.sourceShort != kResponderShort) {
    return;
  }

  gPeerRequestSeen = true;
  gPeerRequestSource = view.sourceShort;
  gPeerRequestDestination = view.destinationShort;
  gPeerRequestSequence = view.sequence;
  Serial.print("peer_rx src=0x");
  printHex16(view.sourceShort);
  Serial.print(" dst=0x");
  printHex16(view.destinationShort);
  Serial.print(" seq=");
  Serial.print(view.sequence);
  Serial.print(" cb=");
  Serial.print(gCallbackSetCount);
  Serial.print("/");
  Serial.print(gCallbackClearCount);
  Serial.println();
}

void updateResults() {
  g_ot_srcmatch_peer_results[0] = gMatchedPollDone ? 1U : 0U;
  g_ot_srcmatch_peer_results[1] = gMatchedPollTxOk ? 1U : 0U;
  g_ot_srcmatch_peer_results[2] = gMatchedPollAck ? 1U : 0U;
  g_ot_srcmatch_peer_results[3] =
      gMatchedPollFramePending ? 1U : 0U;
  g_ot_srcmatch_peer_results[4] = gUnmatchedPollDone ? 1U : 0U;
  g_ot_srcmatch_peer_results[5] = gUnmatchedPollTxOk ? 1U : 0U;
  g_ot_srcmatch_peer_results[6] = gUnmatchedPollAck ? 1U : 0U;
  g_ot_srcmatch_peer_results[7] =
      gUnmatchedPollFramePending ? 1U : 0U;
  g_ot_srcmatch_peer_results[8] = gPeerRequestSeen ? 1U : 0U;
  g_ot_srcmatch_peer_results[9] = gPeerRequestSource;
  g_ot_srcmatch_peer_results[10] = gPeerRequestDestination;
  g_ot_srcmatch_peer_results[11] = gPeerRequestSequence;
  g_ot_srcmatch_peer_results[12] = gCallbackSetCount;
  g_ot_srcmatch_peer_results[13] = gCallbackClearCount;
  g_ot_srcmatch_peer_results[14] = gMatchedPollSequence;
  g_ot_srcmatch_peer_results[15] = gUnmatchedPollSequence;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  gBootMs = millis();
  if (!gRadio.begin(kChannel, 0)) {
    Serial.println("radio begin failed");
    return;
  }
  gRadio.setMacDataRequestPendingCallback(pendingCallback);
  Serial.println("peer_ready");
  updateResults();
}

void loop() {
  const uint32_t elapsedMs = millis() - gBootMs;

  if (!gMatchedPollDone &&
      elapsedMs >= (1500UL + static_cast<uint32_t>(gMatchedPollAttempts) * 1500UL)) {
    ++gMatchedPollAttempts;
    sendPoll("matched", kMatchedRequesterShort);
    if (gMatchedPollAck || gMatchedPollAttempts >= 3U) {
      gMatchedPollDone = true;
    }
  }

  if (gMatchedPollDone && !gUnmatchedPollDone &&
      elapsedMs >= (7000UL + static_cast<uint32_t>(gUnmatchedPollAttempts) * 1500UL)) {
    ++gUnmatchedPollAttempts;
    sendPoll("unmatched", kUnmatchedRequesterShort);
    if (gUnmatchedPollAck || gUnmatchedPollAttempts >= 3U) {
      gUnmatchedPollDone = true;
    }
  }

  if (gMatchedPollDone && gUnmatchedPollDone && !gPeerRequestSeen &&
      elapsedMs >= 15000UL && elapsedMs < 28000UL) {
    pollForPeerDataRequest();
  }

  if (!gPeerRequestSeen && elapsedMs >= 28000UL) {
    gPeerRequestSeen = true;
    Serial.print("peer_rx timeout cb=");
    Serial.print(gCallbackSetCount);
    Serial.print("/");
    Serial.print(gCallbackClearCount);
    Serial.println();
  }

  updateResults();
}
