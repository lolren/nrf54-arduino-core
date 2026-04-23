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
  printFlag("time_seed", MatterRuntimeOwnership::kConnectedHomeIpSupportTimeSeedImported);
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
#else
  Serial.println("matter_foundation chip_headers=disabled");
#endif
}

void loop() {
}
