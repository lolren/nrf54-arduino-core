#include <inttypes.h>
#include <string.h>
#include <matter_manual_pairing.h>
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
#include <lib/support/BytesToHex.h>
#include <lib/support/ThreadOperationalDataset.h>
#include <lib/support/TimeUtils.h>
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

void printHex16(uint16_t value) {
  char buffer[5] = {0};
  snprintf(buffer, sizeof(buffer), "%04" PRIX16, value);
  Serial.print(buffer);
}

void printDate(uint16_t year, uint8_t month, uint8_t day) {
  char buffer[16] = {0};
  snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u",
           static_cast<unsigned>(year), static_cast<unsigned>(month),
           static_cast<unsigned>(day));
  Serial.print(buffer);
}

void printDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                   uint8_t minute, uint8_t second) {
  char buffer[24] = {0};
  snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u",
           static_cast<unsigned>(year), static_cast<unsigned>(month),
           static_cast<unsigned>(day), static_cast<unsigned>(hour),
           static_cast<unsigned>(minute), static_cast<unsigned>(second));
  Serial.print(buffer);
}

void printManualPairingCodeSample(
    const char* label,
    const xiao_nrf54l15::MatterManualPairingPayload& payload,
    const char* expectedCode) {
  char code[xiao_nrf54l15::kMatterManualPairingLongCodeLength + 1U] = {0};
  const bool ok =
      xiao_nrf54l15::matterManualPairingCode(payload, code, sizeof(code));
  Serial.print("matter_foundation ");
  Serial.print(label);
  Serial.print("_ok=");
  Serial.println(ok ? 1 : 0);
  Serial.print("matter_foundation ");
  Serial.print(label);
  Serial.print("_code=");
  Serial.println(ok ? code : "error");
  Serial.print("matter_foundation ");
  Serial.print(label);
  Serial.print("_expected=");
  Serial.println(ok && strcmp(code, expectedCode) == 0 ? 1 : 0);
}

void printQrCodeSample(const char* label,
                       const xiao_nrf54l15::MatterQrCodePayload& payload,
                       const char* expectedCode) {
  char code[xiao_nrf54l15::kMatterQrCodeTextLength + 1U] = {0};
  const bool ok = xiao_nrf54l15::matterQrCode(payload, code, sizeof(code));
  Serial.print("matter_foundation ");
  Serial.print(label);
  Serial.print("_ok=");
  Serial.println(ok ? 1 : 0);
  Serial.print("matter_foundation ");
  Serial.print(label);
  Serial.print("_code=");
  Serial.println(ok ? code : "error");
  Serial.print("matter_foundation ");
  Serial.print(label);
  Serial.print("_expected=");
  Serial.println(ok && strcmp(code, expectedCode) == 0 ? 1 : 0);
}

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
void encodeBigEndian64(uint64_t value, uint8_t (&bytes)[8]) {
  for (size_t i = 0; i < sizeof(bytes); ++i) {
    const uint8_t shift = static_cast<uint8_t>((sizeof(bytes) - 1U - i) * 8U);
    bytes[i] = static_cast<uint8_t>(value >> shift);
  }
}

void printByteSpanHex(chip::ByteSpan bytes) {
  char buffer[(chip::Thread::kSizeOperationalDataset * 2U) + 1U] = {0};
  const CHIP_ERROR error = chip::Encoding::BytesToUppercaseHexString(
      bytes.data(), bytes.size(), buffer, sizeof(buffer));
  if (error == CHIP_NO_ERROR) {
    Serial.print(buffer);
  } else {
    Serial.print("hex-error-0x");
    printHex32(error.AsInteger());
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
  printFlag("time_seed", MatterRuntimeOwnership::kConnectedHomeIpSupportTimeSeedImported);
  printFlag("hex_seed", MatterRuntimeOwnership::kConnectedHomeIpSupportHexSeedImported);
  printFlag("thread_dataset_seed",
            MatterRuntimeOwnership::kConnectedHomeIpSupportThreadDatasetSeedImported);
  printFlag("manual_code_helper",
            MatterRuntimeOwnership::kMatterManualPairingHelperAvailable);
  printFlag("qr_code_helper", MatterRuntimeOwnership::kMatterQrCodeHelperAvailable);
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

  xiao_nrf54l15::MatterManualPairingPayload shortManualCode;
  shortManualCode.setupPinCode = 20202021UL;
  shortManualCode.discriminator = 3840U;
  printManualPairingCodeSample("manual_pairing_short", shortManualCode,
                               "34970112332");

  xiao_nrf54l15::MatterManualPairingPayload longManualCode;
  longManualCode.setupPinCode = 12345679UL;
  longManualCode.discriminator = 2560U;
  longManualCode.vendorId = 45367U;
  longManualCode.productId = 14526U;
  longManualCode.commissioningFlow =
      xiao_nrf54l15::MatterCommissioningFlow::kCustom;
  printManualPairingCodeSample("manual_pairing_long", longManualCode,
                               "641295075345367145262");

  xiao_nrf54l15::MatterQrCodePayload defaultQrCode;
  defaultQrCode.setupPinCode = 2048UL;
  defaultQrCode.discriminator = 128U;
  defaultQrCode.vendorId = 12U;
  defaultQrCode.productId = 1U;
  defaultQrCode.rendezvousFlags = xiao_nrf54l15::kMatterRendezvousSoftAP;
  printQrCodeSample("qr_code_upstream_default", defaultQrCode,
                    "MT:M5L90MP500K64J00000");

  xiao_nrf54l15::MatterQrCodePayload threadQrCode;
  threadQrCode.setupPinCode = 20202021UL;
  threadQrCode.discriminator = 3840U;
  threadQrCode.vendorId = 12U;
  threadQrCode.productId = 1U;
  threadQrCode.rendezvousFlags =
      xiao_nrf54l15::kMatterRendezvousOnNetwork |
      xiao_nrf54l15::kMatterRendezvousThread;
  printQrCodeSample("qr_code_thread_onnetwork", threadQrCode,
                    "MT:M5L90W8E50KA0648G00");

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
  char groupNodeHex[HEX_ENCODED_LENGTH(sizeof(groupNodeBytes)) + 1] = {0};
  const CHIP_ERROR groupNodeHexError = chip::Encoding::BytesToUppercaseHexString(
      groupNodeBytes, sizeof(groupNodeBytes), groupNodeHex, sizeof(groupNodeHex));
  Serial.print("matter_foundation chip_group_node_hex=");
  Serial.println(groupNodeHex);
  const bool groupNodeHexOk =
      groupNodeHexError == CHIP_NO_ERROR &&
      strcmp(groupNodeHex, "FFFFFFFFFFFF1234") == 0;
  Serial.print("matter_foundation chip_group_node_hex_ok=");
  Serial.println(groupNodeHexOk ? 1 : 0);
  uint8_t groupNodeFromHex[8] = {0};
  const size_t groupNodeFromHexLen = chip::Encoding::HexToBytes(
      groupNodeHex, HEX_ENCODED_LENGTH(sizeof(groupNodeBytes)),
      groupNodeFromHex, sizeof(groupNodeFromHex));
  const bool groupNodeHexRoundTripOk =
      groupNodeFromHexLen == sizeof(groupNodeBytes) &&
      memcmp(groupNodeFromHex, groupNodeBytes, sizeof(groupNodeBytes)) == 0;
  Serial.print("matter_foundation chip_group_node_hex_roundtrip=");
  Serial.println(groupNodeHexRoundTripOk ? 1 : 0);
  uint64_t groupNodeFromUpperHex = 0;
  const size_t groupNodeUpperHexLen = chip::Encoding::UppercaseHexToUint64(
      groupNodeHex, HEX_ENCODED_LENGTH(sizeof(groupNodeBytes)),
      groupNodeFromUpperHex);
  const bool groupNodeUpperHexOk =
      groupNodeUpperHexLen == sizeof(groupNodeBytes) &&
      groupNodeFromUpperHex == groupNode;
  Serial.print("matter_foundation chip_group_node_hex_uint64_ok=");
  Serial.println(groupNodeUpperHexOk ? 1 : 0);
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
  char staticKeyHex[HEX_ENCODED_LENGTH(sizeof(staticKeyId)) + 1] = {0};
  const CHIP_ERROR staticKeyHexError = chip::Encoding::Uint32ToHex(
      staticKeyId, staticKeyHex, sizeof(staticKeyHex),
      chip::Encoding::HexFlags::kUppercaseAndNullTerminate);
  Serial.print("matter_foundation chip_key_rotating=0x");
  printHex32(rotatingKeyId);
  Serial.println();
  Serial.print("matter_foundation chip_key_static=0x");
  printHex32(staticKeyId);
  Serial.println();
  Serial.print("matter_foundation chip_key_static_hex=");
  Serial.println(staticKeyHex);
  Serial.print("matter_foundation chip_key_static_hex_ok=");
  Serial.println((staticKeyHexError == CHIP_NO_ERROR &&
                  strcmp(staticKeyHex, "00004022") == 0)
                     ? 1
                     : 0);
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
  constexpr uint16_t kSampleYear = 2026;
  constexpr uint8_t kSampleMonth = 4;
  constexpr uint8_t kSampleDay = 23;
  constexpr uint8_t kSampleHour = 12;
  constexpr uint8_t kSampleMinute = 34;
  constexpr uint8_t kSampleSecond = 56;
  constexpr uint32_t kExpectedChipEpoch = 830262896UL;
  constexpr uint32_t kExpectedUnixSeconds = 1776947696UL;
  constexpr uint32_t kExpectedUnixDays = 20566UL;
  uint32_t chipEpochSeconds = 0;
  const bool chipEpochOk =
      chip::CalendarToChipEpochTime(kSampleYear, kSampleMonth, kSampleDay,
                                    kSampleHour, kSampleMinute, kSampleSecond,
                                    chipEpochSeconds) &&
      chipEpochSeconds == kExpectedChipEpoch;
  Serial.print("matter_foundation chip_time_epoch=");
  Serial.println(chipEpochSeconds);
  Serial.print("matter_foundation chip_time_epoch_ok=");
  Serial.println(chipEpochOk ? 1 : 0);
  uint16_t roundYear = 0;
  uint8_t roundMonth = 0;
  uint8_t roundDay = 0;
  uint8_t roundHour = 0;
  uint8_t roundMinute = 0;
  uint8_t roundSecond = 0;
  chip::ChipEpochToCalendarTime(chipEpochSeconds, roundYear, roundMonth,
                                roundDay, roundHour, roundMinute, roundSecond);
  const bool chipEpochRoundTripOk =
      roundYear == kSampleYear && roundMonth == kSampleMonth &&
      roundDay == kSampleDay && roundHour == kSampleHour &&
      roundMinute == kSampleMinute && roundSecond == kSampleSecond;
  Serial.print("matter_foundation chip_time_roundtrip=");
  printDateTime(roundYear, roundMonth, roundDay, roundHour, roundMinute,
                roundSecond);
  Serial.println();
  Serial.print("matter_foundation chip_time_roundtrip_ok=");
  Serial.println(chipEpochRoundTripOk ? 1 : 0);
  uint32_t unixSeconds = 0;
  const bool unixSecondsOk =
      chip::CalendarTimeToSecondsSinceUnixEpoch(
          kSampleYear, kSampleMonth, kSampleDay, kSampleHour, kSampleMinute,
          kSampleSecond, unixSeconds) &&
      unixSeconds == kExpectedUnixSeconds;
  Serial.print("matter_foundation chip_time_unix_seconds=");
  Serial.println(unixSeconds);
  Serial.print("matter_foundation chip_time_unix_seconds_ok=");
  Serial.println(unixSecondsOk ? 1 : 0);
  uint32_t unixToChipEpochSeconds = 0;
  const bool unixToChipEpochOk =
      chip::UnixEpochToChipEpochTime(unixSeconds, unixToChipEpochSeconds) &&
      unixToChipEpochSeconds == chipEpochSeconds;
  Serial.print("matter_foundation chip_time_unix_to_chip_ok=");
  Serial.println(unixToChipEpochOk ? 1 : 0);
  uint32_t daysSinceUnixEpoch = 0;
  const bool unixDaysOk =
      chip::CalendarDateToDaysSinceUnixEpoch(kSampleYear, kSampleMonth,
                                             kSampleDay, daysSinceUnixEpoch) &&
      daysSinceUnixEpoch == kExpectedUnixDays;
  Serial.print("matter_foundation chip_time_days_since_unix=");
  Serial.println(daysSinceUnixEpoch);
  Serial.print("matter_foundation chip_time_days_since_unix_ok=");
  Serial.println(unixDaysOk ? 1 : 0);
  uint16_t adjustedYear = kSampleYear;
  uint8_t adjustedMonth = kSampleMonth;
  uint8_t adjustedDay = kSampleDay;
  const bool adjustedOk =
      chip::AdjustCalendarDate(adjustedYear, adjustedMonth, adjustedDay, 7) &&
      adjustedYear == 2026 && adjustedMonth == 4 && adjustedDay == 30;
  Serial.print("matter_foundation chip_time_adjusted=");
  printDate(adjustedYear, adjustedMonth, adjustedDay);
  Serial.println();
  Serial.print("matter_foundation chip_time_adjusted_ok=");
  Serial.println(adjustedOk ? 1 : 0);
  Serial.print("matter_foundation chip_time_leap_feb_days=");
  Serial.println(chip::DaysInMonth(2024, chip::kFebruary));
  constexpr uint64_t kDatasetActiveTimestamp = 0x0001020304050607ULL;
  constexpr uint16_t kDatasetChannel = 15;
  constexpr uint16_t kDatasetPanId = 0x1234;
  constexpr uint32_t kDatasetSecurityPolicy = 0x000000A5UL;
  constexpr uint32_t kDatasetDelayMs = 120000UL;
  constexpr char kDatasetNetworkName[] = "StageMatter";
  const uint8_t datasetExtPanId[chip::Thread::kSizeExtendedPanId] = {
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  const uint8_t datasetMasterKey[chip::Thread::kSizeMasterKey] = {
      0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
      0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
  const uint8_t datasetMeshLocalPrefix[chip::Thread::kSizeMeshLocalPrefix] = {
      0xFD, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE};
  const uint8_t datasetPskc[chip::Thread::kSizePSKc] = {
      0x88, 0xCA, 0xFC, 0x25, 0x81, 0x3A, 0xC6, 0x6B,
      0x44, 0x5C, 0xCE, 0xC2, 0xED, 0x43, 0xD2, 0x98};
  const uint8_t datasetChannelMask[] = {0x00, 0x04, 0x00, 0x1F, 0xFF, 0xE0};
  chip::Thread::OperationalDataset dataset;
  CHIP_ERROR datasetBuildError = dataset.SetActiveTimestamp(kDatasetActiveTimestamp);
  if (datasetBuildError == CHIP_NO_ERROR) {
    datasetBuildError = dataset.SetChannel(kDatasetChannel);
  }
  if (datasetBuildError == CHIP_NO_ERROR) {
    datasetBuildError = dataset.SetPanId(kDatasetPanId);
  }
  if (datasetBuildError == CHIP_NO_ERROR) {
    datasetBuildError = dataset.SetExtendedPanId(datasetExtPanId);
  }
  if (datasetBuildError == CHIP_NO_ERROR) {
    datasetBuildError = dataset.SetMasterKey(datasetMasterKey);
  }
  if (datasetBuildError == CHIP_NO_ERROR) {
    datasetBuildError = dataset.SetMeshLocalPrefix(datasetMeshLocalPrefix);
  }
  if (datasetBuildError == CHIP_NO_ERROR) {
    datasetBuildError = dataset.SetNetworkName(kDatasetNetworkName);
  }
  if (datasetBuildError == CHIP_NO_ERROR) {
    datasetBuildError = dataset.SetPSKc(datasetPskc);
  }
  if (datasetBuildError == CHIP_NO_ERROR) {
    datasetBuildError = dataset.SetChannelMask(chip::ByteSpan(
        datasetChannelMask, sizeof(datasetChannelMask)));
  }
  if (datasetBuildError == CHIP_NO_ERROR) {
    datasetBuildError = dataset.SetSecurityPolicy(kDatasetSecurityPolicy);
  }
  if (datasetBuildError == CHIP_NO_ERROR) {
    datasetBuildError = dataset.SetDelayTimer(kDatasetDelayMs);
  }
  Serial.print("matter_foundation chip_thread_dataset_build_ok=");
  Serial.println(datasetBuildError == CHIP_NO_ERROR ? 1 : 0);
  Serial.print("matter_foundation chip_thread_dataset_build_error=0x");
  printHex32(datasetBuildError.AsInteger());
  Serial.println();
  if (datasetBuildError == CHIP_NO_ERROR) {
    const chip::ByteSpan datasetBytes = dataset.AsByteSpan();
    chip::Thread::OperationalDatasetView datasetView;
    const CHIP_ERROR datasetViewError = datasetView.Init(datasetBytes);
    Serial.print("matter_foundation chip_thread_dataset_view_ok=");
    Serial.println(datasetViewError == CHIP_NO_ERROR ? 1 : 0);
    Serial.print("matter_foundation chip_thread_dataset_len=");
    Serial.println(static_cast<unsigned>(datasetBytes.size()));
    Serial.print("matter_foundation chip_thread_dataset_hex=");
    printByteSpanHex(datasetBytes);
    Serial.println();
    Serial.print("matter_foundation chip_thread_dataset_valid=");
    Serial.println(chip::Thread::OperationalDatasetView::IsValid(datasetBytes)
                       ? 1
                       : 0);
    Serial.print("matter_foundation chip_thread_dataset_commissioned=");
    Serial.println(dataset.IsCommissioned() ? 1 : 0);
    if (datasetViewError == CHIP_NO_ERROR) {
      uint64_t roundActiveTimestamp = 0;
      uint16_t roundChannel = 0;
      uint16_t roundPanId = 0;
      uint64_t roundExtPanId = 0;
      char roundNetworkName[chip::Thread::kSizeNetworkName + 1] = {0};
      uint8_t roundMasterKey[chip::Thread::kSizeMasterKey] = {0};
      uint8_t roundMeshLocalPrefix[chip::Thread::kSizeMeshLocalPrefix] = {0};
      uint8_t roundPskc[chip::Thread::kSizePSKc] = {0};
      chip::ByteSpan roundChannelMask;
      uint32_t roundSecurityPolicy = 0;
      uint32_t roundDelayMs = 0;
      CHIP_ERROR roundTripError =
          datasetView.GetActiveTimestamp(roundActiveTimestamp);
      if (roundTripError == CHIP_NO_ERROR) {
        roundTripError = datasetView.GetChannel(roundChannel);
      }
      if (roundTripError == CHIP_NO_ERROR) {
        roundTripError = datasetView.GetPanId(roundPanId);
      }
      if (roundTripError == CHIP_NO_ERROR) {
        roundTripError = datasetView.GetExtendedPanId(roundExtPanId);
      }
      if (roundTripError == CHIP_NO_ERROR) {
        roundTripError = datasetView.GetNetworkName(roundNetworkName);
      }
      if (roundTripError == CHIP_NO_ERROR) {
        roundTripError = datasetView.GetMasterKey(roundMasterKey);
      }
      if (roundTripError == CHIP_NO_ERROR) {
        roundTripError = datasetView.GetMeshLocalPrefix(roundMeshLocalPrefix);
      }
      if (roundTripError == CHIP_NO_ERROR) {
        roundTripError = datasetView.GetPSKc(roundPskc);
      }
      if (roundTripError == CHIP_NO_ERROR) {
        roundTripError = datasetView.GetChannelMask(roundChannelMask);
      }
      if (roundTripError == CHIP_NO_ERROR) {
        roundTripError = datasetView.GetSecurityPolicy(roundSecurityPolicy);
      }
      if (roundTripError == CHIP_NO_ERROR) {
        roundTripError = datasetView.GetDelayTimer(roundDelayMs);
      }
      Serial.print("matter_foundation chip_thread_dataset_roundtrip_ok=");
      Serial.println(roundTripError == CHIP_NO_ERROR ? 1 : 0);
      Serial.print("matter_foundation chip_thread_dataset_roundtrip_error=0x");
      printHex32(roundTripError.AsInteger());
      Serial.println();
      if (roundTripError == CHIP_NO_ERROR) {
        Serial.print("matter_foundation chip_thread_dataset_active=0x");
        printHex64(roundActiveTimestamp);
        Serial.println();
        Serial.print("matter_foundation chip_thread_dataset_channel=");
        Serial.println(roundChannel);
        Serial.print("matter_foundation chip_thread_dataset_panid=0x");
        printHex16(roundPanId);
        Serial.println();
        Serial.print("matter_foundation chip_thread_dataset_extpan=0x");
        printHex64(roundExtPanId);
        Serial.println();
        Serial.print("matter_foundation chip_thread_dataset_name=");
        Serial.println(roundNetworkName);
        Serial.print("matter_foundation chip_thread_dataset_mask=");
        printByteSpanHex(roundChannelMask);
        Serial.println();
        Serial.print("matter_foundation chip_thread_dataset_security=0x");
        printHex32(roundSecurityPolicy);
        Serial.println();
        Serial.print("matter_foundation chip_thread_dataset_delay_ms=");
        Serial.println(roundDelayMs);
        const bool datasetFieldMatch =
            roundActiveTimestamp == kDatasetActiveTimestamp &&
            roundChannel == kDatasetChannel &&
            roundPanId == kDatasetPanId &&
            roundExtPanId == 0x1122334455667788ULL &&
            strcmp(roundNetworkName, kDatasetNetworkName) == 0 &&
            memcmp(roundMasterKey, datasetMasterKey, sizeof(datasetMasterKey)) == 0 &&
            memcmp(roundMeshLocalPrefix, datasetMeshLocalPrefix,
                   sizeof(datasetMeshLocalPrefix)) == 0 &&
            memcmp(roundPskc, datasetPskc, sizeof(datasetPskc)) == 0 &&
            roundChannelMask.size() == sizeof(datasetChannelMask) &&
            memcmp(roundChannelMask.data(), datasetChannelMask,
                   sizeof(datasetChannelMask)) == 0 &&
            roundSecurityPolicy == kDatasetSecurityPolicy &&
            roundDelayMs == kDatasetDelayMs;
        Serial.print("matter_foundation chip_thread_dataset_fields_ok=");
        Serial.println(datasetFieldMatch ? 1 : 0);
        chip::Thread::OperationalDataset copiedDataset(datasetView);
        const chip::ByteSpan copiedBytes = copiedDataset.AsByteSpan();
        const bool datasetCopyOk =
            copiedBytes.size() == datasetBytes.size() &&
            memcmp(copiedBytes.data(), datasetBytes.data(),
                   datasetBytes.size()) == 0;
        Serial.print("matter_foundation chip_thread_dataset_copy_ok=");
        Serial.println(datasetCopyOk ? 1 : 0);
      }
    }
  }
#else
  Serial.println("matter_foundation chip_headers=disabled");
#endif
}

void loop() {
}
