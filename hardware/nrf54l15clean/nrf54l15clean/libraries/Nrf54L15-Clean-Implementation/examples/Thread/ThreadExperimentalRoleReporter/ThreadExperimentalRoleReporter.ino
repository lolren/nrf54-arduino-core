#include <nrf54_thread_experimental.h>

using xiao_nrf54l15::Nrf54ThreadExperimental;

namespace {

Nrf54ThreadExperimental gThread;
Nrf54ThreadExperimental::Role gLastRole =
    Nrf54ThreadExperimental::Role::kUnknown;
uint32_t gLastReportMs = 0;

void printStatus() {
  Serial.print("thread_role role=");
  Serial.print(gThread.roleName());
  Serial.print(" rloc16=0x");
  Serial.print(gThread.rloc16(), HEX);
  Serial.print(" attached=");
  Serial.print(gThread.attached() ? 1 : 0);
  Serial.print(" err=");
  Serial.print(static_cast<int>(gThread.lastError()));
  Serial.println();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  otOperationalDataset dataset = {};
  Nrf54ThreadExperimental::buildDemoDataset(&dataset);
  gThread.setActiveDataset(dataset);
  gThread.begin();
  Serial.println("thread_role boot");
#else
  Serial.println(
      "Enable Tools > Thread Core > Experimental Stage Core (Leader/Child + UDP).");
#endif
}

void loop() {
  gThread.process();

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  const Nrf54ThreadExperimental::Role currentRole = gThread.role();
  if (currentRole != gLastRole || (millis() - gLastReportMs) >= 1000UL) {
    gLastRole = currentRole;
    gLastReportMs = millis();
    printStatus();
  }
#endif
}
