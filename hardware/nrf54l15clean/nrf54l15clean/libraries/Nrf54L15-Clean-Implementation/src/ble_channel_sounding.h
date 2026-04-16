#pragma once

#include <stddef.h>
#include <stdint.h>

#include "nrf54l15_hal.h"
#include "nrf54l15_vpr.h"

class Stream;

namespace xiao_nrf54l15 {

constexpr size_t kBleCsMaxSwitchPatternCount = 8U;
constexpr size_t kBleCsChannelMapBytes = 10U;
constexpr uint8_t kBleCsMainMode1 = 0x1U;
constexpr uint8_t kBleCsMainMode2 = 0x2U;
constexpr uint8_t kBleCsMainMode3 = 0x3U;
constexpr uint8_t kBleCsToneQualityHigh = 0x0U;
constexpr uint8_t kBleCsToneQualityMedium = 0x1U;
constexpr uint8_t kBleCsToneQualityLow = 0x2U;
constexpr uint8_t kBleCsToneQualityUnavailable = 0x3U;
constexpr uint8_t kBleCsToneExtensionNone = 0x0U;
constexpr uint8_t kBleCsToneExtensionUnexpected = 0x1U;
constexpr uint8_t kBleCsToneExtensionExpected = 0x2U;
constexpr uint8_t kBleCsPacketQualityAaCheckOk = 0x0U;
constexpr uint8_t kBleCsPacketQualityAaCheckBitErrors = 0x1U;
constexpr uint8_t kBleCsPacketQualityAaCheckNotFound = 0x2U;
constexpr uint8_t kBleCsPacketRssiNotAvailable = 0x7FU;
constexpr int16_t kBleCsTimeDifferenceNotAvailable =
    static_cast<int16_t>(0x8000);
constexpr uint8_t kBleCsProcedureDoneComplete = 0x0U;
constexpr uint8_t kBleCsProcedureDonePartial = 0x1U;
constexpr uint8_t kBleCsProcedureDoneAborted = 0xFU;
constexpr uint8_t kBleCsSubeventDoneComplete = 0x0U;
constexpr uint8_t kBleCsSubeventDonePartial = 0x1U;
constexpr uint8_t kBleCsSubeventDoneAborted = 0xFU;
constexpr uint16_t kBleCsHciOpReadRemoteSupportedCapabilities = 0x208AU;
constexpr uint16_t kBleCsHciOpSecurityEnable = 0x208CU;
constexpr uint16_t kBleCsHciOpSetDefaultSettings = 0x208DU;
constexpr uint16_t kBleCsHciOpCreateConfig = 0x2090U;
constexpr uint16_t kBleCsHciOpRemoveConfig = 0x2091U;
constexpr uint16_t kBleCsHciOpSetProcedureParameters = 0x2093U;
constexpr uint16_t kBleCsHciOpProcedureEnable = 0x2094U;
constexpr uint8_t kBleCsHciEvtReadRemoteSupportedCapabilitiesComplete = 0x2CU;
constexpr uint8_t kBleCsHciEvtReadRemoteSupportedCapabilitiesCompleteV2 = 0x38U;
constexpr uint8_t kBleCsHciEvtSecurityEnableComplete = 0x2EU;
constexpr uint8_t kBleCsHciEvtConfigComplete = 0x2FU;
constexpr uint8_t kBleCsHciEvtProcedureEnableComplete = 0x30U;
constexpr uint8_t kBleCsHciEvtSubeventResult = 0x31U;
constexpr uint8_t kBleCsHciEvtSubeventResultContinue = 0x32U;
constexpr uint8_t kBleHciPacketTypeCommand = 0x01U;
constexpr uint8_t kBleHciPacketTypeAcl = 0x02U;
constexpr uint8_t kBleHciPacketTypeSco = 0x03U;
constexpr uint8_t kBleHciPacketTypeEvent = 0x04U;
constexpr uint8_t kBleHciPacketTypeIso = 0x05U;
constexpr uint8_t kBleHciEvtCommandComplete = 0x0EU;
constexpr uint8_t kBleHciEvtCommandStatus = 0x0FU;
constexpr uint8_t kBleHciEvtLeMeta = 0x3EU;
constexpr uint8_t kBleHciEvtVendor = 0xFFU;
constexpr size_t kBleCsMaxControllerStepDataBytes = 1024U;

struct BleCsToneSample {
  bool valid = false;
  int8_t rssiDbm = 0;
  uint8_t cteTimeUnits = 0;
  uint8_t cteType = 0;
  int16_t i = 0;
  int16_t q = 0;
  uint16_t magnitude = 0;
  uint16_t magnitudeStd = 0;
  uint16_t phase = 0;
};

struct BleCsDfeCaptureInfo {
  bool present = false;
  bool allZero = true;
  uint16_t amountBytes = 0U;
  uint16_t currentAmountBytes = 0U;
};

struct BleCsIqSample {
  int16_t i = 0;
  int16_t q = 0;
};

struct BleCsSubeventStep {
  uint8_t mode = 0U;
  uint8_t channel = 0U;
  uint8_t dataLen = 0U;
  const uint8_t* data = nullptr;
};

struct BleCsStepMode1Data {
  uint8_t aaCheckQuality = kBleCsPacketQualityAaCheckNotFound;
  uint8_t bitErrors = 0x0FU;
  uint8_t nadm = 0xFFU;
  int8_t packetRssiDbm = 0;
  int16_t timeDifferenceHalfNs = kBleCsTimeDifferenceNotAvailable;
  uint8_t packetAntenna = 0U;
  bool hasRttSoundingSequence = false;
  BleCsIqSample soundingPct1{};
  BleCsIqSample soundingPct2{};
};

struct BleCsStepToneInfo {
  BleCsIqSample pct{};
  uint8_t qualityIndicator = kBleCsToneQualityUnavailable;
  uint8_t extensionIndicator = kBleCsToneExtensionExpected;
};

struct BleCsStepMode2Data {
  uint8_t antennaPermutationIndex = 0U;
  uint8_t toneCount = 0U;
};

struct BleCsStepMode3Data {
  BleCsStepMode1Data timing{};
  uint8_t antennaPermutationIndex = 0U;
  uint8_t toneCount = 0U;
  uint8_t toneDataOffset = 0U;
};

struct BleCsRttSample {
  bool present = false;
  bool valid = false;
  uint8_t aaCheckQuality = 0xFFU;
  uint8_t bitErrors = 0xFFU;
  uint8_t nadm = 0xFFU;
  int8_t packetRssiDbm = 0;
  int16_t timeDifferenceHalfNs = 0;
  uint8_t packetAntenna = 0U;
  uint8_t rawLen = 0U;
  uint8_t rawBytes[8] = {0};
};

struct BleCsChannelMeasurement {
  bool valid = false;
  uint8_t channelIndex = 0;
  uint8_t sequence = 0;
  BleCsToneSample localTone;
  BleCsToneSample peerTone;
  BleCsRttSample localRtt;
  BleCsRttSample peerRtt;
  float combinedPhaseRad = 0.0f;
  float rttDistanceMeters = 0.0f;
};

struct BleCsEstimate {
  bool valid = false;
  uint8_t totalToneChannels = 0;
  uint8_t usedChannels = 0;
  uint8_t rttChannels = 0;
  uint8_t rejectedLowQualityChannels = 0;
  uint8_t rejectedResidualChannels = 0;
  float distanceMeters = 0.0f;
  float phaseSlopeDistanceMeters = 0.0f;
  float rttDistanceMeters = 0.0f;
  float slopeRadPerHz = 0.0f;
  float residualVariance = 0.0f;
  float rttVariance = 0.0f;
  float medianToneQuality = 0.0f;
  float fitDeltaMeters = 0.0f;
};

struct BleCsCalibrationProfile {
  float scale = 1.0f;
  float offsetMeters = 0.0f;
  float referenceDistanceMeters = 0.0f;
  float measuredMedianMeters = 0.0f;
  float measuredMadMeters = 0.0f;
  float boardPairBiasMeters = 0.0f;
  float boardPairEquivalentDelayNs = 0.0f;
  float symmetricPerBoardEquivalentDelayNs = 0.0f;
  float validatedMedianMeters = 0.0f;
  float validatedMadMeters = 0.0f;
  float validatedP90AbsErrorMeters = 0.0f;
  uint16_t sampleCount = 0U;
  uint16_t validatedSampleCount = 0U;
};

struct BleCsPhysicalDistanceEstimate {
  bool valid = false;
  float distanceMeters = 0.0f;
  float typicalErrorMeters = 0.0f;
  float conservativeErrorMeters = 0.0f;
  float lowerBoundMeters = 0.0f;
  float upperBoundMeters = 0.0f;
  uint16_t sampleCount = 0U;
};

struct BleCsSubeventResultHeader {
  uint16_t connHandle = 0U;
  uint8_t configId = 0U;
  uint16_t startAclConnEventCounter = 0U;
  uint16_t procedureCounter = 0U;
  uint16_t frequencyCompensation = 0U;
  int8_t referencePowerLevelDbm = 0;
  uint8_t procedureDoneStatus = kBleCsProcedureDoneComplete;
  uint8_t subeventDoneStatus = kBleCsSubeventDoneComplete;
  uint8_t procedureAbortReason = 0U;
  uint8_t subeventAbortReason = 0U;
  uint8_t numAntennaPaths = 0U;
  uint16_t numStepsReported = 0U;
};

struct BleCsSubeventResult {
  BleCsSubeventResultHeader header{};
  const uint8_t* stepData = nullptr;
  uint16_t stepDataLen = 0U;
  bool isPartial = false;
  bool isComplete = false;
  bool isContinuation = false;
};

struct BleCsHciCommand {
  uint16_t opcode = 0U;
  uint8_t payload[64] = {0};
  uint8_t payloadLen = 0U;
};

struct BleCsHciCommandStatusEvent {
  uint8_t status = 0U;
  uint8_t numCommandPackets = 0U;
  uint16_t opcode = 0U;
};

struct BleCsHciCommandCompleteEvent {
  uint8_t status = 0U;
  uint8_t numCommandPackets = 0U;
  uint16_t opcode = 0U;
  const uint8_t* returnParams = nullptr;
  uint8_t returnParamsLen = 0U;
};

struct BleCsHciLeMetaEvent {
  uint8_t subeventCode = 0U;
  const uint8_t* payload = nullptr;
  uint8_t payloadLen = 0U;
};

enum class BleCsControllerResultSource : uint8_t {
  kLocal = 0U,
  kPeer,
};

enum class BleCsControllerIngressSource : uint8_t {
  kController = 0U,
  kLocalResult,
  kPeerResult,
};

struct BleCsControllerCapabilities {
  bool valid = false;
  bool isV2 = false;
  uint8_t status = 0U;
  uint16_t connHandle = 0U;
  uint8_t numConfigSupported = 0U;
  uint16_t maxConsecutiveProceduresSupported = 0U;
  uint8_t numAntennasSupported = 0U;
  uint8_t maxAntennaPathsSupported = 0U;
  bool initiatorSupported = false;
  bool reflectorSupported = false;
  bool mode3Supported = false;
  uint8_t rttCapability = 0U;
  uint8_t rttAaOnlyN = 0U;
  uint8_t rttSoundingN = 0U;
  uint8_t rttRandomPayloadN = 0U;
  uint16_t nadmSoundingCapability = 0U;
  uint16_t nadmRandomCapability = 0U;
  bool csSync2mPhySupported = false;
  bool csSync2m2btPhySupported = false;
  bool csWithoutFaeSupported = false;
  bool chselAlg3cSupported = false;
  bool pbrFromRttSoundingSeqSupported = false;
  bool csIptReflectorSupported = false;
  uint16_t tIp1TimesSupported = 0U;
  uint16_t tIp2TimesSupported = 0U;
  uint16_t tFcsTimesSupported = 0U;
  uint16_t tPmTimesSupported = 0U;
  uint8_t tSwTimeSupported = 0U;
  uint8_t txSnrCapability = 0U;
  uint16_t tIp2IptTimesSupported = 0U;
  uint8_t tSwIptTimeSupported = 0U;
};

struct BleCsDefaultSettings {
  bool enableInitiatorRole = false;
  bool enableReflectorRole = false;
  uint8_t csSyncAntennaSelection = 0xFFU;
  int8_t maxTxPowerDbm = 0;
};

struct BleCsControllerCreateConfig {
  uint8_t configId = 0U;
  uint8_t createContext = 0U;
  uint8_t mainModeType = 0U;
  uint8_t subModeType = 0U;
  uint8_t minMainModeSteps = 0U;
  uint8_t maxMainModeSteps = 0U;
  uint8_t mainModeRepetition = 0U;
  uint8_t mode0Steps = 0U;
  uint8_t role = 0U;
  uint8_t rttType = 0U;
  uint8_t csSyncPhy = 0U;
  uint8_t channelMap[kBleCsChannelMapBytes] = {0};
  uint8_t channelMapRepetition = 0U;
  uint8_t channelSelectionType = 0U;
  uint8_t ch3cShape = 0U;
  uint8_t ch3cJump = 0U;
  uint8_t csEnhancements1 = 0U;
};

struct BleCsProcedureParameters {
  uint8_t configId = 0U;
  uint16_t maxProcedureLen = 0U;
  uint16_t minProcedureInterval = 0U;
  uint16_t maxProcedureInterval = 0U;
  uint16_t maxProcedureCount = 0U;
  uint32_t minSubeventLen = 0U;
  uint32_t maxSubeventLen = 0U;
  uint8_t toneAntennaConfigSelection = 0U;
  uint8_t phy = 0U;
  int8_t txPowerDelta = 0;
  uint8_t preferredPeerAntenna = 0U;
  uint8_t snrControlInitiator = 0U;
  uint8_t snrControlReflector = 0U;
};

struct BleCsProcedureEnable {
  uint8_t configId = 0U;
  uint8_t enable = 0U;
};

struct BleCsSecurityEnableComplete {
  uint8_t status = 0U;
  uint16_t connHandle = 0U;
};

struct BleCsConfigComplete {
  uint8_t status = 0U;
  uint16_t connHandle = 0U;
  uint8_t configId = 0U;
  uint8_t action = 0U;
  uint8_t mainModeType = 0U;
  uint8_t subModeType = 0U;
  uint8_t minMainModeSteps = 0U;
  uint8_t maxMainModeSteps = 0U;
  uint8_t mainModeRepetition = 0U;
  uint8_t mode0Steps = 0U;
  uint8_t role = 0U;
  uint8_t rttType = 0U;
  uint8_t csSyncPhy = 0U;
  uint8_t channelMap[kBleCsChannelMapBytes] = {0};
  uint8_t channelMapRepetition = 0U;
  uint8_t channelSelectionType = 0U;
  uint8_t ch3cShape = 0U;
  uint8_t ch3cJump = 0U;
  uint8_t csEnhancements1 = 0U;
  uint8_t tIp1TimeUs = 0U;
  uint8_t tIp2TimeUs = 0U;
  uint8_t tFcsTimeUs = 0U;
  uint8_t tPmTimeUs = 0U;
};

struct BleCsProcedureEnableComplete {
  uint8_t status = 0U;
  uint16_t connHandle = 0U;
  uint8_t configId = 0U;
  uint8_t state = 0U;
  uint8_t toneAntennaConfigSelection = 0U;
  int8_t selectedTxPower = 0;
  uint32_t subeventLen = 0U;
  uint8_t subeventsPerEvent = 0U;
  uint16_t subeventInterval = 0U;
  uint16_t eventInterval = 0U;
  uint16_t procedureInterval = 0U;
  uint16_t procedureCount = 0U;
  uint16_t maxProcedureLen = 0U;
};

enum class BleCsControllerWorkflowPhase : uint8_t {
  kIdle = 0U,
  kNeedReadRemoteCapabilities,
  kWaitingRemoteCapabilities,
  kNeedSetDefaultSettings,
  kWaitingSetDefaultSettings,
  kNeedCreateConfig,
  kWaitingConfigComplete,
  kNeedSecurityEnable,
  kWaitingSecurityEnableComplete,
  kNeedSetProcedureParameters,
  kWaitingSetProcedureParameters,
  kNeedProcedureEnable,
  kWaitingProcedureEnableComplete,
  kReady,
  kFailed,
};

struct BleCsControllerWorkflowConfig {
  BleCsDefaultSettings defaultSettings{};
  BleCsControllerCreateConfig createConfig{};
  BleCsProcedureParameters procedureParameters{};
  BleCsProcedureEnable procedureEnable{};
  bool applyDefaultSettings = true;
  bool requireSecurityEnable = true;
};

struct BleCsControllerWorkflowState {
  BleCsControllerWorkflowPhase phase = BleCsControllerWorkflowPhase::kIdle;
  uint16_t connHandle = 0U;
  uint16_t lastOpcode = 0U;
  uint8_t lastStatus = 0U;
  bool remoteCapabilitiesValid = false;
  bool defaultSettingsApplied = false;
  bool configCreated = false;
  bool securityEnabled = false;
  bool procedureParametersApplied = false;
  bool procedureEnabled = false;
  BleCsControllerCapabilities remoteCapabilities{};
  BleCsConfigComplete configComplete{};
  BleCsProcedureEnableComplete procedureEnableComplete{};
};

struct BleCsConfig {
  uint32_t accessAddress = 0xA7DCE5B3UL;
  uint32_t crcInit = 0x445566UL;
  int8_t txPowerDbm = NRF54L15_CLEAN_BLE_DEFAULT_TX_DBM;
  uint8_t cteTimeUnits = 10U;
  uint8_t maxPayloadLength = 32U;
  uint8_t s0Pattern = 0xA5U;
  uint8_t controlChannel = 37U;
  uint16_t controlToProbeDelayUs = 2400U;
  uint16_t probeToReportDelayUs = 1200U;
  uint16_t controlListenWindowUs = 20000U;
  uint16_t probeListenWindowUs = 8000U;
  uint16_t responseListenWindowUs = 12000U;
  uint8_t probeRetries = 4U;
  uint16_t minToneMagnitude = 16U;
  // Bare RADIO RTT AUXDATA layout/channel semantics are not publicly documented
  // well enough to decode reliable ranging results in this clean-core path.
  bool enableRtt = false;
  bool rttFullAccessAddress = true;
  uint8_t rttNumSegments = 0U;
  uint16_t rttEfsDelay = 64U;
  float rttDistanceOffsetMeters = 0.0f;
  bool enableRawDfeCapture = false;
  uint8_t dfeSwitchPatternCount = 0U;
  uint8_t dfeSwitchPattern[kBleCsMaxSwitchPatternCount] = {0};
  uint8_t dfeRepeatPattern = 0U;
  int16_t dfeSwitchOffset16M = 0;
  int16_t dfeSampleOffset16M = 0;
};

class BleChannelSoundingRadio {
 public:
  explicit BleChannelSoundingRadio(uint32_t radioBase = nrf54l15::RADIO_BASE);

  bool begin(const BleCsConfig& config = BleCsConfig());
  void end();

  bool initialized() const;
  const BleCsConfig& config() const;

  bool measureChannel(uint8_t channelIndex, uint8_t sequence,
                      BleCsChannelMeasurement* outMeasurement);
  bool listenAndReflectOnce(uint32_t controlListenWindowUs = 0U);

  static float combinedPhaseRad(const BleCsChannelMeasurement& measurement);
  static bool rttDistanceMeters(const BleCsChannelMeasurement& measurement,
                                float* outDistanceMeters);
  static bool estimateDistancePhaseSlope(const BleCsChannelMeasurement* measurements,
                                         size_t count,
                                         BleCsEstimate* outEstimate);
  static float applyCalibrationProfile(float meters,
                                       const BleCsCalibrationProfile& profile);
  static bool estimatePhysicalDistance(float meters,
                                       const BleCsCalibrationProfile& profile,
                                       BleCsPhysicalDistanceEstimate* outEstimate);
  static float distanceMetersToEquivalentDelayNs(float meters);
  static float equivalentDelayNsToDistanceMeters(float delayNs);
  static BleCsIqSample parsePctSample(const uint8_t pct[3]);
  static void fillValidChannelMap(uint8_t channelMap[kBleCsChannelMapBytes]);
  static bool getAntennaPathPermutation(uint8_t antennaPathCount,
                                        uint8_t permutationIndex,
                                        uint8_t toneIndex,
                                        uint8_t* outAntennaId);
  static bool parseMode1StepData(const BleCsSubeventStep* step,
                                 BleCsStepMode1Data* outData);
  static bool parseMode2StepData(const BleCsSubeventStep* step,
                                 BleCsStepMode2Data* outData);
  static bool parseMode3StepData(const BleCsSubeventStep* step,
                                 BleCsStepMode3Data* outData);
  static bool parseToneInfo(const uint8_t* toneData,
                            size_t toneDataLen,
                            BleCsStepToneInfo* outInfo);
  static bool parseMode2ToneInfo(const BleCsSubeventStep* step,
                                 uint8_t toneIndex,
                                 BleCsStepToneInfo* outInfo);
  static bool parseMode3ToneInfo(const BleCsSubeventStep* step,
                                 uint8_t toneIndex,
                                 BleCsStepToneInfo* outInfo);
  static void parseSubeventStepData(const uint8_t* stepData,
                                    size_t stepDataLen,
                                    bool (*callback)(const BleCsSubeventStep* step,
                                                     void* userData),
                                    void* userData);
  static bool estimateDistanceFromStepBuffers(const uint8_t* localStepData,
                                              size_t localStepDataLen,
                                              const uint8_t* peerStepData,
                                              size_t peerStepDataLen,
                                              bool localRoleIsInitiator,
                                              BleCsEstimate* outEstimate);
  static bool parseHciSubeventResultEvent(const uint8_t* eventData,
                                          size_t eventLen,
                                          BleCsSubeventResult* outResult);
  static bool parseHciSubeventResultContinueEvent(const uint8_t* eventData,
                                                  size_t eventLen,
                                                  BleCsSubeventResult* outResult);
  static bool estimateDistanceFromSubeventResults(const BleCsSubeventResult& localResult,
                                                  const BleCsSubeventResult& peerResult,
                                                  bool localRoleIsInitiator,
                                                  BleCsEstimate* outEstimate);
  static bool buildHciReadRemoteSupportedCapabilitiesCommand(
      uint16_t connHandle, BleCsHciCommand* outCommand);
  static bool buildHciSetDefaultSettingsCommand(
      uint16_t connHandle, const BleCsDefaultSettings& settings, BleCsHciCommand* outCommand);
  static bool buildHciCreateConfigCommand(uint16_t connHandle,
                                          const BleCsControllerCreateConfig& config,
                                          BleCsHciCommand* outCommand);
  static bool buildHciRemoveConfigCommand(uint16_t connHandle,
                                          uint8_t configId,
                                          BleCsHciCommand* outCommand);
  static bool buildHciSecurityEnableCommand(uint16_t connHandle,
                                            BleCsHciCommand* outCommand);
  static bool buildHciSetProcedureParametersCommand(
      uint16_t connHandle,
      const BleCsProcedureParameters& params,
      BleCsHciCommand* outCommand);
  static bool buildHciProcedureEnableCommand(uint16_t connHandle,
                                             const BleCsProcedureEnable& params,
                                             BleCsHciCommand* outCommand);
  static bool encodeHciCommandPacket(const BleCsHciCommand& command,
                                     uint8_t* outPacket,
                                     size_t maxLen,
                                     size_t* outLen);
  static bool parseHciCommandStatusEvent(const uint8_t* packet,
                                         size_t packetLen,
                                         BleCsHciCommandStatusEvent* outEvent);
  static bool parseHciCommandCompleteEvent(const uint8_t* packet,
                                           size_t packetLen,
                                           BleCsHciCommandCompleteEvent* outEvent);
  static bool parseHciLeMetaEvent(const uint8_t* packet,
                                  size_t packetLen,
                                  BleCsHciLeMetaEvent* outEvent);
  static bool parseHciRemoteSupportedCapabilitiesCompleteEvent(
      const uint8_t* eventData,
      size_t eventLen,
      BleCsControllerCapabilities* outCapabilities);
  static bool parseHciRemoteSupportedCapabilitiesCompleteV2Event(
      const uint8_t* eventData,
      size_t eventLen,
      BleCsControllerCapabilities* outCapabilities);
  static bool parseHciSecurityEnableCompleteEvent(
      const uint8_t* eventData,
      size_t eventLen,
      BleCsSecurityEnableComplete* outEvent);
  static bool parseHciConfigCompleteEvent(const uint8_t* eventData,
                                          size_t eventLen,
                                          BleCsConfigComplete* outEvent);
  static bool parseHciProcedureEnableCompleteEvent(
      const uint8_t* eventData,
      size_t eventLen,
      BleCsProcedureEnableComplete* outEvent);
  BleCsDfeCaptureInfo lastDfeCaptureInfo() const;
  bool copyLastDfePacket(uint8_t* outPacket,
                         size_t maxLen,
                         size_t* outLen) const;

 private:
  enum class PacketType : uint8_t {
    kControl = 0x43U,
    kProbe = 0x50U,
    kReport = 0x52U,
  };

  struct RxFrame {
    bool valid = false;
    PacketType type = PacketType::kControl;
    uint8_t sequence = 0;
    uint8_t channelIndex = 0;
    uint8_t flags = 0;
    int8_t rssiDbm = 0;
    uint8_t extra[24] = {0};
    uint8_t extraLen = 0;
  };

  bool configureBle2MCommon();
  bool setLogicalChannel(uint8_t channelIndex);
  void configureRtt(bool enabled, bool reflectorRole);
  void prepareAuxDataCapture();
  void configureTxToneExtension();
  void configureRxToneCapture();
  void clearEvents();
  bool sendFrame(uint8_t logicalChannel,
                 PacketType type,
                 uint8_t sequence,
                 uint8_t channelIndex,
                 uint8_t flags,
                 const uint8_t* extra,
                 uint8_t extraLen,
                 bool enableRtt,
                 bool rttReflectorRole);
  bool receiveFrame(uint8_t logicalChannel,
                    uint32_t listenWindowUs,
                    bool captureTone,
                    bool captureRtt,
                    bool rttReflectorRole,
                    RxFrame* outFrame,
                    BleCsToneSample* outTone,
                    BleCsRttSample* outRtt);
  bool decodeFrame(const uint8_t* packet,
                   size_t packetLen,
                   int8_t rssiDbm,
                   RxFrame* outFrame) const;
  void encodeReportExtra(const BleCsToneSample& tone, uint8_t* outExtra) const;
  void decodeReportExtra(const uint8_t* extra,
                         uint8_t extraLen,
                         BleCsToneSample* outTone) const;
  void encodeRttExtra(const BleCsRttSample& rtt, uint8_t* outExtra) const;
  void decodeRttExtra(const uint8_t* extra,
                      uint8_t extraLen,
                      BleCsRttSample* outRtt) const;
  void captureAuxDataRtt(BleCsRttSample* outRtt);
  void resetDfeCaptureState();
  void updateDfeCaptureState();
  void parseRttRaw(BleCsRttSample* outRtt) const;
  uint8_t makeCteInfo() const;

  NRF_RADIO_Type* radio_;
  PowerManager power_;
  BleCsConfig config_;
  bool initialized_;
  alignas(4) uint8_t txPacket_[3U + 48U];
  alignas(4) uint8_t rxPacket_[3U + 48U];
  alignas(4) uint8_t dfePacket_[512U];
  alignas(4) uint32_t auxDataWords_[4];
  uint16_t lastDfePacketAmountBytes_;
  uint16_t lastDfePacketCurrentAmountBytes_;
  bool lastDfePacketAllZero_;
};

class BleCsSubeventResultReassembler {
 public:
  BleCsSubeventResultReassembler();

  void reset();
  bool active() const;

  bool consumeInitialEvent(const uint8_t* eventData,
                           size_t eventLen,
                           BleCsSubeventResult* outResult);
  bool consumeContinuationEvent(const uint8_t* eventData,
                                size_t eventLen,
                                BleCsSubeventResult* outResult);

 private:
  static constexpr size_t kMaxStepDataBytes = kBleCsMaxControllerStepDataBytes;

  bool appendStepData(const uint8_t* data, size_t len);
  void fillOutput(bool complete,
                  bool continuation,
                  BleCsSubeventResult* outResult) const;

  BleCsSubeventResultHeader header_;
  uint8_t stepData_[kMaxStepDataBytes];
  uint16_t stepDataLen_;
  bool active_;
};

class BleCsControllerWorkflow {
 public:
  BleCsControllerWorkflow();

  void reset();
  bool begin(uint16_t connHandle, const BleCsControllerWorkflowConfig& config);

  bool active() const;
  bool ready() const;
  bool failed() const;

  BleCsControllerWorkflowPhase phase() const;
  const BleCsControllerWorkflowState& state() const;
  const BleCsControllerWorkflowConfig& config() const;

  bool buildNextCommand(BleCsHciCommand* outCommand);
  bool acknowledgeCommandStatus(uint16_t opcode, uint8_t status);
  bool consumeEvent(uint8_t subeventCode, const uint8_t* eventData, size_t eventLen);
  bool consumeHciEventPacket(const uint8_t* packet, size_t packetLen);
  void reconcileReadyShadowState(uint8_t selectedConfigId,
                                 bool sessionOpen,
                                 bool configCreated,
                                 bool securityEnabled,
                                 bool procedureParametersApplied,
                                 bool procedureEnabled);

  static const char* phaseName(BleCsControllerWorkflowPhase phase);

 private:
  static bool validateConfigAgainstCapabilities(
      const BleCsControllerWorkflowConfig& config,
      const BleCsControllerCapabilities& capabilities);
  bool acknowledgeReadyCommandStatus(uint16_t opcode, uint8_t status);
  bool consumeReadyEvent(uint8_t subeventCode, const uint8_t* eventData, size_t eventLen);
  void fail(uint8_t status);

  BleCsControllerWorkflowConfig config_;
  BleCsControllerWorkflowState state_;
};

class BleHciPacketStreamDecoder {
 public:
  BleHciPacketStreamDecoder();

  void reset();
  void setAcceptedPacketTypes(uint32_t acceptedTypes);
  uint32_t acceptedPacketTypes() const;
  uint32_t deliveredPacketCount() const;
  uint32_t ignoredPacketCount() const;
  uint32_t ignoredByteCount() const;
  bool pushBytes(const uint8_t* data,
                 size_t len,
                 bool (*onPacket)(const uint8_t* packet, size_t packetLen, void* userData),
                 void* userData);

 private:
  static constexpr size_t kMaxPacketBytes = 1100U;

  static uint32_t packetTypeMask(uint8_t packetType);
  bool expectedLengthKnown() const;
  bool determineExpectedLength();
  bool packetTypeAccepted() const;
  void resetBuffer();

  uint8_t buffer_[kMaxPacketBytes];
  size_t used_;
  size_t expected_;
  uint32_t acceptedTypes_;
  uint32_t deliveredPackets_;
  uint32_t ignoredPackets_;
  uint32_t ignoredBytes_;
};

struct BleCsControllerSessionConfig {
  BleCsControllerWorkflowConfig workflow{};
  bool localRoleIsInitiator = true;
};

struct BleCsControllerSessionState {
  bool workflowReady = false;
  bool localResultComplete = false;
  bool peerResultComplete = false;
  bool estimateValid = false;
  uint16_t completedProcedureCounter = 0U;
  uint16_t completedConfigId = 0U;
  uint32_t workflowIgnoredPackets = 0U;
  uint32_t localIgnoredPackets = 0U;
  uint32_t peerIgnoredPackets = 0U;
  uint32_t workflowIgnoredBytes = 0U;
  uint32_t localIgnoredBytes = 0U;
  uint32_t peerIgnoredBytes = 0U;
  BleCsEstimate estimate{};
};

class BleCsControllerSession {
 public:
  BleCsControllerSession();

  void reset();
  bool begin(uint16_t connHandle, const BleCsControllerSessionConfig& config);

  bool buildNextCommandPacket(uint8_t* outPacket, size_t maxLen, size_t* outLen);
  bool consumeWorkflowEventPacket(const uint8_t* packet, size_t packetLen);
  bool consumeWorkflowStreamBytes(const uint8_t* data, size_t len);
  bool consumeResultEventPacket(BleCsControllerResultSource source,
                                const uint8_t* packet,
                                size_t packetLen);
  bool consumeResultStreamBytes(BleCsControllerResultSource source,
                                const uint8_t* data,
                                size_t len);

  bool ready() const;
  bool failed() const;
  bool estimateValid() const;
  const BleCsControllerSessionState& state() const;
  const BleCsControllerWorkflowState& workflowState() const;
  const BleCsSubeventResult& localResult() const;
  const BleCsSubeventResult& peerResult() const;
  const BleCsSubeventResult& completedLocalResult() const;
  const BleCsSubeventResult& completedPeerResult() const;
  void resetProcedureRunState();
  void reconcileReadyWorkflowShadow(uint8_t selectedConfigId,
                                    bool sessionOpen,
                                    bool configCreated,
                                    bool securityEnabled,
                                    bool procedureParametersApplied,
                                    bool procedureEnabled);

 private:
  static bool onWorkflowPacket(const uint8_t* packet, size_t packetLen, void* userData);
  static bool onLocalResultPacket(const uint8_t* packet, size_t packetLen, void* userData);
  static bool onPeerResultPacket(const uint8_t* packet, size_t packetLen, void* userData);
  bool consumeResultPacket(BleCsControllerResultSource source,
                           const uint8_t* packet,
                           size_t packetLen);
  void resetAccumulatedProcedureResults();
  void resetAccumulatedProcedureResult(BleCsControllerResultSource source);
  bool accumulateProcedureResult(BleCsControllerResultSource source,
                                 const BleCsSubeventResult& result);
  bool snapshotCompletedResultPair(const BleCsSubeventResult& localResult,
                                   const BleCsSubeventResult& peerResult);
  void updateEstimateIfComplete();

  BleCsControllerSessionConfig config_;
  BleCsControllerSessionState state_;
  BleCsControllerWorkflow workflow_;
  BleHciPacketStreamDecoder workflowDecoder_;
  BleHciPacketStreamDecoder localDecoder_;
  BleHciPacketStreamDecoder peerDecoder_;
  BleCsSubeventResultReassembler localReassembler_;
  BleCsSubeventResultReassembler peerReassembler_;
  BleCsSubeventResult localResult_;
  BleCsSubeventResult peerResult_;
  BleCsSubeventResult accumulatedLocalResult_;
  BleCsSubeventResult accumulatedPeerResult_;
  BleCsSubeventResult completedLocalResult_;
  BleCsSubeventResult completedPeerResult_;
  uint8_t accumulatedLocalStepData_[kBleCsMaxControllerStepDataBytes];
  uint8_t accumulatedPeerStepData_[kBleCsMaxControllerStepDataBytes];
  uint8_t completedLocalStepData_[kBleCsMaxControllerStepDataBytes];
  uint8_t completedPeerStepData_[kBleCsMaxControllerStepDataBytes];
};

using BleCsControllerSendPacketCallback =
    bool (*)(const uint8_t* packet, size_t packetLen, void* userData);

struct BleCsControllerHostConfig {
  BleCsControllerSessionConfig session{};
  BleCsControllerSendPacketCallback sendPacket = nullptr;
  void* userData = nullptr;
};

struct BleCsControllerHostState {
  bool began = false;
  uint32_t sentCommandPackets = 0U;
  uint32_t sentCommandBytes = 0U;
  uint16_t lastCommandOpcode = 0U;
  uint32_t controllerEventPackets = 0U;
  uint32_t localResultPackets = 0U;
  uint32_t peerResultPackets = 0U;
  uint32_t localSubeventResults = 0U;
  uint32_t peerSubeventResults = 0U;
  uint32_t vendorPeerResultTriggers = 0U;
  uint32_t controllerPeerResultMarkers = 0U;
  uint8_t vendorPeerResultConfigId = 0U;
  uint16_t vendorPeerResultProcedureCounter = 0U;
  uint32_t controllerIgnoredPackets = 0U;
  uint32_t controllerIgnoredBytes = 0U;
};

class BleCsControllerHost {
 public:
  BleCsControllerHost();

  void reset();
  bool begin(uint16_t connHandle, const BleCsControllerHostConfig& config);

  bool pumpCommands(uint8_t maxCommands = 1U);
  bool consumeIngressPacket(BleCsControllerIngressSource source,
                            const uint8_t* packet,
                            size_t packetLen);
  bool consumeIngressBytes(BleCsControllerIngressSource source,
                           const uint8_t* data,
                           size_t len);

  bool ready() const;
  bool failed() const;
  bool estimateValid() const;
  const BleCsControllerHostState& state() const;
  const BleCsControllerSessionState& sessionState() const;
  const BleCsControllerWorkflowState& workflowState() const;
  const BleCsSubeventResult& localResult() const;
  const BleCsSubeventResult& peerResult() const;
  const BleCsSubeventResult& completedLocalResult() const;
  const BleCsSubeventResult& completedPeerResult() const;
  void resetProcedureRunState();
  void reconcileReadyWorkflowShadow(uint8_t selectedConfigId,
                                    bool sessionOpen,
                                    bool configCreated,
                                    bool securityEnabled,
                                    bool procedureParametersApplied,
                                    bool procedureEnabled);

 private:
  static bool onControllerPacket(const uint8_t* packet, size_t packetLen, void* userData);
  static bool onLocalPacket(const uint8_t* packet, size_t packetLen, void* userData);
  static bool onPeerPacket(const uint8_t* packet, size_t packetLen, void* userData);

  BleCsControllerHostConfig config_;
  BleCsControllerHostState state_;
  BleCsControllerSession session_;
  BleHciPacketStreamDecoder controllerDecoder_;
  BleHciPacketStreamDecoder localDecoder_;
  BleHciPacketStreamDecoder peerDecoder_;
  bool controllerPeerResultsExpected_ = false;
};

struct BleCsControllerStreamHostConfig {
  BleCsControllerSessionConfig session{};
  ::Stream* controllerStream = nullptr;
  ::Stream* peerResultStream = nullptr;
  uint8_t maxCommandsPerPump = 1U;
  size_t maxControllerBytesPerPoll = 128U;
  size_t maxPeerBytesPerPoll = 128U;
};

struct BleCsControllerStreamHostState {
  uint32_t controllerBytesRead = 0U;
  uint32_t peerBytesRead = 0U;
  uint32_t controllerPacketsWritten = 0U;
  uint32_t controllerBytesWritten = 0U;
  uint8_t lastWriteError = 0U;
};

class BleCsControllerStreamHost {
 public:
  BleCsControllerStreamHost();

  void reset();
  bool begin(uint16_t connHandle, const BleCsControllerStreamHostConfig& config);
  bool pumpCommands();
  bool pollController();
  bool pollPeerResults();
  bool consumeControllerPacket(const uint8_t* packet, size_t packetLen);
  bool consumePeerPacket(const uint8_t* packet, size_t packetLen);
  bool poll();
  bool loopOnce();

  bool ready() const;
  bool failed() const;
  bool estimateValid() const;
  const BleCsControllerStreamHostState& state() const;
  const BleCsControllerHostState& hostState() const;
  const BleCsControllerSessionState& sessionState() const;
  const BleCsControllerWorkflowState& workflowState() const;
  const BleCsSubeventResult& localResult() const;
  const BleCsSubeventResult& peerResult() const;
  const BleCsSubeventResult& completedLocalResult() const;
  const BleCsSubeventResult& completedPeerResult() const;
  void resetProcedureRunState();
  void reconcileReadyWorkflowShadow(uint8_t selectedConfigId,
                                    bool sessionOpen,
                                    bool configCreated,
                                    bool securityEnabled,
                                    bool procedureParametersApplied,
                                    bool procedureEnabled);

 private:
  static bool onSendPacket(const uint8_t* packet, size_t packetLen, void* userData);
  static size_t clampPollBytes(size_t value);

  BleCsControllerStreamHostConfig config_;
  BleCsControllerStreamHostState state_;
  BleCsControllerHost host_;
};

struct BleCsControllerVprBuiltInPeerDemoConfig {
  bool enabled = false;
  float distanceMeters = 0.75f;
  float amplitude = 1024.0f;
  uint8_t channels[4] = {0U, 12U, 24U, 36U};
  uint8_t channelCount = 4U;
};

struct BleCsControllerVprHostConfig {
  BleCsControllerSessionConfig session{};
  ::Stream* peerResultStream = nullptr;
  uint8_t maxCommandsPerPump = 1U;
  size_t maxControllerBytesPerPoll = 128U;
  size_t maxPeerBytesPerPoll = 128U;
  BleCsControllerVprBuiltInPeerDemoConfig builtInPeerDemo{};
};

struct BleCsControllerVprWorkflowStartStatus {
  uint8_t readRemoteSupportedCapabilities = 0xFFU;
  uint8_t setDefaultSettings = 0xFFU;
  uint8_t createConfig = 0xFFU;
  uint8_t securityEnable = 0xFFU;
  uint8_t setProcedureParameters = 0xFFU;
  uint8_t procedureEnable = 0xFFU;
};

struct BleCsControllerVprSelectedStateExpectation {
  uint8_t selectedConfigId = 0U;
  uint8_t storedConfigCount = 0U;
  bool selectedRunnable = false;
};

struct BleCsControllerVprRetainedSelectionExpectation {
  uint8_t activeConfigId = 0U;
  uint8_t slot0ConfigId = 0U;
  uint8_t slot1ConfigId = 0U;
  uint8_t previousConfigId = 0U;
  uint8_t storedConfigCount = 0U;
  bool selectedRunnable = false;
  bool previousRunnable = false;
  uint8_t lastEvictedConfigId = 0U;
  bool checkLastEvictedConfigId = false;
};

struct BleCsControllerVprRetainedSlotsExpectation {
  uint8_t activeConfigId = 0U;
  uint8_t slot0ConfigId = 0U;
  uint8_t slot1ConfigId = 0U;
  uint8_t previousConfigId = 0U;
  uint8_t activePrimarySlotIndex = 0xFFU;
  uint8_t freePrimarySlotCount = 0U;
  uint8_t storedConfigCount = 0U;
};

struct BleCsControllerVprRetainedRunnabilityExpectation {
  bool selectedRunnable = false;
  bool slot0Runnable = false;
  bool slot1Runnable = false;
  bool previousRunnable = false;
};

struct BleCsControllerVprRetainedReadinessExpectation {
  bool selectedSecurityEnabled = false;
  bool slot0SecurityEnabled = false;
  bool slot1SecurityEnabled = false;
  bool previousSecurityEnabled = false;
  bool selectedProcedureParamsApplied = false;
  bool slot0ProcedureParamsApplied = false;
  bool slot1ProcedureParamsApplied = false;
  bool previousProcedureParamsApplied = false;
};

struct BleCsControllerVprRetainedStateExpectation {
  BleCsControllerVprRetainedSlotsExpectation slots{};
  BleCsControllerVprRetainedRunnabilityExpectation runnability{};
  BleCsControllerVprRetainedReadinessExpectation readiness{};
  uint8_t lastEvictedConfigId = 0U;
  bool checkRunnability = false;
  bool checkReadiness = false;
  bool checkLastEvictedConfigId = false;
};

struct BleCsControllerVprHostState {
  uint32_t heartbeat = 0U;
  uint16_t lastOpcode = 0U;
  uint32_t transportStatus = 0U;
  uint32_t lastError = 0U;
  bool running = false;
  bool secureAccessEnabled = false;
  bool linkSessionOpen = false;
  uint16_t linkConnHandle = 0U;
  uint8_t linkProcedureIntervalSelector = 0U;
  uint8_t linkStoredConfigCount = 0U;
  uint8_t linkPeerGapTicks = 0U;
  uint8_t linkLastEvictedConfigId = 0U;
  uint8_t linkConfigId = 0U;
  uint8_t linkSlot0ConfigId = 0U;
  uint8_t linkSlot1ConfigId = 0U;
  uint8_t linkPreviousConfigId = 0U;
  uint8_t linkAuthority0ConfigId = 0U;
  uint8_t linkAuthority1ConfigId = 0U;
  uint8_t linkAuthority2ConfigId = 0U;
  uint8_t linkActivePrimarySlotIndex = 0xFFU;
  uint8_t linkFreePrimarySlotCount = 0U;
  uint16_t linkProcedureCounter = 0U;
  bool linkConfigCreated = false;
  bool linkSecurityEnabled = false;
  bool linkProcedureParamsApplied = false;
  bool linkProcedureEnabled = false;
  bool linkSlot0InUse = false;
  bool linkSlot1InUse = false;
  bool linkPreviousSlotInUse = false;
  bool linkActiveConfigMirroredInPrevious = false;
  bool linkSelectedConfigRunnable = false;
  bool linkSlot0Runnable = false;
  bool linkSlot1Runnable = false;
  bool linkPreviousSlotRunnable = false;
  bool linkSelectedConfigSecurityEnabled = false;
  bool linkSlot0SecurityEnabled = false;
  bool linkSlot1SecurityEnabled = false;
  bool linkPreviousSlotSecurityEnabled = false;
  bool linkSelectedConfigProcedureParamsApplied = false;
  bool linkSlot0ProcedureParamsApplied = false;
  bool linkSlot1ProcedureParamsApplied = false;
  bool linkPreviousSlotProcedureParamsApplied = false;

  bool retainedConfigMatchesSlots(uint8_t activeConfigId,
                                  uint8_t slot0ConfigId,
                                  uint8_t slot1ConfigId,
                                  uint8_t previousConfigId,
                                  uint8_t activePrimarySlotIndex,
                                  uint8_t freePrimarySlotCount,
                                  uint8_t storedConfigCount) const {
    return linkConfigId == activeConfigId &&
           linkSlot0ConfigId == slot0ConfigId &&
           linkSlot1ConfigId == slot1ConfigId &&
           linkPreviousConfigId == previousConfigId &&
           linkSlot0InUse == (slot0ConfigId != 0U) &&
           linkSlot1InUse == (slot1ConfigId != 0U) &&
           linkPreviousSlotInUse == (previousConfigId != 0U) &&
           linkActivePrimarySlotIndex == activePrimarySlotIndex &&
           linkFreePrimarySlotCount == freePrimarySlotCount &&
           linkStoredConfigCount == storedConfigCount;
  }

  bool retainedConfigMatchesRunnability(bool selectedRunnable,
                                        bool slot0Runnable,
                                        bool slot1Runnable,
                                        bool previousRunnable) const {
    return linkSelectedConfigRunnable == selectedRunnable &&
           linkSlot0Runnable == slot0Runnable &&
           linkSlot1Runnable == slot1Runnable &&
           linkPreviousSlotRunnable == previousRunnable;
  }

  bool retainedConfigMatchesSelection(uint8_t activeConfigId,
                                      uint8_t slot0ConfigId,
                                      uint8_t slot1ConfigId,
                                      uint8_t previousConfigId,
                                      uint8_t storedConfigCount,
                                      bool selectedRunnable,
                                      bool previousRunnable) const {
    return linkConfigId == activeConfigId &&
           linkSlot0ConfigId == slot0ConfigId &&
           linkSlot1ConfigId == slot1ConfigId &&
           linkPreviousConfigId == previousConfigId &&
           linkSlot0InUse == (slot0ConfigId != 0U) &&
           linkSlot1InUse == (slot1ConfigId != 0U) &&
           linkPreviousSlotInUse == (previousConfigId != 0U) &&
           linkStoredConfigCount == storedConfigCount &&
           linkSelectedConfigRunnable == selectedRunnable &&
           linkPreviousSlotRunnable == previousRunnable;
  }

  bool retainedConfigMatchesReadiness(bool selectedSecurityEnabled,
                                      bool slot0SecurityEnabled,
                                      bool slot1SecurityEnabled,
                                      bool previousSecurityEnabled,
                                      bool selectedProcedureParamsApplied,
                                      bool slot0ProcedureParamsApplied,
                                      bool slot1ProcedureParamsApplied,
                                      bool previousProcedureParamsApplied) const {
    return linkSelectedConfigSecurityEnabled == selectedSecurityEnabled &&
           linkSlot0SecurityEnabled == slot0SecurityEnabled &&
           linkSlot1SecurityEnabled == slot1SecurityEnabled &&
           linkPreviousSlotSecurityEnabled == previousSecurityEnabled &&
           linkSelectedConfigProcedureParamsApplied ==
               selectedProcedureParamsApplied &&
           linkSlot0ProcedureParamsApplied == slot0ProcedureParamsApplied &&
           linkSlot1ProcedureParamsApplied == slot1ProcedureParamsApplied &&
           linkPreviousSlotProcedureParamsApplied ==
               previousProcedureParamsApplied;
  }

  bool retainedConfigMatchesAuthority(uint8_t authority0ConfigId,
                                      uint8_t authority1ConfigId,
                                      uint8_t authority2ConfigId) const {
    return linkAuthority0ConfigId == authority0ConfigId &&
           linkAuthority1ConfigId == authority1ConfigId &&
           linkAuthority2ConfigId == authority2ConfigId;
  }

  uint32_t retainedConfigAuthorityWord() const {
    return static_cast<uint32_t>(linkAuthority0ConfigId) |
           (static_cast<uint32_t>(linkAuthority1ConfigId) << 8U) |
           (static_cast<uint32_t>(linkAuthority2ConfigId) << 16U);
  }

  bool selectedStateMatches(
      const BleCsControllerVprSelectedStateExpectation& expected) const {
    return linkSessionOpen && linkConfigCreated &&
           linkConfigId == expected.selectedConfigId &&
           linkStoredConfigCount == expected.storedConfigCount &&
           linkSelectedConfigRunnable == expected.selectedRunnable &&
           !linkProcedureEnabled;
  }

  bool retainedConfigMatches(
      const BleCsControllerVprRetainedSelectionExpectation& expected) const {
    return retainedConfigMatchesSelection(expected.activeConfigId,
                                          expected.slot0ConfigId,
                                          expected.slot1ConfigId,
                                          expected.previousConfigId,
                                          expected.storedConfigCount,
                                          expected.selectedRunnable,
                                          expected.previousRunnable) &&
           (!expected.checkLastEvictedConfigId ||
            linkLastEvictedConfigId == expected.lastEvictedConfigId);
  }

  bool retainedConfigMatches(
      const BleCsControllerVprRetainedSlotsExpectation& expected) const {
    return retainedConfigMatchesSlots(expected.activeConfigId,
                                      expected.slot0ConfigId,
                                      expected.slot1ConfigId,
                                      expected.previousConfigId,
                                      expected.activePrimarySlotIndex,
                                      expected.freePrimarySlotCount,
                                      expected.storedConfigCount);
  }

  bool retainedConfigMatches(
      const BleCsControllerVprRetainedRunnabilityExpectation& expected) const {
    return retainedConfigMatchesRunnability(expected.selectedRunnable,
                                           expected.slot0Runnable,
                                           expected.slot1Runnable,
                                           expected.previousRunnable);
  }

  bool retainedConfigMatches(
      const BleCsControllerVprRetainedReadinessExpectation& expected) const {
    return retainedConfigMatchesReadiness(expected.selectedSecurityEnabled,
                                          expected.slot0SecurityEnabled,
                                          expected.slot1SecurityEnabled,
                                          expected.previousSecurityEnabled,
                                          expected.selectedProcedureParamsApplied,
                                          expected.slot0ProcedureParamsApplied,
                                          expected.slot1ProcedureParamsApplied,
                                          expected.previousProcedureParamsApplied);
  }

  bool retainedConfigMatches(
      const BleCsControllerVprRetainedStateExpectation& expected) const {
    return retainedConfigMatches(expected.slots) &&
           (!expected.checkRunnability ||
            retainedConfigMatches(expected.runnability)) &&
           (!expected.checkReadiness ||
            retainedConfigMatches(expected.readiness)) &&
           (!expected.checkLastEvictedConfigId ||
            linkLastEvictedConfigId == expected.lastEvictedConfigId);
  }
};

class BleCsControllerVprHost {
 public:
  BleCsControllerVprHost();
  static void fillDemoConfig(BleCsControllerVprHostConfig* outConfig);

  void reset();
  bool resetTransport(bool clearScripts = true);
  bool addScriptResponse(uint16_t opcode, const uint8_t* response, size_t len);
  bool loadDefaultTransportImage();
  bool bootTransport(uint32_t readySpinLimit = 100000UL);
  bool refreshLinkSession();
  bool beginHost(uint16_t connHandle, const BleCsControllerVprHostConfig& config);
  bool beginFreshHost(uint16_t connHandle,
                      const BleCsControllerVprHostConfig& config,
                      uint8_t maxPumpCount,
                      uint8_t* outPumpCount);
  bool beginFreshHostFromBleConnection(
      VprControllerServiceHost& sourceService,
      const BleCsControllerVprHostConfig& config,
      uint8_t maxPumpCount,
      uint8_t* outPumpCount,
      VprBleConnectionSharedState* outImportedState = nullptr,
      uint32_t sourceStateTimeoutMs = 2500UL);
  bool beginFreshWorkflowFromBleConnection(
      VprControllerServiceHost& sourceService,
      const BleCsControllerVprHostConfig& config,
      bool enableProcedure,
      uint8_t maxPumpCount,
      uint8_t* outPumpCount,
      VprBleConnectionSharedState* outImportedState = nullptr,
      BleCsControllerVprWorkflowStartStatus* outWorkflowStatus = nullptr,
      uint32_t sourceStateTimeoutMs = 2500UL);
  bool sendDirectHciCommand(uint16_t opcode,
                            const uint8_t* params,
                            size_t paramsLen,
                            uint8_t* response,
                            size_t responseSize,
                            size_t* responseLen);
  bool directStartConfiguredWorkflow(
      bool enableProcedure,
      BleCsControllerVprWorkflowStartStatus* outWorkflowStatus = nullptr);
  bool directReadRemoteSupportedCapabilities(uint8_t* outStatus);
  bool directSetDefaultSettings(const BleCsDefaultSettings& settings, uint8_t* outStatus);
  bool directCreateConfig(const BleCsControllerCreateConfig& config, uint8_t* outStatus);
  bool directRemoveConfig(uint8_t configId, uint8_t* outStatus);
  bool directSecurityEnable(uint8_t* outStatus);
  bool directSetProcedureParameters(const BleCsProcedureParameters& params,
                                    uint8_t* outStatus);
  bool directProcedureEnable(const BleCsProcedureEnable& params, uint8_t* outStatus);
  bool directProcedureEnable(uint8_t configId, bool enable, uint8_t* outStatus);
  bool directCurrentProcedureEnable(bool enable, uint8_t* outStatus);
  bool pollUntilRunningWithProcedureCount(uint16_t targetProcedureCount,
                                          uint8_t maxPolls,
                                          uint8_t* outPolls);
  bool pollUntilStopped(uint8_t maxPolls, uint8_t* outPolls);
  bool pollUntilStoppedWithProcedureCount(uint16_t targetProcedureCount,
                                          uint8_t maxPolls,
                                          uint8_t* outPolls);
  bool pollUntilStoppedOnConfig(uint8_t targetConfigId,
                                uint8_t maxPolls,
                                uint8_t* outPolls);
  bool pollUntilRunComplete(uint32_t targetLocalSubevents,
                            uint32_t targetPeerSubevents,
                            uint8_t maxPolls,
                            uint8_t* outPolls);
  bool pollUntilCompletedProcedureResult(uint16_t targetProcedureCount,
                                         uint32_t targetLocalSubevents,
                                         uint32_t targetPeerSubevents,
                                         uint8_t maxPolls,
                                         uint8_t* outPolls);
  bool pollUntilSelectedState(uint8_t selectedConfigId,
                              uint8_t storedCount,
                              bool selectedRunnable,
                              uint8_t maxPolls,
                              uint8_t* outPolls);
  bool pollUntilSelectedState(
      const BleCsControllerVprSelectedStateExpectation& expected,
      uint8_t maxPolls,
      uint8_t* outPolls);
  bool pollUntilRetainedSelectionState(uint8_t activeConfigId,
                                       uint8_t slot0ConfigId,
                                       uint8_t slot1ConfigId,
                                       uint8_t previousConfigId,
                                       uint8_t storedConfigCount,
                                       bool selectedRunnable,
                                       bool previousRunnable,
                                       uint8_t maxPolls,
                                       uint8_t* outPolls);
  bool pollUntilRetainedSelectionState(
      const BleCsControllerVprRetainedSelectionExpectation& expected,
      uint8_t maxPolls,
      uint8_t* outPolls);
  bool settleDirectIdle(uint8_t stablePollsRequired,
                        uint8_t maxPolls,
                        uint8_t* outPolls);
  bool pollUntilRetainedSlots(uint8_t activeConfigId,
                              uint8_t slot0ConfigId,
                              uint8_t slot1ConfigId,
                              uint8_t previousConfigId,
                              uint8_t activePrimarySlotIndex,
                              uint8_t freePrimarySlotCount,
                              uint8_t storedConfigCount,
                              uint8_t maxPolls,
                              uint8_t* outPolls);
  bool pollUntilRetainedSlots(
      const BleCsControllerVprRetainedSlotsExpectation& expected,
      uint8_t maxPolls,
      uint8_t* outPolls);
  bool pollUntilRetainedState(uint8_t activeConfigId,
                              uint8_t slot0ConfigId,
                              uint8_t slot1ConfigId,
                              uint8_t previousConfigId,
                              uint8_t activePrimarySlotIndex,
                              uint8_t freePrimarySlotCount,
                              uint8_t storedConfigCount,
                              bool selectedRunnable,
                              bool slot0Runnable,
                              bool slot1Runnable,
                              bool previousRunnable,
                              bool selectedSecurityEnabled,
                              bool slot0SecurityEnabled,
                              bool slot1SecurityEnabled,
                              bool previousSecurityEnabled,
                              bool selectedProcedureParamsApplied,
                              bool slot0ProcedureParamsApplied,
                              bool slot1ProcedureParamsApplied,
                              bool previousProcedureParamsApplied,
                              uint8_t maxPolls,
                              uint8_t* outPolls);
  bool pollUntilRetainedState(
      const BleCsControllerVprRetainedStateExpectation& expected,
      uint8_t maxPolls,
      uint8_t* outPolls);
  bool pumpCommands();
  bool poll();
  bool loopOnce();

  bool ready() const;
  bool failed() const;
  bool estimateValid() const;
  const BleCsControllerVprHostState& vprState() const;
  const BleCsControllerStreamHostState& streamState() const;
  const BleCsControllerHostState& hostState() const;
  const BleCsControllerSessionState& sessionState() const;
  const BleCsControllerWorkflowState& workflowState() const;
  const BleCsSubeventResult& localResult() const;
  const BleCsSubeventResult& peerResult() const;
  const BleCsSubeventResult& completedLocalResult() const;
  const BleCsSubeventResult& completedPeerResult() const;

  VprSharedTransportStream& transport();
  const VprSharedTransportStream& transport() const;

 private:
  bool currentConnHandle(uint16_t* outConnHandle) const;
  bool sendDirectBuiltCommand(const BleCsHciCommand& command, uint8_t* outStatus);
  bool drainDirectControllerEvents(VprControllerServiceHost* directHost,
                                   const uint8_t* response,
                                   size_t responseLen);
  void syncVprState();

  BleCsControllerVprHostConfig config_;
  BleCsControllerVprHostState vprState_;
  VprSharedTransportStream transport_;
  BleCsControllerStreamHost host_;
};

}  // namespace xiao_nrf54l15
