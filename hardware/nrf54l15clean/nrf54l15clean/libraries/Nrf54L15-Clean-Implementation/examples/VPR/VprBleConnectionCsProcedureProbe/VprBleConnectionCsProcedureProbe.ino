#include "nrf54l15_vpr.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint16_t kConnHandle = 0x0041U;
constexpr uint8_t kRolePeripheral = 1U;
constexpr uint16_t kIntervalUnits = 24U;
constexpr uint16_t kLatency = 0U;
constexpr uint16_t kSupervisionTimeout = 400U;
constexpr uint8_t kPhy1M = 1U;
constexpr uint8_t kDisconnectReason = 0x13U;
constexpr uint8_t kConfigId = 1U;
constexpr uint8_t kMaxProcedureCount = 3U;
constexpr uint16_t kNominalDistanceQ4 = 7537U;
constexpr uint8_t kCompletedLocalSubeventCount = 2U;
constexpr uint8_t kCompletedPeerSubeventCount = 3U;
constexpr uint8_t kCompletedLocalMode1Count = 1U;
constexpr uint8_t kCompletedPeerMode1Count = 1U;
constexpr uint8_t kCompletedLocalMode2Count = 4U;
constexpr uint8_t kCompletedPeerMode2Count = 4U;
constexpr uint8_t kCompletedLocalStepCount = 5U;
constexpr uint8_t kCompletedPeerStepCount = 5U;
constexpr uint32_t kEventTimeoutMs = 2500U;

VprSharedTransportStream g_vpr;
VprControllerServiceHost g_service(&g_vpr);
VprControllerServiceCapabilities g_caps{};
VprBleConnectionSharedState g_weakShared{};
VprBleConnectionSharedState g_strongShared{};
VprBleConnectionSharedState g_finalShared{};
VprBleCsLinkState g_weakLink{};
VprBleCsLinkState g_strongLink{};
VprBleCsWorkflowState g_weakWorkflow{};
VprBleCsWorkflowState g_strongStart{};
VprBleCsWorkflowState g_strongDone{};
VprBleCsWorkflowState g_finalWorkflow{};
uint32_t g_hostDropCount = 0U;
bool g_lastProbeOk = false;

bool ensureService(bool rebootService) {
  if (rebootService && !g_service.bootDefaultService(true)) {
    return false;
  }
  return g_vpr.isRunning() && g_service.readCapabilities(&g_caps);
}

bool runProbe(bool rebootService) {
  memset(&g_caps, 0, sizeof(g_caps));
  memset(&g_weakShared, 0, sizeof(g_weakShared));
  memset(&g_strongShared, 0, sizeof(g_strongShared));
  memset(&g_finalShared, 0, sizeof(g_finalShared));
  memset(&g_weakLink, 0, sizeof(g_weakLink));
  memset(&g_strongLink, 0, sizeof(g_strongLink));
  memset(&g_weakWorkflow, 0, sizeof(g_weakWorkflow));
  memset(&g_strongStart, 0, sizeof(g_strongStart));
  memset(&g_strongDone, 0, sizeof(g_strongDone));
  memset(&g_finalWorkflow, 0, sizeof(g_finalWorkflow));
  g_hostDropCount = 0U;
  g_lastProbeOk = false;

  bool ok = ensureService(rebootService);
  ok = ok && g_service.configureBleConnection(
                 kConnHandle, kRolePeripheral, false, kIntervalUnits, kLatency,
                 kSupervisionTimeout, kPhy1M, kPhy1M, nullptr);
  ok = ok && g_service.waitBleConnectionSharedState(true, 1U, &g_weakShared,
                                                    kEventTimeoutMs);
  ok = ok && g_service.configureBleCsLink(true, kConnHandle, &g_weakLink);
  ok = ok && g_service.configureBleCsWorkflow(
                 kConfigId, true, true, true, true, true, kMaxProcedureCount,
                 &g_weakWorkflow);
  ok = ok && g_service.readBleConnectionSharedState(&g_weakShared);
  ok = ok && g_service.disconnectBleConnection(kConnHandle, kDisconnectReason, nullptr);
  ok = ok &&
       g_service.waitBleConnectionSharedState(false, 2U, nullptr, kEventTimeoutMs);

  ok = ok && g_service.configureBleConnection(
                 kConnHandle, kRolePeripheral, true, kIntervalUnits, kLatency,
                 kSupervisionTimeout, kPhy1M, kPhy1M, nullptr);
  ok = ok && g_service.waitBleConnectionSharedState(true, 3U, nullptr,
                                                    kEventTimeoutMs);
  ok = ok && g_service.configureBleCsLink(true, kConnHandle, &g_strongLink);
  ok = ok && g_service.configureBleCsWorkflow(
                 kConfigId, true, true, true, true, true, kMaxProcedureCount,
                 &g_strongStart);
  ok = ok && g_service.readBleConnectionSharedState(&g_strongShared);
  ok = ok &&
       g_service.waitBleCsWorkflowCompleted(kMaxProcedureCount, &g_strongDone,
                                            kEventTimeoutMs);
  ok = ok && g_service.disconnectBleConnection(kConnHandle, kDisconnectReason, nullptr);
  ok = ok &&
       g_service.waitBleConnectionSharedState(false, 4U, &g_finalShared,
                                              kEventTimeoutMs);
  ok = ok && g_service.readBleCsWorkflowState(&g_finalWorkflow);

  g_hostDropCount = g_service.pendingBleConnectionEventDropCount();
  g_lastProbeOk =
      ok &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleConnectionConfigure) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleConnectionReadState) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleCsLinkConfigure) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleCsWorkflowConfigure) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleCsWorkflowReadState) != 0U &&
      g_weakShared.connected && g_weakShared.csLinkBound &&
      !g_weakShared.csLinkRunnable && g_weakShared.csWorkflowConfigured &&
      !g_weakShared.csWorkflowEnabled && g_weakLink.bound && !g_weakLink.runnable &&
      g_weakWorkflow.linkBound && !g_weakWorkflow.linkRunnable &&
      g_weakWorkflow.configured && !g_weakWorkflow.enabled &&
      !g_weakWorkflow.running && !g_weakWorkflow.completed &&
      g_weakWorkflow.configId == kConfigId &&
      g_weakWorkflow.maxProcedureCount == kMaxProcedureCount &&
      g_strongShared.connected && g_strongShared.csLinkBound &&
      g_strongShared.csLinkRunnable && g_strongShared.csWorkflowConfigured &&
      g_strongShared.csWorkflowEnabled &&
      g_strongLink.bound && g_strongLink.runnable &&
      g_strongStart.linkBound && g_strongStart.linkRunnable &&
      g_strongStart.configured && g_strongStart.enabled &&
      g_strongStart.running && !g_strongStart.completed &&
      g_strongStart.configId == kConfigId &&
      g_strongStart.maxProcedureCount == kMaxProcedureCount &&
      g_strongDone.linkBound && g_strongDone.linkRunnable &&
      g_strongDone.configured && g_strongDone.enabled &&
      !g_strongDone.running && g_strongDone.completed &&
      g_strongDone.completedProcedureCount == kMaxProcedureCount &&
      g_strongDone.completedConfigId == kConfigId &&
      g_strongDone.nominalDistanceQ4 == kNominalDistanceQ4 &&
      g_strongDone.workflowEventCount == kMaxProcedureCount &&
      g_strongDone.completedLocalSubeventCount == kCompletedLocalSubeventCount &&
      g_strongDone.completedPeerSubeventCount == kCompletedPeerSubeventCount &&
      g_strongDone.completedLocalStepCount == kCompletedLocalStepCount &&
      g_strongDone.completedPeerStepCount == kCompletedPeerStepCount &&
      g_strongDone.completedLocalMode1Count == kCompletedLocalMode1Count &&
      g_strongDone.completedPeerMode1Count == kCompletedPeerMode1Count &&
      g_strongDone.completedLocalMode2Count == kCompletedLocalMode2Count &&
      g_strongDone.completedPeerMode2Count == kCompletedPeerMode2Count &&
      !g_finalShared.connected && !g_finalShared.csLinkBound &&
      !g_finalShared.csLinkRunnable && !g_finalShared.csWorkflowConfigured &&
      !g_finalShared.csWorkflowEnabled &&
      !g_finalWorkflow.linkBound && !g_finalWorkflow.linkRunnable &&
      !g_finalWorkflow.configured && !g_finalWorkflow.enabled &&
      !g_finalWorkflow.running && !g_finalWorkflow.completed &&
      g_finalWorkflow.connHandle == 0U && g_hostDropCount == 0U;
  return g_lastProbeOk;
}

void printStatus() {
  Serial.print("probe_ok=");
  Serial.print(g_lastProbeOk ? 1 : 0);
  Serial.print(" svc=");
  Serial.print(g_caps.serviceVersionMajor);
  Serial.print(".");
  Serial.print(g_caps.serviceVersionMinor);
  Serial.print(" opmask=0x");
  Serial.print(g_caps.opMask, HEX);
  Serial.print(" weak=");
  Serial.print(g_weakWorkflow.linkBound ? 1 : 0);
  Serial.print("/");
  Serial.print(g_weakWorkflow.linkRunnable ? 1 : 0);
  Serial.print("/");
  Serial.print(g_weakWorkflow.configured ? 1 : 0);
  Serial.print("/");
  Serial.print(g_weakWorkflow.enabled ? 1 : 0);
  Serial.print("/");
  Serial.print(g_weakWorkflow.running ? 1 : 0);
  Serial.print(" strong=");
  Serial.print(g_strongStart.linkBound ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongStart.linkRunnable ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongStart.configured ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongStart.enabled ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongStart.running ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongDone.completed ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongDone.completedProcedureCount);
  Serial.print("@");
  Serial.print(g_strongDone.nominalDistanceQ4);
  Serial.print(" summary=");
  Serial.print(g_strongDone.completedLocalSubeventCount);
  Serial.print("/");
  Serial.print(g_strongDone.completedPeerSubeventCount);
  Serial.print(" steps=");
  Serial.print(g_strongDone.completedLocalStepCount);
  Serial.print("/");
  Serial.print(g_strongDone.completedPeerStepCount);
  Serial.print(" modes=");
  Serial.print(g_strongDone.completedLocalMode1Count);
  Serial.print("+");
  Serial.print(g_strongDone.completedLocalMode2Count);
  Serial.print("/");
  Serial.print(g_strongDone.completedPeerMode1Count);
  Serial.print("+");
  Serial.print(g_strongDone.completedPeerMode2Count);
  Serial.print(" final=");
  Serial.print(g_finalShared.connected ? 1 : 0);
  Serial.print("/");
  Serial.print(g_finalShared.csLinkBound ? 1 : 0);
  Serial.print("/");
  Serial.print(g_finalShared.csLinkRunnable ? 1 : 0);
  Serial.print("/");
  Serial.print(g_finalShared.csWorkflowConfigured ? 1 : 0);
  Serial.print("/");
  Serial.print(g_finalShared.csWorkflowEnabled ? 1 : 0);
  Serial.print("#");
  Serial.print(g_finalShared.lastDisconnectReason, HEX);
  Serial.print(" wf=");
  Serial.print(g_finalWorkflow.running ? 1 : 0);
  Serial.print("/");
  Serial.print(g_finalWorkflow.completed ? 1 : 0);
  Serial.print("/");
  Serial.print(g_finalWorkflow.workflowEventCount);
  Serial.print(" host_drop=");
  Serial.println(g_hostDropCount);
}

void printHelp() { Serial.println("Commands: r rerun probe, s status"); }

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && (millis() - start) < 1500U) {
  }

  const bool ok = runProbe(true);
  Serial.println("VprBleConnectionCsProcedureProbe ready");
  printStatus();
  Serial.println(ok ? "result=OK" : "result=FAIL");
  printHelp();
}

void loop() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == 'r' || c == 'R') {
      const bool ok = runProbe(true);
      printStatus();
      Serial.println(ok ? "result=OK" : "result=FAIL");
    } else if (c == 's' || c == 'S') {
      printStatus();
    }
  }
  delay(10);
}
