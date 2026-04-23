#pragma once

#include <stddef.h>
#include <stdint.h>

#include <openthread/error.h>
#include <openthread/platform/radio.h>

namespace xiao_nrf54l15 {

struct OpenThreadPlatformSkeletonSnapshot {
  static constexpr size_t kRecentLogLineCount = 8U;
  static constexpr size_t kRecentLogLineLength = 112U;

  bool initialized = false;
  bool settingsInitialized = false;
  bool eventPending = false;
  bool pseudoResetRequested = false;
  bool diagModeEnabled = false;
  bool diagRadioActive = false;

  bool alarmMilliRunning = false;
  bool alarmMicroRunning = false;
  uint32_t alarmMilliDeadline = 0;
  uint32_t alarmMicroDeadline = 0;
  uint32_t alarmMilliFires = 0;
  uint32_t alarmMicroFires = 0;

  otRadioCaps radioCaps = OT_RADIO_CAPS_NONE;
  otRadioState radioState = OT_RADIO_STATE_DISABLED;
  bool radioEnabled = false;
  bool radioBackendWrappedDirect = false;
  bool radioBackendReady = false;
  bool radioEnergyScanPending = false;
  bool radioPromiscuous = false;
  bool radioRxOnWhenIdle = false;
  bool radioSrcMatchEnabled = false;
  bool radioLastTxAcked = false;
  bool radioLastTxAckFramePending = false;
  bool radioLastSrcMatchMatched = false;
  bool radioLastSrcMatchWasShort = false;
  uint8_t radioChannel = 0;
  uint8_t radioLastEdLevel = 0;
  uint8_t radioLastTxLength = 0;
  uint8_t radioLastTxAckLength = 0;
  uint8_t radioLastRxLength = 0;
  uint8_t radioLastError = OT_ERROR_NONE;
  uint8_t radioLastTxSequence = 0;
  uint8_t radioLastRxSequence = 0;
  uint8_t radioLastTxFrameType = 0;
  uint8_t radioLastRxFrameType = 0;
  uint8_t radioLastTxDstAddrMode = 0;
  uint8_t radioLastRxDstAddrMode = 0;
  uint8_t diagLastTxLength = 0;
  uint8_t diagLastTxSequence = 0;
  uint8_t diagLastRxLength = 0;
  uint8_t diagLastRxChannel = 0;
  uint8_t diagLastRxSequence = 0;
  uint8_t diagLastTxError = OT_ERROR_NONE;
  uint8_t cslAccuracyPpm = 0;
  uint8_t cslUncertainty10us = 0;
  uint8_t radioSrcMatchShortCount = 0;
  uint8_t radioSrcMatchExtCount = 0;

  otPanId panId = 0;
  otShortAddress shortAddress = 0;
  otShortAddress alternateShortAddress = OT_RADIO_INVALID_SHORT_ADDR;
  otShortAddress radioLastSrcMatchShortAddress = OT_RADIO_INVALID_SHORT_ADDR;
  otShortAddress radioLastTxDestinationShort = OT_RADIO_INVALID_SHORT_ADDR;
  otShortAddress radioLastRxDestinationShort = OT_RADIO_INVALID_SHORT_ADDR;
  otExtAddress extendedAddress = {};
  int8_t txPowerDbm = OT_RADIO_POWER_INVALID;
  int8_t ccaThresholdDbm = 0;
  int8_t femLnaGainDbm = 0;
  int8_t lastRssiDbm = OT_RADIO_RSSI_INVALID;
  int8_t diagLastRxRssi = OT_RADIO_RSSI_INVALID;
  int8_t receiveSensitivityDbm = -100;
  uint16_t regionCode = 0;

  uint16_t sensitiveKeyCount = 0;
  uint16_t settingsKeyCount = 0;
  uint16_t lastSettingsKey = 0;
  uint16_t lastSettingsLength = 0;
  bool cryptoInitialized = false;
  bool cryptoRandomHardware = false;
  bool cryptoAesReady = false;
  uint16_t cryptoKeyCount = 0;
  uint16_t cryptoLastKeyLength = 0;
  uint32_t cryptoRandomRequests = 0;
  uint32_t cryptoAesEncryptCount = 0;
  uint32_t cryptoUnsupportedCount = 0;
  uint32_t cryptoSupportMask = 0;

  uint32_t processCount = 0;
  uint32_t diagProcessCount = 0;
  uint32_t txRequestCount = 0;
  uint32_t radioTxDoneCount = 0;
  uint32_t radioRxDoneCount = 0;
  uint32_t radioEnergyScanCount = 0;
  uint32_t radioReceivePollCount = 0;
  uint32_t radioFilteredCount = 0;
  uint32_t radioRxCrcErrorCount = 0;
  uint32_t radioRxInvalidLengthCount = 0;
  uint32_t radioSrcMatchAckSetCount = 0;
  uint32_t radioSrcMatchAckClearCount = 0;
  uint32_t diagTxCount = 0;
  uint32_t diagRxCount = 0;

  uint64_t radioNowUs = 0;
  uint8_t radioLastTxHeader[10] = {0};
  uint8_t radioLastRxHeader[10] = {0};
  uint8_t radioLastRxPhr = 0;
  uint8_t radioLastRejectedLength = 0;
  char lastLogLine[kRecentLogLineLength] = {0};
  uint32_t recentLogCount = 0;
  uint32_t recentMleLogCount = 0;
  char recentLogLines[kRecentLogLineCount][kRecentLogLineLength] = {{0}};
  char recentMleLogLines[kRecentLogLineCount][kRecentLogLineLength] = {{0}};

  bool threadCoreDebugValid = false;
  bool threadAttachInProgress = false;
  bool threadAttachTimerRunning = false;
  bool threadReceivedResponseFromParent = false;
  uint8_t threadAttachState = 0;
  uint8_t threadAttachMode = 0;
  uint8_t threadReattachMode = 0;
  uint8_t threadParentRequestCounter = 0;
  uint8_t threadChildIdRequestsRemaining = 0;
  uint8_t threadParentCandidateState = 0;
  uint16_t threadAttachCounter = 0;
  uint16_t threadParentCandidateRloc16 = OT_RADIO_INVALID_SHORT_ADDR;
  uint32_t threadAttachTimerRemainingMs = 0;
  char threadAttachStateName[16] = {0};
  char threadAttachModeName[20] = {0};
  char threadReattachModeName[24] = {0};
  char threadParentCandidateStateName[16] = {0};
};

struct OpenThreadRuntimeOwnership {
  static constexpr bool kCpuAppHostsCore = true;
  static constexpr bool kUsesZigbeeRadioBackend = true;
  static constexpr bool kUsesVprRadioOffload = false;
  static constexpr bool kUsesHalAlarmTimebase = true;
  static constexpr bool kUsesPreferencesSettings = true;
  static constexpr bool kUsesCracenEntropy = true;
  static constexpr bool kUsesCracenAesEcb = true;
  static constexpr bool kHeadersOnlyImportActive = true;
  static constexpr bool kCoreBuildSeamAvailable = true;
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  static constexpr bool kCoreBuildSeamCurrentEnabled = true;
#else
  static constexpr bool kCoreBuildSeamCurrentEnabled = false;
#endif
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_CRYPTO_FALLBACK_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_CRYPTO_FALLBACK_ENABLE != 0)
  static constexpr bool kCoreCryptoFallbackCurrentEnabled = true;
#else
  static constexpr bool kCoreCryptoFallbackCurrentEnabled = false;
#endif
  static constexpr bool kFullCoreIntegrated = false;
  static constexpr bool kMatterIntegrated = false;
  static constexpr bool kFirstPassBoardXiaoNrf54L15 = true;
  static constexpr bool kFirstPassRoleDetached = true;
  static constexpr bool kFirstPassRoleChild = true;
  static constexpr bool kFirstPassRoleRouter = true;
  static constexpr const char* kPlatformGluePath =
      "hardware/nrf54l15clean/nrf54l15clean/libraries/"
      "Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.cpp";
  static constexpr const char* kCoreImportScriptPath =
      "scripts/import_openthread_core_scaffold.sh";
  static constexpr const char* kCoreStagingPath =
      "hardware/nrf54l15clean/nrf54l15clean/libraries/"
      "Nrf54L15-Clean-Implementation/third_party/openthread-core";
  static constexpr const char* kCoreBridgePath =
      "hardware/nrf54l15clean/nrf54l15clean/libraries/"
      "Nrf54L15-Clean-Implementation/src/openthread_core_stage_bridge.cpp";
  static constexpr const char* kCoreUserConfigPath =
      "hardware/nrf54l15clean/nrf54l15clean/libraries/"
      "Nrf54L15-Clean-Implementation/src/openthread-core-user-config.h";
};

class OpenThreadPlatformSkeleton {
 public:
  static void begin();
  static void end();
  static void process(otInstance* instance = nullptr);
  static bool snapshot(OpenThreadPlatformSkeletonSnapshot* outSnapshot);

  static otError fillEntropy(uint8_t* output, uint16_t outputLength);
  static otError writeSetting(uint16_t key, const uint8_t* value, uint16_t valueLength);
  static otError addSetting(uint16_t key, const uint8_t* value, uint16_t valueLength);
  static otError readSetting(uint16_t key, int index, uint8_t* value, uint16_t* valueLength);
  static otError deleteSetting(uint16_t key, int index);
  static void wipeSettings();
};

}  // namespace xiao_nrf54l15
