#include <nrf54_thread_experimental.h>

using xiao_nrf54l15::Nrf54ThreadExperimental;

namespace {

// Paste a real active-dataset TLV hex string here to attach to an existing
// Thread network. Leave empty after the first successful boot to prove that the
// OpenThread settings-backed restore path survives reset.
constexpr char kReferenceDatasetHex[] = "";
constexpr bool kWipeThreadSettingsOnBoot = false;
constexpr uint32_t kStatusIntervalMs = 1000UL;

Nrf54ThreadExperimental gThread;
Nrf54ThreadExperimental::Role gLastRole =
    Nrf54ThreadExperimental::Role::kUnknown;
uint32_t gLastReportMs = 0U;
bool gDatasetHexPrinted = false;
bool gDatasetImportOk = true;
char gDatasetHex[(OT_OPERATIONAL_DATASET_MAX_LENGTH * 2U) + 1U] = {0};

void printStatus(const char* reason) {
  Serial.print("thread_ref reason=");
  Serial.print(reason);
  Serial.print(" role=");
  Serial.print(gThread.roleName());
  Serial.print(" rloc16=0x");
  Serial.print(gThread.rloc16(), HEX);
  Serial.print(" attached=");
  Serial.print(gThread.attached() ? 1 : 0);
  Serial.print(" dataset=");
  Serial.print(gThread.datasetConfigured() ? 1 : 0);
  Serial.print(" restored=");
  Serial.print(gThread.restoredFromSettings() ? 1 : 0);
  Serial.print(" err=");
  Serial.println(static_cast<int>(gThread.lastError()));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  if (kReferenceDatasetHex[0] != '\0') {
    gDatasetImportOk = gThread.setActiveDatasetHex(kReferenceDatasetHex);
    Serial.print("thread_ref import=");
    Serial.println(gDatasetImportOk ? 1 : 0);
  }

  if (gDatasetImportOk && gThread.begin(kWipeThreadSettingsOnBoot)) {
    Serial.print("thread_ref boot wipe=");
    Serial.println(kWipeThreadSettingsOnBoot ? 1 : 0);
    if (kReferenceDatasetHex[0] == '\0') {
      Serial.println("thread_ref dataset_source=settings");
    } else {
      Serial.println("thread_ref dataset_source=compiled_hex");
    }
  } else {
    Serial.println("thread_ref begin-failed");
  }
#else
  Serial.println(
      "Enable Tools > Thread Core > Experimental Stage Core (Leader/Child/Router + UDP).");
#endif
}

void loop() {
  gThread.process();

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  if (!gDatasetImportOk) {
    if ((millis() - gLastReportMs) >= kStatusIntervalMs) {
      gLastReportMs = millis();
      printStatus("dataset-parse-failed");
    }
    return;
  }

  if (!gDatasetHexPrinted &&
      gThread.exportConfiguredOrActiveDatasetHex(gDatasetHex,
                                                 sizeof(gDatasetHex),
                                                 nullptr)) {
    Serial.print("thread_ref dataset_hex=");
    Serial.println(gDatasetHex);
    gDatasetHexPrinted = true;
  }

  const Nrf54ThreadExperimental::Role currentRole = gThread.role();
  if (currentRole != gLastRole || (millis() - gLastReportMs) >= kStatusIntervalMs) {
    gLastRole = currentRole;
    gLastReportMs = millis();
    printStatus("status");
  }
#endif
}
