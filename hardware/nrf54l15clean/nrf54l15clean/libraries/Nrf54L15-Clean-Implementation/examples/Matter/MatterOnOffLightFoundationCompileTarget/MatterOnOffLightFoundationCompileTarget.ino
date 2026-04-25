#include <Arduino.h>
#include <matter_foundation_target.h>
#include <matter_platform_nrf54l15.h>

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core (Leader/Child/Router + UDP) before building this Matter example."
#endif

#if !defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) || \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE == 0)
#error "Enable Tools > Matter Foundation > Experimental Compile Target (On-Network On/Off Light) before building this example."
#endif

#include <lib/core/CHIPError.h>
#include <lib/support/BytesToHex.h>
#include <lib/support/ThreadOperationalDataset.h>

namespace {

xiao_nrf54l15::Nrf54MatterOnOffLightFoundation g_foundation;

void printHex(chip::ByteSpan bytes) {
  char buffer[(chip::Thread::kSizeOperationalDataset * 2U) + 1U] = {0};
  const CHIP_ERROR error = chip::Encoding::BytesToUppercaseHexString(
      bytes.data(), bytes.size(), buffer, sizeof(buffer));
  if (error == CHIP_NO_ERROR) {
    Serial.println(buffer);
  } else {
    Serial.print("hex-error-0x");
    Serial.println(error.AsInteger(), HEX);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  Serial.print("matter_compile_target build=");
  Serial.println(xiao_nrf54l15::matterFoundationBuildMode());
  Serial.print("matter_compile_target target=");
  Serial.println(xiao_nrf54l15::matterFoundationTargetName());
  Serial.print("matter_compile_target seams_aligned=");
  Serial.println(g_foundation.buildSeamsAligned() ? 1 : 0);
  Serial.print("matter_compile_target contract_resolved=");
  Serial.println(g_foundation.threadDependenciesResolved() ? 1 : 0);
  Serial.print("matter_compile_target mechanical_path=");
  Serial.println(g_foundation.mechanicalPathPossible() ? 1 : 0);

  size_t endpointCount = 0;
  const auto* endpoints = g_foundation.endpoints(&endpointCount);
  Serial.print("matter_compile_target endpoint_count=");
  Serial.println(endpointCount);
  for (size_t i = 0; i < endpointCount; ++i) {
    Serial.print("matter_compile_target endpoint[");
    Serial.print(i);
    Serial.print("].id=");
    Serial.println(endpoints[i].endpointId);
    Serial.print("matter_compile_target endpoint[");
    Serial.print(i);
    Serial.print("].device=");
    Serial.println(endpoints[i].deviceTypeName);
    for (size_t j = 0; j < endpoints[i].serverClusterCount; ++j) {
      Serial.print("matter_compile_target endpoint[");
      Serial.print(i);
      Serial.print("].cluster[");
      Serial.print(j);
      Serial.print("]=");
      Serial.print(endpoints[i].serverClusters[j].name);
      Serial.print(":0x");
      Serial.println(endpoints[i].serverClusters[j].id, HEX);
    }
  }

  size_t dependencyCount = 0;
  const auto* dependencies = g_foundation.threadDependencies(&dependencyCount);
  for (size_t i = 0; i < dependencyCount; ++i) {
    Serial.print("matter_compile_target dependency[");
    Serial.print(i);
    Serial.print("]=");
    Serial.print(dependencies[i].feature);
    Serial.print(" required=");
    Serial.print(dependencies[i].required ? 1 : 0);
    Serial.print(" available=");
    Serial.print(dependencies[i].available ? 1 : 0);
    Serial.print(" resolved=");
    Serial.println(dependencies[i].resolved ? 1 : 0);
  }

  char manualCode[xiao_nrf54l15::kMatterManualPairingLongCodeLength + 1U] = {0};
  char qrCode[xiao_nrf54l15::kMatterQrCodeTextLength + 1U] = {0};
  const bool manualOk =
      g_foundation.makeDefaultThreadOnNetworkManualCode(manualCode,
                                                        sizeof(manualCode));
  const bool qrOk =
      g_foundation.makeDefaultThreadOnNetworkQrCode(qrCode, sizeof(qrCode));
  Serial.print("matter_compile_target manual_ok=");
  Serial.println(manualOk ? 1 : 0);
  Serial.print("matter_compile_target manual_code=");
  Serial.println(manualOk ? manualCode : "error");
  Serial.print("matter_compile_target qr_ok=");
  Serial.println(qrOk ? 1 : 0);
  Serial.print("matter_compile_target qr_code=");
  Serial.println(qrOk ? qrCode : "error");

  otOperationalDataset threadDataset = {};
  xiao_nrf54l15::Nrf54ThreadExperimental::buildDemoDataset(&threadDataset);
  chip::Thread::OperationalDataset matterDataset;
  CHIP_ERROR exportError = CHIP_NO_ERROR;
  const bool exportOk =
      g_foundation.exportThreadDataset(threadDataset, &matterDataset, &exportError);
  Serial.print("matter_compile_target dataset_export_ok=");
  Serial.println(exportOk ? 1 : 0);
  Serial.print("matter_compile_target dataset_export_error=0x");
  Serial.println(exportError.AsInteger(), HEX);
  Serial.print("matter_compile_target dataset_commissioned=");
  Serial.println(matterDataset.IsCommissioned() ? 1 : 0);
  Serial.print("matter_compile_target dataset_len=");
  Serial.println(matterDataset.AsByteSpan().size());
  Serial.print("matter_compile_target dataset_hex=");
  printHex(matterDataset.AsByteSpan());

  for (size_t i = 0; i < g_foundation.onboardingStepCount(); ++i) {
    Serial.print("matter_compile_target step[");
    Serial.print(i);
    Serial.print("]=");
    Serial.println(g_foundation.onboardingStep(i));
  }
}

void loop() {}
