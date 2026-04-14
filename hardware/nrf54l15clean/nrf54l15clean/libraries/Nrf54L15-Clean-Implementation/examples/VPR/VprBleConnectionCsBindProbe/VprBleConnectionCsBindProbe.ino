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
constexpr uint32_t kEventTimeoutMs = 2500U;
constexpr uint32_t kProbeSummaryMagic = 0x5642434CUL;  // "VBCL"
constexpr uint32_t kProbeSummaryVersion = 1U;

struct VprBleConnectionCsBindProbeSummary {
  uint32_t magic;
  uint32_t version;
  uint32_t bootCount;
  uint32_t runCount;
  uint32_t completed;
  uint32_t probeOk;
  uint32_t serviceVersionMajor;
  uint32_t serviceVersionMinor;
  uint32_t serviceOpMask;
  uint32_t hostDropCount;
  uint32_t weakConnected;
  uint32_t weakEventCount;
  uint32_t weakSharedConnected;
  uint32_t weakSharedCsBound;
  uint32_t weakSharedCsRunnable;
  uint32_t weakLinkBound;
  uint32_t weakLinkRunnable;
  uint32_t weakLinkEncrypted;
  uint32_t weakLinkHandle;
  uint32_t weakLinkEventCount;
  uint32_t strongConnected;
  uint32_t strongEventCount;
  uint32_t strongSharedConnected;
  uint32_t strongSharedCsBound;
  uint32_t strongSharedCsRunnable;
  uint32_t strongLinkBound;
  uint32_t strongLinkRunnable;
  uint32_t strongLinkEncrypted;
  uint32_t strongLinkHandle;
  uint32_t strongLinkEventCount;
  uint32_t finalSharedConnected;
  uint32_t finalSharedCsBound;
  uint32_t finalSharedCsRunnable;
  uint32_t finalSharedReason;
  uint32_t finalLinkBound;
  uint32_t finalLinkRunnable;
  uint32_t finalLinkConnected;
  uint32_t finalLinkHandle;
  uint32_t finalLinkEventCount;
};

__attribute__((section(".noinit"))) static VprBleConnectionCsBindProbeSummary
    g_probeSummary;

VprSharedTransportStream g_vpr;
VprControllerServiceHost g_service(&g_vpr);
VprControllerServiceCapabilities g_caps{};
VprBleConnectionState g_weakState{};
VprBleConnectionState g_strongState{};
VprBleConnectionSharedState g_weakShared{};
VprBleConnectionSharedState g_strongShared{};
VprBleConnectionSharedState g_finalShared{};
VprBleConnectionEvent g_connectWeakEvent{};
VprBleConnectionEvent g_disconnectWeakEvent{};
VprBleConnectionEvent g_connectStrongEvent{};
VprBleConnectionEvent g_disconnectStrongEvent{};
VprBleCsLinkState g_weakLink{};
VprBleCsLinkState g_strongLink{};
VprBleCsLinkState g_finalLink{};
uint32_t g_hostDropCount = 0U;
bool g_lastProbeOk = false;

void initializeProbeSummary() {
  if (g_probeSummary.magic != kProbeSummaryMagic ||
      g_probeSummary.version != kProbeSummaryVersion) {
    memset(&g_probeSummary, 0, sizeof(g_probeSummary));
    g_probeSummary.magic = kProbeSummaryMagic;
    g_probeSummary.version = kProbeSummaryVersion;
  }
  ++g_probeSummary.bootCount;
}

void syncProbeSummary(bool completed = false) {
  g_probeSummary.completed = completed ? 1U : 0U;
  g_probeSummary.probeOk = g_lastProbeOk ? 1U : 0U;
  g_probeSummary.serviceVersionMajor = g_caps.serviceVersionMajor;
  g_probeSummary.serviceVersionMinor = g_caps.serviceVersionMinor;
  g_probeSummary.serviceOpMask = g_caps.opMask;
  g_probeSummary.hostDropCount = g_hostDropCount;
  g_probeSummary.weakConnected = g_weakState.connected ? 1U : 0U;
  g_probeSummary.weakEventCount = g_weakState.eventCount;
  g_probeSummary.weakSharedConnected = g_weakShared.connected ? 1U : 0U;
  g_probeSummary.weakSharedCsBound = g_weakShared.csLinkBound ? 1U : 0U;
  g_probeSummary.weakSharedCsRunnable = g_weakShared.csLinkRunnable ? 1U : 0U;
  g_probeSummary.weakLinkBound = g_weakLink.bound ? 1U : 0U;
  g_probeSummary.weakLinkRunnable = g_weakLink.runnable ? 1U : 0U;
  g_probeSummary.weakLinkEncrypted = g_weakLink.encrypted ? 1U : 0U;
  g_probeSummary.weakLinkHandle = g_weakLink.connHandle;
  g_probeSummary.weakLinkEventCount = g_weakLink.eventCount;
  g_probeSummary.strongConnected = g_strongState.connected ? 1U : 0U;
  g_probeSummary.strongEventCount = g_strongState.eventCount;
  g_probeSummary.strongSharedConnected = g_strongShared.connected ? 1U : 0U;
  g_probeSummary.strongSharedCsBound = g_strongShared.csLinkBound ? 1U : 0U;
  g_probeSummary.strongSharedCsRunnable = g_strongShared.csLinkRunnable ? 1U : 0U;
  g_probeSummary.strongLinkBound = g_strongLink.bound ? 1U : 0U;
  g_probeSummary.strongLinkRunnable = g_strongLink.runnable ? 1U : 0U;
  g_probeSummary.strongLinkEncrypted = g_strongLink.encrypted ? 1U : 0U;
  g_probeSummary.strongLinkHandle = g_strongLink.connHandle;
  g_probeSummary.strongLinkEventCount = g_strongLink.eventCount;
  g_probeSummary.finalSharedConnected = g_finalShared.connected ? 1U : 0U;
  g_probeSummary.finalSharedCsBound = g_finalShared.csLinkBound ? 1U : 0U;
  g_probeSummary.finalSharedCsRunnable = g_finalShared.csLinkRunnable ? 1U : 0U;
  g_probeSummary.finalSharedReason = g_finalShared.lastDisconnectReason;
  g_probeSummary.finalLinkBound = g_finalLink.bound ? 1U : 0U;
  g_probeSummary.finalLinkRunnable = g_finalLink.runnable ? 1U : 0U;
  g_probeSummary.finalLinkConnected = g_finalLink.connected ? 1U : 0U;
  g_probeSummary.finalLinkHandle = g_finalLink.connHandle;
  g_probeSummary.finalLinkEventCount = g_finalLink.eventCount;
}

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
  memset(&g_connectWeakEvent, 0, sizeof(g_connectWeakEvent));
  memset(&g_disconnectWeakEvent, 0, sizeof(g_disconnectWeakEvent));
  memset(&g_connectStrongEvent, 0, sizeof(g_connectStrongEvent));
  memset(&g_disconnectStrongEvent, 0, sizeof(g_disconnectStrongEvent));
  memset(&g_weakLink, 0, sizeof(g_weakLink));
  memset(&g_strongLink, 0, sizeof(g_strongLink));
  memset(&g_finalLink, 0, sizeof(g_finalLink));
  g_hostDropCount = 0U;
  syncProbeSummary(false);

  bool ok = ensureService(rebootService);
  ok = ok && g_service.configureBleConnection(
                 kConnHandle, kRolePeripheral, false, kIntervalUnits, kLatency,
                 kSupervisionTimeout, kPhy1M, kPhy1M, &g_weakState);
  ok = ok && g_service.waitBleConnectionEvent(&g_connectWeakEvent, kEventTimeoutMs);
  ok = ok && g_service.waitBleConnectionSharedState(true, 1U, &g_weakShared,
                                                    kEventTimeoutMs);
  ok = ok && g_service.configureBleCsLink(true, kConnHandle, &g_weakLink);
  ok = ok && g_service.readBleConnectionSharedState(&g_weakShared);
  ok = ok && g_service.disconnectBleConnection(kConnHandle, kDisconnectReason, nullptr);
  ok = ok && g_service.waitBleConnectionEvent(&g_disconnectWeakEvent, kEventTimeoutMs);
  ok = ok && g_service.waitBleConnectionSharedState(false, 2U, nullptr,
                                                    kEventTimeoutMs);

  ok = ok && g_service.configureBleConnection(
                 kConnHandle, kRolePeripheral, true, kIntervalUnits, kLatency,
                 kSupervisionTimeout, kPhy1M, kPhy1M, &g_strongState);
  ok = ok && g_service.waitBleConnectionEvent(&g_connectStrongEvent, kEventTimeoutMs);
  ok = ok && g_service.waitBleConnectionSharedState(true, 3U, &g_strongShared,
                                                    kEventTimeoutMs);
  ok = ok && g_service.configureBleCsLink(true, kConnHandle, &g_strongLink);
  ok = ok && g_service.readBleConnectionSharedState(&g_strongShared);
  ok = ok && g_service.disconnectBleConnection(kConnHandle, kDisconnectReason, nullptr);
  ok = ok && g_service.waitBleConnectionEvent(&g_disconnectStrongEvent, kEventTimeoutMs);
  ok = ok && g_service.waitBleConnectionSharedState(false, 4U, &g_finalShared,
                                                    kEventTimeoutMs);
  ok = ok && g_service.readBleCsLinkState(&g_finalLink);

  g_hostDropCount = g_service.pendingBleConnectionEventDropCount();
  g_lastProbeOk =
      ok &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleConnectionConfigure) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleConnectionReadState) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleConnectionEvent) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleCsLinkConfigure) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleCsLinkReadState) != 0U &&
      g_weakState.connected && g_weakState.connHandle == kConnHandle &&
      g_weakShared.connected && g_weakShared.connHandle == kConnHandle &&
      g_weakShared.csLinkBound && !g_weakShared.csLinkRunnable &&
      g_weakLink.bound && !g_weakLink.runnable && g_weakLink.connected &&
      !g_weakLink.encrypted && g_weakLink.connHandle == kConnHandle &&
      g_disconnectWeakEvent.flags == 0x02U &&
      g_strongState.connected && g_strongState.connHandle == kConnHandle &&
      g_strongShared.connected && g_strongShared.connHandle == kConnHandle &&
      g_strongShared.csLinkBound && g_strongShared.csLinkRunnable &&
      g_strongLink.bound && g_strongLink.runnable && g_strongLink.connected &&
      g_strongLink.encrypted && g_strongLink.connHandle == kConnHandle &&
      !g_finalShared.connected && !g_finalShared.csLinkBound &&
      !g_finalShared.csLinkRunnable &&
      g_finalShared.lastDisconnectReason == kDisconnectReason &&
      !g_finalLink.bound && !g_finalLink.runnable && !g_finalLink.connected &&
      g_finalLink.connHandle == 0U && g_hostDropCount == 0U;

  syncProbeSummary(g_lastProbeOk);
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
  Serial.print(" link=");
  Serial.print(g_weakLink.bound ? 1 : 0);
  Serial.print("/");
  Serial.print(g_weakLink.runnable ? 1 : 0);
  Serial.print("/");
  Serial.print(g_weakLink.encrypted ? 1 : 0);
  Serial.print("@0x");
  Serial.print(g_weakLink.connHandle, HEX);
  Serial.print(" strong=");
  Serial.print(g_strongShared.connected ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongShared.csLinkBound ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongShared.csLinkRunnable ? 1 : 0);
  Serial.print(" link=");
  Serial.print(g_strongLink.bound ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongLink.runnable ? 1 : 0);
  Serial.print("/");
  Serial.print(g_strongLink.encrypted ? 1 : 0);
  Serial.print("@0x");
  Serial.print(g_strongLink.connHandle, HEX);
  Serial.print(" final=");
  Serial.print(g_finalShared.connected ? 1 : 0);
  Serial.print("/");
  Serial.print(g_finalShared.csLinkBound ? 1 : 0);
  Serial.print("/");
  Serial.print(g_finalShared.csLinkRunnable ? 1 : 0);
  Serial.print("#");
  Serial.print(g_finalShared.lastDisconnectReason, HEX);
  Serial.print(" link=");
  Serial.print(g_finalLink.bound ? 1 : 0);
  Serial.print("/");
  Serial.print(g_finalLink.runnable ? 1 : 0);
  Serial.print("/");
  Serial.print(g_finalLink.connected ? 1 : 0);
  Serial.print("@0x");
  Serial.print(g_finalLink.connHandle, HEX);
  Serial.print(" host_drop=");
  Serial.println(g_hostDropCount);
}

void printHelp() {
  Serial.println("Commands: r rerun probe, s status");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && (millis() - start) < 1500U) {
  }

  initializeProbeSummary();
  ++g_probeSummary.runCount;
  const bool ok = runProbe(true);
  Serial.println("VprBleConnectionCsBindProbe ready");
  printStatus();
  if (!ok) {
    Serial.println("result=FAIL");
  } else {
    Serial.println("result=OK");
  }
  printHelp();
}

void loop() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == 'r' || c == 'R') {
      ++g_probeSummary.runCount;
      const bool ok = runProbe(true);
      printStatus();
      Serial.println(ok ? "result=OK" : "result=FAIL");
    } else if (c == 's' || c == 'S') {
      printStatus();
    }
  }
  delay(10);
}
