#include <Arduino.h>
#include <string.h>

#include "nrf54l15_hal.h"
#include "zigbee_commissioning.h"
#include "zigbee_security.h"
#include "zigbee_stack.h"

#if defined(NRF54L15_CLEAN_ZIGBEE_ENABLED) && (NRF54L15_CLEAN_ZIGBEE_ENABLED == 0)
#error "Enable Tools > Zigbee Support to build ZigbeeHaCoordinatorJoinDemo."
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_CHANNEL
#define NRF54L15_CLEAN_ZIGBEE_CHANNEL 15
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_PAN_ID
#define NRF54L15_CLEAN_ZIGBEE_PAN_ID 0x1234
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_ALLOW_WELL_KNOWN_LINK_KEY
#define NRF54L15_CLEAN_ZIGBEE_ALLOW_WELL_KNOWN_LINK_KEY 1
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_COORDINATOR_IEEE
#define NRF54L15_CLEAN_ZIGBEE_COORDINATOR_IEEE 0x00124B000054A11FULL
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_EXTENDED_PAN_ID
#define NRF54L15_CLEAN_ZIGBEE_EXTENDED_PAN_ID 0x00124B000054C0DEULL
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_ONOFF_LIGHT_IEEE
#define NRF54L15_CLEAN_ZIGBEE_ONOFF_LIGHT_IEEE 0x00124B0001AC1001ULL
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_ONOFF_LIGHT_INSTALL_CODE_BYTES
#define NRF54L15_CLEAN_ZIGBEE_ONOFF_LIGHT_INSTALL_CODE_BYTES                    \
  0x10U, 0xACU, 0x01U, 0x01U, 0x24U, 0x4BU, 0x00U, 0xA1U, 0xB2U, 0xC3U, 0xD4U, \
      0xE5U, 0xF6U, 0x07U, 0x18U, 0x29U, 0x43U, 0x6AU
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_DIMMABLE_LIGHT_IEEE
#define NRF54L15_CLEAN_ZIGBEE_DIMMABLE_LIGHT_IEEE 0x00124B0001AC1002ULL
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_DIMMABLE_LIGHT_INSTALL_CODE_BYTES
#define NRF54L15_CLEAN_ZIGBEE_DIMMABLE_LIGHT_INSTALL_CODE_BYTES                 \
  0x10U, 0xACU, 0x02U, 0x01U, 0x24U, 0x4BU, 0x00U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, \
      0xEEU, 0xFFU, 0x11U, 0x22U, 0x33U, 0xFEU, 0xC9U
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_TEMPERATURE_SENSOR_IEEE
#define NRF54L15_CLEAN_ZIGBEE_TEMPERATURE_SENSOR_IEEE 0x00124B0001AC2001ULL
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_TEMPERATURE_SENSOR_INSTALL_CODE_BYTES
#define NRF54L15_CLEAN_ZIGBEE_TEMPERATURE_SENSOR_INSTALL_CODE_BYTES             \
  0x10U, 0xACU, 0x03U, 0x01U, 0x24U, 0x4BU, 0x00U, 0xCAU, 0xFEU, 0xBAU, 0xBEU, \
      0x10U, 0x21U, 0x32U, 0x43U, 0x54U, 0xDCU, 0xB9U
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_REQUIRE_UNIQUE_LINK_KEY_FOR_REJOIN
#define NRF54L15_CLEAN_ZIGBEE_REQUIRE_UNIQUE_LINK_KEY_FOR_REJOIN 1
#endif

#ifndef NRF54L15_CLEAN_ZIGBEE_TX_DEBUG
#define NRF54L15_CLEAN_ZIGBEE_TX_DEBUG 0
#endif

using namespace xiao_nrf54l15;

namespace {

static ZigbeeRadio g_radio;
static uint8_t g_macSequence = 1U;
static uint8_t g_nwkSequence = 1U;
static uint32_t g_nwkSecurityFrameCounter = 1U;
static uint32_t g_apsTrustCenterFrameCounter = 1U;
static uint8_t g_apsCounter = 1U;
static uint8_t g_zclSequence = 1U;
static uint8_t g_zdoSequence = 1U;
static uint32_t g_lastBeaconMs = 0U;
static uint32_t g_lastStatusMs = 0U;
static uint32_t g_permitJoinDeadlineMs = 0U;
static bool g_permitJoinEnabled = true;

static constexpr uint8_t kChannel =
    static_cast<uint8_t>(NRF54L15_CLEAN_ZIGBEE_CHANNEL);
static constexpr uint16_t kPanId =
    static_cast<uint16_t>(NRF54L15_CLEAN_ZIGBEE_PAN_ID);
static constexpr uint16_t kCoordinatorShort = 0x0000U;
static constexpr uint8_t kCoordinatorEndpoint = 1U;
static constexpr uint64_t kCoordinatorIeee =
    static_cast<uint64_t>(NRF54L15_CLEAN_ZIGBEE_COORDINATOR_IEEE);
static constexpr uint64_t kExtendedPanId =
    static_cast<uint64_t>(NRF54L15_CLEAN_ZIGBEE_EXTENDED_PAN_ID);
static uint8_t g_activeNetworkKeySequence = 0x01U;
static uint8_t g_activeNetworkKey[16] = {0xA1U, 0xB2U, 0xC3U, 0xD4U,
                                         0xE5U, 0xF6U, 0x07U, 0x18U,
                                         0x29U, 0x3AU, 0x4BU, 0x5CU,
                                         0x6DU, 0x7EU, 0x8FU, 0x90U};
static uint8_t g_alternateNetworkKeySequence = 0U;
static uint8_t g_alternateNetworkKey[16] = {0U};
static bool g_haveAlternateNetworkKey = false;

#if NRF54L15_CLEAN_ZIGBEE_TX_DEBUG != 0
void printLastRadioTransmitDebug(const char* prefix) {
  const ZigbeeTransmitDebug debug = g_radio.lastTransmitDebug();
  Serial.print(prefix);
  Serial.print(" tx_len=");
  Serial.print(debug.txLength);
  Serial.print(" end=");
  Serial.print(debug.endSeen ? "yes" : "no");
  Serial.print(" disabled=");
  Serial.print(debug.disabledSeen ? "yes" : "no");
  Serial.print(" ack_req=");
  Serial.print(debug.ackRequested ? "yes" : "no");
  Serial.print(" ack_rx=");
  Serial.print(debug.ackReceived ? "yes" : "no");
  Serial.print(" ack_seq=0x");
  Serial.print(debug.ackSequence, HEX);
  Serial.print(" rx_len=");
  Serial.print(debug.rxLength);
  Serial.print(" rx_seq=0x");
  Serial.print(debug.rxSequence, HEX);
  Serial.print("\r\n");
}
#endif
static constexpr uint8_t kMaxNodes = 8U;
static constexpr uint8_t kPendingPayloadMax = 96U;
static constexpr uint8_t kZclCommandWriteAttributesResponse = 0x04U;
static constexpr uint8_t kZclCommandConfigureReportingResponse = 0x07U;
static constexpr uint8_t kZclCommandReadReportingConfigurationResponse = 0x09U;
static constexpr uint8_t kZclCommandReportAttributes = 0x0AU;
static constexpr uint8_t kZclCommandDefaultResponse = 0x0BU;
static constexpr uint8_t kZclCommandDiscoverAttributesExtendedResponse = 0x16U;
static constexpr uint8_t kOnOffCommandOff = 0x00U;
static constexpr uint8_t kOnOffCommandOn = 0x01U;
static constexpr uint8_t kOnOffCommandToggle = 0x02U;
static constexpr uint8_t kIdentifyCommandIdentify = 0x00U;
static constexpr uint8_t kIdentifyCommandIdentifyQuery = 0x01U;
static constexpr uint8_t kIdentifyCommandTriggerEffect = 0x40U;
static constexpr uint8_t kIdentifyEffectBlink = 0x00U;
static constexpr uint8_t kIdentifyEffectBreathe = 0x01U;
static constexpr uint8_t kIdentifyEffectChannelChange = 0x0BU;
static constexpr uint8_t kIdentifyEffectStopEffect = 0xFFU;
static constexpr uint8_t kGroupsCommandAddGroup = 0x00U;
static constexpr uint8_t kLevelControlCommandMoveToLevelWithOnOff = 0x04U;
static constexpr uint8_t kLevelControlCommandStepWithOnOff = 0x06U;
static constexpr uint16_t kDemoGroupId = 0x1001U;
static constexpr uint32_t kPermitJoinWindowMs = 120000UL;

struct PreconfiguredNodePolicy {
  uint64_t ieeeAddress;
  uint8_t installCode[18];
};

static const PreconfiguredNodePolicy kPreconfiguredNodePolicies[] = {
    {static_cast<uint64_t>(NRF54L15_CLEAN_ZIGBEE_ONOFF_LIGHT_IEEE),
     {NRF54L15_CLEAN_ZIGBEE_ONOFF_LIGHT_INSTALL_CODE_BYTES}},
    {static_cast<uint64_t>(NRF54L15_CLEAN_ZIGBEE_DIMMABLE_LIGHT_IEEE),
     {NRF54L15_CLEAN_ZIGBEE_DIMMABLE_LIGHT_INSTALL_CODE_BYTES}},
    {static_cast<uint64_t>(NRF54L15_CLEAN_ZIGBEE_TEMPERATURE_SENSOR_IEEE),
     {NRF54L15_CLEAN_ZIGBEE_TEMPERATURE_SENSOR_INSTALL_CODE_BYTES}},
};

enum class NodeStage : uint8_t {
  kIdle = 0U,
  kAwaitingNodeDescriptor = 1U,
  kAwaitingPowerDescriptor = 2U,
  kAwaitingActiveEndpoints = 3U,
  kAwaitingSimpleDescriptor = 4U,
  kAwaitingBasicRead = 5U,
  kAwaitingReporting = 6U,
  kReady = 7U,
};

enum class TrustCenterNodeState : uint8_t {
  kIdle = 0U,
  kWaitingTransportKey = 1U,
  kJoined = 2U,
  kWaitingUpdateDevice = 3U,
  kAlternateKeyStaged = 4U,
};

struct PendingApsFrame {
  bool used = false;
  uint8_t deliveryMode = kZigbeeApsDeliveryUnicast;
  uint16_t destinationGroup = 0U;
  uint16_t clusterId = 0U;
  uint16_t profileId = 0U;
  uint8_t destinationEndpoint = 0U;
  uint8_t sourceEndpoint = 0U;
  uint8_t payloadLength = 0U;
  uint8_t payload[kPendingPayloadMax] = {0U};
};

struct PendingMacFrame {
  bool used = false;
  uint8_t length = 0U;
  uint8_t psdu[127] = {0U};
};

struct PendingApsAck {
  bool active = false;
  uint8_t counter = 0U;
  uint16_t clusterId = 0U;
  uint16_t profileId = 0U;
  uint8_t destinationEndpoint = 0U;
  uint8_t sourceEndpoint = 0U;
  uint8_t retriesRemaining = 0U;
  uint32_t deadlineMs = 0U;
  PendingApsFrame frame{};
};

struct RecentInboundAps {
  bool valid = false;
  uint8_t counter = 0U;
  uint16_t clusterId = 0U;
  uint16_t profileId = 0U;
  uint8_t destinationEndpoint = 0U;
  uint8_t sourceEndpoint = 0U;
  uint8_t deliveryMode = 0U;
  uint32_t expiresMs = 0U;
};

static constexpr uint8_t kPendingApsAckSlots = 3U;
static constexpr size_t kNodeTextMax = 33U;

struct NodeEntry {
  bool used = false;
  uint64_t ieeeAddress = 0U;
  uint16_t shortAddress = 0U;
  uint32_t lastSeenMs = 0U;
  bool secureNwkSeen = false;
  uint32_t lastInboundSecurityFrameCounter = 0U;
  uint8_t currentNetworkKeySequence = 0U;
  bool pendingTransportKey = false;
  bool pendingNetworkKeyUpdate = false;
  bool pendingSwitchKey = false;
  bool pendingLeaveWithRejoin = false;
  bool pendingSecureRejoin = false;
  bool pendingAssociationResponse = false;
  bool pendingNwkResponse = false;
  uint16_t pendingAssignedShort = 0U;
  uint8_t pendingAssociationStatus = 0U;
  PendingApsFrame pending{};
  PendingMacFrame pendingMac{};
  PendingApsAck pendingApsAcks[kPendingApsAckSlots]{};
  RecentInboundAps recentInboundAps{};
  bool announced = false;
  bool haveNodeDescriptor = false;
  uint8_t logicalType = 0U;
  uint8_t macCapabilityFlags = 0U;
  uint16_t manufacturerCode = 0U;
  bool havePowerDescriptor = false;
  uint8_t availablePowerSources = 0U;
  uint8_t currentPowerSource = 0U;
  uint8_t currentPowerSourceLevel = 0U;
  bool haveBasicZclVersion = false;
  uint8_t basicZclVersion = 0U;
  bool haveBasicApplicationVersion = false;
  uint8_t basicApplicationVersion = 0U;
  bool haveBasicStackVersion = false;
  uint8_t basicStackVersion = 0U;
  bool haveBasicHwVersion = false;
  uint8_t basicHwVersion = 0U;
  bool haveBasicPowerSource = false;
  uint8_t basicPowerSource = 0U;
  char basicManufacturerName[kNodeTextMax] = {0};
  char basicModelIdentifier[kNodeTextMax] = {0};
  char basicSwBuildId[kNodeTextMax] = {0};
  uint8_t endpoint = 0U;
  uint16_t profileId = 0U;
  uint16_t deviceId = 0U;
  bool supportsOnOff = false;
  bool supportsLevelControl = false;
  bool supportsIdentify = false;
  bool supportsTemperature = false;
  bool supportsPowerConfiguration = false;
  bool haveIdentifyTime = false;
  uint16_t identifyTimeSeconds = 0U;
  bool basicRead = false;
  bool onOffBindingConfigured = false;
  bool levelBindingConfigured = false;
  bool temperatureBindingConfigured = false;
  bool powerBindingConfigured = false;
  bool onOffReportingConfigured = false;
  bool levelReportingConfigured = false;
  bool temperatureReportingConfigured = false;
  bool powerReportingConfigured = false;
  bool onOffReportingVerified = false;
  bool levelReportingVerified = false;
  bool temperatureReportingVerified = false;
  bool powerReportingVerified = false;
  bool awaitingBindResponse = false;
  uint16_t awaitingClusterId = 0U;
  bool haveOnOffState = false;
  bool onOffState = false;
  bool haveLevelState = false;
  uint8_t levelState = 0U;
  bool haveTemperatureState = false;
  int16_t temperatureCentiDegrees = 0;
  bool haveBatteryVoltage = false;
  uint8_t batteryVoltageDecivolts = 0U;
  bool haveBatteryPercentage = false;
  uint8_t batteryPercentageRemainingHalf = 0U;
  bool demoGroupConfigured = false;
  uint8_t endDeviceTimeoutIndex = 0U;
  uint8_t endDeviceConfiguration = 0U;
  uint8_t parentInformation = 0U;
  bool endDeviceTimeoutNegotiated = false;
  ZigbeePreconfiguredKeyMode preconfiguredKeyMode =
      ZigbeePreconfiguredKeyMode::kNone;
  TrustCenterNodeState trustCenterState = TrustCenterNodeState::kIdle;
  NodeStage stage = NodeStage::kIdle;
  uint32_t stageDeadlineMs = 0U;
  uint8_t stageRetriesRemaining = 0U;
  uint8_t basicReadBatchIndex = 0U;
};

static constexpr uint32_t kApsAckTimeoutMs = 900U;
static constexpr uint32_t kRecentInboundApsWindowMs = 4000U;
static constexpr uint8_t kApsAckRetryLimit = 2U;
static constexpr uint32_t kInterviewRetryDelayMs = 1500UL;
static constexpr uint8_t kInterviewRetryLimit = 4U;
static constexpr uint32_t kPollFollowUpListenBudgetUs = 12000UL;

void processIncomingFrame(const ZigbeeFrame& frame);
void pumpImmediateResponseWindow(uint32_t listenBudgetUs);

bool sendApsFrameExtendedWithCounter(uint16_t destinationShort,
                                     uint8_t deliveryMode,
                                     uint16_t destinationGroup,
                                     uint8_t destinationEndpoint,
                                     uint16_t clusterId, uint16_t profileId,
                                     uint8_t sourceEndpoint,
                                     const uint8_t* payload,
                                     uint8_t payloadLength, uint8_t apsCounter,
                                     bool trackAck);
bool buildTransportKeyPsdu(NodeEntry* node, uint8_t* outPsdu,
                           uint8_t* outPsduLength, uint8_t* outKeySequence,
                           bool* outNwkSecurityEnabled);
bool buildUpdateDevicePsdu(NodeEntry* node, uint8_t* outPsdu,
                           uint8_t* outPsduLength);

static NodeEntry g_nodes[kMaxNodes] = {};
static uint16_t g_nextShortAddress = 0x1000U;

NodeEntry* findNodeByIeee(uint64_t ieeeAddress) {
  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    if (g_nodes[i].used && g_nodes[i].ieeeAddress == ieeeAddress) {
      return &g_nodes[i];
    }
  }
  return nullptr;
}

NodeEntry* findNodeByShort(uint16_t shortAddress) {
  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    if (g_nodes[i].used && g_nodes[i].shortAddress == shortAddress) {
      return &g_nodes[i];
    }
  }
  return nullptr;
}

NodeEntry* allocateNode(uint64_t ieeeAddress) {
  NodeEntry* existing = findNodeByIeee(ieeeAddress);
  if (existing != nullptr) {
    return existing;
  }

  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    if (!g_nodes[i].used) {
      memset(&g_nodes[i], 0, sizeof(g_nodes[i]));
      g_nodes[i].used = true;
      g_nodes[i].ieeeAddress = ieeeAddress;
      return &g_nodes[i];
    }
  }
  return nullptr;
}

uint16_t allocateShortAddress() {
  while (findNodeByShort(g_nextShortAddress) != nullptr ||
         g_nextShortAddress == 0x0000U || g_nextShortAddress == 0xFFFFU) {
    ++g_nextShortAddress;
  }
  return g_nextShortAddress++;
}

const char* nodeStageName(NodeStage stage) {
  switch (stage) {
    case NodeStage::kIdle:
      return "idle";
    case NodeStage::kAwaitingNodeDescriptor:
      return "node_desc";
    case NodeStage::kAwaitingPowerDescriptor:
      return "power_desc";
    case NodeStage::kAwaitingActiveEndpoints:
      return "active_ep";
    case NodeStage::kAwaitingSimpleDescriptor:
      return "simple_desc";
    case NodeStage::kAwaitingBasicRead:
      return "basic_read";
    case NodeStage::kAwaitingReporting:
      return "reporting";
    case NodeStage::kReady:
      return "ready";
  }
  return "unknown";
}

void markInterviewStage(NodeEntry* node, NodeStage stage) {
  if (node == nullptr) {
    return;
  }
  if (node->stage != stage) {
    node->stageRetriesRemaining = kInterviewRetryLimit;
  }
  node->stage = stage;
  node->stageDeadlineMs = millis() + kInterviewRetryDelayMs;
}

const uint8_t* findInstallCodeForNode(uint64_t ieeeAddress) {
  for (uint8_t i = 0U;
       i < static_cast<uint8_t>(sizeof(kPreconfiguredNodePolicies) /
                                sizeof(kPreconfiguredNodePolicies[0]));
       ++i) {
    if (kPreconfiguredNodePolicies[i].ieeeAddress == 0U) {
      continue;
    }
    if (kPreconfiguredNodePolicies[i].ieeeAddress == ieeeAddress) {
      return kPreconfiguredNodePolicies[i].installCode;
    }
  }
  return nullptr;
}

bool resolveTrustCenterLinkKey(NodeEntry* node, uint8_t outKey[16],
                               ZigbeePreconfiguredKeyMode* outMode) {
  if (node == nullptr || outKey == nullptr || outMode == nullptr) {
    return false;
  }

  const uint8_t* installCode = findInstallCodeForNode(node->ieeeAddress);
  if (installCode != nullptr &&
      ZigbeeSecurity::deriveInstallCodeLinkKey(installCode, 18U, outKey)) {
    *outMode = ZigbeePreconfiguredKeyMode::kInstallCodeDerived;
    return true;
  }

#if !NRF54L15_CLEAN_ZIGBEE_ALLOW_WELL_KNOWN_LINK_KEY
  return false;
#else
  if (!ZigbeeSecurity::loadZigbeeAlliance09LinkKey(outKey)) {
    return false;
  }
  *outMode = ZigbeePreconfiguredKeyMode::kWellKnown;
  return true;
#endif
}

bool secureRejoinAllowed(const NodeEntry* node) {
  if (node == nullptr) {
    return false;
  }
#if NRF54L15_CLEAN_ZIGBEE_REQUIRE_UNIQUE_LINK_KEY_FOR_REJOIN
  return ZigbeeCommissioning::isUniqueLinkKeyMode(node->preconfiguredKeyMode);
#else
  return node->preconfiguredKeyMode != ZigbeePreconfiguredKeyMode::kNone;
#endif
}

const char* trustCenterStateName(TrustCenterNodeState state) {
  switch (state) {
    case TrustCenterNodeState::kWaitingTransportKey:
      return "waiting_transport_key";
    case TrustCenterNodeState::kJoined:
      return "joined";
    case TrustCenterNodeState::kWaitingUpdateDevice:
      return "waiting_update_device";
    case TrustCenterNodeState::kAlternateKeyStaged:
      return "alternate_key_staged";
    case TrustCenterNodeState::kIdle:
    default:
      return "idle";
  }
}

const uint8_t* keyForSequence(uint8_t keySequence) {
  if (keySequence == g_activeNetworkKeySequence) {
    return g_activeNetworkKey;
  }
  if (g_haveAlternateNetworkKey &&
      keySequence == g_alternateNetworkKeySequence) {
    return g_alternateNetworkKey;
  }
  return nullptr;
}

void clearAlternateNetworkKey() {
  memset(g_alternateNetworkKey, 0, sizeof(g_alternateNetworkKey));
  g_alternateNetworkKeySequence = 0U;
  g_haveAlternateNetworkKey = false;
}

bool deriveAlternateNetworkKey() {
  if (g_haveAlternateNetworkKey) {
    return true;
  }

  const uint8_t nextSequence =
      (g_activeNetworkKeySequence == 0xFFU)
          ? 0x01U
          : static_cast<uint8_t>(g_activeNetworkKeySequence + 1U);
  for (uint8_t i = 0U; i < sizeof(g_alternateNetworkKey); ++i) {
    g_alternateNetworkKey[i] =
        static_cast<uint8_t>(g_activeNetworkKey[i] ^
                             static_cast<uint8_t>(0x5AU + nextSequence +
                                                  (i * 3U)));
  }
  if (memcmp(g_alternateNetworkKey, g_activeNetworkKey,
             sizeof(g_alternateNetworkKey)) == 0) {
    g_alternateNetworkKey[0] ^= 0xA5U;
  }
  g_alternateNetworkKeySequence = nextSequence;
  g_haveAlternateNetworkKey = true;
  return true;
}

void maybePromoteAlternateNetworkKey() {
  if (!g_haveAlternateNetworkKey) {
    return;
  }

  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    if (!g_nodes[i].used || g_nodes[i].shortAddress == 0U) {
      continue;
    }
    if (g_nodes[i].currentNetworkKeySequence != g_alternateNetworkKeySequence) {
      return;
    }
  }

  memcpy(g_activeNetworkKey, g_alternateNetworkKey, sizeof(g_activeNetworkKey));
  g_activeNetworkKeySequence = g_alternateNetworkKeySequence;
  clearAlternateNetworkKey();
  Serial.print("nwk_key_promoted seq=");
  Serial.print(g_activeNetworkKeySequence);
  Serial.print("\r\n");
}

bool sendPsdu(const uint8_t* psdu, uint8_t length) {
  const bool sent = g_radio.transmit(psdu, length, false, 1200000UL);
#if NRF54L15_CLEAN_ZIGBEE_TX_DEBUG != 0
  if (!sent) {
    printLastRadioTransmitDebug("mac_tx_fail");
  }
#endif
  return sent;
}

bool sendBeacon() {
  ZigbeeMacBeaconPayload payload{};
  payload.valid = true;
  payload.protocolId = 0U;
  payload.stackProfile = 2U;
  payload.protocolVersion = 2U;
  payload.panCoordinator = true;
  payload.associationPermit = g_permitJoinEnabled;
  payload.routerCapacity = true;
  payload.endDeviceCapacity = true;
  payload.extendedPanId = kExtendedPanId;
  payload.txOffset = 0x00FFFFFFUL;
  payload.updateId = 0U;

  uint8_t frame[127] = {0U};
  uint8_t length = 0U;
  if (!ZigbeeCodec::buildBeaconFrame(g_macSequence++, kPanId,
                                     kCoordinatorShort, payload, frame,
                                     &length)) {
    return false;
  }
  return sendPsdu(frame, length);
}

bool sendCoordinatorRealignment(NodeEntry* node) {
  if (node == nullptr || node->ieeeAddress == 0U || node->shortAddress == 0U) {
    return false;
  }

  uint8_t frame[127] = {0U};
  uint8_t length = 0U;
  if (!ZigbeeCodec::buildCoordinatorRealignment(
          g_macSequence++, kPanId, kCoordinatorShort, kChannel,
          node->shortAddress, node->ieeeAddress, frame, &length)) {
    return false;
  }
  return sendPsdu(frame, length);
}

bool queuePendingApsFrameExtended(NodeEntry* node, uint8_t deliveryMode,
                                  uint16_t destinationGroup,
                                  uint16_t clusterId, uint16_t profileId,
                                  uint8_t destinationEndpoint,
                                  uint8_t sourceEndpoint,
                                  const uint8_t* payload,
                                  uint8_t payloadLength) {
  if (node == nullptr) {
    return false;
  }
  if (payloadLength > 0U && payload == nullptr) {
    return false;
  }
  if (node->pendingAssociationResponse || node->pendingTransportKey ||
      node->pendingSwitchKey || node->pending.used ||
      payloadLength > kPendingPayloadMax) {
    return false;
  }

  node->pending.used = true;
  node->pending.deliveryMode = deliveryMode;
  node->pending.destinationGroup = destinationGroup;
  node->pending.clusterId = clusterId;
  node->pending.profileId = profileId;
  node->pending.destinationEndpoint = destinationEndpoint;
  node->pending.sourceEndpoint = sourceEndpoint;
  node->pending.payloadLength = payloadLength;
  if (payloadLength > 0U) {
    memcpy(node->pending.payload, payload, payloadLength);
  }
  return true;
}

bool queuePendingApsFrame(NodeEntry* node, uint16_t clusterId,
                          uint16_t profileId, uint8_t destinationEndpoint,
                          uint8_t sourceEndpoint, const uint8_t* payload,
                          uint8_t payloadLength) {
  return queuePendingApsFrameExtended(
      node, kZigbeeApsDeliveryUnicast, 0U, clusterId, profileId,
      destinationEndpoint, sourceEndpoint, payload, payloadLength);
}

void clearPendingApsAck(NodeEntry* node) {
  if (node == nullptr) {
    return;
  }
  memset(node->pendingApsAcks, 0, sizeof(node->pendingApsAcks));
}

void clearPendingApsAckSlot(NodeEntry* node, uint8_t slot) {
  if (node == nullptr || slot >= kPendingApsAckSlots) {
    return;
  }
  memset(&node->pendingApsAcks[slot], 0, sizeof(node->pendingApsAcks[slot]));
}

bool nodeHasPendingApsAck(const NodeEntry& node) {
  for (uint8_t i = 0U; i < kPendingApsAckSlots; ++i) {
    if (node.pendingApsAcks[i].active) {
      return true;
    }
  }
  return false;
}

bool matchesPendingApsAck(const PendingApsAck& pending,
                          const ZigbeeApsAcknowledgementFrame& ack) {
  return pending.active && ack.valid && !ack.ackFormatCommand &&
         ack.counter == pending.counter &&
         ack.clusterId == pending.clusterId &&
         ack.profileId == pending.profileId &&
         ack.destinationEndpoint == pending.sourceEndpoint &&
         ack.sourceEndpoint == pending.destinationEndpoint;
}

bool findPendingApsAckSlot(const NodeEntry& node,
                           const ZigbeeApsAcknowledgementFrame& ack,
                           uint8_t* outSlot) {
  if (outSlot == nullptr) {
    return false;
  }

  for (uint8_t i = 0U; i < kPendingApsAckSlots; ++i) {
    if (matchesPendingApsAck(node.pendingApsAcks[i], ack)) {
      *outSlot = i;
      return true;
    }
  }
  return false;
}

bool pendingApsAckSlotAvailable(const NodeEntry& node, uint32_t nowMs) {
  for (uint8_t i = 0U; i < kPendingApsAckSlots; ++i) {
    if (!node.pendingApsAcks[i].active ||
        static_cast<int32_t>(nowMs - node.pendingApsAcks[i].deadlineMs) >= 0) {
      return true;
    }
  }
  return false;
}

bool resendPendingApsFrame(NodeEntry* node, uint8_t slot);

bool allocatePendingApsAckSlot(NodeEntry* node,
                               const ZigbeeApsDataFrame& aps,
                               uint8_t* outSlot) {
  if (node == nullptr || outSlot == nullptr) {
    return false;
  }

  const uint32_t nowMs = millis();
  uint8_t oldestActiveSlot = 0U;
  bool haveOldestActiveSlot = false;
  for (uint8_t i = 0U; i < kPendingApsAckSlots; ++i) {
    PendingApsAck& pending = node->pendingApsAcks[i];
    if (pending.active && pending.counter == aps.counter &&
        pending.clusterId == aps.clusterId &&
        pending.profileId == aps.profileId &&
        pending.destinationEndpoint == aps.destinationEndpoint &&
        pending.sourceEndpoint == aps.sourceEndpoint) {
      *outSlot = i;
      return true;
    }
    if (!pending.active ||
        static_cast<int32_t>(nowMs - pending.deadlineMs) >= 0) {
      *outSlot = i;
      return true;
    }
    if (!haveOldestActiveSlot ||
        static_cast<int32_t>(pending.deadlineMs -
                             node->pendingApsAcks[oldestActiveSlot].deadlineMs) < 0) {
      oldestActiveSlot = i;
      haveOldestActiveSlot = true;
    }
  }

  if (haveOldestActiveSlot) {
    *outSlot = oldestActiveSlot;
    return true;
  }
  return false;
}

void clearRecentInboundAps(NodeEntry* node) {
  if (node == nullptr) {
    return;
  }
  memset(&node->recentInboundAps, 0, sizeof(node->recentInboundAps));
}

void prepareNodeForRetainedRejoin(NodeEntry* node) {
  if (node == nullptr) {
    return;
  }
  node->pending.used = false;
  node->pending.payloadLength = 0U;
  node->pendingTransportKey = false;
  node->pendingNetworkKeyUpdate = false;
  node->pendingSwitchKey = false;
  node->pendingLeaveWithRejoin = false;
  node->pendingSecureRejoin = false;
  node->pendingAssociationResponse = false;
  node->pendingAssignedShort = 0U;
  node->pendingAssociationStatus = 0U;
  node->secureNwkSeen = false;
  node->lastInboundSecurityFrameCounter = 0U;
  node->trustCenterState = TrustCenterNodeState::kIdle;
  clearPendingApsAck(node);
  clearRecentInboundAps(node);
}

void rememberPendingApsAck(NodeEntry* node, const ZigbeeApsDataFrame& aps,
                           const uint8_t* payload, uint8_t payloadLength) {
  uint8_t slot = 0U;
  if (node == nullptr || aps.deliveryMode != kZigbeeApsDeliveryUnicast ||
      !aps.ackRequested ||
      payloadLength > sizeof(node->pendingApsAcks[0].frame.payload) ||
      (payloadLength > 0U && payload == nullptr)) {
    return;
  }
  if (!allocatePendingApsAckSlot(node, aps, &slot)) {
    return;
  }

  PendingApsAck& pending = node->pendingApsAcks[slot];
  memset(&pending, 0, sizeof(pending));
  pending.active = true;
  pending.counter = aps.counter;
  pending.clusterId = aps.clusterId;
  pending.profileId = aps.profileId;
  pending.destinationEndpoint = aps.destinationEndpoint;
  pending.sourceEndpoint = aps.sourceEndpoint;
  pending.retriesRemaining = kApsAckRetryLimit;
  pending.deadlineMs = millis() + kApsAckTimeoutMs;
  pending.frame.used = true;
  pending.frame.deliveryMode = aps.deliveryMode;
  pending.frame.destinationGroup = aps.destinationGroup;
  pending.frame.clusterId = aps.clusterId;
  pending.frame.profileId = aps.profileId;
  pending.frame.destinationEndpoint = aps.destinationEndpoint;
  pending.frame.sourceEndpoint = aps.sourceEndpoint;
  pending.frame.payloadLength = payloadLength;
  if (payloadLength > 0U) {
    memcpy(pending.frame.payload, payload, payloadLength);
  }
}

void rememberRecentInboundAps(NodeEntry* node, const ZigbeeApsDataFrame& aps) {
  if (node == nullptr) {
    return;
  }
  node->recentInboundAps.valid = true;
  node->recentInboundAps.counter = aps.counter;
  node->recentInboundAps.clusterId = aps.clusterId;
  node->recentInboundAps.profileId = aps.profileId;
  node->recentInboundAps.destinationEndpoint = aps.destinationEndpoint;
  node->recentInboundAps.sourceEndpoint = aps.sourceEndpoint;
  node->recentInboundAps.deliveryMode = aps.deliveryMode;
  node->recentInboundAps.expiresMs = millis() + kRecentInboundApsWindowMs;
}

bool isRecentInboundApsDuplicate(NodeEntry* node, const ZigbeeApsDataFrame& aps,
                                 uint32_t nowMs) {
  if (node == nullptr || !node->recentInboundAps.valid ||
      static_cast<int32_t>(nowMs - node->recentInboundAps.expiresMs) >= 0) {
    clearRecentInboundAps(node);
    return false;
  }
  return node->recentInboundAps.counter == aps.counter &&
         node->recentInboundAps.clusterId == aps.clusterId &&
         node->recentInboundAps.profileId == aps.profileId &&
         node->recentInboundAps.destinationEndpoint == aps.destinationEndpoint &&
         node->recentInboundAps.sourceEndpoint == aps.sourceEndpoint &&
         node->recentInboundAps.deliveryMode == aps.deliveryMode;
}

bool sendNwkCommand(NodeEntry* node, const uint8_t* payload,
                    uint8_t payloadLength);
bool buildNwkCommandPsdu(NodeEntry* node, const uint8_t* payload,
                         uint8_t payloadLength, uint8_t* outPsdu,
                         uint8_t* outPsduLength) {
  if (node == nullptr || node->shortAddress == 0U || payload == nullptr ||
      payloadLength == 0U || outPsdu == nullptr || outPsduLength == nullptr) {
    return false;
  }
  *outPsduLength = 0U;

  const uint8_t keySequence =
      (node->currentNetworkKeySequence != 0U)
          ? node->currentNetworkKeySequence
          : g_activeNetworkKeySequence;
  const uint8_t* networkKey = keyForSequence(keySequence);
  const bool useSecurity =
      (node->secureNwkSeen || node->currentNetworkKeySequence != 0U) &&
      networkKey != nullptr;

  ZigbeeNetworkFrame nwk{};
  nwk.frameType = ZigbeeNwkFrameType::kCommand;
  nwk.securityEnabled = useSecurity;
  nwk.destinationShort = node->shortAddress;
  nwk.sourceShort = kCoordinatorShort;
  nwk.radius = 30U;
  nwk.sequence = g_nwkSequence++;

  uint8_t nwkFrame[127] = {0U};
  uint8_t nwkLength = 0U;
  if (useSecurity) {
    ZigbeeNwkSecurityHeader security{};
    security.valid = true;
    security.securityControl = kZigbeeSecurityControlNwkEncMic32;
    security.frameCounter = g_nwkSecurityFrameCounter++;
    security.sourceIeee = kCoordinatorIeee;
    security.keySequence = keySequence;
    if (!ZigbeeSecurity::buildSecuredNwkFrame(nwk, security, networkKey,
                                              payload, payloadLength, nwkFrame,
                                              &nwkLength)) {
      return false;
    }
  } else if (!ZigbeeCodec::buildNwkFrame(nwk, payload, payloadLength, nwkFrame,
                                         &nwkLength)) {
    return false;
  }

  uint8_t psdu[127] = {0U};
  if (!ZigbeeRadio::buildDataFrameShort(g_macSequence++, kPanId,
                                        node->shortAddress, kCoordinatorShort,
                                        nwkFrame, nwkLength, outPsdu,
                                        outPsduLength, false)) {
    return false;
  }
  return true;
}

bool sendNwkCommand(NodeEntry* node, const uint8_t* payload,
                    uint8_t payloadLength) {
  uint8_t psdu[127] = {0U};
  uint8_t psduLength = 0U;
  if (!buildNwkCommandPsdu(node, payload, payloadLength, psdu, &psduLength)) {
    return false;
  }
  return sendPsdu(psdu, psduLength);
}

bool queuePendingNwkCommand(NodeEntry* node, const uint8_t* payload,
                            uint8_t payloadLength) {
  if (node == nullptr || payload == nullptr || payloadLength == 0U) {
    return false;
  }
  if (!buildNwkCommandPsdu(node, payload, payloadLength, node->pendingMac.psdu,
                           &node->pendingMac.length)) {
    return false;
  }
  node->pendingMac.used = true;
  node->pendingNwkResponse = true;
  return true;
}

void maybeExpirePendingApsAck(NodeEntry* node, uint32_t nowMs) {
  if (node == nullptr) {
    return;
  }

  for (uint8_t i = 0U; i < kPendingApsAckSlots; ++i) {
    PendingApsAck& pending = node->pendingApsAcks[i];
    if (!pending.active ||
        static_cast<int32_t>(nowMs - pending.deadlineMs) < 0) {
      continue;
    }
    if (pending.retriesRemaining > 0U && pending.frame.used) {
      --pending.retriesRemaining;
      const bool resent = resendPendingApsFrame(node, i);
      if (resent) {
        Serial.print("aps_ack retry short=0x");
        Serial.print(node->shortAddress, HEX);
        Serial.print(" ctr=0x");
        Serial.print(pending.counter, HEX);
        Serial.print(" remaining=");
        Serial.print(pending.retriesRemaining);
        Serial.print("\r\n");
        continue;
      }
    }

    Serial.print("aps_ack miss short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" ctr=0x");
    Serial.print(pending.counter, HEX);
    Serial.print(" cluster=0x");
    Serial.print(pending.clusterId, HEX);
    Serial.print("\r\n");
    clearPendingApsAckSlot(node, i);
  }
}

bool sendApsFrameExtendedWithCounter(uint16_t destinationShort,
                                     uint8_t deliveryMode,
                                     uint16_t destinationGroup,
                                     uint8_t destinationEndpoint,
                                     uint16_t clusterId, uint16_t profileId,
                                     uint8_t sourceEndpoint,
                                     const uint8_t* payload,
                                     uint8_t payloadLength, uint8_t apsCounter,
                                     bool trackAck) {
  NodeEntry* node = findNodeByShort(destinationShort);
  if (trackAck && deliveryMode == kZigbeeApsDeliveryUnicast &&
      (node == nullptr || !pendingApsAckSlotAvailable(*node, millis()))) {
    return false;
  }
  const uint8_t keySequence =
      (node != nullptr && node->currentNetworkKeySequence != 0U)
          ? node->currentNetworkKeySequence
          : g_activeNetworkKeySequence;
  const uint8_t* networkKey = keyForSequence(keySequence);
  const bool useSecurity =
      (node != nullptr && node->secureNwkSeen && networkKey != nullptr);

  ZigbeeApsDataFrame aps{};
  aps.frameType = ZigbeeApsFrameType::kData;
  aps.deliveryMode = deliveryMode;
  aps.ackRequested = (deliveryMode == kZigbeeApsDeliveryUnicast);
  aps.destinationEndpoint = destinationEndpoint;
  aps.destinationGroup = destinationGroup;
  aps.clusterId = clusterId;
  aps.profileId = profileId;
  aps.sourceEndpoint = sourceEndpoint;
  aps.counter = apsCounter;

  uint8_t apsFrame[127] = {0U};
  uint8_t apsLength = 0U;
  if (!ZigbeeCodec::buildApsDataFrame(aps, payload, payloadLength, apsFrame,
                                      &apsLength)) {
#if NRF54L15_CLEAN_ZIGBEE_TX_DEBUG != 0
    Serial.print("aps_tx build_aps FAIL short=0x");
    Serial.print(destinationShort, HEX);
    Serial.print(" cluster=0x");
    Serial.print(clusterId, HEX);
    Serial.print(" payload_len=");
    Serial.print(payloadLength);
    Serial.print("\r\n");
#endif
    return false;
  }

  ZigbeeNetworkFrame nwk{};
  nwk.frameType = ZigbeeNwkFrameType::kData;
  nwk.securityEnabled = useSecurity;
  nwk.destinationShort = destinationShort;
  nwk.sourceShort = kCoordinatorShort;
  nwk.radius = 30U;
  nwk.sequence = g_nwkSequence++;

  uint8_t nwkFrame[127] = {0U};
  uint8_t nwkLength = 0U;
  if (useSecurity) {
    ZigbeeNwkSecurityHeader security{};
    security.valid = true;
    security.securityControl = kZigbeeSecurityControlNwkEncMic32;
    security.frameCounter = g_nwkSecurityFrameCounter++;
    security.sourceIeee = kCoordinatorIeee;
    security.keySequence = keySequence;
    if (!ZigbeeSecurity::buildSecuredNwkFrame(nwk, security, networkKey,
                                              apsFrame, apsLength, nwkFrame,
                                              &nwkLength)) {
#if NRF54L15_CLEAN_ZIGBEE_TX_DEBUG != 0
      Serial.print("aps_tx build_secure FAIL short=0x");
      Serial.print(destinationShort, HEX);
      Serial.print(" cluster=0x");
      Serial.print(clusterId, HEX);
      Serial.print(" aps_len=");
      Serial.print(apsLength);
      Serial.print(" key_seq=");
      Serial.print(keySequence);
      Serial.print("\r\n");
#endif
      return false;
    }
  } else if (!ZigbeeCodec::buildNwkFrame(nwk, apsFrame, apsLength, nwkFrame,
                                         &nwkLength)) {
#if NRF54L15_CLEAN_ZIGBEE_TX_DEBUG != 0
    Serial.print("aps_tx build_nwk FAIL short=0x");
    Serial.print(destinationShort, HEX);
    Serial.print(" cluster=0x");
    Serial.print(clusterId, HEX);
    Serial.print(" aps_len=");
    Serial.print(apsLength);
    Serial.print("\r\n");
#endif
    return false;
  }

  uint8_t psdu[127] = {0U};
  uint8_t psduLength = 0U;
  if (!ZigbeeRadio::buildDataFrameShort(g_macSequence++, kPanId,
                                        destinationShort, kCoordinatorShort,
                                        nwkFrame, nwkLength, psdu,
                                        &psduLength, false)) {
#if NRF54L15_CLEAN_ZIGBEE_TX_DEBUG != 0
    Serial.print("aps_tx build_mac FAIL short=0x");
    Serial.print(destinationShort, HEX);
    Serial.print(" cluster=0x");
    Serial.print(clusterId, HEX);
    Serial.print(" nwk_len=");
    Serial.print(nwkLength);
    Serial.print("\r\n");
#endif
    return false;
  }
  const bool sent = sendPsdu(psdu, psduLength);
#if NRF54L15_CLEAN_ZIGBEE_TX_DEBUG != 0
  if (!sent) {
    Serial.print("aps_tx send FAIL short=0x");
    Serial.print(destinationShort, HEX);
    Serial.print(" cluster=0x");
    Serial.print(clusterId, HEX);
    Serial.print(" secure=");
    Serial.print(useSecurity ? "yes" : "no");
    Serial.print(" aps_len=");
    Serial.print(apsLength);
    Serial.print(" nwk_len=");
    Serial.print(nwkLength);
    Serial.print(" psdu_len=");
    Serial.print(psduLength);
    Serial.print("\r\n");
  }
#endif
  if (sent && trackAck) {
    rememberPendingApsAck(node, aps, payload, payloadLength);
  }
  return sent;
}

bool resendPendingApsFrame(NodeEntry* node, uint8_t slot) {
  if (node == nullptr || slot >= kPendingApsAckSlots) {
    return false;
  }

  PendingApsAck& pending = node->pendingApsAcks[slot];
  if (!pending.active || !pending.frame.used) {
    return false;
  }

  const bool sent = sendApsFrameExtendedWithCounter(
      node->shortAddress, pending.frame.deliveryMode,
      pending.frame.destinationGroup, pending.frame.destinationEndpoint,
      pending.frame.clusterId, pending.frame.profileId,
      pending.frame.sourceEndpoint, pending.frame.payload,
      pending.frame.payloadLength, pending.counter, false);
  if (sent) {
    pending.deadlineMs = millis() + kApsAckTimeoutMs;
  }
  return sent;
}

bool sendApsFrameExtended(uint16_t destinationShort, uint8_t deliveryMode,
                          uint16_t destinationGroup,
                          uint8_t destinationEndpoint, uint16_t clusterId,
                          uint16_t profileId, uint8_t sourceEndpoint,
                          const uint8_t* payload, uint8_t payloadLength) {
  return sendApsFrameExtendedWithCounter(destinationShort, deliveryMode,
                                         destinationGroup, destinationEndpoint,
                                         clusterId, profileId, sourceEndpoint,
                                         payload, payloadLength, g_apsCounter++,
                                         true);
}

bool sendApsAcknowledgement(NodeEntry* node, const ZigbeeApsDataFrame& request) {
  if (node == nullptr || request.deliveryMode != kZigbeeApsDeliveryUnicast ||
      !request.ackRequested) {
    return false;
  }

  const uint8_t keySequence =
      (node->currentNetworkKeySequence != 0U)
          ? node->currentNetworkKeySequence
          : g_activeNetworkKeySequence;
  const uint8_t* networkKey = keyForSequence(keySequence);
  const bool useSecurity =
      node->secureNwkSeen && networkKey != nullptr;

  uint8_t apsFrame[127] = {0U};
  uint8_t apsLength = 0U;
  if (!ZigbeeCodec::buildApsDataAcknowledgement(request, apsFrame,
                                                &apsLength)) {
    return false;
  }

  ZigbeeNetworkFrame nwk{};
  nwk.frameType = ZigbeeNwkFrameType::kData;
  nwk.securityEnabled = useSecurity;
  nwk.destinationShort = node->shortAddress;
  nwk.sourceShort = kCoordinatorShort;
  nwk.radius = 30U;
  nwk.sequence = g_nwkSequence++;

  uint8_t nwkFrame[127] = {0U};
  uint8_t nwkLength = 0U;
  if (useSecurity) {
    ZigbeeNwkSecurityHeader security{};
    security.valid = true;
    security.securityControl = kZigbeeSecurityControlNwkEncMic32;
    security.frameCounter = g_nwkSecurityFrameCounter++;
    security.sourceIeee = kCoordinatorIeee;
    security.keySequence = keySequence;
    if (!ZigbeeSecurity::buildSecuredNwkFrame(nwk, security, networkKey,
                                              apsFrame, apsLength, nwkFrame,
                                              &nwkLength)) {
      return false;
    }
  } else if (!ZigbeeCodec::buildNwkFrame(nwk, apsFrame, apsLength, nwkFrame,
                                         &nwkLength)) {
    return false;
  }

  uint8_t psdu[127] = {0U};
  uint8_t psduLength = 0U;
  if (!ZigbeeRadio::buildDataFrameShort(g_macSequence++, kPanId,
                                        node->shortAddress, kCoordinatorShort,
                                        nwkFrame, nwkLength, psdu,
                                        &psduLength, false)) {
    return false;
  }
  return sendPsdu(psdu, psduLength);
}

bool sendNwkRejoinResponse(NodeEntry* node, uint8_t status) {
  if (node == nullptr) {
    return false;
  }

  uint8_t payload[8] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildNwkRejoinResponseCommand(node->shortAddress, status,
                                                  payload, &payloadLength)) {
    return false;
  }
  return sendNwkCommand(node, payload, payloadLength);
}

bool sendEndDeviceTimeoutResponse(NodeEntry* node, uint8_t status,
                                  uint8_t parentInformation) {
  if (node == nullptr) {
    return false;
  }

  uint8_t payload[8] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildNwkEndDeviceTimeoutResponseCommand(
          status, parentInformation, payload, &payloadLength)) {
    return false;
  }
  return sendNwkCommand(node, payload, payloadLength);
}

bool sendTransportKey(NodeEntry* node) {
  uint8_t psdu[127] = {0U};
  uint8_t psduLength = 0U;
  uint8_t keySequence = 0U;
  bool nwkSecurityEnabled = false;
  if (!buildTransportKeyPsdu(node, psdu, &psduLength, &keySequence,
                             &nwkSecurityEnabled)) {
    return false;
  }

  const bool sent = sendPsdu(psdu, psduLength);
  if (!sent) {
    Serial.print("transport_key FAIL stage=tx short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" psdu_len=");
    Serial.print(psduLength);
    Serial.print("\r\n");
  } else {
    Serial.print("transport_key OK short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" seq=");
    Serial.print(keySequence);
    Serial.print(" nwk_sec=");
    Serial.print(nwkSecurityEnabled ? "yes" : "no");
    Serial.print("\r\n");
  }
  return sent;
}

bool buildTransportKeyPsdu(NodeEntry* node, uint8_t* outPsdu,
                           uint8_t* outPsduLength, uint8_t* outKeySequence,
                           bool* outNwkSecurityEnabled) {
  if (node == nullptr || node->shortAddress == 0U || node->ieeeAddress == 0U) {
    Serial.print("transport_key FAIL stage=invalid_node\r\n");
    return false;
  }
  if (outPsdu == nullptr || outPsduLength == nullptr) {
    return false;
  }
  *outPsduLength = 0U;
  if (outKeySequence != nullptr) {
    *outKeySequence = 0U;
  }
  if (outNwkSecurityEnabled != nullptr) {
    *outNwkSecurityEnabled = false;
  }

  uint8_t linkKey[16] = {0U};
  ZigbeePreconfiguredKeyMode keyMode = ZigbeePreconfiguredKeyMode::kNone;
  if (!resolveTrustCenterLinkKey(node, linkKey, &keyMode)) {
    Serial.print("transport_key FAIL stage=resolve_link_key short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print("\r\n");
    return false;
  }
  node->preconfiguredKeyMode = keyMode;

  ZigbeeApsTransportKey transportKey{};
  transportKey.valid = true;
  transportKey.keyType = kZigbeeApsTransportKeyStandardNetworkKey;
  const bool sendAlternateKey =
      node->pendingNetworkKeyUpdate && g_haveAlternateNetworkKey;
  if ((!sendAlternateKey &&
       node->trustCenterState != TrustCenterNodeState::kWaitingTransportKey) ||
      (sendAlternateKey &&
       (node->trustCenterState != TrustCenterNodeState::kJoined ||
        node->currentNetworkKeySequence == 0U ||
        node->currentNetworkKeySequence == g_alternateNetworkKeySequence))) {
    Serial.print("transport_key FAIL stage=state short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" tc=");
    Serial.print(trustCenterStateName(node->trustCenterState));
    Serial.print(" send_alt=");
    Serial.print(sendAlternateKey ? "yes" : "no");
    Serial.print("\r\n");
    return false;
  }
  const uint8_t keySequence =
      sendAlternateKey ? g_alternateNetworkKeySequence : g_activeNetworkKeySequence;
  if (outKeySequence != nullptr) {
    *outKeySequence = keySequence;
  }
  const uint8_t* keyMaterial =
      sendAlternateKey ? g_alternateNetworkKey : g_activeNetworkKey;
  memcpy(transportKey.key, keyMaterial, sizeof(transportKey.key));
  transportKey.keySequence = keySequence;
  transportKey.destinationIeee = node->ieeeAddress;
  transportKey.sourceIeee = kCoordinatorIeee;

  uint8_t apsFrame[127] = {0U};
  uint8_t apsLength = 0U;
  ZigbeeApsSecurityHeader security{};
  security.valid = true;
  security.securityControl = kZigbeeSecurityControlApsEncMic32;
  security.frameCounter = g_apsTrustCenterFrameCounter++;
  security.sourceIeee = kCoordinatorIeee;
  if (!ZigbeeSecurity::buildSecuredApsTransportKeyCommand(
          transportKey, security, linkKey, g_apsCounter++, apsFrame,
          &apsLength)) {
    Serial.print("transport_key FAIL stage=build_aps short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" mode=");
    Serial.print(static_cast<uint8_t>(keyMode));
    Serial.print("\r\n");
    return false;
  }

  ZigbeeNetworkFrame nwk{};
  nwk.frameType = ZigbeeNwkFrameType::kData;
  const uint8_t nwkKeySequence =
      (node->currentNetworkKeySequence != 0U) ? node->currentNetworkKeySequence
                                              : g_activeNetworkKeySequence;
  const uint8_t* nwkKey = keyForSequence(nwkKeySequence);
  nwk.securityEnabled =
      (node->secureNwkSeen && node->currentNetworkKeySequence != 0U &&
       nwkKey != nullptr);
  if (outNwkSecurityEnabled != nullptr) {
    *outNwkSecurityEnabled = nwk.securityEnabled;
  }
  nwk.destinationShort = node->shortAddress;
  nwk.sourceShort = kCoordinatorShort;
  nwk.radius = 30U;
  nwk.sequence = g_nwkSequence++;

  uint8_t nwkFrame[127] = {0U};
  uint8_t nwkLength = 0U;
  if (nwk.securityEnabled) {
    ZigbeeNwkSecurityHeader nwkSecurity{};
    nwkSecurity.valid = true;
    nwkSecurity.securityControl = kZigbeeSecurityControlNwkEncMic32;
    nwkSecurity.frameCounter = g_nwkSecurityFrameCounter++;
    nwkSecurity.sourceIeee = kCoordinatorIeee;
    nwkSecurity.keySequence = nwkKeySequence;
    if (!ZigbeeSecurity::buildSecuredNwkFrame(nwk, nwkSecurity, nwkKey,
                                              apsFrame, apsLength, nwkFrame,
                                              &nwkLength)) {
      Serial.print("transport_key FAIL stage=build_nwk_secure short=0x");
      Serial.print(node->shortAddress, HEX);
      Serial.print(" key_seq=");
      Serial.print(nwkKeySequence);
      Serial.print("\r\n");
      return false;
    }
  } else if (!ZigbeeCodec::buildNwkFrame(nwk, apsFrame, apsLength, nwkFrame,
                                         &nwkLength)) {
    Serial.print("transport_key FAIL stage=build_nwk_plain short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print("\r\n");
    return false;
  }

  if (!ZigbeeRadio::buildDataFrameShort(g_macSequence++, kPanId,
                                        node->shortAddress, kCoordinatorShort,
                                        nwkFrame, nwkLength, outPsdu,
                                        outPsduLength, false)) {
    Serial.print("transport_key FAIL stage=build_mac short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" nwk_len=");
    Serial.print(nwkLength);
    Serial.print("\r\n");
    return false;
  }
  return true;
}

bool sendApsFrame(uint16_t destinationShort, uint8_t destinationEndpoint,
                  uint16_t clusterId, uint16_t profileId,
                  uint8_t sourceEndpoint, const uint8_t* payload,
                  uint8_t payloadLength) {
  return sendApsFrameExtended(destinationShort, kZigbeeApsDeliveryUnicast, 0U,
                              destinationEndpoint, clusterId, profileId,
                              sourceEndpoint, payload, payloadLength);
}

bool queuePendingTransportKey(NodeEntry* node, bool networkKeyUpdate = false) {
  if (node == nullptr || node->pendingAssociationResponse ||
      node->pendingTransportKey || node->pendingSwitchKey || node->pending.used) {
    return false;
  }
  if (networkKeyUpdate) {
    if (!g_haveAlternateNetworkKey ||
        node->trustCenterState != TrustCenterNodeState::kJoined ||
        node->currentNetworkKeySequence == 0U ||
        node->currentNetworkKeySequence == g_alternateNetworkKeySequence) {
      return false;
    }
    node->pendingNetworkKeyUpdate = true;
  } else {
    node->pendingNetworkKeyUpdate = false;
    node->trustCenterState = TrustCenterNodeState::kWaitingTransportKey;
  }
  node->pendingTransportKey = true;
  node->pendingMac.used = false;

  uint8_t keySequence = 0U;
  bool nwkSecurityEnabled = false;
  if (buildTransportKeyPsdu(node, node->pendingMac.psdu, &node->pendingMac.length,
                            &keySequence, &nwkSecurityEnabled)) {
    node->pendingMac.used = true;
    Serial.print("transport_key prepared short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" len=");
    Serial.print(node->pendingMac.length);
    Serial.print(" seq=");
    Serial.print(keySequence);
    Serial.print(" nwk_sec=");
    Serial.print(nwkSecurityEnabled ? "yes" : "no");
    Serial.print("\r\n");
  }
  return true;
}

bool queueNetworkKeyUpdateRollout() {
  if (!deriveAlternateNetworkKey()) {
    return false;
  }

  bool queuedAny = false;
  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    NodeEntry& node = g_nodes[i];
    if (!node.used || node.shortAddress == 0U || node.pendingAssociationResponse ||
        node.pendingTransportKey || node.pendingSwitchKey ||
        node.pendingSecureRejoin || node.pending.used ||
        node.currentNetworkKeySequence == 0U ||
        node.trustCenterState != TrustCenterNodeState::kJoined ||
        node.currentNetworkKeySequence == g_alternateNetworkKeySequence) {
      continue;
    }
    queuedAny = queuePendingTransportKey(&node, true) || queuedAny;
  }
  return queuedAny;
}

bool sendSwitchKey(NodeEntry* node) {
  if (node == nullptr || node->shortAddress == 0U || !g_haveAlternateNetworkKey ||
      node->currentNetworkKeySequence == 0U ||
      node->trustCenterState != TrustCenterNodeState::kAlternateKeyStaged) {
    return false;
  }

  uint8_t linkKey[16] = {0U};
  ZigbeePreconfiguredKeyMode keyMode = ZigbeePreconfiguredKeyMode::kNone;
  if (!resolveTrustCenterLinkKey(node, linkKey, &keyMode)) {
    return false;
  }
  node->preconfiguredKeyMode = keyMode;

  const uint8_t* currentKey = keyForSequence(node->currentNetworkKeySequence);
  if (currentKey == nullptr) {
    return false;
  }

  ZigbeeApsSwitchKey switchKey{};
  switchKey.valid = true;
  switchKey.keySequence = g_alternateNetworkKeySequence;

  uint8_t apsFrame[127] = {0U};
  uint8_t apsLength = 0U;
  ZigbeeApsSecurityHeader apsSecurity{};
  apsSecurity.valid = true;
  apsSecurity.securityControl = kZigbeeSecurityControlApsEncMic32;
  apsSecurity.frameCounter = g_apsTrustCenterFrameCounter++;
  apsSecurity.sourceIeee = kCoordinatorIeee;
  if (!ZigbeeSecurity::buildSecuredApsSwitchKeyCommand(
          switchKey, apsSecurity, linkKey, g_apsCounter++, apsFrame,
          &apsLength)) {
    return false;
  }

  ZigbeeNetworkFrame nwk{};
  nwk.frameType = ZigbeeNwkFrameType::kData;
  nwk.securityEnabled = true;
  nwk.destinationShort = node->shortAddress;
  nwk.sourceShort = kCoordinatorShort;
  nwk.radius = 30U;
  nwk.sequence = g_nwkSequence++;

  uint8_t nwkFrame[127] = {0U};
  uint8_t nwkLength = 0U;
  ZigbeeNwkSecurityHeader security{};
  security.valid = true;
  security.securityControl = kZigbeeSecurityControlNwkEncMic32;
  security.frameCounter = g_nwkSecurityFrameCounter++;
  security.sourceIeee = kCoordinatorIeee;
  security.keySequence = node->currentNetworkKeySequence;
  if (!ZigbeeSecurity::buildSecuredNwkFrame(nwk, security, currentKey, apsFrame,
                                            apsLength, nwkFrame, &nwkLength)) {
    return false;
  }

  uint8_t psdu[127] = {0U};
  uint8_t psduLength = 0U;
  if (!ZigbeeRadio::buildDataFrameShort(g_macSequence++, kPanId,
                                        node->shortAddress, kCoordinatorShort,
                                        nwkFrame, nwkLength, psdu,
                                        &psduLength, false)) {
    return false;
  }
  return sendPsdu(psdu, psduLength);
}

bool sendUpdateDevice(NodeEntry* node) {
  uint8_t psdu[127] = {0U};
  uint8_t psduLength = 0U;
  if (!buildUpdateDevicePsdu(node, psdu, &psduLength)) {
    return false;
  }
  return sendPsdu(psdu, psduLength);
}

bool buildUpdateDevicePsdu(NodeEntry* node, uint8_t* outPsdu,
                           uint8_t* outPsduLength) {
  if (node == nullptr || node->shortAddress == 0U || node->ieeeAddress == 0U) {
    Serial.print("update_device FAIL stage=invalid_node\r\n");
    return false;
  }
  if (outPsdu == nullptr || outPsduLength == nullptr) {
    return false;
  }
  *outPsduLength = 0U;
  if (!secureRejoinAllowed(node) ||
      node->trustCenterState != TrustCenterNodeState::kWaitingUpdateDevice) {
    Serial.print("update_device FAIL stage=state short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" tc=");
    Serial.print(trustCenterStateName(node->trustCenterState));
    Serial.print(" lk=");
    Serial.print(ZigbeeCommissioning::keyModeName(node->preconfiguredKeyMode));
    Serial.print("\r\n");
    return false;
  }

  uint8_t linkKey[16] = {0U};
  ZigbeePreconfiguredKeyMode keyMode = ZigbeePreconfiguredKeyMode::kNone;
  if (!resolveTrustCenterLinkKey(node, linkKey, &keyMode)) {
    Serial.print("update_device FAIL stage=resolve_link_key short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print("\r\n");
    return false;
  }
  node->preconfiguredKeyMode = keyMode;

  ZigbeeApsUpdateDevice updateDevice{};
  updateDevice.valid = true;
  updateDevice.deviceIeee = node->ieeeAddress;
  updateDevice.deviceShort = node->shortAddress;
  updateDevice.status = kZigbeeApsUpdateDeviceStatusStandardSecureRejoin;

  uint8_t apsFrame[127] = {0U};
  uint8_t apsLength = 0U;
  ZigbeeApsSecurityHeader apsSecurity{};
  apsSecurity.valid = true;
  apsSecurity.securityControl = kZigbeeSecurityControlApsEncMic32;
  apsSecurity.frameCounter = g_apsTrustCenterFrameCounter++;
  apsSecurity.sourceIeee = kCoordinatorIeee;
  if (!ZigbeeSecurity::buildSecuredApsUpdateDeviceCommand(
          updateDevice, apsSecurity, linkKey, g_apsCounter++, apsFrame,
          &apsLength)) {
    Serial.print("update_device FAIL stage=build_aps short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print("\r\n");
    return false;
  }

  ZigbeeNetworkFrame nwk{};
  nwk.frameType = ZigbeeNwkFrameType::kData;
  nwk.securityEnabled = true;
  nwk.destinationShort = node->shortAddress;
  nwk.sourceShort = kCoordinatorShort;
  nwk.radius = 30U;
  nwk.sequence = g_nwkSequence++;

  uint8_t nwkFrame[127] = {0U};
  uint8_t nwkLength = 0U;
  ZigbeeNwkSecurityHeader security{};
  security.valid = true;
  security.securityControl = kZigbeeSecurityControlNwkEncMic32;
  security.frameCounter = g_nwkSecurityFrameCounter++;
  security.sourceIeee = kCoordinatorIeee;
  const uint8_t keySequence =
      (node->currentNetworkKeySequence != 0U) ? node->currentNetworkKeySequence
                                              : g_activeNetworkKeySequence;
  const uint8_t* networkKey = keyForSequence(keySequence);
  if (networkKey == nullptr) {
    Serial.print("update_device FAIL stage=network_key short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" seq=");
    Serial.print(keySequence);
    Serial.print("\r\n");
    return false;
  }
  security.keySequence = keySequence;
  if (!ZigbeeSecurity::buildSecuredNwkFrame(nwk, security, networkKey,
                                            apsFrame, apsLength, nwkFrame,
                                            &nwkLength)) {
    Serial.print("update_device FAIL stage=build_nwk short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print("\r\n");
    return false;
  }

  if (!ZigbeeRadio::buildDataFrameShort(g_macSequence++, kPanId,
                                        node->shortAddress, kCoordinatorShort,
                                        nwkFrame, nwkLength, outPsdu,
                                        outPsduLength, false)) {
    Serial.print("update_device FAIL stage=build_mac short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print("\r\n");
    return false;
  }
  Serial.print("update_device prepared short=0x");
  Serial.print(node->shortAddress, HEX);
  Serial.print(" len=");
  Serial.print(*outPsduLength);
  Serial.print(" seq=");
  Serial.print(keySequence);
  Serial.print("\r\n");
  return true;
}

bool sendPendingAssociationResponse(NodeEntry* node) {
  if (node == nullptr || !node->pendingAssociationResponse) {
    return false;
  }

  uint8_t frame[127] = {0U};
  uint8_t length = 0U;
  const bool built = ZigbeeCodec::buildAssociationResponse(
      g_macSequence++, kPanId, node->ieeeAddress, kCoordinatorShort,
      node->pendingAssignedShort, node->pendingAssociationStatus, frame, &length);
  if (!built) {
    return false;
  }
  const bool sent = sendPsdu(frame, length);
  if (sent) {
    node->pendingAssociationResponse = false;
    node->shortAddress = node->pendingAssignedShort;
    node->lastSeenMs = millis();
      if (node->pendingAssociationStatus == 0x00U) {
        if (node->pendingSecureRejoin && secureRejoinAllowed(node)) {
          node->trustCenterState = TrustCenterNodeState::kWaitingUpdateDevice;
          node->pendingMac.used =
              buildUpdateDevicePsdu(node, node->pendingMac.psdu, &node->pendingMac.length);
          Serial.print("assoc_rejoin short=0x");
          Serial.print(node->shortAddress, HEX);
          Serial.print(" tc=");
          Serial.print(trustCenterStateName(node->trustCenterState));
          Serial.print(" prepared=");
          Serial.print(node->pendingMac.used ? "yes" : "no");
          Serial.print("\r\n");
        } else {
          node->pendingSecureRejoin = false;
        (void)queuePendingTransportKey(node);
      }
    }
  }
  return sent;
}

bool sendPendingApsFrame(NodeEntry* node) {
  if (node == nullptr) {
    return false;
  }
  if (node->pendingTransportKey) {
    const bool sent =
        node->pendingMac.used ? sendPsdu(node->pendingMac.psdu, node->pendingMac.length)
                              : sendTransportKey(node);
    if (node->pendingMac.used) {
      Serial.print("transport_key ");
      Serial.print(sent ? "OK" : "FAIL");
      Serial.print(" stage=prepared_tx short=0x");
      Serial.print(node->shortAddress, HEX);
      Serial.print(" len=");
      Serial.print(node->pendingMac.length);
      Serial.print("\r\n");
    }
    if (sent) {
      node->pendingTransportKey = false;
      node->pendingMac.used = false;
      if (node->pendingNetworkKeyUpdate) {
        node->pendingNetworkKeyUpdate = false;
        node->pendingSwitchKey = true;
        node->trustCenterState = TrustCenterNodeState::kAlternateKeyStaged;
      } else {
        node->currentNetworkKeySequence = g_activeNetworkKeySequence;
        node->lastInboundSecurityFrameCounter = 0U;
        node->trustCenterState = TrustCenterNodeState::kJoined;
      }
    }
    return sent;
  }
  if (node->pendingSwitchKey) {
    const bool sent = sendSwitchKey(node);
    if (sent) {
      node->pendingSwitchKey = false;
      node->currentNetworkKeySequence = g_alternateNetworkKeySequence;
      node->lastInboundSecurityFrameCounter = 0U;
      node->trustCenterState = TrustCenterNodeState::kJoined;
      maybePromoteAlternateNetworkKey();
    }
    return sent;
  }
  if (node->pendingSecureRejoin) {
    const bool sent =
        node->pendingMac.used ? sendPsdu(node->pendingMac.psdu, node->pendingMac.length)
                              : sendUpdateDevice(node);
    if (sent) {
      node->pendingSecureRejoin = false;
      node->pendingMac.used = false;
      node->pendingMac.length = 0U;
      node->trustCenterState = TrustCenterNodeState::kJoined;
    }
    return sent;
  }
  if (!node->pending.used) {
    return false;
  }
  if (node->pending.deliveryMode == kZigbeeApsDeliveryUnicast &&
      !pendingApsAckSlotAvailable(*node, millis())) {
    return false;
  }

  const bool sent =
      sendApsFrameExtended(node->shortAddress, node->pending.deliveryMode,
                           node->pending.destinationGroup,
                           node->pending.destinationEndpoint,
                           node->pending.clusterId, node->pending.profileId,
                           node->pending.sourceEndpoint, node->pending.payload,
                           node->pending.payloadLength);
  if (sent) {
    node->pending.used = false;
    node->pending.payloadLength = 0U;
  }
  return sent;
}

bool queueActiveEndpointsRequest(NodeEntry* node) {
  if (node == nullptr) {
    return false;
  }
  uint8_t payload[8] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildZdoActiveEndpointsRequest(g_zdoSequence++,
                                                   node->shortAddress,
                                                   payload, &payloadLength)) {
    return false;
  }
  const bool queued =
      queuePendingApsFrame(node, kZigbeeZdoActiveEndpointsRequest,
                           kZigbeeProfileZdo, 0U, 0U, payload, payloadLength);
  if (queued) {
    markInterviewStage(node, NodeStage::kAwaitingActiveEndpoints);
  }
  return queued;
}

bool queueNodeDescriptorRequest(NodeEntry* node) {
  if (node == nullptr || node->shortAddress == 0U) {
    return false;
  }
  uint8_t payload[8] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildZdoNodeDescriptorRequest(g_zdoSequence++,
                                                  node->shortAddress, payload,
                                                  &payloadLength)) {
    return false;
  }
  const bool queued =
      queuePendingApsFrame(node, kZigbeeZdoNodeDescriptorRequest,
                           kZigbeeProfileZdo, 0U, 0U, payload, payloadLength);
  if (queued) {
    markInterviewStage(node, NodeStage::kAwaitingNodeDescriptor);
  }
  return queued;
}

bool queuePowerDescriptorRequest(NodeEntry* node) {
  if (node == nullptr || node->shortAddress == 0U) {
    return false;
  }
  uint8_t payload[8] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildZdoPowerDescriptorRequest(g_zdoSequence++,
                                                   node->shortAddress, payload,
                                                   &payloadLength)) {
    return false;
  }
  const bool queued =
      queuePendingApsFrame(node, kZigbeeZdoPowerDescriptorRequest,
                           kZigbeeProfileZdo, 0U, 0U, payload, payloadLength);
  if (queued) {
    markInterviewStage(node, NodeStage::kAwaitingPowerDescriptor);
  }
  return queued;
}

bool queueSimpleDescriptorRequest(NodeEntry* node, uint8_t endpoint) {
  if (node == nullptr || endpoint == 0U) {
    return false;
  }
  uint8_t payload[8] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildZdoSimpleDescriptorRequest(g_zdoSequence++,
                                                    node->shortAddress, endpoint,
                                                    payload, &payloadLength)) {
    return false;
  }
  const bool queued =
      queuePendingApsFrame(node, kZigbeeZdoSimpleDescriptorRequest,
                           kZigbeeProfileZdo, 0U, 0U, payload, payloadLength);
  if (queued) {
    markInterviewStage(node, NodeStage::kAwaitingSimpleDescriptor);
  }
  return queued;
}

bool queueBasicReadRequest(NodeEntry* node) {
  if (node == nullptr || node->endpoint == 0U) {
    return false;
  }
  static constexpr uint16_t kBasicReadBatch0[] = {0x0004U, 0x0005U};
  static constexpr uint16_t kBasicReadBatch1[] = {0x0000U, 0x0001U, 0x0002U,
                                                  0x0003U, 0x0007U};
  static constexpr uint16_t kBasicReadBatch2[] = {0x4000U, 0xFFFCU, 0xFFFDU};
  const uint16_t* attributes = nullptr;
  uint8_t attributeCount = 0U;
  switch (node->basicReadBatchIndex) {
    case 0U:
      attributes = kBasicReadBatch0;
      attributeCount =
          static_cast<uint8_t>(sizeof(kBasicReadBatch0) / sizeof(kBasicReadBatch0[0]));
      break;
    case 1U:
      attributes = kBasicReadBatch1;
      attributeCount =
          static_cast<uint8_t>(sizeof(kBasicReadBatch1) / sizeof(kBasicReadBatch1[0]));
      break;
    case 2U:
      attributes = kBasicReadBatch2;
      attributeCount =
          static_cast<uint8_t>(sizeof(kBasicReadBatch2) / sizeof(kBasicReadBatch2[0]));
      break;
    default:
      return false;
  }
  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildReadAttributesRequest(
          attributes, attributeCount, g_zclSequence++, payload,
          &payloadLength)) {
    return false;
  }
  const bool queued =
      queuePendingApsFrame(node, kZigbeeClusterBasic,
                           kZigbeeProfileHomeAutomation, node->endpoint,
                           kCoordinatorEndpoint, payload, payloadLength);
  if (queued) {
    markInterviewStage(node, NodeStage::kAwaitingBasicRead);
  }
  return queued;
}

bool retryInterviewStage(NodeEntry* node) {
  if (node == nullptr || node->stageRetriesRemaining == 0U) {
    return false;
  }

  --node->stageRetriesRemaining;
  bool queued = false;
  switch (node->stage) {
    case NodeStage::kAwaitingNodeDescriptor:
      queued = queueNodeDescriptorRequest(node);
      break;
    case NodeStage::kAwaitingPowerDescriptor:
      queued = queuePowerDescriptorRequest(node);
      break;
    case NodeStage::kAwaitingActiveEndpoints:
      queued = queueActiveEndpointsRequest(node);
      break;
    case NodeStage::kAwaitingSimpleDescriptor:
      queued = queueSimpleDescriptorRequest(node, node->endpoint);
      break;
    case NodeStage::kAwaitingBasicRead:
      queued = queueBasicReadRequest(node);
      break;
    default:
      return false;
  }

  node->stageDeadlineMs = millis() + kInterviewRetryDelayMs;
  Serial.print("interview_retry short=0x");
  Serial.print(node->shortAddress, HEX);
  Serial.print(" stage=");
  Serial.print(nodeStageName(node->stage));
  Serial.print(" rem=");
  Serial.print(node->stageRetriesRemaining);
  Serial.print(" queued=");
  Serial.print(queued ? "yes" : "no");
  Serial.print("\r\n");
  return queued;
}

void maybeRetryInterviewStage(NodeEntry* node, uint32_t nowMs) {
  if (node == nullptr || node->shortAddress == 0U) {
    return;
  }
  switch (node->stage) {
    case NodeStage::kAwaitingNodeDescriptor:
    case NodeStage::kAwaitingPowerDescriptor:
    case NodeStage::kAwaitingActiveEndpoints:
    case NodeStage::kAwaitingSimpleDescriptor:
    case NodeStage::kAwaitingBasicRead:
      break;
    default:
      return;
  }

  if (node->pending.used || node->pendingAssociationResponse ||
      node->pendingTransportKey || node->pendingSecureRejoin ||
      node->pendingSwitchKey || node->pendingNwkResponse ||
      nodeHasPendingApsAck(*node)) {
    return;
  }
  if (node->stageDeadlineMs == 0U ||
      static_cast<int32_t>(nowMs - node->stageDeadlineMs) < 0) {
    return;
  }
  if (node->stageRetriesRemaining == 0U) {
    Serial.print("interview_timeout short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" stage=");
    Serial.print(nodeStageName(node->stage));
    Serial.print("\r\n");
    node->stageDeadlineMs = 0U;
    return;
  }

  (void)retryInterviewStage(node);
}

bool queueOnOffConfigureReporting(NodeEntry* node) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsOnOff) {
    return false;
  }
  ZigbeeReportingConfiguration configuration{};
  configuration.used = true;
  configuration.clusterId = kZigbeeClusterOnOff;
  configuration.attributeId = 0x0000U;
  configuration.dataType = ZigbeeZclDataType::kBoolean;
  configuration.minimumIntervalSeconds = 0U;
  configuration.maximumIntervalSeconds = 60U;
  configuration.reportableChange = 0U;

  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildConfigureReportingRequest(
          &configuration, 1U, g_zclSequence++, payload, &payloadLength)) {
    return false;
  }
  const bool queued =
      queuePendingApsFrame(node, kZigbeeClusterOnOff,
                           kZigbeeProfileHomeAutomation, node->endpoint,
                           kCoordinatorEndpoint, payload, payloadLength);
  if (queued) {
    node->awaitingBindResponse = false;
    node->awaitingClusterId = kZigbeeClusterOnOff;
    node->stage = NodeStage::kAwaitingReporting;
  }
  return queued;
}

bool queueBindRequest(NodeEntry* node, uint16_t clusterId) {
  if (node == nullptr || node->endpoint == 0U || node->ieeeAddress == 0U) {
    return false;
  }

  uint8_t payload[32] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildZdoBindRequest(
          g_zdoSequence++, node->ieeeAddress, node->endpoint, clusterId,
          ZigbeeBindingAddressMode::kExtended, 0U, kCoordinatorIeee,
          kCoordinatorEndpoint, payload, &payloadLength)) {
    return false;
  }
  const bool queued =
      queuePendingApsFrame(node, kZigbeeZdoBindRequest, kZigbeeProfileZdo, 0U,
                           0U, payload, payloadLength);
  if (queued) {
    node->awaitingBindResponse = true;
    node->awaitingClusterId = clusterId;
    node->stage = NodeStage::kAwaitingReporting;
  }
  return queued;
}

bool queueMgmtLeaveRequest(NodeEntry* node, bool requestRejoin = false) {
  if (node == nullptr || node->ieeeAddress == 0U) {
    return false;
  }

  uint8_t payload[16] = {0U};
  uint8_t payloadLength = 0U;
  const uint8_t leaveFlags =
      requestRejoin ? kZigbeeMgmtLeaveFlagRejoin : 0x00U;
  if (!ZigbeeCodec::buildZdoMgmtLeaveRequest(
          g_zdoSequence++, node->ieeeAddress, leaveFlags, payload,
          &payloadLength)) {
    return false;
  }
  const bool queued = queuePendingApsFrame(node, kZigbeeZdoMgmtLeaveRequest,
                                           kZigbeeProfileZdo, 0U, 0U, payload,
                                           payloadLength);
  if (queued) {
    node->pendingLeaveWithRejoin = requestRejoin;
  }
  return queued;
}

bool queueLevelConfigureReporting(NodeEntry* node) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsLevelControl) {
    return false;
  }
  ZigbeeReportingConfiguration configuration{};
  configuration.used = true;
  configuration.clusterId = kZigbeeClusterLevelControl;
  configuration.attributeId = 0x0000U;
  configuration.dataType = ZigbeeZclDataType::kUint8;
  configuration.minimumIntervalSeconds = 0U;
  configuration.maximumIntervalSeconds = 60U;
  configuration.reportableChange = 16U;

  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildConfigureReportingRequest(
          &configuration, 1U, g_zclSequence++, payload, &payloadLength)) {
    return false;
  }
  const bool queued =
      queuePendingApsFrame(node, kZigbeeClusterLevelControl,
                           kZigbeeProfileHomeAutomation, node->endpoint,
                           kCoordinatorEndpoint, payload, payloadLength);
  if (queued) {
    node->awaitingBindResponse = false;
    node->awaitingClusterId = kZigbeeClusterLevelControl;
    node->stage = NodeStage::kAwaitingReporting;
  }
  return queued;
}

bool queueTemperatureConfigureReporting(NodeEntry* node) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsTemperature) {
    return false;
  }
  ZigbeeReportingConfiguration configuration{};
  configuration.used = true;
  configuration.clusterId = kZigbeeClusterTemperatureMeasurement;
  configuration.attributeId = 0x0000U;
  configuration.dataType = ZigbeeZclDataType::kInt16;
  configuration.minimumIntervalSeconds = 5U;
  configuration.maximumIntervalSeconds = 60U;
  configuration.reportableChange = 25U;

  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildConfigureReportingRequest(
          &configuration, 1U, g_zclSequence++, payload, &payloadLength)) {
    return false;
  }
  const bool queued =
      queuePendingApsFrame(node, kZigbeeClusterTemperatureMeasurement,
                           kZigbeeProfileHomeAutomation, node->endpoint,
                           kCoordinatorEndpoint, payload, payloadLength);
  if (queued) {
    node->awaitingBindResponse = false;
    node->awaitingClusterId = kZigbeeClusterTemperatureMeasurement;
    node->stage = NodeStage::kAwaitingReporting;
  }
  return queued;
}

bool queuePowerConfigureReporting(NodeEntry* node) {
  if (node == nullptr || node->endpoint == 0U ||
      !node->supportsPowerConfiguration) {
    return false;
  }

  ZigbeeReportingConfiguration configurations[2];
  memset(configurations, 0, sizeof(configurations));
  configurations[0].used = true;
  configurations[0].clusterId = kZigbeeClusterPowerConfiguration;
  configurations[0].attributeId = 0x0020U;
  configurations[0].dataType = ZigbeeZclDataType::kUint8;
  configurations[0].minimumIntervalSeconds = 30U;
  configurations[0].maximumIntervalSeconds = 300U;
  configurations[0].reportableChange = 1U;
  configurations[1].used = true;
  configurations[1].clusterId = kZigbeeClusterPowerConfiguration;
  configurations[1].attributeId = 0x0021U;
  configurations[1].dataType = ZigbeeZclDataType::kUint8;
  configurations[1].minimumIntervalSeconds = 30U;
  configurations[1].maximumIntervalSeconds = 300U;
  configurations[1].reportableChange = 2U;

  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildConfigureReportingRequest(
          configurations, 2U, g_zclSequence++, payload, &payloadLength)) {
    return false;
  }
  const bool queued =
      queuePendingApsFrame(node, kZigbeeClusterPowerConfiguration,
                           kZigbeeProfileHomeAutomation, node->endpoint,
                           kCoordinatorEndpoint, payload, payloadLength);
  if (queued) {
    node->awaitingBindResponse = false;
    node->awaitingClusterId = kZigbeeClusterPowerConfiguration;
    node->stage = NodeStage::kAwaitingReporting;
  }
  return queued;
}

bool queueReadReportingConfiguration(NodeEntry* node, uint16_t clusterId) {
  if (node == nullptr || node->endpoint == 0U) {
    return false;
  }

  ZigbeeReadReportingConfigurationRecord records[2];
  memset(records, 0, sizeof(records));
  uint8_t recordCount = 0U;
  switch (clusterId) {
    case kZigbeeClusterOnOff:
    case kZigbeeClusterLevelControl:
    case kZigbeeClusterTemperatureMeasurement:
      records[0].direction = 0U;
      records[0].attributeId = 0x0000U;
      recordCount = 1U;
      break;
    case kZigbeeClusterPowerConfiguration:
      records[0].direction = 0U;
      records[0].attributeId = 0x0020U;
      records[1].direction = 0U;
      records[1].attributeId = 0x0021U;
      recordCount = 2U;
      break;
    default:
      return false;
  }

  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildReadReportingConfigurationRequest(
          records, recordCount, g_zclSequence++, payload, &payloadLength)) {
    return false;
  }
  const bool queued =
      queuePendingApsFrame(node, clusterId, kZigbeeProfileHomeAutomation,
                           node->endpoint, kCoordinatorEndpoint, payload,
                           payloadLength);
  if (queued) {
    node->awaitingClusterId = clusterId;
    node->stage = NodeStage::kAwaitingReporting;
  }
  return queued;
}

bool reportingConfigurationMatches(
    uint16_t clusterId,
    const ZigbeeReadReportingConfigurationResponseRecord* records,
    uint8_t recordCount) {
  if (records == nullptr) {
    return false;
  }

  switch (clusterId) {
    case kZigbeeClusterOnOff:
      return recordCount == 1U && records[0].status == 0x00U &&
             records[0].direction == 0U && records[0].attributeId == 0x0000U &&
             records[0].dataType == ZigbeeZclDataType::kBoolean &&
             records[0].minimumIntervalSeconds == 0U &&
             records[0].maximumIntervalSeconds == 60U &&
             records[0].reportableChange == 0U;
    case kZigbeeClusterLevelControl:
      return recordCount == 1U && records[0].status == 0x00U &&
             records[0].direction == 0U && records[0].attributeId == 0x0000U &&
             records[0].dataType == ZigbeeZclDataType::kUint8 &&
             records[0].minimumIntervalSeconds == 0U &&
             records[0].maximumIntervalSeconds == 60U &&
             records[0].reportableChange == 16U;
    case kZigbeeClusterTemperatureMeasurement:
      return recordCount == 1U && records[0].status == 0x00U &&
             records[0].direction == 0U && records[0].attributeId == 0x0000U &&
             records[0].dataType == ZigbeeZclDataType::kInt16 &&
             records[0].minimumIntervalSeconds == 5U &&
             records[0].maximumIntervalSeconds == 60U &&
             records[0].reportableChange == 25U;
    case kZigbeeClusterPowerConfiguration:
      return recordCount == 2U && records[0].status == 0x00U &&
             records[0].direction == 0U && records[0].attributeId == 0x0020U &&
             records[0].dataType == ZigbeeZclDataType::kUint8 &&
             records[0].minimumIntervalSeconds == 30U &&
             records[0].maximumIntervalSeconds == 300U &&
             records[0].reportableChange == 1U && records[1].status == 0x00U &&
             records[1].direction == 0U && records[1].attributeId == 0x0021U &&
             records[1].dataType == ZigbeeZclDataType::kUint8 &&
             records[1].minimumIntervalSeconds == 30U &&
             records[1].maximumIntervalSeconds == 300U &&
             records[1].reportableChange == 2U;
    default:
      return false;
  }
}

bool queueOnOffCommand(NodeEntry* node, uint8_t commandId) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsOnOff) {
    return false;
  }
  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  ZigbeeZclFrame frame{};
  frame.frameType = ZigbeeZclFrameType::kClusterSpecific;
  frame.disableDefaultResponse = false;
  frame.transactionSequence = g_zclSequence++;
  frame.commandId = commandId;
  if (!ZigbeeCodec::buildZclFrame(frame, nullptr, 0U, payload, &payloadLength)) {
    return false;
  }
  return queuePendingApsFrame(node, kZigbeeClusterOnOff,
                              kZigbeeProfileHomeAutomation, node->endpoint,
                              kCoordinatorEndpoint, payload, payloadLength);
}

bool queueLevelMoveToLevel(NodeEntry* node, uint8_t level) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsLevelControl) {
    return false;
  }

  const uint8_t commandPayload[] = {level, 0x00U, 0x00U};
  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  ZigbeeZclFrame frame{};
  frame.frameType = ZigbeeZclFrameType::kClusterSpecific;
  frame.disableDefaultResponse = false;
  frame.transactionSequence = g_zclSequence++;
  frame.commandId = kLevelControlCommandMoveToLevelWithOnOff;
  if (!ZigbeeCodec::buildZclFrame(frame, commandPayload,
                                  static_cast<uint8_t>(sizeof(commandPayload)),
                                  payload, &payloadLength)) {
    return false;
  }

  return queuePendingApsFrame(node, kZigbeeClusterLevelControl,
                              kZigbeeProfileHomeAutomation, node->endpoint,
                              kCoordinatorEndpoint, payload, payloadLength);
}

bool queueLevelStep(NodeEntry* node, bool increase, uint8_t stepSize) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsLevelControl) {
    return false;
  }

  const uint8_t commandPayload[] = {
      static_cast<uint8_t>(increase ? 0x00U : 0x01U), stepSize, 0x00U, 0x00U};
  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  ZigbeeZclFrame frame{};
  frame.frameType = ZigbeeZclFrameType::kClusterSpecific;
  frame.disableDefaultResponse = false;
  frame.transactionSequence = g_zclSequence++;
  frame.commandId = kLevelControlCommandStepWithOnOff;
  if (!ZigbeeCodec::buildZclFrame(frame, commandPayload,
                                  static_cast<uint8_t>(sizeof(commandPayload)),
                                  payload, &payloadLength)) {
    return false;
  }

  return queuePendingApsFrame(node, kZigbeeClusterLevelControl,
                              kZigbeeProfileHomeAutomation, node->endpoint,
                              kCoordinatorEndpoint, payload, payloadLength);
}

bool queueIdentifyCommand(NodeEntry* node, uint16_t identifyTimeSeconds) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsIdentify) {
    return false;
  }

  const uint8_t commandPayload[] = {
      static_cast<uint8_t>(identifyTimeSeconds & 0xFFU),
      static_cast<uint8_t>((identifyTimeSeconds >> 8U) & 0xFFU)};
  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  ZigbeeZclFrame frame{};
  frame.frameType = ZigbeeZclFrameType::kClusterSpecific;
  frame.disableDefaultResponse = false;
  frame.transactionSequence = g_zclSequence++;
  frame.commandId = kIdentifyCommandIdentify;
  if (!ZigbeeCodec::buildZclFrame(frame, commandPayload,
                                  static_cast<uint8_t>(sizeof(commandPayload)),
                                  payload, &payloadLength)) {
    return false;
  }

  return queuePendingApsFrame(node, kZigbeeClusterIdentify,
                              kZigbeeProfileHomeAutomation, node->endpoint,
                              kCoordinatorEndpoint, payload, payloadLength);
}

bool queueIdentifyDiscoverAttributesExtended(NodeEntry* node) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsIdentify) {
    return false;
  }

  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildDiscoverAttributesExtendedRequest(
          0x0000U, 8U, g_zclSequence++, payload, &payloadLength)) {
    return false;
  }

  return queuePendingApsFrame(node, kZigbeeClusterIdentify,
                              kZigbeeProfileHomeAutomation, node->endpoint,
                              kCoordinatorEndpoint, payload, payloadLength);
}

bool queueIdentifyWriteAttribute(NodeEntry* node, uint16_t identifyTimeSeconds,
                                 bool noResponse) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsIdentify) {
    return false;
  }

  ZigbeeWriteAttributeRecord record{};
  record.attributeId = 0x0000U;
  record.value.type = ZigbeeZclDataType::kUint16;
  record.value.data.u16 = identifyTimeSeconds;

  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildWriteAttributesRequest(&record, 1U, g_zclSequence++,
                                                payload, &payloadLength,
                                                noResponse)) {
    return false;
  }

  return queuePendingApsFrame(node, kZigbeeClusterIdentify,
                              kZigbeeProfileHomeAutomation, node->endpoint,
                              kCoordinatorEndpoint, payload, payloadLength);
}

bool queueIdentifyWriteAttributeUndivided(NodeEntry* node,
                                          uint16_t identifyTimeSeconds,
                                          bool injectReadOnlyFailure) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsIdentify) {
    return false;
  }

  ZigbeeWriteAttributeRecord records[2];
  memset(records, 0, sizeof(records));
  records[0].attributeId = 0x0000U;
  records[0].value.type = ZigbeeZclDataType::kUint16;
  records[0].value.data.u16 = identifyTimeSeconds;

  uint8_t recordCount = 1U;
  if (injectReadOnlyFailure) {
    records[1].attributeId = 0xFFFCU;
    records[1].value.type = ZigbeeZclDataType::kBitmap32;
    records[1].value.data.u32 = 0U;
    recordCount = 2U;
  }

  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  if (!ZigbeeCodec::buildWriteAttributesUndividedRequest(
          records, recordCount, g_zclSequence++, payload, &payloadLength)) {
    return false;
  }

  return queuePendingApsFrame(node, kZigbeeClusterIdentify,
                              kZigbeeProfileHomeAutomation, node->endpoint,
                              kCoordinatorEndpoint, payload, payloadLength);
}

bool queueIdentifyQuery(NodeEntry* node) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsIdentify) {
    return false;
  }

  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  ZigbeeZclFrame frame{};
  frame.frameType = ZigbeeZclFrameType::kClusterSpecific;
  frame.disableDefaultResponse = false;
  frame.transactionSequence = g_zclSequence++;
  frame.commandId = kIdentifyCommandIdentifyQuery;
  if (!ZigbeeCodec::buildZclFrame(frame, nullptr, 0U, payload, &payloadLength)) {
    return false;
  }

  return queuePendingApsFrame(node, kZigbeeClusterIdentify,
                              kZigbeeProfileHomeAutomation, node->endpoint,
                              kCoordinatorEndpoint, payload, payloadLength);
}

bool queueTriggerEffect(NodeEntry* node, uint8_t effectIdentifier) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsIdentify) {
    return false;
  }

  const uint8_t commandPayload[] = {effectIdentifier, 0x00U};
  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  ZigbeeZclFrame frame{};
  frame.frameType = ZigbeeZclFrameType::kClusterSpecific;
  frame.disableDefaultResponse = false;
  frame.transactionSequence = g_zclSequence++;
  frame.commandId = kIdentifyCommandTriggerEffect;
  if (!ZigbeeCodec::buildZclFrame(frame, commandPayload,
                                  static_cast<uint8_t>(sizeof(commandPayload)),
                                  payload, &payloadLength)) {
    return false;
  }

  return queuePendingApsFrame(node, kZigbeeClusterIdentify,
                              kZigbeeProfileHomeAutomation, node->endpoint,
                              kCoordinatorEndpoint, payload, payloadLength);
}

bool queueAddGroup(NodeEntry* node, uint16_t groupId, const char* name) {
  if (node == nullptr || node->endpoint == 0U ||
      (!node->supportsOnOff && !node->supportsLevelControl)) {
    return false;
  }

  uint8_t commandPayload[32] = {0U};
  const uint8_t nameLength =
      (name == nullptr)
          ? 0U
          : static_cast<uint8_t>(strnlen(name, sizeof(commandPayload) - 3U));
  commandPayload[0] = static_cast<uint8_t>(groupId & 0xFFU);
  commandPayload[1] = static_cast<uint8_t>((groupId >> 8U) & 0xFFU);
  commandPayload[2] = nameLength;
  if (nameLength > 0U) {
    memcpy(&commandPayload[3], name, nameLength);
  }

  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  ZigbeeZclFrame frame{};
  frame.frameType = ZigbeeZclFrameType::kClusterSpecific;
  frame.disableDefaultResponse = false;
  frame.transactionSequence = g_zclSequence++;
  frame.commandId = kGroupsCommandAddGroup;
  if (!ZigbeeCodec::buildZclFrame(
          frame, commandPayload, static_cast<uint8_t>(3U + nameLength), payload,
          &payloadLength)) {
    return false;
  }

  return queuePendingApsFrame(node, kZigbeeClusterGroups,
                              kZigbeeProfileHomeAutomation, node->endpoint,
                              kCoordinatorEndpoint, payload, payloadLength);
}

bool queueGroupOnOffCommand(NodeEntry* node, uint16_t groupId,
                            uint8_t commandId) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsOnOff ||
      !node->demoGroupConfigured) {
    return false;
  }

  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  ZigbeeZclFrame frame{};
  frame.frameType = ZigbeeZclFrameType::kClusterSpecific;
  frame.disableDefaultResponse = true;
  frame.transactionSequence = g_zclSequence++;
  frame.commandId = commandId;
  if (!ZigbeeCodec::buildZclFrame(frame, nullptr, 0U, payload, &payloadLength)) {
    return false;
  }

  return queuePendingApsFrameExtended(
      node, kZigbeeApsDeliveryGroup, groupId, kZigbeeClusterOnOff,
      kZigbeeProfileHomeAutomation, 0U, kCoordinatorEndpoint, payload,
      payloadLength);
}

bool queueGroupLevelMoveToLevel(NodeEntry* node, uint16_t groupId,
                                uint8_t level) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsLevelControl ||
      !node->demoGroupConfigured) {
    return false;
  }

  const uint8_t commandPayload[] = {level, 0x00U, 0x00U};
  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  ZigbeeZclFrame frame{};
  frame.frameType = ZigbeeZclFrameType::kClusterSpecific;
  frame.disableDefaultResponse = true;
  frame.transactionSequence = g_zclSequence++;
  frame.commandId = kLevelControlCommandMoveToLevelWithOnOff;
  if (!ZigbeeCodec::buildZclFrame(frame, commandPayload,
                                  static_cast<uint8_t>(sizeof(commandPayload)),
                                  payload, &payloadLength)) {
    return false;
  }

  return queuePendingApsFrameExtended(
      node, kZigbeeApsDeliveryGroup, groupId, kZigbeeClusterLevelControl,
      kZigbeeProfileHomeAutomation, 0U, kCoordinatorEndpoint, payload,
      payloadLength);
}

bool queueGroupLevelStep(NodeEntry* node, uint16_t groupId, bool increase,
                         uint8_t stepSize) {
  if (node == nullptr || node->endpoint == 0U || !node->supportsLevelControl ||
      !node->demoGroupConfigured) {
    return false;
  }

  const uint8_t commandPayload[] = {
      static_cast<uint8_t>(increase ? 0x00U : 0x01U), stepSize, 0x00U, 0x00U};
  uint8_t payload[127] = {0U};
  uint8_t payloadLength = 0U;
  ZigbeeZclFrame frame{};
  frame.frameType = ZigbeeZclFrameType::kClusterSpecific;
  frame.disableDefaultResponse = true;
  frame.transactionSequence = g_zclSequence++;
  frame.commandId = kLevelControlCommandStepWithOnOff;
  if (!ZigbeeCodec::buildZclFrame(frame, commandPayload,
                                  static_cast<uint8_t>(sizeof(commandPayload)),
                                  payload, &payloadLength)) {
    return false;
  }

  return queuePendingApsFrameExtended(
      node, kZigbeeApsDeliveryGroup, groupId, kZigbeeClusterLevelControl,
      kZigbeeProfileHomeAutomation, 0U, kCoordinatorEndpoint, payload,
      payloadLength);
}

bool queueDemoGroupEnrollment() {
  bool queuedAny = false;
  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    NodeEntry& node = g_nodes[i];
    if (!node.used || node.shortAddress == 0U || node.endpoint == 0U ||
        node.demoGroupConfigured ||
        (!node.supportsOnOff && !node.supportsLevelControl)) {
      continue;
    }
    queuedAny = queueAddGroup(&node, kDemoGroupId, "DemoGrp") || queuedAny;
  }
  return queuedAny;
}

bool queueDemoGroupOnOff(uint8_t commandId) {
  bool queuedAny = false;
  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    queuedAny = queueGroupOnOffCommand(&g_nodes[i], kDemoGroupId, commandId) ||
                queuedAny;
  }
  return queuedAny;
}

bool queueDemoGroupLevelMoveToLevel(uint8_t level) {
  bool queuedAny = false;
  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    queuedAny =
        queueGroupLevelMoveToLevel(&g_nodes[i], kDemoGroupId, level) ||
        queuedAny;
  }
  return queuedAny;
}

bool queueDemoGroupLevelStep(bool increase, uint8_t stepSize) {
  bool queuedAny = false;
  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    queuedAny =
        queueGroupLevelStep(&g_nodes[i], kDemoGroupId, increase, stepSize) ||
        queuedAny;
  }
  return queuedAny;
}

NodeEntry* firstOnOffNode() {
  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    if (g_nodes[i].used && g_nodes[i].shortAddress != 0U &&
        g_nodes[i].supportsOnOff && g_nodes[i].endpoint != 0U) {
      return &g_nodes[i];
    }
  }
  return nullptr;
}

NodeEntry* firstLevelNode() {
  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    if (g_nodes[i].used && g_nodes[i].shortAddress != 0U &&
        g_nodes[i].supportsLevelControl && g_nodes[i].endpoint != 0U) {
      return &g_nodes[i];
    }
  }
  return nullptr;
}

NodeEntry* firstIdentifyNode() {
  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    if (g_nodes[i].used && g_nodes[i].shortAddress != 0U &&
        g_nodes[i].supportsIdentify && g_nodes[i].endpoint != 0U) {
      return &g_nodes[i];
    }
  }
  return nullptr;
}

NodeEntry* firstJoinedNode() {
  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    if (g_nodes[i].used && g_nodes[i].shortAddress != 0U) {
      return &g_nodes[i];
    }
  }
  return nullptr;
}

void clearAllNodes() {
  memset(g_nodes, 0, sizeof(g_nodes));
  clearAlternateNetworkKey();
}

void clearNodeInterviewState(NodeEntry* node) {
  if (node == nullptr) {
    return;
  }
  node->haveNodeDescriptor = false;
  node->logicalType = 0U;
  node->macCapabilityFlags = 0U;
  node->manufacturerCode = 0U;
  node->havePowerDescriptor = false;
  node->availablePowerSources = 0U;
  node->currentPowerSource = 0U;
  node->currentPowerSourceLevel = 0U;
  node->haveBasicZclVersion = false;
  node->basicZclVersion = 0U;
  node->haveBasicApplicationVersion = false;
  node->basicApplicationVersion = 0U;
  node->haveBasicStackVersion = false;
  node->basicStackVersion = 0U;
  node->haveBasicHwVersion = false;
  node->basicHwVersion = 0U;
  node->haveBasicPowerSource = false;
  node->basicPowerSource = 0U;
  node->basicManufacturerName[0] = '\0';
  node->basicModelIdentifier[0] = '\0';
  node->basicSwBuildId[0] = '\0';
  node->haveOnOffState = false;
  node->onOffState = false;
  node->haveLevelState = false;
  node->levelState = 0U;
  node->haveTemperatureState = false;
  node->temperatureCentiDegrees = 0;
  node->haveBatteryVoltage = false;
  node->batteryVoltageDecivolts = 0U;
  node->haveBatteryPercentage = false;
  node->batteryPercentageRemainingHalf = 0U;
  node->haveIdentifyTime = false;
  node->identifyTimeSeconds = 0U;
}

void copyNodeString(char* destination, size_t destinationSize,
                    const ZigbeeAttributeValue& value) {
  if (destination == nullptr || destinationSize == 0U) {
    return;
  }
  destination[0] = '\0';
  if (value.type != ZigbeeZclDataType::kCharString ||
      value.stringValue == nullptr) {
    return;
  }

  size_t copyLength = value.stringLength;
  if (copyLength >= destinationSize) {
    copyLength = destinationSize - 1U;
  }
  memcpy(destination, value.stringValue, copyLength);
  destination[copyLength] = '\0';
}

void printCentiDegrees(int16_t centiDegrees) {
  if (centiDegrees < 0) {
    Serial.print("-");
    centiDegrees = static_cast<int16_t>(-centiDegrees);
  }
  Serial.print(centiDegrees / 100);
  Serial.print(".");
  const uint8_t fraction = static_cast<uint8_t>(centiDegrees % 100);
  if (fraction < 10U) {
    Serial.print("0");
  }
  Serial.print(fraction);
}

void updateNodeAttributeState(NodeEntry* node, uint16_t clusterId,
                              const ZigbeeAttributeValue& value,
                              uint16_t attributeId) {
  if (node == nullptr) {
    return;
  }

  if (clusterId == kZigbeeClusterBasic) {
    switch (attributeId) {
      case 0x0000U:
        if (value.type == ZigbeeZclDataType::kUint8) {
          node->haveBasicZclVersion = true;
          node->basicZclVersion = value.data.u8;
        }
        return;
      case 0x0001U:
        if (value.type == ZigbeeZclDataType::kUint8) {
          node->haveBasicApplicationVersion = true;
          node->basicApplicationVersion = value.data.u8;
        }
        return;
      case 0x0002U:
        if (value.type == ZigbeeZclDataType::kUint8) {
          node->haveBasicStackVersion = true;
          node->basicStackVersion = value.data.u8;
        }
        return;
      case 0x0003U:
        if (value.type == ZigbeeZclDataType::kUint8) {
          node->haveBasicHwVersion = true;
          node->basicHwVersion = value.data.u8;
        }
        return;
      case 0x0004U:
        copyNodeString(node->basicManufacturerName,
                       sizeof(node->basicManufacturerName), value);
        return;
      case 0x0005U:
        copyNodeString(node->basicModelIdentifier,
                       sizeof(node->basicModelIdentifier), value);
        return;
      case 0x0007U:
        if (value.type == ZigbeeZclDataType::kUint8) {
          node->haveBasicPowerSource = true;
          node->basicPowerSource = value.data.u8;
        }
        return;
      case 0x4000U:
        copyNodeString(node->basicSwBuildId, sizeof(node->basicSwBuildId),
                       value);
        return;
      default:
        break;
    }
  }

  if (clusterId == kZigbeeClusterOnOff && attributeId == 0x0000U &&
      value.type == ZigbeeZclDataType::kBoolean) {
    node->haveOnOffState = true;
    node->onOffState = value.data.boolValue;
    return;
  }

  if (clusterId == kZigbeeClusterLevelControl && attributeId == 0x0000U &&
      value.type == ZigbeeZclDataType::kUint8) {
    node->haveLevelState = true;
    node->levelState = value.data.u8;
    return;
  }

  if (clusterId == kZigbeeClusterTemperatureMeasurement &&
      attributeId == 0x0000U && value.type == ZigbeeZclDataType::kInt16) {
    node->haveTemperatureState = true;
    node->temperatureCentiDegrees = value.data.i16;
    return;
  }

  if (clusterId == kZigbeeClusterPowerConfiguration &&
      attributeId == 0x0020U && value.type == ZigbeeZclDataType::kUint8) {
    node->haveBatteryVoltage = true;
    node->batteryVoltageDecivolts = value.data.u8;
    return;
  }

  if (clusterId == kZigbeeClusterPowerConfiguration &&
      attributeId == 0x0021U && value.type == ZigbeeZclDataType::kUint8) {
    node->haveBatteryPercentage = true;
    node->batteryPercentageRemainingHalf = value.data.u8;
    return;
  }

  if (clusterId == kZigbeeClusterIdentify && attributeId == 0x0000U &&
      value.type == ZigbeeZclDataType::kUint16) {
    node->haveIdentifyTime = true;
    node->identifyTimeSeconds = value.data.u16;
  }
}

void applyReadAttributeState(NodeEntry* node, uint16_t clusterId,
                             const ZigbeeReadAttributeRecord* records,
                             uint8_t recordCount) {
  if (node == nullptr || records == nullptr) {
    return;
  }
  for (uint8_t i = 0U; i < recordCount; ++i) {
    if (records[i].status != 0x00U) {
      continue;
    }
    updateNodeAttributeState(node, clusterId, records[i].value,
                             records[i].attributeId);
  }
}

void applyReportedAttributeState(NodeEntry* node, uint16_t clusterId,
                                 const ZigbeeAttributeReportRecord* records,
                                 uint8_t recordCount) {
  if (node == nullptr || records == nullptr) {
    return;
  }
  for (uint8_t i = 0U; i < recordCount; ++i) {
    updateNodeAttributeState(node, clusterId, records[i].value,
                             records[i].attributeId);
  }
}

bool queueNextReportingStep(NodeEntry* node) {
  if (node == nullptr) {
    return false;
  }
  if (node->supportsOnOff && !node->onOffBindingConfigured) {
    return queueBindRequest(node, kZigbeeClusterOnOff);
  }
  if (node->supportsOnOff && !node->onOffReportingConfigured) {
    return queueOnOffConfigureReporting(node);
  }
  if (node->supportsLevelControl && !node->levelBindingConfigured) {
    return queueBindRequest(node, kZigbeeClusterLevelControl);
  }
  if (node->supportsLevelControl && !node->levelReportingConfigured) {
    return queueLevelConfigureReporting(node);
  }
  if (node->supportsTemperature && !node->temperatureBindingConfigured) {
    return queueBindRequest(node, kZigbeeClusterTemperatureMeasurement);
  }
  if (node->supportsTemperature && !node->temperatureReportingConfigured) {
    return queueTemperatureConfigureReporting(node);
  }
  if (node->supportsPowerConfiguration && !node->powerBindingConfigured) {
    return queueBindRequest(node, kZigbeeClusterPowerConfiguration);
  }
  if (node->supportsPowerConfiguration && !node->powerReportingConfigured) {
    return queuePowerConfigureReporting(node);
  }
  return false;
}

void printAttributeValue(const ZigbeeAttributeValue& value) {
  switch (value.type) {
    case ZigbeeZclDataType::kBoolean:
      Serial.print(value.data.boolValue ? "true" : "false");
      break;
    case ZigbeeZclDataType::kBitmap8:
    case ZigbeeZclDataType::kUint8:
      Serial.print(value.data.u8);
      break;
    case ZigbeeZclDataType::kBitmap16:
    case ZigbeeZclDataType::kUint16:
      Serial.print(value.data.u16);
      break;
    case ZigbeeZclDataType::kInt16:
      Serial.print(value.data.i16);
      break;
    case ZigbeeZclDataType::kUint32:
      Serial.print(value.data.u32);
      break;
    case ZigbeeZclDataType::kCharString:
      for (uint8_t i = 0U; i < value.stringLength; ++i) {
        Serial.print(value.stringValue[i]);
      }
      break;
    default:
      Serial.print("unsupported");
      break;
  }
}

void listNodes() {
  Serial.print("nodes\r\n");
  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    if (!g_nodes[i].used) {
      continue;
    }
    Serial.print(" slot=");
    Serial.print(i);
    Serial.print(" ieee=0x");
    Serial.print(static_cast<uint32_t>(g_nodes[i].ieeeAddress >> 32U), HEX);
    Serial.print(static_cast<uint32_t>(g_nodes[i].ieeeAddress & 0xFFFFFFFFUL), HEX);
    Serial.print(" short=0x");
    Serial.print(g_nodes[i].shortAddress, HEX);
    Serial.print(" ep=");
    Serial.print(g_nodes[i].endpoint);
    Serial.print(" dev=0x");
    Serial.print(g_nodes[i].deviceId, HEX);
    Serial.print(" onoff=");
    Serial.print(g_nodes[i].supportsOnOff ? "yes" : "no");
    Serial.print(" level=");
    Serial.print(g_nodes[i].supportsLevelControl ? "yes" : "no");
    Serial.print(" identify=");
    if (g_nodes[i].supportsIdentify) {
      if (g_nodes[i].haveIdentifyTime) {
        Serial.print(g_nodes[i].identifyTimeSeconds);
      } else {
        Serial.print("?");
      }
    } else {
      Serial.print("no");
    }
    Serial.print(" temp=");
    if (g_nodes[i].supportsTemperature) {
      if (g_nodes[i].haveTemperatureState) {
        printCentiDegrees(g_nodes[i].temperatureCentiDegrees);
        Serial.print("C");
      } else {
        Serial.print("?");
      }
    } else {
      Serial.print("no");
    }
    Serial.print(" batt=");
    if (g_nodes[i].haveBatteryVoltage || g_nodes[i].haveBatteryPercentage) {
      if (g_nodes[i].haveBatteryVoltage) {
        Serial.print(g_nodes[i].batteryVoltageDecivolts / 10);
        Serial.print(".");
        Serial.print(g_nodes[i].batteryVoltageDecivolts % 10);
        Serial.print("V");
      } else {
        Serial.print("?");
      }
      Serial.print("/");
      if (g_nodes[i].haveBatteryPercentage) {
        Serial.print(g_nodes[i].batteryPercentageRemainingHalf / 2);
        if ((g_nodes[i].batteryPercentageRemainingHalf & 0x01U) != 0U) {
          Serial.print(".5");
        }
        Serial.print("%");
      } else {
        Serial.print("?");
      }
    } else {
      Serial.print("?");
    }
    Serial.print(" group=");
    Serial.print(g_nodes[i].demoGroupConfigured ? "0x1001" : "no");
    Serial.print(" lk=");
    Serial.print(
        ZigbeeCommissioning::keyModeName(g_nodes[i].preconfiguredKeyMode));
    Serial.print(" tc=");
    Serial.print(trustCenterStateName(g_nodes[i].trustCenterState));
    Serial.print(" nwk_seq=");
    Serial.print(g_nodes[i].currentNetworkKeySequence);
    Serial.print(" nwk_sec=");
    Serial.print(g_nodes[i].secureNwkSeen ? "yes" : "no");
    Serial.print(" timeout=");
    if (g_nodes[i].endDeviceTimeoutNegotiated) {
      Serial.print("0x");
      Serial.print(g_nodes[i].endDeviceTimeoutIndex, HEX);
    } else {
      Serial.print("no");
    }
    Serial.print(" pending=");
    Serial.print((g_nodes[i].pending.used ||
                  g_nodes[i].pendingAssociationResponse ||
                  g_nodes[i].pendingTransportKey ||
                  g_nodes[i].pendingSwitchKey ||
                  g_nodes[i].pendingSecureRejoin ||
                  nodeHasPendingApsAck(g_nodes[i]))
                     ? "yes"
                     : "no");
    Serial.print(" rptv=");
    if (g_nodes[i].supportsOnOff) {
      Serial.print(g_nodes[i].onOffReportingVerified ? "O" : "o");
    }
    if (g_nodes[i].supportsLevelControl) {
      Serial.print(g_nodes[i].levelReportingVerified ? "L" : "l");
    }
    if (g_nodes[i].supportsTemperature) {
      Serial.print(g_nodes[i].temperatureReportingVerified ? "T" : "t");
    }
    if (g_nodes[i].supportsPowerConfiguration) {
      Serial.print(g_nodes[i].powerReportingVerified ? "P" : "p");
    }
    Serial.print(" state=");
    if (g_nodes[i].haveOnOffState) {
      Serial.print(g_nodes[i].onOffState ? "ON" : "OFF");
    } else {
      Serial.print("?");
    }
    if (g_nodes[i].supportsLevelControl) {
      Serial.print(" lvl=");
      if (g_nodes[i].haveLevelState) {
      Serial.print(g_nodes[i].levelState);
      } else {
        Serial.print("?");
      }
    }
    if (g_nodes[i].basicManufacturerName[0] != '\0' ||
        g_nodes[i].basicModelIdentifier[0] != '\0') {
      Serial.print(" basic=");
      Serial.print(g_nodes[i].basicManufacturerName[0] != '\0'
                       ? g_nodes[i].basicManufacturerName
                       : "?");
      Serial.print("/");
      Serial.print(g_nodes[i].basicModelIdentifier[0] != '\0'
                       ? g_nodes[i].basicModelIdentifier
                       : "?");
    }
    if (g_nodes[i].basicSwBuildId[0] != '\0') {
      Serial.print(" sw=");
      Serial.print(g_nodes[i].basicSwBuildId);
    }
    if (g_nodes[i].haveBasicPowerSource) {
      Serial.print(" psrc=0x");
      Serial.print(g_nodes[i].basicPowerSource, HEX);
    }
    Serial.print("\r\n");
  }
}

void handleAssociationRequest(const ZigbeeMacAssociationRequestView& request,
                              int8_t rssiDbm) {
  if (request.coordinatorPanId != kPanId ||
      request.coordinatorShort != kCoordinatorShort) {
    return;
  }
  if (!g_permitJoinEnabled) {
    Serial.print("assoc_drop reason=permit_join_closed ieee=0x");
    Serial.print(static_cast<uint32_t>(request.deviceExtended >> 32U), HEX);
    Serial.print(static_cast<uint32_t>(request.deviceExtended & 0xFFFFFFFFUL),
                 HEX);
    Serial.print("\r\n");
    return;
  }

  NodeEntry* node = allocateNode(request.deviceExtended);
  if (node == nullptr) {
    Serial.print("assoc_drop reason=no_slot\r\n");
    return;
  }

  node->pending.used = false;
  node->pending.payloadLength = 0U;
  node->pendingMac.used = false;
  node->pendingMac.length = 0U;
  node->pendingNwkResponse = false;
  node->pendingTransportKey = false;
  node->pendingNetworkKeyUpdate = false;
  node->pendingSwitchKey = false;
  clearPendingApsAck(node);
  clearRecentInboundAps(node);
  clearNodeInterviewState(node);
  node->endpoint = 0U;
  node->profileId = 0U;
  node->deviceId = 0U;
  node->supportsOnOff = false;
  node->supportsLevelControl = false;
  node->supportsIdentify = false;
  node->supportsTemperature = false;
  node->supportsPowerConfiguration = false;
  node->basicRead = false;
  node->onOffBindingConfigured = false;
  node->levelBindingConfigured = false;
  node->temperatureBindingConfigured = false;
  node->powerBindingConfigured = false;
  node->onOffReportingConfigured = false;
  node->levelReportingConfigured = false;
  node->temperatureReportingConfigured = false;
  node->powerReportingConfigured = false;
  node->onOffReportingVerified = false;
  node->levelReportingVerified = false;
  node->temperatureReportingVerified = false;
  node->powerReportingVerified = false;
  node->awaitingBindResponse = false;
  node->awaitingClusterId = 0U;
  node->demoGroupConfigured = false;
  node->trustCenterState = TrustCenterNodeState::kIdle;
  if (node->shortAddress == 0U) {
    node->pendingAssignedShort = allocateShortAddress();
    node->secureNwkSeen = false;
    node->lastInboundSecurityFrameCounter = 0U;
    node->currentNetworkKeySequence = 0U;
    node->preconfiguredKeyMode = ZigbeePreconfiguredKeyMode::kNone;
    node->pendingSecureRejoin = false;
  } else {
    node->pendingAssignedShort = node->shortAddress;
    node->pendingSecureRejoin = secureRejoinAllowed(node);
    if (!node->pendingSecureRejoin) {
      node->secureNwkSeen = false;
      node->lastInboundSecurityFrameCounter = 0U;
      node->currentNetworkKeySequence = 0U;
    }
  }
  node->pendingAssociationStatus = 0U;
  node->pendingAssociationResponse = true;
  node->lastSeenMs = millis();

  Serial.print("assoc ieee=0x");
  Serial.print(static_cast<uint32_t>(request.deviceExtended >> 32U), HEX);
  Serial.print(static_cast<uint32_t>(request.deviceExtended & 0xFFFFFFFFUL), HEX);
  Serial.print(" assigned=0x");
  Serial.print(node->pendingAssignedShort, HEX);
  Serial.print(" rejoin=");
  Serial.print(node->pendingSecureRejoin ? "yes" : "no");
  Serial.print(" cap=0x");
  Serial.print(request.capabilityInformation, HEX);
  Serial.print(" rssi=");
  Serial.print(rssiDbm);
  Serial.print("dBm\r\n");
}

void handleDataRequest(const ZigbeeMacFrame& frame) {
  if (frame.source.mode != ZigbeeMacAddressMode::kExtended) {
    return;
  }

  NodeEntry* node = findNodeByIeee(frame.source.extendedAddress);
  if (node == nullptr) {
    return;
  }

  node->lastSeenMs = millis();
  if (node->pendingAssociationResponse) {
    const bool sent = sendPendingAssociationResponse(node);
    Serial.print("assoc_rsp ");
    Serial.print(sent ? "OK" : "FAIL");
    Serial.print(" short=0x");
    Serial.print(node->pendingAssignedShort, HEX);
    Serial.print("\r\n");
    return;
  }

  if (node->pending.used || node->pendingTransportKey ||
      node->pendingSecureRejoin || node->pendingSwitchKey ||
      node->pendingNwkResponse) {
    if (node->pendingNwkResponse && node->pendingMac.used) {
      const bool sent = sendPsdu(node->pendingMac.psdu, node->pendingMac.length);
      if (sent) {
        node->pendingNwkResponse = false;
        node->pendingMac.used = false;
        node->pendingMac.length = 0U;
        pumpImmediateResponseWindow(kPollFollowUpListenBudgetUs);
      }
      Serial.print("poll_deliver ");
      Serial.print(sent ? "OK" : "FAIL");
      Serial.print(" dst=0x");
      Serial.print(node->shortAddress, HEX);
      Serial.print("\r\n");
      return;
    }
    const bool readyForSend =
        !node->pending.used ||
        node->pending.deliveryMode != kZigbeeApsDeliveryUnicast ||
        pendingApsAckSlotAvailable(*node, millis());
    if (readyForSend) {
      const bool sent = sendPendingApsFrame(node);
      if (sent) {
        pumpImmediateResponseWindow(kPollFollowUpListenBudgetUs);
      }
      Serial.print("poll_deliver ");
      Serial.print(sent ? "OK" : "FAIL");
      Serial.print(" dst=0x");
      Serial.print(node->shortAddress, HEX);
      Serial.print("\r\n");
    }
  }
}

void handleOrphanNotification(const ZigbeeMacOrphanNotificationView& orphan,
                              int8_t rssiDbm) {
  NodeEntry* node = findNodeByIeee(orphan.deviceExtended);
  if (node == nullptr || node->shortAddress == 0U || !secureRejoinAllowed(node)) {
    return;
  }

  if (!sendCoordinatorRealignment(node)) {
    return;
  }

  node->pendingSecureRejoin = true;
  node->trustCenterState = TrustCenterNodeState::kWaitingUpdateDevice;
  node->pendingMac.used =
      buildUpdateDevicePsdu(node, node->pendingMac.psdu, &node->pendingMac.length);
  node->lastSeenMs = millis();
  Serial.print("orphan_realign short=0x");
  Serial.print(node->shortAddress, HEX);
  Serial.print(" lk=");
  Serial.print(ZigbeeCommissioning::keyModeName(node->preconfiguredKeyMode));
  Serial.print(" tc=");
  Serial.print(trustCenterStateName(node->trustCenterState));
  Serial.print(" prepared=");
  Serial.print(node->pendingMac.used ? "yes" : "no");
  Serial.print(" rssi=");
  Serial.print(rssiDbm);
  Serial.print("dBm\r\n");
}

void handleNwkRejoinRequest(NodeEntry* node,
                            const ZigbeeNwkRejoinRequest& request) {
  if (node == nullptr || !request.valid) {
    return;
  }

  node->pendingSecureRejoin = secureRejoinAllowed(node);
  if (node->pendingSecureRejoin) {
    node->trustCenterState = TrustCenterNodeState::kWaitingUpdateDevice;
    node->pendingMac.used =
        buildUpdateDevicePsdu(node, node->pendingMac.psdu, &node->pendingMac.length);
  }
  node->lastSeenMs = millis();
  const bool sent = sendNwkRejoinResponse(node, 0x00U);
  Serial.print("nwk_rejoin short=0x");
  Serial.print(node->shortAddress, HEX);
  Serial.print(" cap=0x");
  Serial.print(request.capabilityInformation, HEX);
  Serial.print(" tc=");
  Serial.print(trustCenterStateName(node->trustCenterState));
  Serial.print(" prepared=");
  Serial.print(node->pendingMac.used ? "yes" : "no");
  Serial.print(" rsp=");
  Serial.print(sent ? "OK" : "FAIL");
  Serial.print("\r\n");
}

void handleEndDeviceTimeoutRequest(
    NodeEntry* node, const ZigbeeNwkEndDeviceTimeoutRequest& request) {
  if (node == nullptr || !request.valid) {
    return;
  }

  node->endDeviceTimeoutIndex = request.requestedTimeout;
  node->endDeviceConfiguration = request.endDeviceConfiguration;
  node->parentInformation =
      kZigbeeNwkParentInfoMacDataPollKeepalive |
      kZigbeeNwkParentInfoEndDeviceTimeoutSupported;
  node->endDeviceTimeoutNegotiated = true;
  uint8_t payload[8] = {0U};
  uint8_t payloadLength = 0U;
  const bool prepared =
      ZigbeeCodec::buildNwkEndDeviceTimeoutResponseCommand(
          kZigbeeNwkEndDeviceTimeoutSuccess, node->parentInformation, payload,
          &payloadLength);
  const bool sent =
      prepared && queuePendingNwkCommand(node, payload, payloadLength);
  Serial.print("end_device_timeout short=0x");
  Serial.print(node->shortAddress, HEX);
  Serial.print(" req=0x");
  Serial.print(request.requestedTimeout, HEX);
  Serial.print(" rsp=");
  Serial.print(sent ? "QUEUED" : "FAIL");
  Serial.print("\r\n");
}

void handleZdoFrame(NodeEntry* node, const ZigbeeApsDataFrame& aps) {
  if (node == nullptr) {
    return;
  }

  if (aps.clusterId == kZigbeeZdoMgmtPermitJoinRequest) {
    uint8_t transactionSequence = 0U;
    uint8_t permitDurationSeconds = 0U;
    bool trustCenterSignificance = false;
    if (!ZigbeeCodec::parseZdoMgmtPermitJoinRequest(
            aps.payload, aps.payloadLength, &transactionSequence,
            &permitDurationSeconds, &trustCenterSignificance)) {
      return;
    }

    if (permitDurationSeconds == 0U) {
      g_permitJoinEnabled = false;
      g_permitJoinDeadlineMs = 0U;
    } else {
      g_permitJoinEnabled = true;
      g_permitJoinDeadlineMs =
          (permitDurationSeconds == 0xFFU)
              ? 0U
              : (millis() +
                 (static_cast<uint32_t>(permitDurationSeconds) * 1000UL));
    }

    Serial.print("mgmt_permit_join short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" duration_s=");
    Serial.print(permitDurationSeconds);
    Serial.print(" tc=");
    Serial.print(trustCenterSignificance ? "1" : "0");
    Serial.print("\r\n");

    const uint8_t responsePayload[2] = {transactionSequence, 0x00U};
    (void)sendApsFrame(node->shortAddress, 0U,
                       kZigbeeZdoMgmtPermitJoinResponse, kZigbeeProfileZdo, 0U,
                       responsePayload, sizeof(responsePayload));
    return;
  }

  if (aps.clusterId == kZigbeeZdoDeviceAnnounce && aps.payloadLength >= 12U) {
    const uint16_t announcedShort =
        static_cast<uint16_t>(aps.payload[1]) |
        (static_cast<uint16_t>(aps.payload[2]) << 8U);
    node->shortAddress = announcedShort;
    node->announced = true;
    Serial.print("device_announce short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" ieee=0x");
    Serial.print(static_cast<uint32_t>(node->ieeeAddress >> 32U), HEX);
    Serial.print(static_cast<uint32_t>(node->ieeeAddress & 0xFFFFFFFFUL), HEX);
    Serial.print("\r\n");
    if (!node->haveNodeDescriptor) {
      (void)queueNodeDescriptorRequest(node);
    } else if (!node->havePowerDescriptor) {
      (void)queuePowerDescriptorRequest(node);
    } else if (node->endpoint == 0U) {
      (void)queueActiveEndpointsRequest(node);
    }
    return;
  }

  if (aps.clusterId == kZigbeeZdoNodeDescriptorResponse) {
    ZigbeeZdoNodeDescriptorResponseView view{};
    if (!ZigbeeCodec::parseZdoNodeDescriptorResponse(
            aps.payload, aps.payloadLength, &view) ||
        !view.valid || view.status != 0x00U) {
      return;
    }

    node->haveNodeDescriptor = true;
    node->logicalType = view.logicalType;
    node->macCapabilityFlags = view.macCapabilityFlags;
    node->manufacturerCode = view.manufacturerCode;
    Serial.print("node_desc short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" logical_type=0x");
    Serial.print(node->logicalType, HEX);
    Serial.print(" mac_cap=0x");
    Serial.print(node->macCapabilityFlags, HEX);
    Serial.print(" mfg=0x");
    Serial.print(node->manufacturerCode, HEX);
    Serial.print("\r\n");
    (void)queuePowerDescriptorRequest(node);
    return;
  }

  if (aps.clusterId == kZigbeeZdoPowerDescriptorResponse) {
    ZigbeeZdoPowerDescriptorResponseView view{};
    if (!ZigbeeCodec::parseZdoPowerDescriptorResponse(
            aps.payload, aps.payloadLength, &view) ||
        !view.valid || view.status != 0x00U) {
      return;
    }

    node->havePowerDescriptor = true;
    node->availablePowerSources = view.availablePowerSources;
    node->currentPowerSource = view.currentPowerSource;
    node->currentPowerSourceLevel = view.currentPowerSourceLevel;
    Serial.print("power_desc short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" avail=0x");
    Serial.print(node->availablePowerSources, HEX);
    Serial.print(" current=0x");
    Serial.print(node->currentPowerSource, HEX);
    Serial.print(" level=0x");
    Serial.print(node->currentPowerSourceLevel, HEX);
    Serial.print("\r\n");
    (void)queueActiveEndpointsRequest(node);
    return;
  }

  if (aps.clusterId == kZigbeeZdoActiveEndpointsResponse) {
    ZigbeeZdoActiveEndpointsResponseView view{};
    if (!ZigbeeCodec::parseZdoActiveEndpointsResponse(
            aps.payload, aps.payloadLength, &view) ||
        !view.valid || view.status != 0x00U || view.endpointCount == 0U) {
      return;
    }

    node->endpoint = view.endpoints[0];
    Serial.print("active_ep short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" ep=");
    Serial.print(node->endpoint);
    Serial.print("\r\n");
    (void)queueSimpleDescriptorRequest(node, node->endpoint);
    return;
  }

  if (aps.clusterId == kZigbeeZdoBindResponse) {
    uint8_t transactionSequence = 0U;
    uint8_t status = 0xFFU;
    if (!ZigbeeCodec::parseZdoStatusResponse(aps.payload, aps.payloadLength,
                                             &transactionSequence, &status)) {
      return;
    }

    Serial.print("bind_rsp short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" cluster=0x");
    Serial.print(node->awaitingClusterId, HEX);
    Serial.print(" status=0x");
    Serial.print(status, HEX);
    Serial.print("\r\n");

    if (node->awaitingClusterId == kZigbeeClusterOnOff) {
      node->onOffBindingConfigured = true;
    } else if (node->awaitingClusterId == kZigbeeClusterLevelControl) {
      node->levelBindingConfigured = true;
    } else if (node->awaitingClusterId ==
               kZigbeeClusterTemperatureMeasurement) {
      node->temperatureBindingConfigured = true;
    } else if (node->awaitingClusterId == kZigbeeClusterPowerConfiguration) {
      node->powerBindingConfigured = true;
    }
    node->awaitingBindResponse = false;
    if (queueNextReportingStep(node)) {
      return;
    }
    node->stage = NodeStage::kReady;
    return;
  }

  if (aps.clusterId == kZigbeeZdoMgmtLeaveResponse) {
    uint8_t transactionSequence = 0U;
    uint8_t status = 0xFFU;
    if (!ZigbeeCodec::parseZdoStatusResponse(aps.payload, aps.payloadLength,
                                             &transactionSequence, &status)) {
      return;
    }

    Serial.print("leave_rsp short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" status=0x");
    Serial.print(status, HEX);
    Serial.print(" rejoin=");
    Serial.print(node->pendingLeaveWithRejoin ? "yes" : "no");
    Serial.print("\r\n");
    if (status == 0x00U) {
      if (node->pendingLeaveWithRejoin) {
        prepareNodeForRetainedRejoin(node);
      } else {
        memset(node, 0, sizeof(*node));
      }
    } else {
      node->pendingLeaveWithRejoin = false;
    }
    return;
  }

  if (aps.clusterId == kZigbeeZdoSimpleDescriptorResponse) {
    ZigbeeZdoSimpleDescriptorResponseView view{};
    if (!ZigbeeCodec::parseZdoSimpleDescriptorResponse(
            aps.payload, aps.payloadLength, &view) ||
        !view.valid || view.status != 0x00U) {
      return;
    }

    node->endpoint = view.endpoint;
    node->profileId = view.profileId;
    node->deviceId = view.deviceId;
    node->supportsOnOff = false;
    node->supportsLevelControl = false;
    node->supportsIdentify = false;
    node->supportsTemperature = false;
    node->supportsPowerConfiguration = false;
    node->haveIdentifyTime = false;
    node->identifyTimeSeconds = 0U;
    node->onOffBindingConfigured = false;
    node->levelBindingConfigured = false;
    node->temperatureBindingConfigured = false;
    node->powerBindingConfigured = false;
    node->onOffReportingConfigured = false;
    node->levelReportingConfigured = false;
    node->temperatureReportingConfigured = false;
    node->powerReportingConfigured = false;
    node->onOffReportingVerified = false;
    node->levelReportingVerified = false;
    node->temperatureReportingVerified = false;
    node->powerReportingVerified = false;
    node->awaitingBindResponse = false;
    node->awaitingClusterId = 0U;
    node->demoGroupConfigured = false;
    for (uint8_t i = 0U; i < view.inputClusterCount; ++i) {
      if (view.inputClusters[i] == kZigbeeClusterOnOff) {
        node->supportsOnOff = true;
      } else if (view.inputClusters[i] == kZigbeeClusterLevelControl) {
        node->supportsLevelControl = true;
      } else if (view.inputClusters[i] == kZigbeeClusterIdentify) {
        node->supportsIdentify = true;
      } else if (view.inputClusters[i] == kZigbeeClusterTemperatureMeasurement) {
        node->supportsTemperature = true;
      } else if (view.inputClusters[i] == kZigbeeClusterPowerConfiguration) {
        node->supportsPowerConfiguration = true;
      }
    }

    Serial.print("simple_desc short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" ep=");
    Serial.print(node->endpoint);
    Serial.print(" profile=0x");
    Serial.print(node->profileId, HEX);
    Serial.print(" device=0x");
    Serial.print(node->deviceId, HEX);
    Serial.print(" onoff=");
    Serial.print(node->supportsOnOff ? "yes" : "no");
    Serial.print(" identify=");
    Serial.print(node->supportsIdentify ? "yes" : "no");
    Serial.print(" level=");
    Serial.print(node->supportsLevelControl ? "yes" : "no");
    Serial.print(" temp=");
    Serial.print(node->supportsTemperature ? "yes" : "no");
    Serial.print("\r\n");

    node->basicReadBatchIndex = 0U;
    if (!queueBasicReadRequest(node)) {
      node->stage = NodeStage::kReady;
    }
  }
}

void handleHaFrame(NodeEntry* node, const ZigbeeApsDataFrame& aps) {
  if (node == nullptr) {
    return;
  }

  ZigbeeZclFrame zcl{};
  if (!ZigbeeCodec::parseZclFrame(aps.payload, aps.payloadLength, &zcl) ||
      !zcl.valid) {
    return;
  }

  if (zcl.frameType == ZigbeeZclFrameType::kGlobal &&
      zcl.commandId == 0x01U) {
    ZigbeeReadAttributeRecord records[8];
    uint8_t recordCount = 0U;
    if (!ZigbeeCodec::parseReadAttributesResponse(
            zcl.payload, zcl.payloadLength, records,
            static_cast<uint8_t>(sizeof(records) / sizeof(records[0])),
            &recordCount)) {
      return;
    }
    applyReadAttributeState(node, aps.clusterId, records, recordCount);

    Serial.print("read_rsp short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" cluster=0x");
    Serial.print(aps.clusterId, HEX);
    Serial.print(" ");
    for (uint8_t i = 0U; i < recordCount; ++i) {
      Serial.print("attr=0x");
      Serial.print(records[i].attributeId, HEX);
      Serial.print(":");
      if (records[i].status == 0x00U) {
        printAttributeValue(records[i].value);
      } else {
        Serial.print("status=0x");
        Serial.print(records[i].status, HEX);
      }
      Serial.print(" ");
    }
    Serial.print("\r\n");

    if (aps.clusterId == kZigbeeClusterBasic) {
      if (node->basicReadBatchIndex < 2U) {
        ++node->basicReadBatchIndex;
        if (!queueBasicReadRequest(node)) {
          node->basicRead = true;
          if (!queueNextReportingStep(node)) {
            node->stage = NodeStage::kReady;
          }
        }
      } else {
        node->basicRead = true;
        if (!queueNextReportingStep(node)) {
          node->stage = NodeStage::kReady;
        }
      }
    }
    return;
  }

  if (zcl.frameType == ZigbeeZclFrameType::kGlobal &&
      zcl.commandId == kZclCommandWriteAttributesResponse) {
    ZigbeeWriteAttributeStatusRecord records[8];
    uint8_t recordCount = 0U;
    if (!ZigbeeCodec::parseWriteAttributesResponse(
            zcl.payload, zcl.payloadLength, records,
            static_cast<uint8_t>(sizeof(records) / sizeof(records[0])),
            &recordCount)) {
      return;
    }

    Serial.print("write_attr_rsp short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" cluster=0x");
    Serial.print(aps.clusterId, HEX);
    for (uint8_t i = 0U; i < recordCount; ++i) {
      Serial.print(" status=0x");
      Serial.print(records[i].status, HEX);
      if (records[i].attributeId != 0U || records[i].status != 0x00U) {
        Serial.print(" attr=0x");
        Serial.print(records[i].attributeId, HEX);
      }
    }
    Serial.print("\r\n");
    return;
  }

  if (zcl.frameType == ZigbeeZclFrameType::kGlobal &&
      zcl.commandId == kZclCommandDiscoverAttributesExtendedResponse) {
    ZigbeeDiscoveredExtendedAttributeRecord records[8];
    uint8_t recordCount = 0U;
    bool discoveryComplete = false;
    if (!ZigbeeCodec::parseDiscoverAttributesExtendedResponse(
            zcl.payload, zcl.payloadLength, &discoveryComplete, records,
            static_cast<uint8_t>(sizeof(records) / sizeof(records[0])),
            &recordCount)) {
      return;
    }

    Serial.print("discover_attr_ext_rsp short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" cluster=0x");
    Serial.print(aps.clusterId, HEX);
    Serial.print(" complete=");
    Serial.print(discoveryComplete ? "yes" : "no");
    for (uint8_t i = 0U; i < recordCount; ++i) {
      Serial.print(" attr=0x");
      Serial.print(records[i].attributeId, HEX);
      Serial.print(" type=0x");
      Serial.print(static_cast<uint8_t>(records[i].dataType), HEX);
      Serial.print(" access=0x");
      Serial.print(records[i].accessControl, HEX);
    }
    Serial.print("\r\n");
    return;
  }

  if (zcl.frameType == ZigbeeZclFrameType::kGlobal &&
      zcl.commandId == kZclCommandConfigureReportingResponse) {
    ZigbeeConfigureReportingStatusRecord records[8];
    uint8_t recordCount = 0U;
    if (!ZigbeeCodec::parseConfigureReportingResponse(
            zcl.payload, zcl.payloadLength, records,
            static_cast<uint8_t>(sizeof(records) / sizeof(records[0])),
            &recordCount)) {
      return;
    }

    bool success = false;
    if (recordCount == 1U && records[0].status == 0x00U &&
        records[0].attributeId == 0U) {
      success = true;
    }
    Serial.print("cfg_reporting_rsp short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" cluster=0x");
    Serial.print(aps.clusterId, HEX);
    Serial.print(" status=");
    Serial.print(success ? "OK" : "FAIL");
    for (uint8_t i = 0U; !success && i < recordCount; ++i) {
      Serial.print(" attr=0x");
      Serial.print(records[i].attributeId, HEX);
      Serial.print(" dir=");
      Serial.print(records[i].direction);
      Serial.print(" code=0x");
      Serial.print(records[i].status, HEX);
    }
    Serial.print("\r\n");
    if (!success) {
      node->stage = NodeStage::kReady;
      return;
    }
    if (aps.clusterId == kZigbeeClusterOnOff) {
      node->onOffReportingConfigured = true;
    } else if (aps.clusterId == kZigbeeClusterLevelControl) {
      node->levelReportingConfigured = true;
    } else if (aps.clusterId == kZigbeeClusterTemperatureMeasurement) {
      node->temperatureReportingConfigured = true;
    } else if (aps.clusterId == kZigbeeClusterPowerConfiguration) {
      node->powerReportingConfigured = true;
    }
    if (queueReadReportingConfiguration(node, aps.clusterId)) {
      return;
    }
    if (queueNextReportingStep(node)) {
      return;
    }
    node->stage = NodeStage::kReady;
    return;
  }

  if (zcl.frameType == ZigbeeZclFrameType::kGlobal &&
      zcl.commandId == kZclCommandReadReportingConfigurationResponse) {
    ZigbeeReadReportingConfigurationResponseRecord records[8];
    uint8_t recordCount = 0U;
    if (!ZigbeeCodec::parseReadReportingConfigurationResponse(
            zcl.payload, zcl.payloadLength, records,
            static_cast<uint8_t>(sizeof(records) / sizeof(records[0])),
            &recordCount)) {
      return;
    }

    const bool verified =
        reportingConfigurationMatches(aps.clusterId, records, recordCount);
    Serial.print("read_reporting_rsp short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" cluster=0x");
    Serial.print(aps.clusterId, HEX);
    Serial.print(" verify=");
    Serial.print(verified ? "OK" : "FAIL");
    for (uint8_t i = 0U; i < recordCount; ++i) {
      Serial.print(" attr=0x");
      Serial.print(records[i].attributeId, HEX);
      Serial.print(" status=0x");
      Serial.print(records[i].status, HEX);
    }
    Serial.print("\r\n");

    if (aps.clusterId == kZigbeeClusterOnOff) {
      node->onOffReportingVerified = verified;
    } else if (aps.clusterId == kZigbeeClusterLevelControl) {
      node->levelReportingVerified = verified;
    } else if (aps.clusterId == kZigbeeClusterTemperatureMeasurement) {
      node->temperatureReportingVerified = verified;
    } else if (aps.clusterId == kZigbeeClusterPowerConfiguration) {
      node->powerReportingVerified = verified;
    }
    if (queueNextReportingStep(node)) {
      return;
    }
    node->stage = NodeStage::kReady;
    return;
  }

  if (aps.clusterId == kZigbeeClusterIdentify &&
      zcl.frameType == ZigbeeZclFrameType::kClusterSpecific &&
      zcl.commandId == kIdentifyCommandIdentify) {
    node->haveIdentifyTime = true;
    node->identifyTimeSeconds =
        (zcl.payloadLength >= 2U)
            ? static_cast<uint16_t>(zcl.payload[0]) |
                  (static_cast<uint16_t>(zcl.payload[1]) << 8U)
            : 0U;
    Serial.print("identify_query_rsp short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" timeout_s=");
    Serial.print(node->identifyTimeSeconds);
    Serial.print("\r\n");
    return;
  }

  if (aps.clusterId == kZigbeeClusterGroups &&
      zcl.frameType == ZigbeeZclFrameType::kClusterSpecific &&
      zcl.commandId == kGroupsCommandAddGroup && zcl.payloadLength >= 3U) {
    const uint8_t status = zcl.payload[0];
    const uint16_t groupId = static_cast<uint16_t>(zcl.payload[1]) |
                             (static_cast<uint16_t>(zcl.payload[2]) << 8U);
    if (status == 0x00U && groupId == kDemoGroupId) {
      node->demoGroupConfigured = true;
    }
    Serial.print("group_add_rsp short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" group=0x");
    Serial.print(groupId, HEX);
    Serial.print(" status=0x");
    Serial.print(status, HEX);
    Serial.print("\r\n");
    return;
  }

  if (zcl.frameType == ZigbeeZclFrameType::kGlobal &&
      zcl.commandId == kZclCommandReportAttributes) {
    ZigbeeAttributeReportRecord records[8];
    uint8_t recordCount = 0U;
    if (!ZigbeeCodec::parseAttributeReport(
            zcl.payload, zcl.payloadLength, records,
            static_cast<uint8_t>(sizeof(records) / sizeof(records[0])),
            &recordCount)) {
      return;
    }
    applyReportedAttributeState(node, aps.clusterId, records, recordCount);

    Serial.print("report short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" cluster=0x");
    Serial.print(aps.clusterId, HEX);
    Serial.print(" ");
    for (uint8_t i = 0U; i < recordCount; ++i) {
      Serial.print("attr=0x");
      Serial.print(records[i].attributeId, HEX);
      Serial.print(":");
      printAttributeValue(records[i].value);
      Serial.print(" ");
    }
    Serial.print("\r\n");
    return;
  }

  if (zcl.frameType == ZigbeeZclFrameType::kGlobal &&
      zcl.commandId == kZclCommandDefaultResponse && zcl.payloadLength >= 2U) {
    Serial.print("default_rsp short=0x");
    Serial.print(node->shortAddress, HEX);
    Serial.print(" cmd=0x");
    Serial.print(zcl.payload[0], HEX);
    Serial.print(" status=0x");
    Serial.print(zcl.payload[1], HEX);
    Serial.print("\r\n");
  }
}

void processIncomingFrame(const ZigbeeFrame& frame) {
  ZigbeeMacFrame mac{};
  if (!ZigbeeCodec::parseMacFrame(frame.psdu, frame.length, &mac) || !mac.valid) {
    return;
  }

  if (mac.frameType == ZigbeeMacFrameType::kCommand) {
    if (mac.commandId == kZigbeeMacCommandBeaconRequest) {
      (void)sendBeacon();
      return;
    }

    ZigbeeMacOrphanNotificationView orphan{};
    if (ZigbeeCodec::parseOrphanNotification(frame.psdu, frame.length,
                                             &orphan)) {
      handleOrphanNotification(orphan, frame.rssiDbm);
      return;
    }

    ZigbeeMacAssociationRequestView association{};
    if (ZigbeeCodec::parseAssociationRequest(frame.psdu, frame.length,
                                             &association)) {
      handleAssociationRequest(association, frame.rssiDbm);
      return;
    }

    if (mac.commandId == kZigbeeMacCommandDataRequest &&
        mac.destination.mode == ZigbeeMacAddressMode::kShort &&
        mac.destination.panId == kPanId &&
        mac.destination.shortAddress == kCoordinatorShort) {
      handleDataRequest(mac);
      return;
    }
    return;
  }

  ZigbeeDataFrameView macData{};
  if (!ZigbeeRadio::parseDataFrameShort(frame.psdu, frame.length, &macData) ||
      !macData.valid || macData.panId != kPanId ||
      macData.destinationShort != kCoordinatorShort) {
    return;
  }

  NodeEntry* node = findNodeByShort(macData.sourceShort);
  if (node == nullptr) {
    return;
  }
  node->lastSeenMs = millis();

  ZigbeeNetworkFrame nwk{};
  ZigbeeNwkSecurityHeader security{};
  uint8_t decryptedPayload[127] = {0U};
  uint8_t decryptedPayloadLength = 0U;
  const uint8_t expectedKeySequence =
      (node->currentNetworkKeySequence != 0U) ? node->currentNetworkKeySequence
                                              : g_activeNetworkKeySequence;
  const uint8_t* expectedKey = keyForSequence(expectedKeySequence);
  bool nwkValid = (expectedKey != nullptr) &&
                  ZigbeeSecurity::parseSecuredNwkFrame(
                      macData.payload, macData.payloadLength, expectedKey, &nwk,
                      &security, decryptedPayload, &decryptedPayloadLength,
                      node->ieeeAddress);
  if (nwkValid &&
      (!security.valid || security.sourceIeee != node->ieeeAddress ||
       security.keySequence != expectedKeySequence ||
       security.frameCounter <= node->lastInboundSecurityFrameCounter)) {
    return;
  }
  if (!nwkValid) {
    nwkValid =
        ZigbeeCodec::parseNwkFrame(macData.payload, macData.payloadLength, &nwk);
  }
  if (!nwkValid || !nwk.valid || nwk.destinationShort != kCoordinatorShort) {
    return;
  }
  if (security.valid) {
    node->secureNwkSeen = true;
    node->lastInboundSecurityFrameCounter = security.frameCounter;
  }

  if (nwk.frameType == ZigbeeNwkFrameType::kCommand) {
    ZigbeeNwkRejoinRequest rejoinRequest{};
    if (security.valid &&
        ZigbeeCodec::parseNwkRejoinRequestCommand(nwk.payload, nwk.payloadLength,
                                                  &rejoinRequest) &&
        rejoinRequest.valid) {
      handleNwkRejoinRequest(node, rejoinRequest);
      return;
    }

    ZigbeeNwkEndDeviceTimeoutRequest timeoutRequest{};
    if (security.valid &&
        ZigbeeCodec::parseNwkEndDeviceTimeoutRequestCommand(
            nwk.payload, nwk.payloadLength, &timeoutRequest) &&
        timeoutRequest.valid) {
      handleEndDeviceTimeoutRequest(node, timeoutRequest);
      return;
    }
    return;
  }

  ZigbeeApsDataFrame aps{};
  ZigbeeApsAcknowledgementFrame ack{};
  if (ZigbeeCodec::parseApsAcknowledgementFrame(nwk.payload, nwk.payloadLength,
                                                &ack) &&
      ack.valid) {
    uint8_t ackSlot = 0U;
    if (findPendingApsAckSlot(*node, ack, &ackSlot)) {
      Serial.print("aps_ack short=0x");
      Serial.print(node->shortAddress, HEX);
      Serial.print(" ctr=0x");
      Serial.print(ack.counter, HEX);
      Serial.print(" cluster=0x");
      Serial.print(ack.clusterId, HEX);
      Serial.print("\r\n");
      clearPendingApsAckSlot(node, ackSlot);
    }
    return;
  }
  if (!ZigbeeCodec::parseApsDataFrame(nwk.payload, nwk.payloadLength, &aps) ||
      !aps.valid) {
    return;
  }

  const uint32_t nowMs = millis();
  const bool duplicateAps = isRecentInboundApsDuplicate(node, aps, nowMs);

  if (aps.deliveryMode == kZigbeeApsDeliveryUnicast && aps.ackRequested) {
    (void)sendApsAcknowledgement(node, aps);
  }

  if (aps.profileId == kZigbeeProfileZdo) {
    if (duplicateAps) {
      Serial.print("aps_dup short=0x");
      Serial.print(node->shortAddress, HEX);
      Serial.print(" cluster=0x");
      Serial.print(aps.clusterId, HEX);
      Serial.print("\r\n");
      return;
    }
    rememberRecentInboundAps(node, aps);
    handleZdoFrame(node, aps);
    return;
  }
  if (aps.profileId == kZigbeeProfileHomeAutomation) {
    if (duplicateAps) {
      Serial.print("aps_dup short=0x");
      Serial.print(node->shortAddress, HEX);
      Serial.print(" cluster=0x");
      Serial.print(aps.clusterId, HEX);
      Serial.print("\r\n");
      return;
    }
    rememberRecentInboundAps(node, aps);
    handleHaFrame(node, aps);
  }
}

void pumpImmediateResponseWindow(uint32_t listenBudgetUs) {
  const uint32_t startUs = micros();
  while (static_cast<uint32_t>(micros() - startUs) < listenBudgetUs) {
    const uint32_t elapsedUs = static_cast<uint32_t>(micros() - startUs);
    const uint32_t remainingUs =
        (elapsedUs < listenBudgetUs) ? (listenBudgetUs - elapsedUs) : 0U;
    if (remainingUs == 0U) {
      break;
    }

    ZigbeeFrame frame{};
    if (!g_radio.receive(&frame, remainingUs, 400000UL)) {
      break;
    }
    processIncomingFrame(frame);
  }
}

void handleSerialCommands() {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch == 'b') {
      const bool sent = sendBeacon();
      Serial.print("beacon ");
      Serial.print(sent ? "OK" : "FAIL");
      Serial.print("\r\n");
    } else if (ch == 'l') {
      listNodes();
    } else if (ch == 'p') {
      g_permitJoinEnabled = true;
      g_permitJoinDeadlineMs = millis() + kPermitJoinWindowMs;
      Serial.print("permit_join open ms=");
      Serial.print(kPermitJoinWindowMs);
      Serial.print("\r\n");
    } else if (ch == 'c') {
      clearAllNodes();
      Serial.print("nodes cleared\r\n");
    } else if (ch == 'x') {
      g_permitJoinEnabled = false;
      g_permitJoinDeadlineMs = 0U;
      Serial.print("permit_join closed\r\n");
    } else if (ch == 'd') {
      NodeEntry* node = firstJoinedNode();
      const bool queued = (node != nullptr) && queueActiveEndpointsRequest(node);
      Serial.print("discover ");
      Serial.print(queued ? "OK" : "FAIL");
      Serial.print("\r\n");
    } else if (ch == 'i' || ch == 'e' || ch == 'u' || ch == 'j' || ch == 'w' ||
               ch == 'W' || ch == 's' || ch == 'S' || ch == 'I' ||
               ch == 'R' || ch == 'C') {
      NodeEntry* node = firstIdentifyNode();
      bool queued = false;
      if (ch == 'i') {
        queued = (node != nullptr) && queueIdentifyCommand(node, 5U);
        Serial.print("queue_identify ");
        Serial.print(queued ? "OK" : "FAIL");
        Serial.print(" timeout_s=5");
      } else if (ch == 'e') {
        queued =
            (node != nullptr) && queueIdentifyDiscoverAttributesExtended(node);
        Serial.print("queue_identify_discover_attr_ext ");
        Serial.print(queued ? "OK" : "FAIL");
      } else if (ch == 'u') {
        queued =
            (node != nullptr) && queueIdentifyWriteAttributeUndivided(node, 5U, false);
        Serial.print("queue_identify_write_undivided ");
        Serial.print(queued ? "OK" : "FAIL");
        Serial.print(" timeout_s=5");
      } else if (ch == 'j') {
        queued =
            (node != nullptr) && queueIdentifyWriteAttributeUndivided(node, 5U, true);
        Serial.print("queue_identify_write_undivided_fail ");
        Serial.print(queued ? "OK" : "FAIL");
        Serial.print(" timeout_s=5 attr=0xFFFC");
      } else if (ch == 'w') {
        queued = (node != nullptr) && queueIdentifyWriteAttribute(node, 5U, false);
        Serial.print("queue_identify_write ");
        Serial.print(queued ? "OK" : "FAIL");
        Serial.print(" timeout_s=5");
      } else if (ch == 'W') {
        queued = (node != nullptr) && queueIdentifyWriteAttribute(node, 5U, true);
        Serial.print("queue_identify_write_no_rsp ");
        Serial.print(queued ? "OK" : "FAIL");
        Serial.print(" timeout_s=5");
      } else if (ch == 's') {
        queued = (node != nullptr) && queueIdentifyQuery(node);
        Serial.print("queue_identify_query ");
        Serial.print(queued ? "OK" : "FAIL");
      } else if (ch == 'S') {
        queued =
            (node != nullptr) && queueTriggerEffect(node, kIdentifyEffectStopEffect);
        Serial.print("queue_trigger_effect ");
        Serial.print(queued ? "OK" : "FAIL");
        Serial.print(" effect=stop");
      } else if (ch == 'I') {
        queued = (node != nullptr) && queueTriggerEffect(node, kIdentifyEffectBlink);
        Serial.print("queue_trigger_effect ");
        Serial.print(queued ? "OK" : "FAIL");
        Serial.print(" effect=blink");
      } else if (ch == 'R') {
        queued =
            (node != nullptr) && queueTriggerEffect(node, kIdentifyEffectBreathe);
        Serial.print("queue_trigger_effect ");
        Serial.print(queued ? "OK" : "FAIL");
        Serial.print(" effect=breathe");
      } else {
        queued = (node != nullptr) &&
                 queueTriggerEffect(node, kIdentifyEffectChannelChange);
        Serial.print("queue_trigger_effect ");
        Serial.print(queued ? "OK" : "FAIL");
        Serial.print(" effect=channel_change");
      }
      Serial.print("\r\n");
    } else if (ch == 'v' || ch == 'V') {
      NodeEntry* node = firstJoinedNode();
      const bool requestRejoin = (ch == 'V');
      const bool queued =
          (node != nullptr) && queueMgmtLeaveRequest(node, requestRejoin);
      Serial.print("queue_leave ");
      Serial.print("rejoin=");
      Serial.print(requestRejoin ? "yes " : "no ");
      Serial.print(queued ? "OK" : "FAIL");
      Serial.print("\r\n");
    } else if (ch == 'k') {
      const bool queued = queueNetworkKeyUpdateRollout();
      Serial.print("queue_key_update ");
      Serial.print(queued ? "OK" : "FAIL");
      if (g_haveAlternateNetworkKey) {
        Serial.print(" seq=");
        Serial.print(g_alternateNetworkKeySequence);
      }
      Serial.print("\r\n");
    } else if (ch == 't' || ch == 'o' || ch == 'f') {
      NodeEntry* node = firstOnOffNode();
      uint8_t commandId = kOnOffCommandToggle;
      if (ch == 'o') {
        commandId = kOnOffCommandOn;
      } else if (ch == 'f') {
        commandId = kOnOffCommandOff;
      }
      const bool queued = (node != nullptr) && queueOnOffCommand(node, commandId);
      Serial.print("queue_cmd ");
      Serial.print(queued ? "OK" : "FAIL");
      Serial.print("\r\n");
    } else if (ch == 'U' || ch == 'D' || ch == 'M') {
      NodeEntry* node = firstLevelNode();
      bool queued = false;
      if (ch == 'U') {
        queued = (node != nullptr) && queueLevelStep(node, true, 32U);
      } else if (ch == 'D') {
        queued = (node != nullptr) && queueLevelStep(node, false, 32U);
      } else {
        queued = (node != nullptr) && queueLevelMoveToLevel(node, 128U);
      }
      Serial.print("queue_level ");
      Serial.print(queued ? "OK" : "FAIL");
      Serial.print("\r\n");
    } else if (ch == 'g') {
      const bool queued = queueDemoGroupEnrollment();
      Serial.print("queue_group_enroll ");
      Serial.print(queued ? "OK" : "FAIL");
      Serial.print(" group=0x");
      Serial.print(kDemoGroupId, HEX);
      Serial.print("\r\n");
    } else if (ch == 'O' || ch == 'F' || ch == 'T') {
      uint8_t commandId = kOnOffCommandToggle;
      if (ch == 'O') {
        commandId = kOnOffCommandOn;
      } else if (ch == 'F') {
        commandId = kOnOffCommandOff;
      }
      const bool queued = queueDemoGroupOnOff(commandId);
      Serial.print("queue_group_cmd ");
      Serial.print(queued ? "OK" : "FAIL");
      Serial.print(" group=0x");
      Serial.print(kDemoGroupId, HEX);
      Serial.print("\r\n");
    } else if (ch == '+' || ch == '-' || ch == 'm') {
      bool queued = false;
      if (ch == '+') {
        queued = queueDemoGroupLevelStep(true, 32U);
      } else if (ch == '-') {
        queued = queueDemoGroupLevelStep(false, 32U);
      } else {
        queued = queueDemoGroupLevelMoveToLevel(128U);
      }
      Serial.print("queue_group_level ");
      Serial.print(queued ? "OK" : "FAIL");
      Serial.print(" group=0x");
      Serial.print(kDemoGroupId, HEX);
      Serial.print("\r\n");
    }
  }
}

void pumpRadio() {
  ZigbeeFrame frame{};
  if (!g_radio.receive(&frame, 5000U, 900000UL)) {
    return;
  }
  processIncomingFrame(frame);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  const bool ok = g_radio.begin(kChannel, 8);
  Serial.print("\r\nZigbeeHaCoordinatorJoinDemo start\r\n");
  Serial.print("radio=");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print(" channel=");
  Serial.print(kChannel);
  Serial.print(" pan=0x");
  Serial.print(kPanId, HEX);
  Serial.print(" extpan=0x");
  Serial.print(static_cast<uint32_t>(kExtendedPanId >> 32U), HEX);
  Serial.print(static_cast<uint32_t>(kExtendedPanId & 0xFFFFFFFFUL), HEX);
  Serial.print(" nwk_seq=");
  Serial.print(g_activeNetworkKeySequence);
  Serial.print("\r\n");
  Serial.print("serial commands: b=beacon l=list p=permit_join c=clear_nodes x=close_join d=discover i=identify_5s e=identify_discover_attr_ext u=write_identify_5s_undivided j=write_identify_5s_undivided_fail w=write_identify_5s W=write_identify_5s_no_rsp s=identify_query S=identify_stop I=effect_blink R=effect_breathe C=effect_channel v=leave V=leave_rejoin k=key_update t=toggle o=on f=off U=brighter D=dimmer M=mid g=enroll_group O/F/T=group on/off/toggle +/-/m=group level\r\n");
  g_permitJoinEnabled = true;
  g_permitJoinDeadlineMs = millis() + kPermitJoinWindowMs;
}

void loop() {
  handleSerialCommands();
  pumpRadio();

  const uint32_t now = millis();
  if (g_permitJoinEnabled && g_permitJoinDeadlineMs != 0U &&
      static_cast<int32_t>(now - g_permitJoinDeadlineMs) >= 0) {
    g_permitJoinEnabled = false;
    g_permitJoinDeadlineMs = 0U;
    Serial.print("permit_join timeout\r\n");
  }
  if ((now - g_lastBeaconMs) >= 1500U) {
    g_lastBeaconMs = now;
    (void)sendBeacon();
  }
  for (uint8_t i = 0U; i < kMaxNodes; ++i) {
    if (!g_nodes[i].used || g_nodes[i].shortAddress == 0U) {
      continue;
    }
    maybeExpirePendingApsAck(&g_nodes[i], now);
    maybeRetryInterviewStage(&g_nodes[i], now);
  }

  if ((now - g_lastStatusMs) >= 5000U) {
    g_lastStatusMs = now;
    uint8_t joined = 0U;
    uint8_t pending = 0U;
    for (uint8_t i = 0U; i < kMaxNodes; ++i) {
      if (!g_nodes[i].used || g_nodes[i].shortAddress == 0U) {
        continue;
      }
      ++joined;
      if (g_nodes[i].pending.used || g_nodes[i].pendingAssociationResponse ||
          g_nodes[i].pendingTransportKey || g_nodes[i].pendingSecureRejoin ||
          g_nodes[i].pendingSwitchKey ||
          nodeHasPendingApsAck(g_nodes[i])) {
        ++pending;
      }
    }

    Serial.print("alive joined=");
    Serial.print(joined);
    Serial.print(" pending=");
    Serial.print(pending);
    Serial.print(" permit_join=");
    Serial.print(g_permitJoinEnabled ? "open" : "closed");
    Serial.print("\r\n");
    Gpio::toggle(kPinUserLed);
  }
}
