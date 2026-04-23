#include "openthread_platform_nrf54l15.h"

#include <openthread/dataset.h>
#include <openthread/dataset_ftd.h>
#include <openthread/ip6.h>
#include <openthread/link.h>
#include <openthread/platform/settings.h>
#include <openthread/thread.h>

using xiao_nrf54l15::OpenThreadPlatformSkeleton;
using xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot;
using xiao_nrf54l15::OpenThreadRuntimeOwnership;

namespace {

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
constexpr uint32_t kStageInitDelayMs = 2000UL;
constexpr uint32_t kStageDatasetDelayMs = 3000UL;
constexpr uint32_t kStageDatasetApplyDelayMs = 4000UL;
constexpr uint32_t kStageIp6EnableDelayMs = 5000UL;
constexpr uint32_t kStageThreadEnableDelayMs = 6000UL;
constexpr otPanId kFixedPanId = 0x5D6A;
constexpr uint32_t kFixedChannelMask = OT_CHANNEL_15_MASK;
constexpr uint8_t kFixedChannel = 15U;
constexpr char kFixedNetworkName[] = "Nrf54Stage";
constexpr uint8_t kFixedNetworkKey[OT_NETWORK_KEY_SIZE] = {
    0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
};
constexpr uint8_t kFixedExtPanId[OT_EXT_PAN_ID_SIZE] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
};
constexpr uint8_t kFixedMeshLocalPrefix[OT_MESH_LOCAL_PREFIX_SIZE] = {
    0xFD, 0x54, 0x15, 0xC0, 0xDE, 0x00, 0x00, 0x00,
};
constexpr uint8_t kFixedPskc[OT_PSKC_MAX_SIZE] = {
    0xA5, 0x4C, 0x8D, 0x11, 0x72, 0x3F, 0x90, 0xBE,
    0x4A, 0x62, 0x18, 0xD4, 0xCE, 0x07, 0x39, 0x5B,
};
otInstance* gInstance = nullptr;
bool gInstanceCreated = false;
bool gInstanceInitialized = false;
bool gDatasetGenerated = false;
bool gDatasetApplied = false;
bool gIp6EnableAttempted = false;
bool gThreadEnableAttempted = false;
otOperationalDataset gDataset = {};
otOperationalDataset gActiveDataset = {};
otError gDatasetCreateError = OT_ERROR_NONE;
otError gDatasetSetError = OT_ERROR_NONE;
otError gDatasetReadError = OT_ERROR_NONE;
otError gLinkModeSetError = OT_ERROR_NONE;
otError gIp6SetError = OT_ERROR_NONE;
otError gThreadSetError = OT_ERROR_NONE;
otDeviceRole gLastRole = OT_DEVICE_ROLE_DISABLED;
volatile uint32_t gMacTxTotal = 0;
volatile uint32_t gMacTxData = 0;
volatile uint32_t gMacTxDataPoll = 0;
volatile uint32_t gMacRxTotal = 0;
volatile uint32_t gMacRxData = 0;
volatile uint32_t gMacRxDataPoll = 0;
volatile uint32_t gMacRxErrNoFrame = 0;
volatile uint32_t gMacRxErrUnknownNeighbor = 0;
volatile uint32_t gMacRxErrSec = 0;
volatile uint32_t gMacRxAddressFiltered = 0;
volatile uint32_t gMacRxDestAddrFiltered = 0;
volatile uint32_t gMacRxDuplicated = 0;
volatile uint32_t gPollPeriodMs = 0;
volatile otShortAddress gMacShortAddress = OT_RADIO_INVALID_SHORT_ADDR;
volatile uint32_t gPalProcessCount = 0;
volatile uint32_t gPalTxRequestCount = 0;
volatile uint32_t gPalRadioTxDoneCount = 0;
volatile uint32_t gPalRadioRxDoneCount = 0;
volatile uint32_t gPalRadioReceivePollCount = 0;
volatile uint32_t gPalRadioFilteredCount = 0;
volatile uint32_t gPalRadioRxCrcErrorCount = 0;
volatile uint32_t gPalRadioRxInvalidLengthCount = 0;
volatile uint8_t gPalLastTxLength = 0;
volatile uint8_t gPalLastRxLength = 0;
volatile uint8_t gPalLastRadioError = 0;
volatile uint8_t gPalRadioState = OT_RADIO_STATE_DISABLED;
volatile uint8_t gPalRadioEnabled = 0;
volatile uint8_t gPalRadioRxOnWhenIdle = 0;
volatile otShortAddress gPalLastTxDestinationShort = OT_RADIO_INVALID_SHORT_ADDR;
volatile otShortAddress gPalLastRxDestinationShort = OT_RADIO_INVALID_SHORT_ADDR;
volatile uint8_t gPalLastTxHeader[10] = {0};
volatile uint8_t gPalLastRxHeader[10] = {0};
volatile uint8_t gPalLastRxPhr = 0;
volatile uint8_t gPalLastRejectedLength = 0;
volatile uint16_t gThreadRloc16 = 0xffff;
volatile uint16_t gThreadParentRloc16 = 0xffff;
volatile uint32_t gThreadPartitionId = 0;
volatile uint16_t gMleAttachAttempts = 0;
volatile uint16_t gMleDetachedRoleCount = 0;
volatile uint16_t gMleChildRoleCount = 0;
volatile uint16_t gMleRouterRoleCount = 0;
volatile uint16_t gMleLeaderRoleCount = 0;
volatile uint16_t gMleParentChanges = 0;
volatile otError gThreadParentInfoError = OT_ERROR_NONE;
volatile uint8_t gThreadAttachDebugValid = 0;
volatile uint8_t gThreadAttachInProgress = 0;
volatile uint8_t gThreadAttachTimerRunning = 0;
volatile uint8_t gThreadReceivedResponseFromParent = 0;
volatile uint8_t gThreadAttachState = 0;
volatile uint8_t gThreadAttachMode = 0;
volatile uint8_t gThreadReattachMode = 0;
volatile uint8_t gThreadParentRequestCounter = 0;
volatile uint8_t gThreadChildIdRequestsRemaining = 0;
volatile uint8_t gThreadParentCandidateState = 0;
volatile uint16_t gThreadAttachCounter = 0;
volatile uint16_t gThreadParentCandidateRloc16 = OT_RADIO_INVALID_SHORT_ADDR;
volatile uint32_t gThreadAttachTimerRemainingMs = 0;
volatile char gThreadAttachStateName[16] = {0};
volatile char gThreadAttachModeName[20] = {0};
volatile char gThreadReattachModeName[24] = {0};
volatile char gThreadParentCandidateStateName[16] = {0};
volatile char gPalLastLogLine[OpenThreadPlatformSkeletonSnapshot::kRecentLogLineLength] = {0};
volatile uint32_t gPalRecentLogCount = 0;
volatile uint32_t gPalRecentMleLogCount = 0;
volatile char gPalRecentLogLines[OpenThreadPlatformSkeletonSnapshot::kRecentLogLineCount]
                                [OpenThreadPlatformSkeletonSnapshot::kRecentLogLineLength] = {{0}};
volatile char gPalRecentMleLogLines[OpenThreadPlatformSkeletonSnapshot::kRecentLogLineCount]
                                   [OpenThreadPlatformSkeletonSnapshot::kRecentLogLineLength] = {{0}};
#endif

uint32_t gLastReportMs = 0;
uint32_t gLastPrintedRecentMleLogCount = 0;

bool serialLogReady() {
  return static_cast<bool>(Serial);
}

void logLine(const char* line) {
  if (!serialLogReady()) {
    return;
  }
  Serial.println(line);
}

const char* roleToString(otDeviceRole role) {
  switch (role) {
    case OT_DEVICE_ROLE_DISABLED:
      return "disabled";
    case OT_DEVICE_ROLE_DETACHED:
      return "detached";
    case OT_DEVICE_ROLE_CHILD:
      return "child";
    case OT_DEVICE_ROLE_ROUTER:
      return "router";
    case OT_DEVICE_ROLE_LEADER:
      return "leader";
    default:
      return "unknown";
  }
}

void buildFixedDataset(otOperationalDataset* dataset) {
  if (dataset == nullptr) {
    return;
  }

  memset(dataset, 0, sizeof(*dataset));
  dataset->mActiveTimestamp.mSeconds = 1ULL;
  dataset->mActiveTimestamp.mTicks = 0U;
  dataset->mActiveTimestamp.mAuthoritative = true;
  memcpy(dataset->mNetworkKey.m8, kFixedNetworkKey, sizeof(kFixedNetworkKey));
  strncpy(dataset->mNetworkName.m8,
          kFixedNetworkName,
          sizeof(dataset->mNetworkName.m8) - 1U);
  memcpy(dataset->mExtendedPanId.m8, kFixedExtPanId, sizeof(kFixedExtPanId));
  memcpy(dataset->mMeshLocalPrefix.m8,
         kFixedMeshLocalPrefix,
         sizeof(kFixedMeshLocalPrefix));
  memcpy(dataset->mPskc.m8, kFixedPskc, sizeof(kFixedPskc));
  dataset->mPanId = kFixedPanId;
  dataset->mChannel = kFixedChannel;
  dataset->mWakeupChannel = kFixedChannel;
  dataset->mChannelMask = kFixedChannelMask;
  dataset->mSecurityPolicy.mRotationTime = 672U;
  dataset->mSecurityPolicy.mObtainNetworkKeyEnabled = true;
  dataset->mSecurityPolicy.mNativeCommissioningEnabled = true;
  dataset->mSecurityPolicy.mRoutersEnabled = true;
  dataset->mSecurityPolicy.mExternalCommissioningEnabled = true;
  dataset->mSecurityPolicy.mCommercialCommissioningEnabled = false;
  dataset->mSecurityPolicy.mAutonomousEnrollmentEnabled = false;
  dataset->mSecurityPolicy.mNetworkKeyProvisioningEnabled = true;
  dataset->mSecurityPolicy.mTobleLinkEnabled = false;
  dataset->mSecurityPolicy.mNonCcmRoutersEnabled = false;
  dataset->mSecurityPolicy.mVersionThresholdForRouting = 3U;

  dataset->mComponents.mIsActiveTimestampPresent = true;
  dataset->mComponents.mIsNetworkKeyPresent = true;
  dataset->mComponents.mIsNetworkNamePresent = true;
  dataset->mComponents.mIsExtendedPanIdPresent = true;
  dataset->mComponents.mIsMeshLocalPrefixPresent = true;
  dataset->mComponents.mIsPanIdPresent = true;
  dataset->mComponents.mIsChannelPresent = true;
  dataset->mComponents.mIsPskcPresent = true;
  dataset->mComponents.mIsSecurityPolicyPresent = true;
  dataset->mComponents.mIsChannelMaskPresent = true;
  dataset->mComponents.mIsWakeupChannelPresent = true;
}

void printDatasetLine(const otOperationalDataset& dataset) {
  Serial.print(" dataset=");
  Serial.print(dataset.mNetworkName.m8);
  Serial.print("/ch");
  Serial.print(dataset.mChannel);
  Serial.print("/pan=0x");
  Serial.print(dataset.mPanId, HEX);
  Serial.print("/mask=0x");
  Serial.print(dataset.mChannelMask, HEX);
  Serial.print("/ts=");
  Serial.print(static_cast<unsigned long>(dataset.mActiveTimestamp.mSeconds));
}

void printRoleState() {
  if (!serialLogReady()) {
    return;
  }

  Serial.print("ot_role seam=");
  Serial.print(OpenThreadRuntimeOwnership::kCoreBuildSeamAvailable ? 1 : 0);
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kCoreBuildSeamCurrentEnabled ? 1 : 0);
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kCoreCryptoFallbackCurrentEnabled
                   ? 1
                   : 0);
  Serial.print(" ownership=");
  Serial.print(OpenThreadRuntimeOwnership::kCpuAppHostsCore ? "cpuapp" : "other");
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kUsesZigbeeRadioBackend
                   ? "zigbee-radio"
                   : "other-radio");
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kUsesVprRadioOffload ? "vpr" : "no-vpr");
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kHeadersOnlyImportActive
                   ? "headers-only"
                   : "full-core");
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kFullCoreIntegrated ? "core-live"
                                                               : "core-staged");

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  Serial.print(" init=");
  Serial.print(gInstanceCreated ? 1 : 0);
  Serial.print("/");
  Serial.print(gInstanceInitialized ? 1 : 0);
  Serial.print(" dataset=");
  Serial.print(static_cast<int>(gDatasetCreateError));
  Serial.print("/");
  Serial.print(gDatasetGenerated ? 1 : 0);
  Serial.print("/");
  Serial.print(static_cast<int>(gDatasetSetError));
  Serial.print("/");
  Serial.print(gDatasetApplied ? 1 : 0);
  Serial.print("/");
  Serial.print(static_cast<int>(gDatasetReadError));
  Serial.print(" mode=");
  Serial.print(static_cast<int>(gLinkModeSetError));
  Serial.print(" ip6=");
  Serial.print(static_cast<int>(gIp6SetError));
  Serial.print("/");
  Serial.print(gInstance != nullptr && otIp6IsEnabled(gInstance) ? 1 : 0);
  Serial.print(" thread=");
  Serial.print(static_cast<int>(gThreadSetError));
  Serial.print("/");
  Serial.print(gThreadEnableAttempted ? 1 : 0);
  Serial.print("/");
  Serial.print(roleToString(gLastRole));
  if (gThreadAttachDebugValid != 0U) {
    char attachStateName[sizeof(gThreadAttachStateName)] = {0};
    char attachModeName[sizeof(gThreadAttachModeName)] = {0};
    char reattachModeName[sizeof(gThreadReattachModeName)] = {0};
    char parentCandidateStateName[sizeof(gThreadParentCandidateStateName)] = {0};
    memcpy(attachStateName, const_cast<const char*>(gThreadAttachStateName),
           sizeof(attachStateName));
    memcpy(attachModeName, const_cast<const char*>(gThreadAttachModeName),
           sizeof(attachModeName));
    memcpy(reattachModeName, const_cast<const char*>(gThreadReattachModeName),
           sizeof(reattachModeName));
    memcpy(parentCandidateStateName,
           const_cast<const char*>(gThreadParentCandidateStateName),
           sizeof(parentCandidateStateName));
    Serial.print(" attach=");
    Serial.print(attachStateName);
    Serial.print("/");
    Serial.print(attachModeName);
    Serial.print("/");
    Serial.print(reattachModeName);
    Serial.print(" req=");
    Serial.print(static_cast<unsigned long>(gThreadParentRequestCounter));
    Serial.print(" child=");
    Serial.print(static_cast<unsigned long>(gThreadChildIdRequestsRemaining));
    Serial.print(" cnt=");
    Serial.print(static_cast<unsigned long>(gThreadAttachCounter));
    Serial.print(" tmr=");
    if (gThreadAttachTimerRunning != 0U) {
      Serial.print(static_cast<unsigned long>(gThreadAttachTimerRemainingMs));
    } else {
      Serial.print("-");
    }
    Serial.print(" cand=");
    Serial.print(parentCandidateStateName);
    Serial.print("@0x");
    Serial.print(gThreadParentCandidateRloc16, HEX);
    Serial.print(" act=");
    Serial.print(gThreadAttachInProgress != 0U ? 1 : 0);
    Serial.print(" rsp=");
    Serial.print(gThreadReceivedResponseFromParent != 0U ? 1 : 0);
  }
  printDatasetLine(gActiveDataset.mComponents.mIsNetworkNamePresent ? gActiveDataset
                                                                    : gDataset);

  if (gInstance != nullptr) {
    const otMacCounters* counters = otLinkGetCounters(gInstance);
    Serial.print(" addr=0x");
    Serial.print(otLinkGetShortAddress(gInstance), HEX);
    Serial.print(" poll=");
    Serial.print(static_cast<unsigned long>(otLinkGetPollPeriod(gInstance)));
    if (counters != nullptr) {
      Serial.print(" tx=");
      Serial.print(static_cast<unsigned long>(counters->mTxTotal));
      Serial.print("/");
      Serial.print(static_cast<unsigned long>(counters->mTxData));
      Serial.print("/");
      Serial.print(static_cast<unsigned long>(counters->mTxDataPoll));
      Serial.print(" rx=");
      Serial.print(static_cast<unsigned long>(counters->mRxTotal));
      Serial.print("/");
      Serial.print(static_cast<unsigned long>(counters->mRxData));
      Serial.print("/");
      Serial.print(static_cast<unsigned long>(counters->mRxDataPoll));
      Serial.print(" drop=");
      Serial.print(static_cast<unsigned long>(counters->mRxErrNoFrame));
      Serial.print("/");
      Serial.print(static_cast<unsigned long>(counters->mRxErrUnknownNeighbor));
      Serial.print("/");
      Serial.print(static_cast<unsigned long>(counters->mRxErrSec));
      Serial.print("/");
      Serial.print(static_cast<unsigned long>(counters->mRxAddressFiltered));
      Serial.print("/");
      Serial.print(static_cast<unsigned long>(counters->mRxDestAddrFiltered));
      Serial.print("/");
      Serial.print(static_cast<unsigned long>(counters->mRxDuplicated));
    }
    Serial.print(" palrx=");
    Serial.print(static_cast<unsigned long>(gPalRadioRxDoneCount));
    Serial.print("/");
    Serial.print(static_cast<unsigned long>(gPalRadioReceivePollCount));
    Serial.print("/");
    Serial.print(static_cast<unsigned long>(gPalRadioFilteredCount));
    Serial.print("/");
    Serial.print(static_cast<unsigned long>(gPalRadioRxCrcErrorCount));
    Serial.print("/");
    Serial.print(static_cast<unsigned long>(gPalRadioRxInvalidLengthCount));
    Serial.print("/phr=");
    Serial.print(static_cast<unsigned long>(gPalLastRxPhr));
    Serial.print("/rej=");
    Serial.print(static_cast<unsigned long>(gPalLastRejectedLength));
    Serial.print(" paltx=");
    Serial.print(static_cast<unsigned long>(gPalLastTxLength));
    Serial.print("/palrxlen=");
    Serial.print(static_cast<unsigned long>(gPalLastRxLength));
    Serial.print("/err=");
    Serial.print(static_cast<unsigned long>(gPalLastRadioError));
  }
#else
  Serial.print(" status=disabled");
#endif
  Serial.println();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  logLine("ot_role boot");
  OpenThreadPlatformSkeleton::begin();
  otPlatSettingsInit(nullptr, nullptr, 0);
  otPlatSettingsWipe(nullptr);
  logLine("ot_role platform-ready");
  printRoleState();
}

void loop() {
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  OpenThreadPlatformSkeleton::process(gInstance);

  OpenThreadPlatformSkeletonSnapshot snapshot = {};
  if (OpenThreadPlatformSkeleton::snapshot(&snapshot)) {
    gPalProcessCount = snapshot.processCount;
    gPalTxRequestCount = snapshot.txRequestCount;
    gPalRadioTxDoneCount = snapshot.radioTxDoneCount;
    gPalRadioRxDoneCount = snapshot.radioRxDoneCount;
    gPalRadioReceivePollCount = snapshot.radioReceivePollCount;
    gPalRadioFilteredCount = snapshot.radioFilteredCount;
    gPalRadioRxCrcErrorCount = snapshot.radioRxCrcErrorCount;
    gPalRadioRxInvalidLengthCount = snapshot.radioRxInvalidLengthCount;
    gPalLastTxLength = snapshot.radioLastTxLength;
    gPalLastRxLength = snapshot.radioLastRxLength;
    gPalLastRadioError = snapshot.radioLastError;
    gPalRadioState = static_cast<uint8_t>(snapshot.radioState);
    gPalRadioEnabled = snapshot.radioEnabled ? 1U : 0U;
    gPalRadioRxOnWhenIdle = snapshot.radioRxOnWhenIdle ? 1U : 0U;
    gPalLastRxPhr = snapshot.radioLastRxPhr;
    gPalLastRejectedLength = snapshot.radioLastRejectedLength;
    gPalLastTxDestinationShort = snapshot.radioLastTxDestinationShort;
    gPalLastRxDestinationShort = snapshot.radioLastRxDestinationShort;
    gThreadAttachDebugValid = snapshot.threadCoreDebugValid ? 1U : 0U;
    gThreadAttachInProgress = snapshot.threadAttachInProgress ? 1U : 0U;
    gThreadAttachTimerRunning = snapshot.threadAttachTimerRunning ? 1U : 0U;
    gThreadReceivedResponseFromParent =
        snapshot.threadReceivedResponseFromParent ? 1U : 0U;
    gThreadAttachState = snapshot.threadAttachState;
    gThreadAttachMode = snapshot.threadAttachMode;
    gThreadReattachMode = snapshot.threadReattachMode;
    gThreadParentRequestCounter = snapshot.threadParentRequestCounter;
    gThreadChildIdRequestsRemaining = snapshot.threadChildIdRequestsRemaining;
    gThreadParentCandidateState = snapshot.threadParentCandidateState;
    gThreadAttachCounter = snapshot.threadAttachCounter;
    gThreadParentCandidateRloc16 = snapshot.threadParentCandidateRloc16;
    gThreadAttachTimerRemainingMs = snapshot.threadAttachTimerRemainingMs;
    memcpy(const_cast<uint8_t*>(gPalLastTxHeader), snapshot.radioLastTxHeader,
           sizeof(gPalLastTxHeader));
    memcpy(const_cast<uint8_t*>(gPalLastRxHeader), snapshot.radioLastRxHeader,
           sizeof(gPalLastRxHeader));
    memcpy(const_cast<char*>(gThreadAttachStateName),
           snapshot.threadAttachStateName,
           sizeof(gThreadAttachStateName));
    memcpy(const_cast<char*>(gThreadAttachModeName),
           snapshot.threadAttachModeName,
           sizeof(gThreadAttachModeName));
    memcpy(const_cast<char*>(gThreadReattachModeName),
           snapshot.threadReattachModeName,
           sizeof(gThreadReattachModeName));
    memcpy(const_cast<char*>(gThreadParentCandidateStateName),
           snapshot.threadParentCandidateStateName,
           sizeof(gThreadParentCandidateStateName));
    memcpy(const_cast<char*>(gPalLastLogLine), snapshot.lastLogLine,
           sizeof(gPalLastLogLine));
    gPalRecentLogCount = snapshot.recentLogCount;
    gPalRecentMleLogCount = snapshot.recentMleLogCount;
    for (size_t index = 0;
         index < OpenThreadPlatformSkeletonSnapshot::kRecentLogLineCount;
         ++index) {
      memcpy(const_cast<char*>(gPalRecentLogLines[index]),
             snapshot.recentLogLines[index],
             sizeof(gPalRecentLogLines[index]));
      memcpy(const_cast<char*>(gPalRecentMleLogLines[index]),
             snapshot.recentMleLogLines[index],
             sizeof(gPalRecentMleLogLines[index]));
    }

    if (snapshot.recentMleLogCount != gLastPrintedRecentMleLogCount) {
      gLastPrintedRecentMleLogCount = snapshot.recentMleLogCount;
      for (size_t index = 0;
           index < OpenThreadPlatformSkeletonSnapshot::kRecentLogLineCount;
           ++index) {
        if (snapshot.recentMleLogLines[index][0] == '\0') {
          continue;
        }

        if (serialLogReady()) {
          Serial.print("ot_mle[");
          Serial.print(index);
          Serial.print("] ");
          Serial.println(snapshot.recentMleLogLines[index]);
        }
      }
    }
  }

  if (!gInstanceCreated && millis() >= kStageInitDelayMs) {
    logLine("ot_role init-begin");
    gInstance = otInstanceInitSingle();
    gInstanceCreated = gInstance != nullptr;
    gInstanceInitialized =
        gInstanceCreated && otInstanceIsInitialized(gInstance);
    if (gInstanceCreated) {
      gLastRole = otThreadGetDeviceRole(gInstance);
    }
    logLine("ot_role init-end");
    printRoleState();
  }

  if (gInstanceInitialized && !gDatasetGenerated &&
      millis() >= kStageDatasetDelayMs) {
    buildFixedDataset(&gDataset);
    gDatasetCreateError = OT_ERROR_NONE;
    gDatasetGenerated = true;
    logLine("ot_role dataset-generated");
    printRoleState();
  }

  if (gDatasetGenerated && !gDatasetApplied &&
      millis() >= kStageDatasetApplyDelayMs) {
    gDatasetSetError = otDatasetSetActive(gInstance, &gDataset);
    if (gDatasetSetError == OT_ERROR_NONE) {
      gDatasetReadError = otDatasetGetActive(gInstance, &gActiveDataset);
      gDatasetApplied = (gDatasetReadError == OT_ERROR_NONE);
    }
    logLine("ot_role dataset-applied");
    printRoleState();
  }

  if (gDatasetApplied && !gIp6EnableAttempted &&
      millis() >= kStageIp6EnableDelayMs) {
    gIp6EnableAttempted = true;
    const otLinkModeConfig mode = {true, true, true};
    gLinkModeSetError = otThreadSetLinkMode(gInstance, mode);
    gIp6SetError = otIp6SetEnabled(gInstance, true);
    logLine("ot_role ip6-enabled");
    printRoleState();
  }

  if (gIp6EnableAttempted && !gThreadEnableAttempted &&
      millis() >= kStageThreadEnableDelayMs) {
    gThreadEnableAttempted = true;
    gThreadSetError = otThreadSetEnabled(gInstance, true);
    logLine("ot_role thread-enabled");
    printRoleState();
  }

  if (gInstanceCreated) {
    const otMacCounters* counters = otLinkGetCounters(gInstance);
    gMacShortAddress = otLinkGetShortAddress(gInstance);
    gPollPeriodMs = otLinkGetPollPeriod(gInstance);
    if (counters != nullptr) {
      gMacTxTotal = counters->mTxTotal;
      gMacTxData = counters->mTxData;
      gMacTxDataPoll = counters->mTxDataPoll;
      gMacRxTotal = counters->mRxTotal;
      gMacRxData = counters->mRxData;
      gMacRxDataPoll = counters->mRxDataPoll;
      gMacRxErrNoFrame = counters->mRxErrNoFrame;
      gMacRxErrUnknownNeighbor = counters->mRxErrUnknownNeighbor;
      gMacRxErrSec = counters->mRxErrSec;
      gMacRxAddressFiltered = counters->mRxAddressFiltered;
      gMacRxDestAddrFiltered = counters->mRxDestAddrFiltered;
      gMacRxDuplicated = counters->mRxDuplicated;
    }

    gThreadRloc16 = otThreadGetRloc16(gInstance);
    gThreadPartitionId = otThreadGetPartitionId(gInstance);
    const otMleCounters* mleCounters = otThreadGetMleCounters(gInstance);
    if (mleCounters != nullptr) {
      gMleAttachAttempts = mleCounters->mAttachAttempts;
      gMleDetachedRoleCount = mleCounters->mDetachedRole;
      gMleChildRoleCount = mleCounters->mChildRole;
      gMleRouterRoleCount = mleCounters->mRouterRole;
      gMleLeaderRoleCount = mleCounters->mLeaderRole;
      gMleParentChanges = mleCounters->mParentChanges;
    }

    otRouterInfo parentInfo = {};
    gThreadParentInfoError = otThreadGetParentInfo(gInstance, &parentInfo);
    gThreadParentRloc16 =
        (gThreadParentInfoError == OT_ERROR_NONE) ? parentInfo.mRloc16 : 0xffff;

    const otDeviceRole role = otThreadGetDeviceRole(gInstance);
    if (role != gLastRole) {
      gLastRole = role;
      logLine("ot_role role-change");
      printRoleState();
    }
  }
#else
  OpenThreadPlatformSkeleton::process();
#endif

  if ((millis() - gLastReportMs) >= 1000UL) {
    gLastReportMs = millis();
    printRoleState();
  }

  delay(1);
}
