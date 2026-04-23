#include <Arduino.h>

#include "nrf54l15_hal.h"
#include "openthread_platform_nrf54l15.h"

#include <openthread/error.h>
#include <openthread/platform/radio.h>

using xiao_nrf54l15::OpenThreadPlatformSkeleton;
using xiao_nrf54l15::ZigbeeMacAcknowledgementView;
using xiao_nrf54l15::ZigbeeRadio;

extern "C" __attribute__((used)) volatile uint32_t g_ot_txack_responder_results[8] = {0};

namespace {

constexpr uint8_t kChannel = 15U;
constexpr otPanId kPanId = 0x1234U;
constexpr otShortAddress kResponderShort = 0x1111U;
constexpr otShortAddress kPeerShort = 0x3333U;
constexpr uint8_t kDataRequestCommandId = 0x04U;

uint32_t gBootMs = 0U;
uint8_t gTxSequence = 1U;
uint8_t gTxAttempts = 0U;
uint32_t gTxDoneCount = 0U;
otError gLastTxError = OT_ERROR_NONE;
bool gLastTxAckValid = false;
bool gLastTxAckFramePending = false;
uint8_t gLastTxAckSequence = 0U;

void updateResults() {
  g_ot_txack_responder_results[0] = gTxDoneCount;
  g_ot_txack_responder_results[1] = static_cast<uint32_t>(gLastTxError);
  g_ot_txack_responder_results[2] = gLastTxAckValid ? 1U : 0U;
  g_ot_txack_responder_results[3] =
      gLastTxAckFramePending ? 1U : 0U;
  g_ot_txack_responder_results[4] = gLastTxAckSequence;
  g_ot_txack_responder_results[5] = gTxAttempts;
  g_ot_txack_responder_results[6] = millis() - gBootMs;
  g_ot_txack_responder_results[7] = 0U;
}

void startTransmit() {
  otRadioFrame* frame = otPlatRadioGetTransmitBuffer(nullptr);
  if (frame == nullptr || frame->mPsdu == nullptr) {
    return;
  }

  uint8_t length = 0U;
  if (!ZigbeeRadio::buildMacCommandFrameShort(gTxSequence++, kPanId, kPeerShort,
                                              kResponderShort,
                                              kDataRequestCommandId, nullptr,
                                              0U, frame->mPsdu, &length)) {
    return;
  }

  frame->mLength = length;
  frame->mChannel = kChannel;
  frame->mInfo.mTxInfo.mCsmaCaEnabled = false;
  frame->mInfo.mTxInfo.mTxPower = 0;
  (void)otPlatRadioTransmit(nullptr, frame);
}

}  // namespace

extern "C" void otPlatRadioTxDone(otInstance*, otRadioFrame*, otRadioFrame* ackFrame,
                                  otError error) {
  ++gTxDoneCount;
  gLastTxError = error;
  gLastTxAckValid = false;
  gLastTxAckFramePending = false;
  gLastTxAckSequence = 0U;
  if (ackFrame != nullptr && ackFrame->mPsdu != nullptr) {
    ZigbeeMacAcknowledgementView ack = {};
    if (ZigbeeRadio::parseMacAcknowledgement(ackFrame->mPsdu, ackFrame->mLength,
                                             &ack) &&
        ack.valid) {
      gLastTxAckValid = true;
      gLastTxAckFramePending = ack.framePending;
      gLastTxAckSequence = ack.sequence;
    }
  }
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
  if (!gLastTxAckValid && gTxAttempts < 3U &&
      elapsedMs >= (2000UL + static_cast<uint32_t>(gTxAttempts) * 1000UL)) {
    ++gTxAttempts;
    startTransmit();
  }

  updateResults();
}
