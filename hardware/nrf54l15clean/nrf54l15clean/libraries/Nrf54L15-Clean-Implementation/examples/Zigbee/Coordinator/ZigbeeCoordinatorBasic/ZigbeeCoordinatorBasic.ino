#include <Arduino.h>

#include <math.h>

#include "nrf54l15_hal.h"

#if defined(NRF54L15_CLEAN_ZIGBEE_ENABLED) && (NRF54L15_CLEAN_ZIGBEE_ENABLED == 0)
#error "Enable Tools > Zigbee Support to build ZigbeeCoordinator."
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_CHANNEL
#define NRF54L15_CLEAN_ZIGBEE_CHANNEL 15
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_PAN_ID
#define NRF54L15_CLEAN_ZIGBEE_PAN_ID 0x1234
#endif

using namespace xiao_nrf54l15;

// Simple Zigbee coordinator role for the raw 802.15.4/Zigbee helper path.
//
// This is a teaching/demo sketch, not a full Zigbee stack. It accepts join
// requests, assigns short addresses, receives app payloads, and returns a small
// ACK-like application response.

static ZigbeeRadio g_zb;
static uint8_t g_sequence = 1U;
static uint32_t g_lastStatusMs = 0U;
static uint32_t g_rxFrames = 0U;
static uint32_t g_joinAccepted = 0U;
static uint32_t g_appRx = 0U;
static uint16_t g_nextShortAddress = 0x0100U;

// Network configuration comes from the Tools menu defaults unless overridden by
// the macros above.
static constexpr uint8_t kChannel =
    static_cast<uint8_t>(NRF54L15_CLEAN_ZIGBEE_CHANNEL);
static constexpr uint16_t kPanId =
    static_cast<uint16_t>(NRF54L15_CLEAN_ZIGBEE_PAN_ID);
static constexpr uint16_t kCoordinatorShort = 0x0000U;
static constexpr uint8_t kJoinReqCmdId = 0xA1U;
static constexpr uint8_t kJoinRspCmdId = 0xA2U;
static constexpr uint8_t kRoleCoordinator = 0U;
static constexpr uint8_t kRoleRouter = 1U;
static constexpr uint8_t kRoleEndDevice = 2U;
// Rough RSSI-based distance heuristic for logs only.
static constexpr float kRefRssiAtOneMeterDbm = -59.0f;
static constexpr float kPathLossExponent = 2.0f;

struct NodeEntry {
  bool used;
  uint16_t shortAddr;
  uint16_t tempShort;
  uint8_t role;
  uint32_t lastSeenMs;
};

static NodeEntry g_nodes[8] = {};

static const char* roleName(uint8_t role) {
  if (role == kRoleCoordinator) {
    return "coord";
  }
  if (role == kRoleRouter) {
    return "router";
  }
  if (role == kRoleEndDevice) {
    return "end";
  }
  return "unknown";
}

static int32_t estimateDistanceMm(int8_t rssiDbm) {
  if (rssiDbm >= 0) {
    return -1;
  }
  const float exponent =
      (kRefRssiAtOneMeterDbm - static_cast<float>(rssiDbm)) /
      (10.0f * kPathLossExponent);
  const float meters = powf(10.0f, exponent);
  const int32_t mm = static_cast<int32_t>(meters * 1000.0f + 0.5f);
  if (mm < 0) {
    return -1;
  }
  return mm;
}

static NodeEntry* findNode(uint16_t shortAddr) {
  for (size_t i = 0; i < (sizeof(g_nodes) / sizeof(g_nodes[0])); ++i) {
    if (g_nodes[i].used && g_nodes[i].shortAddr == shortAddr) {
      return &g_nodes[i];
    }
  }
  return nullptr;
}

static NodeEntry* findNodeByTemp(uint16_t tempShort) {
  for (size_t i = 0; i < (sizeof(g_nodes) / sizeof(g_nodes[0])); ++i) {
    if (g_nodes[i].used && g_nodes[i].tempShort == tempShort) {
      return &g_nodes[i];
    }
  }
  return nullptr;
}

static NodeEntry* allocateNode() {
  for (size_t i = 0; i < (sizeof(g_nodes) / sizeof(g_nodes[0])); ++i) {
    if (!g_nodes[i].used) {
      g_nodes[i].used = true;
      return &g_nodes[i];
    }
  }
  return nullptr;
}

static bool assignNode(uint16_t requesterShort, uint8_t role,
                       uint16_t* outAssignedShort) {
  if (outAssignedShort == nullptr) {
    return false;
  }

  NodeEntry* existing = findNode(requesterShort);
  if (existing == nullptr) {
    existing = findNodeByTemp(requesterShort);
  }
  if (existing != nullptr) {
    existing->lastSeenMs = millis();
    existing->role = role;
    *outAssignedShort = existing->shortAddr;
    return true;
  }

  NodeEntry* entry = allocateNode();
  if (entry == nullptr) {
    return false;
  }

  entry->role = role;
  entry->lastSeenMs = millis();
  entry->shortAddr = g_nextShortAddress++;
  entry->tempShort = requesterShort;
  *outAssignedShort = entry->shortAddr;
  ++g_joinAccepted;
  return true;
}

static void sendJoinResponse(uint16_t destinationShort, uint8_t nonce,
                             uint8_t status, uint16_t assignedShort,
                             uint8_t acceptedRole) {
  uint8_t payload[5] = {
      status,
      static_cast<uint8_t>(assignedShort & 0xFFU),
      static_cast<uint8_t>((assignedShort >> 8U) & 0xFFU),
      acceptedRole,
      nonce,
  };

  uint8_t psdu[127] = {0};
  uint8_t psduLen = 0U;
  const bool built = ZigbeeRadio::buildMacCommandFrameShort(
      g_sequence++, kPanId, destinationShort, kCoordinatorShort, kJoinRspCmdId,
      payload, sizeof(payload), psdu, &psduLen, false);
  if (built) {
    (void)g_zb.transmit(psdu, psduLen, false, 1200000UL);
  }
}

static void handleJoinRequest(const ZigbeeMacCommandView& view, int8_t rssiDbm) {
  if (view.payloadLength < 2U) {
    return;
  }

  const uint8_t requestedRole = view.payload[0];
  const uint8_t nonce = view.payload[1];
  uint16_t assignedShort = 0U;
  const bool assigned = assignNode(view.sourceShort, requestedRole, &assignedShort);
  const uint8_t status = assigned ? 0U : 1U;
  sendJoinResponse(view.sourceShort, nonce, status, assignedShort, requestedRole);

  Serial.print("join src=0x");
  Serial.print(view.sourceShort, HEX);
  Serial.print(" role=");
  Serial.print(roleName(requestedRole));
  Serial.print(" status=");
  Serial.print(status == 0U ? "OK" : "NO_SLOT");
  if (assigned) {
    Serial.print(" assigned=0x");
    Serial.print(assignedShort, HEX);
  }
  Serial.print(" rssi=");
  Serial.print(rssiDbm);
  Serial.print("dBm\r\n");
}

static void sendAppAck(uint16_t destinationShort, uint8_t seq) {
  uint8_t payload[4] = {'A', 'C', 'K', seq};
  uint8_t psdu[127] = {0};
  uint8_t psduLen = 0U;
  const bool built = ZigbeeRadio::buildDataFrameShort(
      g_sequence++, kPanId, destinationShort, kCoordinatorShort, payload,
      sizeof(payload), psdu, &psduLen, false);
  if (built) {
    (void)g_zb.transmit(psdu, psduLen, false, 1200000UL);
  }
}

static void handleAppData(const ZigbeeDataFrameView& view, int8_t rssiDbm) {
  if (view.payloadLength < 5U) {
    return;
  }

  if (!((view.payload[0] == 'T' && view.payload[1] == 'E' && view.payload[2] == 'L') ||
        (view.payload[0] == 'R' && view.payload[1] == 'T' && view.payload[2] == 'R'))) {
    return;
  }

  NodeEntry* node = findNode(view.sourceShort);
  if (node == nullptr) {
    Serial.print("app_drop src=0x");
    Serial.print(view.sourceShort, HEX);
    Serial.print(" reason=not_joined\\r\\n");
    return;
  }

  ++g_appRx;
  const uint8_t appSeq = view.payload[3];
  const uint8_t sample = view.payload[4];
  const int32_t mm = estimateDistanceMm(rssiDbm);
  char kind[4] = {
      static_cast<char>(view.payload[0]),
      static_cast<char>(view.payload[1]),
      static_cast<char>(view.payload[2]),
      '\0',
  };

  node->lastSeenMs = millis();

  sendAppAck(view.sourceShort, appSeq);

  Serial.print("app src=0x");
  Serial.print(view.sourceShort, HEX);
  Serial.print(" kind=");
  Serial.print(kind);
  Serial.print(" seq=");
  Serial.print(appSeq);
  Serial.print(" sample=");
  Serial.print(sample);
  Serial.print(" rssi=");
  Serial.print(rssiDbm);
  Serial.print("dBm");
  if (mm > 0) {
    Serial.print(" dist_cm=");
    Serial.print(mm / 10);
    Serial.print(" dist_mm=");
    Serial.print(mm);
  }
  Serial.print("\r\n");
}

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nZigbeeCoordinator start\r\n");
  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  const bool ok = g_zb.begin(kChannel, 8);
  Serial.print("zigbee_phy_init=");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print(" channel=");
  Serial.print(kChannel);
  Serial.print(" pan=0x");
  Serial.print(kPanId, HEX);
  Serial.print(" short=0x");
  Serial.print(kCoordinatorShort, HEX);
  Serial.print("\r\n");
}

void loop() {
  ZigbeeFrame frame{};
  if (g_zb.receive(&frame, 9000U, 1000000UL)) {
    ++g_rxFrames;

    ZigbeeMacCommandView cmdView{};
    if (ZigbeeRadio::parseMacCommandFrameShort(frame.psdu, frame.length, &cmdView) &&
        cmdView.valid && cmdView.panId == kPanId &&
        cmdView.destinationShort == kCoordinatorShort &&
        cmdView.commandId == kJoinReqCmdId) {
      handleJoinRequest(cmdView, frame.rssiDbm);
      Gpio::write(kPinUserLed, false);
    }

    ZigbeeDataFrameView dataView{};
    if (ZigbeeRadio::parseDataFrameShort(frame.psdu, frame.length, &dataView) &&
        dataView.valid && dataView.panId == kPanId &&
        dataView.destinationShort == kCoordinatorShort) {
      handleAppData(dataView, frame.rssiDbm);
      Gpio::write(kPinUserLed, false);
    }
  }

  const uint32_t now = millis();
  if ((now - g_lastStatusMs) >= 3000U) {
    g_lastStatusMs = now;
    uint8_t active = 0U;
    for (size_t i = 0; i < (sizeof(g_nodes) / sizeof(g_nodes[0])); ++i) {
      if (g_nodes[i].used) {
        ++active;
      }
    }

    Serial.print("t=");
    Serial.print(now);
    Serial.print(" rx=");
    Serial.print(g_rxFrames);
    Serial.print(" joins=");
    Serial.print(g_joinAccepted);
    Serial.print(" app=");
    Serial.print(g_appRx);
    Serial.print(" nodes=");
    Serial.print(active);
    Serial.print("\r\n");
    Gpio::write(kPinUserLed, true);
  }

  delay(1);
}
