#include <Arduino.h>

#include "nrf54l15_hal.h"
#include "openthread_platform_nrf54l15.h"

#include <openthread/error.h>
#include <openthread/platform/radio.h>

using xiao_nrf54l15::OpenThreadPlatformSkeleton;
using xiao_nrf54l15::ZigbeeDataFrameView;
using xiao_nrf54l15::ZigbeeMacAcknowledgementView;
using xiao_nrf54l15::ZigbeeRadio;

extern "C" __attribute__((used)) volatile uint32_t g_ot_packet_initiator_results[24] = {0};

namespace {

constexpr uint8_t kChannel = 15U;
constexpr otPanId kPanId = 0x1234U;
constexpr otShortAddress kInitiatorShort = 0x1111U;
constexpr otShortAddress kResponderShort = 0x2222U;
constexpr uint8_t kPingPayload[] = {'P', 'I', 'N', 'G'};
constexpr uint8_t kPongPayload[] = {'P', 'O', 'N', 'G'};

uint32_t gBootMs = 0U;
uint8_t gSequence = 1U;
uint8_t gPingSequence = 0U;
bool gPingTransmitIssued = false;
bool gPingTxDone = false;
otError gPingTxError = OT_ERROR_NONE;
bool gPingAckValid = false;
bool gPingAckFramePending = false;
uint8_t gPingAckSequence = 0U;
uint8_t gPingAttempts = 0U;
uint32_t gLastPingAttemptMs = 0U;

bool gPongSeen = false;
uint8_t gPongSequence = 0U;
uint8_t gPongLength = 0U;
uint8_t gPongChannel = 0U;
int32_t gPongRssiDbm = OT_RADIO_RSSI_INVALID;
uint32_t gPongTimestampLow = 0U;
uint32_t gPongTimestampHigh = 0U;
uint16_t gPongSourceShort = 0U;
uint16_t gPongDestinationShort = 0U;
bool gPongAckRequested = false;
bool gPongPayloadValid = false;

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
  g_ot_packet_initiator_results[0] = gPingTransmitIssued ? 1U : 0U;
  g_ot_packet_initiator_results[1] = gPingTxDone ? 1U : 0U;
  g_ot_packet_initiator_results[2] = static_cast<uint32_t>(gPingTxError);
  g_ot_packet_initiator_results[3] = gPingAckValid ? 1U : 0U;
  g_ot_packet_initiator_results[4] = gPingAckFramePending ? 1U : 0U;
  g_ot_packet_initiator_results[5] = gPingAckSequence;
  g_ot_packet_initiator_results[6] = gPingSequence;
  g_ot_packet_initiator_results[7] = gPingAttempts;

  g_ot_packet_initiator_results[8] = gPongSeen ? 1U : 0U;
  g_ot_packet_initiator_results[9] = gPongSequence;
  g_ot_packet_initiator_results[10] = gPongLength;
  g_ot_packet_initiator_results[11] = gPongChannel;
  g_ot_packet_initiator_results[12] = static_cast<uint32_t>(gPongRssiDbm);
  g_ot_packet_initiator_results[13] = gPongTimestampLow;
  g_ot_packet_initiator_results[14] = gPongTimestampHigh;
  g_ot_packet_initiator_results[15] = gPongSourceShort;
  g_ot_packet_initiator_results[16] = gPongDestinationShort;
  g_ot_packet_initiator_results[17] = gPongAckRequested ? 1U : 0U;
  g_ot_packet_initiator_results[18] = gPongPayloadValid ? 1U : 0U;
  g_ot_packet_initiator_results[19] = millis() - gBootMs;
}

void startPingTransmit() {
  otRadioFrame* frame = otPlatRadioGetTransmitBuffer(nullptr);
  if (frame == nullptr || frame->mPsdu == nullptr) {
    return;
  }

  uint8_t length = 0U;
  const uint8_t sequence = gSequence++;
  if (!ZigbeeRadio::buildDataFrameShort(sequence, kPanId, kResponderShort,
                                        kInitiatorShort, kPingPayload,
                                        sizeof(kPingPayload), frame->mPsdu,
                                        &length, true)) {
    return;
  }

  gPingSequence = sequence;
  frame->mLength = length;
  frame->mChannel = kChannel;
  frame->mInfo.mTxInfo.mCsmaCaEnabled = false;
  frame->mInfo.mTxInfo.mTxPower = 0;
  gPingTransmitIssued = true;
  gPingTxDone = false;
  gPingTxError = OT_ERROR_NONE;
  gPingAckValid = false;
  gPingAckFramePending = false;
  gPingAckSequence = 0U;
  ++gPingAttempts;
  gLastPingAttemptMs = millis() - gBootMs;
  (void)otPlatRadioTransmit(nullptr, frame);
}

}  // namespace

extern "C" void otPlatRadioTxDone(otInstance*, otRadioFrame*, otRadioFrame* ackFrame,
                                  otError error) {
  gPingTxDone = true;
  gPingTxError = error;
  gPingAckValid = false;
  gPingAckFramePending = false;
  gPingAckSequence = 0U;

  if (ackFrame != nullptr && ackFrame->mPsdu != nullptr) {
    ZigbeeMacAcknowledgementView acknowledgement = {};
    if (ZigbeeRadio::parseMacAcknowledgement(ackFrame->mPsdu,
                                             ackFrame->mLength,
                                             &acknowledgement) &&
        acknowledgement.valid) {
      gPingAckValid = true;
      gPingAckFramePending = acknowledgement.framePending;
      gPingAckSequence = acknowledgement.sequence;
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
      !view.valid || view.sourceShort != kResponderShort ||
      view.destinationShort != kInitiatorShort) {
    return;
  }

  gPongSeen = true;
  gPongSequence = view.sequence;
  gPongLength = frame->mLength;
  gPongChannel = frame->mChannel;
  gPongRssiDbm = frame->mInfo.mRxInfo.mRssi;
  gPongTimestampLow =
      static_cast<uint32_t>(frame->mInfo.mRxInfo.mTimestamp & 0xFFFFFFFFULL);
  gPongTimestampHigh =
      static_cast<uint32_t>(frame->mInfo.mRxInfo.mTimestamp >> 32U);
  gPongSourceShort = view.sourceShort;
  gPongDestinationShort = view.destinationShort;
  gPongAckRequested = view.ackRequested;
  gPongPayloadValid = payloadEquals(view.payload, view.payloadLength,
                                    kPongPayload, sizeof(kPongPayload));
}

void setup() {
  gBootMs = millis();
  OpenThreadPlatformSkeleton::begin();
  otPlatRadioEnable(nullptr);
  otPlatRadioSetPanId(nullptr, kPanId);
  otPlatRadioSetShortAddress(nullptr, kInitiatorShort);
  otPlatRadioSetRxOnWhenIdle(nullptr, true);
  otPlatRadioReceive(nullptr, kChannel);
  updateResults();
}

void loop() {
  OpenThreadPlatformSkeleton::process();

  const uint32_t elapsedMs = millis() - gBootMs;
  const bool needRetry = gPingTransmitIssued && !gPingAckValid && !gPongSeen &&
                         gPingAttempts < 4U &&
                         (elapsedMs - gLastPingAttemptMs) >= 1500UL;
  if ((!gPingTransmitIssued && elapsedMs >= 4000UL) || needRetry) {
    startPingTransmit();
  }

  updateResults();
}
