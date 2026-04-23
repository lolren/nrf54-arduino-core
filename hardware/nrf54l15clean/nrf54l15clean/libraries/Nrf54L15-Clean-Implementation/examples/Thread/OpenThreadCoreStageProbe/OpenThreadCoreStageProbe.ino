#include "openthread_platform_nrf54l15.h"

#include <openthread/instance.h>
#include <openthread/platform/settings.h>

using xiao_nrf54l15::OpenThreadPlatformSkeleton;
using xiao_nrf54l15::OpenThreadRuntimeOwnership;

namespace {

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
extern "C" size_t nrf54l15OpenThreadStageInstanceSize(void);
constexpr uint32_t kStageInitDelayMs = 4000UL;
otInstance* gStageInstance = nullptr;
bool gStageInitAttempted = false;
bool gStageInstanceCreated = false;
bool gStageInstanceInitialized = false;
#endif

uint32_t gLastReportMs = 0;

void printStageState() {
  Serial.print("ot_stage seam=");
  Serial.print(OpenThreadRuntimeOwnership::kCoreBuildSeamAvailable ? 1 : 0);
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kCoreBuildSeamCurrentEnabled ? 1 : 0);
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kCoreCryptoFallbackCurrentEnabled
                   ? 1
                   : 0);
  Serial.print(" ownership=");
  Serial.print(OpenThreadRuntimeOwnership::kCpuAppHostsCore ? "cpuapp" : "other");
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kUsesZigbeeRadioBackend
                   ? "zigbee-radio"
                   : "other-radio");
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kUsesVprRadioOffload ? "vpr" : "no-vpr");
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kHeadersOnlyImportActive
                   ? "headers-only"
                   : "full-core");
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kFullCoreIntegrated ? "core-live" : "core-staged");
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  Serial.print(" status=");
  Serial.print(gStageInitAttempted ? 1 : 0);
  Serial.print("/");
  Serial.print(gStageInstanceCreated ? 1 : 0);
  Serial.print("/");
  Serial.print(gStageInstanceInitialized ? 1 : 0);
  Serial.print("/");
  Serial.print(otGetVersionString());
  Serial.print("/");
  Serial.print(nrf54l15OpenThreadStageInstanceSize());
#else
  Serial.print(" status=disabled");
#endif
  Serial.println();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  Serial.println("ot_stage boot");
  OpenThreadPlatformSkeleton::begin();
  otPlatSettingsInit(nullptr, nullptr, 0);
  Serial.println("ot_stage platform-ready");
  printStageState();
}

void loop() {
  OpenThreadPlatformSkeleton::process();

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  if (!gStageInitAttempted && millis() >= kStageInitDelayMs) {
    Serial.println("ot_stage init-begin");
    gStageInitAttempted = true;
    gStageInstance = otInstanceInitSingle();
    gStageInstanceCreated = gStageInstance != nullptr;
    gStageInstanceInitialized =
        gStageInstanceCreated && otInstanceIsInitialized(gStageInstance);
    Serial.println("ot_stage init-end");
    printStageState();
  }
#endif

  if ((millis() - gLastReportMs) >= 1000UL) {
    gLastReportMs = millis();
    printStageState();
  }

  delay(200);
}
