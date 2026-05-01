#include <Arduino.h>
#include <nrf54_thread_experimental.h>
#include <string.h>

using xiao_nrf54l15::Nrf54ThreadExperimental;

Nrf54ThreadExperimental gThread;
uint32_t gPingTxCount = 0;
uint32_t gPingRxCount = 0;
uint32_t gPongTxCount = 0;
uint32_t gPongRxCount = 0;
bool gPongSeen = false;
bool gReplyPending = false;
uint32_t gReportMs = 0;
uint32_t gLastPingMs = 0;

otIp6Address gReplyAddr = {};
uint16_t gReplyPort = 0U;
constexpr uint16_t kUdpPort = 61631U;

void onUdp(void*, const uint8_t* payload, uint16_t length,
           const otMessageInfo& messageInfo) {
  if (gReplyPort == 0) {
    gReplyAddr = messageInfo.mPeerAddr;
    gReplyPort = messageInfo.mPeerPort;
  }
  
  if (length == 11 && memcmp(payload, "thread-ping", 11) == 0) {
    ++gPingRxCount;
    gReplyPending = true;
  }
  if (length == 11 && memcmp(payload, "thread-pong", 11) == 0) {
    ++gPongRxCount;
    gPongSeen = true;
  }
}

void printStatus() {
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

  Serial1.print("role=");
  Serial1.print(gThread.roleName());
  Serial1.print(" rloc16=0x");
  Serial1.print(gThread.rloc16(), HEX);
  Serial1.print(" ping_tx=");
  Serial1.print(gPingTxCount);
  Serial1.print(" ping_rx=");
  Serial1.print(gPingRxCount);
  Serial1.print(" pong_tx=");
  Serial1.print(gPongTxCount);
  Serial1.print(" pong_rx=");
  Serial1.print(gPongRxCount);
  Serial1.print(" pong_seen=");
  Serial1.print(gPongSeen ? 1 : 0);
  Serial1.print(" err=");
  Serial1.print(static_cast<int>(gThread.lastError()));
  Serial1.print("/");
  Serial1.print(static_cast<int>(gThread.lastUdpError()));
  Serial1.println();
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  while (!Serial) delay(10);
  while (!Serial1) delay(10);

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  otOperationalDataset dataset = {};
  Nrf54ThreadExperimental::buildDemoDataset(&dataset);
  gThread.setActiveDataset(dataset);
  gThread.begin();
  gThread.openUdp(kUdpPort, onUdp, nullptr);
  gReportMs = millis();
  gLastPingMs = millis();
  Serial.println("thread_uart_test boot");
  Serial1.println("thread_uart_test boot");
#else
  Serial.println("thread_uart_test: Thread core not enabled");
  Serial1.println("thread_uart_test: Thread core not enabled");
#endif
}

void loop() {
  gThread.process();

  // Leader: reply to received pings with pongs
  if (gReplyPending && gReplyPort != 0) {
    gReplyPending = false;
    if (gThread.sendUdp(gReplyAddr, gReplyPort, "thread-pong", 11)) {
      ++gPongTxCount;
    }
  }

  // Child: send ping to leader every 4 seconds
  if (gThread.role() == Nrf54ThreadExperimental::Role::kChild && !gPongSeen) {
    if ((millis() - gLastPingMs) >= 4000UL) {
      gLastPingMs = millis();
      otIp6Address leaderAddr = {};
      if (gThread.getLeaderRloc(&leaderAddr)) {
        gReplyAddr = leaderAddr;
        gReplyPort = kUdpPort;
        if (gThread.sendUdp(leaderAddr, kUdpPort, "thread-ping", 11)) {
          ++gPingTxCount;
        }
      }
    }
  }

  // Leader: after receiving first pong back, also start pinging the child
  if (gThread.role() == Nrf54ThreadExperimental::Role::kLeader && gReplyPort != 0) {
    if ((millis() - gLastPingMs) >= 4000UL) {
      gLastPingMs = millis();
      if (gThread.sendUdp(gReplyAddr, gReplyPort, "thread-ping", 11)) {
        ++gPingTxCount;
      }
    }
  }

  // Report every 10 seconds
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 10000UL) {
    lastPrint = millis();
    printStatus();
  }
}
