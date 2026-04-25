#include "matter_onnetwork_onoff_light.h"

#include <string.h>

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
#include <lib/support/BytesToHex.h>
#endif

namespace xiao_nrf54l15 {
namespace {

bool commissioningFlowValid(MatterCommissioningFlow flow) {
  switch (flow) {
    case MatterCommissioningFlow::kStandard:
    case MatterCommissioningFlow::kUserActionRequired:
    case MatterCommissioningFlow::kCustom:
      return true;
    default:
      return false;
  }
}

}  // namespace

uint16_t Nrf54MatterOnNetworkOnOffLightNode::remainingWindowSeconds(
    uint32_t endMs) {
  if (endMs == 0U) {
    return 0U;
  }

  const int32_t deltaMs = static_cast<int32_t>(endMs - millis());
  if (deltaMs <= 0) {
    return 0U;
  }

  const uint32_t unsignedDeltaMs = static_cast<uint32_t>(deltaMs);
  return static_cast<uint16_t>((unsignedDeltaMs + 999U) / 1000U);
}

bool Nrf54MatterOnNetworkOnOffLightNode::bytesToUpperHex(
    const uint8_t* data, size_t length, char* outBuffer, size_t outBufferSize,
    size_t* outHexLength) {
  static constexpr char kHexDigits[] = "0123456789ABCDEF";

  if (outHexLength != nullptr) {
    *outHexLength = 0U;
  }
  if (data == nullptr || outBuffer == nullptr ||
      outBufferSize < ((length * 2U) + 1U)) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    outBuffer[i * 2U] = kHexDigits[(data[i] >> 4U) & 0x0FU];
    outBuffer[(i * 2U) + 1U] = kHexDigits[data[i] & 0x0FU];
  }
  outBuffer[length * 2U] = '\0';
  if (outHexLength != nullptr) {
    *outHexLength = length * 2U;
  }
  return true;
}

int Nrf54MatterOnNetworkOnOffLightNode::hexNibble(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'A' && value <= 'F') {
    return 10 + (value - 'A');
  }
  if (value >= 'a' && value <= 'f') {
    return 10 + (value - 'a');
  }
  return -1;
}

bool Nrf54MatterOnNetworkOnOffLightNode::hexToBytes(
    const char* text, uint8_t* outData, size_t outCapacity,
    size_t* outLength) {
  if (outLength != nullptr) {
    *outLength = 0U;
  }
  if (text == nullptr || outData == nullptr) {
    return false;
  }

  int highNibble = -1;
  size_t length = 0U;
  for (const char* current = text; *current != '\0'; ++current) {
    const char c = *current;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ':' ||
        c == '-') {
      continue;
    }

    const int nibble = hexNibble(c);
    if (nibble < 0) {
      return false;
    }

    if (highNibble < 0) {
      highNibble = nibble;
      continue;
    }

    if (length >= outCapacity) {
      return false;
    }

    outData[length++] =
        static_cast<uint8_t>((highNibble << 4U) | static_cast<uint8_t>(nibble));
    highNibble = -1;
  }

  if (highNibble >= 0 || length == 0U) {
    return false;
  }

  if (outLength != nullptr) {
    *outLength = length;
  }
  return true;
}

bool Nrf54MatterOnNetworkOnOffLightNode::begin(
    const MatterOnNetworkOnOffLightConfig* config) {
  if (storageOpen_) {
    return false;
  }

  const MatterOnNetworkOnOffLightConfig defaultConfig = {};
  const MatterOnNetworkOnOffLightConfig& effectiveConfig =
      config != nullptr ? *config : defaultConfig;
  const char* storageNamespace =
      (effectiveConfig.storageNamespace != nullptr &&
       effectiveConfig.storageNamespace[0] != '\0')
          ? effectiveConfig.storageNamespace
          : "matter_node";
  if (!prefs_.begin(storageNamespace, false)) {
    return false;
  }

  storageOpen_ = true;
  datasetSource_ = MatterOnNetworkDatasetSource::kNone;
  autoRequestRouterRole_ = effectiveConfig.autoRequestRouterRole;
  routerRoleRequested_ = false;
  commissioningWindowPending_ = false;
  commissioningWindowExpired_ = false;
  commissioningWindowDurationSeconds_ = 0U;
  commissioningWindowEndMs_ = 0U;
  endpoint_.attach(&light_);
  buildDefaultIdentity(&identity_);

  if (identityValid(effectiveConfig.identity)) {
    identity_ = effectiveConfig.identity;
  }
  if (effectiveConfig.restorePersistentState) {
    MatterOnNetworkIdentity restoredIdentity = {};
    if (loadPersistentIdentity(&restoredIdentity)) {
      identity_ = restoredIdentity;
    }
  }
  if (!savePersistentIdentity()) {
    end();
    return false;
  }

  const char* lightStorageNamespace =
      (effectiveConfig.lightStorageNamespace != nullptr &&
       effectiveConfig.lightStorageNamespace[0] != '\0')
          ? effectiveConfig.lightStorageNamespace
          : "matter_onoff";
  lightReady_ =
      light_.begin(lightStorageNamespace, effectiveConfig.restorePersistentState);
  if (!lightReady_) {
    end();
    return false;
  }

  otOperationalDatasetTlvs persistentDatasetTlvs = {};
  const bool havePersistentThreadDataset =
      effectiveConfig.restorePersistentState &&
      loadPersistentThreadDataset(&persistentDatasetTlvs);

  if (effectiveConfig.explicitThreadDataset != nullptr) {
    if (!useThreadDataset(*effectiveConfig.explicitThreadDataset)) {
      end();
      return false;
    }
  } else if (havePersistentThreadDataset) {
    if (!useThreadDatasetTlvs(persistentDatasetTlvs, false)) {
      end();
      return false;
    }
    datasetSource_ = MatterOnNetworkDatasetSource::kPersistent;
  } else if (effectiveConfig.threadPassPhrase != nullptr ||
             effectiveConfig.threadNetworkName != nullptr ||
             effectiveConfig.threadExtPanId != nullptr) {
    if (effectiveConfig.threadPassPhrase == nullptr ||
        effectiveConfig.threadNetworkName == nullptr ||
        effectiveConfig.threadExtPanId == nullptr ||
        !useThreadDatasetFromPassphrase(effectiveConfig.threadPassPhrase,
                                        effectiveConfig.threadNetworkName,
                                        effectiveConfig.threadExtPanId)) {
      end();
      return false;
    }
  } else if (effectiveConfig.useDemoDataset && !useDemoThreadDataset()) {
    end();
    return false;
  }

  if (effectiveConfig.autoStartThread &&
      !thread_.begin(effectiveConfig.wipeThreadSettings)) {
    end();
    return false;
  }

  return true;
}

void Nrf54MatterOnNetworkOnOffLightNode::end() {
  if (lightReady_) {
    light_.end();
    lightReady_ = false;
  }
  if (storageOpen_) {
    prefs_.end();
    storageOpen_ = false;
  }
  autoRequestRouterRole_ = false;
  routerRoleRequested_ = false;
  commissioningWindowPending_ = false;
  commissioningWindowExpired_ = false;
  commissioningWindowDurationSeconds_ = 0U;
  commissioningWindowEndMs_ = 0U;
  datasetSource_ = MatterOnNetworkDatasetSource::kNone;
  endpoint_.detach();
}

void Nrf54MatterOnNetworkOnOffLightNode::process() {
  thread_.process();
  light_.process();

  if (!autoRequestRouterRole_ || routerRoleRequested_ || !thread_.attached()) {
    return;
  }

  const Nrf54ThreadExperimental::Role currentRole = thread_.role();
  if (currentRole == Nrf54ThreadExperimental::Role::kRouter ||
      currentRole == Nrf54ThreadExperimental::Role::kLeader) {
    routerRoleRequested_ = true;
    return;
  }

  if (currentRole == Nrf54ThreadExperimental::Role::kChild &&
      thread_.requestRouterRole()) {
    routerRoleRequested_ = true;
  }

  if (commissioningWindowEndMs_ != 0U &&
      remainingWindowSeconds(commissioningWindowEndMs_) == 0U) {
    commissioningWindowEndMs_ = 0U;
    commissioningWindowPending_ = false;
    commissioningWindowExpired_ = true;
  } else if (commissioningWindowPending_ && commissioningWindowDurationSeconds_ != 0U &&
             readyForOnNetworkCommissioning()) {
    commissioningWindowPending_ = false;
    commissioningWindowExpired_ = false;
    commissioningWindowEndMs_ =
        millis() + (static_cast<uint32_t>(commissioningWindowDurationSeconds_) *
                    1000UL);
  }
}

bool Nrf54MatterOnNetworkOnOffLightNode::snapshot(
    MatterOnNetworkOnOffLightStatus* outStatus) const {
  if (outStatus == nullptr) {
    return false;
  }

  memset(outStatus, 0, sizeof(*outStatus));
  outStatus->storageOpen = storageOpen_;
  outStatus->lightReady = lightReady_;
  outStatus->threadStarted = thread_.started();
  outStatus->threadAttached = thread_.attached();
  outStatus->threadDatasetConfigured =
      datasetSource_ != MatterOnNetworkDatasetSource::kNone;
  outStatus->threadDatasetExportable = threadDatasetExportable();
  outStatus->buildSeamsAligned = foundation_.buildSeamsAligned();
  outStatus->datasetSource = datasetSource_;
  outStatus->threadRole = thread_.role();
  outStatus->rloc16 = thread_.rloc16();
  outStatus->identity = identity_;
  outStatus->manualCodeReady =
      manualPairingCode(nullptr, 0U) ||
      matterManualPairingPayloadValid(MatterManualPairingPayload{
          identity_.setupPinCode,
          identity_.discriminator,
          identity_.vendorId,
          identity_.productId,
          identity_.commissioningFlow});
  outStatus->qrCodeReady =
      matterQrCodePayloadValid(MatterQrCodePayload{
          0U,
          identity_.setupPinCode,
          identity_.discriminator,
          identity_.vendorId,
          identity_.productId,
          static_cast<uint8_t>(kMatterRendezvousOnNetwork |
                               kMatterRendezvousThread),
          identity_.commissioningFlow});
  outStatus->readyForOnNetworkCommissioning =
      readyForOnNetworkCommissioning();
  outStatus->commissioningWindowPending = commissioningWindowPending_;
  outStatus->commissioningWindowState = commissioningWindowState();
  outStatus->commissioningWindowSecondsRemaining =
      commissioningWindowSecondsRemaining();
  (void)light_.snapshot(&outStatus->light);
  return true;
}

bool Nrf54MatterOnNetworkOnOffLightNode::setIdentity(
    const MatterOnNetworkIdentity& identity, bool persist) {
  if (!identityValid(identity)) {
    return false;
  }

  identity_ = identity;
  return !persist || savePersistentIdentity();
}

const MatterOnNetworkIdentity& Nrf54MatterOnNetworkOnOffLightNode::identity()
    const {
  return identity_;
}

bool Nrf54MatterOnNetworkOnOffLightNode::restoreDefaultIdentity(bool persist) {
  MatterOnNetworkIdentity defaultIdentity = {};
  buildDefaultIdentity(&defaultIdentity);
  return setIdentity(defaultIdentity, persist);
}

bool Nrf54MatterOnNetworkOnOffLightNode::savePersistentIdentity() {
  if (!storageOpen_) {
    return false;
  }

  MatterOnNetworkPersistentState state = {};
  state.magic = kPersistentStateMagic;
  state.version = kPersistentStateVersion;
  state.setupPinCode = identity_.setupPinCode;
  state.discriminator = identity_.discriminator;
  state.vendorId = identity_.vendorId;
  state.productId = identity_.productId;
  state.commissioningFlow = static_cast<uint8_t>(identity_.commissioningFlow);
  return prefs_.putBytes(kPersistentStateKey, &state, sizeof(state)) ==
         sizeof(state);
}

bool Nrf54MatterOnNetworkOnOffLightNode::clearPersistentIdentity() {
  return storageOpen_ && prefs_.remove(kPersistentStateKey);
}

bool Nrf54MatterOnNetworkOnOffLightNode::useDemoThreadDataset() {
  otOperationalDataset dataset = {};
  Nrf54ThreadExperimental::buildDemoDataset(&dataset);
  return useThreadDataset(dataset) &&
         (datasetSource_ = MatterOnNetworkDatasetSource::kDemo,
          true);
}

bool Nrf54MatterOnNetworkOnOffLightNode::useThreadDatasetFromPassphrase(
    const char* passPhrase, const char* networkName,
    const uint8_t extPanId[OT_EXT_PAN_ID_SIZE]) {
  otOperationalDataset dataset = {};
  if (Nrf54ThreadExperimental::buildDatasetFromPassphrase(
          passPhrase, networkName, extPanId, &dataset) != OT_ERROR_NONE ||
      !useThreadDataset(dataset)) {
    return false;
  }
  datasetSource_ = MatterOnNetworkDatasetSource::kPassphrase;
  return true;
}

bool Nrf54MatterOnNetworkOnOffLightNode::useThreadDataset(
    const otOperationalDataset& dataset, bool persist) {
  if (!thread_.setActiveDataset(dataset)) {
    return false;
  }
  datasetSource_ = MatterOnNetworkDatasetSource::kExplicit;
  return !persist || !storageOpen_ || savePersistentThreadDataset();
}

bool Nrf54MatterOnNetworkOnOffLightNode::useThreadDatasetTlvs(
    const otOperationalDatasetTlvs& datasetTlvs, bool persist) {
  if (!otDatasetIsValid(&datasetTlvs, true) ||
      !thread_.setActiveDatasetTlvs(datasetTlvs)) {
    return false;
  }

  datasetSource_ = MatterOnNetworkDatasetSource::kExplicit;
  return !persist || !storageOpen_ || savePersistentThreadDataset();
}

bool Nrf54MatterOnNetworkOnOffLightNode::useThreadDatasetHex(
    const char* datasetHex, bool persist) {
  otOperationalDatasetTlvs datasetTlvs = {};
  size_t tlvLength = 0U;
  if (!hexToBytes(datasetHex, datasetTlvs.mTlvs, sizeof(datasetTlvs.mTlvs),
                  &tlvLength)) {
    return false;
  }
  datasetTlvs.mLength = static_cast<uint8_t>(tlvLength);
  return useThreadDatasetTlvs(datasetTlvs, persist);
}

bool Nrf54MatterOnNetworkOnOffLightNode::savePersistentThreadDataset() {
  if (!storageOpen_) {
    return false;
  }

  otOperationalDatasetTlvs datasetTlvs = {};
  if (!exportOpenThreadDatasetTlvs(&datasetTlvs)) {
    return false;
  }

  MatterOnNetworkPersistentThreadDataset state = {};
  state.magic = kPersistentThreadDatasetMagic;
  state.version = kPersistentThreadDatasetVersion;
  state.length = datasetTlvs.mLength;
  memcpy(state.tlvs, datasetTlvs.mTlvs, datasetTlvs.mLength);
  return prefs_.putBytes(kPersistentThreadDatasetKey, &state, sizeof(state)) ==
         sizeof(state);
}

bool Nrf54MatterOnNetworkOnOffLightNode::clearPersistentThreadDataset() {
  return storageOpen_ && prefs_.remove(kPersistentThreadDatasetKey);
}

bool Nrf54MatterOnNetworkOnOffLightNode::factoryReset() {
  if (!storageOpen_ || !lightReady_) {
    return false;
  }

  closeCommissioningWindow();
  light_.stopIdentify();
  if (!light_.setOn(false, false) ||
      !light_.setStartUpBehavior(
          MatterOnOffLightStartUpBehavior::kRestorePrevious, false) ||
      !light_.savePersistentState() || !restoreDefaultIdentity(true)) {
    return false;
  }

  return !prefs_.isKey(kPersistentThreadDatasetKey) ||
         clearPersistentThreadDataset();
}

bool Nrf54MatterOnNetworkOnOffLightNode::openCommissioningWindow(
    uint16_t seconds) {
  if (!storageOpen_ || seconds == 0U) {
    return false;
  }

  commissioningWindowDurationSeconds_ = seconds;
  commissioningWindowExpired_ = false;
  commissioningWindowEndMs_ = 0U;
  commissioningWindowPending_ = !readyForOnNetworkCommissioning();
  if (!commissioningWindowPending_) {
    commissioningWindowEndMs_ =
        millis() + (static_cast<uint32_t>(seconds) * 1000UL);
  }
  return true;
}

void Nrf54MatterOnNetworkOnOffLightNode::closeCommissioningWindow() {
  commissioningWindowPending_ = false;
  commissioningWindowExpired_ = false;
  commissioningWindowDurationSeconds_ = 0U;
  commissioningWindowEndMs_ = 0U;
}

MatterCommissioningWindowState
Nrf54MatterOnNetworkOnOffLightNode::commissioningWindowState() const {
  if (commissioningWindowSecondsRemaining() != 0U) {
    return MatterCommissioningWindowState::kOpen;
  }
  if (commissioningWindowPending_) {
    return MatterCommissioningWindowState::kPendingReadiness;
  }
  if (commissioningWindowExpired_) {
    return MatterCommissioningWindowState::kExpired;
  }
  return MatterCommissioningWindowState::kClosed;
}

bool Nrf54MatterOnNetworkOnOffLightNode::commissioningWindowOpen() const {
  return commissioningWindowState() == MatterCommissioningWindowState::kOpen;
}

uint16_t
Nrf54MatterOnNetworkOnOffLightNode::commissioningWindowSecondsRemaining()
    const {
  return remainingWindowSeconds(commissioningWindowEndMs_);
}

bool Nrf54MatterOnNetworkOnOffLightNode::buildCommissioningBundle(
    MatterOnNetworkCommissioningBundle* outBundle) const {
  if (outBundle == nullptr) {
    return false;
  }

  memset(outBundle, 0, sizeof(*outBundle));
  outBundle->datasetSource = datasetSource_;
  outBundle->commissioningWindowState = commissioningWindowState();
  outBundle->commissioningWindowSecondsRemaining =
      commissioningWindowSecondsRemaining();
  outBundle->manualCodeReady =
      manualPairingCode(outBundle->manualCode, sizeof(outBundle->manualCode));
  outBundle->qrCodeReady =
      qrCode(outBundle->qrCode, sizeof(outBundle->qrCode));
  outBundle->openThreadDatasetReady = exportOpenThreadDatasetHex(
      outBundle->openThreadDatasetHex, sizeof(outBundle->openThreadDatasetHex),
      &outBundle->openThreadDatasetHexLength);
#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
  outBundle->matterThreadDatasetReady = exportMatterThreadDatasetHex(
      outBundle->matterThreadDatasetHex,
      sizeof(outBundle->matterThreadDatasetHex),
      &outBundle->matterThreadDatasetHexLength, nullptr);
#endif
  outBundle->ready = readyForOnNetworkCommissioning();
  return true;
}

bool Nrf54MatterOnNetworkOnOffLightNode::exportOpenThreadDatasetTlvs(
    otOperationalDatasetTlvs* outTlvs) const {
  if (outTlvs == nullptr) {
    return false;
  }

  memset(outTlvs, 0, sizeof(*outTlvs));
  otOperationalDataset dataset = {};
  if (!thread_.getConfiguredOrActiveDataset(&dataset)) {
    return false;
  }

  otDatasetConvertToTlvs(&dataset, outTlvs);
  return otDatasetIsValid(outTlvs, true);
}

bool Nrf54MatterOnNetworkOnOffLightNode::exportOpenThreadDatasetHex(
    char* outBuffer, size_t outBufferSize, size_t* outHexLength) const {
  otOperationalDatasetTlvs datasetTlvs = {};
  if (!exportOpenThreadDatasetTlvs(&datasetTlvs)) {
    if (outHexLength != nullptr) {
      *outHexLength = 0U;
    }
    return false;
  }
  return bytesToUpperHex(datasetTlvs.mTlvs, datasetTlvs.mLength, outBuffer,
                         outBufferSize, outHexLength);
}

bool Nrf54MatterOnNetworkOnOffLightNode::manualPairingCode(
    char* outBuffer, size_t outBufferSize) const {
  MatterManualPairingPayload payload = {};
  buildManualPayload(&payload);
  if (outBuffer == nullptr || outBufferSize == 0U) {
    return matterManualPairingPayloadValid(payload);
  }
  return matterManualPairingCode(payload, outBuffer, outBufferSize);
}

bool Nrf54MatterOnNetworkOnOffLightNode::qrCode(char* outBuffer,
                                                size_t outBufferSize) const {
  MatterQrCodePayload payload = {};
  buildQrPayload(&payload);
  if (outBuffer == nullptr || outBufferSize == 0U) {
    return matterQrCodePayloadValid(payload);
  }
  return matterQrCode(payload, outBuffer, outBufferSize);
}

bool Nrf54MatterOnNetworkOnOffLightNode::readyForOnNetworkCommissioning()
    const {
  return storageOpen_ && lightReady_ && foundation_.mechanicalPathPossible() &&
         thread_.started() && thread_.attached() &&
         manualPairingCode(nullptr, 0U) && qrCode(nullptr, 0U) &&
         threadDatasetExportable();
}

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
bool Nrf54MatterOnNetworkOnOffLightNode::exportThreadDataset(
    chip::Thread::OperationalDataset* outDataset, CHIP_ERROR* outError) const {
  otOperationalDataset threadDataset = {};
  if (!thread_.getConfiguredOrActiveDataset(&threadDataset)) {
    if (outError != nullptr) {
      *outError = CHIP_ERROR_INCORRECT_STATE;
    }
    return false;
  }
  return foundation_.exportThreadDataset(threadDataset, outDataset, outError);
}

bool Nrf54MatterOnNetworkOnOffLightNode::exportMatterThreadDatasetHex(
    char* outBuffer, size_t outBufferSize, size_t* outHexLength,
    CHIP_ERROR* outError) const {
  chip::Thread::OperationalDataset dataset;
  CHIP_ERROR error = CHIP_NO_ERROR;
  if (!exportThreadDataset(&dataset, &error)) {
    if (outHexLength != nullptr) {
      *outHexLength = 0U;
    }
    if (outError != nullptr) {
      *outError = error;
    }
    return false;
  }

  const chip::ByteSpan span = dataset.AsByteSpan();
  if (outBuffer == nullptr || outBufferSize < ((span.size() * 2U) + 1U)) {
    if (outHexLength != nullptr) {
      *outHexLength = 0U;
    }
    if (outError != nullptr) {
      *outError = CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    return false;
  }

  error = chip::Encoding::BytesToUppercaseHexString(
      span.data(), span.size(), outBuffer, outBufferSize);
  if (outHexLength != nullptr) {
    *outHexLength = (error == CHIP_NO_ERROR) ? (span.size() * 2U) : 0U;
  }
  if (outError != nullptr) {
    *outError = error;
  }
  return error == CHIP_NO_ERROR;
}
#endif

Nrf54MatterOnOffLightDevice& Nrf54MatterOnNetworkOnOffLightNode::light() {
  return light_;
}

const Nrf54MatterOnOffLightDevice&
Nrf54MatterOnNetworkOnOffLightNode::light() const {
  return light_;
}

Nrf54MatterOnOffLightEndpoint& Nrf54MatterOnNetworkOnOffLightNode::endpoint() {
  return endpoint_;
}

const Nrf54MatterOnOffLightEndpoint&
Nrf54MatterOnNetworkOnOffLightNode::endpoint() const {
  return endpoint_;
}

Nrf54ThreadExperimental& Nrf54MatterOnNetworkOnOffLightNode::thread() {
  return thread_;
}

const Nrf54ThreadExperimental& Nrf54MatterOnNetworkOnOffLightNode::thread()
    const {
  return thread_;
}

void Nrf54MatterOnNetworkOnOffLightNode::buildDefaultIdentity(
    MatterOnNetworkIdentity* outIdentity) {
  if (outIdentity == nullptr) {
    return;
  }

  outIdentity->setupPinCode = 20202021UL;
  outIdentity->discriminator = 3840U;
  outIdentity->vendorId = 12U;
  outIdentity->productId = 1U;
  outIdentity->commissioningFlow = MatterCommissioningFlow::kStandard;
}

bool Nrf54MatterOnNetworkOnOffLightNode::identityValid(
    const MatterOnNetworkIdentity& identity) {
  return matterSetupPinValid(identity.setupPinCode) &&
         matterDiscriminatorValid(identity.discriminator) &&
         commissioningFlowValid(identity.commissioningFlow);
}

const char* Nrf54MatterOnNetworkOnOffLightNode::datasetSourceName(
    MatterOnNetworkDatasetSource source) {
  switch (source) {
    case MatterOnNetworkDatasetSource::kNone:
      return "none";
    case MatterOnNetworkDatasetSource::kDemo:
      return "demo";
    case MatterOnNetworkDatasetSource::kPassphrase:
      return "passphrase";
    case MatterOnNetworkDatasetSource::kExplicit:
      return "explicit";
    case MatterOnNetworkDatasetSource::kPersistent:
      return "persistent";
    default:
      return "unknown";
  }
}

const char* Nrf54MatterOnNetworkOnOffLightNode::commissioningWindowStateName(
    MatterCommissioningWindowState state) {
  switch (state) {
    case MatterCommissioningWindowState::kClosed:
      return "closed";
    case MatterCommissioningWindowState::kPendingReadiness:
      return "pending-readiness";
    case MatterCommissioningWindowState::kOpen:
      return "open";
    case MatterCommissioningWindowState::kExpired:
      return "expired";
    default:
      return "unknown";
  }
}

bool Nrf54MatterOnNetworkOnOffLightNode::loadPersistentIdentity(
    MatterOnNetworkIdentity* outIdentity) const {
  if (!storageOpen_ || outIdentity == nullptr ||
      prefs_.getBytesLength(kPersistentStateKey) !=
          sizeof(MatterOnNetworkPersistentState)) {
    return false;
  }

  MatterOnNetworkPersistentState state = {};
  if (prefs_.getBytes(kPersistentStateKey, &state, sizeof(state)) !=
      sizeof(state) ||
      state.magic != kPersistentStateMagic ||
      state.version != kPersistentStateVersion) {
    return false;
  }

  MatterOnNetworkIdentity candidate = {};
  candidate.setupPinCode = state.setupPinCode;
  candidate.discriminator = state.discriminator;
  candidate.vendorId = state.vendorId;
  candidate.productId = state.productId;
  candidate.commissioningFlow =
      static_cast<MatterCommissioningFlow>(state.commissioningFlow);
  if (!identityValid(candidate)) {
    return false;
  }

  *outIdentity = candidate;
  return true;
}

bool Nrf54MatterOnNetworkOnOffLightNode::loadPersistentThreadDataset(
    otOperationalDatasetTlvs* outTlvs) const {
  if (!storageOpen_ || outTlvs == nullptr ||
      prefs_.getBytesLength(kPersistentThreadDatasetKey) !=
          sizeof(MatterOnNetworkPersistentThreadDataset)) {
    return false;
  }

  MatterOnNetworkPersistentThreadDataset state = {};
  if (prefs_.getBytes(kPersistentThreadDatasetKey, &state, sizeof(state)) !=
          sizeof(state) ||
      state.magic != kPersistentThreadDatasetMagic ||
      state.version != kPersistentThreadDatasetVersion ||
      state.length == 0U || state.length > OT_OPERATIONAL_DATASET_MAX_LENGTH) {
    return false;
  }

  memset(outTlvs, 0, sizeof(*outTlvs));
  outTlvs->mLength = state.length;
  memcpy(outTlvs->mTlvs, state.tlvs, state.length);
  return otDatasetIsValid(outTlvs, true);
}

void Nrf54MatterOnNetworkOnOffLightNode::buildManualPayload(
    MatterManualPairingPayload* outPayload) const {
  if (outPayload == nullptr) {
    return;
  }

  memset(outPayload, 0, sizeof(*outPayload));
  outPayload->setupPinCode = identity_.setupPinCode;
  outPayload->discriminator = identity_.discriminator;
  outPayload->vendorId = identity_.vendorId;
  outPayload->productId = identity_.productId;
  outPayload->commissioningFlow = identity_.commissioningFlow;
}

void Nrf54MatterOnNetworkOnOffLightNode::buildQrPayload(
    MatterQrCodePayload* outPayload) const {
  if (outPayload == nullptr) {
    return;
  }

  memset(outPayload, 0, sizeof(*outPayload));
  outPayload->setupPinCode = identity_.setupPinCode;
  outPayload->discriminator = identity_.discriminator;
  outPayload->vendorId = identity_.vendorId;
  outPayload->productId = identity_.productId;
  outPayload->rendezvousFlags =
      kMatterRendezvousOnNetwork | kMatterRendezvousThread;
  outPayload->commissioningFlow = identity_.commissioningFlow;
}

bool Nrf54MatterOnNetworkOnOffLightNode::threadDatasetExportable() const {
  otOperationalDataset dataset = {};
  if (!thread_.getConfiguredOrActiveDataset(&dataset)) {
    return false;
  }

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
  chip::Thread::OperationalDataset matterDataset;
  CHIP_ERROR error = CHIP_NO_ERROR;
  return foundation_.exportThreadDataset(dataset, &matterDataset, &error);
#else
  otOperationalDatasetTlvs datasetTlvs = {};
  otDatasetConvertToTlvs(&dataset, &datasetTlvs);
  return otDatasetIsValid(&datasetTlvs, true);
#endif
}

}  // namespace xiao_nrf54l15
