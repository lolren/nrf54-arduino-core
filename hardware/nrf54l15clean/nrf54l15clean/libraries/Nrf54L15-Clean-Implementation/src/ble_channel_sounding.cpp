#include "ble_channel_sounding.h"

#include <Arduino.h>

#include <math.h>
#include <string.h>

namespace xiao_nrf54l15 {

namespace {

constexpr uint8_t kMagic0 = 'C';
constexpr uint8_t kMagic1 = 'S';
constexpr uint8_t kPayloadHeaderLen = 6U;
constexpr uint8_t kReportToneExtraLen = 11U;
constexpr uint8_t kReportRttExtraLen = 9U;
constexpr uint8_t kReportExtraLen = kReportToneExtraLen + kReportRttExtraLen;
constexpr uint8_t kCteTypeAoA = 0U;
constexpr uint32_t kBleCrcPolynomial = 0x00065BUL;
constexpr uint32_t kCstStartBudgetUs = 1500U;
constexpr uint32_t kRadioEndBudgetUs = 3000U;
constexpr uint32_t kRadioDisableBudgetUs = 3000U;
constexpr uint32_t kAuxDataBudgetUs = 3000U;
constexpr uint32_t kSpinLimit = 3000000UL;
constexpr uint8_t kMicrosPollDivider = 32U;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSpeedOfLightMetersPerSecond = 299792458.0f;
constexpr size_t kMaxCsChannels = 37U;
constexpr size_t kMaxSlopePairs =
    (kMaxCsChannels * (kMaxCsChannels - 1U)) / 2U;
constexpr size_t kMaxCsControllerStepSamples = 256U;
constexpr uint8_t kCsChmapLen = 10U;
constexpr uint8_t kAntennaId1 = 0x1U;
constexpr uint8_t kAntennaId2 = 0x2U;
constexpr uint8_t kAntennaId3 = 0x3U;
constexpr uint8_t kAntennaId4 = 0x4U;
constexpr size_t kBleCsStepHeaderLen = 3U;
constexpr size_t kBleCsMode1StepLen = 6U;
constexpr size_t kBleCsMode1SsRttStepLen = 14U;
constexpr size_t kBleCsToneInfoLen = 4U;
constexpr size_t kBleCsMode3StepBaseLen = 7U;
constexpr size_t kBleCsMode3SsRttStepBaseLen = 15U;
constexpr size_t kBleCsHciSubeventResultHeaderLen = 15U;
constexpr size_t kBleCsHciSubeventResultContinueHeaderLen = 8U;
constexpr size_t kBleCsHciReadRemoteCapsCompleteLen = 31U;
constexpr size_t kBleCsHciReadRemoteCapsCompleteV2Len = 34U;
constexpr size_t kBleCsHciSecurityEnableCompleteLen = 3U;
constexpr size_t kBleCsHciConfigCompleteLen = 33U;
constexpr size_t kBleCsHciProcedureEnableCompleteLen = 21U;
constexpr uint8_t kBleCsVprVendorEvtPeerResultTrigger = 0xB1U;
constexpr uint8_t kBleCsVprVendorEvtPeerResultSource = 0xB2U;

struct BleCsControllerPhasePair {
  bool failed = false;
  bool localPresent = false;
  bool peerPresent = false;
  uint8_t channel = 0U;
  BleCsIqSample local{};
  BleCsIqSample peer{};
  uint8_t localQuality = kBleCsToneQualityUnavailable;
  uint8_t peerQuality = kBleCsToneQualityUnavailable;
};

struct BleCsControllerRttPair {
  bool failed = false;
  bool initiatorPresent = false;
  bool reflectorPresent = false;
  int16_t toaTodInitiator = kBleCsTimeDifferenceNotAvailable;
  int16_t todToaReflector = kBleCsTimeDifferenceNotAvailable;
};

struct BleCsControllerBufferParseContext {
  BleCsControllerPhasePair* phasePairs = nullptr;
  size_t phaseCapacity = 0U;
  size_t* phaseCount = nullptr;
  size_t phaseCursor = 0U;
  BleCsControllerRttPair* rttPairs = nullptr;
  size_t rttCapacity = 0U;
  size_t* rttCount = nullptr;
  size_t rttCursor = 0U;
  bool fillingPeer = false;
  bool bufferRoleIsInitiator = false;
};

inline uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8U);
}

inline void writeLe16(uint8_t* data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

inline uint32_t readLe24(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U);
}

inline void writeLe24(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  data[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
}

inline void writeVolatileLe16(volatile uint8_t* data, uint16_t value) {
  if (data == nullptr) {
    return;
  }
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

inline void writeVolatileLe32(volatile uint8_t* data, uint32_t value) {
  if (data == nullptr) {
    return;
  }
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  data[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  data[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

bool writeBleConnectionBootHandoff(
    volatile Nrf54l15VprTransportHostShared* sharedHost,
    const VprBleConnectionSharedState& state) {
  if (sharedHost == nullptr || !state.connected || state.connHandle == 0U ||
      NRF54L15_VPR_BLE_CONN_HANDOFF_LEN > sizeof(sharedHost->hostData)) {
    return false;
  }

  memset(const_cast<uint8_t*>(
             &const_cast<Nrf54l15VprTransportHostShared*>(sharedHost)->hostData[0]),
         0, sizeof(sharedHost->hostData));
  sharedHost->reserved = NRF54L15_VPR_BLE_CONN_HANDOFF_COOKIE;
  sharedHost->hostData[0] = state.connected ? 1U : 0U;
  writeVolatileLe16(&sharedHost->hostData[1], state.connHandle);
  sharedHost->hostData[3] = state.role;
  sharedHost->hostData[4] = state.encrypted ? 1U : 0U;
  writeVolatileLe16(&sharedHost->hostData[5], state.intervalUnits);
  writeVolatileLe16(&sharedHost->hostData[7], state.latency);
  writeVolatileLe16(&sharedHost->hostData[9], state.supervisionTimeout);
  sharedHost->hostData[11] = state.txPhy;
  sharedHost->hostData[12] = state.rxPhy;
  writeVolatileLe32(&sharedHost->hostData[13], state.eventCount);
  __DMB();
  __DSB();
  return true;
}

uint8_t csFrequencyOffsetMHz(uint8_t channel) {
  if (channel <= 10U) {
    return static_cast<uint8_t>(4U + (2U * channel));
  }
  return static_cast<uint8_t>(6U + (2U * channel));
}

void encodePctSampleBytes(int16_t i, int16_t q, uint8_t outPct[3]) {
  if (outPct == nullptr) {
    return;
  }

  auto clamp12 = [](int16_t value) -> int16_t {
    if (value < -2048) {
      return -2048;
    }
    if (value > 2047) {
      return 2047;
    }
    return value;
  };

  const uint16_t i12 = static_cast<uint16_t>(clamp12(i)) & 0x0FFFU;
  const uint16_t q12 = static_cast<uint16_t>(clamp12(q)) & 0x0FFFU;
  const uint32_t packed =
      static_cast<uint32_t>(i12) | (static_cast<uint32_t>(q12) << 12U);
  outPct[0] = static_cast<uint8_t>(packed & 0xFFU);
  outPct[1] = static_cast<uint8_t>((packed >> 8U) & 0xFFU);
  outPct[2] = static_cast<uint8_t>((packed >> 16U) & 0xFFU);
}

bool appendMode2DemoStep(uint8_t* buffer,
                         size_t maxLen,
                         size_t* offset,
                         uint8_t channel,
                         int16_t i,
                         int16_t q) {
  if (buffer == nullptr || offset == nullptr || (maxLen - *offset) < 8U) {
    return false;
  }

  buffer[*offset + 0U] = kBleCsMainMode2;
  buffer[*offset + 1U] = channel;
  buffer[*offset + 2U] = 5U;
  buffer[*offset + 3U] = 0U;
  encodePctSampleBytes(i, q, &buffer[*offset + 4U]);
  buffer[*offset + 7U] = kBleCsToneQualityHigh;
  *offset += 8U;
  return true;
}

bool buildHciInitialEvent(uint8_t* out,
                          size_t maxLen,
                          uint16_t connHandle,
                          uint8_t configId,
                          uint16_t startAclConnEventCounter,
                          uint16_t procedureCounter,
                          uint8_t numAntennaPaths,
                          uint8_t numStepsReported,
                          const uint8_t* stepBytes,
                          size_t stepLen,
                          bool partial) {
  if (out == nullptr || stepBytes == nullptr || maxLen < (15U + stepLen)) {
    return false;
  }
  writeLe16(out + 0U, connHandle);
  out[2U] = configId;
  writeLe16(out + 3U, startAclConnEventCounter);
  writeLe16(out + 5U, procedureCounter);
  writeLe16(out + 7U, 0U);
  out[9U] = 0U;
  out[10U] = partial ? kBleCsProcedureDonePartial : kBleCsProcedureDoneComplete;
  out[11U] = partial ? kBleCsSubeventDonePartial : kBleCsSubeventDoneComplete;
  out[12U] = 0U;
  out[13U] = numAntennaPaths;
  out[14U] = numStepsReported;
  memcpy(out + 15U, stepBytes, stepLen);
  return true;
}

bool buildHciContinueEvent(uint8_t* out,
                           size_t maxLen,
                           uint16_t connHandle,
                           uint8_t configId,
                           uint8_t numAntennaPaths,
                           uint8_t numStepsReported,
                           const uint8_t* stepBytes,
                           size_t stepLen,
                           bool partial) {
  if (out == nullptr || stepBytes == nullptr || maxLen < (8U + stepLen)) {
    return false;
  }
  writeLe16(out + 0U, connHandle);
  out[2U] = configId;
  out[3U] = partial ? kBleCsProcedureDonePartial : kBleCsProcedureDoneComplete;
  out[4U] = partial ? kBleCsSubeventDonePartial : kBleCsSubeventDoneComplete;
  out[5U] = 0U;
  out[6U] = numAntennaPaths;
  out[7U] = numStepsReported;
  memcpy(out + 8U, stepBytes, stepLen);
  return true;
}

bool buildH4LeMetaEvent(uint8_t* out,
                        size_t maxLen,
                        uint8_t subeventCode,
                        const uint8_t* payload,
                        size_t payloadLen) {
  if (out == nullptr || payload == nullptr || maxLen < (4U + payloadLen)) {
    return false;
  }
  out[0U] = kBleHciPacketTypeEvent;
  out[1U] = kBleHciEvtLeMeta;
  out[2U] = static_cast<uint8_t>(1U + payloadLen);
  out[3U] = subeventCode;
  memcpy(out + 4U, payload, payloadLen);
  return true;
}

bool buildDemoPeerResultPackets(uint16_t connHandle,
                                uint8_t configId,
                                uint16_t procedureCounter,
                                const BleCsControllerVprBuiltInPeerDemoConfig& config,
                                uint8_t* initPacket,
                                size_t initPacketMaxLen,
                                size_t* initPacketLen,
                                uint8_t* contPacket,
                                size_t contPacketMaxLen,
                                size_t* contPacketLen) {
  if (initPacket == nullptr || initPacketLen == nullptr || contPacket == nullptr ||
      contPacketLen == nullptr) {
    return false;
  }

  *initPacketLen = 0U;
  *contPacketLen = 0U;

  const uint8_t channelCount =
      (config.channelCount <= sizeof(config.channels)) ? config.channelCount
                                                       : sizeof(config.channels);
  if (channelCount == 0U) {
    return false;
  }

  uint8_t peerSteps[64] = {0};
  size_t peerLen = 0U;
  for (uint8_t i = 0U; i < channelCount; ++i) {
    const uint8_t channel = config.channels[i];
    const float freqHz =
        (2400.0f + static_cast<float>(csFrequencyOffsetMHz(channel))) * 1000000.0f;
    const float theta = -((4.0f * kPi * config.distanceMeters * freqHz) /
                          kSpeedOfLightMetersPerSecond);
    const int16_t peerI =
        static_cast<int16_t>(lroundf(cosf(theta) * config.amplitude));
    const int16_t peerQ =
        static_cast<int16_t>(lroundf(sinf(theta) * config.amplitude));
    if (!appendMode2DemoStep(peerSteps, sizeof(peerSteps), &peerLen, channel, peerI, peerQ)) {
      return false;
    }
  }

  const size_t splitLen = (peerLen > 16U) ? 16U : peerLen;
  const size_t contLen = peerLen - splitLen;
  const bool partial = contLen > 0U;
  const uint16_t startAclConnEventCounter = 0x1234U;
  const uint8_t numAntennaPaths = 2U;
  const uint8_t initSteps = static_cast<uint8_t>(splitLen / 8U);
  const uint8_t contSteps = static_cast<uint8_t>(contLen / 8U);
  uint8_t initPayload[64] = {0};
  uint8_t contPayload[64] = {0};

  if (!buildHciInitialEvent(initPayload, sizeof(initPayload), connHandle, configId,
                            startAclConnEventCounter, procedureCounter,
                            numAntennaPaths, initSteps, peerSteps, splitLen, partial) ||
      !buildH4LeMetaEvent(initPacket, initPacketMaxLen, kBleCsHciEvtSubeventResult,
                          initPayload, 15U + splitLen)) {
    return false;
  }
  *initPacketLen = 4U + 15U + splitLen;

  if (contLen == 0U) {
    return true;
  }

  if (!buildHciContinueEvent(contPayload, sizeof(contPayload), connHandle, configId,
                             numAntennaPaths, contSteps, peerSteps + splitLen, contLen,
                             false) ||
      !buildH4LeMetaEvent(contPacket, contPacketMaxLen,
                          kBleCsHciEvtSubeventResultContinue, contPayload,
                          8U + contLen)) {
    *initPacketLen = 0U;
    return false;
  }
  *contPacketLen = 4U + 8U + contLen;
  return true;
}

bool decodeHciEventFrame(const uint8_t* packet,
                         size_t packetLen,
                         uint8_t* outEventCode,
                         const uint8_t** outParams,
                         size_t* outParamsLen) {
  if (packet == nullptr || outEventCode == nullptr || outParams == nullptr ||
      outParamsLen == nullptr) {
    return false;
  }

  if (packetLen >= 3U && packet[0] == kBleHciPacketTypeEvent) {
    const size_t paramsLen = packet[2U];
    if (packetLen < (3U + paramsLen)) {
      return false;
    }
    *outEventCode = packet[1U];
    *outParams = packet + 3U;
    *outParamsLen = paramsLen;
    return true;
  }

  if (packetLen >= 2U) {
    const size_t paramsLen = packet[1U];
    if (packetLen < (2U + paramsLen)) {
      return false;
    }
    *outEventCode = packet[0U];
    *outParams = packet + 2U;
    *outParamsLen = paramsLen;
    return true;
  }

  return false;
}

inline int16_t readLe16Signed(const uint8_t* data) {
  return static_cast<int16_t>(readLe16(data));
}

inline bool validDataChannel(uint8_t channelIndex) {
  return channelIndex <= 36U;
}

inline bool validLogicalChannel(uint8_t channelIndex) {
  return (channelIndex <= 36U) || (channelIndex >= 37U && channelIndex <= 39U);
}

uint8_t logicalChannelToFrequency(uint8_t channelIndex) {
  if (channelIndex <= 36U) {
    if (channelIndex <= 10U) {
      return static_cast<uint8_t>(4U + (2U * channelIndex));
    }
    return static_cast<uint8_t>(6U + (2U * channelIndex));
  }

  switch (channelIndex) {
    case 37U:
      return 2U;
    case 38U:
      return 26U;
    case 39U:
    default:
      return 80U;
  }
}

uint32_t bleDataWhiteValue(uint8_t channelIndex) {
  const uint32_t iv = static_cast<uint32_t>(0x40U | (channelIndex & 0x3FU));
  const uint32_t poly = 0x89UL;
  return ((poly << RADIO_DATAWHITE_POLY_Pos) & RADIO_DATAWHITE_POLY_Msk) |
         ((iv << RADIO_DATAWHITE_IV_Pos) & RADIO_DATAWHITE_IV_Msk);
}

uint32_t accessAddressBase(uint32_t accessAddress) {
  return (accessAddress << 8U);
}

uint32_t accessAddressPrefix(uint32_t accessAddress) {
  return ((accessAddress >> 24U) & 0xFFU);
}

inline void channelMapBitSetVal(uint8_t* channelMap, uint8_t bit, uint8_t value) {
  channelMap[bit / 8U] = static_cast<uint8_t>(
      (channelMap[bit / 8U] & ~static_cast<uint8_t>(1U << (bit % 8U))) |
      ((value & 0x1U) << (bit % 8U)));
}

uint32_t txPowerRegFromDbm(int8_t dbm) {
  if (dbm >= 8) {
    return RADIO_TXPOWER_TXPOWER_Pos8dBm;
  }
  if (dbm >= 7) {
    return RADIO_TXPOWER_TXPOWER_Pos7dBm;
  }
  if (dbm >= 6) {
    return RADIO_TXPOWER_TXPOWER_Pos6dBm;
  }
  if (dbm >= 5) {
    return RADIO_TXPOWER_TXPOWER_Pos5dBm;
  }
  if (dbm >= 4) {
    return RADIO_TXPOWER_TXPOWER_Pos4dBm;
  }
  if (dbm >= 3) {
    return RADIO_TXPOWER_TXPOWER_Pos3dBm;
  }
  if (dbm >= 2) {
    return RADIO_TXPOWER_TXPOWER_Pos2dBm;
  }
  if (dbm >= 1) {
    return RADIO_TXPOWER_TXPOWER_Pos1dBm;
  }
  if (dbm >= 0) {
    return RADIO_TXPOWER_TXPOWER_0dBm;
  }
  if (dbm >= -1) {
    return RADIO_TXPOWER_TXPOWER_Neg1dBm;
  }
  if (dbm >= -2) {
    return RADIO_TXPOWER_TXPOWER_Neg2dBm;
  }
  if (dbm >= -3) {
    return RADIO_TXPOWER_TXPOWER_Neg3dBm;
  }
  if (dbm >= -4) {
    return RADIO_TXPOWER_TXPOWER_Neg4dBm;
  }
  if (dbm >= -5) {
    return RADIO_TXPOWER_TXPOWER_Neg5dBm;
  }
  if (dbm >= -6) {
    return RADIO_TXPOWER_TXPOWER_Neg6dBm;
  }
  if (dbm >= -7) {
    return RADIO_TXPOWER_TXPOWER_Neg7dBm;
  }
  if (dbm >= -8) {
    return RADIO_TXPOWER_TXPOWER_Neg8dBm;
  }
  if (dbm >= -9) {
    return RADIO_TXPOWER_TXPOWER_Neg9dBm;
  }
  if (dbm >= -10) {
    return RADIO_TXPOWER_TXPOWER_Neg10dBm;
  }
  if (dbm >= -12) {
    return RADIO_TXPOWER_TXPOWER_Neg12dBm;
  }
  if (dbm >= -14) {
    return RADIO_TXPOWER_TXPOWER_Neg14dBm;
  }
  if (dbm >= -16) {
    return RADIO_TXPOWER_TXPOWER_Neg16dBm;
  }
  if (dbm >= -18) {
    return RADIO_TXPOWER_TXPOWER_Neg18dBm;
  }
  if (dbm >= -20) {
    return RADIO_TXPOWER_TXPOWER_Neg20dBm;
  }
  if (dbm >= -28) {
    return RADIO_TXPOWER_TXPOWER_Neg28dBm;
  }
  if (dbm >= -40) {
    return RADIO_TXPOWER_TXPOWER_Neg40dBm;
  }
  return RADIO_TXPOWER_TXPOWER_Neg46dBm;
}

void clearRadioEvents(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return;
  }

  radio->EVENTS_READY = 0U;
  radio->EVENTS_TXREADY = 0U;
  radio->EVENTS_RXREADY = 0U;
  radio->EVENTS_ADDRESS = 0U;
  radio->EVENTS_PAYLOAD = 0U;
  radio->EVENTS_END = 0U;
  radio->EVENTS_PHYEND = 0U;
  radio->EVENTS_DISABLED = 0U;
  radio->EVENTS_CRCOK = 0U;
  radio->EVENTS_CRCERROR = 0U;
  radio->EVENTS_CTEPRESENT = 0U;
  radio->EVENTS_AUXDATADMAEND = 0U;
  radio->EVENTS_CSTONESEND = 0U;
}

bool waitForFlag(volatile uint32_t* flag, uint32_t budgetUs) {
  if (flag == nullptr) {
    return false;
  }

  const uint32_t startUs = micros();
  uint8_t divider = kMicrosPollDivider;
  uint32_t spins = kSpinLimit;
  while (spins-- > 0U) {
    if (*flag != 0U) {
      return true;
    }

    if (--divider == 0U) {
      divider = kMicrosPollDivider;
      if ((budgetUs > 0U) &&
          (static_cast<uint32_t>(micros() - startUs) >= budgetUs)) {
        break;
      }
    }
  }

  return (*flag != 0U);
}

bool waitForCrcDone(NRF_RADIO_Type* radio, uint32_t budgetUs) {
  if (radio == nullptr) {
    return false;
  }

  const uint32_t startUs = micros();
  uint8_t divider = kMicrosPollDivider;
  uint32_t spins = kSpinLimit;
  while (spins-- > 0U) {
    if ((radio->EVENTS_CRCOK != 0U) || (radio->EVENTS_CRCERROR != 0U)) {
      return true;
    }

    if (--divider == 0U) {
      divider = kMicrosPollDivider;
      if ((budgetUs > 0U) &&
          (static_cast<uint32_t>(micros() - startUs) >= budgetUs)) {
        break;
      }
    }
  }

  return ((radio->EVENTS_CRCOK != 0U) || (radio->EVENTS_CRCERROR != 0U));
}

bool waitForRadioDisabled(NRF_RADIO_Type* radio, uint32_t budgetUs) {
  if (radio == nullptr) {
    return false;
  }

  const uint32_t startUs = micros();
  uint8_t divider = kMicrosPollDivider;
  uint32_t spins = kSpinLimit;
  while (spins-- > 0U) {
    if (radio->EVENTS_DISABLED != 0U) {
      return true;
    }

    const uint32_t state =
        (radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (state == RADIO_STATE_STATE_Disabled) {
      return true;
    }

    if (--divider == 0U) {
      divider = kMicrosPollDivider;
      if ((budgetUs > 0U) &&
          (static_cast<uint32_t>(micros() - startUs) >= budgetUs)) {
        break;
      }
    }
  }

  const uint32_t state =
      (radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  return (radio->EVENTS_DISABLED != 0U) ||
         (state == RADIO_STATE_STATE_Disabled);
}

bool waitForRadioPhyEnd(NRF_RADIO_Type* radio, uint32_t budgetUs) {
  if (radio == nullptr) {
    return false;
  }

  const uint32_t startUs = micros();
  uint8_t divider = kMicrosPollDivider;
  uint32_t spins = kSpinLimit;
  while (spins-- > 0U) {
    if (radio->EVENTS_PHYEND != 0U) {
      return true;
    }

    if (--divider == 0U) {
      divider = kMicrosPollDivider;
      if ((budgetUs > 0U) &&
          (static_cast<uint32_t>(micros() - startUs) >= budgetUs)) {
        break;
      }
    }
  }

  return (radio->EVENTS_PHYEND != 0U);
}

int8_t radioRssiDbm(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return 0;
  }

  const uint8_t raw =
      static_cast<uint8_t>((radio->RSSISAMPLE & RADIO_RSSISAMPLE_RSSISAMPLE_Msk) >>
                           RADIO_RSSISAMPLE_RSSISAMPLE_Pos);
  return -static_cast<int8_t>(raw);
}

void sortFloats(float* values, size_t count) {
  if (values == nullptr || count < 2U) {
    return;
  }

  for (size_t i = 1U; i < count; ++i) {
    const float key = values[i];
    size_t j = i;
    while (j > 0U && values[j - 1U] > key) {
      values[j] = values[j - 1U];
      --j;
    }
    values[j] = key;
  }
}

float medianInPlace(float* values, size_t count) {
  if (values == nullptr || count == 0U) {
    return 0.0f;
  }

  sortFloats(values, count);
  if ((count & 0x1U) == 0U) {
    const size_t upper = count / 2U;
    return (values[upper - 1U] + values[upper]) * 0.5f;
  }
  return values[count / 2U];
}

bool fitTheilSenLine(const float* freqsHz,
                     const float* phases,
                     size_t count,
                     float* outSlope,
                     float* outIntercept) {
  if (freqsHz == nullptr || phases == nullptr || outSlope == nullptr ||
      outIntercept == nullptr || count < 2U || count > kMaxCsChannels) {
    return false;
  }

  float slopes[kMaxSlopePairs] = {0.0f};
  size_t slopeCount = 0U;
  for (size_t i = 0U; i + 1U < count; ++i) {
    for (size_t j = i + 1U; j < count; ++j) {
      const float df = freqsHz[j] - freqsHz[i];
      if (df == 0.0f) {
        continue;
      }
      slopes[slopeCount++] = (phases[j] - phases[i]) / df;
    }
  }
  if (slopeCount == 0U) {
    return false;
  }

  const float slope = medianInPlace(slopes, slopeCount);
  float intercepts[kMaxCsChannels] = {0.0f};
  for (size_t i = 0U; i < count; ++i) {
    intercepts[i] = phases[i] - (slope * freqsHz[i]);
  }

  *outSlope = slope;
  *outIntercept = medianInPlace(intercepts, count);
  return true;
}

bool fitWeightedLine(const float* freqsHz,
                     const float* phases,
                     const float* weights,
                     size_t count,
                     float* outSlope,
                     float* outIntercept) {
  if (freqsHz == nullptr || phases == nullptr || weights == nullptr ||
      outSlope == nullptr || outIntercept == nullptr || count < 2U ||
      count > kMaxCsControllerStepSamples) {
    return false;
  }

  float weightSum = 0.0f;
  float weightedX = 0.0f;
  float weightedY = 0.0f;
  for (size_t i = 0U; i < count; ++i) {
    const float weight = fmaxf(weights[i], 0.0001f);
    weightSum += weight;
    weightedX += weight * freqsHz[i];
    weightedY += weight * phases[i];
  }
  if (!(weightSum > 0.0f)) {
    return false;
  }

  const float xMean = weightedX / weightSum;
  const float yMean = weightedY / weightSum;
  float numerator = 0.0f;
  float denominator = 0.0f;
  for (size_t i = 0U; i < count; ++i) {
    const float weight = fmaxf(weights[i], 0.0001f);
    const float dx = freqsHz[i] - xMean;
    numerator += weight * dx * (phases[i] - yMean);
    denominator += weight * dx * dx;
  }
  if (!(denominator > 0.0f)) {
    return false;
  }

  const float slope = numerator / denominator;
  *outSlope = slope;
  *outIntercept = yMean - (slope * xMean);
  return true;
}

bool estimateMedianAndMad(const float* values,
                          size_t count,
                          float* outMedian,
                          float* outMad) {
  if (values == nullptr || outMedian == nullptr || outMad == nullptr ||
      count == 0U || count > kMaxCsChannels) {
    return false;
  }

  float scratch[kMaxCsChannels] = {0.0f};
  for (size_t i = 0U; i < count; ++i) {
    scratch[i] = values[i];
  }
  const float median = medianInPlace(scratch, count);

  for (size_t i = 0U; i < count; ++i) {
    scratch[i] = fabsf(values[i] - median);
  }
  *outMedian = median;
  *outMad = medianInPlace(scratch, count);
  return true;
}

bool estimateMedianAndMadWide(const float* values,
                              size_t count,
                              float* outMedian,
                              float* outMad) {
  if (values == nullptr || outMedian == nullptr || outMad == nullptr ||
      count == 0U || count > kMaxCsControllerStepSamples) {
    return false;
  }

  float scratch[kMaxCsControllerStepSamples] = {0.0f};
  for (size_t i = 0U; i < count; ++i) {
    scratch[i] = values[i];
  }
  const float median = medianInPlace(scratch, count);
  for (size_t i = 0U; i < count; ++i) {
    scratch[i] = fabsf(values[i] - median);
  }
  *outMedian = median;
  *outMad = medianInPlace(scratch, count);
  return true;
}

void sortPhaseSamplesWithQuality(float* freqsHz,
                                 float* phases,
                                 float* quality,
                                 size_t count) {
  if (freqsHz == nullptr || phases == nullptr || quality == nullptr ||
      count < 2U) {
    return;
  }

  for (size_t i = 1U; i < count; ++i) {
    const float freq = freqsHz[i];
    const float phase = phases[i];
    const float score = quality[i];
    size_t j = i;
    while (j > 0U && freqsHz[j - 1U] > freq) {
      freqsHz[j] = freqsHz[j - 1U];
      phases[j] = phases[j - 1U];
      quality[j] = quality[j - 1U];
      --j;
    }
    freqsHz[j] = freq;
    phases[j] = phase;
    quality[j] = score;
  }
}

float toneQualityScore(const BleCsToneSample& local, const BleCsToneSample& peer) {
  if (!local.valid || !peer.valid) {
    return 0.0f;
  }

  const float localMag = static_cast<float>(local.magnitude);
  const float peerMag = static_cast<float>(peer.magnitude);
  const float localStd = static_cast<float>(local.magnitudeStd);
  const float peerStd = static_cast<float>(peer.magnitudeStd);
  const float denom = 1.0f + localStd + peerStd;
  const float minMagnitude = fminf(localMag, peerMag);
  if (denom <= 0.0f || minMagnitude <= 0.0f) {
    return 0.0f;
  }

  return minMagnitude / denom;
}

float stepToneQualityScore(uint8_t localQuality, uint8_t peerQuality) {
  auto scoreOne = [](uint8_t quality) -> float {
    switch (quality) {
      case kBleCsToneQualityHigh:
        return 1.0f;
      case kBleCsToneQualityMedium:
        return 0.75f;
      case kBleCsToneQualityLow:
        return 0.25f;
      case kBleCsToneQualityUnavailable:
      default:
        return 0.0f;
    }
  };
  return fminf(scoreOne(localQuality), scoreOne(peerQuality));
}

bool controllerPhaseDistanceEstimate(const BleCsControllerPhasePair* pairs,
                                     size_t pairCount,
                                     BleCsEstimate* outEstimate) {
  if (pairs == nullptr || outEstimate == nullptr || pairCount == 0U) {
    return false;
  }

  float freqsHz[kMaxCsControllerStepSamples] = {0.0f};
  float phases[kMaxCsControllerStepSamples] = {0.0f};
  float quality[kMaxCsControllerStepSamples] = {0.0f};
  size_t sampleCount = 0U;
  for (size_t i = 0U; i < pairCount && sampleCount < kMaxCsControllerStepSamples; ++i) {
    if (pairs[i].failed || !pairs[i].localPresent || !pairs[i].peerPresent ||
        !validDataChannel(pairs[i].channel)) {
      continue;
    }
    const float localI = static_cast<float>(pairs[i].local.i);
    const float localQ = static_cast<float>(pairs[i].local.q);
    const float peerI = static_cast<float>(pairs[i].peer.i);
    const float peerQ = static_cast<float>(pairs[i].peer.q);
    const float combI = (localI * peerI) - (localQ * peerQ);
    const float combQ = (localI * peerQ) + (peerI * localQ);
    freqsHz[sampleCount] =
        (2400.0f + static_cast<float>(logicalChannelToFrequency(pairs[i].channel))) *
        1000000.0f;
    phases[sampleCount] = atan2f(combQ, combI);
    quality[sampleCount] =
        stepToneQualityScore(pairs[i].localQuality, pairs[i].peerQuality);
    ++sampleCount;
  }

  outEstimate->totalToneChannels = static_cast<uint8_t>(
      (sampleCount <= 255U) ? sampleCount : 255U);
  if (sampleCount < 2U) {
    return false;
  }

  sortPhaseSamplesWithQuality(freqsHz, phases, quality, sampleCount);
  for (size_t i = 1U; i < sampleCount; ++i) {
    const float prev = phases[i - 1U];
    while ((phases[i] - prev) > kPi) {
      phases[i] -= 2.0f * kPi;
    }
    while ((phases[i] - prev) < -kPi) {
      phases[i] += 2.0f * kPi;
    }
  }

  float medianQuality = 0.0f;
  float qualityMad = 0.0f;
  if (estimateMedianAndMadWide(quality, sampleCount, &medianQuality, &qualityMad)) {
    outEstimate->medianToneQuality = medianQuality;
  }

  float slope = 0.0f;
  float intercept = 0.0f;
  if (!fitWeightedLine(freqsHz, phases, quality, sampleCount, &slope, &intercept)) {
    return false;
  }

  float residuals[kMaxCsControllerStepSamples] = {0.0f};
  float residualAccum = 0.0f;
  for (size_t i = 0U; i < sampleCount; ++i) {
    residuals[i] = phases[i] - (intercept + (slope * freqsHz[i]));
    residualAccum += residuals[i] * residuals[i];
  }

  const float phaseDistance =
      fabsf(-(kSpeedOfLightMetersPerSecond * slope) / (4.0f * kPi));
  if (!isfinite(phaseDistance) || phaseDistance <= 0.0f) {
    return false;
  }

  outEstimate->usedChannels = static_cast<uint8_t>(
      (sampleCount <= 255U) ? sampleCount : 255U);
  outEstimate->phaseSlopeDistanceMeters = phaseDistance;
  outEstimate->distanceMeters = phaseDistance;
  outEstimate->slopeRadPerHz = slope;
  outEstimate->residualVariance = residualAccum / static_cast<float>(sampleCount);
  return true;
}

bool controllerRttDistanceEstimate(const BleCsControllerRttPair* pairs,
                                   size_t pairCount,
                                   BleCsEstimate* outEstimate) {
  if (pairs == nullptr || outEstimate == nullptr || pairCount == 0U) {
    return false;
  }

  float rttDistances[kMaxCsControllerStepSamples] = {0.0f};
  size_t rttCount = 0U;
  for (size_t i = 0U; i < pairCount && rttCount < kMaxCsControllerStepSamples; ++i) {
    if (pairs[i].failed || !pairs[i].initiatorPresent || !pairs[i].reflectorPresent) {
      continue;
    }
    const int32_t roundTripHalfNs =
        static_cast<int32_t>(pairs[i].toaTodInitiator) -
        static_cast<int32_t>(pairs[i].todToaReflector);
    if (roundTripHalfNs <= 0) {
      continue;
    }
    const float tofNs = static_cast<float>(roundTripHalfNs) * 0.25f;
    const float distance =
        tofNs * (kSpeedOfLightMetersPerSecond / 1000000000.0f);
    if (!isfinite(distance) || distance <= 0.0f) {
      continue;
    }
    rttDistances[rttCount++] = distance;
  }

  if (rttCount == 0U) {
    return false;
  }

  float median = 0.0f;
  float mad = 0.0f;
  if (!estimateMedianAndMadWide(rttDistances, rttCount, &median, &mad)) {
    return false;
  }

  float varianceAccum = 0.0f;
  size_t inlierCount = 0U;
  const float threshold = fmaxf(0.20f, mad * 3.0f);
  for (size_t i = 0U; i < rttCount; ++i) {
    if (fabsf(rttDistances[i] - median) > threshold) {
      continue;
    }
    const float err = rttDistances[i] - median;
    varianceAccum += err * err;
    ++inlierCount;
  }
  if (inlierCount == 0U) {
    return false;
  }

  outEstimate->rttChannels = static_cast<uint8_t>(
      (inlierCount <= 255U) ? inlierCount : 255U);
  outEstimate->rttDistanceMeters = median;
  outEstimate->rttVariance = varianceAccum / static_cast<float>(inlierCount);
  return true;
}

bool parseControllerStepBufferCallback(const BleCsSubeventStep* step, void* userData) {
  BleCsControllerBufferParseContext* context =
      static_cast<BleCsControllerBufferParseContext*>(userData);
  if (context == nullptr || step == nullptr) {
    return false;
  }

  if (step->mode == kBleCsMainMode2 || step->mode == kBleCsMainMode3) {
    uint8_t toneCount = 0U;
    if (step->mode == kBleCsMainMode2) {
      BleCsStepMode2Data mode2{};
      if (!BleChannelSoundingRadio::parseMode2StepData(step, &mode2)) {
        return false;
      }
      toneCount = mode2.toneCount;
    } else {
      BleCsStepMode3Data mode3{};
      if (!BleChannelSoundingRadio::parseMode3StepData(step, &mode3)) {
        return false;
      }
      toneCount = mode3.toneCount;
    }

    for (uint8_t toneIndex = 0U; toneIndex < toneCount; ++toneIndex) {
      BleCsStepToneInfo tone{};
      const bool parsed =
          (step->mode == kBleCsMainMode2)
              ? BleChannelSoundingRadio::parseMode2ToneInfo(step, toneIndex, &tone)
              : BleChannelSoundingRadio::parseMode3ToneInfo(step, toneIndex, &tone);
      if (!parsed || tone.extensionIndicator != kBleCsToneExtensionNone) {
        continue;
      }

      if (!context->fillingPeer) {
        if (context->phasePairs == nullptr || context->phaseCount == nullptr ||
            *context->phaseCount >= context->phaseCapacity) {
          return false;
        }
        BleCsControllerPhasePair& pair = context->phasePairs[*context->phaseCount];
        pair.failed = (tone.qualityIndicator == kBleCsToneQualityLow) ||
                      (tone.qualityIndicator == kBleCsToneQualityUnavailable);
        pair.localPresent = true;
        pair.channel = step->channel;
        pair.local = tone.pct;
        pair.localQuality = tone.qualityIndicator;
        ++(*context->phaseCount);
      } else {
        if (context->phasePairs == nullptr || context->phaseCount == nullptr ||
            context->phaseCursor >= *context->phaseCount) {
          return false;
        }
        BleCsControllerPhasePair& pair = context->phasePairs[context->phaseCursor++];
        if (pair.channel != step->channel) {
          pair.failed = true;
        }
        if ((tone.qualityIndicator == kBleCsToneQualityLow) ||
            (tone.qualityIndicator == kBleCsToneQualityUnavailable)) {
          pair.failed = true;
        }
        pair.peerPresent = true;
        pair.peer = tone.pct;
        pair.peerQuality = tone.qualityIndicator;
      }
    }
  }

  if (step->mode == kBleCsMainMode1 || step->mode == kBleCsMainMode3) {
    BleCsStepMode1Data mode1{};
    if (!BleChannelSoundingRadio::parseMode1StepData(step, &mode1)) {
      return false;
    }
    const bool invalidRtt =
        (mode1.aaCheckQuality != kBleCsPacketQualityAaCheckOk) ||
        (static_cast<uint8_t>(mode1.packetRssiDbm) == kBleCsPacketRssiNotAvailable) ||
        (mode1.timeDifferenceHalfNs == kBleCsTimeDifferenceNotAvailable);

    if (!context->fillingPeer) {
      if (context->rttPairs == nullptr || context->rttCount == nullptr ||
          *context->rttCount >= context->rttCapacity) {
        return false;
      }
      BleCsControllerRttPair& pair = context->rttPairs[*context->rttCount];
      pair.failed = invalidRtt;
      if (context->bufferRoleIsInitiator) {
        pair.initiatorPresent = true;
        pair.toaTodInitiator = mode1.timeDifferenceHalfNs;
      } else {
        pair.reflectorPresent = true;
        pair.todToaReflector = mode1.timeDifferenceHalfNs;
      }
      ++(*context->rttCount);
    } else {
      if (context->rttPairs == nullptr || context->rttCount == nullptr ||
          context->rttCursor >= *context->rttCount) {
        return false;
      }
      BleCsControllerRttPair& pair = context->rttPairs[context->rttCursor++];
      if (invalidRtt) {
        pair.failed = true;
      }
      if (context->bufferRoleIsInitiator) {
        pair.initiatorPresent = true;
        pair.toaTodInitiator = mode1.timeDifferenceHalfNs;
      } else {
        pair.reflectorPresent = true;
        pair.todToaReflector = mode1.timeDifferenceHalfNs;
      }
    }
  }

  return true;
}

bool parseAbortReasonNibble(uint8_t packed,
                            uint8_t* outProcedureAbortReason,
                            uint8_t* outSubeventAbortReason) {
  if (outProcedureAbortReason == nullptr || outSubeventAbortReason == nullptr) {
    return false;
  }
  *outProcedureAbortReason = static_cast<uint8_t>(packed & 0x0FU);
  *outSubeventAbortReason = static_cast<uint8_t>((packed >> 4U) & 0x0FU);
  return true;
}

bool rawBytesAllZero(const uint8_t* data, uint8_t len) {
  if (data == nullptr || len == 0U) {
    return true;
  }

  for (uint8_t i = 0U; i < len; ++i) {
    if (data[i] != 0U) {
      return false;
    }
  }
  return true;
}

int16_t clampSignedToBits(int16_t value, uint8_t bits) {
  if (bits == 0U || bits >= 15U) {
    return value;
  }

  const int16_t minValue = static_cast<int16_t>(-(1 << (bits - 1U)));
  const int16_t maxValue = static_cast<int16_t>((1 << (bits - 1U)) - 1);
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

uint32_t encodeSignedField(int16_t value,
                           uint8_t bits,
                           uint32_t mask,
                           uint8_t pos) {
  const int16_t clamped = clampSignedToBits(value, bits);
  const uint32_t fieldMask = mask >> pos;
  const uint32_t encoded =
      static_cast<uint32_t>(static_cast<uint16_t>(clamped)) & fieldMask;
  return (encoded << pos) & mask;
}

static const uint8_t kAntennaPathLut2[2][3] = {
    {kAntennaId1, kAntennaId2, kAntennaId2},
    {kAntennaId2, kAntennaId1, kAntennaId1},
};

static const uint8_t kAntennaPathLut3[6][4] = {
    {kAntennaId1, kAntennaId2, kAntennaId3, kAntennaId3},
    {kAntennaId2, kAntennaId1, kAntennaId3, kAntennaId3},
    {kAntennaId1, kAntennaId3, kAntennaId2, kAntennaId2},
    {kAntennaId3, kAntennaId1, kAntennaId2, kAntennaId2},
    {kAntennaId3, kAntennaId2, kAntennaId1, kAntennaId1},
    {kAntennaId2, kAntennaId3, kAntennaId1, kAntennaId1},
};

static const uint8_t kAntennaPathLut4[24][5] = {
    {kAntennaId1, kAntennaId2, kAntennaId3, kAntennaId4, kAntennaId4},
    {kAntennaId2, kAntennaId1, kAntennaId3, kAntennaId4, kAntennaId4},
    {kAntennaId1, kAntennaId3, kAntennaId2, kAntennaId4, kAntennaId4},
    {kAntennaId3, kAntennaId1, kAntennaId2, kAntennaId4, kAntennaId4},
    {kAntennaId3, kAntennaId2, kAntennaId1, kAntennaId4, kAntennaId4},
    {kAntennaId2, kAntennaId3, kAntennaId1, kAntennaId4, kAntennaId4},
    {kAntennaId1, kAntennaId2, kAntennaId4, kAntennaId3, kAntennaId3},
    {kAntennaId2, kAntennaId1, kAntennaId4, kAntennaId3, kAntennaId3},
    {kAntennaId1, kAntennaId4, kAntennaId2, kAntennaId3, kAntennaId3},
    {kAntennaId4, kAntennaId1, kAntennaId2, kAntennaId3, kAntennaId3},
    {kAntennaId4, kAntennaId2, kAntennaId1, kAntennaId3, kAntennaId3},
    {kAntennaId2, kAntennaId4, kAntennaId1, kAntennaId3, kAntennaId3},
    {kAntennaId1, kAntennaId4, kAntennaId3, kAntennaId2, kAntennaId2},
    {kAntennaId4, kAntennaId1, kAntennaId3, kAntennaId2, kAntennaId2},
    {kAntennaId1, kAntennaId3, kAntennaId4, kAntennaId2, kAntennaId2},
    {kAntennaId3, kAntennaId1, kAntennaId4, kAntennaId2, kAntennaId2},
    {kAntennaId3, kAntennaId4, kAntennaId1, kAntennaId2, kAntennaId2},
    {kAntennaId4, kAntennaId3, kAntennaId1, kAntennaId2, kAntennaId2},
    {kAntennaId4, kAntennaId2, kAntennaId3, kAntennaId1, kAntennaId1},
    {kAntennaId2, kAntennaId4, kAntennaId3, kAntennaId1, kAntennaId1},
    {kAntennaId4, kAntennaId3, kAntennaId2, kAntennaId1, kAntennaId1},
    {kAntennaId3, kAntennaId4, kAntennaId2, kAntennaId1, kAntennaId1},
    {kAntennaId3, kAntennaId2, kAntennaId4, kAntennaId1, kAntennaId1},
    {kAntennaId2, kAntennaId3, kAntennaId4, kAntennaId1, kAntennaId1},
};

}  // namespace

BleChannelSoundingRadio::BleChannelSoundingRadio(uint32_t radioBase)
    : radio_(reinterpret_cast<NRF_RADIO_Type*>(static_cast<uintptr_t>(radioBase))),
      power_(),
      config_(),
      initialized_(false),
      txPacket_{0},
      rxPacket_{0},
      dfePacket_{0},
      auxDataWords_{0},
      lastDfePacketAmountBytes_(0U),
      lastDfePacketCurrentAmountBytes_(0U),
      lastDfePacketAllZero_(true) {}

bool BleChannelSoundingRadio::begin(const BleCsConfig& config) {
  if (radio_ == nullptr) {
    initialized_ = false;
    return false;
  }
  if (!validLogicalChannel(config.controlChannel) || config.maxPayloadLength == 0U ||
      config.maxPayloadLength > 48U || config.cteTimeUnits < 2U ||
      config.cteTimeUnits > 10U ||
      config.dfeSwitchPatternCount > kBleCsMaxSwitchPatternCount) {
    initialized_ = false;
    return false;
  }

  config_ = config;

  if (!ClockControl::startHfxo(true, 1500000UL)) {
    initialized_ = false;
    return false;
  }

  power_.setLatencyMode(PowerLatencyMode::kConstantLatency);

  if (!configureBle2MCommon()) {
    initialized_ = false;
    return false;
  }

  resetDfeCaptureState();
  initialized_ = true;
  return true;
}

void BleChannelSoundingRadio::end() {
  if (radio_ == nullptr) {
    initialized_ = false;
    return;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  (void)waitForRadioDisabled(radio_, kRadioDisableBudgetUs);
  clearEvents();
  power_.setLatencyMode(PowerLatencyMode::kLowPower);
  initialized_ = false;
}

bool BleChannelSoundingRadio::initialized() const { return initialized_; }

const BleCsConfig& BleChannelSoundingRadio::config() const { return config_; }

BleCsIqSample BleChannelSoundingRadio::parsePctSample(const uint8_t pct[3]) {
  if (pct == nullptr) {
    return BleCsIqSample{};
  }

  const uint32_t packed = static_cast<uint32_t>(pct[0]) |
                          (static_cast<uint32_t>(pct[1]) << 8U) |
                          (static_cast<uint32_t>(pct[2]) << 16U);
  const uint16_t iBits = static_cast<uint16_t>(packed & 0x0FFFU);
  const uint16_t qBits = static_cast<uint16_t>((packed >> 12U) & 0x0FFFU);
  const int16_t i = static_cast<int16_t>((iBits ^ (1U << 11U)) - (1U << 11U));
  const int16_t q = static_cast<int16_t>((qBits ^ (1U << 11U)) - (1U << 11U));
  return BleCsIqSample{.i = i, .q = q};
}

void BleChannelSoundingRadio::fillValidChannelMap(uint8_t channelMap[kBleCsChannelMapBytes]) {
  if (channelMap == nullptr) {
    return;
  }

  memset(channelMap, 0xFF, kCsChmapLen);
  channelMapBitSetVal(channelMap, 0U, 0U);
  channelMapBitSetVal(channelMap, 1U, 0U);
  channelMapBitSetVal(channelMap, 23U, 0U);
  channelMapBitSetVal(channelMap, 24U, 0U);
  channelMapBitSetVal(channelMap, 25U, 0U);
  channelMapBitSetVal(channelMap, 77U, 0U);
  channelMapBitSetVal(channelMap, 78U, 0U);
  channelMapBitSetVal(channelMap, 79U, 0U);
}

bool BleChannelSoundingRadio::getAntennaPathPermutation(uint8_t antennaPathCount,
                                                        uint8_t permutationIndex,
                                                        uint8_t toneIndex,
                                                        uint8_t* outAntennaId) {
  if (outAntennaId == nullptr) {
    return false;
  }

  switch (antennaPathCount) {
    case 2U:
      if (permutationIndex >= 2U || toneIndex >= 3U) {
        return false;
      }
      *outAntennaId = kAntennaPathLut2[permutationIndex][toneIndex];
      return true;
    case 3U:
      if (permutationIndex >= 6U || toneIndex >= 4U) {
        return false;
      }
      *outAntennaId = kAntennaPathLut3[permutationIndex][toneIndex];
      return true;
    case 4U:
      if (permutationIndex >= 24U || toneIndex >= 5U) {
        return false;
      }
      *outAntennaId = kAntennaPathLut4[permutationIndex][toneIndex];
      return true;
    default:
      return false;
  }
}

bool BleChannelSoundingRadio::parseMode1StepData(const BleCsSubeventStep* step,
                                                 BleCsStepMode1Data* outData) {
  if (step == nullptr || outData == nullptr || step->data == nullptr ||
      step->dataLen < kBleCsMode1StepLen) {
    return false;
  }
  if (step->mode != kBleCsMainMode1 && step->mode != kBleCsMainMode3) {
    return false;
  }

  bool hasRttSoundingSequence = false;
  if (step->mode == kBleCsMainMode1) {
    if (step->dataLen == kBleCsMode1StepLen) {
      hasRttSoundingSequence = false;
    } else if (step->dataLen == kBleCsMode1SsRttStepLen) {
      hasRttSoundingSequence = true;
    } else {
      return false;
    }
  } else {
    if (step->dataLen >= kBleCsMode3SsRttStepBaseLen &&
        ((step->dataLen - kBleCsMode3SsRttStepBaseLen) % kBleCsToneInfoLen) == 0U) {
      hasRttSoundingSequence = true;
    } else if (step->dataLen >= kBleCsMode3StepBaseLen &&
               ((step->dataLen - kBleCsMode3StepBaseLen) % kBleCsToneInfoLen) == 0U) {
      hasRttSoundingSequence = false;
    } else {
      return false;
    }
  }

  const uint8_t quality = step->data[0];
  outData->aaCheckQuality = static_cast<uint8_t>(quality & 0x0FU);
  outData->bitErrors = static_cast<uint8_t>((quality >> 4U) & 0x0FU);
  outData->nadm = step->data[1];
  outData->packetRssiDbm = static_cast<int8_t>(step->data[2]);
  outData->timeDifferenceHalfNs = readLe16Signed(step->data + 3U);
  outData->packetAntenna = step->data[5];
  outData->hasRttSoundingSequence = hasRttSoundingSequence;
  outData->soundingPct1 = BleCsIqSample{};
  outData->soundingPct2 = BleCsIqSample{};
  if (hasRttSoundingSequence) {
    outData->soundingPct1 = parsePctSample(step->data + 6U);
    outData->soundingPct2 = parsePctSample(step->data + 10U);
  }
  return true;
}

bool BleChannelSoundingRadio::parseMode2StepData(const BleCsSubeventStep* step,
                                                 BleCsStepMode2Data* outData) {
  if (step == nullptr || outData == nullptr || step->data == nullptr ||
      step->dataLen < 1U || step->mode != kBleCsMainMode2) {
    return false;
  }
  const size_t toneBytes = static_cast<size_t>(step->dataLen - 1U);
  if ((toneBytes % kBleCsToneInfoLen) != 0U) {
    return false;
  }

  outData->antennaPermutationIndex = step->data[0];
  outData->toneCount = static_cast<uint8_t>(toneBytes / kBleCsToneInfoLen);
  return true;
}

bool BleChannelSoundingRadio::parseMode3StepData(const BleCsSubeventStep* step,
                                                 BleCsStepMode3Data* outData) {
  if (step == nullptr || outData == nullptr || step->data == nullptr ||
      step->mode != kBleCsMainMode3 || step->dataLen < kBleCsMode3StepBaseLen) {
    return false;
  }

  if (!parseMode1StepData(step, &outData->timing)) {
    return false;
  }
  const uint8_t toneDataOffset = outData->timing.hasRttSoundingSequence
                                     ? static_cast<uint8_t>(kBleCsMode3SsRttStepBaseLen)
                                     : static_cast<uint8_t>(kBleCsMode3StepBaseLen);
  outData->antennaPermutationIndex = step->data[toneDataOffset - 1U];
  outData->toneCount = static_cast<uint8_t>(
      (step->dataLen - toneDataOffset) / kBleCsToneInfoLen);
  outData->toneDataOffset = toneDataOffset;
  return true;
}

bool BleChannelSoundingRadio::parseToneInfo(const uint8_t* toneData,
                                            size_t toneDataLen,
                                            BleCsStepToneInfo* outInfo) {
  if (toneData == nullptr || outInfo == nullptr || toneDataLen < kBleCsToneInfoLen) {
    return false;
  }

  outInfo->pct = parsePctSample(toneData);
  const uint8_t info = toneData[3];
  outInfo->qualityIndicator = static_cast<uint8_t>(info & 0x0FU);
  outInfo->extensionIndicator = static_cast<uint8_t>((info >> 4U) & 0x0FU);
  return true;
}

bool BleChannelSoundingRadio::parseMode2ToneInfo(const BleCsSubeventStep* step,
                                                 uint8_t toneIndex,
                                                 BleCsStepToneInfo* outInfo) {
  BleCsStepMode2Data mode2{};
  if (!parseMode2StepData(step, &mode2) || outInfo == nullptr ||
      toneIndex >= mode2.toneCount) {
    return false;
  }

  const size_t offset = 1U + (static_cast<size_t>(toneIndex) * kBleCsToneInfoLen);
  return parseToneInfo(step->data + offset, static_cast<size_t>(step->dataLen) - offset,
                       outInfo);
}

bool BleChannelSoundingRadio::parseMode3ToneInfo(const BleCsSubeventStep* step,
                                                 uint8_t toneIndex,
                                                 BleCsStepToneInfo* outInfo) {
  BleCsStepMode3Data mode3{};
  if (!parseMode3StepData(step, &mode3) || outInfo == nullptr ||
      toneIndex >= mode3.toneCount) {
    return false;
  }

  const size_t offset =
      static_cast<size_t>(mode3.toneDataOffset) +
      (static_cast<size_t>(toneIndex) * kBleCsToneInfoLen);
  return parseToneInfo(step->data + offset, static_cast<size_t>(step->dataLen) - offset,
                       outInfo);
}

void BleChannelSoundingRadio::parseSubeventStepData(const uint8_t* stepData,
                                                    size_t stepDataLen,
                                                    bool (*callback)(
                                                        const BleCsSubeventStep* step,
                                                        void* userData),
                                                    void* userData) {
  if (stepData == nullptr || callback == nullptr) {
    return;
  }

  size_t offset = 0U;
  while ((stepDataLen - offset) >= kBleCsStepHeaderLen) {
    BleCsSubeventStep step{};
    step.mode = stepData[offset + 0U];
    step.channel = stepData[offset + 1U];
    step.dataLen = stepData[offset + 2U];
    offset += kBleCsStepHeaderLen;

    if (step.dataLen == 0U) {
      return;
    }
    if ((stepDataLen - offset) < step.dataLen) {
      return;
    }

    step.data = stepData + offset;
    if (!callback(&step, userData)) {
      return;
    }
    offset += step.dataLen;
  }
}

bool BleChannelSoundingRadio::parseHciSubeventResultEvent(
    const uint8_t* eventData, size_t eventLen, BleCsSubeventResult* outResult) {
  if (eventData == nullptr || outResult == nullptr ||
      eventLen < kBleCsHciSubeventResultHeaderLen) {
    return false;
  }

  BleCsSubeventResult result{};
  result.header.connHandle = readLe16(eventData + 0U);
  result.header.configId = eventData[2U];
  result.header.startAclConnEventCounter = readLe16(eventData + 3U);
  result.header.procedureCounter = readLe16(eventData + 5U);
  result.header.frequencyCompensation = readLe16(eventData + 7U);
  result.header.referencePowerLevelDbm = static_cast<int8_t>(eventData[9U]);
  result.header.procedureDoneStatus = eventData[10U];
  result.header.subeventDoneStatus = eventData[11U];
  if (!parseAbortReasonNibble(eventData[12U], &result.header.procedureAbortReason,
                              &result.header.subeventAbortReason)) {
    return false;
  }
  result.header.numAntennaPaths = eventData[13U];
  result.header.numStepsReported = eventData[14U];
  result.stepData = eventData + kBleCsHciSubeventResultHeaderLen;
  result.stepDataLen =
      static_cast<uint16_t>(eventLen - kBleCsHciSubeventResultHeaderLen);
  result.isPartial =
      (result.header.subeventDoneStatus == kBleCsSubeventDonePartial);
  result.isComplete = !result.isPartial;
  result.isContinuation = false;

  if (result.header.numStepsReported == 0U) {
    result.stepDataLen = 0U;
    result.stepData = nullptr;
  }

  *outResult = result;
  return true;
}

bool BleChannelSoundingRadio::parseHciSubeventResultContinueEvent(
    const uint8_t* eventData, size_t eventLen, BleCsSubeventResult* outResult) {
  if (eventData == nullptr || outResult == nullptr ||
      eventLen < kBleCsHciSubeventResultContinueHeaderLen) {
    return false;
  }

  BleCsSubeventResult result{};
  result.header.connHandle = readLe16(eventData + 0U);
  result.header.configId = eventData[2U];
  result.header.procedureDoneStatus = eventData[3U];
  result.header.subeventDoneStatus = eventData[4U];
  if (!parseAbortReasonNibble(eventData[5U], &result.header.procedureAbortReason,
                              &result.header.subeventAbortReason)) {
    return false;
  }
  result.header.numAntennaPaths = eventData[6U];
  result.header.numStepsReported = eventData[7U];
  result.stepData = eventData + kBleCsHciSubeventResultContinueHeaderLen;
  result.stepDataLen =
      static_cast<uint16_t>(eventLen - kBleCsHciSubeventResultContinueHeaderLen);
  result.isPartial =
      (result.header.subeventDoneStatus == kBleCsSubeventDonePartial);
  result.isComplete = !result.isPartial;
  result.isContinuation = true;

  if (result.header.numStepsReported == 0U) {
    result.stepDataLen = 0U;
    result.stepData = nullptr;
  }

  *outResult = result;
  return true;
}

bool BleChannelSoundingRadio::estimateDistanceFromStepBuffers(
    const uint8_t* localStepData, size_t localStepDataLen, const uint8_t* peerStepData,
    size_t peerStepDataLen, bool localRoleIsInitiator, BleCsEstimate* outEstimate) {
  if (localStepData == nullptr || peerStepData == nullptr || outEstimate == nullptr) {
    return false;
  }

  *outEstimate = BleCsEstimate{};
  outEstimate->phaseSlopeDistanceMeters = NAN;
  outEstimate->rttDistanceMeters = NAN;
  outEstimate->distanceMeters = NAN;

  BleCsControllerPhasePair phasePairs[kMaxCsControllerStepSamples] = {};
  BleCsControllerRttPair rttPairs[kMaxCsControllerStepSamples] = {};
  size_t phaseCount = 0U;
  size_t rttCount = 0U;

  BleCsControllerBufferParseContext localContext{};
  localContext.phasePairs = phasePairs;
  localContext.phaseCapacity = kMaxCsControllerStepSamples;
  localContext.phaseCount = &phaseCount;
  localContext.rttPairs = rttPairs;
  localContext.rttCapacity = kMaxCsControllerStepSamples;
  localContext.rttCount = &rttCount;
  localContext.fillingPeer = false;
  localContext.bufferRoleIsInitiator = localRoleIsInitiator;
  parseSubeventStepData(localStepData, localStepDataLen, parseControllerStepBufferCallback,
                        &localContext);

  BleCsControllerBufferParseContext peerContext{};
  peerContext.phasePairs = phasePairs;
  peerContext.phaseCapacity = kMaxCsControllerStepSamples;
  peerContext.phaseCount = &phaseCount;
  peerContext.rttPairs = rttPairs;
  peerContext.rttCapacity = kMaxCsControllerStepSamples;
  peerContext.rttCount = &rttCount;
  peerContext.fillingPeer = true;
  peerContext.bufferRoleIsInitiator = !localRoleIsInitiator;
  parseSubeventStepData(peerStepData, peerStepDataLen, parseControllerStepBufferCallback,
                        &peerContext);

  const bool phaseOk = controllerPhaseDistanceEstimate(phasePairs, phaseCount, outEstimate);
  const bool rttOk = controllerRttDistanceEstimate(rttPairs, rttCount, outEstimate);
  if (!phaseOk && !rttOk) {
    return false;
  }

  outEstimate->valid = true;
  if (rttOk && phaseOk) {
    const float delta =
        fabsf(outEstimate->rttDistanceMeters - outEstimate->phaseSlopeDistanceMeters);
    if (delta <= fmaxf(0.25f, sqrtf(outEstimate->rttVariance) + 0.20f)) {
      outEstimate->distanceMeters =
          (0.65f * outEstimate->rttDistanceMeters) +
          (0.35f * outEstimate->phaseSlopeDistanceMeters);
    } else {
      outEstimate->distanceMeters = outEstimate->rttDistanceMeters;
    }
  } else if (rttOk) {
    outEstimate->distanceMeters = outEstimate->rttDistanceMeters;
  } else {
    outEstimate->distanceMeters = outEstimate->phaseSlopeDistanceMeters;
  }

  return isfinite(outEstimate->distanceMeters) &&
         (outEstimate->distanceMeters > 0.0f);
}

bool BleChannelSoundingRadio::estimateDistanceFromSubeventResults(
    const BleCsSubeventResult& localResult, const BleCsSubeventResult& peerResult,
    bool localRoleIsInitiator, BleCsEstimate* outEstimate) {
  if (!localResult.isComplete || !peerResult.isComplete ||
      localResult.stepData == nullptr || peerResult.stepData == nullptr) {
    return false;
  }

  return estimateDistanceFromStepBuffers(localResult.stepData, localResult.stepDataLen,
                                         peerResult.stepData, peerResult.stepDataLen,
                                         localRoleIsInitiator, outEstimate);
}

float BleChannelSoundingRadio::applyCalibrationProfile(
    float meters, const BleCsCalibrationProfile& profile) {
  if (!isfinite(meters)) {
    return NAN;
  }

  const float calibrated = (meters * profile.scale) + profile.offsetMeters;
  return (calibrated >= 0.0f) ? calibrated : 0.0f;
}

bool BleChannelSoundingRadio::estimatePhysicalDistance(
    float meters, const BleCsCalibrationProfile& profile,
    BleCsPhysicalDistanceEstimate* outEstimate) {
  if (outEstimate == nullptr) {
    return false;
  }
  *outEstimate = BleCsPhysicalDistanceEstimate{};

  const float calibrated = applyCalibrationProfile(meters, profile);
  if (!isfinite(calibrated) || profile.validatedSampleCount == 0U) {
    return false;
  }

  float typicalError = profile.validatedMadMeters;
  if (!(isfinite(typicalError) && typicalError > 0.0f)) {
    typicalError = 0.0f;
  }
  float conservativeError = profile.validatedP90AbsErrorMeters;
  if (!(isfinite(conservativeError) && conservativeError > 0.0f)) {
    conservativeError = typicalError;
  }
  if (!(isfinite(conservativeError) && conservativeError > 0.0f)) {
    return false;
  }
  if (isfinite(profile.referenceDistanceMeters) && profile.referenceDistanceMeters > 0.0f) {
    const float referenceLower =
        profile.referenceDistanceMeters - conservativeError;
    const float referenceUpper =
        profile.referenceDistanceMeters + conservativeError;
    if (calibrated < ((referenceLower >= 0.0f) ? referenceLower : 0.0f) ||
        calibrated > referenceUpper) {
      return false;
    }
  }

  outEstimate->valid = true;
  outEstimate->distanceMeters = calibrated;
  outEstimate->typicalErrorMeters = typicalError;
  outEstimate->conservativeErrorMeters = conservativeError;
  const float lowerBound = calibrated - conservativeError;
  outEstimate->lowerBoundMeters = (lowerBound >= 0.0f) ? lowerBound : 0.0f;
  outEstimate->upperBoundMeters = calibrated + conservativeError;
  outEstimate->sampleCount = profile.validatedSampleCount;
  return true;
}

float BleChannelSoundingRadio::distanceMetersToEquivalentDelayNs(float meters) {
  static constexpr float kSpeedOfLightMetersPerSecond = 299792458.0f;
  return (meters / kSpeedOfLightMetersPerSecond) * 1.0e9f;
}

float BleChannelSoundingRadio::equivalentDelayNsToDistanceMeters(float delayNs) {
  static constexpr float kSpeedOfLightMetersPerSecond = 299792458.0f;
  return (delayNs * 1.0e-9f) * kSpeedOfLightMetersPerSecond;
}

bool BleChannelSoundingRadio::buildHciReadRemoteSupportedCapabilitiesCommand(
    uint16_t connHandle, BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpReadRemoteSupportedCapabilities;
  outCommand->payloadLen = 2U;
  writeLe16(outCommand->payload, connHandle);
  return true;
}

bool BleChannelSoundingRadio::buildHciSetDefaultSettingsCommand(
    uint16_t connHandle, const BleCsDefaultSettings& settings, BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpSetDefaultSettings;
  outCommand->payloadLen = 5U;
  writeLe16(outCommand->payload, connHandle);
  uint8_t roleEnable = 0U;
  if (settings.enableInitiatorRole) {
    roleEnable |= 0x01U;
  }
  if (settings.enableReflectorRole) {
    roleEnable |= 0x02U;
  }
  outCommand->payload[2U] = roleEnable;
  outCommand->payload[3U] = settings.csSyncAntennaSelection;
  outCommand->payload[4U] = static_cast<uint8_t>(settings.maxTxPowerDbm);
  return true;
}

bool BleChannelSoundingRadio::buildHciCreateConfigCommand(
    uint16_t connHandle, const BleCsControllerCreateConfig& config, BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpCreateConfig;
  outCommand->payloadLen = 22U;
  writeLe16(outCommand->payload + 0U, connHandle);
  outCommand->payload[2U] = config.configId;
  outCommand->payload[3U] = config.createContext;
  outCommand->payload[4U] = config.mainModeType;
  outCommand->payload[5U] = config.subModeType;
  outCommand->payload[6U] = config.minMainModeSteps;
  outCommand->payload[7U] = config.maxMainModeSteps;
  outCommand->payload[8U] = config.mainModeRepetition;
  outCommand->payload[9U] = config.mode0Steps;
  outCommand->payload[10U] = config.role;
  outCommand->payload[11U] = config.rttType;
  outCommand->payload[12U] = config.csSyncPhy;
  memcpy(outCommand->payload + 13U, config.channelMap, kBleCsChannelMapBytes);
  outCommand->payload[23U] = config.channelMapRepetition;
  outCommand->payload[24U] = config.channelSelectionType;
  outCommand->payload[25U] = config.ch3cShape;
  outCommand->payload[26U] = config.ch3cJump;
  outCommand->payload[27U] = config.csEnhancements1;
  outCommand->payloadLen = 28U;
  return true;
}

bool BleChannelSoundingRadio::buildHciRemoveConfigCommand(uint16_t connHandle,
                                                          uint8_t configId,
                                                          BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpRemoveConfig;
  outCommand->payloadLen = 3U;
  writeLe16(outCommand->payload + 0U, connHandle);
  outCommand->payload[2U] = configId;
  return true;
}

bool BleChannelSoundingRadio::buildHciSecurityEnableCommand(uint16_t connHandle,
                                                            BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpSecurityEnable;
  outCommand->payloadLen = 2U;
  writeLe16(outCommand->payload, connHandle);
  return true;
}

bool BleChannelSoundingRadio::buildHciSetProcedureParametersCommand(
    uint16_t connHandle, const BleCsProcedureParameters& params, BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpSetProcedureParameters;
  outCommand->payloadLen = 23U;
  writeLe16(outCommand->payload + 0U, connHandle);
  outCommand->payload[2U] = params.configId;
  writeLe16(outCommand->payload + 3U, params.maxProcedureLen);
  writeLe16(outCommand->payload + 5U, params.minProcedureInterval);
  writeLe16(outCommand->payload + 7U, params.maxProcedureInterval);
  writeLe16(outCommand->payload + 9U, params.maxProcedureCount);
  writeLe24(outCommand->payload + 11U, params.minSubeventLen);
  writeLe24(outCommand->payload + 14U, params.maxSubeventLen);
  outCommand->payload[17U] = params.toneAntennaConfigSelection;
  outCommand->payload[18U] = params.phy;
  outCommand->payload[19U] = static_cast<uint8_t>(params.txPowerDelta);
  outCommand->payload[20U] = params.preferredPeerAntenna;
  outCommand->payload[21U] = params.snrControlInitiator;
  outCommand->payload[22U] = params.snrControlReflector;
  return true;
}

bool BleChannelSoundingRadio::buildHciProcedureEnableCommand(
    uint16_t connHandle, const BleCsProcedureEnable& params, BleCsHciCommand* outCommand) {
  if (outCommand == nullptr) {
    return false;
  }
  *outCommand = BleCsHciCommand{};
  outCommand->opcode = kBleCsHciOpProcedureEnable;
  outCommand->payloadLen = 4U;
  writeLe16(outCommand->payload + 0U, connHandle);
  outCommand->payload[2U] = params.configId;
  outCommand->payload[3U] = params.enable;
  return true;
}

bool BleChannelSoundingRadio::encodeHciCommandPacket(const BleCsHciCommand& command,
                                                     uint8_t* outPacket,
                                                     size_t maxLen,
                                                     size_t* outLen) {
  if (outLen != nullptr) {
    *outLen = 0U;
  }
  if (outPacket == nullptr || maxLen < (4U + command.payloadLen)) {
    return false;
  }
  outPacket[0U] = kBleHciPacketTypeCommand;
  writeLe16(outPacket + 1U, command.opcode);
  outPacket[3U] = command.payloadLen;
  if (command.payloadLen > 0U) {
    memcpy(outPacket + 4U, command.payload, command.payloadLen);
  }
  if (outLen != nullptr) {
    *outLen = static_cast<size_t>(4U + command.payloadLen);
  }
  return true;
}

bool BleChannelSoundingRadio::parseHciCommandStatusEvent(
    const uint8_t* packet, size_t packetLen, BleCsHciCommandStatusEvent* outEvent) {
  uint8_t eventCode = 0U;
  const uint8_t* params = nullptr;
  size_t paramsLen = 0U;
  if (outEvent == nullptr ||
      !decodeHciEventFrame(packet, packetLen, &eventCode, &params, &paramsLen) ||
      eventCode != kBleHciEvtCommandStatus || paramsLen < 4U) {
    return false;
  }

  BleCsHciCommandStatusEvent evt{};
  evt.status = params[0U];
  evt.numCommandPackets = params[1U];
  evt.opcode = readLe16(params + 2U);
  *outEvent = evt;
  return true;
}

bool BleChannelSoundingRadio::parseHciCommandCompleteEvent(
    const uint8_t* packet, size_t packetLen, BleCsHciCommandCompleteEvent* outEvent) {
  uint8_t eventCode = 0U;
  const uint8_t* params = nullptr;
  size_t paramsLen = 0U;
  if (outEvent == nullptr ||
      !decodeHciEventFrame(packet, packetLen, &eventCode, &params, &paramsLen) ||
      eventCode != kBleHciEvtCommandComplete || paramsLen < 3U) {
    return false;
  }

  BleCsHciCommandCompleteEvent evt{};
  evt.numCommandPackets = params[0U];
  evt.opcode = readLe16(params + 1U);
  evt.returnParams = (paramsLen > 3U) ? (params + 3U) : nullptr;
  evt.returnParamsLen = static_cast<uint8_t>((paramsLen > 3U) ? (paramsLen - 3U) : 0U);
  evt.status = (evt.returnParamsLen > 0U) ? evt.returnParams[0U] : 0U;
  *outEvent = evt;
  return true;
}

bool BleChannelSoundingRadio::parseHciLeMetaEvent(const uint8_t* packet,
                                                  size_t packetLen,
                                                  BleCsHciLeMetaEvent* outEvent) {
  uint8_t eventCode = 0U;
  const uint8_t* params = nullptr;
  size_t paramsLen = 0U;
  if (outEvent == nullptr ||
      !decodeHciEventFrame(packet, packetLen, &eventCode, &params, &paramsLen) ||
      eventCode != kBleHciEvtLeMeta || paramsLen < 1U) {
    return false;
  }

  BleCsHciLeMetaEvent evt{};
  evt.subeventCode = params[0U];
  evt.payload = (paramsLen > 1U) ? (params + 1U) : nullptr;
  evt.payloadLen = static_cast<uint8_t>((paramsLen > 1U) ? (paramsLen - 1U) : 0U);
  *outEvent = evt;
  return true;
}

bool BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteEvent(
    const uint8_t* eventData, size_t eventLen, BleCsControllerCapabilities* outCapabilities) {
  if (eventData == nullptr || outCapabilities == nullptr ||
      eventLen < kBleCsHciReadRemoteCapsCompleteLen) {
    return false;
  }
  BleCsControllerCapabilities caps{};
  caps.isV2 = false;
  caps.status = eventData[0U];
  caps.connHandle = readLe16(eventData + 1U);
  caps.numConfigSupported = eventData[3U];
  caps.maxConsecutiveProceduresSupported = readLe16(eventData + 4U);
  caps.numAntennasSupported = eventData[6U];
  caps.maxAntennaPathsSupported = eventData[7U];
  const uint8_t roles = eventData[8U];
  const uint8_t modes = eventData[9U];
  caps.initiatorSupported = (roles & 0x01U) != 0U;
  caps.reflectorSupported = (roles & 0x02U) != 0U;
  caps.mode3Supported = (modes & 0x01U) != 0U;
  caps.rttCapability = eventData[10U];
  caps.rttAaOnlyN = eventData[11U];
  caps.rttSoundingN = eventData[12U];
  caps.rttRandomPayloadN = eventData[13U];
  caps.nadmSoundingCapability = readLe16(eventData + 14U);
  caps.nadmRandomCapability = readLe16(eventData + 16U);
  const uint8_t syncPhys = eventData[18U];
  caps.csSync2mPhySupported = (syncPhys & 0x02U) != 0U;
  caps.csSync2m2btPhySupported = (syncPhys & 0x04U) != 0U;
  const uint16_t subfeatures = readLe16(eventData + 19U);
  caps.csWithoutFaeSupported = (subfeatures & 0x0002U) != 0U;
  caps.chselAlg3cSupported = (subfeatures & 0x0004U) != 0U;
  caps.pbrFromRttSoundingSeqSupported = (subfeatures & 0x0008U) != 0U;
  caps.tIp1TimesSupported = readLe16(eventData + 21U);
  caps.tIp2TimesSupported = readLe16(eventData + 23U);
  caps.tFcsTimesSupported = readLe16(eventData + 25U);
  caps.tPmTimesSupported = readLe16(eventData + 27U);
  caps.valid = (caps.status == 0U);
  if (eventLen >= 30U) {
    caps.tSwTimeSupported = eventData[29U];
  }
  if (eventLen >= 31U) {
    caps.txSnrCapability = eventData[30U];
  }
  *outCapabilities = caps;
  return true;
}

bool BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteV2Event(
    const uint8_t* eventData, size_t eventLen, BleCsControllerCapabilities* outCapabilities) {
  if (eventData == nullptr || outCapabilities == nullptr ||
      eventLen < kBleCsHciReadRemoteCapsCompleteV2Len) {
    return false;
  }
  BleCsControllerCapabilities caps{};
  if (!parseHciRemoteSupportedCapabilitiesCompleteEvent(eventData, eventLen, &caps)) {
    return false;
  }
  caps.isV2 = true;
  caps.csIptReflectorSupported = (readLe16(eventData + 19U) & 0x0010U) != 0U;
  caps.tSwTimeSupported = eventData[29U];
  caps.txSnrCapability = eventData[30U];
  caps.tIp2IptTimesSupported = readLe16(eventData + 31U);
  if (eventLen >= 34U) {
    caps.tSwIptTimeSupported = eventData[33U];
  }
  *outCapabilities = caps;
  return true;
}

bool BleChannelSoundingRadio::parseHciSecurityEnableCompleteEvent(
    const uint8_t* eventData, size_t eventLen, BleCsSecurityEnableComplete* outEvent) {
  if (eventData == nullptr || outEvent == nullptr ||
      eventLen < kBleCsHciSecurityEnableCompleteLen) {
    return false;
  }
  BleCsSecurityEnableComplete evt{};
  evt.status = eventData[0U];
  evt.connHandle = readLe16(eventData + 1U);
  *outEvent = evt;
  return true;
}

bool BleChannelSoundingRadio::parseHciConfigCompleteEvent(const uint8_t* eventData,
                                                          size_t eventLen,
                                                          BleCsConfigComplete* outEvent) {
  if (eventData == nullptr || outEvent == nullptr ||
      eventLen < kBleCsHciConfigCompleteLen) {
    return false;
  }
  BleCsConfigComplete evt{};
  evt.status = eventData[0U];
  evt.connHandle = readLe16(eventData + 1U);
  evt.configId = eventData[3U];
  evt.action = eventData[4U];
  evt.mainModeType = eventData[5U];
  evt.subModeType = eventData[6U];
  evt.minMainModeSteps = eventData[7U];
  evt.maxMainModeSteps = eventData[8U];
  evt.mainModeRepetition = eventData[9U];
  evt.mode0Steps = eventData[10U];
  evt.role = eventData[11U];
  evt.rttType = eventData[12U];
  evt.csSyncPhy = eventData[13U];
  memcpy(evt.channelMap, eventData + 14U, kBleCsChannelMapBytes);
  evt.channelMapRepetition = eventData[24U];
  evt.channelSelectionType = eventData[25U];
  evt.ch3cShape = eventData[26U];
  evt.ch3cJump = eventData[27U];
  evt.csEnhancements1 = eventData[28U];
  if (eventLen >= 33U) {
    evt.tIp1TimeUs = eventData[29U];
    evt.tIp2TimeUs = eventData[30U];
    evt.tFcsTimeUs = eventData[31U];
    evt.tPmTimeUs = eventData[32U];
  }
  *outEvent = evt;
  return true;
}

bool BleChannelSoundingRadio::parseHciProcedureEnableCompleteEvent(
    const uint8_t* eventData, size_t eventLen, BleCsProcedureEnableComplete* outEvent) {
  if (eventData == nullptr || outEvent == nullptr ||
      eventLen < kBleCsHciProcedureEnableCompleteLen) {
    return false;
  }
  BleCsProcedureEnableComplete evt{};
  evt.status = eventData[0U];
  evt.connHandle = readLe16(eventData + 1U);
  evt.configId = eventData[3U];
  evt.state = eventData[4U];
  evt.toneAntennaConfigSelection = eventData[5U];
  evt.selectedTxPower = static_cast<int8_t>(eventData[6U]);
  evt.subeventLen = readLe24(eventData + 7U);
  evt.subeventsPerEvent = eventData[10U];
  evt.subeventInterval = readLe16(eventData + 11U);
  evt.eventInterval = readLe16(eventData + 13U);
  evt.procedureInterval = readLe16(eventData + 15U);
  if (eventLen >= 19U) {
    evt.procedureCount = readLe16(eventData + 17U);
  }
  if (eventLen >= 21U) {
    evt.maxProcedureLen = readLe16(eventData + 19U);
  }
  *outEvent = evt;
  return true;
}

BleCsSubeventResultReassembler::BleCsSubeventResultReassembler()
    : header_{}, stepData_{0}, stepDataLen_(0U), active_(false) {}

void BleCsSubeventResultReassembler::reset() {
  header_ = BleCsSubeventResultHeader{};
  memset(stepData_, 0, sizeof(stepData_));
  stepDataLen_ = 0U;
  active_ = false;
}

bool BleCsSubeventResultReassembler::active() const { return active_; }

bool BleCsSubeventResultReassembler::appendStepData(const uint8_t* data, size_t len) {
  if (len == 0U) {
    return true;
  }
  if (data == nullptr || (static_cast<size_t>(stepDataLen_) + len) > sizeof(stepData_)) {
    return false;
  }
  memcpy(stepData_ + stepDataLen_, data, len);
  stepDataLen_ = static_cast<uint16_t>(stepDataLen_ + len);
  return true;
}

void BleCsSubeventResultReassembler::fillOutput(bool complete,
                                                bool continuation,
                                                BleCsSubeventResult* outResult) const {
  if (outResult == nullptr) {
    return;
  }
  outResult->header = header_;
  outResult->stepData = (stepDataLen_ > 0U) ? stepData_ : nullptr;
  outResult->stepDataLen = stepDataLen_;
  outResult->isPartial = !complete;
  outResult->isComplete = complete;
  outResult->isContinuation = continuation;
}

bool BleCsSubeventResultReassembler::consumeInitialEvent(const uint8_t* eventData,
                                                         size_t eventLen,
                                                         BleCsSubeventResult* outResult) {
  BleCsSubeventResult parsed{};
  if (!BleChannelSoundingRadio::parseHciSubeventResultEvent(eventData, eventLen, &parsed)) {
    return false;
  }

  if (!parsed.isPartial) {
    reset();
    header_ = parsed.header;
    if (!appendStepData(parsed.stepData, parsed.stepDataLen)) {
      reset();
      return false;
    }
    active_ = false;
    fillOutput(true, false, outResult);
    return true;
  }

  if (parsed.header.procedureDoneStatus != kBleCsProcedureDonePartial ||
      parsed.header.numStepsReported == 0U || parsed.stepData == nullptr ||
      parsed.stepDataLen == 0U) {
    reset();
    return false;
  }

  reset();
  header_ = parsed.header;
  if (!appendStepData(parsed.stepData, parsed.stepDataLen)) {
    reset();
    return false;
  }
  active_ = true;
  fillOutput(false, false, outResult);
  return true;
}

bool BleCsSubeventResultReassembler::consumeContinuationEvent(const uint8_t* eventData,
                                                              size_t eventLen,
                                                              BleCsSubeventResult* outResult) {
  BleCsSubeventResult parsed{};
  if (!BleChannelSoundingRadio::parseHciSubeventResultContinueEvent(eventData, eventLen,
                                                                    &parsed)) {
    return false;
  }
  if (!active_ || parsed.header.connHandle != header_.connHandle ||
      parsed.header.configId != header_.configId) {
    return false;
  }
  if (parsed.header.numAntennaPaths != header_.numAntennaPaths) {
    return false;
  }
  if (!appendStepData(parsed.stepData, parsed.stepDataLen)) {
    reset();
    return false;
  }

  header_.procedureDoneStatus = parsed.header.procedureDoneStatus;
  header_.subeventDoneStatus = parsed.header.subeventDoneStatus;
  header_.procedureAbortReason = parsed.header.procedureAbortReason;
  header_.subeventAbortReason = parsed.header.subeventAbortReason;
  header_.numStepsReported = static_cast<uint16_t>(
      header_.numStepsReported + parsed.header.numStepsReported);

  const bool complete = !parsed.isPartial;
  fillOutput(complete, true, outResult);
  if (complete) {
    active_ = false;
  }
  return true;
}

BleCsControllerWorkflow::BleCsControllerWorkflow() : config_{}, state_{} {}

void BleCsControllerWorkflow::reset() {
  config_ = BleCsControllerWorkflowConfig{};
  state_ = BleCsControllerWorkflowState{};
}

bool BleCsControllerWorkflow::begin(uint16_t connHandle,
                                    const BleCsControllerWorkflowConfig& config) {
  reset();
  if (connHandle == 0U) {
    return false;
  }
  config_ = config;
  state_.connHandle = connHandle;
  state_.phase = BleCsControllerWorkflowPhase::kNeedReadRemoteCapabilities;
  return true;
}

bool BleCsControllerWorkflow::active() const {
  return state_.phase != BleCsControllerWorkflowPhase::kIdle &&
         state_.phase != BleCsControllerWorkflowPhase::kReady &&
         state_.phase != BleCsControllerWorkflowPhase::kFailed;
}

bool BleCsControllerWorkflow::ready() const {
  return state_.phase == BleCsControllerWorkflowPhase::kReady;
}

bool BleCsControllerWorkflow::failed() const {
  return state_.phase == BleCsControllerWorkflowPhase::kFailed;
}

BleCsControllerWorkflowPhase BleCsControllerWorkflow::phase() const {
  return state_.phase;
}

const BleCsControllerWorkflowState& BleCsControllerWorkflow::state() const {
  return state_;
}

const BleCsControllerWorkflowConfig& BleCsControllerWorkflow::config() const {
  return config_;
}

bool BleCsControllerWorkflow::buildNextCommand(BleCsHciCommand* outCommand) {
  if (outCommand == nullptr || failed() || ready() || state_.connHandle == 0U) {
    return false;
  }

  switch (state_.phase) {
    case BleCsControllerWorkflowPhase::kNeedReadRemoteCapabilities:
      if (!BleChannelSoundingRadio::buildHciReadRemoteSupportedCapabilitiesCommand(
              state_.connHandle, outCommand)) {
        return false;
      }
      state_.lastOpcode = outCommand->opcode;
      state_.phase = BleCsControllerWorkflowPhase::kWaitingRemoteCapabilities;
      return true;

    case BleCsControllerWorkflowPhase::kNeedSetDefaultSettings:
      if (!BleChannelSoundingRadio::buildHciSetDefaultSettingsCommand(
              state_.connHandle, config_.defaultSettings, outCommand)) {
        return false;
      }
      state_.lastOpcode = outCommand->opcode;
      state_.phase = BleCsControllerWorkflowPhase::kWaitingSetDefaultSettings;
      return true;

    case BleCsControllerWorkflowPhase::kNeedCreateConfig:
      if (!BleChannelSoundingRadio::buildHciCreateConfigCommand(
              state_.connHandle, config_.createConfig, outCommand)) {
        return false;
      }
      state_.lastOpcode = outCommand->opcode;
      state_.phase = BleCsControllerWorkflowPhase::kWaitingConfigComplete;
      return true;

    case BleCsControllerWorkflowPhase::kNeedSecurityEnable:
      if (!BleChannelSoundingRadio::buildHciSecurityEnableCommand(
              state_.connHandle, outCommand)) {
        return false;
      }
      state_.lastOpcode = outCommand->opcode;
      state_.phase = BleCsControllerWorkflowPhase::kWaitingSecurityEnableComplete;
      return true;

    case BleCsControllerWorkflowPhase::kNeedSetProcedureParameters:
      if (!BleChannelSoundingRadio::buildHciSetProcedureParametersCommand(
              state_.connHandle, config_.procedureParameters, outCommand)) {
        return false;
      }
      state_.lastOpcode = outCommand->opcode;
      state_.phase = BleCsControllerWorkflowPhase::kWaitingSetProcedureParameters;
      return true;

    case BleCsControllerWorkflowPhase::kNeedProcedureEnable:
      if (!BleChannelSoundingRadio::buildHciProcedureEnableCommand(
              state_.connHandle, config_.procedureEnable, outCommand)) {
        return false;
      }
      state_.lastOpcode = outCommand->opcode;
      state_.phase = BleCsControllerWorkflowPhase::kWaitingProcedureEnableComplete;
      return true;

    default:
      return false;
  }
}

bool BleCsControllerWorkflow::acknowledgeCommandStatus(uint16_t opcode, uint8_t status) {
  if (failed() || ready() || opcode != state_.lastOpcode) {
    return false;
  }

  state_.lastStatus = status;
  if (status != 0U) {
    fail(status);
    return true;
  }

  switch (state_.phase) {
    case BleCsControllerWorkflowPhase::kWaitingSetDefaultSettings:
      state_.defaultSettingsApplied = true;
      state_.phase = BleCsControllerWorkflowPhase::kNeedCreateConfig;
      return true;

    case BleCsControllerWorkflowPhase::kWaitingSetProcedureParameters:
      state_.procedureParametersApplied = true;
      state_.phase = BleCsControllerWorkflowPhase::kNeedProcedureEnable;
      return true;

    case BleCsControllerWorkflowPhase::kWaitingRemoteCapabilities:
    case BleCsControllerWorkflowPhase::kWaitingConfigComplete:
    case BleCsControllerWorkflowPhase::kWaitingSecurityEnableComplete:
    case BleCsControllerWorkflowPhase::kWaitingProcedureEnableComplete:
      return true;

    default:
      return false;
  }
}

bool BleCsControllerWorkflow::acknowledgeReadyCommandStatus(uint16_t opcode, uint8_t status) {
  if (failed() || !ready()) {
    return false;
  }

  state_.lastOpcode = opcode;
  state_.lastStatus = status;

  switch (opcode) {
    case kBleCsHciOpReadRemoteSupportedCapabilities:
    case kBleCsHciOpCreateConfig:
    case kBleCsHciOpRemoveConfig:
    case kBleCsHciOpSecurityEnable:
    case kBleCsHciOpProcedureEnable:
      return true;

    case kBleCsHciOpSetDefaultSettings:
      if (status == 0U) {
        state_.defaultSettingsApplied = true;
      }
      return true;

    case kBleCsHciOpSetProcedureParameters:
      if (status == 0U) {
        state_.procedureParametersApplied = true;
      }
      return true;

    default:
      return false;
  }
}

bool BleCsControllerWorkflow::consumeEvent(uint8_t subeventCode,
                                           const uint8_t* eventData,
                                           size_t eventLen) {
  if (eventData == nullptr || failed() || ready()) {
    return false;
  }

  switch (subeventCode) {
    case kBleCsHciEvtReadRemoteSupportedCapabilitiesComplete: {
      BleCsControllerCapabilities capabilities{};
      if (state_.phase != BleCsControllerWorkflowPhase::kWaitingRemoteCapabilities ||
          !BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteEvent(
              eventData, eventLen, &capabilities) ||
          capabilities.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = capabilities.status;
      if (capabilities.status != 0U) {
        fail(capabilities.status);
        return true;
      }
      if (!validateConfigAgainstCapabilities(config_, capabilities)) {
        fail(0x12U);
        return true;
      }
      state_.remoteCapabilities = capabilities;
      state_.remoteCapabilitiesValid = capabilities.valid;
      state_.phase = config_.applyDefaultSettings
                         ? BleCsControllerWorkflowPhase::kNeedSetDefaultSettings
                         : BleCsControllerWorkflowPhase::kNeedCreateConfig;
      return true;
    }

    case kBleCsHciEvtReadRemoteSupportedCapabilitiesCompleteV2: {
      BleCsControllerCapabilities capabilities{};
      if (state_.phase != BleCsControllerWorkflowPhase::kWaitingRemoteCapabilities ||
          !BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteV2Event(
              eventData, eventLen, &capabilities) ||
          capabilities.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = capabilities.status;
      if (capabilities.status != 0U) {
        fail(capabilities.status);
        return true;
      }
      if (!validateConfigAgainstCapabilities(config_, capabilities)) {
        fail(0x12U);
        return true;
      }
      state_.remoteCapabilities = capabilities;
      state_.remoteCapabilitiesValid = capabilities.valid;
      state_.phase = config_.applyDefaultSettings
                         ? BleCsControllerWorkflowPhase::kNeedSetDefaultSettings
                         : BleCsControllerWorkflowPhase::kNeedCreateConfig;
      return true;
    }

    case kBleCsHciEvtConfigComplete: {
      BleCsConfigComplete complete{};
      if (state_.phase != BleCsControllerWorkflowPhase::kWaitingConfigComplete ||
          !BleChannelSoundingRadio::parseHciConfigCompleteEvent(eventData, eventLen,
                                                                &complete) ||
          complete.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = complete.status;
      state_.configComplete = complete;
      if (complete.status != 0U) {
        fail(complete.status);
        return true;
      }
      state_.configCreated = (complete.action != 0U);
      state_.phase = config_.requireSecurityEnable
                         ? BleCsControllerWorkflowPhase::kNeedSecurityEnable
                         : BleCsControllerWorkflowPhase::kNeedSetProcedureParameters;
      return true;
    }

    case kBleCsHciEvtSecurityEnableComplete: {
      BleCsSecurityEnableComplete complete{};
      if (state_.phase != BleCsControllerWorkflowPhase::kWaitingSecurityEnableComplete ||
          !BleChannelSoundingRadio::parseHciSecurityEnableCompleteEvent(eventData, eventLen,
                                                                        &complete) ||
          complete.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = complete.status;
      if (complete.status != 0U) {
        fail(complete.status);
        return true;
      }
      state_.securityEnabled = true;
      state_.phase = BleCsControllerWorkflowPhase::kNeedSetProcedureParameters;
      return true;
    }

    case kBleCsHciEvtProcedureEnableComplete: {
      BleCsProcedureEnableComplete complete{};
      if (state_.phase != BleCsControllerWorkflowPhase::kWaitingProcedureEnableComplete ||
          !BleChannelSoundingRadio::parseHciProcedureEnableCompleteEvent(eventData, eventLen,
                                                                         &complete) ||
          complete.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = complete.status;
      state_.procedureEnableComplete = complete;
      if (complete.status != 0U) {
        fail(complete.status);
        return true;
      }
      state_.procedureEnabled = (complete.state != 0U);
      state_.phase = BleCsControllerWorkflowPhase::kReady;
      return true;
    }

    default:
      return false;
  }
}

bool BleCsControllerWorkflow::consumeReadyEvent(uint8_t subeventCode,
                                                const uint8_t* eventData,
                                                size_t eventLen) {
  if (eventData == nullptr || failed() || !ready()) {
    return false;
  }

  switch (subeventCode) {
    case kBleCsHciEvtReadRemoteSupportedCapabilitiesComplete: {
      BleCsControllerCapabilities capabilities{};
      if (!BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteEvent(
              eventData, eventLen, &capabilities) ||
          capabilities.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = capabilities.status;
      if (capabilities.status == 0U) {
        state_.remoteCapabilities = capabilities;
        state_.remoteCapabilitiesValid = capabilities.valid;
      }
      return true;
    }

    case kBleCsHciEvtReadRemoteSupportedCapabilitiesCompleteV2: {
      BleCsControllerCapabilities capabilities{};
      if (!BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteV2Event(
              eventData, eventLen, &capabilities) ||
          capabilities.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = capabilities.status;
      if (capabilities.status == 0U) {
        state_.remoteCapabilities = capabilities;
        state_.remoteCapabilitiesValid = capabilities.valid;
      }
      return true;
    }

    case kBleCsHciEvtConfigComplete: {
      BleCsConfigComplete complete{};
      if (!BleChannelSoundingRadio::parseHciConfigCompleteEvent(eventData, eventLen,
                                                                &complete) ||
          complete.connHandle != state_.connHandle) {
        return false;
      }
      const BleCsConfigComplete previousConfigComplete = state_.configComplete;
      const BleCsProcedureEnableComplete previousProcedureEnable =
          state_.procedureEnableComplete;
      state_.lastStatus = complete.status;
      state_.configComplete = complete;
      if (complete.status != 0U) {
        return true;
      }
      const bool removedActiveConfig =
          complete.action == 0U &&
          ((previousProcedureEnable.configId != 0U &&
            previousProcedureEnable.configId == complete.configId) ||
           (previousProcedureEnable.configId == 0U &&
            previousConfigComplete.action != 0U &&
            previousConfigComplete.configId == complete.configId));
      if (complete.action != 0U) {
        state_.configCreated = true;
      } else if (removedActiveConfig) {
        state_.configCreated = false;
        state_.remoteCapabilities = BleCsControllerCapabilities{};
        state_.remoteCapabilitiesValid = false;
        state_.defaultSettingsApplied = false;
        state_.securityEnabled = false;
      }
      if (complete.action != 0U || removedActiveConfig) {
        state_.procedureParametersApplied = false;
        state_.procedureEnabled = false;
        state_.procedureEnableComplete = BleCsProcedureEnableComplete{};
        state_.procedureEnableComplete.connHandle = complete.connHandle;
        state_.procedureEnableComplete.configId = complete.configId;
      }
      return true;
    }

    case kBleCsHciEvtSecurityEnableComplete: {
      BleCsSecurityEnableComplete complete{};
      if (!BleChannelSoundingRadio::parseHciSecurityEnableCompleteEvent(eventData, eventLen,
                                                                        &complete) ||
          complete.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = complete.status;
      if (complete.status == 0U) {
        state_.securityEnabled = true;
      }
      return true;
    }

    case kBleCsHciEvtProcedureEnableComplete: {
      BleCsProcedureEnableComplete complete{};
      if (!BleChannelSoundingRadio::parseHciProcedureEnableCompleteEvent(eventData, eventLen,
                                                                         &complete) ||
          complete.connHandle != state_.connHandle) {
        return false;
      }
      state_.lastStatus = complete.status;
      state_.procedureEnableComplete = complete;
      if (complete.status == 0U) {
        state_.procedureEnabled = (complete.state != 0U);
        if (complete.state != 0U) {
          state_.procedureParametersApplied = true;
        }
      }
      return true;
    }

    default:
      return false;
  }
}

bool BleCsControllerWorkflow::consumeHciEventPacket(const uint8_t* packet, size_t packetLen) {
  BleCsHciCommandStatusEvent statusEvent{};
  if (BleChannelSoundingRadio::parseHciCommandStatusEvent(packet, packetLen, &statusEvent)) {
    return ready() ? acknowledgeReadyCommandStatus(statusEvent.opcode, statusEvent.status)
                   : acknowledgeCommandStatus(statusEvent.opcode, statusEvent.status);
  }

  BleCsHciCommandCompleteEvent completeEvent{};
  if (BleChannelSoundingRadio::parseHciCommandCompleteEvent(packet, packetLen, &completeEvent)) {
    return ready() ? acknowledgeReadyCommandStatus(completeEvent.opcode, completeEvent.status)
                   : acknowledgeCommandStatus(completeEvent.opcode, completeEvent.status);
  }

  BleCsHciLeMetaEvent metaEvent{};
  if (BleChannelSoundingRadio::parseHciLeMetaEvent(packet, packetLen, &metaEvent)) {
    return ready() ? consumeReadyEvent(metaEvent.subeventCode, metaEvent.payload,
                                       metaEvent.payloadLen)
                   : consumeEvent(metaEvent.subeventCode, metaEvent.payload,
                                  metaEvent.payloadLen);
  }

  return false;
}

void BleCsControllerWorkflow::reconcileReadyShadowState(uint8_t selectedConfigId,
                                                        bool sessionOpen,
                                                        bool configCreated,
                                                        bool securityEnabled,
                                                        bool procedureParametersApplied,
                                                        bool procedureEnabled) {
  if (!ready()) {
    return;
  }

  if (!sessionOpen) {
    state_.remoteCapabilities = BleCsControllerCapabilities{};
    state_.remoteCapabilitiesValid = false;
    state_.defaultSettingsApplied = false;
    state_.configCreated = false;
    state_.securityEnabled = false;
    state_.procedureParametersApplied = false;
    state_.procedureEnabled = false;
    state_.procedureEnableComplete = BleCsProcedureEnableComplete{};
    state_.procedureEnableComplete.connHandle = state_.connHandle;
    return;
  }

  const bool recoveredSelectedConfig = !state_.configCreated && configCreated;
  if (recoveredSelectedConfig) {
    state_.remoteCapabilitiesValid = true;
    state_.defaultSettingsApplied = true;
    state_.configCreated = true;
    state_.securityEnabled = securityEnabled;
    state_.procedureParametersApplied = procedureParametersApplied;
    if (selectedConfigId != 0U) {
      state_.configComplete.connHandle = state_.connHandle;
      state_.configComplete.status = 0U;
      state_.configComplete.action = 1U;
      state_.configComplete.configId = selectedConfigId;
    }
  }

  if (state_.configCreated && configCreated && selectedConfigId != 0U &&
      state_.configComplete.action != 0U) {
    state_.configComplete.connHandle = state_.connHandle;
    state_.configComplete.status = 0U;
    state_.configComplete.configId = selectedConfigId;
  }

  if (state_.procedureEnabled != procedureEnabled) {
    state_.procedureEnabled = procedureEnabled;
    if (selectedConfigId != 0U) {
      state_.procedureEnableComplete.connHandle = state_.connHandle;
      state_.procedureEnableComplete.configId = selectedConfigId;
      state_.procedureEnableComplete.state = procedureEnabled ? 1U : 0U;
    }
  }
}

const char* BleCsControllerWorkflow::phaseName(BleCsControllerWorkflowPhase phase) {
  switch (phase) {
    case BleCsControllerWorkflowPhase::kIdle:
      return "idle";
    case BleCsControllerWorkflowPhase::kNeedReadRemoteCapabilities:
      return "need_read_remote_caps";
    case BleCsControllerWorkflowPhase::kWaitingRemoteCapabilities:
      return "waiting_remote_caps";
    case BleCsControllerWorkflowPhase::kNeedSetDefaultSettings:
      return "need_set_defaults";
    case BleCsControllerWorkflowPhase::kWaitingSetDefaultSettings:
      return "waiting_set_defaults";
    case BleCsControllerWorkflowPhase::kNeedCreateConfig:
      return "need_create_config";
    case BleCsControllerWorkflowPhase::kWaitingConfigComplete:
      return "waiting_config_complete";
    case BleCsControllerWorkflowPhase::kNeedSecurityEnable:
      return "need_security_enable";
    case BleCsControllerWorkflowPhase::kWaitingSecurityEnableComplete:
      return "waiting_security_enable";
    case BleCsControllerWorkflowPhase::kNeedSetProcedureParameters:
      return "need_set_procedure_params";
    case BleCsControllerWorkflowPhase::kWaitingSetProcedureParameters:
      return "waiting_set_procedure_params";
    case BleCsControllerWorkflowPhase::kNeedProcedureEnable:
      return "need_procedure_enable";
    case BleCsControllerWorkflowPhase::kWaitingProcedureEnableComplete:
      return "waiting_procedure_enable";
    case BleCsControllerWorkflowPhase::kReady:
      return "ready";
    case BleCsControllerWorkflowPhase::kFailed:
      return "failed";
    default:
      return "unknown";
  }
}

bool BleCsControllerWorkflow::validateConfigAgainstCapabilities(
    const BleCsControllerWorkflowConfig& config,
    const BleCsControllerCapabilities& capabilities) {
  if (!capabilities.valid) {
    return false;
  }

  if ((config.createConfig.mainModeType == kBleCsMainMode3 ||
       config.createConfig.subModeType == kBleCsMainMode3) &&
      !capabilities.mode3Supported) {
    return false;
  }

  if (config.createConfig.role == 0U && !capabilities.initiatorSupported) {
    return false;
  }
  if (config.createConfig.role == 1U && !capabilities.reflectorSupported) {
    return false;
  }

  if (config.createConfig.csSyncPhy == 2U && !capabilities.csSync2mPhySupported) {
    return false;
  }
  if (config.createConfig.csSyncPhy == 3U && !capabilities.csSync2m2btPhySupported) {
    return false;
  }

  if (config.createConfig.rttType == 1U || config.createConfig.rttType == 2U) {
    if (capabilities.rttSoundingN == 0U) {
      return false;
    }
  } else if (config.createConfig.rttType >= 3U) {
    if (capabilities.rttRandomPayloadN == 0U) {
      return false;
    }
  }

  if (config.createConfig.channelSelectionType == 1U &&
      !capabilities.chselAlg3cSupported) {
    return false;
  }

  return true;
}

void BleCsControllerWorkflow::fail(uint8_t status) {
  state_.lastStatus = status;
  state_.phase = BleCsControllerWorkflowPhase::kFailed;
}

BleHciPacketStreamDecoder::BleHciPacketStreamDecoder()
    : buffer_{0},
      used_(0U),
      expected_(0U),
      acceptedTypes_(packetTypeMask(kBleHciPacketTypeCommand) |
                     packetTypeMask(kBleHciPacketTypeEvent)),
      deliveredPackets_(0U),
      ignoredPackets_(0U),
      ignoredBytes_(0U) {}

void BleHciPacketStreamDecoder::reset() {
  resetBuffer();
  deliveredPackets_ = 0U;
  ignoredPackets_ = 0U;
  ignoredBytes_ = 0U;
}

void BleHciPacketStreamDecoder::setAcceptedPacketTypes(uint32_t acceptedTypes) {
  acceptedTypes_ = acceptedTypes;
}

uint32_t BleHciPacketStreamDecoder::acceptedPacketTypes() const { return acceptedTypes_; }

uint32_t BleHciPacketStreamDecoder::deliveredPacketCount() const { return deliveredPackets_; }

uint32_t BleHciPacketStreamDecoder::ignoredPacketCount() const { return ignoredPackets_; }

uint32_t BleHciPacketStreamDecoder::ignoredByteCount() const { return ignoredBytes_; }

uint32_t BleHciPacketStreamDecoder::packetTypeMask(uint8_t packetType) {
  return (packetType < 32U) ? (1UL << packetType) : 0UL;
}

bool BleHciPacketStreamDecoder::pushBytes(
    const uint8_t* data, size_t len,
    bool (*onPacket)(const uint8_t* packet, size_t packetLen, void* userData),
    void* userData) {
  if ((len > 0U && data == nullptr) || onPacket == nullptr) {
    return false;
  }

  for (size_t i = 0U; i < len; ++i) {
    if (used_ >= sizeof(buffer_)) {
      resetBuffer();
      return false;
    }
    buffer_[used_++] = data[i];
    if (!determineExpectedLength()) {
      resetBuffer();
      return false;
    }
    if (expectedLengthKnown() && used_ == expected_) {
      if (!packetTypeAccepted()) {
        ++ignoredPackets_;
        ignoredBytes_ = static_cast<uint32_t>(ignoredBytes_ + used_);
        resetBuffer();
        continue;
      }
      const bool ok = onPacket(buffer_, used_, userData);
      resetBuffer();
      if (!ok) {
        return false;
      }
      ++deliveredPackets_;
    } else if (expectedLengthKnown() && used_ > expected_) {
      resetBuffer();
      return false;
    }
  }
  return true;
}

bool BleHciPacketStreamDecoder::expectedLengthKnown() const { return expected_ > 0U; }

bool BleHciPacketStreamDecoder::determineExpectedLength() {
  if (expected_ > 0U || used_ == 0U) {
    return true;
  }

  switch (buffer_[0U]) {
    case kBleHciPacketTypeEvent:
      if (used_ >= 3U) {
        expected_ = static_cast<size_t>(3U + buffer_[2U]);
      }
      return true;

    case kBleHciPacketTypeCommand:
      if (used_ >= 4U) {
        expected_ = static_cast<size_t>(4U + buffer_[3U]);
      }
      return true;

    case kBleHciPacketTypeAcl:
      if (used_ >= 5U) {
        expected_ = static_cast<size_t>(5U + readLe16(buffer_ + 3U));
      }
      return true;

    case kBleHciPacketTypeSco:
      if (used_ >= 4U) {
        expected_ = static_cast<size_t>(4U + buffer_[3U]);
      }
      return true;

    case kBleHciPacketTypeIso:
      if (used_ >= 5U) {
        expected_ = static_cast<size_t>(5U + (readLe16(buffer_ + 3U) & 0x3FFFU));
      }
      return true;

    default:
      return false;
  }
}

bool BleHciPacketStreamDecoder::packetTypeAccepted() const {
  if (used_ == 0U) {
    return false;
  }
  return (acceptedTypes_ & packetTypeMask(buffer_[0U])) != 0U;
}

void BleHciPacketStreamDecoder::resetBuffer() {
  used_ = 0U;
  expected_ = 0U;
}

BleCsControllerSession::BleCsControllerSession()
    : config_{},
      state_{},
      workflow_{},
      workflowDecoder_{},
      localDecoder_{},
      peerDecoder_{},
      localReassembler_{},
      peerReassembler_{},
      localResult_{},
      peerResult_{},
      accumulatedLocalResult_{},
      accumulatedPeerResult_{},
      completedLocalResult_{},
      completedPeerResult_{},
      accumulatedLocalStepData_{0},
      accumulatedPeerStepData_{0},
      completedLocalStepData_{0},
      completedPeerStepData_{0} {}

void BleCsControllerSession::reset() {
  config_ = BleCsControllerSessionConfig{};
  state_ = BleCsControllerSessionState{};
  workflow_.reset();
  workflowDecoder_.reset();
  localDecoder_.reset();
  peerDecoder_.reset();
  workflowDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  localDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  peerDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  localReassembler_.reset();
  peerReassembler_.reset();
  localResult_ = BleCsSubeventResult{};
  peerResult_ = BleCsSubeventResult{};
  accumulatedLocalResult_ = BleCsSubeventResult{};
  accumulatedPeerResult_ = BleCsSubeventResult{};
  completedLocalResult_ = BleCsSubeventResult{};
  completedPeerResult_ = BleCsSubeventResult{};
}

bool parseDirectStatusResponse(const uint8_t* packet,
                               size_t packetLen,
                               uint16_t expectedOpcode,
                               uint8_t* outStatus) {
  if (outStatus == nullptr) {
    return false;
  }
  BleCsHciCommandStatusEvent statusEvent{};
  if (BleChannelSoundingRadio::parseHciCommandStatusEvent(packet, packetLen, &statusEvent) &&
      statusEvent.opcode == expectedOpcode) {
    *outStatus = statusEvent.status;
    return true;
  }

  BleCsHciCommandCompleteEvent completeEvent{};
  if (BleChannelSoundingRadio::parseHciCommandCompleteEvent(packet, packetLen,
                                                            &completeEvent) &&
      completeEvent.opcode == expectedOpcode) {
    *outStatus = completeEvent.status;
    return true;
  }

  return false;
}

bool BleCsControllerSession::begin(uint16_t connHandle,
                                   const BleCsControllerSessionConfig& config) {
  reset();
  config_ = config;
  return workflow_.begin(connHandle, config.workflow);
}

bool BleCsControllerSession::buildNextCommandPacket(uint8_t* outPacket,
                                                    size_t maxLen,
                                                    size_t* outLen) {
  BleCsHciCommand command{};
  if (!workflow_.buildNextCommand(&command)) {
    return false;
  }
  return BleChannelSoundingRadio::encodeHciCommandPacket(command, outPacket, maxLen, outLen);
}

bool BleCsControllerSession::consumeWorkflowEventPacket(const uint8_t* packet, size_t packetLen) {
  const bool ok = workflow_.consumeHciEventPacket(packet, packetLen);
  if (!ok) {
    return false;
  }
  state_.workflowReady = workflow_.ready();
  return true;
}

bool BleCsControllerSession::consumeWorkflowStreamBytes(const uint8_t* data, size_t len) {
  const bool ok = workflowDecoder_.pushBytes(data, len, onWorkflowPacket, this);
  state_.workflowIgnoredPackets = workflowDecoder_.ignoredPacketCount();
  state_.workflowIgnoredBytes = workflowDecoder_.ignoredByteCount();
  return ok;
}

bool BleCsControllerSession::consumeResultEventPacket(BleCsControllerResultSource source,
                                                      const uint8_t* packet,
                                                      size_t packetLen) {
  return consumeResultPacket(source, packet, packetLen);
}

bool BleCsControllerSession::consumeResultStreamBytes(BleCsControllerResultSource source,
                                                      const uint8_t* data,
                                                      size_t len) {
  BleHciPacketStreamDecoder& decoder =
      (source == BleCsControllerResultSource::kLocal) ? localDecoder_ : peerDecoder_;
  const bool ok = decoder
      .pushBytes(data, len,
                 (source == BleCsControllerResultSource::kLocal) ? onLocalResultPacket
                                                                 : onPeerResultPacket,
                 this);
  if (source == BleCsControllerResultSource::kLocal) {
    state_.localIgnoredPackets = decoder.ignoredPacketCount();
    state_.localIgnoredBytes = decoder.ignoredByteCount();
  } else {
    state_.peerIgnoredPackets = decoder.ignoredPacketCount();
    state_.peerIgnoredBytes = decoder.ignoredByteCount();
  }
  return ok;
}

bool BleCsControllerSession::ready() const { return workflow_.ready(); }

bool BleCsControllerSession::failed() const { return workflow_.failed(); }

bool BleCsControllerSession::estimateValid() const { return state_.estimateValid; }

const BleCsControllerSessionState& BleCsControllerSession::state() const { return state_; }

const BleCsControllerWorkflowState& BleCsControllerSession::workflowState() const {
  return workflow_.state();
}

const BleCsSubeventResult& BleCsControllerSession::localResult() const {
  return localResult_;
}

const BleCsSubeventResult& BleCsControllerSession::peerResult() const {
  return peerResult_;
}

const BleCsSubeventResult& BleCsControllerSession::completedLocalResult() const {
  return completedLocalResult_;
}

const BleCsSubeventResult& BleCsControllerSession::completedPeerResult() const {
  return completedPeerResult_;
}

void BleCsControllerSession::resetProcedureRunState() {
  localDecoder_.reset();
  peerDecoder_.reset();
  localDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  peerDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  localReassembler_.reset();
  peerReassembler_.reset();
  localResult_ = BleCsSubeventResult{};
  peerResult_ = BleCsSubeventResult{};
  state_.localResultComplete = false;
  state_.peerResultComplete = false;
  resetAccumulatedProcedureResults();
}

void BleCsControllerSession::reconcileReadyWorkflowShadow(
    uint8_t selectedConfigId, bool sessionOpen, bool configCreated,
    bool securityEnabled, bool procedureParametersApplied, bool procedureEnabled) {
  workflow_.reconcileReadyShadowState(selectedConfigId, sessionOpen, configCreated,
                                      securityEnabled, procedureParametersApplied,
                                      procedureEnabled);
  state_.workflowReady = workflow_.ready();
}

void BleCsControllerSession::resetAccumulatedProcedureResults() {
  resetAccumulatedProcedureResult(BleCsControllerResultSource::kLocal);
  resetAccumulatedProcedureResult(BleCsControllerResultSource::kPeer);
}

void BleCsControllerSession::resetAccumulatedProcedureResult(
    BleCsControllerResultSource source) {
  if (source == BleCsControllerResultSource::kLocal) {
    accumulatedLocalResult_ = BleCsSubeventResult{};
  } else {
    accumulatedPeerResult_ = BleCsSubeventResult{};
  }
}

bool BleCsControllerSession::accumulateProcedureResult(BleCsControllerResultSource source,
                                                       const BleCsSubeventResult& result) {
  if (!result.isComplete || result.stepData == nullptr || result.stepDataLen == 0U) {
    return false;
  }

  BleCsSubeventResult& accumulated =
      (source == BleCsControllerResultSource::kLocal) ? accumulatedLocalResult_
                                                      : accumulatedPeerResult_;
  uint8_t* storage =
      (source == BleCsControllerResultSource::kLocal) ? accumulatedLocalStepData_
                                                      : accumulatedPeerStepData_;

  const bool sameProcedure =
      accumulated.stepData != nullptr &&
      accumulated.header.connHandle == result.header.connHandle &&
      accumulated.header.configId == result.header.configId &&
      accumulated.header.procedureCounter == result.header.procedureCounter &&
      accumulated.header.numAntennaPaths == result.header.numAntennaPaths;
  if (!sameProcedure) {
    accumulated = BleCsSubeventResult{};
  }

  const uint16_t previousSteps = accumulated.header.numStepsReported;
  const uint16_t previousBytes = accumulated.stepDataLen;
  if (static_cast<size_t>(previousBytes) + result.stepDataLen >
      kBleCsMaxControllerStepDataBytes) {
    accumulated = BleCsSubeventResult{};
    return false;
  }

  if (result.stepDataLen > 0U) {
    memcpy(storage + previousBytes, result.stepData, result.stepDataLen);
  }

  accumulated.header = result.header;
  accumulated.header.numStepsReported =
      static_cast<uint16_t>(previousSteps + result.header.numStepsReported);
  accumulated.stepData = storage;
  accumulated.stepDataLen =
      static_cast<uint16_t>(previousBytes + result.stepDataLen);
  accumulated.isContinuation = false;
  accumulated.isPartial =
      (result.header.procedureDoneStatus == kBleCsProcedureDonePartial);
  accumulated.isComplete = !accumulated.isPartial;
  return true;
}

bool BleCsControllerSession::onWorkflowPacket(const uint8_t* packet,
                                              size_t packetLen,
                                              void* userData) {
  BleCsControllerSession* session = static_cast<BleCsControllerSession*>(userData);
  return (session != nullptr) && session->consumeWorkflowEventPacket(packet, packetLen);
}

bool BleCsControllerSession::onLocalResultPacket(const uint8_t* packet,
                                                 size_t packetLen,
                                                 void* userData) {
  BleCsControllerSession* session = static_cast<BleCsControllerSession*>(userData);
  return (session != nullptr) &&
         session->consumeResultPacket(BleCsControllerResultSource::kLocal, packet, packetLen);
}

bool BleCsControllerSession::onPeerResultPacket(const uint8_t* packet,
                                                size_t packetLen,
                                                void* userData) {
  BleCsControllerSession* session = static_cast<BleCsControllerSession*>(userData);
  return (session != nullptr) &&
         session->consumeResultPacket(BleCsControllerResultSource::kPeer, packet, packetLen);
}

bool BleCsControllerSession::consumeResultPacket(BleCsControllerResultSource source,
                                                 const uint8_t* packet,
                                                 size_t packetLen) {
  BleCsHciLeMetaEvent metaEvent{};
  if (!BleChannelSoundingRadio::parseHciLeMetaEvent(packet, packetLen, &metaEvent)) {
    return false;
  }

  BleCsSubeventResultReassembler& reassembler =
      (source == BleCsControllerResultSource::kLocal) ? localReassembler_ : peerReassembler_;
  BleCsSubeventResult& result =
      (source == BleCsControllerResultSource::kLocal) ? localResult_ : peerResult_;

  bool ok = false;
  if (metaEvent.subeventCode == kBleCsHciEvtSubeventResult) {
    ok = reassembler.consumeInitialEvent(metaEvent.payload, metaEvent.payloadLen, &result);
  } else if (metaEvent.subeventCode == kBleCsHciEvtSubeventResultContinue) {
    ok = reassembler.consumeContinuationEvent(metaEvent.payload, metaEvent.payloadLen, &result);
  } else {
    return false;
  }
  if (!ok) {
    return false;
  }

  if (source == BleCsControllerResultSource::kLocal) {
    state_.localResultComplete = result.isComplete;
  } else {
    state_.peerResultComplete = result.isComplete;
  }
  if (result.isComplete) {
    if (!accumulateProcedureResult(source, result)) {
      return false;
    }
  }
  updateEstimateIfComplete();
  return true;
}

bool BleCsControllerSession::snapshotCompletedResultPair(
    const BleCsSubeventResult& localResult,
    const BleCsSubeventResult& peerResult) {
  if (localResult.stepData == nullptr || peerResult.stepData == nullptr ||
      localResult.stepDataLen > sizeof(completedLocalStepData_) ||
      peerResult.stepDataLen > sizeof(completedPeerStepData_)) {
    return false;
  }

  completedLocalResult_ = localResult;
  completedPeerResult_ = peerResult;
  if (localResult.stepDataLen > 0U) {
    memcpy(completedLocalStepData_, localResult.stepData, localResult.stepDataLen);
  }
  if (peerResult.stepDataLen > 0U) {
    memcpy(completedPeerStepData_, peerResult.stepData, peerResult.stepDataLen);
  }
  completedLocalResult_.stepData =
      (localResult.stepDataLen > 0U) ? completedLocalStepData_ : nullptr;
  completedPeerResult_.stepData =
      (peerResult.stepDataLen > 0U) ? completedPeerStepData_ : nullptr;
  return true;
}

void BleCsControllerSession::updateEstimateIfComplete() {
  if (!accumulatedLocalResult_.isComplete || !accumulatedPeerResult_.isComplete) {
    return;
  }
  if (accumulatedLocalResult_.header.procedureDoneStatus != kBleCsProcedureDoneComplete ||
      accumulatedPeerResult_.header.procedureDoneStatus != kBleCsProcedureDoneComplete) {
    return;
  }
  if (accumulatedLocalResult_.header.connHandle != accumulatedPeerResult_.header.connHandle ||
      accumulatedLocalResult_.header.configId != accumulatedPeerResult_.header.configId ||
      accumulatedLocalResult_.header.procedureCounter !=
          accumulatedPeerResult_.header.procedureCounter) {
    return;
  }
  if (!snapshotCompletedResultPair(accumulatedLocalResult_, accumulatedPeerResult_)) {
    return;
  }
  BleCsEstimate estimate{};
  if (!BleChannelSoundingRadio::estimateDistanceFromSubeventResults(
      completedLocalResult_, completedPeerResult_, config_.localRoleIsInitiator,
      &estimate)) {
    return;
  }
  state_.estimate = estimate;
  state_.estimateValid = estimate.valid;
  state_.completedProcedureCounter = completedLocalResult_.header.procedureCounter;
  state_.completedConfigId = completedLocalResult_.header.configId;
  resetAccumulatedProcedureResults();
}

BleCsControllerHost::BleCsControllerHost()
    : config_{},
      state_{},
      session_{},
      controllerDecoder_{},
      localDecoder_{},
      peerDecoder_{},
      controllerPeerResultsExpected_{false} {}

void BleCsControllerHost::reset() {
  config_ = BleCsControllerHostConfig{};
  state_ = BleCsControllerHostState{};
  session_.reset();
  controllerDecoder_.reset();
  localDecoder_.reset();
  peerDecoder_.reset();
  controllerPeerResultsExpected_ = false;
  controllerDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  localDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
  peerDecoder_.setAcceptedPacketTypes(1UL << kBleHciPacketTypeEvent);
}

bool BleCsControllerHost::begin(uint16_t connHandle, const BleCsControllerHostConfig& config) {
  reset();
  if (config.sendPacket == nullptr) {
    return false;
  }
  config_ = config;
  if (!session_.begin(connHandle, config.session)) {
    return false;
  }
  state_.began = true;
  return true;
}

bool BleCsControllerHost::pumpCommands(uint8_t maxCommands) {
  if (!state_.began || config_.sendPacket == nullptr || maxCommands == 0U) {
    return false;
  }

  uint8_t packet[80] = {0};
  size_t packetLen = 0U;
  for (uint8_t i = 0U; i < maxCommands; ++i) {
    if (!session_.buildNextCommandPacket(packet, sizeof(packet), &packetLen)) {
      break;
    }
    const uint16_t opcode = (packetLen >= 3U) ? readLe16(packet + 1U) : 0U;
    switch (opcode) {
      case kBleCsHciOpCreateConfig:
      case kBleCsHciOpRemoveConfig:
      case kBleCsHciOpSetProcedureParameters:
      case kBleCsHciOpProcedureEnable:
        resetProcedureRunState();
        break;
      default:
        break;
    }
    if (!config_.sendPacket(packet, packetLen, config_.userData)) {
      return false;
    }
    ++state_.sentCommandPackets;
    state_.sentCommandBytes =
        static_cast<uint32_t>(state_.sentCommandBytes + packetLen);
    state_.lastCommandOpcode = opcode;
  }
  return true;
}

void BleCsControllerHost::resetProcedureRunState() {
  session_.resetProcedureRunState();
  controllerPeerResultsExpected_ = false;
}

void BleCsControllerHost::reconcileReadyWorkflowShadow(
    uint8_t selectedConfigId, bool sessionOpen, bool configCreated,
    bool securityEnabled, bool procedureParametersApplied, bool procedureEnabled) {
  session_.reconcileReadyWorkflowShadow(selectedConfigId, sessionOpen, configCreated,
                                        securityEnabled, procedureParametersApplied,
                                        procedureEnabled);
}

bool BleCsControllerHost::consumeIngressPacket(BleCsControllerIngressSource source,
                                               const uint8_t* packet,
                                               size_t packetLen) {
  if (packet == nullptr || packetLen == 0U) {
    return false;
  }

  if (source == BleCsControllerIngressSource::kController) {
    uint8_t eventCode = 0U;
    const uint8_t* eventParams = nullptr;
    size_t eventParamsLen = 0U;
    if (decodeHciEventFrame(packet, packetLen, &eventCode, &eventParams, &eventParamsLen) &&
        eventCode == kBleHciEvtVendor && eventParams != nullptr && eventParamsLen > 0U) {
      if (eventParams[0U] == kBleCsVprVendorEvtPeerResultTrigger) {
        ++state_.vendorPeerResultTriggers;
        ++state_.controllerEventPackets;
        return true;
      }
      if (eventParams[0U] == kBleCsVprVendorEvtPeerResultSource) {
        controllerPeerResultsExpected_ = true;
        ++state_.controllerPeerResultMarkers;
        ++state_.controllerEventPackets;
        return true;
      }
    }

    BleCsHciLeMetaEvent metaEvent{};
    if (BleChannelSoundingRadio::parseHciLeMetaEvent(packet, packetLen, &metaEvent) &&
        (metaEvent.subeventCode == kBleCsHciEvtSubeventResult ||
         metaEvent.subeventCode == kBleCsHciEvtSubeventResultContinue)) {
      BleCsSubeventResult parsedResult{};
      bool parsedResultValid = false;
      if (metaEvent.subeventCode == kBleCsHciEvtSubeventResult) {
        parsedResultValid = BleChannelSoundingRadio::parseHciSubeventResultEvent(
            metaEvent.payload, metaEvent.payloadLen, &parsedResult);
      } else {
        parsedResultValid = BleChannelSoundingRadio::parseHciSubeventResultContinueEvent(
            metaEvent.payload, metaEvent.payloadLen, &parsedResult);
      }
      const BleCsControllerResultSource resultSource =
          controllerPeerResultsExpected_ ? BleCsControllerResultSource::kPeer
                                         : BleCsControllerResultSource::kLocal;
      const bool isInitialSubevent =
          parsedResultValid && metaEvent.subeventCode == kBleCsHciEvtSubeventResult;
      if (parsedResultValid && resultSource == BleCsControllerResultSource::kLocal &&
          metaEvent.subeventCode == kBleCsHciEvtSubeventResult) {
        state_.vendorPeerResultConfigId = parsedResult.header.configId;
        state_.vendorPeerResultProcedureCounter = parsedResult.header.procedureCounter;
      }
      const bool ok = session_.consumeResultEventPacket(resultSource, packet, packetLen);
      if (ok) {
        if (resultSource == BleCsControllerResultSource::kLocal) {
          ++state_.localResultPackets;
          if (isInitialSubevent) {
            ++state_.localSubeventResults;
          }
        } else {
          ++state_.peerResultPackets;
          if (isInitialSubevent) {
            ++state_.peerSubeventResults;
          }
          if (parsedResultValid && parsedResult.isComplete) {
            controllerPeerResultsExpected_ = false;
          }
        }
      }
      return ok;
    }

    const bool ok = session_.consumeWorkflowEventPacket(packet, packetLen);
    if (ok) {
      ++state_.controllerEventPackets;
      return true;
    }

    if (session_.ready()) {
      BleCsHciCommandStatusEvent statusEvent{};
      if (BleChannelSoundingRadio::parseHciCommandStatusEvent(packet, packetLen, &statusEvent)) {
        switch (statusEvent.opcode) {
          case kBleCsHciOpReadRemoteSupportedCapabilities:
          case kBleCsHciOpSecurityEnable:
          case kBleCsHciOpSetDefaultSettings:
          case kBleCsHciOpCreateConfig:
          case kBleCsHciOpRemoveConfig:
          case kBleCsHciOpSetProcedureParameters:
          case kBleCsHciOpProcedureEnable:
            ++state_.controllerEventPackets;
            return true;
          default:
            break;
        }
      }

      BleCsHciCommandCompleteEvent completeEvent{};
      if (BleChannelSoundingRadio::parseHciCommandCompleteEvent(packet, packetLen,
                                                                &completeEvent)) {
        switch (completeEvent.opcode) {
          case kBleCsHciOpReadRemoteSupportedCapabilities:
          case kBleCsHciOpSecurityEnable:
          case kBleCsHciOpSetDefaultSettings:
          case kBleCsHciOpCreateConfig:
          case kBleCsHciOpRemoveConfig:
          case kBleCsHciOpSetProcedureParameters:
          case kBleCsHciOpProcedureEnable:
            ++state_.controllerEventPackets;
            return true;
          default:
            break;
        }
      }

      BleCsHciLeMetaEvent ignoredMetaEvent{};
      if (BleChannelSoundingRadio::parseHciLeMetaEvent(packet, packetLen, &ignoredMetaEvent)) {
        switch (ignoredMetaEvent.subeventCode) {
          case kBleCsHciEvtReadRemoteSupportedCapabilitiesComplete:
          case kBleCsHciEvtReadRemoteSupportedCapabilitiesCompleteV2:
          case kBleCsHciEvtConfigComplete:
          case kBleCsHciEvtSecurityEnableComplete:
          case kBleCsHciEvtProcedureEnableComplete:
            ++state_.controllerEventPackets;
            return true;
          default:
            break;
        }
      }
    }

    return false;
  }

  const BleCsControllerResultSource resultSource =
      (source == BleCsControllerIngressSource::kPeerResult)
          ? BleCsControllerResultSource::kPeer
          : BleCsControllerResultSource::kLocal;
  const bool ok = session_.consumeResultEventPacket(resultSource, packet, packetLen);
  if (ok) {
    if (resultSource == BleCsControllerResultSource::kLocal) {
      ++state_.localResultPackets;
    } else {
      ++state_.peerResultPackets;
    }
  }
  return ok;
}

bool BleCsControllerHost::consumeIngressBytes(BleCsControllerIngressSource source,
                                              const uint8_t* data,
                                              size_t len) {
  if (source == BleCsControllerIngressSource::kController) {
    const bool ok = controllerDecoder_.pushBytes(data, len, onControllerPacket, this);
    state_.controllerIgnoredPackets = controllerDecoder_.ignoredPacketCount();
    state_.controllerIgnoredBytes = controllerDecoder_.ignoredByteCount();
    return ok;
  }

  BleHciPacketStreamDecoder& decoder =
      (source == BleCsControllerIngressSource::kPeerResult) ? peerDecoder_ : localDecoder_;
  const bool ok = decoder.pushBytes(
      data, len,
      (source == BleCsControllerIngressSource::kPeerResult) ? onPeerPacket : onLocalPacket,
      this);
  return ok;
}

bool BleCsControllerHost::ready() const { return session_.ready(); }

bool BleCsControllerHost::failed() const { return session_.failed(); }

bool BleCsControllerHost::estimateValid() const { return session_.estimateValid(); }

const BleCsControllerHostState& BleCsControllerHost::state() const { return state_; }

const BleCsControllerSessionState& BleCsControllerHost::sessionState() const {
  return session_.state();
}

const BleCsControllerWorkflowState& BleCsControllerHost::workflowState() const {
  return session_.workflowState();
}

const BleCsSubeventResult& BleCsControllerHost::localResult() const {
  return session_.localResult();
}

const BleCsSubeventResult& BleCsControllerHost::peerResult() const {
  return session_.peerResult();
}

const BleCsSubeventResult& BleCsControllerHost::completedLocalResult() const {
  return session_.completedLocalResult();
}

const BleCsSubeventResult& BleCsControllerHost::completedPeerResult() const {
  return session_.completedPeerResult();
}

bool BleCsControllerHost::onControllerPacket(const uint8_t* packet,
                                             size_t packetLen,
                                             void* userData) {
  BleCsControllerHost* host = static_cast<BleCsControllerHost*>(userData);
  return (host != nullptr) &&
         host->consumeIngressPacket(BleCsControllerIngressSource::kController, packet,
                                    packetLen);
}

bool BleCsControllerHost::onLocalPacket(const uint8_t* packet,
                                        size_t packetLen,
                                        void* userData) {
  BleCsControllerHost* host = static_cast<BleCsControllerHost*>(userData);
  return (host != nullptr) &&
         host->consumeIngressPacket(BleCsControllerIngressSource::kLocalResult, packet,
                                    packetLen);
}

bool BleCsControllerHost::onPeerPacket(const uint8_t* packet,
                                       size_t packetLen,
                                       void* userData) {
  BleCsControllerHost* host = static_cast<BleCsControllerHost*>(userData);
  return (host != nullptr) &&
         host->consumeIngressPacket(BleCsControllerIngressSource::kPeerResult, packet,
                                    packetLen);
}

BleCsControllerStreamHost::BleCsControllerStreamHost()
    : config_{}, state_{}, host_{} {}

void BleCsControllerStreamHost::reset() {
  config_ = BleCsControllerStreamHostConfig{};
  state_ = BleCsControllerStreamHostState{};
  host_.reset();
}

bool BleCsControllerStreamHost::begin(uint16_t connHandle,
                                      const BleCsControllerStreamHostConfig& config) {
  reset();
  if (config.controllerStream == nullptr) {
    return false;
  }
  config_ = config;
  config_.maxCommandsPerPump = (config_.maxCommandsPerPump == 0U) ? 1U : config_.maxCommandsPerPump;
  config_.maxControllerBytesPerPoll = clampPollBytes(config_.maxControllerBytesPerPoll);
  config_.maxPeerBytesPerPoll = clampPollBytes(config_.maxPeerBytesPerPoll);

  BleCsControllerHostConfig hostConfig{};
  hostConfig.session = config_.session;
  hostConfig.sendPacket = onSendPacket;
  hostConfig.userData = this;
  return host_.begin(connHandle, hostConfig);
}

bool BleCsControllerStreamHost::pumpCommands() {
  return host_.pumpCommands(config_.maxCommandsPerPump);
}

bool BleCsControllerStreamHost::pollController() {
  if (config_.controllerStream == nullptr) {
    return false;
  }

  uint8_t bytes[128] = {0};
  const size_t limit = (config_.maxControllerBytesPerPoll < sizeof(bytes))
                           ? config_.maxControllerBytesPerPoll
                           : sizeof(bytes);
  size_t count = 0U;
  while (count < limit && config_.controllerStream->available() > 0) {
    const int raw = config_.controllerStream->read();
    if (raw < 0) {
      break;
    }
    bytes[count++] = static_cast<uint8_t>(raw);
  }
  if (count == 0U) {
    return true;
  }
  state_.controllerBytesRead = static_cast<uint32_t>(state_.controllerBytesRead + count);
  return host_.consumeIngressBytes(BleCsControllerIngressSource::kController, bytes, count);
}

bool BleCsControllerStreamHost::pollPeerResults() {
  if (config_.peerResultStream == nullptr) {
    return true;
  }

  uint8_t bytes[128] = {0};
  const size_t limit =
      (config_.maxPeerBytesPerPoll < sizeof(bytes)) ? config_.maxPeerBytesPerPoll : sizeof(bytes);
  size_t count = 0U;
  while (count < limit && config_.peerResultStream->available() > 0) {
    const int raw = config_.peerResultStream->read();
    if (raw < 0) {
      break;
    }
    bytes[count++] = static_cast<uint8_t>(raw);
  }
  if (count == 0U) {
    return true;
  }
  state_.peerBytesRead = static_cast<uint32_t>(state_.peerBytesRead + count);
  return host_.consumeIngressBytes(BleCsControllerIngressSource::kPeerResult, bytes, count);
}

bool BleCsControllerStreamHost::consumeControllerPacket(const uint8_t* packet, size_t packetLen) {
  if (packet == nullptr || packetLen == 0U) {
    return false;
  }
  state_.controllerBytesRead = static_cast<uint32_t>(state_.controllerBytesRead + packetLen);
  return host_.consumeIngressPacket(BleCsControllerIngressSource::kController, packet, packetLen);
}

bool BleCsControllerStreamHost::consumePeerPacket(const uint8_t* packet, size_t packetLen) {
  if (packet == nullptr || packetLen == 0U) {
    return false;
  }
  state_.peerBytesRead = static_cast<uint32_t>(state_.peerBytesRead + packetLen);
  return host_.consumeIngressPacket(BleCsControllerIngressSource::kPeerResult, packet, packetLen);
}

bool BleCsControllerStreamHost::poll() {
  return pollController() && pollPeerResults();
}

bool BleCsControllerStreamHost::loopOnce() {
  return pumpCommands() && poll();
}

bool BleCsControllerStreamHost::ready() const { return host_.ready(); }

bool BleCsControllerStreamHost::failed() const { return host_.failed(); }

bool BleCsControllerStreamHost::estimateValid() const { return host_.estimateValid(); }

const BleCsControllerStreamHostState& BleCsControllerStreamHost::state() const { return state_; }

const BleCsControllerHostState& BleCsControllerStreamHost::hostState() const {
  return host_.state();
}

const BleCsControllerSessionState& BleCsControllerStreamHost::sessionState() const {
  return host_.sessionState();
}

const BleCsControllerWorkflowState& BleCsControllerStreamHost::workflowState() const {
  return host_.workflowState();
}

const BleCsSubeventResult& BleCsControllerStreamHost::localResult() const {
  return host_.localResult();
}

const BleCsSubeventResult& BleCsControllerStreamHost::peerResult() const {
  return host_.peerResult();
}

const BleCsSubeventResult& BleCsControllerStreamHost::completedLocalResult() const {
  return host_.completedLocalResult();
}

const BleCsSubeventResult& BleCsControllerStreamHost::completedPeerResult() const {
  return host_.completedPeerResult();
}

void BleCsControllerStreamHost::resetProcedureRunState() {
  host_.resetProcedureRunState();
}

void BleCsControllerStreamHost::reconcileReadyWorkflowShadow(
    uint8_t selectedConfigId, bool sessionOpen, bool configCreated,
    bool securityEnabled, bool procedureParametersApplied, bool procedureEnabled) {
  host_.reconcileReadyWorkflowShadow(selectedConfigId, sessionOpen, configCreated,
                                     securityEnabled, procedureParametersApplied,
                                     procedureEnabled);
}

bool BleCsControllerStreamHost::onSendPacket(const uint8_t* packet,
                                             size_t packetLen,
                                             void* userData) {
  BleCsControllerStreamHost* transport = static_cast<BleCsControllerStreamHost*>(userData);
  if (transport == nullptr || transport->config_.controllerStream == nullptr || packet == nullptr ||
      packetLen == 0U) {
    return false;
  }

  const size_t written = transport->config_.controllerStream->write(packet, packetLen);
  if (written != packetLen) {
    transport->state_.lastWriteError = 1U;
    return false;
  }
  ++transport->state_.controllerPacketsWritten;
  transport->state_.controllerBytesWritten =
      static_cast<uint32_t>(transport->state_.controllerBytesWritten + written);
  return true;
}

size_t BleCsControllerStreamHost::clampPollBytes(size_t value) {
  if (value == 0U) {
    return 128U;
  }
  return (value > 1024U) ? 1024U : value;
}

void BleCsControllerVprHost::fillDemoConfig(BleCsControllerVprHostConfig* outConfig) {
  if (outConfig == nullptr) {
    return;
  }

  *outConfig = BleCsControllerVprHostConfig{};
  outConfig->maxCommandsPerPump = 1U;
  outConfig->maxControllerBytesPerPoll = 128U;
  outConfig->maxPeerBytesPerPoll = 128U;

  outConfig->builtInPeerDemo.enabled = true;
  outConfig->builtInPeerDemo.distanceMeters = 0.75f;
  outConfig->builtInPeerDemo.amplitude = 1024.0f;
  outConfig->builtInPeerDemo.channels[0] = 0U;
  outConfig->builtInPeerDemo.channels[1] = 12U;
  outConfig->builtInPeerDemo.channels[2] = 24U;
  outConfig->builtInPeerDemo.channels[3] = 36U;
  outConfig->builtInPeerDemo.channelCount = 4U;

  outConfig->session.localRoleIsInitiator = true;
  outConfig->session.workflow.applyDefaultSettings = true;
  outConfig->session.workflow.requireSecurityEnable = true;
  outConfig->session.workflow.defaultSettings.enableInitiatorRole = true;
  outConfig->session.workflow.defaultSettings.enableReflectorRole = true;
  outConfig->session.workflow.defaultSettings.csSyncAntennaSelection = 0xFEU;
  outConfig->session.workflow.defaultSettings.maxTxPowerDbm = -8;

  outConfig->session.workflow.createConfig.configId = 1U;
  outConfig->session.workflow.createConfig.createContext = 1U;
  outConfig->session.workflow.createConfig.mainModeType = kBleCsMainMode2;
  outConfig->session.workflow.createConfig.subModeType = 0xFFU;
  outConfig->session.workflow.createConfig.minMainModeSteps = 3U;
  outConfig->session.workflow.createConfig.maxMainModeSteps = 5U;
  outConfig->session.workflow.createConfig.mainModeRepetition = 1U;
  outConfig->session.workflow.createConfig.mode0Steps = 1U;
  outConfig->session.workflow.createConfig.role = 0U;
  outConfig->session.workflow.createConfig.rttType = 1U;
  outConfig->session.workflow.createConfig.csSyncPhy = 2U;
  BleChannelSoundingRadio::fillValidChannelMap(
      outConfig->session.workflow.createConfig.channelMap);
  outConfig->session.workflow.createConfig.channelMapRepetition = 1U;
  outConfig->session.workflow.createConfig.channelSelectionType = 1U;
  outConfig->session.workflow.createConfig.ch3cShape = 1U;
  outConfig->session.workflow.createConfig.ch3cJump = 3U;
  outConfig->session.workflow.createConfig.csEnhancements1 = 0x01U;

  outConfig->session.workflow.procedureParameters.configId = 1U;
  outConfig->session.workflow.procedureParameters.maxProcedureLen = 12U;
  outConfig->session.workflow.procedureParameters.minProcedureInterval = 200U;
  outConfig->session.workflow.procedureParameters.maxProcedureInterval = 300U;
  outConfig->session.workflow.procedureParameters.maxProcedureCount = 8U;
  outConfig->session.workflow.procedureParameters.minSubeventLen = 0x000456UL;
  outConfig->session.workflow.procedureParameters.maxSubeventLen = 0x000678UL;
  outConfig->session.workflow.procedureParameters.toneAntennaConfigSelection = 2U;
  outConfig->session.workflow.procedureParameters.phy = 2U;
  outConfig->session.workflow.procedureParameters.txPowerDelta = -6;
  outConfig->session.workflow.procedureParameters.preferredPeerAntenna = 0xFFU;
  outConfig->session.workflow.procedureParameters.snrControlInitiator = 0U;
  outConfig->session.workflow.procedureParameters.snrControlReflector = 0U;

  outConfig->session.workflow.procedureEnable.configId = 1U;
  outConfig->session.workflow.procedureEnable.enable = 1U;
}

BleCsControllerVprHost::BleCsControllerVprHost()
    : config_{}, vprState_{}, transport_{}, host_{} {}

void BleCsControllerVprHost::reset() {
  config_ = BleCsControllerVprHostConfig{};
  vprState_ = BleCsControllerVprHostState{};
  host_.reset();
}

bool BleCsControllerVprHost::resetTransport(bool clearScripts) {
  const bool ok = transport_.resetSharedState(clearScripts);
  vprState_.linkSessionOpen = false;
  vprState_.linkConnHandle = 0U;
  vprState_.linkProcedureIntervalSelector = 0U;
  vprState_.linkStoredConfigCount = 0U;
  vprState_.linkPeerGapTicks = 0U;
  vprState_.linkConfigId = 0U;
  vprState_.linkSlot0ConfigId = 0U;
  vprState_.linkSlot1ConfigId = 0U;
  vprState_.linkPreviousConfigId = 0U;
  vprState_.linkAuthority0ConfigId = 0U;
  vprState_.linkAuthority1ConfigId = 0U;
  vprState_.linkAuthority2ConfigId = 0U;
  vprState_.linkActivePrimarySlotIndex = 0xFFU;
  vprState_.linkFreePrimarySlotCount = 0U;
  vprState_.linkProcedureCounter = 0U;
  vprState_.linkConfigCreated = false;
  vprState_.linkSecurityEnabled = false;
  vprState_.linkProcedureParamsApplied = false;
  vprState_.linkProcedureEnabled = false;
  vprState_.linkSlot0InUse = false;
  vprState_.linkSlot1InUse = false;
  vprState_.linkPreviousSlotInUse = false;
  vprState_.linkActiveConfigMirroredInPrevious = false;
  vprState_.linkSelectedConfigRunnable = false;
  vprState_.linkSlot0Runnable = false;
  vprState_.linkSlot1Runnable = false;
  vprState_.linkPreviousSlotRunnable = false;
  vprState_.linkSelectedConfigSecurityEnabled = false;
  vprState_.linkSlot0SecurityEnabled = false;
  vprState_.linkSlot1SecurityEnabled = false;
  vprState_.linkPreviousSlotSecurityEnabled = false;
  vprState_.linkSelectedConfigProcedureParamsApplied = false;
  vprState_.linkSlot0ProcedureParamsApplied = false;
  vprState_.linkSlot1ProcedureParamsApplied = false;
  vprState_.linkPreviousSlotProcedureParamsApplied = false;
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::addScriptResponse(uint16_t opcode,
                                               const uint8_t* response,
                                               size_t len) {
  const bool ok = transport_.addScriptResponse(opcode, response, len);
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::loadDefaultTransportImage() {
  const bool ok = transport_.loadDefaultCsControllerStubImage();
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::bootTransport(uint32_t readySpinLimit) {
  if (!transport_.bootLoadedFirmware()) {
    syncVprState();
    return false;
  }
  const bool ok = transport_.waitReady(readySpinLimit);
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::refreshLinkSession() {
  syncVprState();
  return vprState_.running && vprState_.transportStatus != 0U;
}

bool BleCsControllerVprHost::beginHost(uint16_t connHandle,
                                       const BleCsControllerVprHostConfig& config) {
  config_ = config;

  volatile Nrf54l15VprTransportHostShared* sharedHost =
      nrf54l15_vpr_transport_host_shared();
  if (sharedHost != nullptr) {
    uint32_t packedDemoChannels = 0U;
    if (config.builtInPeerDemo.enabled && config.builtInPeerDemo.channelCount > 0U) {
      uint8_t lastChannel = config.builtInPeerDemo.channels[0U];
      for (uint8_t i = 0U; i < 4U; ++i) {
        if (i < config.builtInPeerDemo.channelCount) {
          lastChannel = config.builtInPeerDemo.channels[i];
        }
        packedDemoChannels |= (static_cast<uint32_t>(lastChannel) << (8U * i));
      }
    }
    sharedHost->reserved = packedDemoChannels;
  }

  BleCsControllerStreamHostConfig streamConfig{};
  streamConfig.session = config.session;
  streamConfig.controllerStream = &transport_;
  streamConfig.peerResultStream = config.peerResultStream;
  streamConfig.maxCommandsPerPump = config.maxCommandsPerPump;
  streamConfig.maxControllerBytesPerPoll = config.maxControllerBytesPerPoll;
  streamConfig.maxPeerBytesPerPoll = config.maxPeerBytesPerPoll;

  const bool ok = host_.begin(connHandle, streamConfig);
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::beginFreshHost(
    uint16_t connHandle,
    const BleCsControllerVprHostConfig& config,
    uint8_t maxPumpCount,
    uint8_t* outPumpCount) {
  if (outPumpCount != nullptr) {
    *outPumpCount = 0U;
  }

  bool ok = resetTransport(true);
  ok = ok && loadDefaultTransportImage();
  ok = ok && bootTransport();
  ok = ok && beginHost(connHandle, config);

  while (ok && !ready() && !failed()) {
    if (outPumpCount != nullptr && *outPumpCount >= maxPumpCount) {
      break;
    }
    ok = loopOnce();
    if (outPumpCount != nullptr) {
      *outPumpCount = static_cast<uint8_t>(*outPumpCount + 1U);
    }
  }
  return ok;
}

bool BleCsControllerVprHost::beginFreshHostFromBleConnection(
    VprControllerServiceHost& sourceService,
    const BleCsControllerVprHostConfig& config,
    uint8_t maxPumpCount,
    uint8_t* outPumpCount,
    VprBleConnectionSharedState* outImportedState,
    uint32_t sourceStateTimeoutMs) {
  if (outPumpCount != nullptr) {
    *outPumpCount = 0U;
  }
  if (outImportedState != nullptr) {
    *outImportedState = VprBleConnectionSharedState{};
  }

  VprBleConnectionSharedState importedState{};
  if (!sourceService.readBleConnectionSharedState(&importedState)) {
    return false;
  }
  if (!importedState.connected || importedState.connHandle == 0U) {
    if (!sourceService.waitBleConnectionSharedState(true, 1U, &importedState,
                                                    sourceStateTimeoutMs)) {
      return false;
    }
  }
  if (!importedState.connected || importedState.connHandle == 0U) {
    return false;
  }

  if (outImportedState != nullptr) {
    *outImportedState = importedState;
  }

  bool ok = resetTransport(true);
  ok = ok && loadDefaultTransportImage();
  ok = ok && writeBleConnectionBootHandoff(nrf54l15_vpr_transport_host_shared(),
                                           importedState);
  ok = ok && bootTransport();
  syncVprState();

  uint16_t connHandle = importedState.connHandle;
  if (vprState_.linkSessionOpen && vprState_.linkConnHandle != 0U) {
    connHandle = vprState_.linkConnHandle;
  }

  ok = ok && beginHost(connHandle, config);

  while (ok && !ready() && !failed()) {
    if (outPumpCount != nullptr && *outPumpCount >= maxPumpCount) {
      break;
    }
    ok = loopOnce();
    if (outPumpCount != nullptr) {
      *outPumpCount = static_cast<uint8_t>(*outPumpCount + 1U);
    }
  }
  return ok;
}

bool BleCsControllerVprHost::beginFreshWorkflowFromBleConnection(
    VprControllerServiceHost& sourceService,
    const BleCsControllerVprHostConfig& config,
    bool enableProcedure,
    uint8_t maxPumpCount,
    uint8_t* outPumpCount,
    VprBleConnectionSharedState* outImportedState,
    BleCsControllerVprWorkflowStartStatus* outWorkflowStatus,
    uint32_t sourceStateTimeoutMs) {
  const bool ok = beginFreshHostFromBleConnection(sourceService, config, maxPumpCount,
                                                  outPumpCount, outImportedState,
                                                  sourceStateTimeoutMs);
  return ok && directStartConfiguredWorkflow(enableProcedure, outWorkflowStatus);
}

bool BleCsControllerVprHost::directStartConfiguredWorkflow(
    bool enableProcedure,
    BleCsControllerVprWorkflowStartStatus* outWorkflowStatus) {
  if (outWorkflowStatus != nullptr) {
    *outWorkflowStatus = BleCsControllerVprWorkflowStartStatus{};
  }

  const BleCsControllerWorkflowConfig& workflowConfig = config_.session.workflow;
  uint8_t status = 0xFFU;

  if (!directReadRemoteSupportedCapabilities(&status)) {
    if (outWorkflowStatus != nullptr) {
      outWorkflowStatus->readRemoteSupportedCapabilities = status;
    }
    return false;
  }
  if (outWorkflowStatus != nullptr) {
    outWorkflowStatus->readRemoteSupportedCapabilities = status;
  }
  if (status != 0U) {
    return false;
  }

  if (workflowConfig.applyDefaultSettings) {
    status = 0xFFU;
    if (!directSetDefaultSettings(workflowConfig.defaultSettings, &status)) {
      if (outWorkflowStatus != nullptr) {
        outWorkflowStatus->setDefaultSettings = status;
      }
      return false;
    }
    if (outWorkflowStatus != nullptr) {
      outWorkflowStatus->setDefaultSettings = status;
    }
    if (status != 0U) {
      return false;
    }
  }

  status = 0xFFU;
  if (!directCreateConfig(workflowConfig.createConfig, &status)) {
    if (outWorkflowStatus != nullptr) {
      outWorkflowStatus->createConfig = status;
    }
    return false;
  }
  if (outWorkflowStatus != nullptr) {
    outWorkflowStatus->createConfig = status;
  }
  if (status != 0U) {
    return false;
  }

  if (workflowConfig.requireSecurityEnable) {
    status = 0xFFU;
    if (!directSecurityEnable(&status)) {
      if (outWorkflowStatus != nullptr) {
        outWorkflowStatus->securityEnable = status;
      }
      return false;
    }
    if (outWorkflowStatus != nullptr) {
      outWorkflowStatus->securityEnable = status;
    }
    if (status != 0U) {
      return false;
    }
  }

  status = 0xFFU;
  if (!directSetProcedureParameters(workflowConfig.procedureParameters, &status)) {
    if (outWorkflowStatus != nullptr) {
      outWorkflowStatus->setProcedureParameters = status;
    }
    return false;
  }
  if (outWorkflowStatus != nullptr) {
    outWorkflowStatus->setProcedureParameters = status;
  }
  if (status != 0U) {
    return false;
  }

  if (enableProcedure) {
    status = 0xFFU;
    bool started = directProcedureEnable(workflowConfig.procedureEnable, &status);
    if (!started || status != 0U) {
      for (uint8_t i = 0U; i < 8U && !failed(); ++i) {
        if (!poll()) {
          started = false;
          break;
        }
        if (vprState_.linkProcedureEnabled ||
            sessionState().completedProcedureCounter > 0U ||
            hostState().localSubeventResults > 0U ||
            hostState().peerSubeventResults > 0U) {
          status = 0U;
          started = true;
          break;
        }
      }
    }
    if (!started) {
      if (outWorkflowStatus != nullptr) {
        outWorkflowStatus->procedureEnable = status;
      }
      return false;
    }
    if (outWorkflowStatus != nullptr) {
      outWorkflowStatus->procedureEnable = status;
    }
    if (status != 0U) {
      return false;
    }
  }

  return true;
}

bool BleCsControllerVprHost::sendDirectHciCommand(uint16_t opcode,
                                                  const uint8_t* params,
                                                  size_t paramsLen,
                                                  uint8_t* response,
                                                  size_t responseSize,
                                                  size_t* responseLen) {
  bool resetRunStateBefore = false;
  bool resetRunStateAfter = false;
  switch (opcode) {
    case kBleCsHciOpCreateConfig:
    case kBleCsHciOpSetProcedureParameters:
    case kBleCsHciOpProcedureEnable:
      resetRunStateBefore = true;
      break;
    case kBleCsHciOpRemoveConfig:
      resetRunStateAfter = true;
      break;
    default:
      break;
  }
  if (resetRunStateBefore) {
    host_.resetProcedureRunState();
  }

  VprControllerServiceHost directHost(&transport_);
  const bool ok =
      directHost.sendHciCommand(opcode, params, paramsLen, response, responseSize, responseLen);
  const bool drained =
      ok && drainDirectControllerEvents(&directHost, response,
                                        (responseLen != nullptr) ? *responseLen : 0U);
  if (ok && drained && resetRunStateAfter) {
    host_.resetProcedureRunState();
  }
  syncVprState();
  return ok && drained;
}

bool BleCsControllerVprHost::currentConnHandle(uint16_t* outConnHandle) const {
  if (outConnHandle == nullptr) {
    return false;
  }

  uint16_t connHandle = workflowState().connHandle;
  if (connHandle == 0U) {
    connHandle = vprState_.linkConnHandle;
  }
  if (connHandle == 0U) {
    return false;
  }

  *outConnHandle = connHandle;
  return true;
}

bool BleCsControllerVprHost::sendDirectBuiltCommand(const BleCsHciCommand& command,
                                                    uint8_t* outStatus) {
  uint8_t response[64] = {0};
  size_t responseLen = 0U;
  if (!sendDirectHciCommand(command.opcode, command.payload, command.payloadLen, response,
                            sizeof(response), &responseLen)) {
    return false;
  }
  return parseDirectStatusResponse(response, responseLen, command.opcode, outStatus);
}

bool BleCsControllerVprHost::directReadRemoteSupportedCapabilities(uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciReadRemoteSupportedCapabilitiesCommand(connHandle,
                                                                                 &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directSetDefaultSettings(const BleCsDefaultSettings& settings,
                                                      uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciSetDefaultSettingsCommand(connHandle, settings,
                                                                    &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directCreateConfig(const BleCsControllerCreateConfig& config,
                                                uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciCreateConfigCommand(connHandle, config, &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directRemoveConfig(uint8_t configId, uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciRemoveConfigCommand(connHandle, configId, &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directSecurityEnable(uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciSecurityEnableCommand(connHandle, &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directSetProcedureParameters(const BleCsProcedureParameters& params,
                                                          uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciSetProcedureParametersCommand(connHandle, params,
                                                                        &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directProcedureEnable(const BleCsProcedureEnable& params,
                                                   uint8_t* outStatus) {
  uint16_t connHandle = 0U;
  BleCsHciCommand command{};
  return currentConnHandle(&connHandle) &&
         BleChannelSoundingRadio::buildHciProcedureEnableCommand(connHandle, params, &command) &&
         sendDirectBuiltCommand(command, outStatus);
}

bool BleCsControllerVprHost::directProcedureEnable(uint8_t configId,
                                                   bool enable,
                                                   uint8_t* outStatus) {
  BleCsProcedureEnable params{};
  params.configId = configId;
  params.enable = enable ? 1U : 0U;
  return directProcedureEnable(params, outStatus);
}

bool BleCsControllerVprHost::directCurrentProcedureEnable(bool enable,
                                                          uint8_t* outStatus) {
  return directProcedureEnable(workflowState().configComplete.configId, enable,
                               outStatus);
}

bool BleCsControllerVprHost::pollUntilRunningWithProcedureCount(
    uint16_t targetProcedureCount,
    uint8_t maxPolls,
    uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    const bool completed =
        sessionState().completedProcedureCounter >= targetProcedureCount;
    const bool running = vprState_.linkProcedureEnabled;
    if (completed && running) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilStopped(uint8_t maxPolls,
                                              uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    if (!vprState_.linkProcedureEnabled) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return !vprState_.linkProcedureEnabled;
}

bool BleCsControllerVprHost::pollUntilStoppedWithProcedureCount(uint16_t targetProcedureCount,
                                                                uint8_t maxPolls,
                                                                uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    const bool completed = sessionState().completedProcedureCounter >= targetProcedureCount;
    const bool stopped = !vprState_.linkProcedureEnabled;
    if (completed && stopped) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilStoppedOnConfig(uint8_t targetConfigId,
                                                      uint8_t maxPolls,
                                                      uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    const BleCsSubeventResult currentLocal = completedLocalResult();
    const BleCsSubeventResult currentPeer = completedPeerResult();
    const bool stopped = !vprState_.linkProcedureEnabled;
    if (stopped && currentLocal.header.configId == targetConfigId &&
        currentPeer.header.configId == targetConfigId) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilRunComplete(uint32_t targetLocalSubevents,
                                                  uint32_t targetPeerSubevents,
                                                  uint8_t maxPolls,
                                                  uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    const bool completed =
        hostState().localSubeventResults >= targetLocalSubevents &&
        hostState().peerSubeventResults >= targetPeerSubevents;
    const bool stopped = !vprState_.linkProcedureEnabled;
    if (completed && stopped) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilCompletedProcedureResult(
    uint16_t targetProcedureCount,
    uint32_t targetLocalSubevents,
    uint32_t targetPeerSubevents,
    uint8_t maxPolls,
    uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    const bool completed =
        sessionState().completedProcedureCounter >= targetProcedureCount &&
        hostState().localSubeventResults >= targetLocalSubevents &&
        hostState().peerSubeventResults >= targetPeerSubevents;
    if (completed) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilSelectedState(uint8_t selectedConfigId,
                                                    uint8_t storedCount,
                                                    bool selectedRunnable,
                                                    uint8_t maxPolls,
                                                    uint8_t* outPolls) {
  BleCsControllerVprSelectedStateExpectation expected{};
  expected.selectedConfigId = selectedConfigId;
  expected.storedConfigCount = storedCount;
  expected.selectedRunnable = selectedRunnable;
  return pollUntilSelectedState(expected, maxPolls, outPolls);
}

bool BleCsControllerVprHost::pollUntilSelectedState(
    const BleCsControllerVprSelectedStateExpectation& expected,
    uint8_t maxPolls,
    uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    if (vprState_.selectedStateMatches(expected)) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilRetainedSelectionState(uint8_t activeConfigId,
                                                             uint8_t slot0ConfigId,
                                                             uint8_t slot1ConfigId,
                                                             uint8_t previousConfigId,
                                                             uint8_t storedConfigCount,
                                                             bool selectedRunnable,
                                                             bool previousRunnable,
                                                             uint8_t maxPolls,
                                                             uint8_t* outPolls) {
  BleCsControllerVprRetainedSelectionExpectation expected{};
  expected.activeConfigId = activeConfigId;
  expected.slot0ConfigId = slot0ConfigId;
  expected.slot1ConfigId = slot1ConfigId;
  expected.previousConfigId = previousConfigId;
  expected.storedConfigCount = storedConfigCount;
  expected.selectedRunnable = selectedRunnable;
  expected.previousRunnable = previousRunnable;
  return pollUntilRetainedSelectionState(expected, maxPolls, outPolls);
}

bool BleCsControllerVprHost::pollUntilRetainedSelectionState(
    const BleCsControllerVprRetainedSelectionExpectation& expected,
    uint8_t maxPolls,
    uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    if (vprState_.retainedConfigMatches(expected)) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::settleDirectIdle(uint8_t stablePollsRequired,
                                              uint8_t maxPolls,
                                              uint8_t* outPolls) {
  uint8_t stablePolls = 0U;
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
    if (!vprState_.linkProcedureEnabled && transport_.available() == 0) {
      ++stablePolls;
      if (stablePolls >= stablePollsRequired) {
        return true;
      }
    } else {
      stablePolls = 0U;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
  }
  return !vprState_.linkProcedureEnabled && transport_.available() == 0;
}

bool BleCsControllerVprHost::pollUntilRetainedSlots(uint8_t activeConfigId,
                                                    uint8_t slot0ConfigId,
                                                    uint8_t slot1ConfigId,
                                                    uint8_t previousConfigId,
                                                    uint8_t activePrimarySlotIndex,
                                                    uint8_t freePrimarySlotCount,
                                                    uint8_t storedConfigCount,
                                                    uint8_t maxPolls,
                                                    uint8_t* outPolls) {
  BleCsControllerVprRetainedSlotsExpectation expected{};
  expected.activeConfigId = activeConfigId;
  expected.slot0ConfigId = slot0ConfigId;
  expected.slot1ConfigId = slot1ConfigId;
  expected.previousConfigId = previousConfigId;
  expected.activePrimarySlotIndex = activePrimarySlotIndex;
  expected.freePrimarySlotCount = freePrimarySlotCount;
  expected.storedConfigCount = storedConfigCount;
  return pollUntilRetainedSlots(expected, maxPolls, outPolls);
}

bool BleCsControllerVprHost::pollUntilRetainedSlots(
    const BleCsControllerVprRetainedSlotsExpectation& expected,
    uint8_t maxPolls,
    uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    if (vprState_.retainedConfigMatches(expected)) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pollUntilRetainedState(uint8_t activeConfigId,
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
                                                    uint8_t* outPolls) {
  BleCsControllerVprRetainedStateExpectation expected{};
  expected.slots.activeConfigId = activeConfigId;
  expected.slots.slot0ConfigId = slot0ConfigId;
  expected.slots.slot1ConfigId = slot1ConfigId;
  expected.slots.previousConfigId = previousConfigId;
  expected.slots.activePrimarySlotIndex = activePrimarySlotIndex;
  expected.slots.freePrimarySlotCount = freePrimarySlotCount;
  expected.slots.storedConfigCount = storedConfigCount;
  expected.runnability.selectedRunnable = selectedRunnable;
  expected.runnability.slot0Runnable = slot0Runnable;
  expected.runnability.slot1Runnable = slot1Runnable;
  expected.runnability.previousRunnable = previousRunnable;
  expected.readiness.selectedSecurityEnabled = selectedSecurityEnabled;
  expected.readiness.slot0SecurityEnabled = slot0SecurityEnabled;
  expected.readiness.slot1SecurityEnabled = slot1SecurityEnabled;
  expected.readiness.previousSecurityEnabled = previousSecurityEnabled;
  expected.readiness.selectedProcedureParamsApplied =
      selectedProcedureParamsApplied;
  expected.readiness.slot0ProcedureParamsApplied = slot0ProcedureParamsApplied;
  expected.readiness.slot1ProcedureParamsApplied = slot1ProcedureParamsApplied;
  expected.readiness.previousProcedureParamsApplied =
      previousProcedureParamsApplied;
  expected.checkRunnability = true;
  expected.checkReadiness = true;
  return pollUntilRetainedState(expected, maxPolls, outPolls);
}

bool BleCsControllerVprHost::pollUntilRetainedState(
    const BleCsControllerVprRetainedStateExpectation& expected,
    uint8_t maxPolls,
    uint8_t* outPolls) {
  if (outPolls != nullptr) {
    *outPolls = 0U;
  }
  while (!failed()) {
    if (vprState_.retainedConfigMatches(expected)) {
      return true;
    }
    if (outPolls != nullptr && *outPolls >= maxPolls) {
      break;
    }
    if (!poll()) {
      return false;
    }
    if (outPolls != nullptr) {
      *outPolls = static_cast<uint8_t>(*outPolls + 1U);
    }
  }
  return false;
}

bool BleCsControllerVprHost::pumpCommands() {
  const bool ok = host_.pumpCommands();
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::poll() {
  const bool ok = host_.poll();
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::loopOnce() {
  const bool ok = host_.loopOnce();
  syncVprState();
  return ok;
}

bool BleCsControllerVprHost::ready() const { return host_.ready(); }

bool BleCsControllerVprHost::failed() const { return host_.failed(); }

bool BleCsControllerVprHost::estimateValid() const { return host_.estimateValid(); }

const BleCsControllerVprHostState& BleCsControllerVprHost::vprState() const {
  return vprState_;
}

const BleCsControllerStreamHostState& BleCsControllerVprHost::streamState() const {
  return host_.state();
}

const BleCsControllerHostState& BleCsControllerVprHost::hostState() const {
  return host_.hostState();
}

const BleCsControllerSessionState& BleCsControllerVprHost::sessionState() const {
  return host_.sessionState();
}

const BleCsControllerWorkflowState& BleCsControllerVprHost::workflowState() const {
  return host_.workflowState();
}

const BleCsSubeventResult& BleCsControllerVprHost::localResult() const {
  return host_.localResult();
}

const BleCsSubeventResult& BleCsControllerVprHost::peerResult() const {
  return host_.peerResult();
}

const BleCsSubeventResult& BleCsControllerVprHost::completedLocalResult() const {
  return host_.completedLocalResult();
}

const BleCsSubeventResult& BleCsControllerVprHost::completedPeerResult() const {
  return host_.completedPeerResult();
}

VprSharedTransportStream& BleCsControllerVprHost::transport() { return transport_; }

const VprSharedTransportStream& BleCsControllerVprHost::transport() const {
  return transport_;
}

bool BleCsControllerVprHost::drainDirectControllerEvents(VprControllerServiceHost* directHost,
                                                         const uint8_t* response,
                                                         size_t responseLen) {
  if (directHost == nullptr || !host_.hostState().began) {
    return true;
  }

  bool ok = true;
  if (response != nullptr && responseLen > 0U) {
    ok = host_.consumeControllerPacket(response, responseLen);
  }

  uint8_t packet[NRF54L15_VPR_TRANSPORT_MAX_VPR_DATA] = {0};
  size_t packetLen = 0U;
  while (ok && directHost->popPendingH4Event(packet, sizeof(packet), &packetLen)) {
    ok = host_.consumeControllerPacket(packet, packetLen);
  }

  uint8_t pollCount = 0U;
  while (ok && transport_.available() > 0 && pollCount < 8U) {
    ok = host_.pollController();
    ++pollCount;
  }
  return ok;
}

void BleCsControllerVprHost::syncVprState() {
  const BleCsControllerVprHostState previous = vprState_;
  BleCsControllerVprHostState nextState = previous;
  nextState.heartbeat = transport_.heartbeat();
  nextState.lastOpcode = transport_.lastOpcode();
  nextState.transportStatus = transport_.transportStatus();
  nextState.lastError = transport_.lastError();
  nextState.running = transport_.isRunning();
  nextState.secureAccessEnabled = transport_.secureAccessEnabled();
  const uint32_t packedLinkState = transport_.reservedState();
  const uint32_t packedAuxState = transport_.reservedAuxState();
  const uint32_t packedMetaState = transport_.reservedMetaState();
  const uint32_t packedConfigState = transport_.reservedConfigState();
  const uint8_t slotFlags = static_cast<uint8_t>((packedMetaState >> 24U) & 0xFFU);
  nextState.linkConnHandle = static_cast<uint16_t>(packedLinkState & 0x0FFFU);
  nextState.linkProcedureIntervalSelector =
      static_cast<uint8_t>((packedLinkState >> 12U) & 0x0FU);
  nextState.linkStoredConfigCount = static_cast<uint8_t>(packedAuxState & 0x0FU);
  nextState.linkPeerGapTicks = static_cast<uint8_t>((packedAuxState >> 8U) & 0x0FU);
  nextState.linkLastEvictedConfigId = static_cast<uint8_t>((packedAuxState >> 16U) & 0xFFU);
  nextState.linkAuthority2ConfigId = static_cast<uint8_t>((packedAuxState >> 24U) & 0xFFU);
  nextState.linkSessionOpen = (packedLinkState & (1UL << 16U)) != 0U;
  nextState.linkConfigCreated = (packedLinkState & (1UL << 17U)) != 0U;
  nextState.linkSecurityEnabled = (packedLinkState & (1UL << 18U)) != 0U;
  nextState.linkProcedureParamsApplied = (packedLinkState & (1UL << 19U)) != 0U;
  nextState.linkProcedureEnabled = (packedLinkState & (1UL << 20U)) != 0U;
  nextState.linkConfigId = static_cast<uint8_t>((packedLinkState >> 21U) & 0xFFU);
  nextState.linkSlot0ConfigId = static_cast<uint8_t>(packedMetaState & 0xFFU);
  nextState.linkSlot1ConfigId = static_cast<uint8_t>((packedMetaState >> 8U) & 0xFFU);
  nextState.linkPreviousConfigId = static_cast<uint8_t>((packedMetaState >> 16U) & 0xFFU);
  nextState.linkAuthority0ConfigId = static_cast<uint8_t>((packedConfigState >> 12U) & 0xFFU);
  nextState.linkAuthority1ConfigId = static_cast<uint8_t>((packedConfigState >> 20U) & 0xFFU);
  nextState.linkSlot0InUse = (slotFlags & 0x01U) != 0U;
  nextState.linkSlot1InUse = (slotFlags & 0x02U) != 0U;
  nextState.linkPreviousSlotInUse = (slotFlags & 0x04U) != 0U;
  nextState.linkActivePrimarySlotIndex = 0xFFU;
  if ((slotFlags & 0x08U) != 0U) {
    nextState.linkActivePrimarySlotIndex = 0U;
  } else if ((slotFlags & 0x10U) != 0U) {
    nextState.linkActivePrimarySlotIndex = 1U;
  }
  nextState.linkActiveConfigMirroredInPrevious = (slotFlags & 0x20U) != 0U;
  nextState.linkFreePrimarySlotCount = static_cast<uint8_t>((slotFlags >> 6U) & 0x03U);
  nextState.linkSlot0Runnable = (packedConfigState & 0x01U) != 0U;
  nextState.linkSlot1Runnable = (packedConfigState & 0x02U) != 0U;
  nextState.linkPreviousSlotRunnable = (packedConfigState & 0x04U) != 0U;
  nextState.linkSelectedConfigRunnable = (packedConfigState & 0x08U) != 0U;
  nextState.linkSlot0SecurityEnabled = (packedConfigState & 0x10U) != 0U;
  nextState.linkSlot1SecurityEnabled = (packedConfigState & 0x20U) != 0U;
  nextState.linkPreviousSlotSecurityEnabled = (packedConfigState & 0x40U) != 0U;
  nextState.linkSelectedConfigSecurityEnabled = (packedConfigState & 0x80U) != 0U;
  nextState.linkSlot0ProcedureParamsApplied = (packedConfigState & 0x100U) != 0U;
  nextState.linkSlot1ProcedureParamsApplied = (packedConfigState & 0x200U) != 0U;
  nextState.linkPreviousSlotProcedureParamsApplied = (packedConfigState & 0x400U) != 0U;
  nextState.linkSelectedConfigProcedureParamsApplied = (packedConfigState & 0x800U) != 0U;
  nextState.linkProcedureCounter = 0U;
  vprState_ = nextState;

  const bool linkSessionInvalidated =
      (previous.linkSessionOpen && !nextState.linkSessionOpen) ||
      (previous.linkConnHandle != 0U && previous.linkConnHandle != nextState.linkConnHandle);
  const bool linkConfigInvalidated =
      (previous.linkConfigCreated && !nextState.linkConfigCreated) ||
      (previous.linkConfigId != 0U && previous.linkConfigId != nextState.linkConfigId);
  if (linkSessionInvalidated || linkConfigInvalidated) {
    host_.resetProcedureRunState();
  }
  host_.reconcileReadyWorkflowShadow(nextState.linkConfigId, nextState.linkSessionOpen,
                                     nextState.linkConfigCreated,
                                     nextState.linkSecurityEnabled,
                                     nextState.linkProcedureParamsApplied,
                                     nextState.linkProcedureEnabled);
}

BleCsDfeCaptureInfo BleChannelSoundingRadio::lastDfeCaptureInfo() const {
  BleCsDfeCaptureInfo info{};
  info.present = (lastDfePacketAmountBytes_ > 0U);
  info.allZero = lastDfePacketAllZero_;
  info.amountBytes = lastDfePacketAmountBytes_;
  info.currentAmountBytes = lastDfePacketCurrentAmountBytes_;
  return info;
}

bool BleChannelSoundingRadio::copyLastDfePacket(uint8_t* outPacket,
                                                size_t maxLen,
                                                size_t* outLen) const {
  const size_t available =
      (lastDfePacketAmountBytes_ <= sizeof(dfePacket_))
          ? lastDfePacketAmountBytes_
          : sizeof(dfePacket_);
  const size_t copyLen = (available <= maxLen) ? available : maxLen;
  if (outLen != nullptr) {
    *outLen = copyLen;
  }
  if (outPacket == nullptr) {
    return false;
  }
  if (copyLen > 0U) {
    memcpy(outPacket, dfePacket_, copyLen);
  }
  return (copyLen == available);
}

bool BleChannelSoundingRadio::configureBle2MCommon() {
  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  (void)waitForRadioDisabled(radio_, kRadioDisableBudgetUs);
  radio_->TASKS_SOFTRESET = RADIO_TASKS_SOFTRESET_TASKS_SOFTRESET_Trigger;

  radio_->MODE = ((RADIO_MODE_MODE_Ble_2Mbit << RADIO_MODE_MODE_Pos) &
                  RADIO_MODE_MODE_Msk);
  radio_->TIMING = ((RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) &
                    RADIO_TIMING_RU_Msk);

  uint32_t pcnf0 = 0U;
  pcnf0 |= (8UL << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk;
  pcnf0 |= (1UL << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk;
  pcnf0 |= (8UL << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk;
  pcnf0 |= (RADIO_PCNF0_S1INCL_Automatic << RADIO_PCNF0_S1INCL_Pos) &
           RADIO_PCNF0_S1INCL_Msk;
  pcnf0 |= (RADIO_PCNF0_PLEN_16bit << RADIO_PCNF0_PLEN_Pos) &
           RADIO_PCNF0_PLEN_Msk;
  pcnf0 |= (RADIO_PCNF0_CRCINC_Exclude << RADIO_PCNF0_CRCINC_Pos) &
           RADIO_PCNF0_CRCINC_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_TERMLEN_Pos) & RADIO_PCNF0_TERMLEN_Msk;
  radio_->PCNF0 = pcnf0;

  uint32_t pcnf1 = 0U;
  pcnf1 |= (static_cast<uint32_t>(config_.maxPayloadLength)
            << RADIO_PCNF1_MAXLEN_Pos) &
           RADIO_PCNF1_MAXLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk;
  pcnf1 |= (3UL << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk;
  pcnf1 |= (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) &
           RADIO_PCNF1_ENDIAN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) &
           RADIO_PCNF1_WHITEEN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEOFFSET_Include << RADIO_PCNF1_WHITEOFFSET_Pos) &
           RADIO_PCNF1_WHITEOFFSET_Msk;
  radio_->PCNF1 = pcnf1;

  radio_->BASE0 = accessAddressBase(config_.accessAddress);
  radio_->PREFIX0 = (radio_->PREFIX0 & ~RADIO_PREFIX0_AP0_Msk) |
                    ((accessAddressPrefix(config_.accessAddress)
                      << RADIO_PREFIX0_AP0_Pos) &
                     RADIO_PREFIX0_AP0_Msk);
  radio_->TXADDRESS =
      (0UL << RADIO_TXADDRESS_TXADDRESS_Pos) & RADIO_TXADDRESS_TXADDRESS_Msk;
  radio_->RXADDRESSES =
      (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos) &
      RADIO_RXADDRESSES_ADDR0_Msk;

  uint32_t crccnf = 0U;
  crccnf |= (RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos) &
            RADIO_CRCCNF_LEN_Msk;
  crccnf |= (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos) &
            RADIO_CRCCNF_SKIPADDR_Msk;
  radio_->CRCCNF = crccnf;
  radio_->CRCPOLY = (kBleCrcPolynomial & RADIO_CRCPOLY_CRCPOLY_Msk);
  radio_->CRCINIT = (config_.crcInit & RADIO_CRCINIT_CRCINIT_Msk);

  radio_->TIFS = 150U;
  radio_->TXPOWER = ((txPowerRegFromDbm(config_.txPowerDbm)
                      << RADIO_TXPOWER_TXPOWER_Pos) &
                     RADIO_TXPOWER_TXPOWER_Msk);

  radio_->DFEPACKET.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dfePacket_)) &
      RADIO_DFEPACKET_PTR_PTR_Msk;
  radio_->DFEPACKET.MAXCNT =
      (sizeof(dfePacket_) << RADIO_DFEPACKET_MAXCNT_MAXCNT_Pos) &
      RADIO_DFEPACKET_MAXCNT_MAXCNT_Msk;
  for (uint8_t i = 0U; i < 2U; ++i) {
    radio_->AUXDATADMA[i].PTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&auxDataWords_[i * 2U])) &
        RADIO_AUXDATADMA_PTR_PTR_Msk;
    radio_->AUXDATADMA[i].MAXCNT =
        (2UL << RADIO_AUXDATADMA_MAXCNT_MAXCNT_Pos) &
        RADIO_AUXDATADMA_MAXCNT_MAXCNT_Msk;
    radio_->AUXDATADMA[i].ENABLE =
        (RADIO_AUXDATADMA_ENABLE_ENABLE_Disabled
         << RADIO_AUXDATADMA_ENABLE_ENABLE_Pos) &
        RADIO_AUXDATADMA_ENABLE_ENABLE_Msk;
    radio_->AUXDATA.CNF[i] = 0U;
  }
  configureRtt(false, false);

  return setLogicalChannel(config_.controlChannel);
}

bool BleChannelSoundingRadio::setLogicalChannel(uint8_t channelIndex) {
  if (!validLogicalChannel(channelIndex)) {
    return false;
  }

  const uint8_t freq = logicalChannelToFrequency(channelIndex);
  radio_->FREQUENCY =
      ((static_cast<uint32_t>(freq) << RADIO_FREQUENCY_FREQUENCY_Pos) &
       RADIO_FREQUENCY_FREQUENCY_Msk) |
      (0UL << RADIO_FREQUENCY_MAP_Pos);
  radio_->DATAWHITE = bleDataWhiteValue(channelIndex);
  return true;
}

void BleChannelSoundingRadio::configureRtt(bool enabled, bool reflectorRole) {
  if (!config_.enableRtt || !enabled) {
    radio_->RTT.CONFIG = 0U;
    return;
  }

  uint32_t config = 0U;
  config |= (RADIO_RTT_CONFIG_EN_Enabled << RADIO_RTT_CONFIG_EN_Pos) &
            RADIO_RTT_CONFIG_EN_Msk;
  config |= ((config_.rttFullAccessAddress ? RADIO_RTT_CONFIG_ENFULLAA_Enabled
                                           : RADIO_RTT_CONFIG_ENFULLAA_Disabled)
             << RADIO_RTT_CONFIG_ENFULLAA_Pos) &
            RADIO_RTT_CONFIG_ENFULLAA_Msk;
  config |= ((reflectorRole ? RADIO_RTT_CONFIG_ROLE_Reflector
                            : RADIO_RTT_CONFIG_ROLE_Initiator)
             << RADIO_RTT_CONFIG_ROLE_Pos) &
            RADIO_RTT_CONFIG_ROLE_Msk;
  config |= ((static_cast<uint32_t>(config_.rttNumSegments)
              << RADIO_RTT_CONFIG_NUMSEGMENTS_Pos) &
             RADIO_RTT_CONFIG_NUMSEGMENTS_Msk);
  config |= ((static_cast<uint32_t>(config_.rttEfsDelay)
              << RADIO_RTT_CONFIG_EFSDELAY_Pos) &
             RADIO_RTT_CONFIG_EFSDELAY_Msk);
  radio_->RTT.CONFIG = config;
  radio_->RTT.SEGMENT01 = 0U;
  radio_->RTT.SEGMENT23 = 0U;
  radio_->RTT.SEGMENT45 = 0U;
  radio_->RTT.SEGMENT67 = 0U;
}

void BleChannelSoundingRadio::prepareAuxDataCapture() {
  memset(auxDataWords_, 0, sizeof(auxDataWords_));
  radio_->EVENTS_AUXDATADMAEND = 0U;
  for (uint8_t i = 0U; i < 2U; ++i) {
    radio_->AUXDATA.CNF[i] =
        ((RADIO_AUXDATA_CNF_ACQMODE_Rtt << RADIO_AUXDATA_CNF_ACQMODE_Pos) &
         RADIO_AUXDATA_CNF_ACQMODE_Msk) |
        ((RADIO_AUXDATA_CNF_DIR_Acq << RADIO_AUXDATA_CNF_DIR_Pos) &
         RADIO_AUXDATA_CNF_DIR_Msk);
    radio_->AUXDATADMA[i].PTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&auxDataWords_[i * 2U])) &
        RADIO_AUXDATADMA_PTR_PTR_Msk;
    radio_->AUXDATADMA[i].MAXCNT =
        (2UL << RADIO_AUXDATADMA_MAXCNT_MAXCNT_Pos) &
        RADIO_AUXDATADMA_MAXCNT_MAXCNT_Msk;
    radio_->AUXDATADMA[i].ENABLE =
        (RADIO_AUXDATADMA_ENABLE_ENABLE_Enabled
         << RADIO_AUXDATADMA_ENABLE_ENABLE_Pos) &
        RADIO_AUXDATADMA_ENABLE_ENABLE_Msk;
  }
}

void BleChannelSoundingRadio::configureTxToneExtension() {
  radio_->DFEMODE = ((RADIO_DFEMODE_DFEOPMODE_AoD << RADIO_DFEMODE_DFEOPMODE_Pos) &
                     RADIO_DFEMODE_DFEOPMODE_Msk);

  uint32_t dfectrl1 = 0U;
  dfectrl1 |= (static_cast<uint32_t>(config_.cteTimeUnits)
               << RADIO_DFECTRL1_NUMBEROF8US_Pos) &
              RADIO_DFECTRL1_NUMBEROF8US_Msk;
  dfectrl1 |= (RADIO_DFECTRL1_DFEINEXTENSION_CRC
               << RADIO_DFECTRL1_DFEINEXTENSION_Pos) &
              RADIO_DFECTRL1_DFEINEXTENSION_Msk;
  dfectrl1 |= (RADIO_DFECTRL1_TSWITCHSPACING_2us
               << RADIO_DFECTRL1_TSWITCHSPACING_Pos) &
              RADIO_DFECTRL1_TSWITCHSPACING_Msk;
  dfectrl1 |= (RADIO_DFECTRL1_TSAMPLESPACINGREF_500ns
               << RADIO_DFECTRL1_TSAMPLESPACINGREF_Pos) &
              RADIO_DFECTRL1_TSAMPLESPACINGREF_Msk;
  dfectrl1 |= (RADIO_DFECTRL1_SAMPLETYPE_IQ
               << RADIO_DFECTRL1_SAMPLETYPE_Pos) &
              RADIO_DFECTRL1_SAMPLETYPE_Msk;
  dfectrl1 |= (RADIO_DFECTRL1_TSAMPLESPACING_500ns
               << RADIO_DFECTRL1_TSAMPLESPACING_Pos) &
              RADIO_DFECTRL1_TSAMPLESPACING_Msk;
  if (config_.dfeSwitchPatternCount > 0U) {
    dfectrl1 |= ((static_cast<uint32_t>(config_.dfeRepeatPattern)
                  << RADIO_DFECTRL1_REPEATPATTERN_Pos) &
                 RADIO_DFECTRL1_REPEATPATTERN_Msk);
  }
  radio_->DFECTRL1 = dfectrl1;
  radio_->DFECTRL2 =
      encodeSignedField(config_.dfeSwitchOffset16M,
                        13U,
                        RADIO_DFECTRL2_TSWITCHOFFSET_Msk,
                        RADIO_DFECTRL2_TSWITCHOFFSET_Pos) |
      encodeSignedField(config_.dfeSampleOffset16M,
                        12U,
                        RADIO_DFECTRL2_TSAMPLEOFFSET_Msk,
                        RADIO_DFECTRL2_TSAMPLEOFFSET_Pos);

  radio_->CLEARPATTERN = 1U;
  for (uint8_t i = 0U; i < config_.dfeSwitchPatternCount; ++i) {
    radio_->SWITCHPATTERN =
        (static_cast<uint32_t>(config_.dfeSwitchPattern[i])
         << RADIO_SWITCHPATTERN_SWITCHPATTERN_Pos) &
        RADIO_SWITCHPATTERN_SWITCHPATTERN_Msk;
  }

  const uint32_t numSamples = static_cast<uint32_t>(config_.cteTimeUnits) * 16UL;
  const uint32_t coeff =
      (65536UL + (static_cast<uint32_t>(config_.cteTimeUnits) / 2UL)) /
      static_cast<uint32_t>(config_.cteTimeUnits);
  radio_->CSTONES.MODE = ((RADIO_CSTONES_MODE_TPM_Enabled
                           << RADIO_CSTONES_MODE_TPM_Pos) &
                          RADIO_CSTONES_MODE_TPM_Msk) |
                         ((RADIO_CSTONES_MODE_TFM_Disabled
                           << RADIO_CSTONES_MODE_TFM_Pos) &
                          RADIO_CSTONES_MODE_TFM_Msk);
  radio_->CSTONES.NUMSAMPLES =
      (numSamples << RADIO_CSTONES_NUMSAMPLES_NUMSAMPLES_Pos) &
      RADIO_CSTONES_NUMSAMPLES_NUMSAMPLES_Msk;
  radio_->CSTONES.NEXTFREQUENCY = 0U;
  radio_->CSTONES.FAEPEER = 0U;
  radio_->CSTONES.PHASESHIFT = 0U;
  radio_->CSTONES.NUMSAMPLESCOEFF =
      (coeff << RADIO_CSTONES_NUMSAMPLESCOEFF_NUMSAMPLESCOEFF_Pos) &
      RADIO_CSTONES_NUMSAMPLESCOEFF_NUMSAMPLESCOEFF_Msk;
  radio_->CSTONES.DOWNSAMPLE =
      ((RADIO_CSTONES_DOWNSAMPLE_ENABLEFILTER_OFF
        << RADIO_CSTONES_DOWNSAMPLE_ENABLEFILTER_Pos) &
       RADIO_CSTONES_DOWNSAMPLE_ENABLEFILTER_Msk) |
      ((RADIO_CSTONES_DOWNSAMPLE_RATE_BLE2M
        << RADIO_CSTONES_DOWNSAMPLE_RATE_Pos) &
       RADIO_CSTONES_DOWNSAMPLE_RATE_Msk);
}

void BleChannelSoundingRadio::configureRxToneCapture() {
  configureTxToneExtension();
  radio_->DFEMODE = ((RADIO_DFEMODE_DFEOPMODE_AoA << RADIO_DFEMODE_DFEOPMODE_Pos) &
                     RADIO_DFEMODE_DFEOPMODE_Msk);

  uint32_t cteInline = 0U;
  cteInline |= (RADIO_CTEINLINECONF_CTEINLINECTRLEN_Enabled
                << RADIO_CTEINLINECONF_CTEINLINECTRLEN_Pos) &
               RADIO_CTEINLINECONF_CTEINLINECTRLEN_Msk;
  cteInline |= (RADIO_CTEINLINECONF_CTEINFOINS1_InS1
                << RADIO_CTEINLINECONF_CTEINFOINS1_Pos) &
               RADIO_CTEINLINECONF_CTEINFOINS1_Msk;
  cteInline |= (RADIO_CTEINLINECONF_CTEERRORHANDLING_No
                << RADIO_CTEINLINECONF_CTEERRORHANDLING_Pos) &
               RADIO_CTEINLINECONF_CTEERRORHANDLING_Msk;
  cteInline |= (RADIO_CTEINLINECONF_CTETIMEVALIDRANGE_20
                << RADIO_CTEINLINECONF_CTETIMEVALIDRANGE_Pos) &
               RADIO_CTEINLINECONF_CTETIMEVALIDRANGE_Msk;
  cteInline |= (RADIO_CTEINLINECONF_CTEINLINERXMODE1US_1us
                << RADIO_CTEINLINECONF_CTEINLINERXMODE1US_Pos) &
               RADIO_CTEINLINECONF_CTEINLINERXMODE1US_Msk;
  cteInline |= (RADIO_CTEINLINECONF_CTEINLINERXMODE2US_1us
                << RADIO_CTEINLINECONF_CTEINLINERXMODE2US_Pos) &
               RADIO_CTEINLINECONF_CTEINLINERXMODE2US_Msk;
  cteInline |= (static_cast<uint32_t>(config_.s0Pattern)
                << RADIO_CTEINLINECONF_S0CONF_Pos) &
               RADIO_CTEINLINECONF_S0CONF_Msk;
  cteInline |= (0xFFUL << RADIO_CTEINLINECONF_S0MASK_Pos) &
               RADIO_CTEINLINECONF_S0MASK_Msk;
  radio_->CTEINLINECONF = cteInline;
}

void BleChannelSoundingRadio::clearEvents() { clearRadioEvents(radio_); }

uint8_t BleChannelSoundingRadio::makeCteInfo() const {
  return static_cast<uint8_t>((kCteTypeAoA << 6U) |
                              (config_.cteTimeUnits & 0x1FU));
}

bool BleChannelSoundingRadio::sendFrame(uint8_t logicalChannel,
                                        PacketType type,
                                        uint8_t sequence,
                                        uint8_t channelIndex,
                                        uint8_t flags,
                                        const uint8_t* extra,
                                        uint8_t extraLen,
                                        bool enableRtt,
                                        bool rttReflectorRole) {
  if (!initialized_ || !validLogicalChannel(logicalChannel) ||
      !validDataChannel(channelIndex)) {
    return false;
  }

  const uint8_t payloadLen =
      static_cast<uint8_t>(kPayloadHeaderLen + extraLen);
  if (payloadLen == 0U || payloadLen > config_.maxPayloadLength) {
    return false;
  }
  if (extraLen > 0U && extra == nullptr) {
    return false;
  }

  configureTxToneExtension();
  configureRtt(enableRtt, rttReflectorRole);
  if (!setLogicalChannel(logicalChannel)) {
    return false;
  }

  txPacket_[0] = config_.s0Pattern;
  txPacket_[1] = payloadLen;
  txPacket_[2] = makeCteInfo();
  txPacket_[3] = kMagic0;
  txPacket_[4] = kMagic1;
  txPacket_[5] = static_cast<uint8_t>(type);
  txPacket_[6] = sequence;
  txPacket_[7] = channelIndex;
  txPacket_[8] = flags;
  if (extraLen > 0U) {
    memcpy(&txPacket_[9], extra, extraLen);
  }

  clearEvents();
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(txPacket_)) &
      RADIO_PACKETPTR_PTR_Msk;
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool endSeen = waitForRadioPhyEnd(radio_, kRadioEndBudgetUs);
  const bool disabled = waitForRadioDisabled(radio_, kRadioDisableBudgetUs);
  radio_->SHORTS = 0U;
  clearEvents();
  return endSeen && disabled;
}

bool BleChannelSoundingRadio::decodeFrame(const uint8_t* packet,
                                          size_t packetLen,
                                          int8_t rssiDbm,
                                          RxFrame* outFrame) const {
  if (packet == nullptr || outFrame == nullptr || packetLen < 3U + kPayloadHeaderLen) {
    return false;
  }
  if (packet[0] != config_.s0Pattern) {
    return false;
  }

  const uint8_t payloadLen = packet[1];
  if (payloadLen < kPayloadHeaderLen ||
      payloadLen > config_.maxPayloadLength ||
      packetLen < static_cast<size_t>(3U + payloadLen)) {
    return false;
  }

  const uint8_t* payload = &packet[3];
  if (payload[0] != kMagic0 || payload[1] != kMagic1) {
    return false;
  }

  const uint8_t typeValue = payload[2];
  if (typeValue != static_cast<uint8_t>(PacketType::kControl) &&
      typeValue != static_cast<uint8_t>(PacketType::kProbe) &&
      typeValue != static_cast<uint8_t>(PacketType::kReport)) {
    return false;
  }

  outFrame->valid = true;
  outFrame->type = static_cast<PacketType>(typeValue);
  outFrame->sequence = payload[3];
  outFrame->channelIndex = payload[4];
  outFrame->flags = payload[5];
  outFrame->rssiDbm = rssiDbm;
  outFrame->extraLen = static_cast<uint8_t>(payloadLen - kPayloadHeaderLen);
  if (outFrame->extraLen > sizeof(outFrame->extra)) {
    return false;
  }
  if (outFrame->extraLen > 0U) {
    memcpy(outFrame->extra, &payload[kPayloadHeaderLen], outFrame->extraLen);
  }
  return validDataChannel(outFrame->channelIndex);
}

void BleChannelSoundingRadio::encodeReportExtra(const BleCsToneSample& tone,
                                                uint8_t* outExtra) const {
  if (outExtra == nullptr) {
    return;
  }

  writeLe16(&outExtra[0], static_cast<uint16_t>(tone.i));
  writeLe16(&outExtra[2], static_cast<uint16_t>(tone.q));
  writeLe16(&outExtra[4], tone.magnitude);
  writeLe16(&outExtra[6], tone.magnitudeStd);
  outExtra[8] = tone.cteTimeUnits;
  outExtra[9] = tone.cteType;
  outExtra[10] = static_cast<uint8_t>(tone.rssiDbm);
}

void BleChannelSoundingRadio::decodeReportExtra(const uint8_t* extra,
                                                uint8_t extraLen,
                                                BleCsToneSample* outTone) const {
  if (extra == nullptr || outTone == nullptr || extraLen < kReportToneExtraLen) {
    return;
  }

  outTone->i = static_cast<int16_t>(readLe16(&extra[0]));
  outTone->q = static_cast<int16_t>(readLe16(&extra[2]));
  outTone->magnitude = readLe16(&extra[4]);
  outTone->magnitudeStd = readLe16(&extra[6]);
  outTone->cteTimeUnits = extra[8];
  outTone->cteType = extra[9];
  outTone->rssiDbm = static_cast<int8_t>(extra[10]);
  outTone->valid = (outTone->magnitude >= config_.minToneMagnitude) &&
                   ((outTone->i != 0) || (outTone->q != 0));
}

void BleChannelSoundingRadio::encodeRttExtra(const BleCsRttSample& rtt,
                                             uint8_t* outExtra) const {
  if (outExtra == nullptr) {
    return;
  }

  outExtra[0] = rtt.rawLen;
  memset(&outExtra[1], 0, kReportRttExtraLen - 1U);
  const uint8_t copyLen =
      (rtt.rawLen <= sizeof(rtt.rawBytes)) ? rtt.rawLen : sizeof(rtt.rawBytes);
  if (copyLen > 0U) {
    memcpy(&outExtra[1], rtt.rawBytes, copyLen);
  }
}

void BleChannelSoundingRadio::parseRttRaw(BleCsRttSample* outRtt) const {
  if (outRtt == nullptr || outRtt->rawLen == 0U) {
    return;
  }

  outRtt->present = true;
  outRtt->valid = false;

  // Keep raw AUXDATA bytes for debug, but do not derive RTT timing fields
  // from an inferred layout that does not match the observed hardware output.
  if (rawBytesAllZero(outRtt->rawBytes, outRtt->rawLen)) {
    outRtt->present = false;
  }
}

void BleChannelSoundingRadio::decodeRttExtra(const uint8_t* extra,
                                             uint8_t extraLen,
                                             BleCsRttSample* outRtt) const {
  if (extra == nullptr || outRtt == nullptr || extraLen < kReportRttExtraLen) {
    return;
  }

  outRtt->rawLen = extra[0];
  if (outRtt->rawLen > sizeof(outRtt->rawBytes)) {
    outRtt->rawLen = sizeof(outRtt->rawBytes);
  }
  if (outRtt->rawLen > (extraLen - 1U)) {
    outRtt->rawLen = static_cast<uint8_t>(extraLen - 1U);
  }
  memset(outRtt->rawBytes, 0, sizeof(outRtt->rawBytes));
  if (outRtt->rawLen > 0U) {
    memcpy(outRtt->rawBytes, &extra[1], outRtt->rawLen);
  }
  parseRttRaw(outRtt);
}

void BleChannelSoundingRadio::captureAuxDataRtt(BleCsRttSample* outRtt) {
  if (outRtt == nullptr) {
    return;
  }

  outRtt->present = false;
  outRtt->valid = false;
  outRtt->rawLen = 0U;
  memset(outRtt->rawBytes, 0, sizeof(outRtt->rawBytes));

  BleCsRttSample candidates[2] = {};
  for (uint8_t i = 0U; i < 2U; ++i) {
    const uint32_t amountWords =
        (radio_->AUXDATADMA[i].AMOUNT & RADIO_AUXDATADMA_AMOUNT_AMOUNT_Msk) >>
        RADIO_AUXDATADMA_AMOUNT_AMOUNT_Pos;
    const uint8_t byteCount = static_cast<uint8_t>(
        ((amountWords * sizeof(uint32_t)) <= sizeof(candidates[i].rawBytes))
            ? (amountWords * sizeof(uint32_t))
            : sizeof(candidates[i].rawBytes));
    if (byteCount == 0U) {
      continue;
    }

    candidates[i].present = true;
    candidates[i].rawLen = byteCount;
    memcpy(candidates[i].rawBytes, &auxDataWords_[i * 2U], byteCount);
    parseRttRaw(&candidates[i]);
  }

  uint8_t selected = 0U;
  if (candidates[1].present) {
    bool firstAllZero = true;
    bool secondAllZero = true;
    for (uint8_t i = 0U; i < candidates[0].rawLen; ++i) {
      if (candidates[0].rawBytes[i] != 0U) {
        firstAllZero = false;
        break;
      }
    }
    for (uint8_t i = 0U; i < candidates[1].rawLen; ++i) {
      if (candidates[1].rawBytes[i] != 0U) {
        secondAllZero = false;
        break;
      }
    }

    if ((!candidates[0].present && candidates[1].present) ||
        (firstAllZero && !secondAllZero) ||
        (!candidates[0].valid && candidates[1].valid)) {
      selected = 1U;
    }
  }

  if (!candidates[selected].present) {
    return;
  }

  *outRtt = candidates[selected];
}

void BleChannelSoundingRadio::resetDfeCaptureState() {
  lastDfePacketAmountBytes_ = 0U;
  lastDfePacketCurrentAmountBytes_ = 0U;
  lastDfePacketAllZero_ = true;
}

void BleChannelSoundingRadio::updateDfeCaptureState() {
  const uint16_t amount = static_cast<uint16_t>(
      (radio_->DFEPACKET.AMOUNT & RADIO_DFEPACKET_AMOUNT_AMOUNT_Msk) >>
      RADIO_DFEPACKET_AMOUNT_AMOUNT_Pos);
  const uint16_t currentAmount = static_cast<uint16_t>(
      (radio_->DFEPACKET.CURRENTAMOUNT & RADIO_DFEPACKET_CURRENTAMOUNT_AMOUNT_Msk) >>
      RADIO_DFEPACKET_CURRENTAMOUNT_AMOUNT_Pos);
  const uint8_t cappedAmount = static_cast<uint8_t>(
      (amount <= sizeof(dfePacket_)) ? amount : sizeof(dfePacket_));
  lastDfePacketAmountBytes_ = amount;
  lastDfePacketCurrentAmountBytes_ = currentAmount;
  lastDfePacketAllZero_ = rawBytesAllZero(dfePacket_, cappedAmount);
}

bool BleChannelSoundingRadio::receiveFrame(uint8_t logicalChannel,
                                           uint32_t listenWindowUs,
                                           bool captureTone,
                                           bool captureRtt,
                                           bool rttReflectorRole,
                                           RxFrame* outFrame,
                                           BleCsToneSample* outTone,
                                           BleCsRttSample* outRtt) {
  if (!initialized_ || !validLogicalChannel(logicalChannel) || outFrame == nullptr) {
    return false;
  }

  *outFrame = RxFrame{};
  if (outTone != nullptr) {
    *outTone = BleCsToneSample{};
  }
  if (outRtt != nullptr) {
    *outRtt = BleCsRttSample{};
  }
  resetDfeCaptureState();

  configureRxToneCapture();
  configureRtt(captureRtt, rttReflectorRole);
  if (!setLogicalChannel(logicalChannel)) {
    return false;
  }

  memset(rxPacket_, 0, sizeof(rxPacket_));
  clearEvents();
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rxPacket_)) &
      RADIO_PACKETPTR_PTR_Msk;

  uint32_t shorts =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk);
  if (!captureTone) {
    shorts |= ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
                << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
               RADIO_SHORTS_PHYEND_DISABLE_Msk);
  }
  radio_->SHORTS = shorts;
  if (captureRtt && outRtt != nullptr) {
    prepareAuxDataCapture();
    radio_->TASKS_AUXDATADMASTART =
        RADIO_TASKS_AUXDATADMASTART_TASKS_AUXDATADMASTART_Trigger;
  } else {
    for (uint8_t i = 0U; i < 2U; ++i) {
      radio_->AUXDATADMA[i].ENABLE =
          (RADIO_AUXDATADMA_ENABLE_ENABLE_Disabled
           << RADIO_AUXDATADMA_ENABLE_ENABLE_Pos) &
          RADIO_AUXDATADMA_ENABLE_ENABLE_Msk;
      radio_->AUXDATA.CNF[i] = 0U;
    }
  }
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;

  if (!waitForCrcDone(radio_, listenWindowUs)) {
    if (captureRtt) {
      radio_->TASKS_AUXDATADMASTOP =
          RADIO_TASKS_AUXDATADMASTOP_TASKS_AUXDATADMASTOP_Trigger;
      for (uint8_t i = 0U; i < 2U; ++i) {
        radio_->AUXDATADMA[i].ENABLE =
            (RADIO_AUXDATADMA_ENABLE_ENABLE_Disabled
             << RADIO_AUXDATADMA_ENABLE_ENABLE_Pos) &
            RADIO_AUXDATADMA_ENABLE_ENABLE_Msk;
        radio_->AUXDATA.CNF[i] = 0U;
      }
    }
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    (void)waitForRadioDisabled(radio_, kRadioDisableBudgetUs);
    radio_->SHORTS = 0U;
    configureRtt(false, false);
    clearEvents();
    return false;
  }

  const bool crcOk = (radio_->EVENTS_CRCOK != 0U) ||
                     (((radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
                       RADIO_CRCSTATUS_CRCSTATUS_Pos) ==
                      RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  const int8_t rssiDbm = radioRssiDbm(radio_);

  if (!captureTone) {
    (void)waitForRadioDisabled(radio_, kRadioDisableBudgetUs);
  } else {
    (void)waitForRadioPhyEnd(radio_, kRadioEndBudgetUs);
    if (config_.enableRawDfeCapture) {
      updateDfeCaptureState();
    }
    if (crcOk && outTone != nullptr && (radio_->EVENTS_CTEPRESENT != 0U)) {
      radio_->EVENTS_CSTONESEND = 0U;
      radio_->TASKS_CSTONESSTART = RADIO_TASKS_CSTONESSTART_TASKS_CSTONESSTART_Trigger;
      const bool cstReady = waitForFlag(&radio_->EVENTS_CSTONESEND, kCstStartBudgetUs);
      if (cstReady) {
        const uint32_t pct16 = radio_->CSTONES.PCT16;
        const uint32_t magPhase = radio_->CSTONES.MAGPHASEMEAN;
        outTone->i = static_cast<int16_t>(pct16 & 0xFFFFU);
        outTone->q = static_cast<int16_t>((pct16 >> 16U) & 0xFFFFU);
        outTone->magnitude =
            static_cast<uint16_t>((magPhase >> 16U) & 0xFFFFU);
        outTone->phase = static_cast<uint16_t>(magPhase & 0xFFFFU);
        outTone->magnitudeStd = static_cast<uint16_t>(
            (radio_->CSTONES.MAGSTD & RADIO_CSTONES_MAGSTD_MAGSTD_Msk) >>
            RADIO_CSTONES_MAGSTD_MAGSTD_Pos);
        outTone->rssiDbm = rssiDbm;
        outTone->cteTimeUnits = static_cast<uint8_t>(
            (radio_->CTESTATUS & RADIO_CTESTATUS_CTETIME_Msk) >>
            RADIO_CTESTATUS_CTETIME_Pos);
        outTone->cteType = static_cast<uint8_t>(
            (radio_->CTESTATUS & RADIO_CTESTATUS_CTETYPE_Msk) >>
            RADIO_CTESTATUS_CTETYPE_Pos);
        outTone->valid = crcOk &&
                         (outTone->magnitude >= config_.minToneMagnitude) &&
                         ((outTone->i != 0) || (outTone->q != 0));
      }
    }

    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    (void)waitForRadioDisabled(radio_, kRadioDisableBudgetUs);
  }

  if (captureRtt) {
    (void)waitForFlag(&radio_->EVENTS_AUXDATADMAEND, kAuxDataBudgetUs);
    if (outRtt != nullptr) {
      captureAuxDataRtt(outRtt);
    }
    radio_->TASKS_AUXDATADMASTOP =
        RADIO_TASKS_AUXDATADMASTOP_TASKS_AUXDATADMASTOP_Trigger;
    for (uint8_t i = 0U; i < 2U; ++i) {
      radio_->AUXDATADMA[i].ENABLE =
          (RADIO_AUXDATADMA_ENABLE_ENABLE_Disabled
           << RADIO_AUXDATADMA_ENABLE_ENABLE_Pos) &
          RADIO_AUXDATADMA_ENABLE_ENABLE_Msk;
      radio_->AUXDATA.CNF[i] = 0U;
    }
  }

  radio_->SHORTS = 0U;
  configureRtt(false, false);

  bool decoded = false;
  if (crcOk) {
    const size_t frameLen = static_cast<size_t>(3U + rxPacket_[1]);
    decoded = decodeFrame(rxPacket_, frameLen, rssiDbm, outFrame);
  }

  clearEvents();
  return crcOk && decoded;
}

bool BleChannelSoundingRadio::measureChannel(uint8_t channelIndex,
                                             uint8_t sequence,
                                             BleCsChannelMeasurement* outMeasurement) {
  if (!initialized_ || outMeasurement == nullptr || !validDataChannel(channelIndex)) {
    return false;
  }

  *outMeasurement = BleCsChannelMeasurement{};
  outMeasurement->channelIndex = channelIndex;
  outMeasurement->sequence = sequence;

  for (uint8_t attempt = 0U; attempt < config_.probeRetries; ++attempt) {
    if (!sendFrame(config_.controlChannel, PacketType::kControl, sequence,
                   channelIndex, 0U, nullptr, 0U, false, false)) {
      continue;
    }

    delayMicroseconds(config_.controlToProbeDelayUs);

    if (!sendFrame(channelIndex, PacketType::kProbe, sequence, channelIndex,
                   0U, nullptr, 0U, config_.enableRtt, false)) {
      continue;
    }

    RxFrame report{};
    BleCsToneSample localTone{};
    BleCsRttSample localRtt{};
    if (!receiveFrame(channelIndex, config_.responseListenWindowUs, true,
                      config_.enableRtt, false, &report, &localTone, &localRtt)) {
      continue;
    }
    if (!report.valid || report.type != PacketType::kReport ||
        report.sequence != sequence || report.channelIndex != channelIndex) {
      continue;
    }

    BleCsToneSample peerTone{};
    BleCsRttSample peerRtt{};
    if ((report.flags & 0x01U) != 0U) {
      decodeReportExtra(report.extra, report.extraLen, &peerTone);
    }
    if ((report.flags & 0x02U) != 0U && report.extraLen >= kReportExtraLen) {
      decodeRttExtra(&report.extra[kReportToneExtraLen],
                     static_cast<uint8_t>(report.extraLen - kReportToneExtraLen),
                     &peerRtt);
    }

    outMeasurement->localTone = localTone;
    outMeasurement->peerTone = peerTone;
    outMeasurement->localRtt = localRtt;
    outMeasurement->peerRtt = peerRtt;
    outMeasurement->combinedPhaseRad = combinedPhaseRad(*outMeasurement);
    (void)rttDistanceMeters(*outMeasurement, &outMeasurement->rttDistanceMeters);
    outMeasurement->valid =
        (localTone.valid && peerTone.valid) || (localRtt.valid && peerRtt.valid);
    return outMeasurement->valid;
  }

  return false;
}

bool BleChannelSoundingRadio::listenAndReflectOnce(uint32_t controlListenWindowUs) {
  if (!initialized_) {
    return false;
  }

  const uint32_t windowUs =
      (controlListenWindowUs != 0U) ? controlListenWindowUs
                                    : config_.controlListenWindowUs;

  RxFrame control{};
  if (!receiveFrame(config_.controlChannel, windowUs, false, false, false, &control,
                    nullptr, nullptr)) {
    return false;
  }
  if (!control.valid || control.type != PacketType::kControl ||
      !validDataChannel(control.channelIndex)) {
    return false;
  }

  RxFrame probe{};
  BleCsToneSample localTone{};
  BleCsRttSample localRtt{};
  if (!receiveFrame(control.channelIndex, config_.probeListenWindowUs, true,
                    config_.enableRtt, true, &probe, &localTone, &localRtt)) {
    return false;
  }
  if (!probe.valid || probe.type != PacketType::kProbe ||
      probe.sequence != control.sequence ||
      probe.channelIndex != control.channelIndex) {
    return false;
  }

  uint8_t reportExtra[kReportExtraLen] = {0};
  encodeReportExtra(localTone, reportExtra);
  encodeRttExtra(localRtt, &reportExtra[kReportToneExtraLen]);
  uint8_t flags = localTone.valid ? 0x01U : 0x00U;
  if (localRtt.present) {
    flags |= 0x02U;
  }
  if (config_.probeToReportDelayUs > 0U) {
    delayMicroseconds(config_.probeToReportDelayUs);
  }

  return sendFrame(probe.channelIndex, PacketType::kReport, probe.sequence,
                   probe.channelIndex, flags, reportExtra, sizeof(reportExtra),
                   config_.enableRtt, true);
}

float BleChannelSoundingRadio::combinedPhaseRad(
    const BleCsChannelMeasurement& measurement) {
  if (!measurement.localTone.valid || !measurement.peerTone.valid) {
    return 0.0f;
  }

  const float localI = static_cast<float>(measurement.localTone.i);
  const float localQ = static_cast<float>(measurement.localTone.q);
  const float peerI = static_cast<float>(measurement.peerTone.i);
  const float peerQ = static_cast<float>(measurement.peerTone.q);
  const float combI = (localI * peerI) - (localQ * peerQ);
  const float combQ = (localI * peerQ) + (peerI * localQ);
  return atan2f(combQ, combI);
}

bool BleChannelSoundingRadio::rttDistanceMeters(
    const BleCsChannelMeasurement& measurement, float* outDistanceMeters) {
  if (outDistanceMeters == nullptr) {
    return false;
  }

  *outDistanceMeters = 0.0f;
  if (!measurement.localRtt.valid || !measurement.peerRtt.valid) {
    return false;
  }

  const int32_t roundTripHalfNs =
      static_cast<int32_t>(measurement.localRtt.timeDifferenceHalfNs) -
      static_cast<int32_t>(measurement.peerRtt.timeDifferenceHalfNs);
  if (roundTripHalfNs <= 0) {
    return false;
  }

  const float tofNs = static_cast<float>(roundTripHalfNs) * 0.25f;
  const float distance =
      tofNs * (kSpeedOfLightMetersPerSecond / 1000000000.0f);
  if (!isfinite(distance) || distance <= 0.0f) {
    return false;
  }

  *outDistanceMeters = distance;
  return true;
}

bool BleChannelSoundingRadio::estimateDistancePhaseSlope(
    const BleCsChannelMeasurement* measurements, size_t count,
    BleCsEstimate* outEstimate) {
  if (measurements == nullptr || outEstimate == nullptr) {
    return false;
  }

  *outEstimate = BleCsEstimate{};
  outEstimate->phaseSlopeDistanceMeters = NAN;
  outEstimate->rttDistanceMeters = NAN;
  outEstimate->distanceMeters = NAN;

  float freqsHz[kMaxCsChannels] = {0.0f};
  float phases[kMaxCsChannels] = {0.0f};
  float toneQuality[kMaxCsChannels] = {0.0f};
  size_t phaseCount = 0U;

  for (size_t i = 0U; i < count && phaseCount < kMaxCsChannels; ++i) {
    if (!measurements[i].valid || !measurements[i].localTone.valid ||
        !measurements[i].peerTone.valid || !validDataChannel(measurements[i].channelIndex)) {
      continue;
    }

    freqsHz[phaseCount] =
        (2400.0f + static_cast<float>(logicalChannelToFrequency(
                        measurements[i].channelIndex))) *
        1000000.0f;
    phases[phaseCount] = combinedPhaseRad(measurements[i]);
    toneQuality[phaseCount] =
        toneQualityScore(measurements[i].localTone, measurements[i].peerTone);
    ++phaseCount;
  }
  outEstimate->totalToneChannels = static_cast<uint8_t>(phaseCount);

  bool phaseOk = false;
  if (phaseCount >= 8U) {
    sortPhaseSamplesWithQuality(freqsHz, phases, toneQuality, phaseCount);

    float qualityScratch[kMaxCsChannels] = {0.0f};
    for (size_t i = 0U; i < phaseCount; ++i) {
      qualityScratch[i] = toneQuality[i];
    }
    const float medianQuality = medianInPlace(qualityScratch, phaseCount);
    outEstimate->medianToneQuality = medianQuality;

    const float qualityThreshold = fmaxf(0.35f, medianQuality * 0.35f);
    float qualityFreqs[kMaxCsChannels] = {0.0f};
    float qualityPhases[kMaxCsChannels] = {0.0f};
    float qualityScores[kMaxCsChannels] = {0.0f};
    size_t qualityCount = 0U;
    for (size_t i = 0U; i < phaseCount; ++i) {
      if (toneQuality[i] < qualityThreshold) {
        continue;
      }
      qualityFreqs[qualityCount] = freqsHz[i];
      qualityPhases[qualityCount] = phases[i];
      qualityScores[qualityCount] = toneQuality[i];
      ++qualityCount;
    }

    if (qualityCount >= 8U && qualityCount < phaseCount) {
      outEstimate->rejectedLowQualityChannels =
          static_cast<uint8_t>(phaseCount - qualityCount);
      phaseCount = qualityCount;
      for (size_t i = 0U; i < phaseCount; ++i) {
        freqsHz[i] = qualityFreqs[i];
        phases[i] = qualityPhases[i];
        toneQuality[i] = qualityScores[i];
      }
    }

    for (size_t i = 1U; i < phaseCount; ++i) {
      const float prev = phases[i - 1U];
      while ((phases[i] - prev) > kPi) {
        phases[i] -= 2.0f * kPi;
      }
      while ((phases[i] - prev) < -kPi) {
        phases[i] += 2.0f * kPi;
      }
    }

    float slope = 0.0f;
    float intercept = 0.0f;
    if (fitTheilSenLine(freqsHz, phases, phaseCount, &slope, &intercept)) {
      float residuals[kMaxCsChannels] = {0.0f};
      float absResiduals[kMaxCsChannels] = {0.0f};
      for (size_t i = 0U; i < phaseCount; ++i) {
        residuals[i] = phases[i] - (intercept + (slope * freqsHz[i]));
        absResiduals[i] = fabsf(residuals[i]);
      }

      const float mad = medianInPlace(absResiduals, phaseCount);
      const float inlierThreshold = fmaxf(0.45f, mad * 3.0f);

      float inlierFreqs[kMaxCsChannels] = {0.0f};
      float inlierPhases[kMaxCsChannels] = {0.0f};
      float inlierQuality[kMaxCsChannels] = {0.0f};
      size_t inlierCount = 0U;
      for (size_t i = 0U; i < phaseCount; ++i) {
        if (fabsf(residuals[i]) <= inlierThreshold) {
          inlierFreqs[inlierCount] = freqsHz[i];
          inlierPhases[inlierCount] = phases[i];
          inlierQuality[inlierCount] = toneQuality[i];
          ++inlierCount;
        }
      }

      if (inlierCount >= 8U && inlierCount < phaseCount &&
          fitTheilSenLine(inlierFreqs, inlierPhases, inlierCount, &slope, &intercept)) {
        outEstimate->rejectedResidualChannels =
            static_cast<uint8_t>(phaseCount - inlierCount);
        phaseCount = inlierCount;
        for (size_t i = 0U; i < phaseCount; ++i) {
          freqsHz[i] = inlierFreqs[i];
          phases[i] = inlierPhases[i];
          toneQuality[i] = inlierQuality[i];
        }
      }

      float refinedSlope = slope;
      float refinedIntercept = intercept;
      if (fitWeightedLine(freqsHz, phases, toneQuality, phaseCount, &refinedSlope,
                          &refinedIntercept)) {
        const float robustDistance = fabsf(
            -(kSpeedOfLightMetersPerSecond * slope) / (4.0f * kPi));
        const float refinedDistance = fabsf(
            -(kSpeedOfLightMetersPerSecond * refinedSlope) / (4.0f * kPi));
        outEstimate->fitDeltaMeters = fabsf(refinedDistance - robustDistance);
        slope = refinedSlope;
        intercept = refinedIntercept;
      }

      float residualSse = 0.0f;
      for (size_t i = 0U; i < phaseCount; ++i) {
        const float err = phases[i] - (intercept + (slope * freqsHz[i]));
        residualSse += err * err;
      }

      const float phaseDistance = fabsf(
          -(kSpeedOfLightMetersPerSecond * slope) / (4.0f * kPi));
      if (isfinite(phaseDistance) && phaseDistance > 0.0f) {
        phaseOk = true;
        outEstimate->usedChannels = static_cast<uint8_t>(phaseCount);
        outEstimate->phaseSlopeDistanceMeters = phaseDistance;
        outEstimate->slopeRadPerHz = slope;
        outEstimate->residualVariance =
            residualSse / static_cast<float>(phaseCount);
      }
    }
  }

  float rttDistances[kMaxCsChannels] = {0.0f};
  size_t rttCount = 0U;
  for (size_t i = 0U; i < count && rttCount < kMaxCsChannels; ++i) {
    float distance = 0.0f;
    if (rttDistanceMeters(measurements[i], &distance)) {
      rttDistances[rttCount++] = distance;
    }
  }

  bool rttOk = false;
  if (rttCount >= 4U) {
    float rttMedian = 0.0f;
    float rttMad = 0.0f;
    if (estimateMedianAndMad(rttDistances, rttCount, &rttMedian, &rttMad)) {
      const float inlierThreshold = fmaxf(0.20f, rttMad * 3.0f);
      float inliers[kMaxCsChannels] = {0.0f};
      size_t inlierCount = 0U;
      for (size_t i = 0U; i < rttCount; ++i) {
        if (fabsf(rttDistances[i] - rttMedian) <= inlierThreshold) {
          inliers[inlierCount++] = rttDistances[i];
        }
      }

      if (inlierCount >= 3U &&
          estimateMedianAndMad(inliers, inlierCount, &rttMedian, &rttMad)) {
        float varianceAccum = 0.0f;
        for (size_t i = 0U; i < inlierCount; ++i) {
          const float err = inliers[i] - rttMedian;
          varianceAccum += err * err;
        }
        rttOk = true;
        outEstimate->rttChannels = static_cast<uint8_t>(inlierCount);
        outEstimate->rttDistanceMeters = rttMedian;
        outEstimate->rttVariance =
            varianceAccum / static_cast<float>(inlierCount);
      }
    }
  }

  if (!phaseOk && !rttOk) {
    return false;
  }

  outEstimate->valid = true;
  if (rttOk && phaseOk) {
    const float delta = fabsf(outEstimate->rttDistanceMeters -
                              outEstimate->phaseSlopeDistanceMeters);
    if (delta <= fmaxf(0.25f, sqrtf(outEstimate->rttVariance) + 0.20f)) {
      outEstimate->distanceMeters =
          (0.65f * outEstimate->rttDistanceMeters) +
          (0.35f * outEstimate->phaseSlopeDistanceMeters);
    } else {
      outEstimate->distanceMeters = outEstimate->rttDistanceMeters;
    }
  } else if (rttOk) {
    outEstimate->distanceMeters = outEstimate->rttDistanceMeters;
  } else {
    outEstimate->distanceMeters = outEstimate->phaseSlopeDistanceMeters;
  }

  return isfinite(outEstimate->distanceMeters) &&
         (outEstimate->distanceMeters > 0.0f);
}

}  // namespace xiao_nrf54l15
