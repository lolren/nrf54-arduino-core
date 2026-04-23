#include <Arduino.h>

#include "nrf54l15_hal.h"
#include "openthread_platform_nrf54l15.h"

#include <openthread/error.h>
#include <openthread/platform/radio.h>

using xiao_nrf54l15::OpenThreadPlatformSkeleton;
using xiao_nrf54l15::ZigbeeDataFrameView;
using xiao_nrf54l15::ZigbeeMacAcknowledgementView;
using xiao_nrf54l15::ZigbeeRadio;

extern "C" __attribute__((used)) volatile uint32_t g_ot_packet_responder_results[24] = {0};

namespace {

constexpr uint8_t kChannel = 15U;
constexpr otPanId kPanId = 0x1234U;
constexpr otShortAddress kInitiatorShort = 0x1111U;
constexpr otShortAddress kResponderShort = 0x2222U;
constexpr uint8_t kPingPayload[] = {'P', 'I', 'N', 'G'};
constexpr uint8_t kPongPayload[] = {'P', 'O', 'N', 'G'};

uint32_t gBootMs = 0U;
uint8_t gSequence = 0x80U;

bool gPingSeen = false;
uint8_t gPingSequence = 0U;
uint8_t gPingLength = 0U;
uint8_t gPingChannel = 0U;
int32_t gPingRssiDbm = OT_RADIO_RSSI_INVALID;
uint32_t gPingTimestampLow = 0U;
uint32_t gPingTimestampHigh = 0U;
uint16_t gPingSourceShort = 0U;
uint16_t gPingDestinationShort = 0U;
bool gPingAckRequested = false;
bool gPingPayloadValid = false;

bool gPongTransmitPending = false;
bool gPongTransmitIssued = false;
bool gPongTxDone = false;
otError gPongTxError = OT_ERROR_NONE;
bool gPongAckValid = false;
bool gPongAckFramePending = false;
uint8_t gPongAckSequence = 0U;
uint8_t gPongSequence = 0U;
uint8_t gPongAttempts = 0U;
uint32_t gLastPongAttemptMs = 0U;

bool payloadEquals(const uint8_t* actual, uint8_t actualLength,
                   const uint8_t* expected, uint8_t expectedLength) {
  if (actual == nullptr || expected == nullptr || actualLength != expectedLength) {
    return false;
  }
  for (uint8_t i = 0; i < actualLength; ++i) {
    if (actual[i] != expected[i]) {
      return false;
    }
  }
  return true;
}

void updateResults() {
  g_ot_packet_responder_results[0] = gPingSeen ? 1U : 0U;
  g_ot_packet_responder_results[1] = gPingSequence;
  g_ot_packet_responder_results[2] = gPingLength;
  g_ot_packet_responder_results[3] = gPingChannel;
  g_ot_packet_responder_results[4] = static_cast<uint32_t>(gPingRssiDbm);
  g_ot_packet_responder_results[5] = gPingTimestampLow;
  g_ot_packet_responder_results[6] = gPingTimestampHigh;
  g_ot_packet_responder_results[7] = gPingSourceShort;
  g_ot_packet_responder_results[8] = gPingDestinationShort;
  g_ot_packet_responder_results[9] = gPingAckRequested ? 1U : 0U;
  g_ot_packet_responder_results[10] = gPingPayloadValid ? 1U : 0U;

  g_ot_packet_responder_results[12] = gPongTransmitIssued ? 1U : 0U;
  g_ot_packet_responder_results[13] = gPongTxDone ? 1U : 0U;
  g_ot_packet_responder_results[14] = static_cast<uint32_t>(gPongTxError);
  g_ot_packet_responder_results[15] = gPongAckValid ? 1U : 0U;
  g_ot_packet_responder_results[16] = gPongAckFramePending ? 1U : 0U;
  g_ot_packet_responder_results[17] = gPongAckSequence;
  g_ot_packet_responder_results[18] = gPongSequence;
  g_ot_packet_responder_results[19] = millis() - gBootMs;
  g_ot_packet_responder_results[20] = gPongAttempts;
}

void startPongTransmit() {
  otRadioFrame* frame = otPlatRadioGetTransmitBuffer(nullptr);
  if (frame == nullptr || frame->mPsdu == nullptr) {
    return;
  }

  uint8_t length = 0U;
  const uint8_t sequence = gSequence++;
  if (!ZigbeeRadio::buildDataFrameShort(sequence, kPanId, kInitiatorShort,
                                        kResponderShort, kPongPayload,
                                        sizeof(kPongPayload), frame->mPsdu,
                                        &length, true)) {
    return;
  }

  gPongSequence = sequence;
  frame->mLength = length;
  frame->mChannel = kChannel;
  frame->mInfo.mTxInfo.mCsmaCaEnabled = false;
  frame->mInfo.mTxInfo.mTxPower = 0;
  gPongTransmitIssued = true;
  gPongTxDone = false;
  gPongTxError = OT_ERROR_NONE;
  gPongAckValid = false;
  gPongAckFramePending = false;
  gPongAckSequence = 0U;
  ++gPongAttempts;
  gLastPongAttemptMs = millis() - gBootMs;
  gPongTransmitPending = false;
  (void)otPlatRadioTransmit(nullptr, frame);
}

}  // namespace

extern "C" void otPlatRadioTxDone(otInstance*, otRadioFrame*, otRadioFrame* ackFrame,
                                  otError error) {
  gPongTxDone = true;
  gPongTxError = error;
  gPongAckValid = false;
  gPongAckFramePending = false;
  gPongAckSequence = 0U;

  if (ackFrame != nullptr && ackFrame->mPsdu != nullptr) {
    ZigbeeMacAcknowledgementView acknowledgement = {};
    if (ZigbeeRadio::parseMacAcknowledgement(ackFrame->mPsdu,
                                             ackFrame->mLength,
                                             &acknowledgement) &&
        acknowledgement.valid) {
      gPongAckValid = true;
      gPongAckFramePending = acknowledgement.framePending;
      gPongAckSequence = acknowledgement.sequence;
    }
  }
}

extern "C" void otPlatRadioReceiveDone(otInstance*, otRadioFrame* frame,
                                       otError error) {
  if (error != OT_ERROR_NONE || frame == nullptr || frame->mPsdu == nullptr) {
    return;
  }

  ZigbeeDataFrameView view = {};
  if (!ZigbeeRadio::parseDataFrameShort(frame->mPsdu, frame->mLength, &view) ||
      !view.valid || view.sourceShort != kInitiatorShort ||
      view.destinationShort != kResponderShort) {
    return;
  }

  gPingSeen = true;
  gPingSequence = view.sequence;
  gPingLength = frame->mLength;
  gPingChannel = frame->mChannel;
  gPingRssiDbm = frame->mInfo.mRxInfo.mRssi;
  gPingTimestampLow =
      static_cast<uint32_t>(frame->mInfo.mRxInfo.mTimestamp & 0xFFFFFFFFULL);
  gPingTimestampHigh =
      static_cast<uint32_t>(frame->mInfo.mRxInfo.mTimestamp >> 32U);
  gPingSourceShort = view.sourceShort;
  gPingDestinationShort = view.destinationShort;
  gPingAckRequested = view.ackRequested;
  gPingPayloadValid = payloadEquals(view.payload, view.payloadLength,
                                    kPingPayload, sizeof(kPingPayload));
  gPongTransmitPending = true;
}

void setup() {
  gBootMs = millis();
  OpenThreadPlatformSkeleton::begin();
  otPlatRadioEnable(nullptr);
  otPlatRadioSetPanId(nullptr, kPanId);
  otPlatRadioSetShortAddress(nullptr, kResponderShort);
  otPlatRadioSetRxOnWhenIdle(nullptr, true);
  otPlatRadioReceive(nullptr, kChannel);
  updateResults();
}

void loop() {
  OpenThreadPlatformSkeleton::process();

  const uint32_t elapsedMs = millis() - gBootMs;
  const bool needRetry = gPongTransmitIssued && !gPongAckValid &&
                         gPongAttempts < 4U &&
                         (elapsedMs - gLastPongAttemptMs) >= 1500UL;
  if ((gPongTransmitPending && !gPongTransmitIssued) || needRetry) {
    startPongTransmit();
  }

  updateResults();
}
