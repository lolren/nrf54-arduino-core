#include <inttypes.h>
#include <matter_platform_nrf54l15.h>

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
#include <lib/core/CHIPVendorIdentifiers.hpp>
#include <lib/core/NodeId.h>
#endif

using xiao_nrf54l15::MatterRuntimeOwnership;

namespace {

void printFlag(const char* label, bool value) {
  Serial.print(label);
  Serial.print('=');
  Serial.println(value ? 1 : 0);
}

void printHex64(uint64_t value) {
  const uint32_t high = static_cast<uint32_t>(value >> 32U);
  const uint32_t low = static_cast<uint32_t>(value);
  char buffer[17] = {0};
  snprintf(buffer, sizeof(buffer), "%08" PRIX32 "%08" PRIX32, high, low);
  Serial.print(buffer);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  Serial.print("matter_foundation build=");
  Serial.println(xiao_nrf54l15::matterFoundationBuildMode());
  Serial.print("matter_foundation transport=");
  Serial.println(xiao_nrf54l15::matterFoundationTransportName());
  Serial.print("matter_foundation commissioning=");
  Serial.println(xiao_nrf54l15::matterFoundationCommissioningName());
  Serial.print("matter_foundation import_mode=");
  Serial.println(xiao_nrf54l15::matterFoundationImportMode());
  Serial.print("matter_foundation device=");
  Serial.println(MatterRuntimeOwnership::kFirstDeviceType);

  printFlag("cpuapp", MatterRuntimeOwnership::kCpuAppHostsMatter);
  printFlag("thread", MatterRuntimeOwnership::kUsesThreadTransport);
  printFlag("ble", MatterRuntimeOwnership::kUsesBleRendezvousFirst);
  printFlag("prefs", MatterRuntimeOwnership::kUsesPreferencesStorage);
  printFlag("entropy", MatterRuntimeOwnership::kUsesCracenEntropy);
  printFlag("crypto", MatterRuntimeOwnership::kUsesCracenCrypto);
  printFlag("clock", MatterRuntimeOwnership::kUsesHalTimebase);
  printFlag("loop", MatterRuntimeOwnership::kUsesCooperativeLoopPump);
  printFlag("vpr", MatterRuntimeOwnership::kUsesVprOffload);
  printFlag("import", MatterRuntimeOwnership::kConnectedHomeIpImportPathDefined);
  printFlag("imported", MatterRuntimeOwnership::kConnectedHomeIpCurrentlyImported);
  printFlag("header_seed", MatterRuntimeOwnership::kConnectedHomeIpHeaderSeedImported);
  printFlag("full_scaffold", MatterRuntimeOwnership::kConnectedHomeIpFullScaffoldImported);
  printFlag("matter_target", MatterRuntimeOwnership::kCompileOnlyMatterTargetClaimed);

  Serial.print("matter_foundation import=");
  Serial.println(MatterRuntimeOwnership::kMatterImportScriptPath);
  Serial.print("matter_foundation stage=");
  Serial.println(MatterRuntimeOwnership::kMatterStagingPath);
  Serial.print("matter_foundation ref=");
  Serial.println(MatterRuntimeOwnership::kMatterImportedRef);
  Serial.print("matter_foundation docs=");
  Serial.println(MatterRuntimeOwnership::kOwnershipDocPath);

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
  Serial.print("matter_foundation chip_vendor_google=0x");
  Serial.println(static_cast<uint16_t>(chip::VendorId::Google), HEX);
  Serial.print("matter_foundation chip_group_node=0x");
  printHex64(chip::NodeIdFromGroupId(0x1234));
  Serial.println();
#else
  Serial.println("matter_foundation chip_headers=disabled");
#endif
}

void loop() {
}
