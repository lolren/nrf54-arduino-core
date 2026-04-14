#include "nrf54l15_vpr.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint32_t kDefaultIntervalTicks = 80000U;
constexpr uint8_t kDefaultChannelMask = 0x07U;
constexpr uint32_t kEventTimeoutMs = 2500U;
constexpr uint32_t kProbeSummaryMagic = 0x56424150UL;
constexpr uint32_t kProbeSummaryVersion = 1U;

struct VprBleLegacyAdvertisingProbeSummary {
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
  uint32_t state0Enabled;
  uint32_t state0Mask;
  uint32_t state0Random;
  uint32_t state1Enabled;
  uint32_t state1Mask;
  uint32_t state1LastMask;
  uint32_t state1EventCount;
  uint32_t state1DroppedEvents;
  uint32_t event0Mask;
  uint32_t event0Count;
  uint32_t event0Sequence;
  uint32_t event1Mask;
  uint32_t event1Count;
  uint32_t event1Sequence;
};

__attribute__((section(".noinit"))) static VprBleLegacyAdvertisingProbeSummary
    g_probeSummary;

VprSharedTransportStream g_vpr;
VprControllerServiceHost g_service(&g_vpr);
VprControllerServiceCapabilities g_caps{};
VprBleLegacyAdvertisingState g_state0{};
VprBleLegacyAdvertisingState g_state1{};
VprBleLegacyAdvertisingEvent g_event0{};
VprBleLegacyAdvertisingEvent g_event1{};
uint32_t g_hostDropCount = 0U;
bool g_lastProbeOk = false;
bool g_lastRandomDelay = false;

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
  g_probeSummary.state0Enabled = g_state0.enabled ? 1U : 0U;
  g_probeSummary.state0Mask = g_state0.channelMask;
  g_probeSummary.state0Random = g_state0.addRandomDelay ? 1U : 0U;
  g_probeSummary.state1Enabled = g_state1.enabled ? 1U : 0U;
  g_probeSummary.state1Mask = g_state1.channelMask;
  g_probeSummary.state1LastMask = g_state1.lastChannelMask;
  g_probeSummary.state1EventCount = g_state1.eventCount;
  g_probeSummary.state1DroppedEvents = g_state1.droppedEvents;
  g_probeSummary.event0Mask = g_event0.channelMask;
  g_probeSummary.event0Count = g_event0.eventCount;
  g_probeSummary.event0Sequence = g_event0.sequence;
  g_probeSummary.event1Mask = g_event1.channelMask;
  g_probeSummary.event1Count = g_event1.eventCount;
  g_probeSummary.event1Sequence = g_event1.sequence;
}

bool ensureService(bool rebootService) {
  if (rebootService && !g_service.bootDefaultService(true)) {
    return false;
  }
  return g_vpr.isRunning() && g_service.readCapabilities(&g_caps);
}

bool channelMaskSubset(uint8_t mask, uint8_t fullMask) {
  return mask != 0U && (mask & static_cast<uint8_t>(~fullMask)) == 0U;
}

bool runProbe(bool rebootService, bool addRandomDelay) {
  g_lastProbeOk = false;
  g_lastRandomDelay = addRandomDelay;
  memset(&g_caps, 0, sizeof(g_caps));
  memset(&g_state0, 0, sizeof(g_state0));
  memset(&g_state1, 0, sizeof(g_state1));
  memset(&g_event0, 0, sizeof(g_event0));
  memset(&g_event1, 0, sizeof(g_event1));
  g_hostDropCount = 0U;
  syncProbeSummary(false);

  if (!ensureService(rebootService)) {
    return false;
  }
  if (!g_service.configureBleLegacyAdvertising(true, kDefaultIntervalTicks,
                                               kDefaultChannelMask,
                                               addRandomDelay, &g_state0)) {
    return false;
  }
  if (!g_service.waitBleLegacyAdvertisingEvent(&g_event0, kEventTimeoutMs) ||
      !g_service.waitBleLegacyAdvertisingEvent(&g_event1, kEventTimeoutMs) ||
      !g_service.readBleLegacyAdvertisingState(&g_state1)) {
    return false;
  }

  g_hostDropCount = g_service.pendingBleLegacyAdvertisingEventDropCount();
  g_lastProbeOk =
      (g_caps.opMask & VprControllerServiceHost::kOpBleLegacyAdvertisingConfigure) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleLegacyAdvertisingReadState) != 0U &&
      (g_caps.opMask & VprControllerServiceHost::kOpBleLegacyAdvertisingEvent) != 0U &&
      g_state0.enabled && g_state1.enabled &&
      g_state0.channelMask == kDefaultChannelMask &&
      g_state1.channelMask == kDefaultChannelMask &&
      g_state0.addRandomDelay == addRandomDelay &&
      g_state1.addRandomDelay == addRandomDelay &&
      g_state1.eventCount >= g_event1.eventCount &&
      g_event1.eventCount > g_event0.eventCount &&
      g_event1.sequence > g_event0.sequence &&
      channelMaskSubset(g_event0.channelMask, kDefaultChannelMask) &&
      channelMaskSubset(g_event1.channelMask, kDefaultChannelMask) &&
      channelMaskSubset(g_state1.lastChannelMask, kDefaultChannelMask) &&
      g_hostDropCount == 0U;
  syncProbeSummary(g_lastProbeOk);
  return g_lastProbeOk;
}

void stopAdvertising() {
  (void)g_service.configureBleLegacyAdvertising(false, 0U, kDefaultChannelMask,
                                                false, &g_state1);
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
  Serial.print(" adv_en=");
  Serial.print(g_state1.enabled ? 1 : 0);
  Serial.print(" mask=0x");
  Serial.print(g_state1.channelMask, HEX);
  Serial.print(" last=0x");
  Serial.print(g_state1.lastChannelMask, HEX);
  Serial.print(" rnd=");
  Serial.print(g_state1.addRandomDelay ? 1 : 0);
  Serial.print(" interval=");
  Serial.print(g_state1.intervalTicks);
  Serial.print(" last_delay=");
  Serial.print(g_state1.lastRandomDelayTicks);
  Serial.print(" count=");
  Serial.print(g_state1.eventCount);
  Serial.print(" drop=");
  Serial.print(g_state1.droppedEvents);
  Serial.print(" host_drop=");
  Serial.print(g_hostDropCount);
  Serial.print(" ev0=");
  Serial.print(g_event0.channelMask, HEX);
  Serial.print("@");
  Serial.print(g_event0.heartbeat);
  Serial.print("#");
  Serial.print(g_event0.sequence);
  Serial.print(" ev1=");
  Serial.print(g_event1.channelMask, HEX);
  Serial.print("@");
  Serial.print(g_event1.heartbeat);
  Serial.print("#");
  Serial.println(g_event1.sequence);
}

void printNextEvent() {
  VprBleLegacyAdvertisingEvent event{};
  if (!g_service.waitBleLegacyAdvertisingEvent(&event, kEventTimeoutMs)) {
    Serial.println("adv event wait failed");
    return;
  }
  Serial.print("adv_event flags=0x");
  Serial.print(event.flags, HEX);
  Serial.print(" mask=0x");
  Serial.print(event.channelMask, HEX);
  Serial.print(" count=");
  Serial.print(event.eventCount);
  Serial.print(" heartbeat=");
  Serial.print(event.heartbeat);
  Serial.print(" random=");
  Serial.print(event.randomDelayTicks);
  Serial.print(" seq=");
  Serial.println(event.sequence);
}

void printHelp() {
  Serial.println(
      "Commands: r rerun probe, d rerun with random delay, e wait next adv event, x stop adv, s status");
}

}  // namespace

void setup() {
  initializeProbeSummary();
  ++g_probeSummary.runCount;
  (void)runProbe(true, false);
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("VPR BLE legacy advertising probe");
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
      Serial.println(runProbe(false, false) ? 1 : 0);
      printStatus();
      break;
    case 'd':
      Serial.print("probe delay=");
      Serial.println(runProbe(false, true) ? 1 : 0);
      printStatus();
      break;
    case 'e':
      printNextEvent();
      break;
    case 'x':
      stopAdvertising();
      Serial.println("adv stopped");
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
