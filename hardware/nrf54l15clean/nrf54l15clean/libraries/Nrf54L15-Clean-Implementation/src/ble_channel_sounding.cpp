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

inline uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8U);
}

inline void writeLe16(uint8_t* data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
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
      count > kMaxCsChannels) {
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

void sortPhaseSamples(float* freqsHz, float* phases, size_t count) {
  if (freqsHz == nullptr || phases == nullptr || count < 2U) {
    return;
  }

  for (size_t i = 1U; i < count; ++i) {
    const float freq = freqsHz[i];
    const float phase = phases[i];
    size_t j = i;
    while (j > 0U && freqsHz[j - 1U] > freq) {
      freqsHz[j] = freqsHz[j - 1U];
      phases[j] = phases[j - 1U];
      --j;
    }
    freqsHz[j] = freq;
    phases[j] = phase;
  }
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

}  // namespace

BleChannelSoundingRadio::BleChannelSoundingRadio(uint32_t radioBase)
    : radio_(reinterpret_cast<NRF_RADIO_Type*>(static_cast<uintptr_t>(radioBase))),
      power_(),
      config_(),
      initialized_(false),
      txPacket_{0},
      rxPacket_{0},
      dfePacket_{0},
      auxDataWords_{0} {}

bool BleChannelSoundingRadio::begin(const BleCsConfig& config) {
  if (radio_ == nullptr) {
    initialized_ = false;
    return false;
  }
  if (!validLogicalChannel(config.controlChannel) || config.maxPayloadLength == 0U ||
      config.maxPayloadLength > 48U || config.cteTimeUnits < 2U ||
      config.cteTimeUnits > 10U) {
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
  radio_->DFECTRL1 = dfectrl1;
  radio_->DFECTRL2 = 0U;

  radio_->CLEARPATTERN = 1U;

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
