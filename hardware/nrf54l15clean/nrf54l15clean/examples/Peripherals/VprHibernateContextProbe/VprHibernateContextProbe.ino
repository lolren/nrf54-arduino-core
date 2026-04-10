#include "nrf54l15_vpr.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint32_t kTickerPeriodTicks = 100000U;
constexpr uint32_t kTickerStep = 5U;
constexpr uint32_t kHibernateSettleMs = 80U;

VprSharedTransportStream g_vpr;
VprControllerServiceHost g_service(&g_vpr);
VprControllerServiceCapabilities g_caps{};
VprTickerState g_ticker{};
uint32_t g_savedChecksum = 0U;
uint32_t g_savedNonZeroBytes = 0U;
uint32_t g_countBeforeHibernate = 0U;
uint32_t g_countAfterHibernate = 0U;
uint32_t g_sleepCtrl = 0U;
bool g_lastProbeOk = false;

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
  memset(&g_caps, 0, sizeof(g_caps));
  return g_service.bootDefaultService(true) && g_service.readCapabilities(&g_caps);
}

bool runProbe() {
  g_lastProbeOk = false;
  g_savedChecksum = 0U;
  g_savedNonZeroBytes = 0U;
  g_countBeforeHibernate = 0U;
  g_countAfterHibernate = 0U;

  if (!bootService()) {
    return false;
  }
  if (!VprControl::clearSavedContext()) {
    return false;
  }
  if (!VprControl::enableContextRestore(true)) {
    return false;
  }
  g_sleepCtrl = 0x0002000FU;

  if (!g_service.configureTicker(true, kTickerPeriodTicks, kTickerStep, &g_ticker)) {
    return false;
  }
  delay(120);
  if (!g_service.readTickerState(&g_ticker)) {
    return false;
  }
  g_countBeforeHibernate = g_ticker.count;

  if (!g_service.enterHibernate()) {
    return false;
  }
  delay(kHibernateSettleMs);

  if (!summarizeSavedContext()) {
    return false;
  }
  g_countAfterHibernate = g_ticker.count;
  g_lastProbeOk = VprControl::contextRestoreEnabled() && g_savedNonZeroBytes > 0U;
  return g_lastProbeOk;
}

bool primeTicker() {
  if (!g_service.configureTicker(true, kTickerPeriodTicks, kTickerStep, &g_ticker)) {
    return false;
  }
  delay(120);
  if (!g_service.readTickerState(&g_ticker)) {
    return false;
  }
  g_countBeforeHibernate = g_ticker.count;
  return true;
}

bool enterHibernateNow() {
  if (!VprControl::enableContextRestore(true)) {
    return false;
  }
  g_sleepCtrl = 0x0002000FU;
  if (!g_service.enterHibernate()) {
    return false;
  }
  delay(kHibernateSettleMs);
  if (!summarizeSavedContext()) {
    return false;
  }
  g_lastProbeOk = VprControl::contextRestoreEnabled() && g_savedNonZeroBytes > 0U;
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
  Serial.print(" restore=");
  Serial.print(VprControl::contextRestoreEnabled() ? 1 : 0);
  Serial.print(" p1ret=0x");
  Serial.print(VprControl::rawMemconfPower1Ret(), HEX);
  Serial.print(" p0ret2=0x");
  Serial.print(VprControl::rawMemconfPower0Ret2(), HEX);
  Serial.print(" sleepctrl=0x");
  Serial.print(g_sleepCtrl, HEX);
  Serial.print(" count_before=");
  Serial.print(g_countBeforeHibernate);
  Serial.print(" ticker_count=");
  Serial.print(g_ticker.count);
  Serial.print(" saved_nonzero=");
  Serial.print(g_savedNonZeroBytes);
  Serial.print(" saved_fnv=0x");
  Serial.println(g_savedChecksum, HEX);
}

void printContextSummaryNow() {
  if (!summarizeSavedContext()) {
    Serial.println("saved context read failed");
    return;
  }
  Serial.print("saved_nonzero=");
  Serial.print(g_savedNonZeroBytes);
  Serial.print(" saved_fnv=0x");
  Serial.println(g_savedChecksum, HEX);
}

void printHelp() {
  Serial.println("Commands: r run full probe, b boot service, t prime ticker, h enter hibernate, c context summary, s status");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("VPR hibernate context probe");
  printHelp();
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
    case 'h':
      Serial.print("hibernate=");
      Serial.println(enterHibernateNow() ? 1 : 0);
      printStatus();
      break;
    case 'c':
      printContextSummaryNow();
      break;
    case 's':
      printStatus();
      break;
    default:
      printHelp();
      break;
  }
}
