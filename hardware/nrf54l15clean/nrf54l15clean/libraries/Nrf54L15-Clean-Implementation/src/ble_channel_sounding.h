#pragma once

#include <stddef.h>
#include <stdint.h>

#include "nrf54l15_hal.h"

namespace xiao_nrf54l15 {

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
};

}  // namespace xiao_nrf54l15
