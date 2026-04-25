#pragma once

#include <stddef.h>
#include <stdint.h>

#include "matter_manual_pairing.h"
#include "matter_platform_nrf54l15.h"
#include "nrf54_thread_experimental.h"
#include "openthread_platform_nrf54l15.h"

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/ThreadOperationalDataset.h>
#endif

namespace xiao_nrf54l15 {

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
using MatterEndpointId = chip::EndpointId;
using MatterClusterId = chip::ClusterId;
using MatterDeviceTypeId = chip::DeviceTypeId;
#else
using MatterEndpointId = uint16_t;
using MatterClusterId = uint32_t;
using MatterDeviceTypeId = uint32_t;
#endif

struct MatterFoundationClusterDescriptor {
  MatterClusterId id = 0;
  const char* name = nullptr;
};

struct MatterFoundationEndpointDescriptor {
  MatterEndpointId endpointId = 0;
  MatterDeviceTypeId deviceTypeId = 0;
  const char* deviceTypeName = nullptr;
  const MatterFoundationClusterDescriptor* serverClusters = nullptr;
  size_t serverClusterCount = 0;
};

struct MatterFoundationThreadDependency {
  const char* feature = nullptr;
  bool required = false;
  bool available = false;
  bool resolved = false;
};

struct MatterThreadDependencyContract {
  static constexpr bool kRequiresThreadCoreStage = true;
  static constexpr bool kProvidesThreadCoreStage =
      OpenThreadRuntimeOwnership::kCoreBuildSeamAvailable;
  static constexpr bool kRequiresThreadTransport = true;
  static constexpr bool kProvidesThreadTransport =
      MatterRuntimeOwnership::kUsesThreadTransport;
  static constexpr bool kRequiresOnNetworkOnlyCommissioning = true;
  static constexpr bool kProvidesOnNetworkOnlyCommissioning =
      MatterRuntimeOwnership::kUsesOnNetworkCommissioningFirst;
  static constexpr bool kRequiresPreferencesStorage = true;
  static constexpr bool kProvidesPreferencesStorage =
      MatterRuntimeOwnership::kUsesPreferencesStorage;
  static constexpr bool kRequiresCooperativeLoopPump = true;
  static constexpr bool kProvidesCooperativeLoopPump =
      MatterRuntimeOwnership::kUsesCooperativeLoopPump;
  static constexpr bool kRequiresDatasetSetGet = true;
  static constexpr bool kProvidesDatasetSetGet = true;
  static constexpr bool kRequiresDatasetFromPassphrase = true;
  static constexpr bool kProvidesDatasetFromPassphrase = true;
  static constexpr bool kRequiresAttachAndRoleQuery = true;
  static constexpr bool kProvidesAttachAndRoleQuery = true;
  static constexpr bool kRequiresBleRendezvous = false;
  static constexpr bool kProvidesBleRendezvous =
      MatterRuntimeOwnership::kUsesBleRendezvousFirst;
  static constexpr bool kRequiresCommissionerJoiner = false;
  static constexpr bool kProvidesCommissionerJoiner = false;
  static constexpr bool kRequiresBorderRouter = false;
  static constexpr bool kProvidesBorderRouter = false;
  static constexpr bool kRequiresUdpSketchSurface = false;
  static constexpr bool kProvidesUdpSketchSurface = true;
  static constexpr bool kResolved =
      (!kRequiresThreadCoreStage || kProvidesThreadCoreStage) &&
      (!kRequiresThreadTransport || kProvidesThreadTransport) &&
      (!kRequiresOnNetworkOnlyCommissioning ||
       kProvidesOnNetworkOnlyCommissioning) &&
      (!kRequiresPreferencesStorage || kProvidesPreferencesStorage) &&
      (!kRequiresCooperativeLoopPump || kProvidesCooperativeLoopPump) &&
      (!kRequiresDatasetSetGet || kProvidesDatasetSetGet) &&
      (!kRequiresDatasetFromPassphrase || kProvidesDatasetFromPassphrase) &&
      (!kRequiresAttachAndRoleQuery || kProvidesAttachAndRoleQuery) &&
      (!kRequiresBleRendezvous || kProvidesBleRendezvous) &&
      (!kRequiresCommissionerJoiner || kProvidesCommissionerJoiner) &&
      (!kRequiresBorderRouter || kProvidesBorderRouter) &&
      (!kRequiresUdpSketchSurface || kProvidesUdpSketchSurface);
};

class Nrf54MatterOnOffLightFoundation {
 public:
  static constexpr MatterEndpointId kRootEndpointId = 0U;
  static constexpr MatterEndpointId kPrimaryEndpointId = 1U;

  static constexpr MatterDeviceTypeId kRootNodeDeviceTypeId = 0x0016U;
  static constexpr MatterDeviceTypeId kOnOffLightDeviceTypeId = 0x0100U;

  static constexpr MatterClusterId kDescriptorClusterId = 0x001DU;
  static constexpr MatterClusterId kAccessControlClusterId = 0x001FU;
  static constexpr MatterClusterId kBasicInformationClusterId = 0x0028U;
  static constexpr MatterClusterId kGeneralCommissioningClusterId = 0x0030U;
  static constexpr MatterClusterId kNetworkCommissioningClusterId = 0x0031U;
  static constexpr MatterClusterId kAdministratorCommissioningClusterId =
      0x003CU;
  static constexpr MatterClusterId kOperationalCredentialsClusterId = 0x003EU;
  static constexpr MatterClusterId kIdentifyClusterId = 0x0003U;
  static constexpr MatterClusterId kGroupsClusterId = 0x0004U;
  static constexpr MatterClusterId kScenesClusterId = 0x0005U;
  static constexpr MatterClusterId kOnOffClusterId = 0x0006U;

  static constexpr size_t kEndpointCount = 2U;
  static constexpr size_t kThreadDependencyCount = 12U;

  Nrf54MatterOnOffLightFoundation();

  const MatterFoundationEndpointDescriptor* endpoints(
      size_t* outCount = nullptr) const;
  const MatterFoundationThreadDependency* threadDependencies(
      size_t* outCount = nullptr) const;

  bool threadDependenciesResolved() const;
  bool buildSeamsAligned() const;
  bool mechanicalPathPossible() const;

  void buildDefaultThreadOnNetworkQrPayload(MatterQrCodePayload* outPayload) const;
  void buildDefaultThreadOnNetworkManualPayload(
      MatterManualPairingPayload* outPayload) const;
  bool makeDefaultThreadOnNetworkQrCode(char* outBuffer,
                                        size_t outBufferSize) const;
  bool makeDefaultThreadOnNetworkManualCode(char* outBuffer,
                                            size_t outBufferSize) const;

  size_t onboardingStepCount() const;
  const char* onboardingStep(size_t index) const;

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
  bool exportThreadDataset(const otOperationalDataset& threadDataset,
                           chip::Thread::OperationalDataset* outDataset,
                           CHIP_ERROR* outError = nullptr) const;
  bool exportThreadDatasetFromExperimental(
      const Nrf54ThreadExperimental& thread,
      chip::Thread::OperationalDataset* outDataset,
      CHIP_ERROR* outError = nullptr) const;
#endif

 private:
#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
  static bool setThreadDatasetFromOpenThread(
      const otOperationalDataset& threadDataset,
      chip::Thread::OperationalDataset* outDataset,
      CHIP_ERROR* outError);
#endif

  MatterFoundationEndpointDescriptor endpoints_[kEndpointCount];
  MatterFoundationThreadDependency threadDependencies_[kThreadDependencyCount];
};

}  // namespace xiao_nrf54l15
