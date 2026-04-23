#include <inttypes.h>
#include <matter_platform_nrf54l15.h>

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
#include <lib/core/CHIPVendorIdentifiers.hpp>
#include <lib/core/CHIPError.h>
#include <lib/core/CHIPKeyIds.h>
#include <lib/core/ErrorStr.h>
#include <lib/core/NodeId.h>
#include <lib/support/Base64.h>
#include <lib/support/Base85.h>
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

void printHex32(uint32_t value) {
  char buffer[9] = {0};
  snprintf(buffer, sizeof(buffer), "%08" PRIX32, value);
  Serial.print(buffer);
}

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
void encodeBigEndian64(uint64_t value, uint8_t (&bytes)[8]) {
  for (size_t i = 0; i < sizeof(bytes); ++i) {
    const uint8_t shift = static_cast<uint8_t>((sizeof(bytes) - 1U - i) * 8U);
    bytes[i] = static_cast<uint8_t>(value >> shift);
  }
}
#endif

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
  printFlag("support_seed", MatterRuntimeOwnership::kConnectedHomeIpSupportSeedImported);
  printFlag("error_seed", MatterRuntimeOwnership::kConnectedHomeIpCoreErrorSeedImported);
  printFlag("key_seed", MatterRuntimeOwnership::kConnectedHomeIpCoreKeySeedImported);
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
  chip::RegisterCHIPLayerErrorFormatter();
  Serial.print("matter_foundation chip_vendor_google=0x");
  Serial.println(static_cast<uint16_t>(chip::VendorId::Google), HEX);
  Serial.print("matter_foundation chip_group_node=0x");
  const uint64_t groupNode = chip::NodeIdFromGroupId(0x1234);
  printHex64(groupNode);
  Serial.println();
  uint8_t groupNodeBytes[8] = {0};
  encodeBigEndian64(groupNode, groupNodeBytes);
  char groupNodeBase64[BASE64_ENCODED_LEN(sizeof(groupNodeBytes)) + 1] = {0};
  const uint16_t groupNodeBase64Len =
      chip::Base64Encode(groupNodeBytes, sizeof(groupNodeBytes), groupNodeBase64);
  groupNodeBase64[groupNodeBase64Len] = '\0';
  Serial.print("matter_foundation chip_group_node_b64=");
  Serial.println(groupNodeBase64);
  char groupNodeBase85[32] = {0};
  const uint16_t groupNodeBase85Len =
      chip::Base85Encode(groupNodeBytes, sizeof(groupNodeBytes), groupNodeBase85);
  groupNodeBase85[groupNodeBase85Len] = '\0';
  Serial.print("matter_foundation chip_group_node_b85=");
  Serial.println(groupNodeBase85);
  uint8_t groupNodeRoundTrip[8] = {0};
  const uint16_t groupNodeRoundTripLen = chip::Base85Decode(
      groupNodeBase85, groupNodeBase85Len, groupNodeRoundTrip);
  bool groupNodeBase85RoundTripOk =
      groupNodeRoundTripLen == sizeof(groupNodeBytes);
  for (size_t i = 0; i < sizeof(groupNodeBytes) && groupNodeBase85RoundTripOk;
       ++i) {
    if (groupNodeRoundTrip[i] != groupNodeBytes[i]) {
      groupNodeBase85RoundTripOk = false;
    }
  }
  Serial.print("matter_foundation chip_group_node_b85_roundtrip=");
  Serial.println(groupNodeBase85RoundTripOk ? 1 : 0);
  const CHIP_ERROR invalidArgument = CHIP_ERROR_INVALID_ARGUMENT;
  Serial.print("matter_foundation chip_error_invalid_argument=0x");
  printHex32(invalidArgument.AsInteger());
  Serial.println();
  Serial.print("matter_foundation chip_error_invalid_argument_str=");
  Serial.println(chip::ErrorStr(invalidArgument, false));
  const uint32_t masterKeyId = chip::ChipKeyId::MakeAppGroupMasterKeyId(0x22);
  const uint32_t rotatingKeyId = chip::ChipKeyId::MakeAppRotatingKeyId(
      chip::ChipKeyId::kFabricRootKey, chip::ChipKeyId::MakeEpochKeyId(3),
      masterKeyId, false);
  const uint32_t nextRotatingKeyId =
      chip::ChipKeyId::UpdateEpochKeyId(rotatingKeyId,
                                        chip::ChipKeyId::MakeEpochKeyId(4));
  const uint32_t staticKeyId =
      chip::ChipKeyId::ConvertToStaticAppKeyId(rotatingKeyId);
  Serial.print("matter_foundation chip_key_rotating=0x");
  printHex32(rotatingKeyId);
  Serial.println();
  Serial.print("matter_foundation chip_key_static=0x");
  printHex32(staticKeyId);
  Serial.println();
  Serial.print("matter_foundation chip_key_desc=");
  Serial.println(chip::ChipKeyId::DescribeKey(staticKeyId));
  Serial.print("matter_foundation chip_key_valid=");
  Serial.println(chip::ChipKeyId::IsValidKeyId(staticKeyId) ? 1 : 0);
  Serial.print("matter_foundation chip_key_group=");
  Serial.println(chip::ChipKeyId::IsAppGroupKey(staticKeyId) ? 1 : 0);
  Serial.print("matter_foundation chip_key_same_group=");
  Serial.println(chip::ChipKeyId::IsSameKeyOrGroup(rotatingKeyId, nextRotatingKeyId)
                     ? 1
                     : 0);
  Serial.print("matter_foundation chip_key_message_ok=");
  Serial.println(chip::ChipKeyId::IsMessageSessionId(rotatingKeyId, false) ? 1
                                                                            : 0);
#else
  Serial.println("matter_foundation chip_headers=disabled");
#endif
}

void loop() {
}
