#pragma once

namespace xiao_nrf54l15 {

struct MatterRuntimeOwnership {
  static constexpr bool kCpuAppHostsMatter = true;
  static constexpr bool kUsesThreadTransport = true;
  static constexpr bool kUsesOnNetworkCommissioningFirst = true;
  static constexpr bool kUsesBleRendezvousFirst = false;
  static constexpr bool kUsesPreferencesStorage = true;
  static constexpr bool kUsesCracenEntropy = true;
  static constexpr bool kUsesCracenCrypto = true;
  static constexpr bool kUsesHalTimebase = true;
  static constexpr bool kUsesCooperativeLoopPump = true;
  static constexpr bool kUsesVprOffload = false;
  static constexpr bool kConnectedHomeIpImportPathDefined = true;
  static constexpr bool kConnectedHomeIpCurrentlyImported = true;
  static constexpr bool kConnectedHomeIpHeaderSeedImported = true;
  static constexpr bool kConnectedHomeIpSupportSeedImported = true;
  static constexpr bool kConnectedHomeIpCoreErrorSeedImported = true;
  static constexpr bool kConnectedHomeIpCoreKeySeedImported = true;
  static constexpr bool kConnectedHomeIpSupportTimeSeedImported = true;
  static constexpr bool kConnectedHomeIpSupportHexSeedImported = true;
  static constexpr bool kConnectedHomeIpSupportThreadDatasetSeedImported = true;
  static constexpr bool kMatterManualPairingHelperAvailable = true;
  static constexpr bool kMatterQrCodeHelperAvailable = true;
  static constexpr bool kConnectedHomeIpFullScaffoldImported = false;
  static constexpr bool kCompileOnlyMatterTargetClaimed = false;
  static constexpr bool kFirstDeviceTypeOnOffLight = true;
#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
  static constexpr bool kMatterBuildSeamCurrentEnabled = true;
#else
  static constexpr bool kMatterBuildSeamCurrentEnabled = false;
#endif
  static constexpr const char* kFirstCommissioningTarget =
      "on-network-only";
  static constexpr const char* kFirstDeviceType = "on-off-light";
  static constexpr const char* kThreadPlatformHeaderPath =
      "hardware/nrf54l15clean/nrf54l15clean/libraries/"
      "Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.h";
  static constexpr const char* kMatterImportScriptPath =
      "scripts/import_connectedhomeip_scaffold.sh";
  static constexpr const char* kMatterStagingPath =
      "hardware/nrf54l15clean/nrf54l15clean/libraries/"
      "Nrf54L15-Clean-Implementation/third_party/connectedhomeip";
  static constexpr const char* kMatterImportedRef =
      "337f8f54b4f0813681664e5b179dc3e16fdd14a0";
  static constexpr const char* kOwnershipDocPath =
      "docs/MATTER_RUNTIME_OWNERSHIP.md";
  static constexpr const char* kFoundationManifestPath =
      "docs/MATTER_FOUNDATION_MANIFEST.md";
};

inline const char* matterFoundationBuildMode() {
  return MatterRuntimeOwnership::kMatterBuildSeamCurrentEnabled
             ? "staged-seam"
             : "disabled";
}

inline const char* matterFoundationTransportName() {
  return MatterRuntimeOwnership::kUsesThreadTransport ? "thread" : "none";
}

inline const char* matterFoundationCommissioningName() {
  return MatterRuntimeOwnership::kUsesOnNetworkCommissioningFirst
             ? "on-network-only"
             : "ble+rendezvous";
}

inline const char* matterFoundationImportMode() {
  return MatterRuntimeOwnership::kMatterQrCodeHelperAvailable
             ? "header+support+error+key+time+hex+thread-dataset+onboarding-code-seed"
         : MatterRuntimeOwnership::kMatterManualPairingHelperAvailable
             ? "header+support+error+key+time+hex+thread-dataset+manual-code-seed"
         : MatterRuntimeOwnership::kConnectedHomeIpSupportThreadDatasetSeedImported
             ? "header+support+error+key+time+hex+thread-dataset-seed"
         : MatterRuntimeOwnership::kConnectedHomeIpSupportHexSeedImported
             ? "header+support+error+key+time+hex-seed"
         : MatterRuntimeOwnership::kConnectedHomeIpSupportTimeSeedImported
             ? "header+support+error+key+time-seed"
         : MatterRuntimeOwnership::kConnectedHomeIpCoreKeySeedImported
             ? "header+support+error+key-seed"
         : MatterRuntimeOwnership::kConnectedHomeIpCoreErrorSeedImported
             ? "header+support+error-seed"
         : MatterRuntimeOwnership::kConnectedHomeIpSupportSeedImported
             ? "header+support-seed"
         : MatterRuntimeOwnership::kConnectedHomeIpHeaderSeedImported
             ? "header-seed"
             : "path-only";
}

}  // namespace xiao_nrf54l15
