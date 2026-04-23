#include <nrf54_thread_experimental.h>

#include <string.h>

using xiao_nrf54l15::Nrf54ThreadExperimental;

namespace {

constexpr uint16_t kUdpPort = 61631U;
constexpr uint32_t kPingRetryMs = 4000UL;
constexpr char kPingText[] = "hello-ping";
constexpr char kPongText[] = "hello-pong";

Nrf54ThreadExperimental gThread;
Nrf54ThreadExperimental::Role gLastRole =
    Nrf54ThreadExperimental::Role::kUnknown;
otIp6Address gReplyAddr = {};
uint16_t gReplyPort = 0U;
uint32_t gLastReportMs = 0;
uint32_t gLastPingMs = 0;
uint32_t gPingTxCount = 0;
uint32_t gPingRxCount = 0;
uint32_t gPongTxCount = 0;
uint32_t gPongRxCount = 0;
bool gReplyPending = false;
bool gPongSeen = false;
char gLastRxText[24] = {0};

void storeText(const uint8_t* payload, uint16_t length) {
  memset(gLastRxText, 0, sizeof(gLastRxText));
  if (payload == nullptr || length == 0U) {
    return;
  }
  if (length >= sizeof(gLastRxText)) {
    length = sizeof(gLastRxText) - 1U;
  }
  memcpy(gLastRxText, payload, length);
}

void onUdp(void*, const uint8_t* payload, uint16_t length,
           const otMessageInfo& messageInfo) {
  storeText(payload, length);

  if (length == strlen(kPingText) &&
      memcmp(payload, kPingText, strlen(kPingText)) == 0) {
    ++gPingRxCount;
    gReplyAddr = messageInfo.mPeerAddr;
    gReplyPort = messageInfo.mPeerPort;
    gReplyPending = true;
    return;
  }

  if (length == strlen(kPongText) &&
      memcmp(payload, kPongText, strlen(kPongText)) == 0) {
    ++gPongRxCount;
    gPongSeen = true;
  }
}

void printStatus() {
  Serial.print("thread_udp role=");
  Serial.print(gThread.roleName());
  Serial.print(" rloc16=0x");
  Serial.print(gThread.rloc16(), HEX);
  Serial.print(" ping=");
  Serial.print(gPingTxCount);
  Serial.print("/");
  Serial.print(gPingRxCount);
  Serial.print(" pong=");
  Serial.print(gPongTxCount);
  Serial.print("/");
  Serial.print(gPongRxCount);
  Serial.print(" ok=");
  Serial.print(gPongSeen ? 1 : 0);
  Serial.print(" last=");
  Serial.print(gLastRxText);
  Serial.print(" err=");
  Serial.print(static_cast<int>(gThread.lastError()));
  Serial.print("/");
  Serial.print(static_cast<int>(gThread.lastUdpError()));
  Serial.println();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  otOperationalDataset dataset = {};
  Nrf54ThreadExperimental::buildDemoDataset(&dataset);
  gThread.setActiveDataset(dataset);
  gThread.begin();
  gThread.openUdp(kUdpPort, onUdp, nullptr);
  Serial.println("thread_udp boot");
#else
  Serial.println(
      "Enable Tools > Thread Core > Experimental Stage Core (Leader/Child + UDP).");
#endif
}

void loop() {
  gThread.process();

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  const Nrf54ThreadExperimental::Role currentRole = gThread.role();
  if (currentRole != gLastRole) {
    gLastRole = currentRole;
    printStatus();
  }

  if (currentRole == Nrf54ThreadExperimental::Role::kLeader && gReplyPending) {
    if (gThread.sendUdp(gReplyAddr,
                        gReplyPort != 0U ? gReplyPort : kUdpPort,
                        kPongText,
                        strlen(kPongText))) {
      ++gPongTxCount;
      gReplyPending = false;
    }
  }

  if (currentRole == Nrf54ThreadExperimental::Role::kChild && !gPongSeen &&
      (millis() - gLastPingMs) >= kPingRetryMs) {
    otIp6Address leaderAddr = {};
    gLastPingMs = millis();
    if (gThread.getLeaderRloc(&leaderAddr) &&
        gThread.sendUdp(leaderAddr, kUdpPort, kPingText, strlen(kPingText))) {
      ++gPingTxCount;
    }
  }

  if ((millis() - gLastReportMs) >= 1000UL) {
    gLastReportMs = millis();
    printStatus();
  }
#endif
}
