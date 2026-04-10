#include "nrf54l15_vpr.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint32_t kTickerPeriodTicks = 100000U;
constexpr uint32_t kTickerStep = 5U;
constexpr uint32_t kHibernateSettlePollMs = 20U;
constexpr uint32_t kHibernateSettleTimeoutMs = 1200U;
constexpr uint32_t kResumeSettleMs = 1500U;
constexpr uint32_t kProbeSummaryMagic = 0x56505253UL;
constexpr uint32_t kProbeSummaryVersion = 1U;

enum ProbeStage : uint32_t {
  kStageIdle = 0U,
  kStageSetup = 1U,
  kStageProbeBoot = 0x10U,
  kStageProbeClearCtx = 0x11U,
  kStageProbePrime = 0x12U,
  kStageProbeHibernate = 0x13U,
  kStageProbeSummary = 0x14U,
  kStageProbeSkipWake = 0x15U,
  kStageProbeTryResume = 0x16U,
  kStageResumeBegin = 0x20U,
  kStageResumeClearEvent = 0x21U,
  kStageResumeRestoreOff = 0x22U,
  kStageResumeTransportFail = 0x23U,
  kStageResumeRtPeriph = 0x24U,
  kStageResumeRtPeriphOk = 0x25U,
  kStageResumeClearSigSkip = 0x26U,
  kStageResumeClearDbgOk = 0x27U,
  kStageResumeHartResetFail = 0x28U,
  kStageResumeHartResetOk = 0x29U,
  kStageResumeNotRunningPreCpurun = 0x2AU,
  kStageResumeCpurunOk = 0x2BU,
  kStageResumeWaitFail = 0x2CU,
  kStageResumeTransportOk = 0x2DU,
  kStageResumeCapsOk = 0x2EU,
  kStageResumeCapsFail = 0x2FU,
  kStageResumeInfoOk = 0x30U,
  kStageResumeInfoFail = 0x31U,
  kStageResumeTickerOk = 0x32U,
  kStageResumeTickerFail = 0x33U,
  kStageWakeBegin = 0x40U,
  kStageWakeCapsOk = 0x41U,
  kStageWakeCapsFail = 0x42U,
  kStageWakeInfoOk = 0x43U,
  kStageWakeInfoFail = 0x44U,
  kStageWakeTickerOk = 0x45U,
  kStageWakeTickerFail = 0x46U,
};

struct VprResumeProbeSummary {
  uint32_t magic;
  uint32_t version;
  uint32_t bootCount;
  uint32_t runCount;
  uint32_t completed;
  uint32_t lastStage;
  uint32_t probeOk;
  uint32_t resumeOk;
  uint32_t recoveryOk;
  uint32_t wakeOnCommandOk;
  uint32_t restoredFlag;
  uint32_t contextRestoreEnabled;
  uint32_t sleepControl;
  uint32_t memconfPower1Ret;
  uint32_t memconfPower0Ret2;
  uint32_t transportStatus;
  uint32_t infoFlags;
  uint32_t countBeforeHibernate;
  uint32_t countAfterResume;
  uint32_t savedChecksum;
  uint32_t savedNonZeroBytes;
  uint32_t heartbeatBeforeResume;
  uint32_t heartbeatAfterResume;
  uint32_t rawHeartbeat;
  uint32_t lastOpcodeAfterResume;
  uint32_t lastErrorAfterResume;
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

__attribute__((section(".noinit"))) static VprResumeProbeSummary g_probeSummary;

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
uint32_t g_countAfterResume = 0U;
uint32_t g_transportHeartbeatBeforeResume = 0U;
uint32_t g_transportHeartbeatAfterResume = 0U;
uint16_t g_lastOpcodeAfterResume = 0U;
uint32_t g_lastErrorAfterResume = 0U;
bool g_lastProbeOk = false;
bool g_lastResumeOk = false;
bool g_lastRecoveryOk = false;
bool g_lastWakeOnCommandOk = false;
uint32_t g_rawHostSeq = 0U;
uint32_t g_rawHostFlags = 0U;
uint32_t g_rawHostLen = 0U;
uint32_t g_rawVprSeq = 0U;
uint32_t g_rawVprFlags = 0U;
uint32_t g_rawVprLen = 0U;
uint32_t g_rawVprStatus = 0U;
uint32_t g_rawVprLastOpcode = 0U;
uint8_t g_rawVprDataPrefix[8] = {};
bool g_stageSerialEnabled = false;

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
  g_probeSummary.resumeOk = g_lastResumeOk ? 1U : 0U;
  g_probeSummary.recoveryOk = g_lastRecoveryOk ? 1U : 0U;
  g_probeSummary.wakeOnCommandOk = g_lastWakeOnCommandOk ? 1U : 0U;
  g_probeSummary.restoredFlag =
      ((g_infoAfter.transportFlags &
        VprControllerServiceHost::kTransportFlagRestoredFromHibernate) != 0U)
          ? 1U
          : 0U;
  g_probeSummary.contextRestoreEnabled = VprControl::contextRestoreEnabled() ? 1U : 0U;
  g_probeSummary.sleepControl = VprControl::rawSleepControl();
  g_probeSummary.memconfPower1Ret = VprControl::rawMemconfPower1Ret();
  g_probeSummary.memconfPower0Ret2 = VprControl::rawMemconfPower0Ret2();
  g_probeSummary.transportStatus = g_vpr.transportStatus();
  g_probeSummary.infoFlags = g_infoAfter.transportFlags;
  g_probeSummary.countBeforeHibernate = g_countBeforeHibernate;
  g_probeSummary.countAfterResume = g_countAfterResume;
  g_probeSummary.savedChecksum = g_savedChecksum;
  g_probeSummary.savedNonZeroBytes = g_savedNonZeroBytes;
  g_probeSummary.heartbeatBeforeResume = g_transportHeartbeatBeforeResume;
  g_probeSummary.heartbeatAfterResume = g_transportHeartbeatAfterResume;
  g_probeSummary.rawHeartbeat = g_vpr.heartbeat();
  g_probeSummary.lastOpcodeAfterResume = g_lastOpcodeAfterResume;
  g_probeSummary.lastErrorAfterResume = g_lastErrorAfterResume;
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

void noteStage(ProbeStage stageId, const __FlashStringHelper* stage) {
  g_probeSummary.lastStage = static_cast<uint32_t>(stageId);
  syncProbeSummary(false);
  if (g_stageSerialEnabled) {
    Serial.print("stage=");
    Serial.println(stage);
    Serial.flush();
  }
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

bool bootService() {
  memset(&g_capsBefore, 0, sizeof(g_capsBefore));
  return g_service.bootDefaultService(true) && g_service.readCapabilities(&g_capsBefore);
}

bool primeTicker() {
  if (!g_service.configureTicker(true, kTickerPeriodTicks, kTickerStep, &g_tickerBefore)) {
    return false;
  }
  delay(kResumeSettleMs);
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

bool resumeService() {
  noteStage(kStageResumeBegin, F("resume.begin"));
  g_transportHeartbeatBeforeResume = g_vpr.heartbeat();
  if (!VprControl::contextRestoreEnabled()) {
    noteStage(kStageResumeRestoreOff, F("resume.restore_off"));
    noteStage(kStageResumeTransportFail, F("resume.transport_fail"));
    g_transportHeartbeatAfterResume = g_vpr.heartbeat();
    g_lastOpcodeAfterResume = g_vpr.lastOpcode();
    g_lastErrorAfterResume = g_vpr.lastError();
    syncProbeSummary(false);
    return false;
  }
  noteStage(kStageResumeClearEvent, F("resume.clear_event"));
  if (!g_vpr.restartAfterHibernateReset()) {
    noteStage(kStageResumeWaitFail, F("resume.wait_fail"));
    g_transportHeartbeatAfterResume = g_vpr.heartbeat();
    g_lastOpcodeAfterResume = g_vpr.lastOpcode();
    g_lastErrorAfterResume = g_vpr.lastError();
    syncProbeSummary(false);
    return false;
  }
  noteStage(kStageResumeTransportOk, F("resume.transport_ok"));
  delay(kResumeSettleMs);
  g_transportHeartbeatAfterResume = g_vpr.heartbeat();
  g_lastOpcodeAfterResume = g_vpr.lastOpcode();
  g_lastErrorAfterResume = g_vpr.lastError();
  const bool capsOk = g_service.readCapabilities(&g_capsAfter);
  noteStage(capsOk ? kStageResumeCapsOk : kStageResumeCapsFail,
            capsOk ? F("resume.caps_ok") : F("resume.caps_fail"));
  captureRawTransportState();
  if (!capsOk) {
    syncProbeSummary(false);
    return false;
  }
  const bool infoOk = g_service.readTransportInfo(&g_infoAfter);
  noteStage(infoOk ? kStageResumeInfoOk : kStageResumeInfoFail,
            infoOk ? F("resume.info_ok") : F("resume.info_fail"));
  if (!infoOk) {
    syncProbeSummary(false);
    return false;
  }
  const bool tickerOk = g_service.readTickerState(&g_tickerAfter);
  noteStage(tickerOk ? kStageResumeTickerOk : kStageResumeTickerFail,
            tickerOk ? F("resume.ticker_ok") : F("resume.ticker_fail"));
  syncProbeSummary(false);
  return tickerOk;
}

bool wakeViaServiceCommand() {
  noteStage(kStageWakeBegin, F("wake.begin"));
  g_transportHeartbeatBeforeResume = g_vpr.heartbeat();
  memset(&g_capsAfter, 0, sizeof(g_capsAfter));
  memset(&g_infoAfter, 0, sizeof(g_infoAfter));
  memset(&g_tickerAfter, 0, sizeof(g_tickerAfter));
  const bool capsOk = g_service.readCapabilities(&g_capsAfter);
  noteStage(capsOk ? kStageWakeCapsOk : kStageWakeCapsFail,
            capsOk ? F("wake.caps_ok") : F("wake.caps_fail"));
  const bool infoOk = capsOk && g_service.readTransportInfo(&g_infoAfter);
  if (capsOk) {
    noteStage(infoOk ? kStageWakeInfoOk : kStageWakeInfoFail,
              infoOk ? F("wake.info_ok") : F("wake.info_fail"));
  }
  const bool tickerOk = infoOk && g_service.readTickerState(&g_tickerAfter);
  if (infoOk) {
    noteStage(tickerOk ? kStageWakeTickerOk : kStageWakeTickerFail,
              tickerOk ? F("wake.ticker_ok") : F("wake.ticker_fail"));
  }
  const bool ok = capsOk && infoOk && tickerOk;
  g_transportHeartbeatAfterResume = g_vpr.heartbeat();
  g_lastOpcodeAfterResume = g_vpr.lastOpcode();
  g_lastErrorAfterResume = g_vpr.lastError();
  captureRawTransportState();
  syncProbeSummary(false);
  return ok;
}

bool recoverServiceAfterFailure() {
  if (!g_vpr.retainedHibernateStatePending()) {
    return false;
  }
  if (!g_service.recoverAfterHibernateFailure()) {
    return false;
  }
  memset(&g_capsAfter, 0, sizeof(g_capsAfter));
  memset(&g_infoAfter, 0, sizeof(g_infoAfter));
  const bool capsOk = g_service.readCapabilities(&g_capsAfter);
  const bool infoOk = capsOk && g_service.readTransportInfo(&g_infoAfter);
  captureRawTransportState();
  syncProbeSummary(false);
  return capsOk && infoOk &&
         g_infoAfter.transportStatus == NRF54L15_VPR_TRANSPORT_STATUS_READY;
}

bool runProbe() {
  g_lastProbeOk = false;
  g_lastResumeOk = false;
  g_lastRecoveryOk = false;
  g_lastWakeOnCommandOk = false;
  g_savedChecksum = 0U;
  g_savedNonZeroBytes = 0U;
  g_countBeforeHibernate = 0U;
  g_countAfterResume = 0U;
  g_transportHeartbeatBeforeResume = 0U;
  g_transportHeartbeatAfterResume = 0U;
  g_lastOpcodeAfterResume = 0U;
  g_lastErrorAfterResume = 0U;
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
  g_probeSummary.completed = 0U;
  g_probeSummary.lastStage = static_cast<uint32_t>(kStageIdle);
  syncProbeSummary(false);

  noteStage(kStageProbeBoot, F("probe.boot"));
  if (!bootService()) {
    syncProbeSummary(false);
    return false;
  }
  noteStage(kStageProbeClearCtx, F("probe.clear_ctx"));
  if (!VprControl::clearSavedContext() || !VprControl::enableContextRestore(true)) {
    syncProbeSummary(false);
    return false;
  }
  noteStage(kStageProbePrime, F("probe.prime"));
  if (!primeTicker()) {
    syncProbeSummary(false);
    return false;
  }
  noteStage(kStageProbeHibernate, F("probe.hibernate"));
  if (!g_service.enterHibernate()) {
    syncProbeSummary(false);
    return false;
  }
  if (!waitForHibernateQuiesce()) {
    syncProbeSummary(false);
    return false;
  }
  noteStage(kStageProbeSummary, F("probe.summary"));
  if (!summarizeSavedContext()) {
    syncProbeSummary(false);
    return false;
  }

  noteStage(kStageProbeSkipWake, F("probe.skip_wake"));
  g_lastWakeOnCommandOk = false;
  noteStage(kStageProbeTryResume, F("probe.try_resume"));
  g_lastResumeOk = resumeService();
  if (!g_lastResumeOk) {
    g_lastRecoveryOk = recoverServiceAfterFailure();
    syncProbeSummary(false);
    return false;
  }

  g_countAfterResume = g_tickerAfter.count;
  g_lastProbeOk =
      (g_capsAfter.serviceVersionMajor == g_capsBefore.serviceVersionMajor) &&
      (g_capsAfter.serviceVersionMinor == g_capsBefore.serviceVersionMinor) &&
      (g_capsAfter.opMask == g_capsBefore.opMask) &&
      g_tickerAfter.enabled &&
      (g_tickerAfter.periodTicks == kTickerPeriodTicks) &&
      (g_tickerAfter.step == kTickerStep) &&
      (g_countAfterResume >= g_countBeforeHibernate) &&
      (g_savedNonZeroBytes > 0U) &&
      (g_infoAfter.transportStatus == NRF54L15_VPR_TRANSPORT_STATUS_READY);
  syncProbeSummary(true);
  return g_lastProbeOk;
}

void printStatus() {
  Serial.print("probe_ok=");
  Serial.print(g_lastProbeOk ? 1 : 0);
  Serial.print(" resume_ok=");
  Serial.print(g_lastResumeOk ? 1 : 0);
  Serial.print(" recovery_ok=");
  Serial.print(g_lastRecoveryOk ? 1 : 0);
  Serial.print(" wake_cmd_ok=");
  Serial.print(g_lastWakeOnCommandOk ? 1 : 0);
  Serial.print(" running=");
  Serial.print(VprControl::isRunning() ? 1 : 0);
  Serial.print(" restore=");
  Serial.print(VprControl::contextRestoreEnabled() ? 1 : 0);
  Serial.print(" sleepctrl=0x");
  Serial.print(VprControl::rawSleepControl(), HEX);
  Serial.print(" transport_status=");
  Serial.print(g_vpr.transportStatus());
  Serial.print(" info_flags=0x");
  Serial.print(g_infoAfter.transportFlags, HEX);
  Serial.print(" restored=");
  Serial.print((g_infoAfter.transportFlags & VprControllerServiceHost::kTransportFlagRestoredFromHibernate) != 0U ? 1 : 0);
  Serial.print(" raw_hb=");
  Serial.print(g_vpr.heartbeat());
  Serial.print(" hb_resume=");
  Serial.print(g_transportHeartbeatBeforeResume);
  Serial.print("->");
  Serial.print(g_transportHeartbeatAfterResume);
  Serial.print(" svc_before=");
  Serial.print(g_capsBefore.serviceVersionMajor);
  Serial.print(".");
  Serial.print(g_capsBefore.serviceVersionMinor);
  Serial.print(" svc_after=");
  Serial.print(g_capsAfter.serviceVersionMajor);
  Serial.print(".");
  Serial.print(g_capsAfter.serviceVersionMinor);
  Serial.print(" opmask=0x");
  Serial.print(g_capsAfter.opMask, HEX);
  Serial.print(" count_before=");
  Serial.print(g_countBeforeHibernate);
  Serial.print(" count_after=");
  Serial.print(g_countAfterResume);
  Serial.print(" heartbeat=");
  Serial.print(g_infoAfter.heartbeat);
  Serial.print(" last_opcode=0x");
  Serial.print(g_lastOpcodeAfterResume, HEX);
  Serial.print(" last_error=");
  Serial.print(g_lastErrorAfterResume);
  Serial.print(" raw_host=");
  Serial.print(g_rawHostSeq);
  Serial.print("/");
  Serial.print(g_rawHostFlags, HEX);
  Serial.print("/");
  Serial.print(g_rawHostLen);
  Serial.print(" raw_vpr=");
  Serial.print(g_rawVprSeq);
  Serial.print("/");
  Serial.print(g_rawVprFlags, HEX);
  Serial.print("/");
  Serial.print(g_rawVprLen);
  Serial.print("/");
  Serial.print(g_rawVprStatus);
  Serial.print(" raw_last=0x");
  Serial.print(g_rawVprLastOpcode, HEX);
  Serial.print(" raw_pfx=");
  for (size_t i = 0; i < sizeof(g_rawVprDataPrefix); ++i) {
    if (i != 0) {
      Serial.print('.');
    }
    if (g_rawVprDataPrefix[i] < 16U) {
      Serial.print('0');
    }
    Serial.print(g_rawVprDataPrefix[i], HEX);
  }
  Serial.print(" saved_nonzero=");
  Serial.print(g_savedNonZeroBytes);
  Serial.print(" saved_fnv=0x");
  Serial.println(g_savedChecksum, HEX);
  Serial.print("summary_addr=0x");
  Serial.print(reinterpret_cast<uintptr_t>(&g_probeSummary), HEX);
  Serial.print(" last_stage=0x");
  Serial.print(g_probeSummary.lastStage, HEX);
  Serial.print(" runs=");
  Serial.print(g_probeSummary.runCount);
  Serial.print(" boots=");
  Serial.println(g_probeSummary.bootCount);
}

void printHelp() {
  Serial.println("Commands: r run hibernate-reset probe, b boot service, t prime ticker, u restart after hibernate reset, w wake via command, c context summary, s status");
}

}  // namespace

void setup() {
  initializeProbeSummary();
  g_probeSummary.lastStage = static_cast<uint32_t>(kStageSetup);
  syncProbeSummary(false);
  g_stageSerialEnabled = false;
  runProbe();
  Serial.begin(115200);
  delay(1200);
  g_stageSerialEnabled = true;
  Serial.println();
  Serial.println("VPR hibernate reset/restart probe");
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
    case 'b':
      Serial.print("boot=");
      Serial.println(bootService() ? 1 : 0);
      printStatus();
      break;
    case 't':
      Serial.print("ticker=");
      Serial.println(primeTicker() ? 1 : 0);
      printStatus();
      break;
    case 'u':
      g_lastResumeOk = resumeService();
      Serial.print("restart=");
      Serial.println(g_lastResumeOk ? 1 : 0);
      g_countAfterResume = g_tickerAfter.count;
      printStatus();
      break;
    case 'w':
      g_lastWakeOnCommandOk = wakeViaServiceCommand();
      Serial.print("wakecmd=");
      Serial.println(g_lastWakeOnCommandOk ? 1 : 0);
      g_countAfterResume = g_tickerAfter.count;
      printStatus();
      break;
    case 'c':
      Serial.print("context=");
      Serial.println(summarizeSavedContext() ? 1 : 0);
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
