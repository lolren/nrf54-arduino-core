#include <Arduino.h>
#include <nrf54_thread_experimental.h>

#include <stdlib.h>
#include <string.h>

using xiao_nrf54l15::Nrf54ThreadExperimental;

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core (Leader/Child/Router + UDP) before building this example."
#endif

namespace {

constexpr size_t kLineBufferSize =
    (OT_OPERATIONAL_DATASET_MAX_LENGTH * 2U) + 32U;
constexpr uint32_t kHeartbeatMs = 5000UL;

Nrf54ThreadExperimental gThread;
char g_lineBuffer[kLineBufferSize] = {0};
size_t g_lineLength = 0U;
uint32_t g_lastHeartbeatMs = 0U;
Nrf54ThreadExperimental::Role g_lastRole =
    Nrf54ThreadExperimental::Role::kUnknown;
char g_datasetHex[(OT_OPERATIONAL_DATASET_MAX_LENGTH * 2U) + 1U] = {0};

void printState(const char* reason) {
  Serial.print("thread_cmd reason=");
  Serial.println(reason);
  Serial.print("thread_cmd role=");
  Serial.println(gThread.roleName());
  Serial.print("thread_cmd attached=");
  Serial.println(gThread.attached() ? 1 : 0);
  Serial.print("thread_cmd dataset=");
  Serial.println(gThread.datasetConfigured() ? 1 : 0);
  Serial.print("thread_cmd restored=");
  Serial.println(gThread.restoredFromSettings() ? 1 : 0);
  Serial.print("thread_cmd rloc16=0x");
  Serial.println(gThread.rloc16(), HEX);
  Serial.print("thread_cmd err=");
  Serial.println(static_cast<int>(gThread.lastError()));
}

void printDataset() {
  if (!gThread.exportConfiguredOrActiveDatasetHex(g_datasetHex,
                                                  sizeof(g_datasetHex),
                                                  nullptr)) {
    Serial.println("thread_cmd dataset_hex=error");
    return;
  }

  Serial.print("thread_cmd dataset_hex=");
  Serial.println(g_datasetHex);
}

void printHelp() {
  Serial.println("thread_cmd commands:");
  Serial.println("thread_cmd   state");
  Serial.println("thread_cmd   dataset");
  Serial.println("thread_cmd   demo-dataset");
  Serial.println("thread_cmd   dataset-hex <ot-tlv-hex>");
  Serial.println("thread_cmd   router");
  Serial.println("thread_cmd   restart");
  Serial.println("thread_cmd   wipe-settings");
  Serial.println("thread_cmd   help");
}

void handleLine(char* line) {
  if (line == nullptr || line[0] == '\0') {
    return;
  }

  if (strcmp(line, "help") == 0) {
    printHelp();
    return;
  }
  if (strcmp(line, "state") == 0) {
    printState("state");
    return;
  }
  if (strcmp(line, "dataset") == 0) {
    printDataset();
    return;
  }
  if (strcmp(line, "demo-dataset") == 0) {
    otOperationalDataset dataset = {};
    Nrf54ThreadExperimental::buildDemoDataset(&dataset);
    Serial.print("thread_cmd demo_dataset=");
    Serial.println(gThread.setActiveDataset(dataset) ? 1 : 0);
    Serial.println("thread_cmd note=process_loop_will_apply_dataset");
    printState("demo-dataset");
    return;
  }
  if (strcmp(line, "router") == 0) {
    Serial.print("thread_cmd router_request=");
    Serial.println(gThread.requestRouterRole() ? 1 : 0);
    printState("router");
    return;
  }
  if (strcmp(line, "restart") == 0) {
    Serial.print("thread_cmd restart=");
    Serial.println(gThread.restart(false) ? 1 : 0);
    printState("restart");
    return;
  }
  if (strcmp(line, "wipe-settings") == 0) {
    Serial.print("thread_cmd settings_wiped=");
    Serial.println(gThread.wipePersistentSettings() ? 1 : 0);
    Serial.println("thread_cmd note=reboot_or_reapply_dataset_before_attach");
    printState("wipe-settings");
    return;
  }
  if (strncmp(line, "dataset-hex ", 12) == 0) {
    const bool importOk = gThread.setActiveDatasetHex(line + 12);
    const bool restartOk = importOk && gThread.restart(false);
    Serial.print("thread_cmd dataset_import=");
    Serial.println(importOk ? 1 : 0);
    Serial.print("thread_cmd dataset_restart=");
    Serial.println(restartOk ? 1 : 0);
    printState("dataset-hex");
    return;
  }

  Serial.print("thread_cmd unknown=");
  Serial.println(line);
  printHelp();
}

void pollSerial() {
  while (Serial.available() > 0) {
    const int raw = Serial.read();
    if (raw < 0) {
      return;
    }

    const char c = static_cast<char>(raw);
    if (c == '\r' || c == '\n') {
      g_lineBuffer[g_lineLength] = '\0';
      handleLine(g_lineBuffer);
      g_lineLength = 0U;
      memset(g_lineBuffer, 0, sizeof(g_lineBuffer));
      continue;
    }

    if (g_lineLength + 1U < sizeof(g_lineBuffer)) {
      g_lineBuffer[g_lineLength++] = c;
      g_lineBuffer[g_lineLength] = '\0';
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  const bool beginOk = gThread.begin(false);
  Serial.print("thread_cmd begin=");
  Serial.println(beginOk ? 1 : 0);
  printState("boot");
  printHelp();
}

void loop() {
  gThread.process();
  pollSerial();

  const Nrf54ThreadExperimental::Role currentRole = gThread.role();
  if (currentRole != g_lastRole) {
    g_lastRole = currentRole;
    printState("role-change");
  }

  if ((millis() - g_lastHeartbeatMs) >= kHeartbeatMs) {
    g_lastHeartbeatMs = millis();
    printState("heartbeat");
  }
}
