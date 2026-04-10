#include "nrf54l15_vpr.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint32_t kPingCookieBase = 0x50000000UL;
constexpr uint32_t kTickerPeriodTicks = 80000U;
constexpr uint32_t kTickerStep = 7U;
constexpr uint8_t kRestartCycles = 5U;
constexpr uint32_t kProbeSummaryMagic = 0x5652504CUL;
constexpr uint32_t kProbeSummaryVersion = 1U;

struct VprRestartProbeSummary {
  uint32_t magic;
  uint32_t version;
  uint32_t bootCount;
  uint32_t runCount;
  uint32_t completed;
  uint32_t probeOk;
  uint32_t cycleCount;
  uint32_t cyclePasses;
  uint32_t cycleIndex;
  uint32_t failureStage;
  uint32_t heartbeat;
  uint32_t echoedCookie;
  uint32_t count0;
  uint32_t count1;
  uint32_t transportStatus;
  uint32_t transportError;
  uint32_t transportFlags;
  uint32_t serviceVersionMajor;
  uint32_t serviceVersionMinor;
  uint32_t serviceOpMask;
  uint32_t serviceMaxInput;
};

__attribute__((section(".noinit"))) static VprRestartProbeSummary g_probeSummary;

VprSharedTransportStream g_vpr;
VprControllerServiceHost g_service(&g_vpr);
VprControllerServiceCapabilities g_caps{};
VprControllerServiceInfo g_info{};
VprTickerState g_ticker{};

uint8_t g_lastCycleCount = 0U;
uint8_t g_lastCyclePasses = 0U;
uint32_t g_lastEchoedCookie = 0U;
uint32_t g_lastHeartbeat = 0U;
uint32_t g_lastCount0 = 0U;
uint32_t g_lastCount1 = 0U;
bool g_lastProbeOk = false;
uint8_t g_lastCycleIndex = 0U;
uint8_t g_lastFailureStage = 0U;
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
  g_probeSummary.cycleCount = g_lastCycleCount;
  g_probeSummary.cyclePasses = g_lastCyclePasses;
  g_probeSummary.cycleIndex = g_lastCycleIndex;
  g_probeSummary.failureStage = g_lastFailureStage;
  g_probeSummary.heartbeat = g_lastHeartbeat;
  g_probeSummary.echoedCookie = g_lastEchoedCookie;
  g_probeSummary.count0 = g_lastCount0;
  g_probeSummary.count1 = g_lastCount1;
  g_probeSummary.transportStatus = g_info.transportStatus;
  g_probeSummary.transportError = g_info.transportError;
  g_probeSummary.transportFlags = g_info.transportFlags;
  g_probeSummary.serviceVersionMajor = g_caps.serviceVersionMajor;
  g_probeSummary.serviceVersionMinor = g_caps.serviceVersionMinor;
  g_probeSummary.serviceOpMask = g_caps.opMask;
  g_probeSummary.serviceMaxInput = g_caps.maxInputLen;
}

void noteStage(uint8_t stage) {
  g_lastFailureStage = stage;
  syncProbeSummary(false);
}

bool probeServiceCycle(uint8_t cycleIndex) {
  g_lastCycleIndex = cycleIndex;
  memset(&g_info, 0, sizeof(g_info));
  memset(&g_caps, 0, sizeof(g_caps));
  memset(&g_ticker, 0, sizeof(g_ticker));
  g_lastEchoedCookie = 0U;
  g_lastHeartbeat = 0U;
  g_lastCount0 = 0U;
  g_lastCount1 = 0U;

  const uint32_t cookie = kPingCookieBase + static_cast<uint32_t>(cycleIndex);
  if (!g_service.probe(cookie, &g_info, &g_lastEchoedCookie, &g_lastHeartbeat) ||
      g_lastEchoedCookie != cookie ||
      !g_service.readCapabilities(&g_caps) ||
      !g_service.configureTicker(true, kTickerPeriodTicks, kTickerStep, &g_ticker)) {
    noteStage(1U);
    return false;
  }

  g_lastCount0 = g_ticker.count;
  delay(220);
  if (!g_service.readTickerState(&g_ticker)) {
    noteStage(2U);
    return false;
  }
  g_lastCount1 = g_ticker.count;
  const bool ok = g_info.transportStatus == NRF54L15_VPR_TRANSPORT_STATUS_READY &&
                  g_caps.serviceVersionMajor == 1U &&
                  g_ticker.enabled &&
                  g_ticker.periodTicks == kTickerPeriodTicks &&
                  g_ticker.step == kTickerStep &&
                  g_lastCount1 > g_lastCount0;
  noteStage(ok ? 0U : 3U);
  return ok;
}

bool runRestartLifecycleProbe() {
  g_lastProbeOk = false;
  g_lastCycleCount = kRestartCycles;
  g_lastCyclePasses = 0U;
  g_lastCycleIndex = 0U;
  g_lastFailureStage = 0U;
  syncProbeSummary(false);

  if (!g_service.bootDefaultService(true)) {
    noteStage(10U);
    return false;
  }
  if (probeServiceCycle(0U)) {
    ++g_lastCyclePasses;
    syncProbeSummary(false);
  } else {
    return false;
  }

  for (uint8_t cycle = 1U; cycle < kRestartCycles; ++cycle) {
    if (!g_service.restartLoadedService(true)) {
      g_lastCycleIndex = cycle;
      noteStage(11U);
      return false;
    }
    if (!probeServiceCycle(cycle)) {
      return false;
    }
    ++g_lastCyclePasses;
    syncProbeSummary(false);
  }

  g_lastProbeOk = g_lastCyclePasses == g_lastCycleCount;
  syncProbeSummary(g_lastProbeOk);
  return g_lastProbeOk;
}

void printStatus() {
  Serial.print("probe_ok=");
  Serial.print(g_lastProbeOk ? 1 : 0);
  Serial.print(" running=");
  Serial.print(g_vpr.isRunning() ? 1 : 0);
  Serial.print(" cycle=");
  Serial.print(g_lastCycleIndex);
  Serial.print(" fail_stage=");
  Serial.print(g_lastFailureStage);
  Serial.print(" cycles=");
  Serial.print(g_lastCyclePasses);
  Serial.print("/");
  Serial.print(g_lastCycleCount);
  Serial.print(" heartbeat=");
  Serial.print(g_lastHeartbeat);
  Serial.print(" echoed_cookie=0x");
  Serial.print(g_lastEchoedCookie, HEX);
  Serial.print(" svc=");
  Serial.print(g_caps.serviceVersionMajor);
  Serial.print(".");
  Serial.print(g_caps.serviceVersionMinor);
  Serial.print(" opmask=0x");
  Serial.print(g_caps.opMask, HEX);
  Serial.print(" transport_status=");
  Serial.print(g_info.transportStatus);
  Serial.print(" ticker_en=");
  Serial.print(g_ticker.enabled ? 1 : 0);
  Serial.print(" period=");
  Serial.print(g_ticker.periodTicks);
  Serial.print(" step=");
  Serial.print(g_ticker.step);
  Serial.print(" count0=");
  Serial.print(g_lastCount0);
  Serial.print(" count1=");
  Serial.println(g_lastCount1);
}

void printHelp() {
  Serial.println("Commands: r run restart lifecycle probe, s status");
}

}  // namespace

void setup() {
  initializeProbeSummary();
  ++g_probeSummary.runCount;
  g_stageSerialEnabled = false;
  (void)runRestartLifecycleProbe();
  Serial.begin(115200);
  delay(1200);
  g_stageSerialEnabled = true;
  Serial.println();
  Serial.println("VPR restart lifecycle probe");
  printHelp();
  Serial.print("probe boot=");
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
      Serial.println(runRestartLifecycleProbe() ? 1 : 0);
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
