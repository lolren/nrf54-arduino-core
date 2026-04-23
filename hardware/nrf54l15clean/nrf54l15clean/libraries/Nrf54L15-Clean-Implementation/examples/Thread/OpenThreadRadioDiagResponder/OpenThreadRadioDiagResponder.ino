#include <Arduino.h>

#include "nrf54l15_hal.h"
#include "openthread_platform_nrf54l15.h"

#include <openthread/error.h>
#include <openthread/platform/diag.h>
#include <openthread/platform/radio.h>

using xiao_nrf54l15::OpenThreadPlatformSkeleton;
using xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot;
using xiao_nrf54l15::ZigbeeDataFrameView;
using xiao_nrf54l15::ZigbeeRadio;

extern "C" __attribute__((used)) volatile uint32_t g_ot_diag_responder_results[24] = {0};

namespace {

constexpr uint8_t kDiagPayloadLength = 8U;
constexpr uint8_t kRequestPattern = 0xA1U;
constexpr uint8_t kResponsePattern = 0xB2U;

uint32_t gBootMs = 0U;
uint32_t gDiagOutputCount = 0U;
uint32_t gDiagOutputBytes = 0U;

otError gVersionError = OT_ERROR_NONE;
otError gChannelError = OT_ERROR_NONE;
otError gPowerError = OT_ERROR_NONE;
otError gStartError = OT_ERROR_NONE;
otError gSendError = OT_ERROR_NONE;
otError gStatsError = OT_ERROR_NONE;

bool gRequestSeen = false;
uint8_t gRequestLength = 0U;
uint8_t gRequestChannel = 0U;
int32_t gRequestRssiDbm = OT_RADIO_RSSI_INVALID;
uint8_t gRequestSequence = 0U;
uint16_t gRequestSourceShort = 0U;
uint16_t gRequestDestinationShort = 0U;
bool gRequestPayloadValid = false;

bool gResponsePending = false;
uint32_t gResponseReadyMs = 0U;
uint8_t gResponseAttempts = 0U;
bool gStatsIssued = false;
uint32_t gTxDoneCount = 0U;
otError gLastTxError = OT_ERROR_NONE;
uint8_t gLastTxSequence = 0U;

otError runDiagCommand(const char* arg0,
                       const char* arg1 = nullptr,
                       const char* arg2 = nullptr) {
  char argv0[24] = {0};
  char argv1[24] = {0};
  char argv2[24] = {0};
  char* argv[3] = {nullptr, nullptr, nullptr};
  uint8_t argc = 0U;

  strncpy(argv0, arg0, sizeof(argv0) - 1U);
  argv[argc++] = argv0;
  if (arg1 != nullptr) {
    strncpy(argv1, arg1, sizeof(argv1) - 1U);
    argv[argc++] = argv1;
  }
  if (arg2 != nullptr) {
    strncpy(argv2, arg2, sizeof(argv2) - 1U);
    argv[argc++] = argv2;
  }
  return otPlatDiagProcess(nullptr, argc, argv);
}

bool payloadMatchesPattern(const uint8_t* payload, uint8_t payloadLength,
                           uint8_t pattern) {
  if (payload == nullptr || payloadLength != kDiagPayloadLength) {
    return false;
  }
  for (uint8_t i = 0; i < payloadLength; ++i) {
    if (payload[i] != static_cast<uint8_t>(pattern + i)) {
      return false;
    }
  }
  return true;
}

void diagOutputCallback(const char* format, va_list args, void*) {
  char line[96] = {0};
  vsnprintf(line, sizeof(line), format, args);
  ++gDiagOutputCount;
  gDiagOutputBytes += strlen(line);
}

void updateResults() {
  OpenThreadPlatformSkeletonSnapshot snapshot = {};
  OpenThreadPlatformSkeleton::snapshot(&snapshot);

  g_ot_diag_responder_results[0] = static_cast<uint32_t>(gVersionError);
  g_ot_diag_responder_results[1] = static_cast<uint32_t>(gChannelError);
  g_ot_diag_responder_results[2] = static_cast<uint32_t>(gPowerError);
  g_ot_diag_responder_results[3] = static_cast<uint32_t>(gStartError);
  g_ot_diag_responder_results[4] = static_cast<uint32_t>(gSendError);
  g_ot_diag_responder_results[5] = static_cast<uint32_t>(gStatsError);
  g_ot_diag_responder_results[6] = gDiagOutputCount;
  g_ot_diag_responder_results[7] = gDiagOutputBytes;
  g_ot_diag_responder_results[8] = gRequestSeen ? 1U : 0U;
  g_ot_diag_responder_results[9] = gRequestLength;
  g_ot_diag_responder_results[10] = gRequestChannel;
  g_ot_diag_responder_results[11] = static_cast<uint32_t>(gRequestRssiDbm);
  g_ot_diag_responder_results[12] = gRequestSequence;
  g_ot_diag_responder_results[13] = gRequestSourceShort;
  g_ot_diag_responder_results[14] = gRequestDestinationShort;
  g_ot_diag_responder_results[15] = gRequestPayloadValid ? 1U : 0U;
  g_ot_diag_responder_results[16] = gTxDoneCount;
  g_ot_diag_responder_results[17] = static_cast<uint32_t>(gLastTxError);
  g_ot_diag_responder_results[18] = gLastTxSequence;
  g_ot_diag_responder_results[19] = snapshot.diagTxCount;
  g_ot_diag_responder_results[20] = snapshot.diagRxCount;
  g_ot_diag_responder_results[21] = snapshot.diagLastTxError;
  g_ot_diag_responder_results[22] = snapshot.diagLastRxLength;
  g_ot_diag_responder_results[23] = millis() - gBootMs;
}

}  // namespace

extern "C" void otPlatRadioTxDone(otInstance*, otRadioFrame* frame,
                                  otRadioFrame*, otError error) {
  ++gTxDoneCount;
  gLastTxError = error;
  gLastTxSequence = (frame != nullptr && frame->mPsdu != nullptr && frame->mLength >= 3U)
                        ? frame->mPsdu[2]
                        : 0U;
}

extern "C" void otPlatDiagRadioReceived(otInstance*, otRadioFrame* frame,
                                        otError error) {
  if (error != OT_ERROR_NONE || frame == nullptr || frame->mPsdu == nullptr) {
    return;
  }

  ZigbeeDataFrameView view = {};
  if (!ZigbeeRadio::parseDataFrameShort(frame->mPsdu, frame->mLength, &view) ||
      !view.valid) {
    return;
  }

  if (!payloadMatchesPattern(view.payload, view.payloadLength, kRequestPattern)) {
    return;
  }

  gRequestSeen = true;
  gRequestLength = frame->mLength;
  gRequestChannel = frame->mChannel;
  gRequestRssiDbm = frame->mInfo.mRxInfo.mRssi;
  gRequestSequence = view.sequence;
  gRequestSourceShort = view.sourceShort;
  gRequestDestinationShort = view.destinationShort;
  gRequestPayloadValid = true;
  gResponsePending = true;
  gResponseReadyMs = millis() + 120UL;
  gResponseAttempts = 0U;
}

void setup() {
  gBootMs = millis();
  OpenThreadPlatformSkeleton::begin();
  otPlatDiagSetOutputCallback(nullptr, diagOutputCallback, nullptr);

  gVersionError = runDiagCommand("version");
  gChannelError = runDiagCommand("channel", "15");
  gPowerError = runDiagCommand("power", "0");
  gStartError = runDiagCommand("start");
  updateResults();
}

void loop() {
  OpenThreadPlatformSkeleton::process();

  if (gResponsePending && gSendError == OT_ERROR_NONE &&
      gResponseAttempts < 3U &&
      gTxDoneCount == gResponseAttempts &&
      static_cast<int32_t>(millis() - gResponseReadyMs) >= 0) {
    ++gResponseAttempts;
    gSendError = runDiagCommand("send", "8", "0xB2");
    gResponseReadyMs = millis() + 250UL;
  }

  if (gResponsePending && gResponseAttempts >= 3U &&
      gTxDoneCount >= gResponseAttempts) {
    gResponsePending = false;
  }

  if (gRequestSeen && !gResponsePending && gTxDoneCount >= 1U && !gStatsIssued) {
    gStatsIssued = true;
    gStatsError = runDiagCommand("stats");
  }

  updateResults();
}
