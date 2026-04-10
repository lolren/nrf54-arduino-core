#include "nrf54l15_vpr.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint32_t kDefaultPeriodTicks = 100000U;
constexpr uint32_t kDefaultStep = 3U;

VprSharedTransportStream g_vpr;
VprControllerServiceHost g_service(&g_vpr);
VprControllerServiceCapabilities g_caps{};
VprTickerState g_state{};
uint32_t g_lastCount0 = 0U;
uint32_t g_lastCount1 = 0U;
bool g_lastProbeOk = false;

bool ensureService(bool rebootService) {
  if (rebootService && !g_service.bootDefaultService(true)) {
    return false;
  }
  return g_vpr.isRunning() && g_service.readCapabilities(&g_caps);
}

bool runTickerProbe(bool rebootService, uint32_t periodTicks, uint32_t step) {
  g_lastProbeOk = false;
  g_lastCount0 = 0U;
  g_lastCount1 = 0U;
  memset(&g_caps, 0, sizeof(g_caps));
  memset(&g_state, 0, sizeof(g_state));

  if (!ensureService(rebootService)) {
    return false;
  }
  if (!g_service.configureTicker(true, periodTicks, step, &g_state)) {
    return false;
  }
  g_lastCount0 = g_state.count;
  delay(250);
  if (!g_service.readTickerState(&g_state)) {
    return false;
  }
  g_lastCount1 = g_state.count;
  g_lastProbeOk = g_state.enabled && g_state.periodTicks == periodTicks &&
                  g_state.step == step && g_lastCount1 > g_lastCount0;
  return g_lastProbeOk;
}

void stopTicker() {
  if (g_service.configureTicker(false, 0U, 1U, &g_state)) {
    g_lastCount1 = g_state.count;
  }
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
  Serial.print(" ticker_en=");
  Serial.print(g_state.enabled ? 1 : 0);
  Serial.print(" period=");
  Serial.print(g_state.periodTicks);
  Serial.print(" step=");
  Serial.print(g_state.step);
  Serial.print(" count0=");
  Serial.print(g_lastCount0);
  Serial.print(" count1=");
  Serial.println(g_lastCount1);
}

void printHelp() {
  Serial.println("Commands: r rerun ticker probe, c ticker state, x stop ticker, b reboot service, s status");
}

void readTickerStateNow() {
  if (!g_service.readTickerState(&g_state)) {
    Serial.println("ticker state read failed");
    return;
  }
  Serial.print("ticker_en=");
  Serial.print(g_state.enabled ? 1 : 0);
  Serial.print(" period=");
  Serial.print(g_state.periodTicks);
  Serial.print(" step=");
  Serial.print(g_state.step);
  Serial.print(" count=");
  Serial.println(g_state.count);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("VPR ticker offload probe");
  printHelp();
  Serial.print("probe boot=");
  Serial.println(runTickerProbe(true, kDefaultPeriodTicks, kDefaultStep) ? 1 : 0);
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
      Serial.println(runTickerProbe(false, kDefaultPeriodTicks, kDefaultStep) ? 1 : 0);
      printStatus();
      break;
    case 'b':
      Serial.print("probe reboot=");
      Serial.println(runTickerProbe(true, kDefaultPeriodTicks, kDefaultStep) ? 1 : 0);
      printStatus();
      break;
    case 'c':
      readTickerStateNow();
      break;
    case 'x':
      stopTicker();
      Serial.println("ticker stopped");
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
