#include "nrf54_thread_experimental.h"

#include <Arduino.h>
#include <openthread/dataset_ftd.h>
#include <openthread/message.h>
#include <openthread/platform/settings.h>

#include <string.h>

namespace xiao_nrf54l15 {
namespace {

constexpr otPanId kDemoPanId = 0x5D6AU;
constexpr uint32_t kDemoChannelMask = OT_CHANNEL_15_MASK;
constexpr uint8_t kDemoChannel = 15U;
constexpr char kDemoNetworkName[] = "Nrf54Stage";
constexpr uint8_t kDemoNetworkKey[OT_NETWORK_KEY_SIZE] = {
    0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
};
constexpr uint8_t kDemoExtPanId[OT_EXT_PAN_ID_SIZE] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
};
constexpr uint8_t kDemoMeshLocalPrefix[OT_MESH_LOCAL_PREFIX_SIZE] = {
    0xFD, 0x54, 0x15, 0xC0, 0xDE, 0x00, 0x00, 0x00,
};
constexpr uint8_t kDemoPskc[OT_PSKC_MAX_SIZE] = {
    0xA5, 0x4C, 0x8D, 0x11, 0x72, 0x3F, 0x90, 0xBE,
    0x4A, 0x62, 0x18, 0xD4, 0xCE, 0x07, 0x39, 0x5B,
};

}  // namespace

bool Nrf54ThreadExperimental::begin(bool wipeSettings) {
  if (beginCalled_) {
    return false;
  }

  OpenThreadPlatformSkeleton::begin();
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  otPlatSettingsInit(nullptr, nullptr, 0);
  if (wipeSettings) {
    otPlatSettingsWipe(nullptr);
    settingsWiped_ = true;
  }
#endif
  wipeSettings_ = wipeSettings;
  beginMs_ = millis();
  beginCalled_ = true;
  lastError_ = OT_ERROR_NONE;
  lastUdpError_ = OT_ERROR_NONE;
  return true;
}

void Nrf54ThreadExperimental::process() {
  OpenThreadPlatformSkeleton::process(instance_);

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
  return;
#else
  if (!beginCalled_) {
    return;
  }

  const uint32_t elapsedMs = millis() - beginMs_;

  if (instance_ == nullptr && elapsedMs >= kStageInitDelayMs) {
    instance_ = otInstanceInitSingle();
    if (instance_ == nullptr || !otInstanceIsInitialized(instance_)) {
      lastError_ = OT_ERROR_FAILED;
      return;
    }
  }

  if (instance_ != nullptr && datasetConfigured_ && !datasetApplied_ &&
      elapsedMs >= kStageDatasetApplyDelayMs) {
    lastError_ = otDatasetSetActive(instance_, &dataset_);
    if (lastError_ == OT_ERROR_NONE) {
      datasetApplied_ = true;
    }
  }

  if (instance_ != nullptr && datasetApplied_ && !linkConfigured_ &&
      elapsedMs >= kStageIp6EnableDelayMs) {
    const otLinkModeConfig mode = {true, true, true};
    lastError_ = otThreadSetLinkMode(instance_, mode);
    if (lastError_ == OT_ERROR_NONE) {
      linkConfigured_ = true;
      lastError_ = otIp6SetEnabled(instance_, true);
      ip6Enabled_ = (lastError_ == OT_ERROR_NONE);
    }
  }

  if (instance_ != nullptr && ip6Enabled_ && !threadEnabled_ &&
      elapsedMs >= kStageThreadEnableDelayMs) {
    lastError_ = otThreadSetEnabled(instance_, true);
    threadEnabled_ = (lastError_ == OT_ERROR_NONE);
  }

  if (instance_ != nullptr && ip6Enabled_ && udpRequested_ && !udpOpened_) {
    memset(&udpSocket_, 0, sizeof(udpSocket_));
    lastUdpError_ =
        otUdpOpen(instance_, &udpSocket_, handleUdpReceiveStatic, this);
    if (lastUdpError_ == OT_ERROR_NONE) {
      otSockAddr sockAddr = {};
      sockAddr.mPort = udpPort_;
      lastUdpError_ =
          otUdpBind(instance_, &udpSocket_, &sockAddr, OT_NETIF_THREAD_INTERNAL);
      udpOpened_ = (lastUdpError_ == OT_ERROR_NONE);
    }
  }
#endif
}

bool Nrf54ThreadExperimental::setActiveDataset(
    const otOperationalDataset& dataset) {
  dataset_ = dataset;
  datasetConfigured_ = true;
  return true;
}

bool Nrf54ThreadExperimental::getActiveDataset(
    otOperationalDataset* outDataset) const {
  if (outDataset == nullptr || instance_ == nullptr) {
    return false;
  }
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  return otDatasetGetActive(instance_, outDataset) == OT_ERROR_NONE;
#else
  memset(outDataset, 0, sizeof(*outDataset));
  return false;
#endif
}

bool Nrf54ThreadExperimental::getConfiguredOrActiveDataset(
    otOperationalDataset* outDataset) const {
  if (outDataset == nullptr) {
    return false;
  }

  if (getActiveDataset(outDataset)) {
    return true;
  }

  if (!datasetConfigured_) {
    memset(outDataset, 0, sizeof(*outDataset));
    return false;
  }

  *outDataset = dataset_;
  return true;
}

bool Nrf54ThreadExperimental::requestRouterRole() {
#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
  lastError_ = OT_ERROR_INVALID_STATE;
  return false;
#else
  if (instance_ == nullptr || !threadEnabled_) {
    lastError_ = OT_ERROR_INVALID_STATE;
    return false;
  }

  const Role currentRole = role();
  if (currentRole == Role::kRouter || currentRole == Role::kLeader) {
    lastError_ = OT_ERROR_NONE;
    return true;
  }

  lastError_ = otThreadBecomeRouter(instance_);
  return lastError_ == OT_ERROR_NONE;
#endif
}

bool Nrf54ThreadExperimental::openUdp(uint16_t port,
                                      UdpReceiveCallback callback,
                                      void* callbackContext) {
  udpPort_ = port;
  udpCallback_ = callback;
  udpCallbackContext_ = callbackContext;
  udpRequested_ = true;
  return true;
}

bool Nrf54ThreadExperimental::sendUdp(const otIp6Address& peerAddr,
                                      uint16_t peerPort,
                                      const void* payload,
                                      uint16_t payloadLength) {
#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
  (void)peerAddr;
  (void)peerPort;
  (void)payload;
  (void)payloadLength;
  lastUdpError_ = OT_ERROR_INVALID_STATE;
  return false;
#else
  if (instance_ == nullptr || !udpOpened_ || payload == nullptr ||
      payloadLength == 0U) {
    lastUdpError_ = OT_ERROR_INVALID_STATE;
    return false;
  }

  otMessage* message = otUdpNewMessage(instance_, nullptr);
  if (message == nullptr) {
    lastUdpError_ = OT_ERROR_NO_BUFS;
    return false;
  }

  lastUdpError_ =
      otMessageAppend(message, static_cast<const uint8_t*>(payload), payloadLength);
  if (lastUdpError_ != OT_ERROR_NONE) {
    otMessageFree(message);
    return false;
  }

  otMessageInfo messageInfo = {};
  messageInfo.mPeerAddr = peerAddr;
  messageInfo.mPeerPort = peerPort;
  messageInfo.mSockPort = udpPort_;
  messageInfo.mHopLimit = 64U;

  lastUdpError_ = otUdpSend(instance_, &udpSocket_, message, &messageInfo);
  if (lastUdpError_ != OT_ERROR_NONE) {
    otMessageFree(message);
    return false;
  }
  return true;
#endif
}

bool Nrf54ThreadExperimental::getLeaderRloc(otIp6Address* outLeaderAddr) const {
  if (outLeaderAddr == nullptr || instance_ == nullptr) {
    return false;
  }
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  return otThreadGetLeaderRloc(instance_, outLeaderAddr) == OT_ERROR_NONE;
#else
  memset(outLeaderAddr, 0, sizeof(*outLeaderAddr));
  return false;
#endif
}

bool Nrf54ThreadExperimental::started() const { return beginCalled_; }

bool Nrf54ThreadExperimental::attached() const {
  const Role currentRole = role();
  return currentRole == Role::kChild || currentRole == Role::kRouter ||
         currentRole == Role::kLeader;
}

Nrf54ThreadExperimental::Role Nrf54ThreadExperimental::role() const {
  if (instance_ == nullptr) {
    return Role::kDisabled;
  }
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  return convertRole(otThreadGetDeviceRole(instance_));
#else
  return Role::kDisabled;
#endif
}

const char* Nrf54ThreadExperimental::roleName() const {
  return roleName(role());
}

uint16_t Nrf54ThreadExperimental::rloc16() const {
  if (instance_ == nullptr) {
    return OT_RADIO_INVALID_SHORT_ADDR;
  }
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  return otThreadGetRloc16(instance_);
#else
  return OT_RADIO_INVALID_SHORT_ADDR;
#endif
}

otError Nrf54ThreadExperimental::lastError() const { return lastError_; }

otError Nrf54ThreadExperimental::lastUdpError() const { return lastUdpError_; }

otInstance* Nrf54ThreadExperimental::rawInstance() const { return instance_; }

const char* Nrf54ThreadExperimental::roleName(Role role) {
  switch (role) {
    case Role::kDisabled:
      return "disabled";
    case Role::kDetached:
      return "detached";
    case Role::kChild:
      return "child";
    case Role::kRouter:
      return "router";
    case Role::kLeader:
      return "leader";
    default:
      return "unknown";
  }
}

void Nrf54ThreadExperimental::buildDemoDataset(
    otOperationalDataset* outDataset) {
  if (outDataset == nullptr) {
    return;
  }

  memset(outDataset, 0, sizeof(*outDataset));
  outDataset->mActiveTimestamp.mSeconds = 1ULL;
  outDataset->mActiveTimestamp.mAuthoritative = true;
  memcpy(outDataset->mNetworkKey.m8, kDemoNetworkKey, sizeof(kDemoNetworkKey));
  strncpy(outDataset->mNetworkName.m8,
          kDemoNetworkName,
          sizeof(outDataset->mNetworkName.m8) - 1U);
  memcpy(outDataset->mExtendedPanId.m8, kDemoExtPanId, sizeof(kDemoExtPanId));
  memcpy(outDataset->mMeshLocalPrefix.m8,
         kDemoMeshLocalPrefix,
         sizeof(kDemoMeshLocalPrefix));
  memcpy(outDataset->mPskc.m8, kDemoPskc, sizeof(kDemoPskc));
  outDataset->mPanId = kDemoPanId;
  outDataset->mChannel = kDemoChannel;
  outDataset->mWakeupChannel = kDemoChannel;
  outDataset->mChannelMask = kDemoChannelMask;
  outDataset->mSecurityPolicy.mRotationTime = 672U;
  outDataset->mSecurityPolicy.mObtainNetworkKeyEnabled = true;
  outDataset->mSecurityPolicy.mNativeCommissioningEnabled = true;
  outDataset->mSecurityPolicy.mRoutersEnabled = true;
  outDataset->mSecurityPolicy.mExternalCommissioningEnabled = true;
  outDataset->mSecurityPolicy.mNetworkKeyProvisioningEnabled = true;
  outDataset->mSecurityPolicy.mVersionThresholdForRouting = 3U;

  outDataset->mComponents.mIsActiveTimestampPresent = true;
  outDataset->mComponents.mIsNetworkKeyPresent = true;
  outDataset->mComponents.mIsNetworkNamePresent = true;
  outDataset->mComponents.mIsExtendedPanIdPresent = true;
  outDataset->mComponents.mIsMeshLocalPrefixPresent = true;
  outDataset->mComponents.mIsPanIdPresent = true;
  outDataset->mComponents.mIsChannelPresent = true;
  outDataset->mComponents.mIsPskcPresent = true;
  outDataset->mComponents.mIsSecurityPolicyPresent = true;
  outDataset->mComponents.mIsChannelMaskPresent = true;
  outDataset->mComponents.mIsWakeupChannelPresent = true;
}

otError Nrf54ThreadExperimental::generatePskc(
    const char* passPhrase, const char* networkName,
    const uint8_t extPanId[OT_EXT_PAN_ID_SIZE], otPskc* outPskc) {
  if (passPhrase == nullptr || networkName == nullptr || extPanId == nullptr ||
      outPskc == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
  memset(outPskc, 0, sizeof(*outPskc));
  return OT_ERROR_INVALID_STATE;
#else
  otNetworkName parsedNetworkName = {};
  otExtendedPanId parsedExtPanId = {};
  otError error = otNetworkNameFromString(&parsedNetworkName, networkName);
  if (error != OT_ERROR_NONE) {
    memset(outPskc, 0, sizeof(*outPskc));
    return error;
  }

  memcpy(parsedExtPanId.m8, extPanId, sizeof(parsedExtPanId.m8));
  error = otDatasetGeneratePskc(passPhrase, &parsedNetworkName, &parsedExtPanId,
                                outPskc);
  if (error != OT_ERROR_NONE) {
    memset(outPskc, 0, sizeof(*outPskc));
  }
  return error;
#endif
}

otError Nrf54ThreadExperimental::buildDatasetFromPassphrase(
    const char* passPhrase, const char* networkName,
    const uint8_t extPanId[OT_EXT_PAN_ID_SIZE], otOperationalDataset* outDataset) {
  if (passPhrase == nullptr || networkName == nullptr || extPanId == nullptr ||
      outDataset == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }

  buildDemoDataset(outDataset);

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
  return OT_ERROR_INVALID_STATE;
#else
  otError error = otNetworkNameFromString(&outDataset->mNetworkName, networkName);
  if (error != OT_ERROR_NONE) {
    return error;
  }

  memcpy(outDataset->mExtendedPanId.m8, extPanId, OT_EXT_PAN_ID_SIZE);
  error = generatePskc(passPhrase, networkName, extPanId, &outDataset->mPskc);
  if (error != OT_ERROR_NONE) {
    return error;
  }

  outDataset->mComponents.mIsNetworkNamePresent = true;
  outDataset->mComponents.mIsExtendedPanIdPresent = true;
  outDataset->mComponents.mIsPskcPresent = true;
  return OT_ERROR_NONE;
#endif
}

void Nrf54ThreadExperimental::handleUdpReceiveStatic(
    void* context, otMessage* message, const otMessageInfo* messageInfo) {
  if (context == nullptr) {
    return;
  }
  static_cast<Nrf54ThreadExperimental*>(context)->handleUdpReceive(message,
                                                                   messageInfo);
}

void Nrf54ThreadExperimental::handleUdpReceive(
    otMessage* message, const otMessageInfo* messageInfo) {
  if (udpCallback_ == nullptr || message == nullptr || messageInfo == nullptr) {
    return;
  }

  const uint16_t length = otMessageGetLength(message);
  uint8_t buffer[256] = {0};
  uint16_t copyLength = length;
  if (copyLength > sizeof(buffer)) {
    copyLength = sizeof(buffer);
  }
  if (copyLength != 0U) {
    otMessageRead(message, 0, buffer, copyLength);
  }

  udpCallback_(udpCallbackContext_, buffer, copyLength, *messageInfo);
}

Nrf54ThreadExperimental::Role Nrf54ThreadExperimental::convertRole(
    otDeviceRole role) {
  switch (role) {
    case OT_DEVICE_ROLE_DISABLED:
      return Role::kDisabled;
    case OT_DEVICE_ROLE_DETACHED:
      return Role::kDetached;
    case OT_DEVICE_ROLE_CHILD:
      return Role::kChild;
    case OT_DEVICE_ROLE_ROUTER:
      return Role::kRouter;
    case OT_DEVICE_ROLE_LEADER:
      return Role::kLeader;
    default:
      return Role::kUnknown;
  }
}

}  // namespace xiao_nrf54l15
