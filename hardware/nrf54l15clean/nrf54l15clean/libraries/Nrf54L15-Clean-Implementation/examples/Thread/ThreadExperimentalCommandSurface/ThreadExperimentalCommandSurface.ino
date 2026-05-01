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
volatile otChangedFlags g_callbackFlags = 0U;
volatile uint8_t g_callbackRoleValue =
    static_cast<uint8_t>(Nrf54ThreadExperimental::Role::kUnknown);

void handleThreadStateChanged(void* context, otChangedFlags flags,
                              Nrf54ThreadExperimental::Role role) {
  (void)context;
  g_callbackFlags |= flags;
  g_callbackRoleValue = static_cast<uint8_t>(role);
}

void printChangedFlags(const char* label, otChangedFlags flags) {
  Serial.print("thread_cmd ");
  Serial.print(label);
  Serial.print("=0x");
  Serial.println(static_cast<unsigned long>(flags), HEX);

  Serial.print("thread_cmd ");
  Serial.print(label);
  Serial.print("_names=");
  if (flags == 0U) {
    Serial.println("none");
    return;
  }

  bool first = true;
  const struct {
    otChangedFlags flag;
    const char* name;
  } kFlagNames[] = {
      {OT_CHANGED_THREAD_ROLE, "role"},
      {OT_CHANGED_THREAD_NETIF_STATE, "netif"},
      {OT_CHANGED_THREAD_RLOC_ADDED, "rloc_added"},
      {OT_CHANGED_THREAD_RLOC_REMOVED, "rloc_removed"},
      {OT_CHANGED_THREAD_ML_ADDR, "ml_addr"},
      {OT_CHANGED_ACTIVE_DATASET, "active_dataset"},
      {OT_CHANGED_PENDING_DATASET, "pending_dataset"},
      {OT_CHANGED_THREAD_PARTITION_ID, "partition"},
      {OT_CHANGED_THREAD_CHANNEL, "channel"},
      {OT_CHANGED_THREAD_PANID, "panid"},
      {OT_CHANGED_THREAD_NETWORK_NAME, "name"},
      {OT_CHANGED_THREAD_EXT_PANID, "extpanid"},
      {OT_CHANGED_THREAD_CHILD_ADDED, "child_added"},
      {OT_CHANGED_THREAD_CHILD_REMOVED, "child_removed"},
      {OT_CHANGED_PARENT_LINK_QUALITY, "parent_lqi"},
  };

  for (size_t i = 0; i < (sizeof(kFlagNames) / sizeof(kFlagNames[0])); ++i) {
    if ((flags & kFlagNames[i].flag) == 0U) {
      continue;
    }
    if (!first) {
      Serial.print(',');
    }
    Serial.print(kFlagNames[i].name);
    first = false;
  }
  Serial.println();
}

void printAttachDiagnostics() {
  Nrf54ThreadExperimental::AttachDiagnostics diagnostics;
  const bool ok = gThread.getAttachDiagnostics(&diagnostics);
  Serial.print("thread_cmd attach_diag_ok=");
  Serial.println(ok ? 1 : 0);
  if (!ok) {
    return;
  }

  Serial.print("thread_cmd attach_duration_ms=");
  Serial.println(diagnostics.currentAttachDurationMs);
  Serial.print("thread_cmd attach_attempts=");
  Serial.println(diagnostics.attachAttempts);
  Serial.print("thread_cmd better_partition_attach_attempts=");
  Serial.println(diagnostics.betterPartitionAttachAttempts);
  Serial.print("thread_cmd better_parent_attach_attempts=");
  Serial.println(diagnostics.betterParentAttachAttempts);
  Serial.print("thread_cmd parent_changes=");
  Serial.println(diagnostics.parentChanges);
}

void printAttachDebugState() {
  Nrf54ThreadExperimental::AttachDebugState debugState;
  const bool ok = gThread.getAttachDebugState(&debugState);
  Serial.print("thread_cmd attach_debug_ok=");
  Serial.println(ok ? 1 : 0);
  if (!ok) {
    return;
  }

  Serial.print("thread_cmd attach_debug_valid=");
  Serial.println(debugState.valid ? 1 : 0);
  Serial.print("thread_cmd attach_in_progress=");
  Serial.println(debugState.attachInProgress ? 1 : 0);
  Serial.print("thread_cmd attach_timer_running=");
  Serial.println(debugState.attachTimerRunning ? 1 : 0);
  Serial.print("thread_cmd attach_received_parent_response=");
  Serial.println(debugState.receivedResponseFromParent ? 1 : 0);
  Serial.print("thread_cmd attach_state=");
  Serial.println(debugState.attachStateName);
  Serial.print("thread_cmd attach_mode=");
  Serial.println(debugState.attachModeName);
  Serial.print("thread_cmd reattach_mode=");
  Serial.println(debugState.reattachModeName);
  Serial.print("thread_cmd parent_candidate_state=");
  Serial.println(debugState.parentCandidateStateName);
  Serial.print("thread_cmd parent_request_counter=");
  Serial.println(debugState.parentRequestCounter);
  Serial.print("thread_cmd child_id_requests_remaining=");
  Serial.println(debugState.childIdRequestsRemaining);
  Serial.print("thread_cmd attach_counter=");
  Serial.println(debugState.attachCounter);
  Serial.print("thread_cmd attach_timer_remaining_ms=");
  Serial.println(debugState.attachTimerRemainingMs);
  Serial.print("thread_cmd parent_candidate_rloc16=0x");
  Serial.println(debugState.parentCandidateRloc16, HEX);
}

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
  printChangedFlags("last_flags", gThread.lastChangedFlags());
  printChangedFlags("pending_flags", gThread.pendingChangedFlags());
  printAttachDiagnostics();
  printAttachDebugState();
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
  Serial.println("thread_cmd   stats");
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
  if (strcmp(line, "stats") == 0) {
    printAttachDiagnostics();
    printChangedFlags("last_flags", gThread.lastChangedFlags());
    printChangedFlags("pending_flags", gThread.pendingChangedFlags());
    printChangedFlags("callback_flags", g_callbackFlags);
    Serial.print("thread_cmd callback_role=");
    Serial.println(Nrf54ThreadExperimental::roleName(
        static_cast<Nrf54ThreadExperimental::Role>(g_callbackRoleValue)));
    printAttachDebugState();
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
  gThread.setStateChangedCallback(handleThreadStateChanged, nullptr);
  Serial.print("thread_cmd begin=");
  Serial.println(beginOk ? 1 : 0);
  printState("boot");
  printHelp();
}

void loop() {
  gThread.process();
  pollSerial();

  if (g_callbackFlags != 0U) {
    const otChangedFlags callbackFlags = g_callbackFlags;
    g_callbackFlags = 0U;
    printChangedFlags("callback_flags", callbackFlags);
    Serial.print("thread_cmd callback_role=");
    Serial.println(Nrf54ThreadExperimental::roleName(
        static_cast<Nrf54ThreadExperimental::Role>(g_callbackRoleValue)));
  }

  const otChangedFlags changedFlags = gThread.consumePendingChangedFlags();
  if (changedFlags != 0U) {
    printChangedFlags("event_flags", changedFlags);
    printState("state-change");
  }

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
