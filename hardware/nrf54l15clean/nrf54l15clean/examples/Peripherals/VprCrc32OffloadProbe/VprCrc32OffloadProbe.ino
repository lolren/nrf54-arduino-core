#include <string.h>

#include "nrf54l15_vpr.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint8_t kDefaultPayload[] = "xiao-nrf54l15-vpr-crc32";
constexpr uint8_t kAlternatePayload[] = {
    0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
    0x88U, 0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU,
};

VprSharedTransportStream g_vpr;
VprControllerServiceHost g_service(&g_vpr);
VprControllerServiceCapabilities g_caps{};
uint32_t g_lastHostCrc = 0U;
uint32_t g_lastVprCrc = 0U;
uint32_t g_lastProcessedLen = 0U;
bool g_lastProbeOk = false;

uint32_t hostCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  if (data == nullptr) {
    return crc ^ 0xFFFFFFFFUL;
  }
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      const uint32_t mask = -(crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return crc ^ 0xFFFFFFFFUL;
}

bool ensureService(bool rebootService) {
  if (rebootService && !g_service.bootDefaultService(true)) {
    return false;
  }
  return g_vpr.isRunning() && g_service.readCapabilities(&g_caps);
}

bool runProbe(const uint8_t* data, size_t len, bool rebootService) {
  g_lastProbeOk = false;
  g_lastHostCrc = 0U;
  g_lastVprCrc = 0U;
  g_lastProcessedLen = 0U;
  memset(&g_caps, 0, sizeof(g_caps));

  if (!ensureService(rebootService)) {
    return false;
  }
  g_lastHostCrc = hostCrc32(data, len);
  if (!g_service.crc32(data, len, &g_lastVprCrc, &g_lastProcessedLen)) {
    return false;
  }
  g_lastProbeOk = (g_lastHostCrc == g_lastVprCrc) &&
                  (g_lastProcessedLen == static_cast<uint32_t>(len));
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
  Serial.print(" max_in=");
  Serial.print(g_caps.maxInputLen);
  Serial.print(" len=");
  Serial.print(g_lastProcessedLen);
  Serial.print(" host_crc=0x");
  Serial.print(g_lastHostCrc, HEX);
  Serial.print(" vpr_crc=0x");
  Serial.println(g_lastVprCrc, HEX);
}

void printHelp() {
  Serial.println("Commands: r rerun default payload, a alternate payload, c capabilities, b reboot service, s status");
}

void printCapabilities() {
  if (!g_service.readCapabilities(&g_caps)) {
    Serial.println("capabilities read failed");
    return;
  }
  Serial.print("svc=");
  Serial.print(g_caps.serviceVersionMajor);
  Serial.print(".");
  Serial.print(g_caps.serviceVersionMinor);
  Serial.print(" opmask=0x");
  Serial.print(g_caps.opMask, HEX);
  Serial.print(" max_in=");
  Serial.println(g_caps.maxInputLen);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("VPR CRC32 offload probe");
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
    case 'c':
      printCapabilities();
      break;
    case 's':
      printStatus();
      break;
    default:
      printHelp();
      break;
  }
}
