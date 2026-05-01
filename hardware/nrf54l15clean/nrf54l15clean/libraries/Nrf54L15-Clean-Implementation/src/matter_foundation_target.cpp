#include "matter_foundation_target.h"

#include <string.h>

namespace xiao_nrf54l15 {
namespace {

constexpr MatterFoundationClusterDescriptor kRootEndpointClusters[] = {
    {Nrf54MatterOnOffLightFoundation::kDescriptorClusterId, "Descriptor"},
    {Nrf54MatterOnOffLightFoundation::kAccessControlClusterId,
     "AccessControl"},
    {Nrf54MatterOnOffLightFoundation::kBasicInformationClusterId,
     "BasicInformation"},
    {Nrf54MatterOnOffLightFoundation::kGeneralCommissioningClusterId,
     "GeneralCommissioning"},
    {Nrf54MatterOnOffLightFoundation::kNetworkCommissioningClusterId,
     "NetworkCommissioning"},
    {Nrf54MatterOnOffLightFoundation::kAdministratorCommissioningClusterId,
     "AdministratorCommissioning"},
    {Nrf54MatterOnOffLightFoundation::kOperationalCredentialsClusterId,
     "OperationalCredentials"},
};

constexpr MatterFoundationClusterDescriptor kPrimaryEndpointClusters[] = {
    {Nrf54MatterOnOffLightFoundation::kDescriptorClusterId, "Descriptor"},
    {Nrf54MatterOnOffLightFoundation::kIdentifyClusterId, "Identify"},
    {Nrf54MatterOnOffLightFoundation::kGroupsClusterId, "Groups"},
    {Nrf54MatterOnOffLightFoundation::kScenesClusterId, "Scenes"},
    {Nrf54MatterOnOffLightFoundation::kOnOffClusterId, "OnOff"},
};

constexpr const char* kOnboardingSteps[] = {
    "Enable Tools > Thread Core > Experimental Stage Core (Leader/Child/Router + UDP).",
    "Enable Tools > Matter Foundation > Experimental Compile Target (On-Network On/Off Light).",
    "Build or fetch a staged Thread dataset, then export it into CHIP TLV form for on-network commissioning.",
    "Use the generated manual code or QR code plus the exported dataset as the first in-tree commissioning seed.",
};

constexpr bool resolved(bool required, bool available) {
  return !required || available;
}

}  // namespace

Nrf54MatterOnOffLightFoundation::Nrf54MatterOnOffLightFoundation() {
  endpoints_[0] = {kRootEndpointId,
                   kRootNodeDeviceTypeId,
                   "root-node",
                   kRootEndpointClusters,
                   sizeof(kRootEndpointClusters) /
                       sizeof(kRootEndpointClusters[0])};
  endpoints_[1] = {kPrimaryEndpointId,
                   kOnOffLightDeviceTypeId,
                   "on-off-light",
                   kPrimaryEndpointClusters,
                   sizeof(kPrimaryEndpointClusters) /
                       sizeof(kPrimaryEndpointClusters[0])};

  threadDependencies_[0] = {"thread core stage seam",
                            MatterThreadDependencyContract::kRequiresThreadCoreStage,
                            MatterThreadDependencyContract::kProvidesThreadCoreStage,
                            resolved(
                                MatterThreadDependencyContract::kRequiresThreadCoreStage,
                                MatterThreadDependencyContract::kProvidesThreadCoreStage)};
  threadDependencies_[1] = {"thread transport",
                            MatterThreadDependencyContract::kRequiresThreadTransport,
                            MatterThreadDependencyContract::kProvidesThreadTransport,
                            resolved(
                                MatterThreadDependencyContract::kRequiresThreadTransport,
                                MatterThreadDependencyContract::kProvidesThreadTransport)};
  threadDependencies_[2] = {
      "on-network-only commissioning", MatterThreadDependencyContract::kRequiresOnNetworkOnlyCommissioning,
      MatterThreadDependencyContract::kProvidesOnNetworkOnlyCommissioning,
      resolved(MatterThreadDependencyContract::kRequiresOnNetworkOnlyCommissioning,
               MatterThreadDependencyContract::kProvidesOnNetworkOnlyCommissioning)};
  threadDependencies_[3] = {"preferences storage",
                            MatterThreadDependencyContract::kRequiresPreferencesStorage,
                            MatterThreadDependencyContract::kProvidesPreferencesStorage,
                            resolved(
                                MatterThreadDependencyContract::kRequiresPreferencesStorage,
                                MatterThreadDependencyContract::kProvidesPreferencesStorage)};
  threadDependencies_[4] = {
      "cooperative loop pump", MatterThreadDependencyContract::kRequiresCooperativeLoopPump,
      MatterThreadDependencyContract::kProvidesCooperativeLoopPump,
      resolved(MatterThreadDependencyContract::kRequiresCooperativeLoopPump,
               MatterThreadDependencyContract::kProvidesCooperativeLoopPump)};
  threadDependencies_[5] = {"dataset set/get API",
                            MatterThreadDependencyContract::kRequiresDatasetSetGet,
                            MatterThreadDependencyContract::kProvidesDatasetSetGet,
                            resolved(
                                MatterThreadDependencyContract::kRequiresDatasetSetGet,
                                MatterThreadDependencyContract::kProvidesDatasetSetGet)};
  threadDependencies_[6] = {
      "passphrase dataset derivation", MatterThreadDependencyContract::kRequiresDatasetFromPassphrase,
      MatterThreadDependencyContract::kProvidesDatasetFromPassphrase,
      resolved(MatterThreadDependencyContract::kRequiresDatasetFromPassphrase,
               MatterThreadDependencyContract::kProvidesDatasetFromPassphrase)};
  threadDependencies_[7] = {
      "attach and role query", MatterThreadDependencyContract::kRequiresAttachAndRoleQuery,
      MatterThreadDependencyContract::kProvidesAttachAndRoleQuery,
      resolved(MatterThreadDependencyContract::kRequiresAttachAndRoleQuery,
               MatterThreadDependencyContract::kProvidesAttachAndRoleQuery)};
  threadDependencies_[8] = {"BLE rendezvous",
                            MatterThreadDependencyContract::kRequiresBleRendezvous,
                            MatterThreadDependencyContract::kProvidesBleRendezvous,
                            resolved(
                                MatterThreadDependencyContract::kRequiresBleRendezvous,
                                MatterThreadDependencyContract::kProvidesBleRendezvous)};
  threadDependencies_[9] = {"commissioner / joiner",
                            MatterThreadDependencyContract::kRequiresCommissionerJoiner,
                            MatterThreadDependencyContract::kProvidesCommissionerJoiner,
                            resolved(
                                MatterThreadDependencyContract::kRequiresCommissionerJoiner,
                                MatterThreadDependencyContract::kProvidesCommissionerJoiner)};
  threadDependencies_[10] = {"border router",
                             MatterThreadDependencyContract::kRequiresBorderRouter,
                             MatterThreadDependencyContract::kProvidesBorderRouter,
                             resolved(
                                 MatterThreadDependencyContract::kRequiresBorderRouter,
                                 MatterThreadDependencyContract::kProvidesBorderRouter)};
  threadDependencies_[11] = {"UDP sketch surface",
                             MatterThreadDependencyContract::kRequiresUdpSketchSurface,
                             MatterThreadDependencyContract::kProvidesUdpSketchSurface,
                             resolved(
                                 MatterThreadDependencyContract::kRequiresUdpSketchSurface,
                                 MatterThreadDependencyContract::kProvidesUdpSketchSurface)};
}

const MatterFoundationEndpointDescriptor* Nrf54MatterOnOffLightFoundation::endpoints(
    size_t* outCount) const {
  if (outCount != nullptr) {
    *outCount = kEndpointCount;
  }
  return endpoints_;
}

const MatterFoundationEndpointDescriptor* Nrf54MatterOnOffLightFoundation::endpoint(
    MatterEndpointId endpointId) const {
  for (size_t i = 0; i < kEndpointCount; ++i) {
    if (endpoints_[i].endpointId == endpointId) {
      return &endpoints_[i];
    }
  }
  return nullptr;
}

bool Nrf54MatterOnOffLightFoundation::describeEndpoint(
    MatterEndpointId endpointId,
    MatterFoundationDescriptorSummary* outSummary) const {
  if (outSummary == nullptr) {
    return false;
  }

  memset(outSummary, 0, sizeof(*outSummary));
  const MatterFoundationEndpointDescriptor* descriptor = endpoint(endpointId);
  if (descriptor == nullptr) {
    return false;
  }

  outSummary->valid = true;
  outSummary->rootEndpoint = endpointId == kRootEndpointId;
  outSummary->endpointId = descriptor->endpointId;
  outSummary->deviceTypeId = descriptor->deviceTypeId;
  outSummary->deviceTypeName = descriptor->deviceTypeName;
  outSummary->serverClusterCount = descriptor->serverClusterCount;
  outSummary->childEndpointCount =
      outSummary->rootEndpoint ? (kEndpointCount - 1U) : 0U;
  return true;
}

bool Nrf54MatterOnOffLightFoundation::supportsEndpoint(
    MatterEndpointId endpointId) const {
  return endpoint(endpointId) != nullptr;
}

bool Nrf54MatterOnOffLightFoundation::supportsServerCluster(
    MatterEndpointId endpointId, MatterClusterId clusterId) const {
  return serverCluster(endpointId, clusterId) != nullptr;
}

const MatterFoundationClusterDescriptor*
Nrf54MatterOnOffLightFoundation::serverCluster(
    MatterEndpointId endpointId,
    MatterClusterId clusterId) const {
  const MatterFoundationEndpointDescriptor* descriptor = endpoint(endpointId);
  if (descriptor == nullptr || descriptor->serverClusters == nullptr) {
    return nullptr;
  }

  for (size_t i = 0; i < descriptor->serverClusterCount; ++i) {
    if (descriptor->serverClusters[i].id == clusterId) {
      return &descriptor->serverClusters[i];
    }
  }
  return nullptr;
}

size_t Nrf54MatterOnOffLightFoundation::endpointPartsList(
    MatterEndpointId endpointId,
    MatterEndpointId* outEndpoints,
    size_t outCapacity) const {
  if (endpointId != kRootEndpointId) {
    return 0U;
  }

  size_t written = 0U;
  for (size_t i = 0; i < kEndpointCount; ++i) {
    if (endpoints_[i].endpointId == kRootEndpointId) {
      continue;
    }
    if (outEndpoints != nullptr && written < outCapacity) {
      outEndpoints[written] = endpoints_[i].endpointId;
    }
    ++written;
  }
  return written;
}

const char* Nrf54MatterOnOffLightFoundation::clusterName(
    MatterEndpointId endpointId,
    MatterClusterId clusterId) const {
  const MatterFoundationClusterDescriptor* descriptor =
      serverCluster(endpointId, clusterId);
  return descriptor != nullptr && descriptor->name != nullptr
             ? descriptor->name
             : "UnknownCluster";
}

const MatterFoundationThreadDependency*
Nrf54MatterOnOffLightFoundation::threadDependencies(size_t* outCount) const {
  if (outCount != nullptr) {
    *outCount = kThreadDependencyCount;
  }
  return threadDependencies_;
}

bool Nrf54MatterOnOffLightFoundation::threadDependenciesResolved() const {
  return MatterThreadDependencyContract::kResolved;
}

bool Nrf54MatterOnOffLightFoundation::buildSeamsAligned() const {
  return MatterRuntimeOwnership::kMatterBuildSeamCurrentEnabled &&
         OpenThreadRuntimeOwnership::kCoreBuildSeamCurrentEnabled;
}

bool Nrf54MatterOnOffLightFoundation::mechanicalPathPossible() const {
  return MatterRuntimeOwnership::kCompileOnlyMatterTargetClaimed &&
         buildSeamsAligned() && threadDependenciesResolved();
}

void Nrf54MatterOnOffLightFoundation::buildDefaultThreadOnNetworkQrPayload(
    MatterQrCodePayload* outPayload) const {
  if (outPayload == nullptr) {
    return;
  }

  memset(outPayload, 0, sizeof(*outPayload));
  outPayload->setupPinCode = 20202021UL;
  outPayload->discriminator = 3840U;
  outPayload->vendorId = 12U;
  outPayload->productId = 1U;
  outPayload->rendezvousFlags =
      kMatterRendezvousOnNetwork | kMatterRendezvousThread;
  outPayload->commissioningFlow = MatterCommissioningFlow::kStandard;
}

void Nrf54MatterOnOffLightFoundation::buildDefaultThreadOnNetworkManualPayload(
    MatterManualPairingPayload* outPayload) const {
  if (outPayload == nullptr) {
    return;
  }

  memset(outPayload, 0, sizeof(*outPayload));
  outPayload->setupPinCode = 20202021UL;
  outPayload->discriminator = 3840U;
  outPayload->commissioningFlow = MatterCommissioningFlow::kStandard;
}

bool Nrf54MatterOnOffLightFoundation::makeDefaultThreadOnNetworkQrCode(
    char* outBuffer, size_t outBufferSize) const {
  MatterQrCodePayload payload;
  buildDefaultThreadOnNetworkQrPayload(&payload);
  return matterQrCode(payload, outBuffer, outBufferSize);
}

bool Nrf54MatterOnOffLightFoundation::makeDefaultThreadOnNetworkManualCode(
    char* outBuffer, size_t outBufferSize) const {
  MatterManualPairingPayload payload;
  buildDefaultThreadOnNetworkManualPayload(&payload);
  return matterManualPairingCode(payload, outBuffer, outBufferSize);
}

size_t Nrf54MatterOnOffLightFoundation::onboardingStepCount() const {
  return sizeof(kOnboardingSteps) / sizeof(kOnboardingSteps[0]);
}

const char* Nrf54MatterOnOffLightFoundation::onboardingStep(size_t index) const {
  return index < onboardingStepCount() ? kOnboardingSteps[index] : nullptr;
}

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
bool Nrf54MatterOnOffLightFoundation::exportThreadDataset(
    const otOperationalDataset& threadDataset,
    chip::Thread::OperationalDataset* outDataset,
    CHIP_ERROR* outError) const {
  return setThreadDatasetFromOpenThread(threadDataset, outDataset, outError);
}

bool Nrf54MatterOnOffLightFoundation::exportThreadDatasetFromExperimental(
    const Nrf54ThreadExperimental& thread,
    chip::Thread::OperationalDataset* outDataset,
    CHIP_ERROR* outError) const {
  otOperationalDataset threadDataset = {};
  if (!thread.getActiveDataset(&threadDataset)) {
    if (outError != nullptr) {
      *outError = CHIP_ERROR_INCORRECT_STATE;
    }
    return false;
  }
  return setThreadDatasetFromOpenThread(threadDataset, outDataset, outError);
}

bool Nrf54MatterOnOffLightFoundation::setThreadDatasetFromOpenThread(
    const otOperationalDataset& threadDataset,
    chip::Thread::OperationalDataset* outDataset,
    CHIP_ERROR* outError) {
  CHIP_ERROR error = CHIP_NO_ERROR;
  if (outDataset == nullptr) {
    error = CHIP_ERROR_INVALID_ARGUMENT;
  } else {
    outDataset->Clear();

    if (threadDataset.mComponents.mIsActiveTimestampPresent) {
      error = outDataset->SetActiveTimestamp(
          threadDataset.mActiveTimestamp.mSeconds);
    }
    if (error == CHIP_NO_ERROR && threadDataset.mComponents.mIsChannelPresent) {
      error = outDataset->SetChannel(threadDataset.mChannel);
    }
    if (error == CHIP_NO_ERROR &&
        threadDataset.mComponents.mIsExtendedPanIdPresent) {
      error = outDataset->SetExtendedPanId(threadDataset.mExtendedPanId.m8);
    }
    if (error == CHIP_NO_ERROR && threadDataset.mComponents.mIsNetworkKeyPresent) {
      error = outDataset->SetMasterKey(threadDataset.mNetworkKey.m8);
    }
    if (error == CHIP_NO_ERROR &&
        threadDataset.mComponents.mIsMeshLocalPrefixPresent) {
      error = outDataset->SetMeshLocalPrefix(
          threadDataset.mMeshLocalPrefix.m8);
    }
    if (error == CHIP_NO_ERROR &&
        threadDataset.mComponents.mIsNetworkNamePresent) {
      error = outDataset->SetNetworkName(threadDataset.mNetworkName.m8);
    }
    if (error == CHIP_NO_ERROR && threadDataset.mComponents.mIsPanIdPresent) {
      error = outDataset->SetPanId(threadDataset.mPanId);
    }
    if (error == CHIP_NO_ERROR && threadDataset.mComponents.mIsPskcPresent) {
      error = outDataset->SetPSKc(threadDataset.mPskc.m8);
    }
    if (error == CHIP_NO_ERROR && !outDataset->IsCommissioned()) {
      error = CHIP_ERROR_INCORRECT_STATE;
    }
  }

  if (outError != nullptr) {
    *outError = error;
  }
  return error == CHIP_NO_ERROR;
}
#endif

}  // namespace xiao_nrf54l15
