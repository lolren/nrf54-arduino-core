/*
 * ThreadExperimentalUdpPing — Thread UDP communication test between two XIAO boards
 *
 * Flash this example to BOTH boards. Each board:
 *   1. Joins a Thread network (same passphrase/network)
 *   2. Sends UDP "ping" messages to the other board
 *   3. Expects "pong" replies
 *
 * Results are stored in volatile globals for pyocd reading.
 */
#include <Arduino.h>
#include <nrf54_thread_experimental.h>
#include <string.h>

using xiao_nrf54l15::Nrf54ThreadExperimental;

extern "C" __attribute__((used)) volatile uint32_t g_thread_test_results[16] = {0};

namespace {

constexpr uint16_t kUdpPort = 61631U;
constexpr uint32_t kPingRetryMs = 4000UL;
constexpr char kPingText[] = "thread-ping";
constexpr char kPongText[] = "thread-pong";

Nrf54ThreadExperimental gThread;
uint32_t gPingTxCount = 0;
uint32_t gPingRxCount = 0;
uint32_t gPongTxCount = 0;
uint32_t gPongRxCount = 0;
bool gPongSeen = false;
uint32_t gReportMs = 0;

otIp6Address gReplyAddr = {};
uint16_t gReplyPort = 0U;
bool gReplyPending = false;

void storeRxPayload(const uint8_t* payload, uint16_t length) {
  if (payload == nullptr || length == 0U) return;

  if (length == sizeof(kPingText) - 1 &&
      memcmp(payload, kPingText, sizeof(kPingText) - 1) == 0) {
    ++gPingRxCount;
  }

  if (length == sizeof(kPongText) - 1 &&
      memcmp(payload, kPongText, sizeof(kPongText) - 1) == 0) {
    ++gPongRxCount;
    gPongSeen = true;
  }
}

void onUdp(void*, const uint8_t* payload, uint16_t length,
           const otMessageInfo& messageInfo) {
  storeRxPayload(payload, length);

  // Store reply address from any UDP message
  if (gReplyPort == 0) {
    gReplyAddr = messageInfo.mPeerAddr;
    gReplyPort = messageInfo.mPeerPort;
  }
}

void updateResults() {
  g_thread_test_results[0] = gThread.started() ? 1U : 0U;
  g_thread_test_results[1] = gThread.attached() ? 1U : 0U;
  g_thread_test_results[2] = static_cast<uint32_t>(gThread.role());
  g_thread_test_results[3] = gThread.rloc16();
  g_thread_test_results[4] = static_cast<uint32_t>(gThread.lastError());
  g_thread_test_results[5] = static_cast<uint32_t>(gThread.lastUdpError());
  g_thread_test_results[6] = gThread.attached() ? 1U : 0U;
  g_thread_test_results[7] = gPingTxCount;
  g_thread_test_results[8] = gPingRxCount;
  g_thread_test_results[9] = gPongTxCount;
  g_thread_test_results[10] = gPongRxCount;
  g_thread_test_results[11] = gPongSeen ? 1U : 0U;
  g_thread_test_results[12] = gReplyPending ? 1U : 0U;
  g_thread_test_results[13] = millis() - gReportMs;
  g_thread_test_results[14] = 0;
  g_thread_test_results[15] = 0;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  otOperationalDataset dataset = {};
  Nrf54ThreadExperimental::buildDemoDataset(&dataset);
  gThread.setActiveDataset(dataset);
  gThread.begin();
  gThread.openUdp(kUdpPort, onUdp, nullptr);
  gReportMs = millis();
  Serial.println("thread_ping boot");
#else
  Serial.println("thread_ping: Thread core not enabled");
#endif
}

void loop() {
  gThread.process();

  // Handle reply to ping (both boards send pings, so reply with pong)
  if (gReplyPending && gReplyPort != 0) {
    gReplyPending = false;
    gThread.sendUdp(gReplyAddr, gReplyPort, kPongText, sizeof(kPongText) - 1);
    ++gPongTxCount;
  }

  // Send ping every 4 seconds once we have a reply address
  if (gThread.attached() && gReplyPort != 0) {
    uint32_t now = millis();
    if ((now - gReportMs) > kPingRetryMs) {
      gReportMs = now;
      gThread.sendUdp(gReplyAddr, gReplyPort, kPingText, sizeof(kPingText) - 1);
      ++gPingTxCount;
    }
  }

  updateResults();

  // Report to Serial every 10 seconds
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 10000UL) {
    lastPrint = millis();
    Serial.print("role=");
    Serial.print(gThread.roleName());
    Serial.print(" rloc16=0x");
    Serial.print(gThread.rloc16(), HEX);
    Serial.print(" ping_tx=");
    Serial.print(gPingTxCount);
    Serial.print(" ping_rx=");
    Serial.print(gPingRxCount);
    Serial.print(" pong_tx=");
    Serial.print(gPongTxCount);
    Serial.print(" pong_rx=");
    Serial.print(gPongRxCount);
    Serial.print(" pong_seen=");
    Serial.print(gPongSeen ? 1 : 0);
    Serial.print(" err=");
    Serial.print(static_cast<int>(gThread.lastError()));
    Serial.print("/");
    Serial.print(static_cast<int>(gThread.lastUdpError()));
    Serial.println();
  }
}
