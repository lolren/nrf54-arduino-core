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
constexpr uint32_t kEventTimeoutMs = 2500U;

VprSharedTransportStream g_vpr;
VprControllerServiceHost g_service(&g_vpr);
VprControllerServiceCapabilities g_caps{};
VprBleConnectionState g_weakState{};
VprBleConnectionState g_strongState{};
VprBleConnectionSharedState g_weakShared{};
VprBleConnectionSharedState g_strongShared{};
VprBleConnectionSharedState g_finalShared{};
VprBleCsLinkState g_weakLink{};
VprBleCsLinkState g_strongLink{};
VprBleCsWorkflowState g_weakWorkflow{};
VprBleCsWorkflowState g_strongWorkflow{};
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
  g_lastProbeOk = false;
  memset(&g_caps, 0, sizeof(g_caps));
  memset(&g_weakState, 0, sizeof(g_weakState));
  memset(&g_strongState, 0, sizeof(g_strongState));
  memset(&g_weakShared, 0, sizeof(g_weakShared));
  memset(&g_strongShared, 0, sizeof(g_strongShared));
  memset(&g_finalShared, 0, sizeof(g_finalShared));
  memset(&g_weakLink, 0, sizeof(g_weakLink));
  memset(&g_strongLink, 0, sizeof(g_strongLink));
  memset(&g_weakWorkflow, 0, sizeof(g_weakWorkflow));
  memset(&g_strongWorkflow, 0, sizeof(g_strongWorkflow));
  memset(&g_finalWorkflow, 0, sizeof(g_finalWorkflow));
  g_hostDropCount = 0U;

  bool ok = ensureService(rebootService);
  ok = ok && g_service.configureBleConnection(
                 kConnHandle, kRolePeripheral, false, kIntervalUnits, kLatency,
                 kSupervisionTimeout, kPhy1M, kPhy1M, &g_weakState);
  ok = ok &&
       g_service.waitBleConnectionSharedState(true, 1U, &g_weakShared, kEventTimeoutMs);
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
                 kSupervisionTimeout, kPhy1M, kPhy1M, &g_strongState);
  ok = ok &&
       g_service.waitBleConnectionSharedState(true, 3U, &g_strongShared, kEventTimeoutMs);
  ok = ok && g_service.configureBleCsLink(true, kConnHandle, &g_strongLink);
  ok = ok && g_service.configureBleCsWorkflow(
                 kConfigId, true, true, true, true, true, kMaxProcedureCount,
                 &g_strongWorkflow);
  ok = ok && g_service.readBleConnectionSharedState(&g_strongShared);
  ok = ok && g_service.disconnectBleConnection(kConnHandle, kDisconnectReason, nullptr);
  ok = ok &&
       g_service.waitBleConnectionSharedState(false, 4U, &g_finalShared, kEventTimeoutMs);
  ok = ok && g_service.readBleCsWorkflowState(&g_finalWorkflow);

  g_hostDropCount = g_service.pendingBleConnectionEventDropCount();
  g_lastProbeOk =
      ok &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleConnectionConfigure) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleConnectionReadState) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleConnectionEvent) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleCsLinkConfigure) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleCsLinkReadState) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleCsWorkflowConfigure) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleCsWorkflowReadState) != 0U &&
      g_weakShared.connected && g_weakShared.csLinkBound &&
      g_weakShared.csLinkRunnable == false &&
      g_weakShared.csWorkflowConfigured && !g_weakShared.csWorkflowEnabled &&
      g_weakWorkflow.linkBound && !g_weakWorkflow.linkRunnable &&
      g_weakWorkflow.configured && !g_weakWorkflow.enabled &&
      !g_weakWorkflow.encrypted && g_weakWorkflow.connHandle == kConnHandle &&
      g_weakWorkflow.configId == kConfigId &&
      g_weakWorkflow.maxProcedureCount == kMaxProcedureCount &&
      g_strongShared.connected && g_strongShared.csLinkBound &&
      g_strongShared.csLinkRunnable && g_strongShared.csWorkflowConfigured &&
      g_strongShared.csWorkflowEnabled &&
      g_strongWorkflow.linkBound && g_strongWorkflow.linkRunnable &&
      g_strongWorkflow.configured && g_strongWorkflow.enabled &&
      g_strongWorkflow.encrypted && g_strongWorkflow.connHandle == kConnHandle &&
      g_strongWorkflow.configId == kConfigId &&
      g_strongWorkflow.maxProcedureCount == kMaxProcedureCount &&
      !g_finalShared.connected && !g_finalShared.csLinkBound &&
      !g_finalShared.csLinkRunnable && !g_finalShared.csWorkflowConfigured &&
      !g_finalShared.csWorkflowEnabled &&
      !g_finalWorkflow.linkBound && !g_finalWorkflow.linkRunnable &&
      !g_finalWorkflow.configured && !g_finalWorkflow.enabled &&
      !g_finalWorkflow.connected && g_finalWorkflow.connHandle == 0U &&
      g_hostDropCount == 0U;
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
  Serial.print(g_weakShared.connected ? 1 : 0);
  Serial.print("/");
  Serial.print(g_weakShared.csLinkBound ? 1 : 0);
  Serial.print("/");
  Serial.print(g_weakShared.csLinkRunnable ? 1 : 0);
  Serial.print("/");
  Serial.print(g_weakShared.csWorkflowConfigured ? 1 : 0);
  Serial.print("/");
  Serial.print(g_weakShared.csWorkflowEnabled ? 1 : 0);
  Serial.print(" wf=");
  Serial.print(g_weakWorkflow.linkBound ? 1 : 0);
  Serial.print("/");
  Serial.print(g_weakWorkflow.linkRunnable ? 1 : 0);
  Serial.print("/");
  Serial.print(g_weakWorkflow.configured ? 1 : 0);
  Serial.print("/");
  Serial.print(g_weakWorkflow.enabled ? 1 : 0);
  Serial.print("@0x");
  Serial.print(g_weakWorkflow.connHandle, HEX);
  Serial.print("#");
  Serial.print(g_weakWorkflow.configId);
  Serial.print("/");
  Serial.print(g_weakWorkflow.maxProcedureCount);
  Serial.print(" strong=");
  Serial.print(g_strongShared.connected ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongShared.csLinkBound ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongShared.csLinkRunnable ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongShared.csWorkflowConfigured ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongShared.csWorkflowEnabled ? 1 : 0);
  Serial.print(" wf=");
  Serial.print(g_strongWorkflow.linkBound ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongWorkflow.linkRunnable ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongWorkflow.configured ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongWorkflow.enabled ? 1 : 0);
  Serial.print("@0x");
  Serial.print(g_strongWorkflow.connHandle, HEX);
  Serial.print("#");
  Serial.print(g_strongWorkflow.configId);
  Serial.print("/");
  Serial.print(g_strongWorkflow.maxProcedureCount);
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
  Serial.print(g_finalWorkflow.linkBound ? 1 : 0);
  Serial.print("/");
  Serial.print(g_finalWorkflow.linkRunnable ? 1 : 0);
  Serial.print("/");
  Serial.print(g_finalWorkflow.configured ? 1 : 0);
  Serial.print("/");
  Serial.print(g_finalWorkflow.enabled ? 1 : 0);
  Serial.print("@0x");
  Serial.print(g_finalWorkflow.connHandle, HEX);
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
  Serial.println("VprBleConnectionCsWorkflowProbe ready");
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
