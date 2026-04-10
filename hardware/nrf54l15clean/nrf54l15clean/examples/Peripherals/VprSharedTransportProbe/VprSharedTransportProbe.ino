#include "nrf54l15_vpr.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint32_t kPingCookie = 0x12345678UL;

VprSharedTransportStream g_vpr;
VprControllerServiceHost g_service(&g_vpr);
uint32_t g_lastHeartbeat = 0U;
uint32_t g_lastScriptCount = 0U;
uint8_t g_lastTransportStatus = 0U;
uint8_t g_lastTransportError = 0U;
uint8_t g_lastTransportFlags = 0U;
uint32_t g_lastEchoedCookie = 0U;
uint8_t g_lastServiceVersionMajor = 0U;
uint8_t g_lastServiceVersionMinor = 0U;
uint32_t g_lastServiceOpMask = 0U;
uint32_t g_lastServiceMaxInput = 0U;
bool g_lastProbeOk = false;

bool probeRunningTransport() {
  g_lastProbeOk = false;
  g_lastHeartbeat = 0U;
  g_lastScriptCount = 0U;
  g_lastTransportStatus = 0U;
  g_lastTransportError = 0U;
  g_lastTransportFlags = 0U;
  g_lastEchoedCookie = 0U;
  g_lastServiceVersionMajor = 0U;
  g_lastServiceVersionMinor = 0U;
  g_lastServiceOpMask = 0U;
  g_lastServiceMaxInput = 0U;

  if (!g_vpr.isRunning()) {
    return false;
  }

  VprControllerServiceInfo info{};
  VprControllerServiceCapabilities caps{};
  if (!g_service.probe(kPingCookie, &info, &g_lastEchoedCookie, &g_lastHeartbeat)) {
    return false;
  }
  if (g_lastEchoedCookie != kPingCookie) {
    return false;
  }
  if (!g_service.readCapabilities(&caps)) {
    return false;
  }
  g_lastTransportStatus = info.transportStatus;
  g_lastTransportError = info.transportError;
  g_lastTransportFlags = info.transportFlags;
  g_lastHeartbeat = info.heartbeat;
  g_lastScriptCount = info.scriptCount;
  g_lastServiceVersionMajor = caps.serviceVersionMajor;
  g_lastServiceVersionMinor = caps.serviceVersionMinor;
  g_lastServiceOpMask = caps.opMask;
  g_lastServiceMaxInput = caps.maxInputLen;
  g_lastProbeOk = true;
  return true;
}

bool runProbe(bool rebootTransport) {
  if (rebootTransport && !g_service.bootDefaultService(true)) {
    return false;
  }
  return probeRunningTransport();
}

void printStatus() {
  Serial.print("probe_ok=");
  Serial.print(g_lastProbeOk ? 1 : 0);
  Serial.print(" running=");
  Serial.print(g_vpr.isRunning() ? 1 : 0);
  Serial.print(" secure=");
  Serial.print(g_vpr.secureAccessEnabled() ? 1 : 0);
  Serial.print(" init_pc=0x");
  Serial.print(g_vpr.initPc(), HEX);
  Serial.print(" spu_perm=0x");
  Serial.print(g_vpr.spuPerm(), HEX);
  Serial.print(" transport_status=");
  Serial.print(g_lastTransportStatus);
  Serial.print(" sleepctrl=0x");
  Serial.print(g_vpr.rawSleepControl(), HEX);
  Serial.print(" ret0_2=0x");
  Serial.print(VprControl::rawMemconfPower0Ret2(), HEX);
  Serial.print(" ret1=0x");
  Serial.print(VprControl::rawMemconfPower1Ret(), HEX);
  Serial.print(" transport_err=");
  Serial.print(g_lastTransportError);
  Serial.print(" transport_flags=0x");
  Serial.print(g_lastTransportFlags, HEX);
  Serial.print(" restored=");
  Serial.print((g_lastTransportFlags & VprControllerServiceHost::kTransportFlagRestoredFromHibernate) != 0U ? 1 : 0);
  Serial.print(" heartbeat=");
  Serial.print(g_lastHeartbeat);
  Serial.print(" echoed_cookie=0x");
  Serial.print(g_lastEchoedCookie, HEX);
  Serial.print(" script_count=");
  Serial.print(g_lastScriptCount);
  Serial.print(" svc=");
  Serial.print(g_lastServiceVersionMajor);
  Serial.print(".");
  Serial.print(g_lastServiceVersionMinor);
  Serial.print(" opmask=0x");
  Serial.print(g_lastServiceOpMask, HEX);
  Serial.print(" max_in=");
  Serial.println(g_lastServiceMaxInput);
}

void printHelp() {
  Serial.println("Commands: r probe current transport, s status");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("VPR shared transport probe");
  printHelp();
  Serial.print("probe boot=");
  Serial.println(runProbe(true) ? 1 : 0);
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
