#include "nrf54l15_vpr.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint32_t kTickerPeriodTicks = 100000U;
constexpr uint32_t kTickerStep = 5U;
constexpr uint32_t kHibernateSettlePollMs = 20U;
constexpr uint32_t kHibernateSettleTimeoutMs = 1200U;
constexpr uint32_t kWakeSettleMs = 1200U;
constexpr uint32_t kProbeSummaryMagic = 0x5650574BUL;
constexpr uint32_t kProbeSummaryVersion = 1U;

enum ProbeStage : uint32_t {
  kStageIdle = 0U,
  kStageBoot = 0x10U,
  kStagePrime = 0x11U,
  kStageHibernate = 0x12U,
  kStageSummary = 0x13U,
  kStageWakeBegin = 0x20U,
  kStageWakeCapsFail = 0x21U,
  kStageWakeInfoFail = 0x22U,
  kStageWakeTickerFail = 0x23U,
  kStageWakeOk = 0x24U,
};

struct VprHibernateWakeSummary {
  uint32_t magic;
  uint32_t version;
  uint32_t bootCount;
  uint32_t runCount;
  uint32_t completed;
  uint32_t lastStage;
  uint32_t probeOk;
  uint32_t wakeOk;
  uint32_t restoredFlag;
  uint32_t contextRestoreEnabled;
  uint32_t transportStatus;
  uint32_t infoFlags;
  uint32_t countBeforeHibernate;
  uint32_t countAfterWake;
  uint32_t savedChecksum;
  uint32_t savedNonZeroBytes;
  uint32_t heartbeatBeforeWake;
  uint32_t heartbeatAfterWake;
  uint32_t lastOpcodeAfterWake;
  uint32_t lastErrorAfterWake;
  uint32_t rawHostSeq;
  uint32_t rawHostFlags;
  uint32_t rawHostLen;
  uint32_t rawVprSeq;
  uint32_t rawVprFlags;
  uint32_t rawVprLen;
  uint32_t rawVprStatus;
  uint32_t rawVprLastOpcode;
  uint32_t rawVprDataWord0;
  uint32_t rawVprDataWord1;
};

__attribute__((section(".noinit"))) static VprHibernateWakeSummary g_probeSummary;

VprSharedTransportStream g_vpr;
VprControllerServiceHost g_service(&g_vpr);
VprControllerServiceCapabilities g_capsBefore{};
VprControllerServiceCapabilities g_capsAfter{};
VprControllerServiceInfo g_infoAfter{};
VprTickerState g_tickerBefore{};
VprTickerState g_tickerAfter{};

uint32_t g_savedChecksum = 0U;
uint32_t g_savedNonZeroBytes = 0U;
uint32_t g_countBeforeHibernate = 0U;
uint32_t g_countAfterWake = 0U;
uint32_t g_heartbeatBeforeWake = 0U;
uint32_t g_heartbeatAfterWake = 0U;
uint16_t g_lastOpcodeAfterWake = 0U;
uint32_t g_lastErrorAfterWake = 0U;
uint32_t g_rawHostSeq = 0U;
uint32_t g_rawHostFlags = 0U;
uint32_t g_rawHostLen = 0U;
uint32_t g_rawVprSeq = 0U;
uint32_t g_rawVprFlags = 0U;
uint32_t g_rawVprLen = 0U;
uint32_t g_rawVprStatus = 0U;
uint32_t g_rawVprLastOpcode = 0U;
uint8_t g_rawVprDataPrefix[8] = {};
bool g_lastProbeOk = false;
bool g_lastWakeOk = false;

void initializeProbeSummary() {
  if (g_probeSummary.magic != kProbeSummaryMagic ||
      g_probeSummary.version != kProbeSummaryVersion) {
    memset(&g_probeSummary, 0, sizeof(g_probeSummary));
    g_probeSummary.magic = kProbeSummaryMagic;
    g_probeSummary.version = kProbeSummaryVersion;
  }
  ++g_probeSummary.bootCount;
}

void captureRawTransportState() {
  volatile Nrf54l15VprTransportHostShared* host = nrf54l15_vpr_transport_host_shared();
  volatile Nrf54l15VprTransportVprShared* vpr = nrf54l15_vpr_transport_vpr_shared();
  g_rawHostSeq = host->hostSeq;
  g_rawHostFlags = host->hostFlags;
  g_rawHostLen = host->hostLen;
  g_rawVprSeq = vpr->vprSeq;
  g_rawVprFlags = vpr->vprFlags;
  g_rawVprLen = vpr->vprLen;
  g_rawVprStatus = vpr->status;
  g_rawVprLastOpcode = vpr->lastOpcode;
  for (size_t i = 0; i < sizeof(g_rawVprDataPrefix); ++i) {
    g_rawVprDataPrefix[i] = vpr->vprData[i];
  }
}

uint32_t fnv1a32Bytes(const uint8_t* data, size_t len) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint32_t>(data[i]);
    hash *= 16777619UL;
  }
  return hash;
}

bool summarizeSavedContext() {
  uint8_t buffer[NRF54L15_VPR_CONTEXT_SAVE_SIZE];
  if (!VprControl::readSavedContext(buffer, sizeof(buffer), 0U)) {
    return false;
  }
  g_savedChecksum = fnv1a32Bytes(buffer, sizeof(buffer));
  g_savedNonZeroBytes = 0U;
  for (size_t i = 0; i < sizeof(buffer); ++i) {
    if (buffer[i] != 0U) {
      ++g_savedNonZeroBytes;
    }
  }
  return true;
}

void syncProbeSummary(bool completed = false) {
  g_probeSummary.completed = completed ? 1U : 0U;
  g_probeSummary.probeOk = g_lastProbeOk ? 1U : 0U;
  g_probeSummary.wakeOk = g_lastWakeOk ? 1U : 0U;
  g_probeSummary.restoredFlag =
      ((g_infoAfter.transportFlags &
        VprControllerServiceHost::kTransportFlagRestoredFromHibernate) != 0U)
          ? 1U
          : 0U;
  g_probeSummary.contextRestoreEnabled = VprControl::contextRestoreEnabled() ? 1U : 0U;
  g_probeSummary.transportStatus = g_vpr.transportStatus();
  g_probeSummary.infoFlags = g_infoAfter.transportFlags;
  g_probeSummary.countBeforeHibernate = g_countBeforeHibernate;
  g_probeSummary.countAfterWake = g_countAfterWake;
  g_probeSummary.savedChecksum = g_savedChecksum;
  g_probeSummary.savedNonZeroBytes = g_savedNonZeroBytes;
  g_probeSummary.heartbeatBeforeWake = g_heartbeatBeforeWake;
  g_probeSummary.heartbeatAfterWake = g_heartbeatAfterWake;
  g_probeSummary.lastOpcodeAfterWake = g_lastOpcodeAfterWake;
  g_probeSummary.lastErrorAfterWake = g_lastErrorAfterWake;
  g_probeSummary.rawHostSeq = g_rawHostSeq;
  g_probeSummary.rawHostFlags = g_rawHostFlags;
  g_probeSummary.rawHostLen = g_rawHostLen;
  g_probeSummary.rawVprSeq = g_rawVprSeq;
  g_probeSummary.rawVprFlags = g_rawVprFlags;
  g_probeSummary.rawVprLen = g_rawVprLen;
  g_probeSummary.rawVprStatus = g_rawVprStatus;
  g_probeSummary.rawVprLastOpcode = g_rawVprLastOpcode;
  g_probeSummary.rawVprDataWord0 =
      static_cast<uint32_t>(g_rawVprDataPrefix[0]) |
      (static_cast<uint32_t>(g_rawVprDataPrefix[1]) << 8U) |
      (static_cast<uint32_t>(g_rawVprDataPrefix[2]) << 16U) |
      (static_cast<uint32_t>(g_rawVprDataPrefix[3]) << 24U);
  g_probeSummary.rawVprDataWord1 =
      static_cast<uint32_t>(g_rawVprDataPrefix[4]) |
      (static_cast<uint32_t>(g_rawVprDataPrefix[5]) << 8U) |
      (static_cast<uint32_t>(g_rawVprDataPrefix[6]) << 16U) |
      (static_cast<uint32_t>(g_rawVprDataPrefix[7]) << 24U);
}

void noteStage(ProbeStage stageId) {
  g_probeSummary.lastStage = static_cast<uint32_t>(stageId);
  syncProbeSummary(false);
}

bool bootService() {
  memset(&g_capsBefore, 0, sizeof(g_capsBefore));
  return g_service.bootDefaultService(true) && g_service.readCapabilities(&g_capsBefore);
}

bool primeTicker() {
  if (!g_service.configureTicker(true, kTickerPeriodTicks, kTickerStep, &g_tickerBefore)) {
    return false;
  }
  delay(kWakeSettleMs);
  if (!g_service.readTickerState(&g_tickerBefore)) {
    return false;
  }
  g_countBeforeHibernate = g_tickerBefore.count;
  return g_countBeforeHibernate > 0U;
}

bool waitForHibernateQuiesce() {
  uint32_t lastHeartbeat = g_vpr.heartbeat();
  uint8_t stableSamples = 0U;
  const uint32_t start = millis();
  while ((millis() - start) < kHibernateSettleTimeoutMs) {
    delay(kHibernateSettlePollMs);
    const uint32_t currentHeartbeat = g_vpr.heartbeat();
    if (currentHeartbeat == lastHeartbeat) {
      if (++stableSamples >= 3U) {
        return true;
      }
    } else {
      stableSamples = 0U;
      lastHeartbeat = currentHeartbeat;
    }
  }
  return false;
}

bool wakeViaServiceCommand() {
  noteStage(kStageWakeBegin);
  g_heartbeatBeforeWake = g_vpr.heartbeat();
  memset(&g_capsAfter, 0, sizeof(g_capsAfter));
  memset(&g_infoAfter, 0, sizeof(g_infoAfter));
  memset(&g_tickerAfter, 0, sizeof(g_tickerAfter));
  const bool capsOk = g_service.readCapabilities(&g_capsAfter);
  if (!capsOk) {
    noteStage(kStageWakeCapsFail);
    g_heartbeatAfterWake = g_vpr.heartbeat();
    g_lastOpcodeAfterWake = g_vpr.lastOpcode();
    g_lastErrorAfterWake = g_vpr.lastError();
    captureRawTransportState();
    syncProbeSummary(false);
    return false;
  }
  const bool infoOk = g_service.readTransportInfo(&g_infoAfter);
  if (!infoOk) {
    noteStage(kStageWakeInfoFail);
    g_heartbeatAfterWake = g_vpr.heartbeat();
    g_lastOpcodeAfterWake = g_vpr.lastOpcode();
    g_lastErrorAfterWake = g_vpr.lastError();
    captureRawTransportState();
    syncProbeSummary(false);
    return false;
  }
  delay(kWakeSettleMs);
  const bool tickerOk = g_service.readTickerState(&g_tickerAfter);
  noteStage(tickerOk ? kStageWakeOk : kStageWakeTickerFail);
  g_heartbeatAfterWake = g_vpr.heartbeat();
  g_lastOpcodeAfterWake = g_vpr.lastOpcode();
  g_lastErrorAfterWake = g_vpr.lastError();
  captureRawTransportState();
  syncProbeSummary(false);
  return tickerOk;
}

bool runProbe() {
  g_lastProbeOk = false;
  g_lastWakeOk = false;
  g_savedChecksum = 0U;
  g_savedNonZeroBytes = 0U;
  g_countBeforeHibernate = 0U;
  g_countAfterWake = 0U;
  g_heartbeatBeforeWake = 0U;
  g_heartbeatAfterWake = 0U;
  g_lastOpcodeAfterWake = 0U;
  g_lastErrorAfterWake = 0U;
  g_rawHostSeq = 0U;
  g_rawHostFlags = 0U;
  g_rawHostLen = 0U;
  g_rawVprSeq = 0U;
  g_rawVprFlags = 0U;
  g_rawVprLen = 0U;
  g_rawVprStatus = 0U;
  g_rawVprLastOpcode = 0U;
  memset(g_rawVprDataPrefix, 0, sizeof(g_rawVprDataPrefix));
  memset(&g_capsAfter, 0, sizeof(g_capsAfter));
  memset(&g_infoAfter, 0, sizeof(g_infoAfter));
  memset(&g_tickerBefore, 0, sizeof(g_tickerBefore));
  memset(&g_tickerAfter, 0, sizeof(g_tickerAfter));
  ++g_probeSummary.runCount;
  syncProbeSummary(false);

  noteStage(kStageBoot);
  if (!bootService()) {
    return false;
  }
  if (!VprControl::clearSavedContext() || !VprControl::enableContextRestore(true)) {
    return false;
  }
  noteStage(kStagePrime);
  if (!primeTicker()) {
    return false;
  }
  noteStage(kStageHibernate);
  if (!g_service.enterHibernate() || !waitForHibernateQuiesce()) {
    return false;
  }
  noteStage(kStageSummary);
  if (!summarizeSavedContext()) {
    return false;
  }

  g_lastWakeOk = wakeViaServiceCommand();
  g_countAfterWake = g_tickerAfter.count;
  g_lastProbeOk =
      g_lastWakeOk &&
      (g_capsAfter.serviceVersionMajor == g_capsBefore.serviceVersionMajor) &&
      (g_capsAfter.serviceVersionMinor == g_capsBefore.serviceVersionMinor) &&
      (g_capsAfter.opMask == g_capsBefore.opMask) &&
      g_tickerAfter.enabled &&
      (g_tickerAfter.periodTicks == kTickerPeriodTicks) &&
      (g_tickerAfter.step == kTickerStep) &&
      (g_countAfterWake >= g_countBeforeHibernate) &&
      (g_savedNonZeroBytes > 0U) &&
      (g_infoAfter.transportStatus == NRF54L15_VPR_TRANSPORT_STATUS_READY);
  syncProbeSummary(true);
  return g_lastProbeOk;
}

void printStatus() {
  Serial.print("probe_ok=");
  Serial.print(g_lastProbeOk ? 1 : 0);
  Serial.print(" wake_ok=");
  Serial.print(g_lastWakeOk ? 1 : 0);
  Serial.print(" running=");
  Serial.print(VprControl::isRunning() ? 1 : 0);
  Serial.print(" restore=");
  Serial.print(VprControl::contextRestoreEnabled() ? 1 : 0);
  Serial.print(" transport_status=");
  Serial.print(g_vpr.transportStatus());
  Serial.print(" info_flags=0x");
  Serial.print(g_infoAfter.transportFlags, HEX);
  Serial.print(" restored=");
  Serial.print((g_infoAfter.transportFlags &
                VprControllerServiceHost::kTransportFlagRestoredFromHibernate) != 0U
                   ? 1
                   : 0);
  Serial.print(" count_before=");
  Serial.print(g_countBeforeHibernate);
  Serial.print(" count_after=");
  Serial.print(g_countAfterWake);
  Serial.print(" heartbeat=");
  Serial.print(g_heartbeatBeforeWake);
  Serial.print("->");
  Serial.print(g_heartbeatAfterWake);
  Serial.print(" last_opcode=0x");
  Serial.print(g_lastOpcodeAfterWake, HEX);
  Serial.print(" last_error=");
  Serial.print(g_lastErrorAfterWake);
  Serial.print(" saved_nonzero=");
  Serial.print(g_savedNonZeroBytes);
  Serial.print(" saved_fnv=0x");
  Serial.println(g_savedChecksum, HEX);
  Serial.print("summary_addr=0x");
  Serial.println(reinterpret_cast<uintptr_t>(&g_probeSummary), HEX);
}

void printHelp() {
  Serial.println("Commands: r run wake probe, s status");
}

}  // namespace

void setup() {
  initializeProbeSummary();
  runProbe();
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("VPR hibernate wake probe");
  Serial.print("summary_addr=0x");
  Serial.println(reinterpret_cast<uintptr_t>(&g_probeSummary), HEX);
  printHelp();
  Serial.print("auto_probe=");
  Serial.println(g_lastProbeOk ? 1 : 0);
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
      Serial.println(runProbe() ? 1 : 0);
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
