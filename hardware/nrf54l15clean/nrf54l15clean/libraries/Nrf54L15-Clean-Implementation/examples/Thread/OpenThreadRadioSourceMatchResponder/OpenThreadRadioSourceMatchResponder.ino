#include <Arduino.h>

#include "nrf54l15_hal.h"
#include "openthread_platform_nrf54l15.h"

#include <openthread/error.h>
#include <openthread/platform/radio.h>

using xiao_nrf54l15::OpenThreadPlatformSkeleton;
using xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot;
using xiao_nrf54l15::ZigbeeMacAcknowledgementView;
using xiao_nrf54l15::ZigbeeMacCommandView;
using xiao_nrf54l15::ZigbeeRadio;

extern "C" __attribute__((used)) volatile uint32_t g_ot_srcmatch_results[16] = {0};

namespace {

constexpr uint8_t kChannel = 15U;
constexpr otPanId kPanId = 0x1234U;
constexpr otShortAddress kResponderShort = 0x1111U;
constexpr otShortAddress kMatchedRequesterShort = 0x2222U;
constexpr otShortAddress kUnmatchedRequesterShort = 0x4444U;
constexpr otShortAddress kPeerShort = 0x3333U;
constexpr uint8_t kDataRequestCommandId = 0x04U;

uint32_t gRxDataRequestCount = 0;
uint32_t gRxMatchedCount = 0;
uint32_t gRxUnmatchedCount = 0;
uint32_t gTxDoneCount = 0;
otError gLastTxError = OT_ERROR_NONE;
bool gLastTxAckValid = false;
bool gLastTxAckFramePending = false;
uint8_t gLastTxAckSequence = 0U;
uint8_t gTxSequence = 1U;
uint8_t gTransmitAttempts = 0U;
bool gSummaryPrinted = false;
uint32_t gSetupStartMs = 0U;

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

void printSnapshot(const OpenThreadPlatformSkeletonSnapshot& snapshot) {
  Serial.print("src=");
  Serial.print(snapshot.radioSrcMatchEnabled ? 1 : 0);
  Serial.print("/");
  Serial.print(snapshot.radioSrcMatchShortCount);
  Serial.print("/");
  Serial.print(snapshot.radioSrcMatchExtCount);
  Serial.print(" ackcb=");
  Serial.print(snapshot.radioSrcMatchAckSetCount);
  Serial.print("/");
  Serial.print(snapshot.radioSrcMatchAckClearCount);
  Serial.print(" txack=");
  Serial.print(snapshot.radioLastTxAcked ? 1 : 0);
  Serial.print("/");
  Serial.print(snapshot.radioLastTxAckFramePending ? 1 : 0);
  Serial.print("/");
  Serial.print(snapshot.radioLastTxAckLength);
  Serial.print(" lastsrc=0x");
  printHex16(snapshot.radioLastSrcMatchShortAddress);
}

void printSummary() {
  OpenThreadPlatformSkeletonSnapshot snapshot = {};
  OpenThreadPlatformSkeleton::snapshot(&snapshot);
  Serial.print("ot_srcmatch rx=");
  Serial.print(gRxDataRequestCount);
  Serial.print("/");
  Serial.print(gRxMatchedCount);
  Serial.print("/");
  Serial.print(gRxUnmatchedCount);
  Serial.print(" tx=");
  Serial.print(gTxDoneCount);
  Serial.print("/");
  Serial.print(static_cast<int>(gLastTxError));
  Serial.print("/");
  Serial.print(gLastTxAckValid ? 1 : 0);
  Serial.print("/");
  Serial.print(gLastTxAckFramePending ? 1 : 0);
  Serial.print("/");
  Serial.print(gLastTxAckSequence);
  Serial.print(" ");
  printSnapshot(snapshot);
  Serial.println();
}

void updateResults() {
  OpenThreadPlatformSkeletonSnapshot snapshot = {};
  OpenThreadPlatformSkeleton::snapshot(&snapshot);
  g_ot_srcmatch_results[0] = gRxDataRequestCount;
  g_ot_srcmatch_results[1] = gRxMatchedCount;
  g_ot_srcmatch_results[2] = gRxUnmatchedCount;
  g_ot_srcmatch_results[3] = gTxDoneCount;
  g_ot_srcmatch_results[4] = static_cast<uint32_t>(gLastTxError);
  g_ot_srcmatch_results[5] = gLastTxAckValid ? 1U : 0U;
  g_ot_srcmatch_results[6] = gLastTxAckFramePending ? 1U : 0U;
  g_ot_srcmatch_results[7] = gLastTxAckSequence;
  g_ot_srcmatch_results[8] = snapshot.radioSrcMatchEnabled ? 1U : 0U;
  g_ot_srcmatch_results[9] = snapshot.radioSrcMatchShortCount;
  g_ot_srcmatch_results[10] = snapshot.radioSrcMatchExtCount;
  g_ot_srcmatch_results[11] = snapshot.radioSrcMatchAckSetCount;
  g_ot_srcmatch_results[12] = snapshot.radioSrcMatchAckClearCount;
  g_ot_srcmatch_results[13] = snapshot.radioLastTxAcked ? 1U : 0U;
  g_ot_srcmatch_results[14] =
      snapshot.radioLastTxAckFramePending ? 1U : 0U;
  g_ot_srcmatch_results[15] = snapshot.radioLastSrcMatchShortAddress;
}

void startResponderTransmit() {
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

extern "C" void otPlatRadioReceiveDone(otInstance*, otRadioFrame* frame,
                                       otError error) {
  if (error != OT_ERROR_NONE || frame == nullptr || frame->mPsdu == nullptr) {
    return;
  }

  ZigbeeMacCommandView view = {};
  if (!ZigbeeRadio::parseMacCommandFrameShort(frame->mPsdu, frame->mLength,
                                              &view) ||
      !view.valid || view.commandId != kDataRequestCommandId) {
    return;
  }

  ++gRxDataRequestCount;
  if (view.sourceShort == kMatchedRequesterShort) {
    ++gRxMatchedCount;
  } else if (view.sourceShort == kUnmatchedRequesterShort) {
    ++gRxUnmatchedCount;
  }
}

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  gSetupStartMs = millis();
  OpenThreadPlatformSkeleton::begin();

  otPlatRadioEnable(nullptr);
  otPlatRadioSetPanId(nullptr, kPanId);
  otPlatRadioSetShortAddress(nullptr, kResponderShort);
  otPlatRadioSetRxOnWhenIdle(nullptr, true);
  otPlatRadioEnableSrcMatch(nullptr, true);
  (void)otPlatRadioAddSrcMatchShortEntry(nullptr, kMatchedRequesterShort);
  otPlatRadioReceive(nullptr, kChannel);

  OpenThreadPlatformSkeletonSnapshot snapshot = {};
  OpenThreadPlatformSkeleton::snapshot(&snapshot);
  Serial.print("ot_srcmatch_ready ");
  printSnapshot(snapshot);
  Serial.println();
  updateResults();
}

void loop() {
  OpenThreadPlatformSkeleton::process();

  const uint32_t elapsedMs = millis() - gSetupStartMs;
  if (!gLastTxAckValid && gTransmitAttempts < 3U &&
      elapsedMs >= (15000UL +
                    static_cast<uint32_t>(gTransmitAttempts) * 1500UL)) {
    ++gTransmitAttempts;
    startResponderTransmit();
  }

  if (!gSummaryPrinted && gRxMatchedCount >= 1U && gRxUnmatchedCount >= 1U &&
      gTxDoneCount >= 1U) {
    gSummaryPrinted = true;
    printSummary();
  } else if (!gSummaryPrinted && elapsedMs >= 28000UL) {
    gSummaryPrinted = true;
    printSummary();
  }

  updateResults();
}
