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
constexpr uint32_t kProbeSummaryMagic = 0x56424350UL;
constexpr uint32_t kProbeSummaryVersion = 1U;

struct VprBleConnectionProbeSummary {
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
  uint32_t configuredConnected;
  uint32_t configuredHandle;
  uint32_t configuredInterval;
  uint32_t configuredTimeout;
  uint32_t event0Flags;
  uint32_t event0Handle;
  uint32_t event0Reason;
  uint32_t event0Sequence;
  uint32_t state1Connected;
  uint32_t state1Handle;
  uint32_t state1EventCount;
  uint32_t state1DisconnectCount;
  uint32_t shared1Connected;
  uint32_t shared1Handle;
  uint32_t shared1EventCount;
  uint32_t shared1LastFlags;
  uint32_t state2Connected;
  uint32_t state2Handle;
  uint32_t state2EventCount;
  uint32_t state2DisconnectCount;
  uint32_t shared2Connected;
  uint32_t shared2Handle;
  uint32_t shared2EventCount;
  uint32_t shared2LastFlags;
  uint32_t shared2Reason;
  uint32_t event1Flags;
  uint32_t event1Handle;
  uint32_t event1Reason;
  uint32_t event1Sequence;
};

__attribute__((section(".noinit"))) static VprBleConnectionProbeSummary
    g_probeSummary;

VprSharedTransportStream g_vpr;
VprControllerServiceHost g_service(&g_vpr);
VprControllerServiceCapabilities g_caps{};
VprBleConnectionState g_state0{};
VprBleConnectionState g_state1{};
VprBleConnectionState g_state2{};
VprBleConnectionSharedState g_shared1{};
VprBleConnectionSharedState g_shared2{};
VprBleConnectionEvent g_event0{};
VprBleConnectionEvent g_event1{};
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
  g_probeSummary.configuredConnected = g_state0.connected ? 1U : 0U;
  g_probeSummary.configuredHandle = g_state0.connHandle;
  g_probeSummary.configuredInterval = g_state0.intervalUnits;
  g_probeSummary.configuredTimeout = g_state0.supervisionTimeout;
  g_probeSummary.event0Flags = g_event0.flags;
  g_probeSummary.event0Handle = g_event0.connHandle;
  g_probeSummary.event0Reason = g_event0.reason;
  g_probeSummary.event0Sequence = g_event0.sequence;
  g_probeSummary.state1Connected = g_state1.connected ? 1U : 0U;
  g_probeSummary.state1Handle = g_state1.connHandle;
  g_probeSummary.state1EventCount = g_state1.eventCount;
  g_probeSummary.state1DisconnectCount = g_state1.disconnectCount;
  g_probeSummary.shared1Connected = g_shared1.connected ? 1U : 0U;
  g_probeSummary.shared1Handle = g_shared1.connHandle;
  g_probeSummary.shared1EventCount = g_shared1.eventCount;
  g_probeSummary.shared1LastFlags = g_shared1.lastEventFlags;
  g_probeSummary.state2Connected = g_state2.connected ? 1U : 0U;
  g_probeSummary.state2Handle = g_state2.connHandle;
  g_probeSummary.state2EventCount = g_state2.eventCount;
  g_probeSummary.state2DisconnectCount = g_state2.disconnectCount;
  g_probeSummary.shared2Connected = g_shared2.connected ? 1U : 0U;
  g_probeSummary.shared2Handle = g_shared2.connHandle;
  g_probeSummary.shared2EventCount = g_shared2.eventCount;
  g_probeSummary.shared2LastFlags = g_shared2.lastEventFlags;
  g_probeSummary.shared2Reason = g_shared2.lastDisconnectReason;
  g_probeSummary.event1Flags = g_event1.flags;
  g_probeSummary.event1Handle = g_event1.connHandle;
  g_probeSummary.event1Reason = g_event1.reason;
  g_probeSummary.event1Sequence = g_event1.sequence;
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
  memset(&g_state0, 0, sizeof(g_state0));
  memset(&g_state1, 0, sizeof(g_state1));
  memset(&g_state2, 0, sizeof(g_state2));
  memset(&g_shared1, 0, sizeof(g_shared1));
  memset(&g_shared2, 0, sizeof(g_shared2));
  memset(&g_event0, 0, sizeof(g_event0));
  memset(&g_event1, 0, sizeof(g_event1));
  g_hostDropCount = 0U;
  syncProbeSummary(false);

  if (!ensureService(rebootService)) {
    return false;
  }
  if (!g_service.configureBleConnection(kConnHandle, kRolePeripheral, true,
                                        kIntervalUnits, kLatency,
                                        kSupervisionTimeout, kPhy1M, kPhy1M,
                                        &g_state0) ||
      !g_service.waitBleConnectionEvent(&g_event0, kEventTimeoutMs) ||
      !g_service.waitBleConnectionSharedState(true, 1U, &g_shared1,
                                             kEventTimeoutMs) ||
      !g_service.readBleConnectionState(&g_state1) ||
      !g_service.disconnectBleConnection(kConnHandle, kDisconnectReason,
                                         &g_state2) ||
      !g_service.waitBleConnectionSharedState(false, 2U, &g_shared2,
                                             kEventTimeoutMs) ||
      !g_service.waitBleConnectionEvent(&g_event1, kEventTimeoutMs)) {
    return false;
  }

  g_hostDropCount = g_service.pendingBleConnectionEventDropCount();
  g_lastProbeOk =
      (g_caps.opMask & VprControllerServiceHost::kOpBleConnectionConfigure) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleConnectionReadState) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleConnectionEvent) != 0U &&
      g_state0.connected && g_state0.connHandle == kConnHandle &&
      g_state0.role == kRolePeripheral && g_state0.encrypted &&
      g_state0.intervalUnits == kIntervalUnits &&
      g_state0.supervisionTimeout == kSupervisionTimeout &&
      g_event0.flags == 0x01U && g_event0.connHandle == kConnHandle &&
      g_event0.reason == 0U &&
      g_shared1.connected && g_shared1.connHandle == kConnHandle &&
      g_shared1.eventCount >= 1U && g_shared1.lastEventFlags == 0x01U &&
      g_shared1.role == kRolePeripheral && g_shared1.encrypted &&
      g_shared1.intervalUnits == kIntervalUnits &&
      g_shared1.supervisionTimeout == kSupervisionTimeout &&
      g_state1.connected && g_state1.connHandle == kConnHandle &&
      g_state1.eventCount >= 1U && g_state1.disconnectCount == 0U &&
      !g_shared2.connected && g_shared2.connHandle == 0U &&
      g_shared2.eventCount >= 2U && g_shared2.lastEventFlags == 0x02U &&
      g_shared2.lastDisconnectReason == kDisconnectReason &&
      !g_state2.connected && g_state2.connHandle == 0U &&
      g_state2.eventCount >= 1U && g_state2.disconnectCount >= 1U &&
      g_event1.flags == 0x02U && g_event1.connHandle == kConnHandle &&
      g_event1.reason == kDisconnectReason &&
      g_event1.sequence > g_event0.sequence &&
      g_hostDropCount == 0U;
  syncProbeSummary(g_lastProbeOk);
  return g_lastProbeOk;
}

void printStatus() {
  Serial.print("probe_ok=");
  Serial.print(g_lastProbeOk ? 1 : 0);
  Serial.print(" running=");
  Serial.print(g_vpr.isRunning() ? 1 : 0);
  Serial.print(" svc=");
  Serial.print(g_caps.serviceVersionMajor);
  Serial.print(".");
  Serial.print(g_caps.serviceVersionMinor);
  Serial.print(" opmask=0x");
  Serial.print(g_caps.opMask, HEX);
  Serial.print(" cfg=");
  Serial.print(g_state0.connected ? 1 : 0);
  Serial.print("@0x");
  Serial.print(g_state0.connHandle, HEX);
  Serial.print(" state=");
  Serial.print(g_state1.connected ? 1 : 0);
  Serial.print("/");
  Serial.print(g_state2.connected ? 1 : 0);
  Serial.print(" shared=");
  Serial.print(g_shared1.connected ? 1 : 0);
  Serial.print("/");
  Serial.print(g_shared2.connected ? 1 : 0);
  Serial.print("#");
  Serial.print(g_shared2.lastDisconnectReason, HEX);
  Serial.print(" ev0=");
  Serial.print(g_event0.flags, HEX);
  Serial.print("@0x");
  Serial.print(g_event0.connHandle, HEX);
  Serial.print("#");
  Serial.print(g_event0.sequence);
  Serial.print(" ev1=");
  Serial.print(g_event1.flags, HEX);
  Serial.print("@0x");
  Serial.print(g_event1.connHandle, HEX);
  Serial.print("/");
  Serial.print(g_event1.reason, HEX);
  Serial.print("#");
  Serial.print(g_event1.sequence);
  Serial.print(" host_drop=");
  Serial.println(g_hostDropCount);
}

void printHelp() {
  Serial.println("Commands: r rerun probe, s status");
}

}  // namespace

void setup() {
  initializeProbeSummary();
  ++g_probeSummary.runCount;
  (void)runProbe(true);
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("VPR BLE connection-state probe");
  printHelp();
  Serial.print("probe boot=");
  Serial.println(g_lastProbeOk ? 1 : 0);
  Serial.print("summary_addr=0x");
  Serial.println((uintptr_t)&g_probeSummary, HEX);
  printStatus();
}

void loop() {
  if (!Serial.available()) {
    delay(20);
    return;
  }
  const int incoming = Serial.read();
  if (incoming < 0) {
    return;
  }

  switch (static_cast<char>(incoming)) {
    case 'r':
      Serial.print("probe run=");
      Serial.println(runProbe(false) ? 1 : 0);
      printStatus();
      break;
    case 's':
      printStatus();
      break;
    default:
      printHelp();
      break;
  }
}
