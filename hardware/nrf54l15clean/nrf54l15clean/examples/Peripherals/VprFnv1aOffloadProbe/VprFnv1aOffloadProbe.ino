#include <string.h>

#include "nrf54l15_vpr.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint8_t kDefaultPayload[] = "xiao-nrf54l15-vpr-offload";
constexpr uint8_t kAlternatePayload[] = {
    0x10U, 0x32U, 0x54U, 0x76U, 0x98U, 0xBAU, 0xDCU, 0xFEU,
    0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xABU, 0xCDU, 0xEFU,
};

VprSharedTransportStream g_vpr;
VprControllerServiceHost g_service(&g_vpr);
VprControllerServiceInfo g_lastInfo{};
uint32_t g_lastHostHash = 0U;
uint32_t g_lastVprHash = 0U;
uint32_t g_lastProcessedLen = 0U;
bool g_lastProbeOk = false;

uint32_t hostFnv1a32(const uint8_t* data, size_t len) {
  uint32_t hash = 2166136261UL;
  if (data == nullptr) {
    return hash;
  }
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint32_t>(data[i]);
    hash *= 16777619UL;
  }
  return hash;
}

bool runProbe(const uint8_t* data, size_t len, bool rebootService) {
  g_lastProbeOk = false;
  g_lastHostHash = 0U;
  g_lastVprHash = 0U;
  g_lastProcessedLen = 0U;
  memset(&g_lastInfo, 0, sizeof(g_lastInfo));

  if (rebootService && !g_service.bootDefaultService(true)) {
    return false;
  }

  if (!g_vpr.isRunning()) {
    return false;
  }

  g_lastHostHash = hostFnv1a32(data, len);
  if (!g_service.hashFnv1a32(data, len, &g_lastVprHash, &g_lastProcessedLen)) {
    return false;
  }
  if (!g_service.readTransportInfo(&g_lastInfo)) {
    return false;
  }

  g_lastProbeOk = (g_lastHostHash == g_lastVprHash) &&
                  (g_lastProcessedLen == static_cast<uint32_t>(len));
  return g_lastProbeOk;
}

void printStatus() {
  Serial.print("probe_ok=");
  Serial.print(g_lastProbeOk ? 1 : 0);
  Serial.print(" running=");
  Serial.print(g_vpr.isRunning() ? 1 : 0);
  Serial.print(" transport_status=");
  Serial.print(g_lastInfo.transportStatus);
  Serial.print(" heartbeat=");
  Serial.print(g_lastInfo.heartbeat);
  Serial.print(" len=");
  Serial.print(g_lastProcessedLen);
  Serial.print(" host_hash=0x");
  Serial.print(g_lastHostHash, HEX);
  Serial.print(" vpr_hash=0x");
  Serial.println(g_lastVprHash, HEX);
}

void printHelp() {
  Serial.println("Commands: r rerun default payload, a alternate payload, b reboot service, s status");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("VPR FNV1a offload probe");
  printHelp();
  Serial.print("probe boot=");
  Serial.println(runProbe(kDefaultPayload, strlen(reinterpret_cast<const char*>(kDefaultPayload)),
                          true)
                     ? 1
                     : 0);
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
      Serial.print("probe default=");
      Serial.println(runProbe(kDefaultPayload,
                              strlen(reinterpret_cast<const char*>(kDefaultPayload)),
                              false)
                         ? 1
                         : 0);
      printStatus();
      break;
    case 'a':
      Serial.print("probe alternate=");
      Serial.println(runProbe(kAlternatePayload, sizeof(kAlternatePayload), false) ? 1 : 0);
      printStatus();
      break;
    case 'b':
      Serial.print("probe reboot=");
      Serial.println(runProbe(kDefaultPayload,
                              strlen(reinterpret_cast<const char*>(kDefaultPayload)),
                              true)
                         ? 1
                         : 0);
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
