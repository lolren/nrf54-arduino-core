/*
 * BleChannelSoundingInitiator
 *
 * Measures distance to a peer (BleChannelSoundingReflector) using BLE
 * Channel Sounding (CS), a BLE 6.0 feature that estimates distance from
 * the phase shift of tone exchanges across multiple radio channels.
 *
 * How it works (simplified):
 *   1. For each of 37 channels, the Initiator sends a probe tone and the
 *      Reflector sends one back. The round-trip phase difference reveals
 *      the frequency-dependent delay, which corresponds to distance.
 *   2. A linear regression over all 37 channels (phase-slope method)
 *      gives a distance estimate in metres.
 *   3. A 5-sample sliding median filter smooths the output.
 *
 * Serial commands (type in Arduino Serial Monitor):
 *   status          – print current calibration parameters
 *   clear           – reset calibration to defaults
 *   zero            – set offset so current reading = 0 m
 *   ref <m>         – set offset so current reading = <m> metres
 *   offset <m>      – manually set additive offset in metres
 *   scale <factor>  – manually set multiplicative scale factor
 *
 * Must be paired with BleChannelSoundingReflector on a second board.
 * Both boards must share the same control channel (default: ch 37).
 *
 * Note: RTT (Round-Trip Time) ranging is disabled in this core build.
 */

#include <Arduino.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ble_channel_sounding.h"
#include "nrf54l15_hal.h"
#include "nrf54l15_vpr.h"
#include "nrf54l15_vpr_transport_shared.h"

#ifdef ledOn
#undef ledOn
#endif
#ifdef ledOff
#undef ledOff
#endif

using namespace xiao_nrf54l15;

namespace {

// kCeramic: use on-board antenna. kExternal: requires physical external antenna.
static constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
// Number of BLE data channels to sweep per distance estimate (0–36 = 37 channels).
static constexpr uint8_t kSweepChannelCount = 37U;
// Number of recent distance estimates kept in the sliding median filter.
static constexpr uint8_t kMedianWindow = 5U;
// Default calibration: scale=1.0 means no scaling, offset=0.0 means no shift.
static constexpr float kCalibrationScaleDefault = 1.0f;
static constexpr float kCalibrationOffsetMetersDefault = 0.0f;

static BleChannelSoundingRadio gCs;
static BleCsChannelMeasurement gMeasurements[kSweepChannelCount];
static BleCsEstimate gLastEstimate{};
static bool gLastEstimateValid = false;
static bool gCsReady = false;
static float gRecentDistances[kMedianWindow] = {0.0f};
static uint8_t gRecentCount = 0U;
static uint8_t gRecentIndex = 0U;
static uint8_t gSequence = 0U;
static uint32_t gSweepCount = 0U;
static uint32_t gValidSweepCount = 0U;
static uint32_t gAcceptedSweepCount = 0U;
static uint32_t gLastLogMs = 0U;
static uint8_t gLastValidChannels = 0U;
static float gCalibrationScale = kCalibrationScaleDefault;
static float gCalibrationOffsetMeters = kCalibrationOffsetMetersDefault;
static char gCommandBuffer[48] = {0};
static uint8_t gCommandLength = 0U;

#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
struct VprSelectDemoSummary {
  uint32_t magic;
  uint32_t stage;
  uint32_t ok;
  uint32_t pumpCount;
  uint32_t statusWord;
  uint32_t initialAuthority;
  uint32_t createdAuthority;
  uint32_t securedAuthority;
  uint32_t armedAuthority;
  uint32_t baseSelectedAuthority;
  uint32_t altSelectedAuthority;
};

__attribute__((section(".noinit"))) static volatile VprSelectDemoSummary gVprSelectDemoSummary;
#endif
static BleCsDfeCaptureInfo gLastDfeInfo{};

struct StepDemoContext {
  bool parsed = false;
  uint8_t channel = 0U;
  BleCsStepMode2Data mode2{};
  BleCsStepToneInfo tones[2];
};

struct HciHostDemoContext {
  BleCsControllerHost* host = nullptr;
  bool injectAcl = true;
  uint8_t capsV2Payload[34] = {0};
  uint8_t securityPayload[3] = {0};
  uint8_t configPayload[33] = {0};
  uint8_t procedurePayload[21] = {0};
  uint8_t aclPacket[32] = {0};
};

class ByteQueueStream : public Stream {
 public:
  ByteQueueStream() : buffer_{0}, head_(0U), tail_(0U), used_(0U) {}

  void clear() {
    head_ = 0U;
    tail_ = 0U;
    used_ = 0U;
  }

  bool enqueue(const uint8_t* data, size_t len) {
    if ((len > 0U && data == nullptr) || (used_ + len) > sizeof(buffer_)) {
      return false;
    }
    for (size_t i = 0U; i < len; ++i) {
      buffer_[tail_] = data[i];
      tail_ = static_cast<size_t>((tail_ + 1U) % sizeof(buffer_));
      ++used_;
    }
    return true;
  }

  int available() override { return static_cast<int>(used_); }

  int read() override {
    if (used_ == 0U) {
      return -1;
    }
    const uint8_t value = buffer_[head_];
    head_ = static_cast<size_t>((head_ + 1U) % sizeof(buffer_));
    --used_;
    return value;
  }

  int peek() override { return (used_ == 0U) ? -1 : buffer_[head_]; }

  void flush() override {}

  size_t write(uint8_t byte) override { return enqueue(&byte, 1U) ? 1U : 0U; }

  using Print::write;
  size_t write(const uint8_t* data, size_t len) override { return enqueue(data, len) ? len : 0U; }

 private:
  uint8_t buffer_[2048];
  size_t head_;
  size_t tail_;
  size_t used_;
};

class HciDemoControllerStream : public Stream {
 public:
  HciDemoControllerStream() : context_(nullptr), rx_{} {}

  void setContext(HciHostDemoContext* context) { context_ = context; }

  bool enqueueIncoming(const uint8_t* data, size_t len) { return rx_.enqueue(data, len); }

  void clear() { rx_.clear(); }

  int available() override { return rx_.available(); }
  int read() override { return rx_.read(); }
  int peek() override { return rx_.peek(); }
  void flush() override {}

  size_t write(uint8_t byte) override { return write(&byte, 1U); }

  using Print::write;
  size_t write(const uint8_t* data, size_t len) override;

 private:
  bool enqueueController(const uint8_t* data, size_t len);

  HciHostDemoContext* context_;
  ByteQueueStream rx_;
};

void ledOn() {
  (void)Gpio::write(kPinUserLed, false);
}

void ledOff() {
  (void)Gpio::write(kPinUserLed, true);
}

void pulse(uint8_t count, uint16_t onMs = 30U, uint16_t offMs = 60U) {
  for (uint8_t i = 0U; i < count; ++i) {
    ledOn();
    delay(onMs);
    ledOff();
    if ((i + 1U) < count) {
      delay(offMs);
    }
  }
}

[[noreturn]] void failStage(uint8_t stage) {
  while (true) {
    pulse(stage, 90U, 120U);
    delay(900U);
  }
}

void configureBoard() {
  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  ledOff();

  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && (static_cast<uint32_t>(millis() - start) < 1500U)) {
  }

  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  if (!BoardControl::enableRfPath(kAntennaPath)) {
    failStage(1);
  }
}

void storeDistance(float meters) {
  if (!isfinite(meters) || meters <= 0.0f) {
    return;
  }

  gRecentDistances[gRecentIndex] = meters;
  gRecentIndex = static_cast<uint8_t>((gRecentIndex + 1U) % kMedianWindow);
  if (gRecentCount < kMedianWindow) {
    ++gRecentCount;
  }
}

float medianDistance() {
  if (gRecentCount == 0U) {
    return NAN;
  }

  float values[kMedianWindow] = {0.0f};
  for (uint8_t i = 0U; i < gRecentCount; ++i) {
    values[i] = gRecentDistances[i];
  }

  for (uint8_t i = 0U; i < gRecentCount; ++i) {
    for (uint8_t j = static_cast<uint8_t>(i + 1U); j < gRecentCount; ++j) {
      if (values[j] < values[i]) {
        const float tmp = values[i];
        values[i] = values[j];
        values[j] = tmp;
      }
    }
  }

  if ((gRecentCount & 0x1U) == 0U) {
    const uint8_t upper = static_cast<uint8_t>(gRecentCount / 2U);
    return (values[upper - 1U] + values[upper]) * 0.5f;
  }
  return values[gRecentCount / 2U];
}

bool estimateAcceptableForDisplay(const BleCsEstimate& estimate) {
  if (!estimate.valid || !isfinite(estimate.phaseSlopeDistanceMeters) ||
      estimate.phaseSlopeDistanceMeters <= 0.0f) {
    return false;
  }

  if (estimate.usedChannels < 10U) {
    return false;
  }

  if (estimate.medianToneQuality < 0.45f) {
    return false;
  }

  if (estimate.residualVariance > 1.20f) {
    return false;
  }

  if (estimate.fitDeltaMeters > 0.75f) {
    return false;
  }

  return true;
}

float applyCalibration(float meters) {
  if (!isfinite(meters)) {
    return NAN;
  }

  const float calibrated = (meters * gCalibrationScale) + gCalibrationOffsetMeters;
  return (calibrated >= 0.0f) ? calibrated : 0.0f;
}

void printDistanceField(const __FlashStringHelper* label, float meters) {
  Serial.print(label);
  if (isfinite(meters)) {
    Serial.print(meters, 4);
  } else {
    Serial.print(F("na"));
  }
}

void printDistanceIntegerField(const __FlashStringHelper* label,
                               float meters,
                               bool millimeters) {
  Serial.print(label);
  if (!isfinite(meters)) {
    Serial.print(F("na"));
    return;
  }

  if (millimeters) {
    Serial.print(lroundf(meters * 1000.0f));
  } else {
    Serial.print(lroundf(meters * 100.0f));
  }
}

bool parseCommandFloat(const char* text, float* outValue) {
  if (text == nullptr || outValue == nullptr) {
    return false;
  }

  char* end = nullptr;
  const float value = strtof(text, &end);
  if (end == text || !isfinite(value)) {
    return false;
  }
  while (*end == ' ' || *end == '\t') {
    ++end;
  }
  if (*end != '\0') {
    return false;
  }

  *outValue = value;
  return true;
}

void printCalibrationStatus() {
  Serial.print(F("calibration scale="));
  Serial.print(gCalibrationScale, 6);
  Serial.print(F(" offset_m="));
  Serial.print(gCalibrationOffsetMeters, 4);
  if (gLastEstimateValid && isfinite(gLastEstimate.phaseSlopeDistanceMeters)) {
    Serial.print(F(" raw_phase_m="));
    Serial.print(gLastEstimate.phaseSlopeDistanceMeters, 4);
    Serial.print(F(" calibrated_phase_m="));
    Serial.print(applyCalibration(gLastEstimate.phaseSlopeDistanceMeters), 4);
  }
  Serial.println();
}

void printDfeStatus() {
  gLastDfeInfo = gCs.lastDfeCaptureInfo();
  Serial.print(F("dfe present="));
  Serial.print(gLastDfeInfo.present ? 1 : 0);
  Serial.print(F(" all_zero="));
  Serial.print(gLastDfeInfo.allZero ? 1 : 0);
  Serial.print(F(" amount="));
  Serial.print(gLastDfeInfo.amountBytes);
  Serial.print(F(" current="));
  Serial.print(gLastDfeInfo.currentAmountBytes);

  if (!gLastDfeInfo.present || gLastDfeInfo.amountBytes == 0U) {
    Serial.println();
    return;
  }

  uint8_t raw[32] = {0};
  size_t rawLen = 0U;
  const bool fullDump = gCs.copyLastDfePacket(raw, sizeof(raw), &rawLen);

  Serial.print(F(" raw="));
  for (size_t i = 0U; i < rawLen; ++i) {
    if (i > 0U) {
      Serial.print(':');
    }
    if (raw[i] < 0x10U) {
      Serial.print('0');
    }
    Serial.print(raw[i], HEX);
  }
  if (!fullDump) {
    Serial.print(F(" dump=truncated"));
  }
  if (rawLen >= 3U) {
    const BleCsIqSample iq = BleChannelSoundingRadio::parsePctSample(raw);
    Serial.print(F(" pct0_i="));
    Serial.print(iq.i);
    Serial.print(F(" pct0_q="));
    Serial.print(iq.q);
  }
  Serial.println();
}

void printRttRawBytes(const __FlashStringHelper* label, const BleCsRttSample& rtt) {
  Serial.print(label);
  Serial.print(F(" present="));
  Serial.print(rtt.present ? 1 : 0);
  Serial.print(F(" valid="));
  Serial.print(rtt.valid ? 1 : 0);
  Serial.print(F(" len="));
  Serial.print(rtt.rawLen);
  Serial.print(F(" raw="));
  for (uint8_t i = 0U; i < rtt.rawLen; ++i) {
    if (i > 0U) {
      Serial.print(':');
    }
    if (rtt.rawBytes[i] < 0x10U) {
      Serial.print('0');
    }
    Serial.print(rtt.rawBytes[i], HEX);
  }
}

uint8_t csFrequencyOffsetMHz(uint8_t channel) {
  if (channel <= 10U) {
    return static_cast<uint8_t>(4U + (2U * channel));
  }
  return static_cast<uint8_t>(6U + (2U * channel));
}

void encodePctSampleBytes(int16_t i, int16_t q, uint8_t outPct[3]) {
  auto clamp12 = [](int16_t v) -> int16_t {
    if (v < -2048) {
      return -2048;
    }
    if (v > 2047) {
      return 2047;
    }
    return v;
  };

  const uint16_t i12 = static_cast<uint16_t>(clamp12(i)) & 0x0FFFU;
  const uint16_t q12 = static_cast<uint16_t>(clamp12(q)) & 0x0FFFU;
  const uint32_t packed = static_cast<uint32_t>(i12) |
                          (static_cast<uint32_t>(q12) << 12U);
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

struct StepDumpContext {
  uint8_t printed = 0U;
};

struct StepChannelCollectContext {
  uint8_t channels[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
  uint8_t count = 0U;
};

struct StepModeCollectContext {
  uint8_t mode1Count = 0U;
  uint8_t mode2Count = 0U;
  uint8_t mode3Count = 0U;
};

struct StepPermutationCollectContext {
  uint8_t permutations[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
  uint8_t count = 0U;
};

struct StepQualityCollectContext {
  uint8_t quality[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
  uint8_t count = 0U;
};

struct StepAmplitudeCollectContext {
  uint16_t amplitude[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
  uint8_t count = 0U;
};

struct StepPhaseCollectContext {
  int16_t phaseDeg[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t count = 0U;
};

uint16_t approxPctAmplitude(const BleCsIqSample& sample) {
  const float i = static_cast<float>(sample.i);
  const float q = static_cast<float>(sample.q);
  const float magnitude = sqrtf((i * i) + (q * q));
  if (!isfinite(magnitude) || magnitude <= 0.0f) {
    return 0U;
  }
  if (magnitude >= 65535.0f) {
    return 65535U;
  }
  return static_cast<uint16_t>(magnitude + 0.5f);
}

int16_t approxPctPhaseDegrees(const BleCsIqSample& sample) {
  const float phase = atan2f(static_cast<float>(sample.q), static_cast<float>(sample.i));
  float degrees = phase * (180.0f / PI);
  if (!isfinite(degrees)) {
    return 0;
  }
  if (degrees >= 0.0f) {
    degrees += 0.5f;
  } else {
    degrees -= 0.5f;
  }
  return static_cast<int16_t>(degrees);
}

bool printStepDumpCallback(const BleCsSubeventStep* step, void* userData) {
  StepDumpContext* ctx = static_cast<StepDumpContext*>(userData);
  if (step == nullptr || ctx == nullptr || ctx->printed >= 4U) {
    return false;
  }

  Serial.print(F(" step"));
  Serial.print(ctx->printed);
  Serial.print(F("_mode="));
  Serial.print(step->mode);
  Serial.print(F(" ch="));
  Serial.print(step->channel);
  Serial.print(F(" len="));
  Serial.print(step->dataLen);

  if (step->mode == kBleCsMainMode2) {
    BleCsStepMode2Data mode2{};
    BleCsStepToneInfo tone{};
    if (BleChannelSoundingRadio::parseMode2StepData(step, &mode2)) {
      Serial.print(F(" perm="));
      Serial.print(mode2.antennaPermutationIndex);
      Serial.print(F(" tones="));
      Serial.print(mode2.toneCount);
    }
    if (BleChannelSoundingRadio::parseMode2ToneInfo(step, 0U, &tone)) {
      Serial.print(F(" i="));
      Serial.print(tone.pct.i);
      Serial.print(F(" q="));
      Serial.print(tone.pct.q);
      Serial.print(F(" ph="));
      Serial.print(approxPctPhaseDegrees(tone.pct));
      Serial.print(F(" amp="));
      Serial.print(approxPctAmplitude(tone.pct));
      Serial.print(F(" ql="));
      Serial.print(tone.qualityIndicator);
    }
  }
  if (step->mode == kBleCsMainMode1 || step->mode == kBleCsMainMode3) {
    BleCsStepMode1Data mode1{};
    if (BleChannelSoundingRadio::parseMode1StepData(step, &mode1)) {
      Serial.print(F(" aa="));
      Serial.print(mode1.aaCheckQuality);
      Serial.print(F(" rssi="));
      Serial.print(mode1.packetRssiDbm);
      Serial.print(F(" tdiff="));
      Serial.print(mode1.timeDifferenceHalfNs);
      Serial.print(F(" pant="));
      Serial.print(mode1.packetAntenna);
    }
  }

  ++ctx->printed;
  return true;
}

bool collectStepChannelCallback(const BleCsSubeventStep* step, void* userData) {
  StepChannelCollectContext* ctx = static_cast<StepChannelCollectContext*>(userData);
  if (step == nullptr || ctx == nullptr) {
    return false;
  }
  if (ctx->count < 8U) {
    ctx->channels[ctx->count++] = step->channel;
  }
  return true;
}

bool collectStepModeCallback(const BleCsSubeventStep* step, void* userData) {
  StepModeCollectContext* ctx = static_cast<StepModeCollectContext*>(userData);
  if (step == nullptr || ctx == nullptr) {
    return false;
  }
  if (step->mode == kBleCsMainMode1) {
    ++ctx->mode1Count;
  } else if (step->mode == kBleCsMainMode2) {
    ++ctx->mode2Count;
  } else if (step->mode == kBleCsMainMode3) {
    ++ctx->mode3Count;
  }
  return true;
}

bool collectStepPermutationCallback(const BleCsSubeventStep* step, void* userData) {
  StepPermutationCollectContext* ctx = static_cast<StepPermutationCollectContext*>(userData);
  if (step == nullptr || ctx == nullptr || step->mode != kBleCsMainMode2) {
    return true;
  }
  BleCsStepMode2Data mode2{};
  if (!BleChannelSoundingRadio::parseMode2StepData(step, &mode2)) {
    return false;
  }
  if (ctx->count < 8U) {
    ctx->permutations[ctx->count++] = mode2.antennaPermutationIndex;
  }
  return true;
}

bool collectStepQualityCallback(const BleCsSubeventStep* step, void* userData) {
  StepQualityCollectContext* ctx = static_cast<StepQualityCollectContext*>(userData);
  if (step == nullptr || ctx == nullptr || step->mode != kBleCsMainMode2) {
    return true;
  }
  BleCsStepToneInfo tone{};
  if (!BleChannelSoundingRadio::parseMode2ToneInfo(step, 0U, &tone)) {
    return false;
  }
  if (ctx->count < 8U) {
    ctx->quality[ctx->count++] = tone.qualityIndicator;
  }
  return true;
}

bool collectStepAmplitudeCallback(const BleCsSubeventStep* step, void* userData) {
  StepAmplitudeCollectContext* ctx = static_cast<StepAmplitudeCollectContext*>(userData);
  if (step == nullptr || ctx == nullptr || step->mode != kBleCsMainMode2) {
    return true;
  }
  BleCsStepToneInfo tone{};
  if (!BleChannelSoundingRadio::parseMode2ToneInfo(step, 0U, &tone)) {
    return false;
  }
  if (ctx->count < 8U) {
    ctx->amplitude[ctx->count++] = approxPctAmplitude(tone.pct);
  }
  return true;
}

bool collectStepPhaseCallback(const BleCsSubeventStep* step, void* userData) {
  StepPhaseCollectContext* ctx = static_cast<StepPhaseCollectContext*>(userData);
  if (step == nullptr || ctx == nullptr || step->mode != kBleCsMainMode2) {
    return true;
  }
  BleCsStepToneInfo tone{};
  if (!BleChannelSoundingRadio::parseMode2ToneInfo(step, 0U, &tone)) {
    return false;
  }
  if (ctx->count < 8U) {
    ctx->phaseDeg[ctx->count++] = approxPctPhaseDegrees(tone.pct);
  }
  return true;
}

StepChannelCollectContext collectStepChannels(const BleCsSubeventResult& result) {
  StepChannelCollectContext ctx{};
  BleChannelSoundingRadio::parseSubeventStepData(result.stepData, result.stepDataLen,
                                                 collectStepChannelCallback, &ctx);
  return ctx;
}

StepModeCollectContext collectStepModes(const BleCsSubeventResult& result) {
  StepModeCollectContext ctx{};
  BleChannelSoundingRadio::parseSubeventStepData(result.stepData, result.stepDataLen,
                                                 collectStepModeCallback, &ctx);
  return ctx;
}

StepPermutationCollectContext collectStepPermutations(const BleCsSubeventResult& result) {
  StepPermutationCollectContext ctx{};
  BleChannelSoundingRadio::parseSubeventStepData(result.stepData, result.stepDataLen,
                                                 collectStepPermutationCallback, &ctx);
  return ctx;
}

StepQualityCollectContext collectStepQuality(const BleCsSubeventResult& result) {
  StepQualityCollectContext ctx{};
  BleChannelSoundingRadio::parseSubeventStepData(result.stepData, result.stepDataLen,
                                                 collectStepQualityCallback, &ctx);
  return ctx;
}

StepAmplitudeCollectContext collectStepAmplitude(const BleCsSubeventResult& result) {
  StepAmplitudeCollectContext ctx{};
  BleChannelSoundingRadio::parseSubeventStepData(result.stepData, result.stepDataLen,
                                                 collectStepAmplitudeCallback, &ctx);
  return ctx;
}

StepPhaseCollectContext collectStepPhase(const BleCsSubeventResult& result) {
  StepPhaseCollectContext ctx{};
  BleChannelSoundingRadio::parseSubeventStepData(result.stepData, result.stepDataLen,
                                                 collectStepPhaseCallback, &ctx);
  return ctx;
}

void printSubeventResultDump(const __FlashStringHelper* label,
                             const BleCsSubeventResult& result) {
  Serial.print(label);
  Serial.print(F(" complete="));
  Serial.print(result.isComplete ? 1 : 0);
  Serial.print(F(" partial="));
  Serial.print(result.isPartial ? 1 : 0);
  Serial.print(F(" cont="));
  Serial.print(result.isContinuation ? 1 : 0);
  Serial.print(F(" conn=0x"));
  Serial.print(result.header.connHandle, HEX);
  Serial.print(F(" cfg="));
  Serial.print(result.header.configId);
  Serial.print(F(" proc="));
  Serial.print(result.header.procedureCounter);
  Serial.print(F(" acl=0x"));
  Serial.print(result.header.startAclConnEventCounter, HEX);
  Serial.print(F(" fc=0x"));
  Serial.print(result.header.frequencyCompensation, HEX);
  Serial.print(F(" pwr="));
  Serial.print(static_cast<int>(result.header.referencePowerLevelDbm));
  Serial.print(F(" ant="));
  Serial.print(static_cast<unsigned int>(result.header.numAntennaPaths));
  Serial.print(F(" steps="));
  Serial.print(result.header.numStepsReported);
  Serial.print(F(" bytes="));
  Serial.print(result.stepDataLen);
  Serial.print(F(" raw="));
  for (uint16_t i = 0U; i < result.stepDataLen; ++i) {
    if (i != 0U) {
      Serial.print(':');
    }
    if (result.stepData[i] < 0x10U) {
      Serial.print('0');
    }
    Serial.print(result.stepData[i], HEX);
  }
  StepDumpContext ctx{};
  BleChannelSoundingRadio::parseSubeventStepData(result.stepData, result.stepDataLen,
                                                 printStepDumpCallback, &ctx);
  Serial.println();
}

void encodePctToneInfoBytes(int16_t i, int16_t q, uint8_t outTone[4]) {
  if (outTone == nullptr) {
    return;
  }
  encodePctSampleBytes(i, q, outTone);
  outTone[3] = kBleCsToneQualityHigh;
}

bool appendMode1SsRttDemoStep(uint8_t* buffer,
                              size_t maxLen,
                              size_t* offset,
                              uint8_t channel,
                              int16_t timeDifferenceHalfNs,
                              uint8_t antenna,
                              int16_t pct1I,
                              int16_t pct1Q,
                              int16_t pct2I,
                              int16_t pct2Q) {
  if (buffer == nullptr || offset == nullptr || (maxLen - *offset) < 17U) {
    return false;
  }

  buffer[*offset + 0U] = kBleCsMainMode1;
  buffer[*offset + 1U] = channel;
  buffer[*offset + 2U] = 14U;
  buffer[*offset + 3U] = kBleCsPacketQualityAaCheckOk;
  buffer[*offset + 4U] = 0U;
  buffer[*offset + 5U] = static_cast<uint8_t>(static_cast<int8_t>(-42));
  buffer[*offset + 6U] =
      static_cast<uint8_t>(static_cast<uint16_t>(timeDifferenceHalfNs) & 0xFFU);
  buffer[*offset + 7U] = static_cast<uint8_t>(
      (static_cast<uint16_t>(timeDifferenceHalfNs) >> 8U) & 0xFFU);
  buffer[*offset + 8U] = antenna;
  encodePctToneInfoBytes(pct1I, pct1Q, &buffer[*offset + 9U]);
  encodePctToneInfoBytes(pct2I, pct2Q, &buffer[*offset + 13U]);
  *offset += 17U;
  return true;
}

void writeLe16Demo(uint8_t* out, uint16_t value) {
  if (out == nullptr) {
    return;
  }
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void writeLe24Demo(uint8_t* out, uint32_t value) {
  if (out == nullptr) {
    return;
  }
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  out[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
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
  writeLe16Demo(out + 0U, connHandle);
  out[2U] = configId;
  writeLe16Demo(out + 3U, startAclConnEventCounter);
  writeLe16Demo(out + 5U, procedureCounter);
  writeLe16Demo(out + 7U, 0U);
  out[9U] = 0;
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
  writeLe16Demo(out + 0U, connHandle);
  out[2U] = configId;
  out[3U] = partial ? kBleCsProcedureDonePartial : kBleCsProcedureDoneComplete;
  out[4U] = partial ? kBleCsSubeventDonePartial : kBleCsSubeventDoneComplete;
  out[5U] = 0U;
  out[6U] = numAntennaPaths;
  out[7U] = numStepsReported;
  memcpy(out + 8U, stepBytes, stepLen);
  return true;
}

bool buildRemoteCapsCompleteEvent(uint8_t* out,
                                  size_t maxLen,
                                  uint16_t connHandle,
                                  bool v2) {
  const size_t eventLen = v2 ? 34U : 31U;
  if (out == nullptr || maxLen < eventLen) {
    return false;
  }

  memset(out, 0, eventLen);
  out[0U] = 0U;
  writeLe16Demo(out + 1U, connHandle);
  out[3U] = 4U;
  writeLe16Demo(out + 4U, 6U);
  out[6U] = 4U;
  out[7U] = 4U;
  out[8U] = 0x03U;
  out[9U] = 0x01U;
  out[10U] = 0x07U;
  out[11U] = 0x02U;
  out[12U] = 0x03U;
  out[13U] = 0x04U;
  writeLe16Demo(out + 14U, 0x0001U);
  writeLe16Demo(out + 16U, 0x0001U);
  out[18U] = 0x06U;
  writeLe16Demo(out + 19U, v2 ? 0x001EU : 0x000EU);
  writeLe16Demo(out + 21U, 10U);
  writeLe16Demo(out + 23U, 20U);
  writeLe16Demo(out + 25U, 30U);
  writeLe16Demo(out + 27U, 40U);
  out[29U] = 3U;
  out[30U] = 4U;
  if (v2) {
    writeLe16Demo(out + 31U, 50U);
    out[33U] = 6U;
  }
  return true;
}

bool buildSecurityEnableCompleteEvent(uint8_t* out, size_t maxLen, uint16_t connHandle) {
  if (out == nullptr || maxLen < 3U) {
    return false;
  }
  out[0U] = 0U;
  writeLe16Demo(out + 1U, connHandle);
  return true;
}

bool buildConfigCompleteEvent(uint8_t* out, size_t maxLen, uint16_t connHandle) {
  if (out == nullptr || maxLen < 33U) {
    return false;
  }

  uint8_t channelMap[kBleCsChannelMapBytes] = {0};
  BleChannelSoundingRadio::fillValidChannelMap(channelMap);

  memset(out, 0, 33U);
  out[0U] = 0U;
  writeLe16Demo(out + 1U, connHandle);
  out[3U] = 1U;
  out[4U] = 1U;
  out[5U] = kBleCsMainMode2;
  out[6U] = 0xFFU;
  out[7U] = 3U;
  out[8U] = 5U;
  out[9U] = 1U;
  out[10U] = 1U;
  out[11U] = 0U;
  out[12U] = 1U;
  out[13U] = 2U;
  memcpy(out + 14U, channelMap, sizeof(channelMap));
  out[24U] = 1U;
  out[25U] = 1U;
  out[26U] = 1U;
  out[27U] = 3U;
  out[28U] = 0x01U;
  out[29U] = 10U;
  out[30U] = 20U;
  out[31U] = 30U;
  out[32U] = 40U;
  return true;
}

bool buildProcedureEnableCompleteEvent(uint8_t* out, size_t maxLen, uint16_t connHandle) {
  if (out == nullptr || maxLen < 21U) {
    return false;
  }

  memset(out, 0, 21U);
  out[0U] = 0U;
  writeLe16Demo(out + 1U, connHandle);
  out[3U] = 1U;
  out[4U] = 1U;
  out[5U] = 2U;
  out[6U] = static_cast<uint8_t>(static_cast<int8_t>(-12));
  writeLe24Demo(out + 7U, 0x000456UL);
  out[10U] = 4U;
  writeLe16Demo(out + 11U, 100U);
  writeLe16Demo(out + 13U, 200U);
  writeLe16Demo(out + 15U, 300U);
  writeLe16Demo(out + 17U, 8U);
  writeLe16Demo(out + 19U, 12U);
  return true;
}

bool buildH4CommandStatusEvent(uint8_t* out, size_t maxLen, uint16_t opcode, uint8_t status) {
  if (out == nullptr || maxLen < 7U) {
    return false;
  }
  out[0U] = kBleHciPacketTypeEvent;
  out[1U] = kBleHciEvtCommandStatus;
  out[2U] = 4U;
  out[3U] = status;
  out[4U] = 1U;
  writeLe16Demo(out + 5U, opcode);
  return true;
}

bool buildH4CommandCompleteEvent(uint8_t* out, size_t maxLen, uint16_t opcode, uint8_t status) {
  if (out == nullptr || maxLen < 7U) {
    return false;
  }
  out[0U] = kBleHciPacketTypeEvent;
  out[1U] = kBleHciEvtCommandComplete;
  out[2U] = 4U;
  out[3U] = 1U;
  writeLe16Demo(out + 4U, opcode);
  out[6U] = status;
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

bool buildH4AclPacket(uint8_t* out,
                      size_t maxLen,
                      uint16_t connHandle,
                      const uint8_t* payload,
                      size_t payloadLen) {
  if (out == nullptr || payload == nullptr || payloadLen > 0xFFFFU ||
      maxLen < (5U + payloadLen)) {
    return false;
  }
  out[0U] = kBleHciPacketTypeAcl;
  writeLe16Demo(out + 1U, connHandle);
  writeLe16Demo(out + 3U, static_cast<uint16_t>(payloadLen));
  memcpy(out + 5U, payload, payloadLen);
  return true;
}

bool feedHostIngressChunked(BleCsControllerHost& host,
                            BleCsControllerIngressSource source,
                            const uint8_t* bytes,
                            size_t len) {
  size_t offset = 0U;
  while (offset < len) {
    const size_t chunk = ((offset & 1U) == 0U) ? 3U : 5U;
    const size_t send = ((len - offset) < chunk) ? (len - offset) : chunk;
    if (!host.consumeIngressBytes(source, bytes + offset, send)) {
      return false;
    }
    offset += send;
  }
  return true;
}

bool hciHostDemoSendPacket(const uint8_t* packet, size_t packetLen, void* userData) {
  HciHostDemoContext* context = static_cast<HciHostDemoContext*>(userData);
  if (context == nullptr || context->host == nullptr || packet == nullptr || packetLen < 4U) {
    return false;
  }

  auto feedController = [&](const uint8_t* bytes, size_t len) -> bool {
    if (context->injectAcl &&
        !feedHostIngressChunked(*context->host, BleCsControllerIngressSource::kController,
                                context->aclPacket, 11U)) {
      return false;
    }
    if (!feedHostIngressChunked(*context->host, BleCsControllerIngressSource::kController, bytes,
                                len)) {
      return false;
    }
    if (context->injectAcl &&
        !feedHostIngressChunked(*context->host, BleCsControllerIngressSource::kController,
                                context->aclPacket, 11U)) {
      return false;
    }
    return true;
  };

  const uint16_t opcode = static_cast<uint16_t>(packet[1U]) |
                          (static_cast<uint16_t>(packet[2U]) << 8U);
  uint8_t eventPacket[64] = {0};

  switch (opcode) {
    case kBleCsHciOpReadRemoteSupportedCapabilities:
      return buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
             feedController(eventPacket, 7U) &&
             buildH4LeMetaEvent(eventPacket, sizeof(eventPacket),
                                kBleCsHciEvtReadRemoteSupportedCapabilitiesCompleteV2,
                                context->capsV2Payload, sizeof(context->capsV2Payload)) &&
             feedController(eventPacket, 4U + sizeof(context->capsV2Payload));

    case kBleCsHciOpSetDefaultSettings:
      return buildH4CommandCompleteEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
             feedController(eventPacket, 7U);

    case kBleCsHciOpCreateConfig:
      return buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
             feedController(eventPacket, 7U) &&
             buildH4LeMetaEvent(eventPacket, sizeof(eventPacket), kBleCsHciEvtConfigComplete,
                                context->configPayload, sizeof(context->configPayload)) &&
             feedController(eventPacket, 4U + sizeof(context->configPayload));

    case kBleCsHciOpSecurityEnable:
      return buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
             feedController(eventPacket, 7U) &&
             buildH4LeMetaEvent(eventPacket, sizeof(eventPacket),
                                kBleCsHciEvtSecurityEnableComplete, context->securityPayload,
                                sizeof(context->securityPayload)) &&
             feedController(eventPacket, 4U + sizeof(context->securityPayload));

    case kBleCsHciOpSetProcedureParameters:
      return buildH4CommandCompleteEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
             feedController(eventPacket, 7U);

    case kBleCsHciOpProcedureEnable:
      return buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
             feedController(eventPacket, 7U) &&
             buildH4LeMetaEvent(eventPacket, sizeof(eventPacket),
                                kBleCsHciEvtProcedureEnableComplete, context->procedurePayload,
                                sizeof(context->procedurePayload)) &&
             feedController(eventPacket, 4U + sizeof(context->procedurePayload));

    default:
      return false;
  }
}

bool HciDemoControllerStream::enqueueController(const uint8_t* data, size_t len) {
  if (context_ == nullptr) {
    return false;
  }
  if (context_->injectAcl && !rx_.enqueue(context_->aclPacket, 11U)) {
    return false;
  }
  if (!rx_.enqueue(data, len)) {
    return false;
  }
  if (context_->injectAcl && !rx_.enqueue(context_->aclPacket, 11U)) {
    return false;
  }
  return true;
}

size_t HciDemoControllerStream::write(const uint8_t* data, size_t len) {
  if (context_ == nullptr || data == nullptr || len < 4U) {
    return 0U;
  }

  const uint16_t opcode = static_cast<uint16_t>(data[1U]) |
                          (static_cast<uint16_t>(data[2U]) << 8U);
  uint8_t eventPacket[64] = {0};
  bool ok = false;

  switch (opcode) {
    case kBleCsHciOpReadRemoteSupportedCapabilities:
      ok = buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
           enqueueController(eventPacket, 7U) &&
           buildH4LeMetaEvent(eventPacket, sizeof(eventPacket),
                              kBleCsHciEvtReadRemoteSupportedCapabilitiesCompleteV2,
                              context_->capsV2Payload, sizeof(context_->capsV2Payload)) &&
           enqueueController(eventPacket, 4U + sizeof(context_->capsV2Payload));
      break;

    case kBleCsHciOpSetDefaultSettings:
      ok = buildH4CommandCompleteEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
           enqueueController(eventPacket, 7U);
      break;

    case kBleCsHciOpCreateConfig:
      ok = buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
           enqueueController(eventPacket, 7U) &&
           buildH4LeMetaEvent(eventPacket, sizeof(eventPacket), kBleCsHciEvtConfigComplete,
                              context_->configPayload, sizeof(context_->configPayload)) &&
           enqueueController(eventPacket, 4U + sizeof(context_->configPayload));
      break;

    case kBleCsHciOpSecurityEnable:
      ok = buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
           enqueueController(eventPacket, 7U) &&
           buildH4LeMetaEvent(eventPacket, sizeof(eventPacket),
                              kBleCsHciEvtSecurityEnableComplete, context_->securityPayload,
                              sizeof(context_->securityPayload)) &&
           enqueueController(eventPacket, 4U + sizeof(context_->securityPayload));
      break;

    case kBleCsHciOpSetProcedureParameters:
      ok = buildH4CommandCompleteEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
           enqueueController(eventPacket, 7U);
      break;

    case kBleCsHciOpProcedureEnable:
      ok = buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
           enqueueController(eventPacket, 7U) &&
           buildH4LeMetaEvent(eventPacket, sizeof(eventPacket),
                              kBleCsHciEvtProcedureEnableComplete, context_->procedurePayload,
                              sizeof(context_->procedurePayload)) &&
           enqueueController(eventPacket, 4U + sizeof(context_->procedurePayload));
      break;

    default:
      ok = false;
      break;
  }

  return ok ? len : 0U;
}

bool parseStepDemoCallback(const BleCsSubeventStep* step, void* userData) {
  StepDemoContext* context = static_cast<StepDemoContext*>(userData);
  if (context == nullptr || step == nullptr) {
    return false;
  }
  context->parsed = false;
  if (!BleChannelSoundingRadio::parseMode2StepData(step, &context->mode2)) {
    return false;
  }
  context->parsed = true;
  context->channel = step->channel;
  const uint8_t toneLimit = (context->mode2.toneCount < 2U) ? context->mode2.toneCount : 2U;
  for (uint8_t i = 0U; i < toneLimit; ++i) {
    if (!BleChannelSoundingRadio::parseMode2ToneInfo(step, i, &context->tones[i])) {
      context->parsed = false;
      return false;
    }
  }
  return false;
}

void printStepParserDemo() {
  static const uint8_t sampleStepData[] = {
      kBleCsMainMode2, 19U, 9U,
      3U,
      0x34U, 0x12U, 0xA1U, 0x10U,
      0x78U, 0x56U, 0xB2U, 0x21U,
  };

  StepDemoContext context{};
  BleChannelSoundingRadio::parseSubeventStepData(
      sampleStepData, sizeof(sampleStepData), parseStepDemoCallback, &context);

  Serial.print(F("stepdemo parsed="));
  Serial.print(context.parsed ? 1 : 0);
  if (!context.parsed) {
    Serial.println();
    return;
  }

  Serial.print(F(" mode=2"));
  Serial.print(F(" ch="));
  Serial.print(context.channel);
  Serial.print(F(" perm="));
  Serial.print(context.mode2.antennaPermutationIndex);
  Serial.print(F(" tones="));
  Serial.print(context.mode2.toneCount);
  for (uint8_t i = 0U; i < context.mode2.toneCount && i < 2U; ++i) {
    Serial.print(F(" tone"));
    Serial.print(i);
    Serial.print(F("_i="));
    Serial.print(context.tones[i].pct.i);
    Serial.print(F(" tone"));
    Serial.print(i);
    Serial.print(F("_q="));
    Serial.print(context.tones[i].pct.q);
    Serial.print(F(" tone"));
    Serial.print(i);
    Serial.print(F("_qi="));
    Serial.print(context.tones[i].qualityIndicator);
    Serial.print(F(" tone"));
    Serial.print(i);
    Serial.print(F("_ext="));
    Serial.print(context.tones[i].extensionIndicator);
  }
  Serial.println();
}

void printStepEstimateDemo() {
  static const uint8_t demoChannels[] = {2U, 14U, 26U, 38U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);
  static constexpr float kDemoDistanceMeters = 0.75f;
  static constexpr float kDemoAmplitude = 1024.0f;

  uint8_t localSteps[64] = {0};
  uint8_t peerSteps[64] = {0};
  size_t localLen = 0U;
  size_t peerLen = 0U;
  bool built = true;

  for (size_t i = 0U; i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    const float freqHz =
        (2400.0f + static_cast<float>(csFrequencyOffsetMHz(channel))) * 1000000.0f;
    const float theta =
        -((4.0f * 3.14159265358979323846f * kDemoDistanceMeters * freqHz) /
          299792458.0f);
    const int16_t peerI = static_cast<int16_t>(lroundf(cosf(theta) * kDemoAmplitude));
    const int16_t peerQ = static_cast<int16_t>(lroundf(sinf(theta) * kDemoAmplitude));
    built &= appendMode2DemoStep(localSteps, sizeof(localSteps), &localLen, channel, 1024, 0);
    built &= appendMode2DemoStep(peerSteps, sizeof(peerSteps), &peerLen, channel, peerI, peerQ);
  }

  BleCsEstimate estimate{};
  const bool ok = built && BleChannelSoundingRadio::estimateDistanceFromStepBuffers(
                                localSteps, localLen, peerSteps, peerLen, true, &estimate);
  Serial.print(F("stepestdemo ok="));
  Serial.print(ok ? 1 : 0);
  if (!ok) {
    Serial.println();
    return;
  }

  Serial.print(F(" dist_m="));
  Serial.print(estimate.distanceMeters, 4);
  Serial.print(F(" phase_m="));
  Serial.print(estimate.phaseSlopeDistanceMeters, 4);
  Serial.print(F(" used="));
  Serial.print(estimate.usedChannels);
  Serial.print(F(" residual="));
  Serial.print(estimate.residualVariance, 6);
  Serial.println();
}

void printHciEstimateDemo() {
  static const uint8_t demoChannels[] = {2U, 14U, 26U, 38U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);
  static constexpr float kDemoDistanceMeters = 0.75f;
  static constexpr float kDemoAmplitude = 1024.0f;
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static constexpr uint8_t kDemoConfigId = 1U;

  uint8_t localSteps[64] = {0};
  uint8_t peerSteps[64] = {0};
  size_t localLen = 0U;
  size_t peerLen = 0U;
  bool built = true;

  for (size_t i = 0U; i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    const float freqHz =
        (2400.0f + static_cast<float>(csFrequencyOffsetMHz(channel))) * 1000000.0f;
    const float theta =
        -((4.0f * 3.14159265358979323846f * kDemoDistanceMeters * freqHz) /
          299792458.0f);
    const int16_t peerI = static_cast<int16_t>(lroundf(cosf(theta) * kDemoAmplitude));
    const int16_t peerQ = static_cast<int16_t>(lroundf(sinf(theta) * kDemoAmplitude));
    built &= appendMode2DemoStep(localSteps, sizeof(localSteps), &localLen, channel, 1024, 0);
    built &= appendMode2DemoStep(peerSteps, sizeof(peerSteps), &peerLen, channel, peerI, peerQ);
  }

  const size_t splitLen = 16U;
  uint8_t localInit[64] = {0};
  uint8_t localCont[64] = {0};
  uint8_t peerInit[64] = {0};
  uint8_t peerCont[64] = {0};
  built &= buildHciInitialEvent(localInit, sizeof(localInit), kDemoConnHandle, kDemoConfigId,
                                0x1234U, 7U, 2U, 2U, localSteps, splitLen, true);
  built &= buildHciContinueEvent(localCont, sizeof(localCont), kDemoConnHandle, kDemoConfigId,
                                 2U, 2U, localSteps + splitLen, localLen - splitLen, false);
  built &= buildHciInitialEvent(peerInit, sizeof(peerInit), kDemoConnHandle, kDemoConfigId,
                                0x1234U, 7U, 2U, 2U, peerSteps, splitLen, true);
  built &= buildHciContinueEvent(peerCont, sizeof(peerCont), kDemoConnHandle, kDemoConfigId,
                                 2U, 2U, peerSteps + splitLen, peerLen - splitLen, false);

  BleCsSubeventResultReassembler localReasm;
  BleCsSubeventResultReassembler peerReasm;
  BleCsSubeventResult localResult{};
  BleCsSubeventResult peerResult{};
  BleCsEstimate estimate{};

  const bool ok = built &&
                  localReasm.consumeInitialEvent(localInit, 15U + splitLen, &localResult) &&
                  localReasm.consumeContinuationEvent(localCont,
                                                     8U + (localLen - splitLen),
                                                     &localResult) &&
                  peerReasm.consumeInitialEvent(peerInit, 15U + splitLen, &peerResult) &&
                  peerReasm.consumeContinuationEvent(peerCont,
                                                    8U + (peerLen - splitLen),
                                                    &peerResult) &&
                  BleChannelSoundingRadio::estimateDistanceFromSubeventResults(
                      localResult, peerResult, true, &estimate);

  Serial.print(F("hcidemo ok="));
  Serial.print(ok ? 1 : 0);
  if (!ok) {
    Serial.println();
    return;
  }

  Serial.print(F(" dist_m="));
  Serial.print(estimate.distanceMeters, 4);
  Serial.print(F(" phase_m="));
  Serial.print(estimate.phaseSlopeDistanceMeters, 4);
  Serial.print(F(" steps="));
  Serial.print(localResult.header.numStepsReported);
  Serial.print(F(" partial="));
  Serial.print(localResult.isPartial ? 1 : 0);
  Serial.print(F(" complete="));
  Serial.println(localResult.isComplete ? 1 : 0);
}

void printHciRttDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static constexpr uint8_t kDemoConfigId = 2U;

  uint8_t localSteps[32] = {0};
  uint8_t peerSteps[32] = {0};
  size_t localLen = 0U;
  size_t peerLen = 0U;
  bool built = true;

  built &= appendMode1SsRttDemoStep(localSteps, sizeof(localSteps), &localLen, 19U, 30, 1U,
                                    1024, 0, 980, 120);
  built &= appendMode1SsRttDemoStep(peerSteps, sizeof(peerSteps), &peerLen, 19U, 20, 2U,
                                    900, -80, 870, 40);

  uint8_t localEvent[64] = {0};
  uint8_t peerEvent[64] = {0};
  built &= buildHciInitialEvent(localEvent, sizeof(localEvent), kDemoConnHandle, kDemoConfigId,
                                0x3456U, 9U, 1U, 1U, localSteps, localLen, false);
  built &= buildHciInitialEvent(peerEvent, sizeof(peerEvent), kDemoConnHandle, kDemoConfigId,
                                0x3456U, 9U, 1U, 1U, peerSteps, peerLen, false);

  BleCsSubeventResult localResult{};
  BleCsSubeventResult peerResult{};
  BleCsEstimate estimate{};
  BleCsStepMode1Data localMode1{};
  BleCsStepMode1Data peerMode1{};
  struct Mode1Capture {
    BleCsStepMode1Data* out = nullptr;
    bool parsed = false;
  };
  auto captureMode1 = [](const BleCsSubeventStep* step, void* userData) -> bool {
    Mode1Capture* capture = static_cast<Mode1Capture*>(userData);
    if (capture == nullptr || capture->out == nullptr || capture->parsed) {
      return false;
    }
    if (step == nullptr || step->mode != kBleCsMainMode1) {
      return false;
    }
    capture->parsed = BleChannelSoundingRadio::parseMode1StepData(step, capture->out);
    return false;
  };

  Mode1Capture localCapture{.out = &localMode1};
  Mode1Capture peerCapture{.out = &peerMode1};
  const bool ok =
      built &&
      BleChannelSoundingRadio::parseHciSubeventResultEvent(localEvent,
                                                           15U + localLen,
                                                           &localResult) &&
      BleChannelSoundingRadio::parseHciSubeventResultEvent(peerEvent,
                                                           15U + peerLen,
                                                           &peerResult) &&
      BleChannelSoundingRadio::estimateDistanceFromSubeventResults(localResult, peerResult,
                                                                   true, &estimate);
  if (localResult.stepData != nullptr) {
    BleChannelSoundingRadio::parseSubeventStepData(localResult.stepData, localResult.stepDataLen,
                                                   captureMode1, &localCapture);
  }
  if (peerResult.stepData != nullptr) {
    BleChannelSoundingRadio::parseSubeventStepData(peerResult.stepData, peerResult.stepDataLen,
                                                   captureMode1, &peerCapture);
  }

  Serial.print(F("hcirttdemo ok="));
  Serial.print((ok && localCapture.parsed && peerCapture.parsed) ? 1 : 0);
  if (!(ok && localCapture.parsed && peerCapture.parsed)) {
    Serial.println();
    return;
  }

  Serial.print(F(" dist_m="));
  Serial.print(estimate.distanceMeters, 4);
  Serial.print(F(" rtt_m="));
  Serial.print(estimate.rttDistanceMeters, 4);
  Serial.print(F(" local_ss="));
  Serial.print(localMode1.hasRttSoundingSequence ? 1 : 0);
  Serial.print(F(" peer_ss="));
  Serial.print(peerMode1.hasRttSoundingSequence ? 1 : 0);
  Serial.print(F(" local_pct1_i="));
  Serial.print(localMode1.soundingPct1.i);
  Serial.print(F(" local_pct2_q="));
  Serial.print(localMode1.soundingPct2.q);
  Serial.print(F(" peer_pct1_i="));
  Serial.print(peerMode1.soundingPct1.i);
  Serial.print(F(" peer_pct2_q="));
  Serial.println(peerMode1.soundingPct2.q);
}

void printHciPacketDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;

  BleCsHciCommand readCaps{};
  BleCsHciCommand defaultsCmd{};
  BleCsHciCommand createConfig{};
  BleCsHciCommand removeConfig{};
  BleCsHciCommand securityEnable{};
  BleCsHciCommand setProcedure{};
  BleCsHciCommand procedureEnable{};

  BleCsDefaultSettings defaults{};
  defaults.enableInitiatorRole = true;
  defaults.enableReflectorRole = true;
  defaults.csSyncAntennaSelection = 0xFEU;
  defaults.maxTxPowerDbm = -8;

  BleCsControllerCreateConfig config{};
  config.configId = 1U;
  config.createContext = 1U;
  config.mainModeType = kBleCsMainMode2;
  config.subModeType = 0xFFU;
  config.minMainModeSteps = 3U;
  config.maxMainModeSteps = 5U;
  config.mainModeRepetition = 1U;
  config.mode0Steps = 1U;
  config.role = 0U;
  config.rttType = 1U;
  config.csSyncPhy = 2U;
  BleChannelSoundingRadio::fillValidChannelMap(config.channelMap);
  config.channelMapRepetition = 1U;
  config.channelSelectionType = 1U;
  config.ch3cShape = 1U;
  config.ch3cJump = 3U;
  config.csEnhancements1 = 0x01U;

  BleCsProcedureParameters params{};
  params.configId = 1U;
  params.maxProcedureLen = 12U;
  params.minProcedureInterval = 200U;
  params.maxProcedureInterval = 300U;
  params.maxProcedureCount = 8U;
  params.minSubeventLen = 0x000456UL;
  params.maxSubeventLen = 0x000678UL;
  params.toneAntennaConfigSelection = 2U;
  params.phy = 2U;
  params.txPowerDelta = -6;
  params.preferredPeerAntenna = 0xFFU;
  params.snrControlInitiator = 0U;
  params.snrControlReflector = 0U;

  BleCsProcedureEnable enable{};
  enable.configId = 1U;
  enable.enable = 1U;

  uint8_t capsV1Event[31] = {0};
  uint8_t capsV2Event[34] = {0};
  uint8_t securityEvent[3] = {0};
  uint8_t configEvent[33] = {0};
  uint8_t procedureEvent[21] = {0};

  BleCsControllerCapabilities capsV1{};
  BleCsControllerCapabilities capsV2{};
  BleCsSecurityEnableComplete secComplete{};
  BleCsConfigComplete cfgComplete{};
  BleCsProcedureEnableComplete procComplete{};

  const bool ok =
      BleChannelSoundingRadio::buildHciReadRemoteSupportedCapabilitiesCommand(
          kDemoConnHandle, &readCaps) &&
      BleChannelSoundingRadio::buildHciSetDefaultSettingsCommand(
          kDemoConnHandle, defaults, &defaultsCmd) &&
      BleChannelSoundingRadio::buildHciCreateConfigCommand(
          kDemoConnHandle, config, &createConfig) &&
      BleChannelSoundingRadio::buildHciRemoveConfigCommand(
          kDemoConnHandle, config.configId, &removeConfig) &&
      BleChannelSoundingRadio::buildHciSecurityEnableCommand(
          kDemoConnHandle, &securityEnable) &&
      BleChannelSoundingRadio::buildHciSetProcedureParametersCommand(
          kDemoConnHandle, params, &setProcedure) &&
      BleChannelSoundingRadio::buildHciProcedureEnableCommand(
          kDemoConnHandle, enable, &procedureEnable) &&
      buildRemoteCapsCompleteEvent(capsV1Event, sizeof(capsV1Event), kDemoConnHandle, false) &&
      buildRemoteCapsCompleteEvent(capsV2Event, sizeof(capsV2Event), kDemoConnHandle, true) &&
      buildSecurityEnableCompleteEvent(securityEvent, sizeof(securityEvent), kDemoConnHandle) &&
      buildConfigCompleteEvent(configEvent, sizeof(configEvent), kDemoConnHandle) &&
      buildProcedureEnableCompleteEvent(procedureEvent, sizeof(procedureEvent), kDemoConnHandle) &&
      BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteEvent(
          capsV1Event, sizeof(capsV1Event), &capsV1) &&
      BleChannelSoundingRadio::parseHciRemoteSupportedCapabilitiesCompleteV2Event(
          capsV2Event, sizeof(capsV2Event), &capsV2) &&
      BleChannelSoundingRadio::parseHciSecurityEnableCompleteEvent(
          securityEvent, sizeof(securityEvent), &secComplete) &&
      BleChannelSoundingRadio::parseHciConfigCompleteEvent(
          configEvent, sizeof(configEvent), &cfgComplete) &&
      BleChannelSoundingRadio::parseHciProcedureEnableCompleteEvent(
          procedureEvent, sizeof(procedureEvent), &procComplete);

  Serial.print(F("hcipktdemo ok="));
  Serial.print(ok ? 1 : 0);
  if (!ok) {
    Serial.println();
    return;
  }

  Serial.print(F(" read="));
  Serial.print(readCaps.opcode, HEX);
  Serial.print('/');
  Serial.print(readCaps.payloadLen);
  Serial.print(F(" create="));
  Serial.print(createConfig.opcode, HEX);
  Serial.print('/');
  Serial.print(createConfig.payloadLen);
  Serial.print(F(" proc="));
  Serial.print(setProcedure.opcode, HEX);
  Serial.print('/');
  Serial.print(setProcedure.payloadLen);
  Serial.print(F(" caps1_cfg="));
  Serial.print(capsV1.numConfigSupported);
  Serial.print(F(" caps2_ipt="));
  Serial.print(capsV2.tIp2IptTimesSupported);
  Serial.print(F(" mode3="));
  Serial.print(capsV2.mode3Supported ? 1 : 0);
  Serial.print(F(" sec="));
  Serial.print(secComplete.status);
  Serial.print(F(" cfg_ch3c="));
  Serial.print(cfgComplete.ch3cJump);
  Serial.print(F(" proc_len="));
  Serial.print(procComplete.subeventLen);
  Serial.print(F(" proc_cnt="));
  Serial.println(procComplete.procedureCount);
}

void printHciWorkflowDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;

  BleCsControllerWorkflow workflow;
  BleCsControllerWorkflowConfig workflowConfig{};
  workflowConfig.applyDefaultSettings = true;
  workflowConfig.requireSecurityEnable = true;
  workflowConfig.defaultSettings.enableInitiatorRole = true;
  workflowConfig.defaultSettings.enableReflectorRole = true;
  workflowConfig.defaultSettings.csSyncAntennaSelection = 0xFEU;
  workflowConfig.defaultSettings.maxTxPowerDbm = -8;

  workflowConfig.createConfig.configId = 1U;
  workflowConfig.createConfig.createContext = 1U;
  workflowConfig.createConfig.mainModeType = kBleCsMainMode2;
  workflowConfig.createConfig.subModeType = 0xFFU;
  workflowConfig.createConfig.minMainModeSteps = 3U;
  workflowConfig.createConfig.maxMainModeSteps = 5U;
  workflowConfig.createConfig.mainModeRepetition = 1U;
  workflowConfig.createConfig.mode0Steps = 1U;
  workflowConfig.createConfig.role = 0U;
  workflowConfig.createConfig.rttType = 1U;
  workflowConfig.createConfig.csSyncPhy = 2U;
  BleChannelSoundingRadio::fillValidChannelMap(workflowConfig.createConfig.channelMap);
  workflowConfig.createConfig.channelMapRepetition = 1U;
  workflowConfig.createConfig.channelSelectionType = 1U;
  workflowConfig.createConfig.ch3cShape = 1U;
  workflowConfig.createConfig.ch3cJump = 3U;
  workflowConfig.createConfig.csEnhancements1 = 0x01U;

  workflowConfig.procedureParameters.configId = 1U;
  workflowConfig.procedureParameters.maxProcedureLen = 12U;
  workflowConfig.procedureParameters.minProcedureInterval = 200U;
  workflowConfig.procedureParameters.maxProcedureInterval = 300U;
  workflowConfig.procedureParameters.maxProcedureCount = 8U;
  workflowConfig.procedureParameters.minSubeventLen = 0x000456UL;
  workflowConfig.procedureParameters.maxSubeventLen = 0x000678UL;
  workflowConfig.procedureParameters.toneAntennaConfigSelection = 2U;
  workflowConfig.procedureParameters.phy = 2U;
  workflowConfig.procedureParameters.txPowerDelta = -6;
  workflowConfig.procedureParameters.preferredPeerAntenna = 0xFFU;
  workflowConfig.procedureParameters.snrControlInitiator = 0U;
  workflowConfig.procedureParameters.snrControlReflector = 0U;

  workflowConfig.procedureEnable.configId = 1U;
  workflowConfig.procedureEnable.enable = 1U;

  const bool began = workflow.begin(kDemoConnHandle, workflowConfig);
  uint8_t capsV2Event[34] = {0};
  uint8_t securityEvent[3] = {0};
  uint8_t configEvent[33] = {0};
  uint8_t procedureEvent[21] = {0};

  const bool builtEvents =
      buildRemoteCapsCompleteEvent(capsV2Event, sizeof(capsV2Event), kDemoConnHandle, true) &&
      buildSecurityEnableCompleteEvent(securityEvent, sizeof(securityEvent), kDemoConnHandle) &&
      buildConfigCompleteEvent(configEvent, sizeof(configEvent), kDemoConnHandle) &&
      buildProcedureEnableCompleteEvent(procedureEvent, sizeof(procedureEvent), kDemoConnHandle);

  BleCsHciCommand command{};
  uint8_t builtCount = 0U;
  bool ok = began && builtEvents;

  for (uint8_t i = 0U; ok && i < 8U && workflow.active(); ++i) {
    if (!workflow.buildNextCommand(&command)) {
      ok = false;
      break;
    }
    ++builtCount;

    switch (command.opcode) {
      case kBleCsHciOpReadRemoteSupportedCapabilities:
        ok = workflow.consumeEvent(kBleCsHciEvtReadRemoteSupportedCapabilitiesCompleteV2,
                                   capsV2Event, sizeof(capsV2Event));
        break;
      case kBleCsHciOpSetDefaultSettings:
      case kBleCsHciOpSetProcedureParameters:
        ok = workflow.acknowledgeCommandStatus(command.opcode, 0U);
        break;
      case kBleCsHciOpCreateConfig:
        ok = workflow.consumeEvent(kBleCsHciEvtConfigComplete,
                                   configEvent, sizeof(configEvent));
        break;
      case kBleCsHciOpSecurityEnable:
        ok = workflow.consumeEvent(kBleCsHciEvtSecurityEnableComplete,
                                   securityEvent, sizeof(securityEvent));
        break;
      case kBleCsHciOpProcedureEnable:
        ok = workflow.consumeEvent(kBleCsHciEvtProcedureEnableComplete,
                                   procedureEvent, sizeof(procedureEvent));
        break;
      default:
        ok = false;
        break;
    }
  }

  Serial.print(F("hciworkflowdemo ok="));
  Serial.print((ok && workflow.ready()) ? 1 : 0);
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(workflow.phase()));
  Serial.print(F(" built="));
  Serial.print(builtCount);
  Serial.print(F(" last_status="));
  Serial.print(workflow.state().lastStatus);
  Serial.print(F(" defaults="));
  Serial.print(workflow.state().defaultSettingsApplied ? 1 : 0);
  Serial.print(F(" config="));
  Serial.print(workflow.state().configCreated ? 1 : 0);
  Serial.print(F(" security="));
  Serial.print(workflow.state().securityEnabled ? 1 : 0);
  Serial.print(F(" params="));
  Serial.print(workflow.state().procedureParametersApplied ? 1 : 0);
  Serial.print(F(" enabled="));
  Serial.print(workflow.state().procedureEnabled ? 1 : 0);
  Serial.print(F(" remote_cfg="));
  Serial.print(workflow.state().remoteCapabilities.numConfigSupported);
  Serial.print(F(" remote_3c="));
  Serial.println(workflow.state().remoteCapabilities.chselAlg3cSupported ? 1 : 0);
}

void printHciH4Demo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;

  BleCsControllerWorkflow workflow;
  BleCsControllerWorkflowConfig workflowConfig{};
  workflowConfig.applyDefaultSettings = true;
  workflowConfig.requireSecurityEnable = true;
  workflowConfig.defaultSettings.enableInitiatorRole = true;
  workflowConfig.defaultSettings.enableReflectorRole = true;
  workflowConfig.defaultSettings.csSyncAntennaSelection = 0xFEU;
  workflowConfig.defaultSettings.maxTxPowerDbm = -8;

  workflowConfig.createConfig.configId = 1U;
  workflowConfig.createConfig.createContext = 1U;
  workflowConfig.createConfig.mainModeType = kBleCsMainMode2;
  workflowConfig.createConfig.subModeType = 0xFFU;
  workflowConfig.createConfig.minMainModeSteps = 3U;
  workflowConfig.createConfig.maxMainModeSteps = 5U;
  workflowConfig.createConfig.mainModeRepetition = 1U;
  workflowConfig.createConfig.mode0Steps = 1U;
  workflowConfig.createConfig.role = 0U;
  workflowConfig.createConfig.rttType = 1U;
  workflowConfig.createConfig.csSyncPhy = 2U;
  BleChannelSoundingRadio::fillValidChannelMap(workflowConfig.createConfig.channelMap);
  workflowConfig.createConfig.channelMapRepetition = 1U;
  workflowConfig.createConfig.channelSelectionType = 1U;
  workflowConfig.createConfig.ch3cShape = 1U;
  workflowConfig.createConfig.ch3cJump = 3U;
  workflowConfig.createConfig.csEnhancements1 = 0x01U;

  workflowConfig.procedureParameters.configId = 1U;
  workflowConfig.procedureParameters.maxProcedureLen = 12U;
  workflowConfig.procedureParameters.minProcedureInterval = 200U;
  workflowConfig.procedureParameters.maxProcedureInterval = 300U;
  workflowConfig.procedureParameters.maxProcedureCount = 8U;
  workflowConfig.procedureParameters.minSubeventLen = 0x000456UL;
  workflowConfig.procedureParameters.maxSubeventLen = 0x000678UL;
  workflowConfig.procedureParameters.toneAntennaConfigSelection = 2U;
  workflowConfig.procedureParameters.phy = 2U;
  workflowConfig.procedureParameters.txPowerDelta = -6;
  workflowConfig.procedureParameters.preferredPeerAntenna = 0xFFU;
  workflowConfig.procedureParameters.snrControlInitiator = 0U;
  workflowConfig.procedureParameters.snrControlReflector = 0U;

  workflowConfig.procedureEnable.configId = 1U;
  workflowConfig.procedureEnable.enable = 1U;

  const bool began = workflow.begin(kDemoConnHandle, workflowConfig);

  uint8_t capsV2Payload[34] = {0};
  uint8_t securityPayload[3] = {0};
  uint8_t configPayload[33] = {0};
  uint8_t procedurePayload[21] = {0};
  uint8_t eventPacket[64] = {0};
  uint8_t commandPacket[80] = {0};

  const bool builtPayloads =
      buildRemoteCapsCompleteEvent(capsV2Payload, sizeof(capsV2Payload), kDemoConnHandle, true) &&
      buildSecurityEnableCompleteEvent(securityPayload, sizeof(securityPayload), kDemoConnHandle) &&
      buildConfigCompleteEvent(configPayload, sizeof(configPayload), kDemoConnHandle) &&
      buildProcedureEnableCompleteEvent(procedurePayload, sizeof(procedurePayload), kDemoConnHandle);

  BleCsHciCommand command{};
  size_t commandPacketLen = 0U;
  size_t totalCommandBytes = 0U;
  uint8_t builtCount = 0U;
  bool ok = began && builtPayloads;

  for (uint8_t i = 0U; ok && i < 8U && workflow.active(); ++i) {
    if (!workflow.buildNextCommand(&command) ||
        !BleChannelSoundingRadio::encodeHciCommandPacket(command, commandPacket,
                                                         sizeof(commandPacket),
                                                         &commandPacketLen)) {
      ok = false;
      break;
    }
    totalCommandBytes += commandPacketLen;
    ++builtCount;

    switch (command.opcode) {
      case kBleCsHciOpReadRemoteSupportedCapabilities:
        ok = buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket),
                                       command.opcode, 0U) &&
             workflow.consumeHciEventPacket(eventPacket, 7U) &&
             buildH4LeMetaEvent(eventPacket, sizeof(eventPacket),
                                kBleCsHciEvtReadRemoteSupportedCapabilitiesCompleteV2,
                                capsV2Payload, sizeof(capsV2Payload)) &&
             workflow.consumeHciEventPacket(eventPacket, 4U + sizeof(capsV2Payload));
        break;

      case kBleCsHciOpSetDefaultSettings:
        ok = buildH4CommandCompleteEvent(eventPacket, sizeof(eventPacket),
                                         command.opcode, 0U) &&
             workflow.consumeHciEventPacket(eventPacket, 7U);
        break;

      case kBleCsHciOpCreateConfig:
        ok = buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket),
                                       command.opcode, 0U) &&
             workflow.consumeHciEventPacket(eventPacket, 7U) &&
             buildH4LeMetaEvent(eventPacket, sizeof(eventPacket),
                                kBleCsHciEvtConfigComplete,
                                configPayload, sizeof(configPayload)) &&
             workflow.consumeHciEventPacket(eventPacket, 4U + sizeof(configPayload));
        break;

      case kBleCsHciOpSecurityEnable:
        ok = buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket),
                                       command.opcode, 0U) &&
             workflow.consumeHciEventPacket(eventPacket, 7U) &&
             buildH4LeMetaEvent(eventPacket, sizeof(eventPacket),
                                kBleCsHciEvtSecurityEnableComplete,
                                securityPayload, sizeof(securityPayload)) &&
             workflow.consumeHciEventPacket(eventPacket, 4U + sizeof(securityPayload));
        break;

      case kBleCsHciOpSetProcedureParameters:
        ok = buildH4CommandCompleteEvent(eventPacket, sizeof(eventPacket),
                                         command.opcode, 0U) &&
             workflow.consumeHciEventPacket(eventPacket, 7U);
        break;

      case kBleCsHciOpProcedureEnable:
        ok = buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket),
                                       command.opcode, 0U) &&
             workflow.consumeHciEventPacket(eventPacket, 7U) &&
             buildH4LeMetaEvent(eventPacket, sizeof(eventPacket),
                                kBleCsHciEvtProcedureEnableComplete,
                                procedurePayload, sizeof(procedurePayload)) &&
             workflow.consumeHciEventPacket(eventPacket, 4U + sizeof(procedurePayload));
        break;

      default:
        ok = false;
        break;
    }
  }

  Serial.print(F("hcih4demo ok="));
  Serial.print((ok && workflow.ready()) ? 1 : 0);
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(workflow.phase()));
  Serial.print(F(" built="));
  Serial.print(builtCount);
  Serial.print(F(" cmd_bytes="));
  Serial.print(totalCommandBytes);
  Serial.print(F(" last_status="));
  Serial.print(workflow.state().lastStatus);
  Serial.print(F(" defaults="));
  Serial.print(workflow.state().defaultSettingsApplied ? 1 : 0);
  Serial.print(F(" config="));
  Serial.print(workflow.state().configCreated ? 1 : 0);
  Serial.print(F(" security="));
  Serial.print(workflow.state().securityEnabled ? 1 : 0);
  Serial.print(F(" params="));
  Serial.print(workflow.state().procedureParametersApplied ? 1 : 0);
  Serial.print(F(" enabled="));
  Serial.println(workflow.state().procedureEnabled ? 1 : 0);
}

void printHciSessionDemoImpl(bool injectAcl, const __FlashStringHelper* label) {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t demoChannels[] = {0U, 12U, 24U, 36U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);
  static constexpr float kDemoDistanceMeters = 0.75f;
  static constexpr float kDemoAmplitude = 1024.0f;

  BleCsControllerSession session;
  BleCsControllerSessionConfig sessionConfig{};
  sessionConfig.localRoleIsInitiator = true;
  sessionConfig.workflow.applyDefaultSettings = true;
  sessionConfig.workflow.requireSecurityEnable = true;
  sessionConfig.workflow.defaultSettings.enableInitiatorRole = true;
  sessionConfig.workflow.defaultSettings.enableReflectorRole = true;
  sessionConfig.workflow.defaultSettings.csSyncAntennaSelection = 0xFEU;
  sessionConfig.workflow.defaultSettings.maxTxPowerDbm = -8;

  sessionConfig.workflow.createConfig.configId = 1U;
  sessionConfig.workflow.createConfig.createContext = 1U;
  sessionConfig.workflow.createConfig.mainModeType = kBleCsMainMode2;
  sessionConfig.workflow.createConfig.subModeType = 0xFFU;
  sessionConfig.workflow.createConfig.minMainModeSteps = 3U;
  sessionConfig.workflow.createConfig.maxMainModeSteps = 5U;
  sessionConfig.workflow.createConfig.mainModeRepetition = 1U;
  sessionConfig.workflow.createConfig.mode0Steps = 1U;
  sessionConfig.workflow.createConfig.role = 0U;
  sessionConfig.workflow.createConfig.rttType = 1U;
  sessionConfig.workflow.createConfig.csSyncPhy = 2U;
  BleChannelSoundingRadio::fillValidChannelMap(sessionConfig.workflow.createConfig.channelMap);
  sessionConfig.workflow.createConfig.channelMapRepetition = 1U;
  sessionConfig.workflow.createConfig.channelSelectionType = 1U;
  sessionConfig.workflow.createConfig.ch3cShape = 1U;
  sessionConfig.workflow.createConfig.ch3cJump = 3U;
  sessionConfig.workflow.createConfig.csEnhancements1 = 0x01U;

  sessionConfig.workflow.procedureParameters.configId = 1U;
  sessionConfig.workflow.procedureParameters.maxProcedureLen = 12U;
  sessionConfig.workflow.procedureParameters.minProcedureInterval = 200U;
  sessionConfig.workflow.procedureParameters.maxProcedureInterval = 300U;
  sessionConfig.workflow.procedureParameters.maxProcedureCount = 8U;
  sessionConfig.workflow.procedureParameters.minSubeventLen = 0x000456UL;
  sessionConfig.workflow.procedureParameters.maxSubeventLen = 0x000678UL;
  sessionConfig.workflow.procedureParameters.toneAntennaConfigSelection = 2U;
  sessionConfig.workflow.procedureParameters.phy = 2U;
  sessionConfig.workflow.procedureParameters.txPowerDelta = -6;
  sessionConfig.workflow.procedureParameters.preferredPeerAntenna = 0xFFU;
  sessionConfig.workflow.procedureParameters.snrControlInitiator = 0U;
  sessionConfig.workflow.procedureParameters.snrControlReflector = 0U;

  sessionConfig.workflow.procedureEnable.configId = 1U;
  sessionConfig.workflow.procedureEnable.enable = 1U;

  bool ok = session.begin(kDemoConnHandle, sessionConfig);

  uint8_t capsV2Payload[34] = {0};
  uint8_t securityPayload[3] = {0};
  uint8_t configPayload[33] = {0};
  uint8_t procedurePayload[21] = {0};
  ok = ok &&
       buildRemoteCapsCompleteEvent(capsV2Payload, sizeof(capsV2Payload), kDemoConnHandle, true) &&
       buildSecurityEnableCompleteEvent(securityPayload, sizeof(securityPayload), kDemoConnHandle) &&
       buildConfigCompleteEvent(configPayload, sizeof(configPayload), kDemoConnHandle) &&
       buildProcedureEnableCompleteEvent(procedurePayload, sizeof(procedurePayload),
                                         kDemoConnHandle);

  uint8_t localSteps[64] = {0};
  uint8_t peerSteps[64] = {0};
  size_t localLen = 0U;
  size_t peerLen = 0U;
  for (size_t i = 0U; ok && i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    const float freqHz =
        (2400.0f + static_cast<float>(csFrequencyOffsetMHz(channel))) * 1000000.0f;
    const float theta =
        -((4.0f * 3.14159265358979323846f * kDemoDistanceMeters * freqHz) /
          299792458.0f);
    const int16_t peerI = static_cast<int16_t>(lroundf(cosf(theta) * kDemoAmplitude));
    const int16_t peerQ = static_cast<int16_t>(lroundf(sinf(theta) * kDemoAmplitude));
    ok &= appendMode2DemoStep(localSteps, sizeof(localSteps), &localLen, channel, 1024, 0);
    ok &= appendMode2DemoStep(peerSteps, sizeof(peerSteps), &peerLen, channel, peerI, peerQ);
  }

  const size_t splitLen = 16U;
  uint8_t localInitPayload[64] = {0};
  uint8_t localContPayload[64] = {0};
  uint8_t peerInitPayload[64] = {0};
  uint8_t peerContPayload[64] = {0};
  ok = ok &&
       buildHciInitialEvent(localInitPayload, sizeof(localInitPayload), kDemoConnHandle, 1U,
                            0x1234U, 7U, 2U, 2U, localSteps, splitLen, true) &&
       buildHciContinueEvent(localContPayload, sizeof(localContPayload), kDemoConnHandle, 1U,
                             2U, 2U, localSteps + splitLen, localLen - splitLen, false) &&
       buildHciInitialEvent(peerInitPayload, sizeof(peerInitPayload), kDemoConnHandle, 1U,
                            0x1234U, 7U, 2U, 2U, peerSteps, splitLen, true) &&
       buildHciContinueEvent(peerContPayload, sizeof(peerContPayload), kDemoConnHandle, 1U,
                             2U, 2U, peerSteps + splitLen, peerLen - splitLen, false);

  uint8_t localInitPacket[96] = {0};
  uint8_t localContPacket[96] = {0};
  uint8_t peerInitPacket[96] = {0};
  uint8_t peerContPacket[96] = {0};
  uint8_t aclPacket[32] = {0};
  static const uint8_t aclPayload[] = {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U};
  ok = ok &&
       buildH4LeMetaEvent(localInitPacket, sizeof(localInitPacket),
                          kBleCsHciEvtSubeventResult, localInitPayload, 15U + splitLen) &&
       buildH4LeMetaEvent(localContPacket, sizeof(localContPacket),
                          kBleCsHciEvtSubeventResultContinue, localContPayload,
                          8U + (localLen - splitLen)) &&
       buildH4LeMetaEvent(peerInitPacket, sizeof(peerInitPacket),
                          kBleCsHciEvtSubeventResult, peerInitPayload, 15U + splitLen) &&
       buildH4LeMetaEvent(peerContPacket, sizeof(peerContPacket),
                          kBleCsHciEvtSubeventResultContinue, peerContPayload,
                          8U + (peerLen - splitLen)) &&
       buildH4AclPacket(aclPacket, sizeof(aclPacket), kDemoConnHandle, aclPayload,
                        sizeof(aclPayload));

  auto feedChunked = [](BleCsControllerSession& target,
                        const uint8_t* bytes,
                        size_t len,
                        bool workflow,
                        BleCsControllerResultSource source) -> bool {
    size_t offset = 0U;
    while (offset < len) {
      const size_t chunk = ((offset & 1U) == 0U) ? 3U : 5U;
      const size_t send = ((len - offset) < chunk) ? (len - offset) : chunk;
      const bool stepOk =
          workflow ? target.consumeWorkflowStreamBytes(bytes + offset, send)
                   : target.consumeResultStreamBytes(source, bytes + offset, send);
      if (!stepOk) {
        return false;
      }
      offset += send;
    }
    return true;
  };

  auto feedMaybeMixed = [&](BleCsControllerSession& target,
                            const uint8_t* bytes,
                            size_t len,
                            bool workflow,
                            BleCsControllerResultSource source) -> bool {
    if (injectAcl &&
        !feedChunked(target, aclPacket, 5U + sizeof(aclPayload), workflow, source)) {
      return false;
    }
    if (!feedChunked(target, bytes, len, workflow, source)) {
      return false;
    }
    if (injectAcl &&
        !feedChunked(target, aclPacket, 5U + sizeof(aclPayload), workflow, source)) {
      return false;
    }
    return true;
  };

  uint8_t eventPacket[64] = {0};
  uint8_t commandPacket[80] = {0};
  size_t commandPacketLen = 0U;
  size_t totalCommandBytes = 0U;
  uint8_t builtCount = 0U;

  while (ok && !session.ready() && !session.failed() && builtCount < 8U) {
    ok = session.buildNextCommandPacket(commandPacket, sizeof(commandPacket), &commandPacketLen);
    if (!ok) {
      break;
    }
    totalCommandBytes += commandPacketLen;
    ++builtCount;
    const uint16_t opcode = static_cast<uint16_t>(commandPacket[1U]) |
                            (static_cast<uint16_t>(commandPacket[2U]) << 8U);

    switch (opcode) {
      case kBleCsHciOpReadRemoteSupportedCapabilities:
        ok = buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
             feedMaybeMixed(session, eventPacket, 7U, true,
                            BleCsControllerResultSource::kLocal) &&
             buildH4LeMetaEvent(eventPacket, sizeof(eventPacket),
                                kBleCsHciEvtReadRemoteSupportedCapabilitiesCompleteV2,
                                capsV2Payload, sizeof(capsV2Payload)) &&
             feedMaybeMixed(session, eventPacket, 4U + sizeof(capsV2Payload), true,
                            BleCsControllerResultSource::kLocal);
        break;
      case kBleCsHciOpSetDefaultSettings:
        ok = buildH4CommandCompleteEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
             feedMaybeMixed(session, eventPacket, 7U, true,
                            BleCsControllerResultSource::kLocal);
        break;
      case kBleCsHciOpCreateConfig:
        ok = buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
             feedMaybeMixed(session, eventPacket, 7U, true,
                            BleCsControllerResultSource::kLocal) &&
             buildH4LeMetaEvent(eventPacket, sizeof(eventPacket), kBleCsHciEvtConfigComplete,
                                configPayload, sizeof(configPayload)) &&
             feedMaybeMixed(session, eventPacket, 4U + sizeof(configPayload), true,
                            BleCsControllerResultSource::kLocal);
        break;
      case kBleCsHciOpSecurityEnable:
        ok = buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
             feedMaybeMixed(session, eventPacket, 7U, true,
                            BleCsControllerResultSource::kLocal) &&
             buildH4LeMetaEvent(eventPacket, sizeof(eventPacket),
                                kBleCsHciEvtSecurityEnableComplete, securityPayload,
                                sizeof(securityPayload)) &&
             feedMaybeMixed(session, eventPacket, 4U + sizeof(securityPayload), true,
                            BleCsControllerResultSource::kLocal);
        break;
      case kBleCsHciOpSetProcedureParameters:
        ok = buildH4CommandCompleteEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
             feedMaybeMixed(session, eventPacket, 7U, true,
                            BleCsControllerResultSource::kLocal);
        break;
      case kBleCsHciOpProcedureEnable:
        ok = buildH4CommandStatusEvent(eventPacket, sizeof(eventPacket), opcode, 0U) &&
             feedMaybeMixed(session, eventPacket, 7U, true,
                            BleCsControllerResultSource::kLocal) &&
             buildH4LeMetaEvent(eventPacket, sizeof(eventPacket),
                                kBleCsHciEvtProcedureEnableComplete, procedurePayload,
                                sizeof(procedurePayload)) &&
             feedMaybeMixed(session, eventPacket, 4U + sizeof(procedurePayload), true,
                            BleCsControllerResultSource::kLocal);
        break;
      default:
        ok = false;
        break;
    }
  }

  ok = ok &&
       feedMaybeMixed(session, localInitPacket, 4U + 15U + splitLen, false,
                      BleCsControllerResultSource::kLocal) &&
       feedMaybeMixed(session, peerInitPacket, 4U + 15U + splitLen, false,
                      BleCsControllerResultSource::kPeer) &&
       feedMaybeMixed(session, localContPacket, 4U + 8U + (localLen - splitLen), false,
                      BleCsControllerResultSource::kLocal) &&
       feedMaybeMixed(session, peerContPacket, 4U + 8U + (peerLen - splitLen), false,
                      BleCsControllerResultSource::kPeer);

  Serial.print(label);
  Serial.print(F(" ok="));
  Serial.print((ok && session.ready() && session.estimateValid()) ? 1 : 0);
  Serial.print(F(" built="));
  Serial.print(builtCount);
  Serial.print(F(" cmd_bytes="));
  Serial.print(totalCommandBytes);
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(session.workflowState().phase));
  Serial.print(F(" wf_ready="));
  Serial.print(session.state().workflowReady ? 1 : 0);
  Serial.print(F(" local="));
  Serial.print(session.state().localResultComplete ? 1 : 0);
  Serial.print(F(" peer="));
  Serial.print(session.state().peerResultComplete ? 1 : 0);
  Serial.print(F(" ign_wf="));
  Serial.print(session.state().workflowIgnoredPackets);
  Serial.print('/');
  Serial.print(session.state().workflowIgnoredBytes);
  Serial.print(F(" ign_local="));
  Serial.print(session.state().localIgnoredPackets);
  Serial.print('/');
  Serial.print(session.state().localIgnoredBytes);
  Serial.print(F(" ign_peer="));
  Serial.print(session.state().peerIgnoredPackets);
  Serial.print('/');
  Serial.print(session.state().peerIgnoredBytes);
  Serial.print(F(" proc="));
  Serial.print(session.state().completedProcedureCounter);
  Serial.print(F(" dist_m="));
  if (session.state().estimateValid) {
    Serial.println(session.state().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciSessionDemo() { printHciSessionDemoImpl(false, F("hcisessiondemo")); }

void printHciMixedStreamDemo() { printHciSessionDemoImpl(true, F("hcimixdemo")); }

void printHciHostDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t demoChannels[] = {0U, 12U, 24U, 36U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);
  static constexpr float kDemoDistanceMeters = 0.75f;
  static constexpr float kDemoAmplitude = 1024.0f;
  static const uint8_t aclPayload[] = {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U};

  BleCsControllerHost host;
  BleCsControllerHostConfig hostConfig{};
  hostConfig.session.localRoleIsInitiator = true;
  hostConfig.session.workflow.applyDefaultSettings = true;
  hostConfig.session.workflow.requireSecurityEnable = true;
  hostConfig.session.workflow.defaultSettings.enableInitiatorRole = true;
  hostConfig.session.workflow.defaultSettings.enableReflectorRole = true;
  hostConfig.session.workflow.defaultSettings.csSyncAntennaSelection = 0xFEU;
  hostConfig.session.workflow.defaultSettings.maxTxPowerDbm = -8;

  hostConfig.session.workflow.createConfig.configId = 1U;
  hostConfig.session.workflow.createConfig.createContext = 1U;
  hostConfig.session.workflow.createConfig.mainModeType = kBleCsMainMode2;
  hostConfig.session.workflow.createConfig.subModeType = 0xFFU;
  hostConfig.session.workflow.createConfig.minMainModeSteps = 3U;
  hostConfig.session.workflow.createConfig.maxMainModeSteps = 5U;
  hostConfig.session.workflow.createConfig.mainModeRepetition = 1U;
  hostConfig.session.workflow.createConfig.mode0Steps = 1U;
  hostConfig.session.workflow.createConfig.role = 0U;
  hostConfig.session.workflow.createConfig.rttType = 1U;
  hostConfig.session.workflow.createConfig.csSyncPhy = 2U;
  BleChannelSoundingRadio::fillValidChannelMap(
      hostConfig.session.workflow.createConfig.channelMap);
  hostConfig.session.workflow.createConfig.channelMapRepetition = 1U;
  hostConfig.session.workflow.createConfig.channelSelectionType = 1U;
  hostConfig.session.workflow.createConfig.ch3cShape = 1U;
  hostConfig.session.workflow.createConfig.ch3cJump = 3U;
  hostConfig.session.workflow.createConfig.csEnhancements1 = 0x01U;

  hostConfig.session.workflow.procedureParameters.configId = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 12U;
  hostConfig.session.workflow.procedureParameters.minProcedureInterval = 200U;
  hostConfig.session.workflow.procedureParameters.maxProcedureInterval = 300U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 8U;
  hostConfig.session.workflow.procedureParameters.minSubeventLen = 0x000456UL;
  hostConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000678UL;
  hostConfig.session.workflow.procedureParameters.toneAntennaConfigSelection = 2U;
  hostConfig.session.workflow.procedureParameters.phy = 2U;
  hostConfig.session.workflow.procedureParameters.txPowerDelta = -6;
  hostConfig.session.workflow.procedureParameters.preferredPeerAntenna = 0xFFU;
  hostConfig.session.workflow.procedureParameters.snrControlInitiator = 0U;
  hostConfig.session.workflow.procedureParameters.snrControlReflector = 0U;

  hostConfig.session.workflow.procedureEnable.configId = 1U;
  hostConfig.session.workflow.procedureEnable.enable = 1U;

  HciHostDemoContext context{};
  context.host = &host;
  context.injectAcl = true;
  hostConfig.sendPacket = hciHostDemoSendPacket;
  hostConfig.userData = &context;

  bool ok =
      buildRemoteCapsCompleteEvent(context.capsV2Payload, sizeof(context.capsV2Payload),
                                   kDemoConnHandle, true) &&
      buildSecurityEnableCompleteEvent(context.securityPayload,
                                       sizeof(context.securityPayload), kDemoConnHandle) &&
      buildConfigCompleteEvent(context.configPayload, sizeof(context.configPayload),
                               kDemoConnHandle) &&
      buildProcedureEnableCompleteEvent(context.procedurePayload,
                                        sizeof(context.procedurePayload), kDemoConnHandle) &&
      buildH4AclPacket(context.aclPacket, sizeof(context.aclPacket), kDemoConnHandle,
                       aclPayload, sizeof(aclPayload)) &&
      host.begin(kDemoConnHandle, hostConfig);

  uint8_t localSteps[64] = {0};
  uint8_t peerSteps[64] = {0};
  size_t localLen = 0U;
  size_t peerLen = 0U;
  for (size_t i = 0U; ok && i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    const float freqHz =
        (2400.0f + static_cast<float>(csFrequencyOffsetMHz(channel))) * 1000000.0f;
    const float theta =
        -((4.0f * 3.14159265358979323846f * kDemoDistanceMeters * freqHz) /
          299792458.0f);
    const int16_t peerI = static_cast<int16_t>(lroundf(cosf(theta) * kDemoAmplitude));
    const int16_t peerQ = static_cast<int16_t>(lroundf(sinf(theta) * kDemoAmplitude));
    ok &= appendMode2DemoStep(localSteps, sizeof(localSteps), &localLen, channel, 1024, 0);
    ok &= appendMode2DemoStep(peerSteps, sizeof(peerSteps), &peerLen, channel, peerI, peerQ);
  }

  static constexpr size_t kSplitLen = 16U;
  uint8_t localInitPayload[64] = {0};
  uint8_t localContPayload[64] = {0};
  uint8_t peerInitPayload[64] = {0};
  uint8_t peerContPayload[64] = {0};
  uint8_t localInitPacket[96] = {0};
  uint8_t localContPacket[96] = {0};
  uint8_t peerInitPacket[96] = {0};
  uint8_t peerContPacket[96] = {0};

  ok = ok &&
       buildHciInitialEvent(localInitPayload, sizeof(localInitPayload), kDemoConnHandle, 1U,
                            0x1234U, 7U, 2U, 2U, localSteps, kSplitLen, true) &&
       buildHciContinueEvent(localContPayload, sizeof(localContPayload), kDemoConnHandle, 1U,
                             2U, 2U, localSteps + kSplitLen, localLen - kSplitLen, false) &&
       buildHciInitialEvent(peerInitPayload, sizeof(peerInitPayload), kDemoConnHandle, 1U,
                            0x1234U, 7U, 2U, 2U, peerSteps, kSplitLen, true) &&
       buildHciContinueEvent(peerContPayload, sizeof(peerContPayload), kDemoConnHandle, 1U,
                             2U, 2U, peerSteps + kSplitLen, peerLen - kSplitLen, false) &&
       buildH4LeMetaEvent(localInitPacket, sizeof(localInitPacket),
                          kBleCsHciEvtSubeventResult, localInitPayload, 15U + kSplitLen) &&
       buildH4LeMetaEvent(localContPacket, sizeof(localContPacket),
                          kBleCsHciEvtSubeventResultContinue, localContPayload,
                          8U + (localLen - kSplitLen)) &&
       buildH4LeMetaEvent(peerInitPacket, sizeof(peerInitPacket),
                          kBleCsHciEvtSubeventResult, peerInitPayload, 15U + kSplitLen) &&
       buildH4LeMetaEvent(peerContPacket, sizeof(peerContPacket),
                          kBleCsHciEvtSubeventResultContinue, peerContPayload,
                          8U + (peerLen - kSplitLen));

  uint8_t pumpCount = 0U;
  while (ok && !host.ready() && !host.failed() && pumpCount < 8U) {
    ok = host.pumpCommands(1U);
    ++pumpCount;
  }

  auto feedController = [&](const uint8_t* bytes, size_t len) -> bool {
    return feedHostIngressChunked(host, BleCsControllerIngressSource::kController,
                                  context.aclPacket, 11U) &&
           feedHostIngressChunked(host, BleCsControllerIngressSource::kController, bytes, len) &&
           feedHostIngressChunked(host, BleCsControllerIngressSource::kController,
                                  context.aclPacket, 11U);
  };

  ok = ok && host.ready() &&
       feedController(localInitPacket, 4U + 15U + kSplitLen) &&
       feedController(localContPacket, 4U + 8U + (localLen - kSplitLen)) &&
       feedHostIngressChunked(host, BleCsControllerIngressSource::kPeerResult,
                              peerInitPacket, 4U + 15U + kSplitLen) &&
       feedHostIngressChunked(host, BleCsControllerIngressSource::kPeerResult,
                              peerContPacket, 4U + 8U + (peerLen - kSplitLen));

  Serial.print(F("hcihostdemo ok="));
  Serial.print((ok && host.ready() && host.estimateValid()) ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" sent="));
  Serial.print(host.state().sentCommandPackets);
  Serial.print('/');
  Serial.print(host.state().sentCommandBytes);
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(host.workflowState().phase));
  Serial.print(F(" ctrl_evt="));
  Serial.print(host.state().controllerEventPackets);
  Serial.print(F(" local_evt="));
  Serial.print(host.state().localResultPackets);
  Serial.print(F(" peer_evt="));
  Serial.print(host.state().peerResultPackets);
  Serial.print(F(" ctrl_ign="));
  Serial.print(host.state().controllerIgnoredPackets);
  Serial.print('/');
  Serial.print(host.state().controllerIgnoredBytes);
  Serial.print(F(" proc="));
  Serial.print(host.sessionState().completedProcedureCounter);
  Serial.print(F(" dist_m="));
  if (host.estimateValid()) {
    Serial.println(host.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciStreamDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t demoChannels[] = {0U, 12U, 24U, 36U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);
  static constexpr float kDemoDistanceMeters = 0.75f;
  static constexpr float kDemoAmplitude = 1024.0f;
  static const uint8_t aclPayload[] = {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U};

  HciDemoControllerStream controllerStream;
  ByteQueueStream peerStream;
  BleCsControllerStreamHost host;
  BleCsControllerStreamHostConfig hostConfig{};
  hostConfig.controllerStream = &controllerStream;
  hostConfig.peerResultStream = &peerStream;
  hostConfig.maxCommandsPerPump = 1U;
  hostConfig.maxControllerBytesPerPoll = 128U;
  hostConfig.maxPeerBytesPerPoll = 128U;
  hostConfig.session.localRoleIsInitiator = true;
  hostConfig.session.workflow.applyDefaultSettings = true;
  hostConfig.session.workflow.requireSecurityEnable = true;
  hostConfig.session.workflow.defaultSettings.enableInitiatorRole = true;
  hostConfig.session.workflow.defaultSettings.enableReflectorRole = true;
  hostConfig.session.workflow.defaultSettings.csSyncAntennaSelection = 0xFEU;
  hostConfig.session.workflow.defaultSettings.maxTxPowerDbm = -8;

  hostConfig.session.workflow.createConfig.configId = 1U;
  hostConfig.session.workflow.createConfig.createContext = 1U;
  hostConfig.session.workflow.createConfig.mainModeType = kBleCsMainMode2;
  hostConfig.session.workflow.createConfig.subModeType = 0xFFU;
  hostConfig.session.workflow.createConfig.minMainModeSteps = 3U;
  hostConfig.session.workflow.createConfig.maxMainModeSteps = 5U;
  hostConfig.session.workflow.createConfig.mainModeRepetition = 1U;
  hostConfig.session.workflow.createConfig.mode0Steps = 1U;
  hostConfig.session.workflow.createConfig.role = 0U;
  hostConfig.session.workflow.createConfig.rttType = 1U;
  hostConfig.session.workflow.createConfig.csSyncPhy = 2U;
  BleChannelSoundingRadio::fillValidChannelMap(
      hostConfig.session.workflow.createConfig.channelMap);
  hostConfig.session.workflow.createConfig.channelMapRepetition = 1U;
  hostConfig.session.workflow.createConfig.channelSelectionType = 1U;
  hostConfig.session.workflow.createConfig.ch3cShape = 1U;
  hostConfig.session.workflow.createConfig.ch3cJump = 3U;
  hostConfig.session.workflow.createConfig.csEnhancements1 = 0x01U;

  hostConfig.session.workflow.procedureParameters.configId = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 12U;
  hostConfig.session.workflow.procedureParameters.minProcedureInterval = 200U;
  hostConfig.session.workflow.procedureParameters.maxProcedureInterval = 300U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 8U;
  hostConfig.session.workflow.procedureParameters.minSubeventLen = 0x000456UL;
  hostConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000678UL;
  hostConfig.session.workflow.procedureParameters.toneAntennaConfigSelection = 2U;
  hostConfig.session.workflow.procedureParameters.phy = 2U;
  hostConfig.session.workflow.procedureParameters.txPowerDelta = -6;
  hostConfig.session.workflow.procedureParameters.preferredPeerAntenna = 0xFFU;
  hostConfig.session.workflow.procedureParameters.snrControlInitiator = 0U;
  hostConfig.session.workflow.procedureParameters.snrControlReflector = 0U;

  hostConfig.session.workflow.procedureEnable.configId = 1U;
  hostConfig.session.workflow.procedureEnable.enable = 1U;

  HciHostDemoContext context{};
  context.injectAcl = true;
  bool ok =
      buildRemoteCapsCompleteEvent(context.capsV2Payload, sizeof(context.capsV2Payload),
                                   kDemoConnHandle, true) &&
      buildSecurityEnableCompleteEvent(context.securityPayload,
                                       sizeof(context.securityPayload), kDemoConnHandle) &&
      buildConfigCompleteEvent(context.configPayload, sizeof(context.configPayload),
                               kDemoConnHandle) &&
      buildProcedureEnableCompleteEvent(context.procedurePayload,
                                        sizeof(context.procedurePayload), kDemoConnHandle) &&
      buildH4AclPacket(context.aclPacket, sizeof(context.aclPacket), kDemoConnHandle,
                       aclPayload, sizeof(aclPayload));
  controllerStream.setContext(&context);

  uint8_t localSteps[64] = {0};
  uint8_t peerSteps[64] = {0};
  size_t localLen = 0U;
  size_t peerLen = 0U;
  for (size_t i = 0U; ok && i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    const float freqHz =
        (2400.0f + static_cast<float>(csFrequencyOffsetMHz(channel))) * 1000000.0f;
    const float theta =
        -((4.0f * 3.14159265358979323846f * kDemoDistanceMeters * freqHz) /
          299792458.0f);
    const int16_t peerI = static_cast<int16_t>(lroundf(cosf(theta) * kDemoAmplitude));
    const int16_t peerQ = static_cast<int16_t>(lroundf(sinf(theta) * kDemoAmplitude));
    ok &= appendMode2DemoStep(localSteps, sizeof(localSteps), &localLen, channel, 1024, 0);
    ok &= appendMode2DemoStep(peerSteps, sizeof(peerSteps), &peerLen, channel, peerI, peerQ);
  }

  static constexpr size_t kSplitLen = 16U;
  uint8_t localInitPayload[64] = {0};
  uint8_t localContPayload[64] = {0};
  uint8_t peerInitPayload[64] = {0};
  uint8_t peerContPayload[64] = {0};
  uint8_t localInitPacket[96] = {0};
  uint8_t localContPacket[96] = {0};
  uint8_t peerInitPacket[96] = {0};
  uint8_t peerContPacket[96] = {0};

  ok = ok &&
       buildHciInitialEvent(localInitPayload, sizeof(localInitPayload), kDemoConnHandle, 1U,
                            0x1234U, 7U, 2U, 2U, localSteps, kSplitLen, true) &&
       buildHciContinueEvent(localContPayload, sizeof(localContPayload), kDemoConnHandle, 1U,
                             2U, 2U, localSteps + kSplitLen, localLen - kSplitLen, false) &&
       buildHciInitialEvent(peerInitPayload, sizeof(peerInitPayload), kDemoConnHandle, 1U,
                            0x1234U, 7U, 2U, 2U, peerSteps, kSplitLen, true) &&
       buildHciContinueEvent(peerContPayload, sizeof(peerContPayload), kDemoConnHandle, 1U,
                             2U, 2U, peerSteps + kSplitLen, peerLen - kSplitLen, false) &&
       buildH4LeMetaEvent(localInitPacket, sizeof(localInitPacket),
                          kBleCsHciEvtSubeventResult, localInitPayload, 15U + kSplitLen) &&
       buildH4LeMetaEvent(localContPacket, sizeof(localContPacket),
                          kBleCsHciEvtSubeventResultContinue, localContPayload,
                          8U + (localLen - kSplitLen)) &&
       buildH4LeMetaEvent(peerInitPacket, sizeof(peerInitPacket),
                          kBleCsHciEvtSubeventResult, peerInitPayload, 15U + kSplitLen) &&
       buildH4LeMetaEvent(peerContPacket, sizeof(peerContPacket),
                          kBleCsHciEvtSubeventResultContinue, peerContPayload,
                          8U + (peerLen - kSplitLen)) &&
       host.begin(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !host.ready() && !host.failed() && pumpCount < 48U) {
    ok = host.loopOnce();
    ++pumpCount;
  }

  ok = ok && host.ready() && controllerStream.enqueueIncoming(context.aclPacket, 11U) &&
       controllerStream.enqueueIncoming(localInitPacket, 4U + 15U + kSplitLen) &&
       controllerStream.enqueueIncoming(context.aclPacket, 11U) &&
       controllerStream.enqueueIncoming(localContPacket, 4U + 8U + (localLen - kSplitLen)) &&
       peerStream.enqueue(peerInitPacket, 4U + 15U + kSplitLen) &&
       peerStream.enqueue(peerContPacket, 4U + 8U + (peerLen - kSplitLen));

  for (uint8_t i = 0U; ok && !host.estimateValid() && i < 8U; ++i) {
    ok = host.poll();
  }

  Serial.print(F("hcistreamdemo ok="));
  Serial.print((ok && host.ready() && host.estimateValid()) ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" wrote="));
  Serial.print(host.state().controllerPacketsWritten);
  Serial.print('/');
  Serial.print(host.state().controllerBytesWritten);
  Serial.print(F(" read="));
  Serial.print(host.state().controllerBytesRead);
  Serial.print('/');
  Serial.print(host.state().peerBytesRead);
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(host.workflowState().phase));
  Serial.print(F(" ctrl_evt="));
  Serial.print(host.hostState().controllerEventPackets);
  Serial.print(F(" peer_evt="));
  Serial.print(host.hostState().peerResultPackets);
  Serial.print(F(" ctrl_ign="));
  Serial.print(host.hostState().controllerIgnoredPackets);
  Serial.print('/');
  Serial.print(host.hostState().controllerIgnoredBytes);
  Serial.print(F(" proc="));
  Serial.print(host.sessionState().completedProcedureCounter);
  Serial.print(F(" dist_m="));
  if (host.estimateValid()) {
    Serial.println(host.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprTransportDemo() {
  volatile Nrf54l15VprTransportHostShared* sharedHost = nrf54l15_vpr_transport_host_shared();
  volatile Nrf54l15VprTransportVprShared* sharedVpr = nrf54l15_vpr_transport_vpr_shared();
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t demoChannels[] = {2U, 14U, 26U, 38U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);

  BleCsControllerVprHost vprHost;
  Serial.println(F("hcivprtransportdemo stage=begin"));
  Serial.flush();
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  // Override the stub defaults through the real Create Config channel map path.
  memset(hostConfig.session.workflow.createConfig.channelMap, 0,
         sizeof(hostConfig.session.workflow.createConfig.channelMap));
  for (size_t i = 0U; i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    hostConfig.session.workflow.createConfig.channelMap[channel >> 3U] |=
        static_cast<uint8_t>(1U << (channel & 0x07U));
  }
  hostConfig.session.workflow.createConfig.minMainModeSteps = 4U;
  hostConfig.session.workflow.createConfig.maxMainModeSteps = 6U;
  hostConfig.session.workflow.createConfig.mainModeRepetition = 2U;
  hostConfig.session.workflow.createConfig.channelMapRepetition = 2U;
  hostConfig.session.workflow.defaultSettings.maxTxPowerDbm = -9;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 17U;
  hostConfig.session.workflow.procedureParameters.minProcedureInterval = 111U;
  hostConfig.session.workflow.procedureParameters.maxProcedureInterval = 222U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 5U;
  hostConfig.session.workflow.procedureParameters.toneAntennaConfigSelection = 3U;
  hostConfig.session.workflow.procedureParameters.txPowerDelta = -4;

  bool ok = true;

  ok = ok && vprHost.resetTransport(true);
  Serial.print(F("hcivprtransportdemo stage=shared ok="));
  Serial.println(ok ? 1 : 0);
  Serial.flush();
  Serial.print(F("hcivprtransportdemo stage=builtins ok="));
  Serial.println(1);
  Serial.flush();
  ok = ok && vprHost.loadDefaultTransportImage();
  Serial.print(F("hcivprtransportdemo stage=vpr_image ok="));
  Serial.println(ok ? 1 : 0);
  Serial.flush();
  ok = ok && vprHost.bootTransport();
  Serial.print(F("hcivprtransportdemo stage=vpr_boot ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" hb="));
  Serial.print(vprHost.vprState().heartbeat);
  Serial.print(F(" run="));
  Serial.print(vprHost.vprState().running ? 1 : 0);
  Serial.print(F(" initpc=0x"));
  Serial.print(vprHost.transport().initPc(), HEX);
  Serial.print(F(" sec="));
  Serial.print(vprHost.vprState().secureAccessEnabled ? 1 : 0);
  Serial.print(F(" perm=0x"));
  Serial.print(vprHost.transport().spuPerm(), HEX);
  Serial.print(F(" status=0x"));
  Serial.print(vprHost.vprState().transportStatus, HEX);
  Serial.print(F(" err=0x"));
  Serial.print(vprHost.vprState().lastError, HEX);
  Serial.print(F(" dm=0x"));
  Serial.print(vprHost.transport().debugStatus(), HEX);
  Serial.print(F(" h0=0x"));
  Serial.print(vprHost.transport().haltSummary0(), HEX);
  Serial.print(F(" h1=0x"));
  Serial.print(vprHost.transport().haltSummary1(), HEX);
  Serial.print(F(" tasks=0x"));
  Serial.print(vprHost.transport().rawNordicTasks(), HEX);
  Serial.print(F(" mpc_evt=0x"));
  Serial.print(vprHost.transport().rawMpcMemAccErrEvent(), HEX);
  Serial.print(F(" mpc_addr=0x"));
  Serial.print(vprHost.transport().rawMpcMemAccErrAddress(), HEX);
  Serial.print(F(" mpc_info=0x"));
  Serial.print(vprHost.transport().rawMpcMemAccErrInfo(), HEX);
  Serial.print(F(" dbg=0x"));
  Serial.println(sharedVpr->reserved, HEX);
  Serial.flush();
  Serial.print(F("hcivprtransportdemo stage=vpr_ready ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" hb="));
  Serial.print(vprHost.vprState().heartbeat);
  Serial.print(F(" run="));
  Serial.print(vprHost.vprState().running ? 1 : 0);
  Serial.print(F(" initpc=0x"));
  Serial.print(vprHost.transport().initPc(), HEX);
  Serial.print(F(" sec="));
  Serial.print(vprHost.vprState().secureAccessEnabled ? 1 : 0);
  Serial.print(F(" perm=0x"));
  Serial.print(vprHost.transport().spuPerm(), HEX);
  Serial.print(F(" status=0x"));
  Serial.print(vprHost.vprState().transportStatus, HEX);
  Serial.print(F(" err=0x"));
  Serial.print(vprHost.vprState().lastError, HEX);
  Serial.print(F(" dm=0x"));
  Serial.print(vprHost.transport().debugStatus(), HEX);
  Serial.print(F(" h0=0x"));
  Serial.print(vprHost.transport().haltSummary0(), HEX);
  Serial.print(F(" h1=0x"));
  Serial.print(vprHost.transport().haltSummary1(), HEX);
  Serial.print(F(" tasks=0x"));
  Serial.print(vprHost.transport().rawNordicTasks(), HEX);
  Serial.print(F(" mpc_evt=0x"));
  Serial.print(vprHost.transport().rawMpcMemAccErrEvent(), HEX);
  Serial.print(F(" mpc_addr=0x"));
  Serial.print(vprHost.transport().rawMpcMemAccErrAddress(), HEX);
  Serial.print(F(" mpc_info=0x"));
  Serial.print(vprHost.transport().rawMpcMemAccErrInfo(), HEX);
  Serial.print(F(" dbg=0x"));
  Serial.println(sharedVpr->reserved, HEX);
  Serial.flush();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);
  Serial.print(F("hcivprtransportdemo stage=host_begin ok="));
  Serial.println(ok ? 1 : 0);
  Serial.flush();

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }

  ok = ok && vprHost.ready();

  for (uint8_t i = 0U; ok && !vprHost.estimateValid() && i < 8U; ++i) {
    ok = vprHost.poll();
  }

  Serial.print(F("hcivprtransportdemo ok="));
  Serial.print((ok && vprHost.ready() && vprHost.estimateValid()) ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" wrote="));
  Serial.print(vprHost.streamState().controllerPacketsWritten);
  Serial.print('/');
  Serial.print(vprHost.streamState().controllerBytesWritten);
  Serial.print(F(" read="));
  Serial.print(vprHost.streamState().controllerBytesRead);
  Serial.print('/');
  Serial.print(vprHost.streamState().peerBytesRead);
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(vprHost.workflowState().phase));
  Serial.print(F(" hb="));
  Serial.print(vprHost.vprState().heartbeat);
  Serial.print(F(" opcode=0x"));
  Serial.print(vprHost.vprState().lastOpcode, HEX);
  Serial.print(F(" status=0x"));
  Serial.print(vprHost.vprState().transportStatus, HEX);
  Serial.print(F(" hs="));
  Serial.print(sharedHost->hostSeq);
  Serial.print('/');
  Serial.print(sharedHost->hostFlags, HEX);
  Serial.print(F(" vs="));
  Serial.print(sharedVpr->vprSeq);
  Serial.print('/');
  Serial.print(sharedVpr->vprFlags, HEX);
  Serial.print(F(" mpc_evt=0x"));
  Serial.print(vprHost.transport().rawMpcMemAccErrEvent(), HEX);
  Serial.print(F(" mpc_addr=0x"));
  Serial.print(vprHost.transport().rawMpcMemAccErrAddress(), HEX);
  Serial.print(F(" mpc_info=0x"));
  Serial.print(vprHost.transport().rawMpcMemAccErrInfo(), HEX);
  Serial.print(F(" dbg=0x"));
  Serial.print(sharedVpr->reserved, HEX);
  Serial.print(F(" ctrl_evt="));
  Serial.print(vprHost.hostState().controllerEventPackets);
  Serial.print(F(" peer_trig="));
  Serial.print(vprHost.hostState().vendorPeerResultTriggers);
  Serial.print(F(" peer_mark="));
  Serial.print(vprHost.hostState().controllerPeerResultMarkers);
  Serial.print(F(" peer_evt="));
  Serial.print(vprHost.hostState().peerResultPackets);
  Serial.print(F(" cfg_ch="));
  for (size_t i = 0U; i < kDemoChannelCount; ++i) {
    if (i != 0U) {
      Serial.print(',');
    }
    Serial.print(demoChannels[i]);
  }
  Serial.print(F(" cfg_steps="));
  Serial.print(vprHost.workflowState().configComplete.minMainModeSteps);
  Serial.print('-');
  Serial.print(vprHost.workflowState().configComplete.maxMainModeSteps);
  Serial.print(F(" cfg_rep="));
  Serial.print(vprHost.workflowState().configComplete.mainModeRepetition);
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" proc="));
  Serial.print(vprHost.sessionState().completedProcedureCounter);
  Serial.print(F(" proc_cnt="));
  Serial.print(vprHost.workflowState().procedureEnableComplete.procedureCount);
  Serial.print(F(" proc_len="));
  Serial.print(vprHost.workflowState().procedureEnableComplete.maxProcedureLen);
  Serial.print(F(" tone_sel="));
  Serial.print(vprHost.workflowState().procedureEnableComplete.toneAntennaConfigSelection);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprDumpDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t demoChannels[] = {2U, 14U, 26U, 38U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  memset(hostConfig.session.workflow.createConfig.channelMap, 0,
         sizeof(hostConfig.session.workflow.createConfig.channelMap));
  for (size_t i = 0U; i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    hostConfig.session.workflow.createConfig.channelMap[channel >> 3U] |=
        static_cast<uint8_t>(1U << (channel & 0x07U));
  }
  hostConfig.session.workflow.createConfig.minMainModeSteps = 4U;
  hostConfig.session.workflow.createConfig.maxMainModeSteps = 6U;
  hostConfig.session.workflow.createConfig.mainModeRepetition = 2U;
  hostConfig.session.workflow.createConfig.channelMapRepetition = 2U;
  hostConfig.session.workflow.defaultSettings.maxTxPowerDbm = -9;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 17U;
  hostConfig.session.workflow.procedureParameters.minProcedureInterval = 111U;
  hostConfig.session.workflow.procedureParameters.maxProcedureInterval = 222U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 5U;
  hostConfig.session.workflow.procedureParameters.toneAntennaConfigSelection = 3U;
  hostConfig.session.workflow.procedureParameters.txPowerDelta = -4;

  bool ok = vprHost.resetTransport(true) && vprHost.loadDefaultTransportImage() &&
            vprHost.bootTransport() && vprHost.beginHost(kDemoConnHandle, hostConfig);
  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready();
  for (uint8_t i = 0U; ok && !vprHost.estimateValid() && i < 8U; ++i) {
    ok = vprHost.poll();
  }

  Serial.print(F("hcivprdumpdemo ok="));
  Serial.print((ok && vprHost.ready() && vprHost.estimateValid()) ? 1 : 0);
  Serial.print(F(" proc="));
  Serial.print(vprHost.sessionState().completedProcedureCounter);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
  printSubeventResultDump(F("local"), vprHost.completedLocalResult());
  printSubeventResultDump(F("peer"), vprHost.completedPeerResult());
}

void printHciVprRttOffDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t demoChannels[] = {2U, 14U, 26U, 38U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  memset(hostConfig.session.workflow.createConfig.channelMap, 0,
         sizeof(hostConfig.session.workflow.createConfig.channelMap));
  for (size_t i = 0U; i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    hostConfig.session.workflow.createConfig.channelMap[channel >> 3U] |=
        static_cast<uint8_t>(1U << (channel & 0x07U));
  }
  hostConfig.session.workflow.createConfig.minMainModeSteps = 4U;
  hostConfig.session.workflow.createConfig.maxMainModeSteps = 6U;
  hostConfig.session.workflow.createConfig.mainModeRepetition = 2U;
  hostConfig.session.workflow.createConfig.channelMapRepetition = 2U;
  hostConfig.session.workflow.createConfig.rttType = 0U;
  hostConfig.session.workflow.defaultSettings.maxTxPowerDbm = -9;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 17U;
  hostConfig.session.workflow.procedureParameters.minProcedureInterval = 111U;
  hostConfig.session.workflow.procedureParameters.maxProcedureInterval = 222U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.toneAntennaConfigSelection = 3U;
  hostConfig.session.workflow.procedureParameters.txPowerDelta = -4;

  bool ok = vprHost.resetTransport(true) && vprHost.loadDefaultTransportImage() &&
            vprHost.bootTransport() && vprHost.beginHost(kDemoConnHandle, hostConfig);
  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready();
  for (uint8_t i = 0U; ok && !vprHost.estimateValid() && i < 8U; ++i) {
    ok = vprHost.poll();
  }

  const StepModeCollectContext localModes =
      collectStepModes(vprHost.completedLocalResult());
  const StepModeCollectContext peerModes =
      collectStepModes(vprHost.completedPeerResult());

  Serial.print(F("hcivprrttoffdemo ok="));
  Serial.print((ok && vprHost.ready() && vprHost.estimateValid() &&
                localModes.mode1Count == 0U && peerModes.mode1Count == 0U &&
                localModes.mode2Count >= 4U && peerModes.mode2Count >= 4U)
                   ? 1
                   : 0);
  Serial.print(F(" proc="));
  Serial.print(vprHost.sessionState().completedProcedureCounter);
  Serial.print(F(" lm1="));
  Serial.print(localModes.mode1Count);
  Serial.print(F(" pm1="));
  Serial.print(peerModes.mode1Count);
  Serial.print(F(" lm2="));
  Serial.print(localModes.mode2Count);
  Serial.print(F(" pm2="));
  Serial.print(peerModes.mode2Count);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprStateDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  hostConfig.session.workflow.procedureEnable.enable = 0U;
  hostConfig.session.workflow.procedureEnable.enable = 0U;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();

  uint8_t badCreateStatus = 0xFFU;
  bool badCreateRejected = false;
  uint8_t badStatus = 0xFFU;
  bool badRejected = false;
  uint8_t badRangeStatus = 0xFFU;
  bool badRangeRejected = false;
  uint8_t removeStatus = 0xFFU;
  bool removed = false;
  uint8_t postRemoveStatus = 0xFFU;
  bool postRemoveRejected = false;

  if (ok) {
    VprControllerServiceHost directHost(&vprHost.transport());
    BleCsControllerCreateConfig badCreateConfig = hostConfig.session.workflow.createConfig;
    memset(badCreateConfig.channelMap, 0, sizeof(badCreateConfig.channelMap));
    BleCsHciCommand badCreateCommand{};
    ok = BleChannelSoundingRadio::buildHciCreateConfigCommand(
        kDemoConnHandle, badCreateConfig, &badCreateCommand);
    if (ok) {
      uint8_t response[64];
      size_t responseLen = 0U;
      BleCsHciCommandStatusEvent statusEvent{};
      badCreateRejected =
          directHost.sendHciCommand(badCreateCommand.opcode, badCreateCommand.payload,
                                    badCreateCommand.payloadLen, response,
                                    sizeof(response), &responseLen) &&
          BleChannelSoundingRadio::parseHciCommandStatusEvent(
              response, responseLen, &statusEvent) &&
          statusEvent.opcode == badCreateCommand.opcode && statusEvent.status != 0U;
      if (badCreateRejected) {
        badCreateStatus = statusEvent.status;
      }
    }
  }

  if (ok) {
    VprControllerServiceHost directHost(&vprHost.transport());
    BleCsHciCommand badCommand{};
    ok = BleChannelSoundingRadio::buildHciSetProcedureParametersCommand(
        kDemoConnHandle, hostConfig.session.workflow.procedureParameters, &badCommand);
    if (ok) {
      uint8_t response[64];
      size_t responseLen = 0U;
      BleCsHciCommandCompleteEvent complete{};
      badRejected = directHost.sendHciCommand(badCommand.opcode, badCommand.payload,
                                              badCommand.payloadLen, response,
                                              sizeof(response), &responseLen) &&
                    BleChannelSoundingRadio::parseHciCommandCompleteEvent(
                        response, responseLen, &complete) &&
                    complete.opcode == badCommand.opcode && complete.status != 0U;
      if (badRejected) {
        badStatus = complete.status;
      }
    }
  }

  ok = ok && badCreateRejected && badRejected;
  ok = ok && vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }

  ok = ok && vprHost.ready();

  if (ok && vprHost.ready()) {
    VprControllerServiceHost directHost(&vprHost.transport());
    BleCsProcedureParameters badRangeParams = hostConfig.session.workflow.procedureParameters;
    badRangeParams.maxProcedureCount = 0U;
    BleCsHciCommand badRangeCommand{};
    ok = BleChannelSoundingRadio::buildHciSetProcedureParametersCommand(
        kDemoConnHandle, badRangeParams, &badRangeCommand);
    if (ok) {
      uint8_t response[64];
      size_t responseLen = 0U;
      BleCsHciCommandCompleteEvent complete{};
      badRangeRejected =
          directHost.sendHciCommand(badRangeCommand.opcode, badRangeCommand.payload,
                                    badRangeCommand.payloadLen, response,
                                    sizeof(response), &responseLen) &&
          BleChannelSoundingRadio::parseHciCommandCompleteEvent(
              response, responseLen, &complete) &&
          complete.opcode == badRangeCommand.opcode && complete.status != 0U;
      if (badRangeRejected) {
        badRangeStatus = complete.status;
      }
    }

    BleCsHciCommand removeCommand{};
    ok = BleChannelSoundingRadio::buildHciRemoveConfigCommand(
        kDemoConnHandle, vprHost.workflowState().configComplete.configId, &removeCommand);
    if (ok) {
      uint8_t response[64];
      size_t responseLen = 0U;
      BleCsHciCommandCompleteEvent complete{};
      removed = directHost.sendHciCommand(removeCommand.opcode, removeCommand.payload,
                                          removeCommand.payloadLen, response,
                                          sizeof(response), &responseLen) &&
                BleChannelSoundingRadio::parseHciCommandCompleteEvent(
                    response, responseLen, &complete) &&
                complete.opcode == removeCommand.opcode;
      if (removed) {
        removeStatus = complete.status;
        removed = (complete.status == 0U);
      }
    }
    if (removed) {
      BleCsHciCommand badCommand{};
      ok = BleChannelSoundingRadio::buildHciSetProcedureParametersCommand(
          kDemoConnHandle, hostConfig.session.workflow.procedureParameters, &badCommand);
      if (ok) {
        uint8_t response[64];
        size_t responseLen = 0U;
        BleCsHciCommandCompleteEvent complete{};
        postRemoveRejected =
            directHost.sendHciCommand(badCommand.opcode, badCommand.payload,
                                      badCommand.payloadLen, response, sizeof(response),
                                      &responseLen) &&
            BleChannelSoundingRadio::parseHciCommandCompleteEvent(
                response, responseLen, &complete) &&
            complete.opcode == badCommand.opcode && complete.status != 0U;
        if (postRemoveRejected) {
          postRemoveStatus = complete.status;
        }
      }
    }
  }

  ok = ok && badRangeRejected && removed && postRemoveRejected;

  Serial.print(F("hcivprstatedemo ok="));
  Serial.print((ok && badCreateRejected && badRejected && badRangeRejected && removed &&
                postRemoveRejected && vprHost.ready())
                   ? 1
                   : 0);
  Serial.print(F(" bad_create=0x"));
  Serial.print(badCreateStatus, HEX);
  Serial.print(F(" bad_setproc=0x"));
  Serial.print(badStatus, HEX);
  Serial.print(F(" bad_range=0x"));
  Serial.print(badRangeStatus, HEX);
  Serial.print(F(" remove=0x"));
  Serial.print(removeStatus, HEX);
  Serial.print(F(" post_remove=0x"));
  Serial.print(postRemoveStatus, HEX);
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(vprHost.workflowState().phase));
  Serial.print(F(" proc="));
  Serial.print(vprHost.sessionState().completedProcedureCounter);
  Serial.print(F(" proc_cnt="));
  Serial.print(vprHost.workflowState().procedureEnableComplete.procedureCount);
  Serial.print(F(" cfg="));
  Serial.print(vprHost.workflowState().configComplete.configId);
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" dist_m="));
  Serial.println(F("na"));
}

void printHciVprMultiDemo() {
  volatile Nrf54l15VprTransportHostShared* sharedHost = nrf54l15_vpr_transport_host_shared();
  volatile Nrf54l15VprTransportVprShared* sharedVpr = nrf54l15_vpr_transport_vpr_shared();
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static constexpr uint16_t kTargetProcedureCount = 3U;
  static const uint8_t demoChannels[] = {2U, 14U, 26U, 38U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  memset(hostConfig.session.workflow.createConfig.channelMap, 0,
         sizeof(hostConfig.session.workflow.createConfig.channelMap));
  for (size_t i = 0U; i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    hostConfig.session.workflow.createConfig.channelMap[channel >> 3U] |=
        static_cast<uint8_t>(1U << (channel & 0x07U));
  }
  hostConfig.session.workflow.procedureParameters.maxProcedureCount =
      kTargetProcedureCount;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 19U;
  hostConfig.session.workflow.procedureParameters.minProcedureInterval = 140U;
  hostConfig.session.workflow.procedureParameters.maxProcedureInterval = 260U;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }

  ok = ok && vprHost.ready();

  uint8_t pollCount = 0U;
  uint16_t lastProcedureCounter = 0U;
  uint8_t procedureTransitions = 0U;
  uint32_t lastTransitionHeartbeat = 0U;
  uint32_t minTransitionGap = 0U;
  uint32_t maxTransitionGap = 0U;
  uint8_t minIntervalSelector = 0xFFU;
  uint8_t maxIntervalSelector = 0U;
  uint8_t lastLocalResultPackets = 0U;
  uint8_t lastPeerMarkers = 0U;
  uint32_t lastLocalPacketHeartbeat = 0U;
  uint32_t minPeerGap = 0U;
  uint32_t maxPeerGap = 0U;
  while (ok && !vprHost.failed() &&
         vprHost.sessionState().completedProcedureCounter < kTargetProcedureCount &&
         pollCount < 48U) {
    ok = vprHost.poll();
    ++pollCount;
    const uint32_t heartbeat = vprHost.vprState().heartbeat;
    const uint8_t intervalSelector = vprHost.vprState().linkProcedureIntervalSelector;
    if (intervalSelector < minIntervalSelector) {
      minIntervalSelector = intervalSelector;
    }
    if (intervalSelector > maxIntervalSelector) {
      maxIntervalSelector = intervalSelector;
    }
    const uint8_t localResultPackets = vprHost.hostState().localResultPackets;
    if (localResultPackets != lastLocalResultPackets) {
      lastLocalResultPackets = localResultPackets;
      lastLocalPacketHeartbeat = heartbeat;
    }
    const uint8_t peerMarkers = vprHost.hostState().controllerPeerResultMarkers;
    if (peerMarkers != lastPeerMarkers) {
      if (lastLocalPacketHeartbeat != 0U) {
        const uint32_t gap = heartbeat - lastLocalPacketHeartbeat;
        if (minPeerGap == 0U || gap < minPeerGap) {
          minPeerGap = gap;
        }
        if (gap > maxPeerGap) {
          maxPeerGap = gap;
        }
      }
      lastPeerMarkers = peerMarkers;
    }
    const uint16_t procedureCounter =
        vprHost.sessionState().completedProcedureCounter;
    if (procedureCounter != 0U && procedureCounter != lastProcedureCounter) {
      lastProcedureCounter = procedureCounter;
      ++procedureTransitions;
      if (lastTransitionHeartbeat != 0U) {
        const uint32_t gap = heartbeat - lastTransitionHeartbeat;
        if (minTransitionGap == 0U || gap < minTransitionGap) {
          minTransitionGap = gap;
        }
        if (gap > maxTransitionGap) {
          maxTransitionGap = gap;
        }
      }
      lastTransitionHeartbeat = heartbeat;
    }
  }

  const bool countersReached =
      vprHost.sessionState().completedProcedureCounter == kTargetProcedureCount;
  const bool markersReached =
      vprHost.hostState().controllerPeerResultMarkers >= kTargetProcedureCount;
  const bool peerPacketsReached =
      vprHost.hostState().peerResultPackets >= kTargetProcedureCount;
  const bool stopped = !vprHost.vprState().linkProcedureEnabled;
  const uint8_t vprPeerGapTicks = vprHost.vprState().linkPeerGapTicks;
  const StepChannelCollectContext finalChannels =
      collectStepChannels(vprHost.completedLocalResult());
  const StepModeCollectContext finalModes =
      collectStepModes(vprHost.completedLocalResult());
  const StepPermutationCollectContext finalPermutations =
      collectStepPermutations(vprHost.completedLocalResult());
  const StepQualityCollectContext finalQuality =
      collectStepQuality(vprHost.completedLocalResult());
  const StepAmplitudeCollectContext localAmplitude =
      collectStepAmplitude(vprHost.completedLocalResult());
  const StepAmplitudeCollectContext peerAmplitude =
      collectStepAmplitude(vprHost.completedPeerResult());
  const StepPhaseCollectContext localPhase =
      collectStepPhase(vprHost.completedLocalResult());
  const StepPhaseCollectContext peerPhase =
      collectStepPhase(vprHost.completedPeerResult());
  const bool intervalPolicyMoved =
      (hostConfig.session.workflow.procedureParameters.maxProcedureInterval >
       hostConfig.session.workflow.procedureParameters.minProcedureInterval)
          ? (maxIntervalSelector > minIntervalSelector)
          : true;
  const bool finalOk =
      !vprHost.failed() && vprHost.ready() && countersReached && markersReached &&
      peerPacketsReached && vprHost.estimateValid() && stopped &&
      finalModes.mode1Count == 1U && finalModes.mode2Count >= 4U &&
      intervalPolicyMoved;

  Serial.print(F("hcivprmultidemo ok="));
  Serial.print(finalOk ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" polled="));
  Serial.print(pollCount);
  Serial.print(F(" proc="));
  Serial.print(vprHost.sessionState().completedProcedureCounter);
  Serial.print(F(" transitions="));
  Serial.print(procedureTransitions);
  Serial.print(F(" target="));
  Serial.print(kTargetProcedureCount);
  Serial.print(F(" ctrl_evt="));
  Serial.print(vprHost.hostState().controllerEventPackets);
  Serial.print(F(" peer_mark="));
  Serial.print(vprHost.hostState().controllerPeerResultMarkers);
  Serial.print(F(" peer_evt="));
  Serial.print(vprHost.hostState().peerResultPackets);
  Serial.print(F(" stopped="));
  Serial.print(stopped ? 1 : 0);
  Serial.print(F(" hb_gap="));
  Serial.print(minTransitionGap);
  Serial.print('/');
  Serial.print(maxTransitionGap);
  Serial.print(F(" int_sel="));
  Serial.print(minIntervalSelector);
  Serial.print('/');
  Serial.print(maxIntervalSelector);
  Serial.print(F(" peer_gap="));
  Serial.print(vprPeerGapTicks);
  Serial.print(F(" host_peer_gap="));
  Serial.print(minPeerGap);
  Serial.print('/');
  Serial.print(maxPeerGap);
  Serial.print(F(" hs="));
  Serial.print(sharedHost->hostSeq);
  Serial.print('/');
  Serial.print(sharedHost->hostFlags, HEX);
  Serial.print(F(" vs="));
  Serial.print(sharedVpr->vprSeq);
  Serial.print('/');
  Serial.print(sharedVpr->vprFlags, HEX);
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(vprHost.workflowState().phase));
  Serial.print(F(" vpr_flags="));
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" vpr_cfg="));
  Serial.print(vprHost.vprState().linkConfigId);
  Serial.print(F(" steps="));
  Serial.print(vprHost.completedLocalResult().header.numStepsReported);
  Serial.print(F(" m1="));
  Serial.print(finalModes.mode1Count);
  Serial.print(F(" m2="));
  Serial.print(finalModes.mode2Count);
  Serial.print(F(" perm="));
  for (uint8_t i = 0U; i < finalPermutations.count; ++i) {
    if (i != 0U) {
      Serial.print(',');
    }
    Serial.print(finalPermutations.permutations[i]);
  }
  Serial.print(F(" ql="));
  for (uint8_t i = 0U; i < finalQuality.count; ++i) {
    if (i != 0U) {
      Serial.print(',');
    }
    Serial.print(finalQuality.quality[i]);
  }
  Serial.print(F(" la="));
  for (uint8_t i = 0U; i < localAmplitude.count; ++i) {
    if (i != 0U) {
      Serial.print(',');
    }
    Serial.print(localAmplitude.amplitude[i]);
  }
  Serial.print(F(" pa="));
  for (uint8_t i = 0U; i < peerAmplitude.count; ++i) {
    if (i != 0U) {
      Serial.print(',');
    }
    Serial.print(peerAmplitude.amplitude[i]);
  }
  Serial.print(F(" lph="));
  for (uint8_t i = 0U; i < localPhase.count; ++i) {
    if (i != 0U) {
      Serial.print(',');
    }
    Serial.print(localPhase.phaseDeg[i]);
  }
  Serial.print(F(" pph="));
  for (uint8_t i = 0U; i < peerPhase.count; ++i) {
    if (i != 0U) {
      Serial.print(',');
    }
    Serial.print(peerPhase.phaseDeg[i]);
  }
  Serial.print(F(" ch="));
  for (uint8_t i = 0U; i < finalChannels.count; ++i) {
    if (i != 0U) {
      Serial.print(',');
    }
    Serial.print(finalChannels.channels[i]);
  }
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprChunkDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t demoChannels[] = {2U, 14U, 26U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  memset(hostConfig.session.workflow.createConfig.channelMap, 0,
         sizeof(hostConfig.session.workflow.createConfig.channelMap));
  for (size_t i = 0U; i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    hostConfig.session.workflow.createConfig.channelMap[channel >> 3U] |=
        static_cast<uint8_t>(1U << (channel & 0x07U));
  }
  hostConfig.session.workflow.createConfig.minMainModeSteps = 3U;
  hostConfig.session.workflow.createConfig.maxMainModeSteps = 3U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 12U;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }

  ok = ok && vprHost.ready();

  uint8_t pollCount = 0U;
  while (ok && !vprHost.failed() &&
         vprHost.sessionState().completedProcedureCounter < 1U &&
         pollCount < 24U) {
    ok = vprHost.poll();
    ++pollCount;
  }

  const BleCsSubeventResult& local = vprHost.localResult();
  const BleCsSubeventResult& peer = vprHost.peerResult();
  const bool noLocalContinue = local.isComplete && !local.isContinuation && !local.isPartial;
  const bool noPeerContinue = peer.isComplete && !peer.isContinuation && !peer.isPartial;
  ok = ok && vprHost.ready() &&
       local.header.procedureCounter == 1U && peer.header.procedureCounter == 1U &&
       local.header.numStepsReported == 3U && peer.header.numStepsReported == 3U &&
       noLocalContinue && noPeerContinue &&
       vprHost.hostState().peerResultPackets == 1U &&
       vprHost.hostState().controllerPeerResultMarkers == 1U;

  Serial.print(F("hcivprchunkdemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" polled="));
  Serial.print(pollCount);
  Serial.print(F(" proc="));
  Serial.print(local.header.procedureCounter);
  Serial.print(F(" ctrl_evt="));
  Serial.print(vprHost.hostState().controllerEventPackets);
  Serial.print(F(" peer_mark="));
  Serial.print(vprHost.hostState().controllerPeerResultMarkers);
  Serial.print(F(" peer_evt="));
  Serial.print(vprHost.hostState().peerResultPackets);
  Serial.print(F(" local_flags="));
  Serial.print(local.isComplete ? 'C' : '-');
  Serial.print(local.isPartial ? 'P' : '-');
  Serial.print(local.isContinuation ? 'T' : '-');
  Serial.print(F(" peer_flags="));
  Serial.print(peer.isComplete ? 'C' : '-');
  Serial.print(peer.isPartial ? 'P' : '-');
  Serial.print(peer.isContinuation ? 'T' : '-');
  Serial.print(F(" local_steps="));
  Serial.print(local.header.numStepsReported);
  Serial.print(F(" peer_steps="));
  Serial.print(peer.header.numStepsReported);
  Serial.print(F(" local_bytes="));
  Serial.print(local.stepDataLen);
  Serial.print(F(" peer_bytes="));
  Serial.print(peer.stepDataLen);
  Serial.print(F(" est="));
  Serial.print(vprHost.estimateValid() ? 1 : 0);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprContinueDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t demoChannels[] = {2U, 14U, 26U, 38U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  memset(hostConfig.session.workflow.createConfig.channelMap, 0,
         sizeof(hostConfig.session.workflow.createConfig.channelMap));
  for (size_t i = 0U; i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    hostConfig.session.workflow.createConfig.channelMap[channel >> 3U] |=
        static_cast<uint8_t>(1U << (channel & 0x07U));
  }
  hostConfig.session.workflow.createConfig.rttType = 1U;
  hostConfig.session.workflow.createConfig.minMainModeSteps = 5U;
  hostConfig.session.workflow.createConfig.maxMainModeSteps = 5U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 12U;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 64U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }

  ok = ok && vprHost.ready();

  uint8_t pollCount = 0U;
  while (ok && !vprHost.failed() &&
         vprHost.sessionState().completedProcedureCounter < 1U &&
         pollCount < 64U) {
    ok = vprHost.poll();
    ++pollCount;
  }

  const BleCsSubeventResult& local = vprHost.localResult();
  const BleCsSubeventResult& peer = vprHost.peerResult();
  StepModeCollectContext localModes{};
  StepModeCollectContext peerModes{};
  BleChannelSoundingRadio::parseSubeventStepData(local.stepData, local.stepDataLen,
                                                 collectStepModeCallback, &localModes);
  BleChannelSoundingRadio::parseSubeventStepData(peer.stepData, peer.stepDataLen,
                                                 collectStepModeCallback, &peerModes);

  const bool localContinued = local.isComplete && !local.isPartial && local.isContinuation;
  const bool peerContinued = peer.isComplete && !peer.isPartial && peer.isContinuation;
  ok = ok && vprHost.ready() &&
       local.header.procedureCounter == 1U && peer.header.procedureCounter == 1U &&
       local.header.numStepsReported == 6U && peer.header.numStepsReported == 6U &&
       localModes.mode1Count == 1U && localModes.mode2Count == 5U &&
       peerModes.mode1Count == 1U && peerModes.mode2Count == 5U &&
       localContinued && peerContinued &&
       vprHost.hostState().localResultPackets == 3U &&
       vprHost.hostState().peerResultPackets == 3U &&
       vprHost.hostState().controllerPeerResultMarkers == 1U &&
       vprHost.estimateValid();

  Serial.print(F("hcivprcontinuedemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" polled="));
  Serial.print(pollCount);
  Serial.print(F(" proc="));
  Serial.print(local.header.procedureCounter);
  Serial.print(F(" ctrl_evt="));
  Serial.print(vprHost.hostState().controllerEventPackets);
  Serial.print(F(" local_evt="));
  Serial.print(vprHost.hostState().localResultPackets);
  Serial.print(F(" peer_mark="));
  Serial.print(vprHost.hostState().controllerPeerResultMarkers);
  Serial.print(F(" peer_evt="));
  Serial.print(vprHost.hostState().peerResultPackets);
  Serial.print(F(" local_flags="));
  Serial.print(local.isComplete ? 'C' : '-');
  Serial.print(local.isPartial ? 'P' : '-');
  Serial.print(local.isContinuation ? 'T' : '-');
  Serial.print(F(" peer_flags="));
  Serial.print(peer.isComplete ? 'C' : '-');
  Serial.print(peer.isPartial ? 'P' : '-');
  Serial.print(peer.isContinuation ? 'T' : '-');
  Serial.print(F(" local_steps="));
  Serial.print(local.header.numStepsReported);
  Serial.print(F(" peer_steps="));
  Serial.print(peer.header.numStepsReported);
  Serial.print(F(" m1="));
  Serial.print(localModes.mode1Count);
  Serial.print('/');
  Serial.print(peerModes.mode1Count);
  Serial.print(F(" m2="));
  Serial.print(localModes.mode2Count);
  Serial.print('/');
  Serial.print(peerModes.mode2Count);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprSubeventDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t demoChannels[] = {2U, 14U, 26U, 38U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  memset(hostConfig.session.workflow.createConfig.channelMap, 0,
         sizeof(hostConfig.session.workflow.createConfig.channelMap));
  for (size_t i = 0U; i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    hostConfig.session.workflow.createConfig.channelMap[channel >> 3U] |=
        static_cast<uint8_t>(1U << (channel & 0x07U));
  }
  hostConfig.session.workflow.createConfig.rttType = 1U;
  hostConfig.session.workflow.createConfig.minMainModeSteps = 5U;
  hostConfig.session.workflow.createConfig.maxMainModeSteps = 5U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 12U;
  hostConfig.session.workflow.procedureParameters.minSubeventLen = 0x000100UL;
  hostConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000100UL;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 64U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }

  ok = ok && vprHost.ready();

  uint8_t pollCount = 0U;
  uint32_t lastLocalPacketHeartbeat = 0U;
  uint32_t lastPeerPacketHeartbeat = 0U;
  uint32_t minLocalGap = 0U;
  uint32_t maxLocalGap = 0U;
  uint32_t minPeerGap = 0U;
  uint32_t maxPeerGap = 0U;
  uint32_t lastLocalPackets = vprHost.hostState().localResultPackets;
  uint32_t lastPeerPackets = vprHost.hostState().peerResultPackets;
  while (ok && !vprHost.failed() &&
         vprHost.sessionState().completedProcedureCounter < 1U &&
         pollCount < 96U) {
    ok = vprHost.poll();
    ++pollCount;

    const uint32_t heartbeat = vprHost.vprState().heartbeat;
    const uint32_t localPackets = vprHost.hostState().localResultPackets;
    if (localPackets != lastLocalPackets) {
      if (lastLocalPacketHeartbeat != 0U) {
        const uint32_t gap = heartbeat - lastLocalPacketHeartbeat;
        if (minLocalGap == 0U || gap < minLocalGap) {
          minLocalGap = gap;
        }
        if (gap > maxLocalGap) {
          maxLocalGap = gap;
        }
      }
      lastLocalPacketHeartbeat = heartbeat;
      lastLocalPackets = localPackets;
    }

    const uint32_t peerPackets = vprHost.hostState().peerResultPackets;
    if (peerPackets != lastPeerPackets) {
      if (lastPeerPacketHeartbeat != 0U) {
        const uint32_t gap = heartbeat - lastPeerPacketHeartbeat;
        if (minPeerGap == 0U || gap < minPeerGap) {
          minPeerGap = gap;
        }
        if (gap > maxPeerGap) {
          maxPeerGap = gap;
        }
      }
      lastPeerPacketHeartbeat = heartbeat;
      lastPeerPackets = peerPackets;
    }
  }

  const BleCsSubeventResult& local = vprHost.localResult();
  const BleCsSubeventResult& peer = vprHost.peerResult();
  StepModeCollectContext localModes{};
  StepModeCollectContext peerModes{};
  BleChannelSoundingRadio::parseSubeventStepData(local.stepData, local.stepDataLen,
                                                 collectStepModeCallback, &localModes);
  BleChannelSoundingRadio::parseSubeventStepData(peer.stepData, peer.stepDataLen,
                                                 collectStepModeCallback, &peerModes);

  const bool localContinued = local.isComplete && !local.isPartial && local.isContinuation;
  const bool peerContinued = peer.isComplete && !peer.isPartial && peer.isContinuation;
  ok = ok && vprHost.ready() &&
       local.header.procedureCounter == 1U && peer.header.procedureCounter == 1U &&
       local.header.numStepsReported == 6U && peer.header.numStepsReported == 6U &&
       localModes.mode1Count == 1U && localModes.mode2Count == 5U &&
       peerModes.mode1Count == 1U && peerModes.mode2Count == 5U &&
       localContinued && peerContinued &&
       vprHost.hostState().localResultPackets == 6U &&
       vprHost.hostState().peerResultPackets == 6U &&
       vprHost.hostState().controllerPeerResultMarkers == 1U &&
       minLocalGap > 0U && minPeerGap > 0U &&
       vprHost.estimateValid();

  Serial.print(F("hcivprsubeventdemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" polled="));
  Serial.print(pollCount);
  Serial.print(F(" proc="));
  Serial.print(local.header.procedureCounter);
  Serial.print(F(" local_evt="));
  Serial.print(vprHost.hostState().localResultPackets);
  Serial.print(F(" peer_mark="));
  Serial.print(vprHost.hostState().controllerPeerResultMarkers);
  Serial.print(F(" peer_evt="));
  Serial.print(vprHost.hostState().peerResultPackets);
  Serial.print(F(" local_gap="));
  Serial.print(minLocalGap);
  Serial.print('/');
  Serial.print(maxLocalGap);
  Serial.print(F(" peer_gap="));
  Serial.print(minPeerGap);
  Serial.print('/');
  Serial.print(maxPeerGap);
  Serial.print(F(" local_flags="));
  Serial.print(local.isComplete ? 'C' : '-');
  Serial.print(local.isPartial ? 'P' : '-');
  Serial.print(local.isContinuation ? 'T' : '-');
  Serial.print(F(" peer_flags="));
  Serial.print(peer.isComplete ? 'C' : '-');
  Serial.print(peer.isPartial ? 'P' : '-');
  Serial.print(peer.isContinuation ? 'T' : '-');
  Serial.print(F(" steps="));
  Serial.print(local.header.numStepsReported);
  Serial.print('/');
  Serial.print(peer.header.numStepsReported);
  Serial.print(F(" m1="));
  Serial.print(localModes.mode1Count);
  Serial.print('/');
  Serial.print(peerModes.mode1Count);
  Serial.print(F(" m2="));
  Serial.print(localModes.mode2Count);
  Serial.print('/');
  Serial.print(peerModes.mode2Count);
  Serial.print(F(" subevt=0x"));
  Serial.print(hostConfig.session.workflow.procedureParameters.minSubeventLen, HEX);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprMultiSubeventDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t demoChannels[] = {2U, 14U, 26U, 38U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  memset(hostConfig.session.workflow.createConfig.channelMap, 0,
         sizeof(hostConfig.session.workflow.createConfig.channelMap));
  for (size_t i = 0U; i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    hostConfig.session.workflow.createConfig.channelMap[channel >> 3U] |=
        static_cast<uint8_t>(1U << (channel & 0x07U));
  }
  hostConfig.session.workflow.createConfig.rttType = 1U;
  hostConfig.session.workflow.createConfig.minMainModeSteps = 6U;
  hostConfig.session.workflow.createConfig.maxMainModeSteps = 6U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 16U;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 64U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }

  ok = ok && vprHost.ready();

  uint8_t pollCount = 0U;
  while (ok && !vprHost.failed() &&
         vprHost.sessionState().completedProcedureCounter < 1U &&
         pollCount < 96U) {
    ok = vprHost.poll();
    ++pollCount;
  }

  const BleCsSubeventResult& local = vprHost.localResult();
  const BleCsSubeventResult& peer = vprHost.peerResult();
  const BleCsSubeventResult& completedLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult& completedPeer = vprHost.completedPeerResult();
  StepModeCollectContext localModes{};
  StepModeCollectContext peerModes{};
  BleChannelSoundingRadio::parseSubeventStepData(completedLocal.stepData,
                                                 completedLocal.stepDataLen,
                                                 collectStepModeCallback, &localModes);
  BleChannelSoundingRadio::parseSubeventStepData(completedPeer.stepData,
                                                 completedPeer.stepDataLen,
                                                 collectStepModeCallback, &peerModes);

  ok = ok && vprHost.ready() &&
       local.header.procedureCounter == 1U && peer.header.procedureCounter == 1U &&
       local.header.numStepsReported == 3U && peer.header.numStepsReported == 3U &&
       completedLocal.header.procedureCounter == 1U &&
       completedPeer.header.procedureCounter == 1U &&
       completedLocal.header.numStepsReported == 7U &&
       completedPeer.header.numStepsReported == 7U &&
       local.header.procedureDoneStatus == kBleCsProcedureDoneComplete &&
       peer.header.procedureDoneStatus == kBleCsProcedureDoneComplete &&
       vprHost.hostState().localSubeventResults == 2U &&
       vprHost.hostState().peerSubeventResults == 2U &&
       vprHost.hostState().controllerPeerResultMarkers == 2U &&
       vprHost.estimateValid() &&
       localModes.mode1Count == 1U && localModes.mode2Count == 6U &&
       peerModes.mode1Count == 1U && peerModes.mode2Count == 6U;

  Serial.print(F("hcivprmultisubdemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" polled="));
  Serial.print(pollCount);
  Serial.print(F(" proc="));
  Serial.print(completedLocal.header.procedureCounter);
  Serial.print(F(" local_sub="));
  Serial.print(vprHost.hostState().localSubeventResults);
  Serial.print(F(" peer_sub="));
  Serial.print(vprHost.hostState().peerSubeventResults);
  Serial.print(F(" peer_mark="));
  Serial.print(vprHost.hostState().controllerPeerResultMarkers);
  Serial.print(F(" last_steps="));
  Serial.print(local.header.numStepsReported);
  Serial.print('/');
  Serial.print(peer.header.numStepsReported);
  Serial.print(F(" steps="));
  Serial.print(completedLocal.header.numStepsReported);
  Serial.print('/');
  Serial.print(completedPeer.header.numStepsReported);
  Serial.print(F(" m1="));
  Serial.print(localModes.mode1Count);
  Serial.print('/');
  Serial.print(peerModes.mode1Count);
  Serial.print(F(" m2="));
  Serial.print(localModes.mode2Count);
  Serial.print('/');
  Serial.print(peerModes.mode2Count);
  Serial.print(F(" local_evt="));
  Serial.print(vprHost.hostState().localResultPackets);
  Serial.print(F(" peer_evt="));
  Serial.print(vprHost.hostState().peerResultPackets);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprSubcountDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t demoChannels[] = {2U, 14U, 26U, 38U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  memset(hostConfig.session.workflow.createConfig.channelMap, 0,
         sizeof(hostConfig.session.workflow.createConfig.channelMap));
  for (size_t i = 0U; i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    hostConfig.session.workflow.createConfig.channelMap[channel >> 3U] |=
        static_cast<uint8_t>(1U << (channel & 0x07U));
  }
  hostConfig.session.workflow.createConfig.rttType = 1U;
  hostConfig.session.workflow.createConfig.minMainModeSteps = 6U;
  hostConfig.session.workflow.createConfig.maxMainModeSteps = 6U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 16U;
  hostConfig.session.workflow.procedureParameters.minSubeventLen = 0x000100UL;
  hostConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000100UL;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 64U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }

  ok = ok && vprHost.ready();

  uint8_t pollCount = 0U;
  while (ok && !vprHost.failed() &&
         vprHost.sessionState().completedProcedureCounter < 1U &&
         pollCount < 128U) {
    ok = vprHost.poll();
    ++pollCount;
  }

  const BleCsSubeventResult& local = vprHost.localResult();
  const BleCsSubeventResult& peer = vprHost.peerResult();
  const BleCsSubeventResult& completedLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult& completedPeer = vprHost.completedPeerResult();
  StepModeCollectContext localModes{};
  StepModeCollectContext peerModes{};
  BleChannelSoundingRadio::parseSubeventStepData(completedLocal.stepData,
                                                 completedLocal.stepDataLen,
                                                 collectStepModeCallback, &localModes);
  BleChannelSoundingRadio::parseSubeventStepData(completedPeer.stepData,
                                                 completedPeer.stepDataLen,
                                                 collectStepModeCallback, &peerModes);

  ok = ok && vprHost.ready() &&
       local.header.numStepsReported == 2U && peer.header.numStepsReported == 2U &&
       completedLocal.header.procedureCounter == 1U &&
       completedPeer.header.procedureCounter == 1U &&
       completedLocal.header.numStepsReported == 7U &&
       completedPeer.header.numStepsReported == 7U &&
       vprHost.hostState().localSubeventResults == 3U &&
       vprHost.hostState().peerSubeventResults == 3U &&
       vprHost.hostState().controllerPeerResultMarkers == 3U &&
       vprHost.hostState().localResultPackets == 3U &&
       vprHost.hostState().peerResultPackets == 3U &&
       vprHost.estimateValid() &&
       localModes.mode1Count == 1U && localModes.mode2Count == 6U &&
       peerModes.mode1Count == 1U && peerModes.mode2Count == 6U;

  Serial.print(F("hcivprsubcountdemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" polled="));
  Serial.print(pollCount);
  Serial.print(F(" proc="));
  Serial.print(completedLocal.header.procedureCounter);
  Serial.print(F(" local_sub="));
  Serial.print(vprHost.hostState().localSubeventResults);
  Serial.print(F(" peer_sub="));
  Serial.print(vprHost.hostState().peerSubeventResults);
  Serial.print(F(" peer_mark="));
  Serial.print(vprHost.hostState().controllerPeerResultMarkers);
  Serial.print(F(" last_steps="));
  Serial.print(local.header.numStepsReported);
  Serial.print('/');
  Serial.print(peer.header.numStepsReported);
  Serial.print(F(" steps="));
  Serial.print(completedLocal.header.numStepsReported);
  Serial.print('/');
  Serial.print(completedPeer.header.numStepsReported);
  Serial.print(F(" local_evt="));
  Serial.print(vprHost.hostState().localResultPackets);
  Serial.print(F(" peer_evt="));
  Serial.print(vprHost.hostState().peerResultPackets);
  Serial.print(F(" m1="));
  Serial.print(localModes.mode1Count);
  Serial.print('/');
  Serial.print(peerModes.mode1Count);
  Serial.print(F(" m2="));
  Serial.print(localModes.mode2Count);
  Serial.print('/');
  Serial.print(peerModes.mode2Count);
  Serial.print(F(" subevt=0x"));
  Serial.print(hostConfig.session.workflow.procedureParameters.minSubeventLen, HEX);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprAbortDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static constexpr uint16_t kTargetProcedureCount = 5U;

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  hostConfig.session.workflow.procedureParameters.maxProcedureCount =
      kTargetProcedureCount;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 19U;
  hostConfig.session.workflow.procedureParameters.minProcedureInterval = 220U;
  hostConfig.session.workflow.procedureParameters.maxProcedureInterval = 320U;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }

  ok = ok && vprHost.ready();

  uint8_t preDisablePolls = 0U;
  while (ok && !vprHost.failed() &&
         vprHost.sessionState().completedProcedureCounter < 1U &&
         preDisablePolls < 32U) {
    ok = vprHost.poll();
    ++preDisablePolls;
  }

  const uint16_t preDisableProcedure =
      vprHost.sessionState().completedProcedureCounter;
  const uint8_t preDisablePeerMarkers =
      vprHost.hostState().controllerPeerResultMarkers;

  auto drainTransport = [&]() {
    while (vprHost.transport().available() > 0) {
      (void)vprHost.transport().read();
    }
  };

  BleCsProcedureEnable disableParams{};
  disableParams.configId = vprHost.workflowState().configComplete.configId;
  disableParams.enable = 0U;
  BleCsHciCommand disableCommand{};
  const bool disableBuilt =
      BleChannelSoundingRadio::buildHciProcedureEnableCommand(
          kDemoConnHandle, disableParams, &disableCommand);
  uint8_t disableResponse[64] = {0};
  size_t disableResponseLen = 0U;
  drainTransport();
  VprControllerServiceHost directHost(&vprHost.transport());
  const bool disableWritten =
      disableBuilt &&
      directHost.sendHciCommand(disableCommand.opcode, disableCommand.payload,
                                disableCommand.payloadLen, disableResponse,
                                sizeof(disableResponse), &disableResponseLen);
  ok = ok && disableWritten;

  uint8_t postDisablePolls = 0U;
  while (ok && !vprHost.failed() && vprHost.vprState().linkProcedureEnabled &&
         postDisablePolls < 24U) {
    ok = vprHost.poll();
    ++postDisablePolls;
  }

  const uint16_t stoppedProcedure = vprHost.sessionState().completedProcedureCounter;
  const uint8_t stoppedPeerMarkers =
      vprHost.hostState().controllerPeerResultMarkers;

  uint8_t settlePolls = 0U;
  while (ok && !vprHost.failed() && settlePolls < 24U) {
    ok = vprHost.poll();
    ++settlePolls;
  }

  const uint16_t finalProcedure = vprHost.sessionState().completedProcedureCounter;
  const uint8_t finalPeerMarkers =
      vprHost.hostState().controllerPeerResultMarkers;
  const bool stopped = !vprHost.vprState().linkProcedureEnabled;
  const bool haltedProcedures = finalProcedure == stoppedProcedure;
  const bool haltedPeerMarkers = finalPeerMarkers == stoppedPeerMarkers;
  const bool abortedEarly = finalProcedure < kTargetProcedureCount;
  const bool passed = disableBuilt && disableWritten && stopped && abortedEarly &&
                      haltedProcedures && haltedPeerMarkers &&
                      preDisableProcedure >= 1U;

  Serial.print(F("hcivprabortdemo ok="));
  Serial.print(passed ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" pre_polls="));
  Serial.print(preDisablePolls);
  Serial.print(F(" post_polls="));
  Serial.print(postDisablePolls);
  Serial.print(F(" settle="));
  Serial.print(settlePolls);
  Serial.print(F(" built="));
  Serial.print(disableBuilt ? 1 : 0);
  Serial.print(F(" wrote="));
  Serial.print(disableWritten ? 1 : 0);
  Serial.print(F(" pre_proc="));
  Serial.print(preDisableProcedure);
  Serial.print(F(" stop_proc="));
  Serial.print(stoppedProcedure);
  Serial.print(F(" final_proc="));
  Serial.print(finalProcedure);
  Serial.print(F(" pre_mark="));
  Serial.print(preDisablePeerMarkers);
  Serial.print(F(" stop_mark="));
  Serial.print(stoppedPeerMarkers);
  Serial.print(F(" final_mark="));
  Serial.print(finalPeerMarkers);
  Serial.print(F(" flags="));
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(vprHost.workflowState().phase));
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprManualDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static constexpr uint16_t kTargetProcedureCount = 4U;

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  hostConfig.session.workflow.procedureEnable.enable = 0U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount =
      kTargetProcedureCount;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 19U;
  hostConfig.session.workflow.procedureParameters.minProcedureInterval = 220U;
  hostConfig.session.workflow.procedureParameters.maxProcedureInterval = 320U;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready() && !vprHost.vprState().linkProcedureEnabled;

  uint8_t startStatus = 0xFFU;
  uint8_t stopStatus = 0xFFU;
  uint8_t restartStatus = 0xFFU;
  uint8_t finalStopStatus = 0xFFU;
  bool started = false;
  bool stopped = false;
  bool restarted = false;
  bool finalStopped = false;
  uint8_t startPolls = 0U;
  uint8_t stopPolls = 0U;
  uint8_t restartPolls = 0U;
  uint8_t finalStopPolls = 0U;
  uint16_t firstProcedure = 0U;
  uint16_t restartProcedure = 0U;
  uint16_t postStopProcedure = 0U;
  uint16_t finalProcedure = 0U;

  ok = ok && vprHost.directCurrentProcedureEnable(true, &startStatus);
  ok = ok && vprHost.pollUntilRunningWithProcedureCount(1U, 48U, &startPolls);
  firstProcedure = vprHost.sessionState().completedProcedureCounter;
  started = ok && vprHost.vprState().linkProcedureEnabled && firstProcedure >= 1U;

  ok = ok && started && vprHost.directCurrentProcedureEnable(false, &stopStatus);
  ok = ok && vprHost.pollUntilStopped(24U, &stopPolls);
  postStopProcedure = vprHost.sessionState().completedProcedureCounter;
  stopped = ok && !vprHost.vprState().linkProcedureEnabled;

  ok = ok && stopped && vprHost.directCurrentProcedureEnable(true, &restartStatus);
  ok = ok && vprHost.pollUntilRunningWithProcedureCount(1U, 48U, &restartPolls);
  restartProcedure = vprHost.sessionState().completedProcedureCounter;
  restarted = ok && vprHost.vprState().linkProcedureEnabled && restartProcedure >= 1U;

  ok = ok && restarted && vprHost.directCurrentProcedureEnable(false, &finalStopStatus);
  ok = ok && vprHost.pollUntilStopped(24U, &finalStopPolls);
  finalProcedure = vprHost.sessionState().completedProcedureCounter;
  finalStopped = ok && !vprHost.vprState().linkProcedureEnabled;

  const bool passed = ok && started && stopped && restarted && finalStopped;

  Serial.print(F("hcivprmanualdemo ok="));
  Serial.print(passed ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" start=0x"));
  Serial.print(startStatus, HEX);
  Serial.print(F(" stop=0x"));
  Serial.print(stopStatus, HEX);
  Serial.print(F(" restart=0x"));
  Serial.print(restartStatus, HEX);
  Serial.print(F(" final_stop=0x"));
  Serial.print(finalStopStatus, HEX);
  Serial.print(F(" polls="));
  Serial.print(startPolls);
  Serial.print('/');
  Serial.print(stopPolls);
  Serial.print('/');
  Serial.print(restartPolls);
  Serial.print('/');
  Serial.print(finalStopPolls);
  Serial.print(F(" proc="));
  Serial.print(firstProcedure);
  Serial.print('/');
  Serial.print(postStopProcedure);
  Serial.print('/');
  Serial.print(restartProcedure);
  Serial.print('/');
  Serial.print(finalProcedure);
  Serial.print(F(" flags="));
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(vprHost.workflowState().phase));
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprReconfigDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t demoChannels[] = {2U, 14U, 26U, 38U};
  static constexpr size_t kDemoChannelCount =
      sizeof(demoChannels) / sizeof(demoChannels[0]);

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  memset(hostConfig.session.workflow.createConfig.channelMap, 0,
         sizeof(hostConfig.session.workflow.createConfig.channelMap));
  for (size_t i = 0U; i < kDemoChannelCount; ++i) {
    const uint8_t channel = demoChannels[i];
    hostConfig.session.workflow.createConfig.channelMap[channel >> 3U] |=
        static_cast<uint8_t>(1U << (channel & 0x07U));
  }
  hostConfig.session.workflow.createConfig.rttType = 1U;
  hostConfig.session.workflow.createConfig.minMainModeSteps = 6U;
  hostConfig.session.workflow.createConfig.maxMainModeSteps = 6U;
  hostConfig.session.workflow.procedureEnable.enable = 0U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 16U;
  hostConfig.session.workflow.procedureParameters.minSubeventLen = 0x000100UL;
  hostConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000100UL;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready() && !vprHost.vprState().linkProcedureEnabled;

  const BleCsProcedureParameters tightParams = hostConfig.session.workflow.procedureParameters;
  BleCsProcedureParameters wideParams = tightParams;
  wideParams.minSubeventLen = 0x000456UL;
  wideParams.maxSubeventLen = 0x000678UL;

  uint8_t wideSetStatus = 0xFFU;
  uint8_t wideStartStatus = 0xFFU;
  uint8_t tightSetStatus = 0xFFU;
  uint8_t tightStartStatus = 0xFFU;
  uint8_t widePolls = 0U;
  uint8_t tightPolls = 0U;
  const uint32_t baseLocalSub = vprHost.hostState().localSubeventResults;
  const uint32_t basePeerSub = vprHost.hostState().peerSubeventResults;
  const uint32_t baseLocalEvt = vprHost.hostState().localResultPackets;
  const uint32_t basePeerEvt = vprHost.hostState().peerResultPackets;
  const uint16_t baseCompleted = vprHost.sessionState().completedProcedureCounter;

  ok = ok && vprHost.directSetProcedureParameters(wideParams, &wideSetStatus);
  ok = ok && vprHost.directCurrentProcedureEnable(true, &wideStartStatus);
  ok = ok &&
       vprHost.pollUntilRunComplete(baseLocalSub + 2U, basePeerSub + 2U,
                                    160U, &widePolls);

  const uint32_t wideLocalSub =
      vprHost.hostState().localSubeventResults - baseLocalSub;
  const uint32_t widePeerSub =
      vprHost.hostState().peerSubeventResults - basePeerSub;
  const uint32_t wideLocalEvt =
      vprHost.hostState().localResultPackets - baseLocalEvt;
  const uint32_t widePeerEvt =
      vprHost.hostState().peerResultPackets - basePeerEvt;
  const uint16_t wideCompleted = vprHost.sessionState().completedProcedureCounter;

  ok = ok && vprHost.directSetProcedureParameters(tightParams, &tightSetStatus);
  ok = ok && vprHost.directCurrentProcedureEnable(true, &tightStartStatus);
  ok = ok && vprHost.pollUntilRunComplete(baseLocalSub + 5U, basePeerSub + 5U,
                                          160U, &tightPolls);

  const uint32_t totalLocalSub =
      vprHost.hostState().localSubeventResults - baseLocalSub;
  const uint32_t totalPeerSub =
      vprHost.hostState().peerSubeventResults - basePeerSub;
  const uint32_t totalLocalEvt =
      vprHost.hostState().localResultPackets - baseLocalEvt;
  const uint32_t totalPeerEvt =
      vprHost.hostState().peerResultPackets - basePeerEvt;
  const uint32_t tightLocalSub = totalLocalSub - wideLocalSub;
  const uint32_t tightPeerSub = totalPeerSub - widePeerSub;
  const uint32_t tightLocalEvt = totalLocalEvt - wideLocalEvt;
  const uint32_t tightPeerEvt = totalPeerEvt - widePeerEvt;
  const uint16_t finalCompleted = vprHost.sessionState().completedProcedureCounter;

  ok = ok && wideSetStatus == 0U && wideStartStatus == 0U &&
       tightSetStatus == 0U && tightStartStatus == 0U &&
       wideCompleted == 1U && finalCompleted == 1U &&
       !vprHost.vprState().linkProcedureEnabled &&
       wideLocalSub == 2U && widePeerSub == 2U &&
       wideLocalEvt == 3U && widePeerEvt == 3U &&
       tightLocalSub == 3U && tightPeerSub == 3U &&
       tightLocalEvt == 3U && tightPeerEvt == 3U &&
       vprHost.estimateValid();

  Serial.print(F("hcivprreconfigdemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" wide=0x"));
  Serial.print(wideSetStatus, HEX);
  Serial.print('/');
  Serial.print(wideStartStatus, HEX);
  Serial.print(F(" tight=0x"));
  Serial.print(tightSetStatus, HEX);
  Serial.print('/');
  Serial.print(tightStartStatus, HEX);
  Serial.print(F(" polls="));
  Serial.print(widePolls);
  Serial.print('/');
  Serial.print(tightPolls);
  Serial.print(F(" proc="));
  Serial.print(baseCompleted);
  Serial.print('/');
  Serial.print(wideCompleted);
  Serial.print('/');
  Serial.print(finalCompleted);
  Serial.print(F(" wide_sub="));
  Serial.print(wideLocalSub);
  Serial.print('/');
  Serial.print(widePeerSub);
  Serial.print(F(" wide_evt="));
  Serial.print(wideLocalEvt);
  Serial.print('/');
  Serial.print(widePeerEvt);
  Serial.print(F(" tight_sub="));
  Serial.print(tightLocalSub);
  Serial.print('/');
  Serial.print(tightPeerSub);
  Serial.print(F(" tight_evt="));
  Serial.print(tightLocalEvt);
  Serial.print('/');
  Serial.print(tightPeerEvt);
  Serial.print(F(" flags="));
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(vprHost.workflowState().phase));
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprConfigSwapDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  hostConfig.session.workflow.procedureEnable.enable = 0U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 16U;
  hostConfig.session.workflow.procedureParameters.minSubeventLen = 0x000100UL;
  hostConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000100UL;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready();

  auto sendDirectReadCaps = [&](uint8_t* outStatus) -> bool {
    return vprHost.directReadRemoteSupportedCapabilities(outStatus);
  };

  auto sendDirectSetDefaults = [&](uint8_t* outStatus) -> bool {
    return vprHost.directSetDefaultSettings(hostConfig.session.workflow.defaultSettings, outStatus);
  };

  auto sendDirectRemove = [&](uint8_t configId, uint8_t* outStatus) -> bool {
    return vprHost.directRemoveConfig(configId, outStatus);
  };

  auto sendDirectCreate = [&](const BleCsControllerCreateConfig& config,
                              uint8_t* outStatus) -> bool {
    return vprHost.directCreateConfig(config, outStatus);
  };

  auto sendDirectSecurity = [&](uint8_t* outStatus) -> bool {
    return vprHost.directSecurityEnable(outStatus);
  };

  auto sendDirectSetProc = [&](const BleCsProcedureParameters& params,
                               uint8_t* outStatus) -> bool {
    return vprHost.directSetProcedureParameters(params, outStatus);
  };

  auto sendDirectEnable = [&](uint8_t configId, uint8_t enable,
                              uint8_t* outStatus) -> bool {
    return vprHost.directProcedureEnable(configId, enable != 0U, outStatus);
  };

  const uint8_t baseConfigId = vprHost.workflowState().configComplete.configId;
  BleCsControllerCreateConfig newConfig = hostConfig.session.workflow.createConfig;
  newConfig.configId = static_cast<uint8_t>(baseConfigId + 1U);
  newConfig.rttType = 0U;
  newConfig.minMainModeSteps = 4U;
  newConfig.maxMainModeSteps = 4U;

  BleCsProcedureParameters newParams = hostConfig.session.workflow.procedureParameters;
  newParams.configId = newConfig.configId;
  newParams.maxProcedureCount = 1U;
  newParams.maxProcedureLen = 16U;
  newParams.minSubeventLen = 0x000100UL;
  newParams.maxSubeventLen = 0x000100UL;

  uint8_t removeStatus = 0xFFU;
  uint8_t readCapsStatus = 0xFFU;
  uint8_t defaultsStatus = 0xFFU;
  uint8_t createStatus = 0xFFU;
  uint8_t securityStatus = 0xFFU;
  uint8_t setProcStatus = 0xFFU;
  uint8_t enableStatus = 0xFFU;
  uint8_t removePolls = 0U;
  uint8_t reopenPolls = 0U;
  uint8_t createPolls = 0U;
  uint8_t runPolls = 0U;
  bool removedShadow = false;
  bool reopenedShadow = false;
  bool createdShadow = false;

  ok = ok && sendDirectRemove(baseConfigId, &removeStatus);
  while (ok && !vprHost.failed() && removePolls < 24U) {
    removedShadow =
        removeStatus == 0U && !vprHost.vprState().linkSessionOpen &&
        !vprHost.vprState().linkConfigCreated &&
        !vprHost.vprState().linkSecurityEnabled &&
        !vprHost.vprState().linkProcedureParamsApplied &&
        !vprHost.vprState().linkProcedureEnabled &&
        !vprHost.workflowState().remoteCapabilitiesValid &&
        !vprHost.workflowState().defaultSettingsApplied &&
        !vprHost.workflowState().configCreated &&
        !vprHost.workflowState().securityEnabled &&
        !vprHost.workflowState().procedureParametersApplied &&
        !vprHost.workflowState().procedureEnabled &&
        vprHost.workflowState().configComplete.configId == baseConfigId;
    if (removedShadow) {
      break;
    }
    ok = vprHost.poll();
    ++removePolls;
  }
  ok = ok && removedShadow;

  ok = ok && sendDirectReadCaps(&readCapsStatus);
  ok = ok && sendDirectSetDefaults(&defaultsStatus);
  while (ok && !vprHost.failed() && reopenPolls < 24U) {
    reopenedShadow =
        readCapsStatus == 0U && defaultsStatus == 0U &&
        vprHost.vprState().linkSessionOpen &&
        vprHost.workflowState().remoteCapabilitiesValid &&
        vprHost.workflowState().defaultSettingsApplied &&
        !vprHost.workflowState().configCreated &&
        !vprHost.workflowState().securityEnabled &&
        !vprHost.workflowState().procedureParametersApplied &&
        !vprHost.workflowState().procedureEnabled;
    if (reopenedShadow) {
      break;
    }
    ok = vprHost.poll();
    ++reopenPolls;
  }
  ok = ok && reopenedShadow;

  ok = ok && sendDirectCreate(newConfig, &createStatus);
  ok = ok && sendDirectSecurity(&securityStatus);
  while (ok && !vprHost.failed() && createPolls < 24U) {
    createdShadow =
        createStatus == 0U && securityStatus == 0U &&
        vprHost.vprState().linkSessionOpen &&
        vprHost.vprState().linkConfigCreated &&
        vprHost.vprState().linkConfigId == newConfig.configId &&
        vprHost.vprState().linkSecurityEnabled &&
        vprHost.workflowState().configCreated &&
        vprHost.workflowState().securityEnabled &&
        !vprHost.workflowState().procedureParametersApplied &&
        !vprHost.workflowState().procedureEnabled &&
        vprHost.workflowState().configComplete.configId == newConfig.configId &&
        vprHost.workflowState().configComplete.rttType == 0U &&
        vprHost.workflowState().configComplete.minMainModeSteps == 4U &&
        vprHost.workflowState().configComplete.maxMainModeSteps == 4U;
    if (createdShadow) {
      break;
    }
    ok = vprHost.poll();
    ++createPolls;
  }
  ok = ok && createdShadow;

  ok = ok && sendDirectSetProc(newParams, &setProcStatus);
  const bool paramsShadow =
      setProcStatus == 0U &&
      vprHost.vprState().linkProcedureParamsApplied &&
      !vprHost.vprState().linkProcedureEnabled &&
      vprHost.workflowState().procedureParametersApplied &&
      !vprHost.workflowState().procedureEnabled &&
      vprHost.workflowState().lastStatus == 0U;
  ok = ok && paramsShadow;

  ok = ok && sendDirectEnable(newConfig.configId, 1U, &enableStatus);
  while (ok && !vprHost.failed() &&
         (vprHost.sessionState().completedProcedureCounter < 1U ||
          vprHost.vprState().linkProcedureEnabled) &&
         runPolls < 96U) {
    ok = vprHost.poll();
    ++runPolls;
  }

  const BleCsSubeventResult& completedLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult& completedPeer = vprHost.completedPeerResult();
  const StepModeCollectContext localModes = collectStepModes(completedLocal);
  const StepModeCollectContext peerModes = collectStepModes(completedPeer);
  const bool ranNewConfig =
      enableStatus == 0U && vprHost.estimateValid() &&
      vprHost.sessionState().completedProcedureCounter >= 1U &&
      !vprHost.vprState().linkProcedureEnabled &&
      vprHost.workflowState().procedureEnableComplete.configId == newConfig.configId &&
      vprHost.workflowState().procedureEnableComplete.state == 1U &&
      completedLocal.header.configId == newConfig.configId &&
      completedPeer.header.configId == newConfig.configId &&
      localModes.mode1Count == 0U && peerModes.mode1Count == 0U &&
      localModes.mode2Count == 4U && peerModes.mode2Count == 4U;
  ok = ok && ranNewConfig;

  Serial.print(F("hcivprcfgswapdemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" rm=0x"));
  Serial.print(removeStatus, HEX);
  Serial.print(F(" caps=0x"));
  Serial.print(readCapsStatus, HEX);
  Serial.print(F(" def=0x"));
  Serial.print(defaultsStatus, HEX);
  Serial.print(F(" create=0x"));
  Serial.print(createStatus, HEX);
  Serial.print(F(" sec=0x"));
  Serial.print(securityStatus, HEX);
  Serial.print(F(" set=0x"));
  Serial.print(setProcStatus, HEX);
  Serial.print(F(" run=0x"));
  Serial.print(enableStatus, HEX);
  Serial.print(F(" polls="));
  Serial.print(removePolls);
  Serial.print('/');
  Serial.print(reopenPolls);
  Serial.print('/');
  Serial.print(createPolls);
  Serial.print('/');
  Serial.print(runPolls);
  Serial.print(F(" cfg="));
  Serial.print(baseConfigId);
  Serial.print(F("->"));
  Serial.print(newConfig.configId);
  Serial.print(F(" flags="));
  Serial.print(vprHost.vprState().linkSessionOpen ? 'L' : '-');
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" shadow="));
  Serial.print(vprHost.workflowState().remoteCapabilitiesValid ? 'R' : '-');
  Serial.print(vprHost.workflowState().defaultSettingsApplied ? 'D' : '-');
  Serial.print(vprHost.workflowState().configCreated ? 'C' : '-');
  Serial.print(vprHost.workflowState().securityEnabled ? 'S' : '-');
  Serial.print(vprHost.workflowState().procedureParametersApplied ? 'P' : '-');
  Serial.print(vprHost.workflowState().procedureEnabled ? 'E' : '-');
  Serial.print(F(" cfg_state="));
  Serial.print(vprHost.workflowState().configComplete.configId);
  Serial.print('/');
  Serial.print(vprHost.workflowState().procedureEnableComplete.configId);
  Serial.print(F(" steps="));
  Serial.print(localModes.mode1Count);
  Serial.print('+');
  Serial.print(localModes.mode2Count);
  Serial.print('/');
  Serial.print(peerModes.mode1Count);
  Serial.print('+');
  Serial.print(peerModes.mode2Count);
  Serial.print(F(" proc="));
  Serial.print(vprHost.sessionState().completedProcedureCounter);
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(vprHost.workflowState().phase));
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprMultiConfigDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t kAltChannels[] = {6U, 18U, 30U, 39U};

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  hostConfig.session.workflow.procedureEnable.enable = 0U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 16U;
  hostConfig.session.workflow.procedureParameters.minSubeventLen = 0x000100UL;
  hostConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000100UL;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready();

  auto sendDirectCreate = [&](const BleCsControllerCreateConfig& config,
                              uint8_t* outStatus) -> bool {
    return vprHost.directCreateConfig(config, outStatus);
  };

  auto sendDirectSecurity = [&](uint8_t* outStatus) -> bool {
    return vprHost.directSecurityEnable(outStatus);
  };

  auto sendDirectSetProc = [&](const BleCsProcedureParameters& params,
                               uint8_t* outStatus) -> bool {
    return vprHost.directSetProcedureParameters(params, outStatus);
  };

  auto sendDirectEnable = [&](uint8_t configId, uint8_t enable,
                              uint8_t* outStatus) -> bool {
    return vprHost.directProcedureEnable(configId, enable != 0U, outStatus);
  };

  auto pollUntilStoppedWithProcedure = [&](uint16_t targetProcedureCount,
                                           uint8_t* outPolls) -> bool {
    return vprHost.pollUntilStoppedWithProcedureCount(targetProcedureCount, 160U,
                                                      outPolls);
  };

  auto pollUntilStoppedOnConfig = [&](uint8_t targetConfigId,
                                      uint8_t* outPolls) -> bool {
    return vprHost.pollUntilStoppedOnConfig(targetConfigId, 96U, outPolls);
  };

  auto settleDirectIdle = [&](uint8_t* outPolls) -> bool {
    return vprHost.settleDirectIdle(4U, 32U, outPolls);
  };

  const uint8_t baseConfigId = vprHost.workflowState().configComplete.configId;
  const uint16_t baseCompleted = vprHost.sessionState().completedProcedureCounter;
  BleCsControllerCreateConfig altConfig = hostConfig.session.workflow.createConfig;
  altConfig.configId = static_cast<uint8_t>(baseConfigId + 1U);
  altConfig.rttType = 0U;
  altConfig.minMainModeSteps = 4U;
  altConfig.maxMainModeSteps = 4U;
  memset(altConfig.channelMap, 0, sizeof(altConfig.channelMap));
  for (size_t i = 0U; i < sizeof(kAltChannels) / sizeof(kAltChannels[0]); ++i) {
    const uint8_t channel = kAltChannels[i];
    altConfig.channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
  }

  BleCsProcedureParameters altParams = hostConfig.session.workflow.procedureParameters;
  altParams.configId = altConfig.configId;
  altParams.maxProcedureCount = 1U;
  altParams.maxProcedureLen = 16U;
  altParams.minSubeventLen = 0x000100UL;
  altParams.maxSubeventLen = 0x000100UL;

  uint8_t createStatus = 0xFFU;
  uint8_t securityStatus = 0xFFU;
  uint8_t setAltStatus = 0xFFU;
  uint8_t runAltStatus = 0xFFU;
  uint8_t runBaseStatus = 0xFFU;
  uint8_t createPolls = 0U;
  uint8_t runAltPolls = 0U;
  uint8_t runBasePolls = 0U;

  ok = ok && sendDirectCreate(altConfig, &createStatus);
  ok = ok && sendDirectSecurity(&securityStatus);
  ok = ok && sendDirectSetProc(altParams, &setAltStatus);

  while (ok && !vprHost.failed() && createPolls < 24U) {
    const bool created =
        createStatus == 0U && securityStatus == 0U && setAltStatus == 0U &&
        vprHost.vprState().linkConfigCreated &&
        vprHost.vprState().linkSecurityEnabled &&
        vprHost.vprState().linkProcedureParamsApplied &&
        vprHost.vprState().linkConfigId == altConfig.configId &&
        vprHost.workflowState().configComplete.configId == altConfig.configId &&
        vprHost.workflowState().configComplete.rttType == 0U &&
        vprHost.workflowState().procedureParametersApplied;
    if (created) {
      break;
    }
    ok = vprHost.poll();
    ++createPolls;
  }

  ok = ok && sendDirectEnable(altConfig.configId, 1U, &runAltStatus);
  ok = ok && pollUntilStoppedWithProcedure(baseCompleted + 1U, &runAltPolls);
  const BleCsSubeventResult altLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult altPeer = vprHost.completedPeerResult();
  const StepModeCollectContext altLocalModes = collectStepModes(altLocal);
  const StepModeCollectContext altPeerModes = collectStepModes(altPeer);
  const bool altRunOk =
      runAltStatus == 0U && altLocal.header.configId == altConfig.configId &&
      altPeer.header.configId == altConfig.configId &&
      altLocalModes.mode1Count == 0U && altPeerModes.mode1Count == 0U &&
      altLocalModes.mode2Count == 4U && altPeerModes.mode2Count == 4U &&
      vprHost.workflowState().procedureEnableComplete.configId == altConfig.configId &&
      vprHost.vprState().linkConfigId == altConfig.configId;
  ok = ok && altRunOk;

  ok = ok && sendDirectEnable(baseConfigId, 1U, &runBaseStatus);
  ok = ok && pollUntilStoppedOnConfig(baseConfigId, &runBasePolls);
  const BleCsSubeventResult baseLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult basePeer = vprHost.completedPeerResult();
  const StepModeCollectContext baseLocalModes = collectStepModes(baseLocal);
  const StepModeCollectContext basePeerModes = collectStepModes(basePeer);
  const bool baseRunOk =
      runBaseStatus == 0U && baseLocal.header.configId == baseConfigId &&
      basePeer.header.configId == baseConfigId &&
      baseLocalModes.mode2Count >= 3U && basePeerModes.mode2Count >= 3U &&
      vprHost.workflowState().procedureEnableComplete.configId == baseConfigId &&
      vprHost.vprState().linkConfigId == baseConfigId;
  ok = ok && baseRunOk;

  uint8_t rerunAltStatus = 0xFFU;
  uint8_t rerunAltPolls = 0U;
  ok = ok && sendDirectEnable(altConfig.configId, 1U, &rerunAltStatus);
  ok = ok && pollUntilStoppedOnConfig(altConfig.configId, &rerunAltPolls);
  const BleCsSubeventResult altRerunLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult altRerunPeer = vprHost.completedPeerResult();
  const StepModeCollectContext altRerunLocalModes = collectStepModes(altRerunLocal);
  const StepModeCollectContext altRerunPeerModes = collectStepModes(altRerunPeer);
  const bool altRerunOk =
      rerunAltStatus == 0U &&
      altRerunLocal.header.configId == altConfig.configId &&
      altRerunPeer.header.configId == altConfig.configId &&
      altRerunLocalModes.mode1Count == 0U && altRerunPeerModes.mode1Count == 0U &&
      altRerunLocalModes.mode2Count == 4U && altRerunPeerModes.mode2Count == 4U &&
      vprHost.workflowState().procedureEnableComplete.configId == altConfig.configId &&
      vprHost.vprState().linkConfigId == altConfig.configId;
  ok = ok && baseRunOk && altRerunOk && vprHost.estimateValid();

  Serial.print(F("hcivprmulticfgdemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" create=0x"));
  Serial.print(createStatus, HEX);
  Serial.print(F(" sec=0x"));
  Serial.print(securityStatus, HEX);
  Serial.print(F(" set=0x"));
  Serial.print(setAltStatus, HEX);
  Serial.print(F(" run2=0x"));
  Serial.print(runAltStatus, HEX);
  Serial.print(F(" run1=0x"));
  Serial.print(runBaseStatus, HEX);
  Serial.print(F(" run2b=0x"));
  Serial.print(rerunAltStatus, HEX);
  Serial.print(F(" polls="));
  Serial.print(createPolls);
  Serial.print('/');
  Serial.print(runAltPolls);
  Serial.print('/');
  Serial.print(runBasePolls);
  Serial.print('/');
  Serial.print(rerunAltPolls);
  Serial.print(F(" cfg="));
  Serial.print(baseConfigId);
  Serial.print('/');
  Serial.print(altConfig.configId);
  Serial.print(F(" alt_steps="));
  Serial.print(altLocalModes.mode1Count);
  Serial.print('+');
  Serial.print(altLocalModes.mode2Count);
  Serial.print('/');
  Serial.print(altPeerModes.mode1Count);
  Serial.print('+');
  Serial.print(altPeerModes.mode2Count);
  Serial.print(F(" base_steps="));
  Serial.print(baseLocalModes.mode1Count);
  Serial.print('+');
  Serial.print(baseLocalModes.mode2Count);
  Serial.print('/');
  Serial.print(basePeerModes.mode1Count);
  Serial.print('+');
  Serial.print(basePeerModes.mode2Count);
  Serial.print(F(" alt2_steps="));
  Serial.print(altRerunLocalModes.mode1Count);
  Serial.print('+');
  Serial.print(altRerunLocalModes.mode2Count);
  Serial.print('/');
  Serial.print(altRerunPeerModes.mode1Count);
  Serial.print('+');
  Serial.print(altRerunPeerModes.mode2Count);
  Serial.print(F(" link_cfg="));
  Serial.print(vprHost.vprState().linkConfigId);
  Serial.print(F(" proc_cfg="));
  Serial.print(vprHost.workflowState().procedureEnableComplete.configId);
  Serial.print(F(" flags="));
  Serial.print(vprHost.vprState().linkSessionOpen ? 'L' : '-');
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprStoredRemoveDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t kAltChannels[] = {6U, 18U, 30U, 39U};

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  hostConfig.session.workflow.procedureEnable.enable = 0U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 16U;
  hostConfig.session.workflow.procedureParameters.minSubeventLen = 0x000100UL;
  hostConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000100UL;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready();

  auto sendDirectCreate = [&](const BleCsControllerCreateConfig& config,
                              uint8_t* outStatus) -> bool {
    return vprHost.directCreateConfig(config, outStatus);
  };

  auto sendDirectRemove = [&](uint8_t configId, uint8_t* outStatus) -> bool {
    return vprHost.directRemoveConfig(configId, outStatus);
  };

  auto sendDirectSecurity = [&](uint8_t* outStatus) -> bool {
    return vprHost.directSecurityEnable(outStatus);
  };

  auto sendDirectSetProc = [&](const BleCsProcedureParameters& params,
                               uint8_t* outStatus) -> bool {
    return vprHost.directSetProcedureParameters(params, outStatus);
  };

  auto sendDirectEnable = [&](uint8_t configId, uint8_t enable,
                              uint8_t* outStatus) -> bool {
    return vprHost.directProcedureEnable(configId, enable != 0U, outStatus);
  };

  auto pollUntilStoppedOnConfig = [&](uint8_t targetConfigId,
                                      uint8_t* outPolls) -> bool {
    return vprHost.pollUntilStoppedOnConfig(targetConfigId, 96U, outPolls);
  };

  auto settleDirectIdle = [&](uint8_t* outPolls) -> bool {
    return vprHost.settleDirectIdle(4U, 32U, outPolls);
  };

  const uint8_t baseConfigId = vprHost.workflowState().configComplete.configId;
  BleCsControllerCreateConfig altConfig = hostConfig.session.workflow.createConfig;
  altConfig.configId = static_cast<uint8_t>(baseConfigId + 1U);
  altConfig.rttType = 0U;
  altConfig.minMainModeSteps = 4U;
  altConfig.maxMainModeSteps = 4U;
  memset(altConfig.channelMap, 0, sizeof(altConfig.channelMap));
  for (size_t i = 0U; i < sizeof(kAltChannels) / sizeof(kAltChannels[0]); ++i) {
    const uint8_t channel = kAltChannels[i];
    altConfig.channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
  }

  BleCsProcedureParameters altParams = hostConfig.session.workflow.procedureParameters;
  altParams.configId = altConfig.configId;
  altParams.maxProcedureCount = 1U;
  altParams.maxProcedureLen = 16U;
  altParams.minSubeventLen = 0x000100UL;
  altParams.maxSubeventLen = 0x000100UL;

  uint8_t createStatus = 0xFFU;
  uint8_t securityStatus = 0xFFU;
  uint8_t setAltStatus = 0xFFU;
  uint8_t runAltStatus = 0xFFU;
  uint8_t runBaseStatus = 0xFFU;
  uint8_t removeAltStatus = 0xFFU;
  uint8_t rerunBaseStatus = 0xFFU;
  uint8_t runRemovedStatus = 0xFFU;
  uint8_t createPolls = 0U;
  uint8_t runAltPolls = 0U;
  uint8_t runBasePolls = 0U;
  uint8_t runAltSettlePolls = 0U;
  uint8_t runBaseSettlePolls = 0U;
  uint8_t removeAltPolls = 0U;
  uint8_t rerunBasePolls = 0U;
  uint8_t removeSettlePolls = 0U;

  ok = ok && sendDirectCreate(altConfig, &createStatus);
  ok = ok && sendDirectSecurity(&securityStatus);
  ok = ok && sendDirectSetProc(altParams, &setAltStatus);

  while (ok && !vprHost.failed() && createPolls < 24U) {
    const bool created =
        createStatus == 0U && securityStatus == 0U && setAltStatus == 0U &&
        vprHost.vprState().linkConfigCreated &&
        vprHost.vprState().linkSecurityEnabled &&
        vprHost.vprState().linkProcedureParamsApplied &&
        vprHost.vprState().linkConfigId == altConfig.configId &&
        vprHost.workflowState().configComplete.configId == altConfig.configId &&
        vprHost.workflowState().configComplete.rttType == 0U &&
        vprHost.workflowState().procedureParametersApplied;
    if (created) {
      break;
    }
    ok = vprHost.poll();
    ++createPolls;
  }
  ok = ok && createStatus == 0U && securityStatus == 0U && setAltStatus == 0U &&
       createPolls < 24U;

  ok = ok && sendDirectEnable(altConfig.configId, 1U, &runAltStatus);
  ok = ok && pollUntilStoppedOnConfig(altConfig.configId, &runAltPolls);
  const BleCsSubeventResult altLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult altPeer = vprHost.completedPeerResult();
  const StepModeCollectContext altLocalModes = collectStepModes(altLocal);
  const StepModeCollectContext altPeerModes = collectStepModes(altPeer);
  const bool altRunOk =
      runAltStatus == 0U &&
      altLocal.header.configId == altConfig.configId &&
      altPeer.header.configId == altConfig.configId &&
      altLocalModes.mode1Count == 0U && altPeerModes.mode1Count == 0U &&
      altLocalModes.mode2Count == 4U && altPeerModes.mode2Count == 4U &&
      vprHost.workflowState().procedureEnableComplete.configId == altConfig.configId &&
      vprHost.vprState().linkConfigId == altConfig.configId;
  ok = ok && altRunOk;

  ok = ok && sendDirectEnable(baseConfigId, 1U, &runBaseStatus);
  ok = ok && pollUntilStoppedOnConfig(baseConfigId, &runBasePolls);
  const BleCsSubeventResult baseLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult basePeer = vprHost.completedPeerResult();
  const StepModeCollectContext baseLocalModes = collectStepModes(baseLocal);
  const StepModeCollectContext basePeerModes = collectStepModes(basePeer);
  const bool baseRunOk =
      runBaseStatus == 0U && baseLocal.header.configId == baseConfigId &&
      basePeer.header.configId == baseConfigId &&
      baseLocalModes.mode2Count >= 3U && basePeerModes.mode2Count >= 3U &&
      vprHost.workflowState().procedureEnableComplete.configId == baseConfigId &&
      vprHost.vprState().linkConfigId == baseConfigId;
  ok = ok && baseRunOk;

  while (ok && !vprHost.failed() && removeSettlePolls < 16U) {
    ok = vprHost.poll();
    ++removeSettlePolls;
    if (removeSettlePolls >= 4U && !vprHost.vprState().linkProcedureEnabled &&
        vprHost.transport().available() == 0) {
      break;
    }
  }

  ok = ok && sendDirectRemove(altConfig.configId, &removeAltStatus);
  bool removedInactiveShadow = false;
  while (ok && !vprHost.failed() && removeAltPolls < 24U) {
    removedInactiveShadow =
        removeAltStatus == 0U &&
        vprHost.vprState().linkSessionOpen &&
        vprHost.vprState().linkConfigCreated &&
        vprHost.vprState().linkSecurityEnabled &&
        vprHost.vprState().linkProcedureParamsApplied &&
        !vprHost.vprState().linkProcedureEnabled &&
        vprHost.vprState().linkConfigId == baseConfigId &&
        vprHost.workflowState().remoteCapabilitiesValid &&
        vprHost.workflowState().defaultSettingsApplied &&
        vprHost.workflowState().configCreated &&
        vprHost.workflowState().securityEnabled &&
        vprHost.workflowState().procedureParametersApplied &&
        vprHost.workflowState().configComplete.configId == altConfig.configId &&
        vprHost.workflowState().configComplete.action == 0U &&
        vprHost.workflowState().procedureEnableComplete.configId == baseConfigId;
    if (removedInactiveShadow) {
      break;
    }
    ok = vprHost.poll();
    ++removeAltPolls;
  }
  ok = ok && removedInactiveShadow;

  ok = ok && sendDirectEnable(baseConfigId, 1U, &rerunBaseStatus);
  ok = ok && pollUntilStoppedOnConfig(baseConfigId, &rerunBasePolls);
  const BleCsSubeventResult baseRerunLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult baseRerunPeer = vprHost.completedPeerResult();
  const StepModeCollectContext baseRerunLocalModes = collectStepModes(baseRerunLocal);
  const StepModeCollectContext baseRerunPeerModes = collectStepModes(baseRerunPeer);
  const bool baseRerunOk =
      rerunBaseStatus == 0U &&
      baseRerunLocal.header.configId == baseConfigId &&
      baseRerunPeer.header.configId == baseConfigId &&
      baseRerunLocalModes.mode2Count >= 3U && baseRerunPeerModes.mode2Count >= 3U &&
      vprHost.workflowState().procedureEnableComplete.configId == baseConfigId &&
      vprHost.vprState().linkConfigId == baseConfigId;
  ok = ok && baseRerunOk;

  ok = ok && sendDirectEnable(altConfig.configId, 1U, &runRemovedStatus);
  const bool removedConfigRejected =
      runRemovedStatus == 0x12U &&
      !vprHost.vprState().linkProcedureEnabled &&
      vprHost.vprState().linkConfigId == baseConfigId &&
      vprHost.workflowState().procedureEnableComplete.configId == baseConfigId;
  ok = ok && removedConfigRejected && vprHost.estimateValid();

  Serial.print(F("hcivprrmstoredemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" create=0x"));
  Serial.print(createStatus, HEX);
  Serial.print(F(" sec=0x"));
  Serial.print(securityStatus, HEX);
  Serial.print(F(" set=0x"));
  Serial.print(setAltStatus, HEX);
  Serial.print(F(" run2=0x"));
  Serial.print(runAltStatus, HEX);
  Serial.print(F(" run1=0x"));
  Serial.print(runBaseStatus, HEX);
  Serial.print(F(" rm2=0x"));
  Serial.print(removeAltStatus, HEX);
  Serial.print(F(" run1b=0x"));
  Serial.print(rerunBaseStatus, HEX);
  Serial.print(F(" run2x=0x"));
  Serial.print(runRemovedStatus, HEX);
  Serial.print(F(" polls="));
  Serial.print(createPolls);
  Serial.print('/');
  Serial.print(runAltPolls);
  Serial.print('/');
  Serial.print(runBasePolls);
  Serial.print('/');
  Serial.print(removeSettlePolls);
  Serial.print('/');
  Serial.print(removeAltPolls);
  Serial.print('/');
  Serial.print(rerunBasePolls);
  Serial.print(F(" cfg="));
  Serial.print(baseConfigId);
  Serial.print('/');
  Serial.print(altConfig.configId);
  Serial.print(F(" alt_steps="));
  Serial.print(altLocalModes.mode1Count);
  Serial.print('+');
  Serial.print(altLocalModes.mode2Count);
  Serial.print('/');
  Serial.print(altPeerModes.mode1Count);
  Serial.print('+');
  Serial.print(altPeerModes.mode2Count);
  Serial.print(F(" base2_steps="));
  Serial.print(baseRerunLocalModes.mode1Count);
  Serial.print('+');
  Serial.print(baseRerunLocalModes.mode2Count);
  Serial.print('/');
  Serial.print(baseRerunPeerModes.mode1Count);
  Serial.print('+');
  Serial.print(baseRerunPeerModes.mode2Count);
  Serial.print(F(" cfg_evt="));
  Serial.print(vprHost.workflowState().configComplete.configId);
  Serial.print('/');
  Serial.print(vprHost.workflowState().configComplete.action);
  Serial.print(F(" link_cfg="));
  Serial.print(vprHost.vprState().linkConfigId);
  Serial.print(F(" proc_cfg="));
  Serial.print(vprHost.workflowState().procedureEnableComplete.configId);
  Serial.print(F(" flags="));
  Serial.print(vprHost.vprState().linkSessionOpen ? 'L' : '-');
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" shadow="));
  Serial.print(vprHost.workflowState().remoteCapabilitiesValid ? 'R' : '-');
  Serial.print(vprHost.workflowState().defaultSettingsApplied ? 'D' : '-');
  Serial.print(vprHost.workflowState().configCreated ? 'C' : '-');
  Serial.print(vprHost.workflowState().securityEnabled ? 'S' : '-');
  Serial.print(vprHost.workflowState().procedureParametersApplied ? 'P' : '-');
  Serial.print(vprHost.workflowState().procedureEnabled ? 'E' : '-');
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprActiveRemoveDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t kAltChannels[] = {6U, 18U, 30U, 39U};

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  hostConfig.session.workflow.procedureEnable.enable = 0U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 16U;
  hostConfig.session.workflow.procedureParameters.minSubeventLen = 0x000100UL;
  hostConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000100UL;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready();

  auto sendDirectCreate = [&](const BleCsControllerCreateConfig& config,
                              uint8_t* outStatus) -> bool {
    return vprHost.directCreateConfig(config, outStatus);
  };

  auto sendDirectRemove = [&](uint8_t configId, uint8_t* outStatus) -> bool {
    return vprHost.directRemoveConfig(configId, outStatus);
  };

  auto sendDirectSecurity = [&](uint8_t* outStatus) -> bool {
    return vprHost.directSecurityEnable(outStatus);
  };

  auto sendDirectSetProc = [&](const BleCsProcedureParameters& params,
                               uint8_t* outStatus) -> bool {
    return vprHost.directSetProcedureParameters(params, outStatus);
  };

  auto sendDirectEnable = [&](uint8_t configId, uint8_t enable,
                              uint8_t* outStatus) -> bool {
    return vprHost.directProcedureEnable(configId, enable != 0U, outStatus);
  };

  auto pollUntilStoppedOnConfig = [&](uint8_t targetConfigId,
                                      uint8_t* outPolls) -> bool {
    return vprHost.pollUntilStoppedOnConfig(targetConfigId, 96U, outPolls);
  };

  auto pollUntilSelectedState = [&](uint8_t selectedConfigId, uint8_t storedCount,
                                    bool selectedRunnable, uint8_t* outPolls) -> bool {
    return vprHost.pollUntilSelectedState(selectedConfigId, storedCount,
                                          selectedRunnable, 32U, outPolls);
  };

  const uint8_t baseConfigId = vprHost.workflowState().configComplete.configId;
  BleCsControllerCreateConfig altConfig = hostConfig.session.workflow.createConfig;
  altConfig.configId = static_cast<uint8_t>(baseConfigId + 1U);
  altConfig.rttType = 0U;
  altConfig.minMainModeSteps = 4U;
  altConfig.maxMainModeSteps = 4U;
  memset(altConfig.channelMap, 0, sizeof(altConfig.channelMap));
  for (size_t i = 0U; i < sizeof(kAltChannels) / sizeof(kAltChannels[0]); ++i) {
    const uint8_t channel = kAltChannels[i];
    altConfig.channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
  }

  BleCsProcedureParameters baseParams = hostConfig.session.workflow.procedureParameters;
  baseParams.maxProcedureCount = 1U;
  baseParams.maxProcedureLen = 16U;
  baseParams.minSubeventLen = 0x000100UL;
  baseParams.maxSubeventLen = 0x000100UL;

  BleCsProcedureParameters altParams = baseParams;
  altParams.configId = altConfig.configId;

  uint8_t createStatus = 0xFFU;
  uint8_t securityStatus = 0xFFU;
  uint8_t setAltStatus = 0xFFU;
  uint8_t setBaseStatus = 0xFFU;
  uint8_t removeBaseStatus = 0xFFU;
  uint8_t runAltStatus = 0xFFU;
  uint8_t runRemovedStatus = 0xFFU;
  uint8_t createPolls = 0U;
  uint8_t baseSelectPolls = 0U;
  uint8_t promotedPolls = 0U;
  uint8_t runAltPolls = 0U;

  ok = ok && sendDirectCreate(altConfig, &createStatus);
  ok = ok && sendDirectSecurity(&securityStatus);
  ok = ok && sendDirectSetProc(altParams, &setAltStatus);
  ok = ok && pollUntilSelectedState(altConfig.configId, 2U, true, &createPolls);
  const BleCsControllerVprHostState armedAltState = vprHost.vprState();

  ok = ok && sendDirectSetProc(baseParams, &setBaseStatus);
  ok = ok && pollUntilSelectedState(baseConfigId, 2U, true, &baseSelectPolls);
  const BleCsControllerVprHostState baseSelectedState = vprHost.vprState();

  ok = ok && sendDirectRemove(baseConfigId, &removeBaseStatus);
  ok = ok && pollUntilSelectedState(altConfig.configId, 1U, true, &promotedPolls);
  const BleCsControllerVprHostState promotedState = vprHost.vprState();

  ok = ok && sendDirectEnable(altConfig.configId, 1U, &runAltStatus);
  ok = ok && pollUntilStoppedOnConfig(altConfig.configId, &runAltPolls);
  const BleCsSubeventResult altLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult altPeer = vprHost.completedPeerResult();
  const StepModeCollectContext altLocalModes = collectStepModes(altLocal);
  const StepModeCollectContext altPeerModes = collectStepModes(altPeer);

  ok = ok && sendDirectEnable(baseConfigId, 1U, &runRemovedStatus);

  const bool promotedRunOk =
      createStatus == 0U && securityStatus == 0U && setAltStatus == 0U &&
      setBaseStatus == 0U && removeBaseStatus == 0U && runAltStatus == 0U &&
      runRemovedStatus == 0x12U &&
      armedAltState.linkConfigId == altConfig.configId &&
      armedAltState.linkSelectedConfigRunnable &&
      baseSelectedState.linkConfigId == baseConfigId &&
      baseSelectedState.linkSelectedConfigRunnable &&
      promotedState.linkConfigId == altConfig.configId &&
      promotedState.linkStoredConfigCount == 1U &&
      promotedState.linkSelectedConfigRunnable &&
      altLocal.header.configId == altConfig.configId &&
      altPeer.header.configId == altConfig.configId &&
      altLocalModes.mode1Count == 0U && altPeerModes.mode1Count == 0U &&
      altLocalModes.mode2Count == 4U && altPeerModes.mode2Count == 4U &&
      !vprHost.vprState().linkProcedureEnabled &&
      vprHost.vprState().linkConfigId == altConfig.configId &&
      vprHost.vprState().linkSelectedConfigRunnable;
  ok = ok && promotedRunOk;

  Serial.print(F("hcivprrmactivedemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" create=0x"));
  Serial.print(createStatus, HEX);
  Serial.print(F(" sec=0x"));
  Serial.print(securityStatus, HEX);
  Serial.print(F(" set=0x"));
  Serial.print(setAltStatus, HEX);
  Serial.print('/');
  Serial.print(setBaseStatus, HEX);
  Serial.print(F(" rm1=0x"));
  Serial.print(removeBaseStatus, HEX);
  Serial.print(F(" run2=0x"));
  Serial.print(runAltStatus, HEX);
  Serial.print(F(" run1x=0x"));
  Serial.print(runRemovedStatus, HEX);
  Serial.print(F(" polls="));
  Serial.print(createPolls);
  Serial.print('/');
  Serial.print(baseSelectPolls);
  Serial.print('/');
  Serial.print(promotedPolls);
  Serial.print('/');
  Serial.print(runAltPolls);
  Serial.print(F(" cfg="));
  Serial.print(baseConfigId);
  Serial.print('/');
  Serial.print(altConfig.configId);
  Serial.print(F(" active="));
  Serial.print(armedAltState.linkConfigId);
  Serial.print('>');
  Serial.print(baseSelectedState.linkConfigId);
  Serial.print('>');
  Serial.print(promotedState.linkConfigId);
  Serial.print(F(" count="));
  Serial.print(armedAltState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(baseSelectedState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(promotedState.linkStoredConfigCount);
  Serial.print(F(" run="));
  Serial.print(armedAltState.linkSelectedConfigRunnable ? 1 : 0);
  Serial.print('>');
  Serial.print(baseSelectedState.linkSelectedConfigRunnable ? 1 : 0);
  Serial.print('>');
  Serial.print(promotedState.linkSelectedConfigRunnable ? 1 : 0);
  Serial.print(F(" slots="));
  Serial.print(promotedState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(promotedState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(promotedState.linkPreviousConfigId);
  Serial.print(F(" slot_run="));
  Serial.print(promotedState.linkSlot0Runnable ? 1 : 0);
  Serial.print(promotedState.linkSlot1Runnable ? '1' : '0');
  Serial.print(promotedState.linkPreviousSlotRunnable ? '1' : '0');
  Serial.print(F(" alt_steps="));
  Serial.print(altLocalModes.mode1Count);
  Serial.print('+');
  Serial.print(altLocalModes.mode2Count);
  Serial.print('/');
  Serial.print(altPeerModes.mode1Count);
  Serial.print('+');
  Serial.print(altPeerModes.mode2Count);
  Serial.print(F(" shadow="));
  Serial.print(vprHost.workflowState().remoteCapabilitiesValid ? 'R' : '-');
  Serial.print(vprHost.workflowState().defaultSettingsApplied ? 'D' : '-');
  Serial.print(vprHost.workflowState().configCreated ? 'C' : '-');
  Serial.print(vprHost.workflowState().securityEnabled ? 'S' : '-');
  Serial.print(vprHost.workflowState().procedureParametersApplied ? 'P' : '-');
  Serial.print(vprHost.workflowState().procedureEnabled ? 'E' : '-');
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprInventoryDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t kAltChannels[] = {6U, 18U, 30U, 39U};

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  hostConfig.session.workflow.procedureEnable.enable = 0U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 16U;
  hostConfig.session.workflow.procedureParameters.minSubeventLen = 0x000100UL;
  hostConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000100UL;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready();

  auto sendDirectCreate = [&](const BleCsControllerCreateConfig& config,
                              uint8_t* outStatus) -> bool {
    return vprHost.directCreateConfig(config, outStatus);
  };

  auto sendDirectRemove = [&](uint8_t configId, uint8_t* outStatus) -> bool {
    return vprHost.directRemoveConfig(configId, outStatus);
  };

  auto sendDirectSecurity = [&](uint8_t* outStatus) -> bool {
    return vprHost.directSecurityEnable(outStatus);
  };

  auto sendDirectSetProc = [&](const BleCsProcedureParameters& params,
                               uint8_t* outStatus) -> bool {
    return vprHost.directSetProcedureParameters(params, outStatus);
  };

  auto sendDirectEnable = [&](uint8_t configId, uint8_t enable,
                              uint8_t* outStatus) -> bool {
    return vprHost.directProcedureEnable(configId, enable != 0U, outStatus);
  };

  auto pollUntilStoppedOnConfig = [&](uint8_t targetConfigId,
                                      uint8_t* outPolls) -> bool {
    return vprHost.pollUntilStoppedOnConfig(targetConfigId, 96U, outPolls);
  };

  auto settleDirectIdle = [&](uint8_t* outPolls) -> bool {
    return vprHost.settleDirectIdle(4U, 32U, outPolls);
  };

  const uint8_t baseConfigId = vprHost.workflowState().configComplete.configId;
  const uint8_t countInitial = vprHost.vprState().linkStoredConfigCount;
  BleCsControllerCreateConfig altConfig = hostConfig.session.workflow.createConfig;
  altConfig.configId = static_cast<uint8_t>(baseConfigId + 1U);
  altConfig.rttType = 0U;
  altConfig.minMainModeSteps = 4U;
  altConfig.maxMainModeSteps = 4U;
  memset(altConfig.channelMap, 0, sizeof(altConfig.channelMap));
  for (size_t i = 0U; i < sizeof(kAltChannels) / sizeof(kAltChannels[0]); ++i) {
    const uint8_t channel = kAltChannels[i];
    altConfig.channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
  }

  BleCsProcedureParameters altParams = hostConfig.session.workflow.procedureParameters;
  altParams.configId = altConfig.configId;
  altParams.maxProcedureCount = 1U;
  altParams.maxProcedureLen = 16U;
  altParams.minSubeventLen = 0x000100UL;
  altParams.maxSubeventLen = 0x000100UL;

  uint8_t createStatus = 0xFFU;
  uint8_t securityStatus = 0xFFU;
  uint8_t setAltStatus = 0xFFU;
  uint8_t runAltStatus = 0xFFU;
  uint8_t runBaseStatus = 0xFFU;
  uint8_t removeAltStatus = 0xFFU;
  uint8_t removeBaseStatus = 0xFFU;
  uint8_t createPolls = 0U;
  uint8_t runAltPolls = 0U;
  uint8_t runBasePolls = 0U;
  uint8_t runAltSettlePolls = 0U;
  uint8_t runBaseSettlePolls = 0U;
  uint8_t removeAltPolls = 0U;
  uint8_t removeBasePolls = 0U;
  uint8_t altSettlePolls = 0U;
  uint8_t removeSettlePolls = 0U;

  ok = ok && sendDirectCreate(altConfig, &createStatus);
  ok = ok && sendDirectSecurity(&securityStatus);
  ok = ok && sendDirectSetProc(altParams, &setAltStatus);

  while (ok && !vprHost.failed() && createPolls < 24U) {
    const bool created =
        createStatus == 0U && securityStatus == 0U && setAltStatus == 0U &&
        vprHost.vprState().linkConfigId == altConfig.configId &&
        vprHost.vprState().linkStoredConfigCount == 2U &&
        vprHost.workflowState().configComplete.configId == altConfig.configId;
    if (created) {
      break;
    }
    ok = vprHost.poll();
    ++createPolls;
  }
  const uint8_t countAfterCreate = vprHost.vprState().linkStoredConfigCount;
  ok = ok && createStatus == 0U && securityStatus == 0U && setAltStatus == 0U &&
       countAfterCreate == 2U;

  ok = ok && sendDirectEnable(altConfig.configId, 1U, &runAltStatus);
  ok = ok && pollUntilStoppedOnConfig(altConfig.configId, &runAltPolls);
  ok = ok && runAltStatus == 0U &&
       vprHost.workflowState().procedureEnableComplete.configId == altConfig.configId &&
       vprHost.vprState().linkConfigId == altConfig.configId;

  ok = ok && settleDirectIdle(&altSettlePolls);

  ok = ok && sendDirectEnable(baseConfigId, 1U, &runBaseStatus);
  ok = ok && pollUntilStoppedOnConfig(baseConfigId, &runBasePolls);
  ok = ok && runBaseStatus == 0U &&
       vprHost.workflowState().procedureEnableComplete.configId == baseConfigId &&
       vprHost.vprState().linkConfigId == baseConfigId;

  ok = ok && settleDirectIdle(&removeSettlePolls);

  ok = ok && sendDirectRemove(altConfig.configId, &removeAltStatus);
  while (ok && !vprHost.failed() && removeAltPolls < 24U) {
    if (removeAltStatus == 0U && vprHost.vprState().linkStoredConfigCount == 1U &&
        vprHost.vprState().linkSessionOpen &&
        vprHost.vprState().linkConfigId == baseConfigId &&
        vprHost.workflowState().configComplete.configId == altConfig.configId &&
        vprHost.workflowState().configComplete.action == 0U) {
      break;
    }
    ok = vprHost.poll();
    ++removeAltPolls;
  }
  const uint8_t countAfterRemoveAlt = vprHost.vprState().linkStoredConfigCount;
  ok = ok && removeAltStatus == 0U && countAfterRemoveAlt == 1U;

  ok = ok && sendDirectRemove(baseConfigId, &removeBaseStatus);
  while (ok && !vprHost.failed() && removeBasePolls < 24U) {
    if (removeBaseStatus == 0U && vprHost.vprState().linkStoredConfigCount == 0U &&
        !vprHost.vprState().linkSessionOpen && !vprHost.vprState().linkConfigCreated &&
        !vprHost.vprState().linkSecurityEnabled &&
        !vprHost.vprState().linkProcedureParamsApplied &&
        !vprHost.vprState().linkProcedureEnabled) {
      break;
    }
    ok = vprHost.poll();
    ++removeBasePolls;
  }
  const uint8_t countAfterRemoveBase = vprHost.vprState().linkStoredConfigCount;
  ok = ok && removeBaseStatus == 0U && countInitial == 1U &&
       countAfterCreate == 2U && countAfterRemoveAlt == 1U &&
       countAfterRemoveBase == 0U;

  Serial.print(F("hcivprinventorydemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" create=0x"));
  Serial.print(createStatus, HEX);
  Serial.print(F(" sec=0x"));
  Serial.print(securityStatus, HEX);
  Serial.print(F(" set=0x"));
  Serial.print(setAltStatus, HEX);
  Serial.print(F(" run2=0x"));
  Serial.print(runAltStatus, HEX);
  Serial.print(F(" run1=0x"));
  Serial.print(runBaseStatus, HEX);
  Serial.print(F(" rm2=0x"));
  Serial.print(removeAltStatus, HEX);
  Serial.print(F(" rm1=0x"));
  Serial.print(removeBaseStatus, HEX);
  Serial.print(F(" polls="));
  Serial.print(createPolls);
  Serial.print('/');
  Serial.print(runAltPolls);
  Serial.print('/');
  Serial.print(runBasePolls);
  Serial.print('/');
  Serial.print(altSettlePolls);
  Serial.print('/');
  Serial.print(removeSettlePolls);
  Serial.print('/');
  Serial.print(removeAltPolls);
  Serial.print('/');
  Serial.print(removeBasePolls);
  Serial.print(F(" cfg="));
  Serial.print(baseConfigId);
  Serial.print('/');
  Serial.print(altConfig.configId);
  Serial.print(F(" count="));
  Serial.print(countInitial);
  Serial.print('>');
  Serial.print(countAfterCreate);
  Serial.print('>');
  Serial.print(countAfterRemoveAlt);
  Serial.print('>');
  Serial.print(countAfterRemoveBase);
  Serial.print(F(" link_cfg="));
  Serial.print(vprHost.vprState().linkConfigId);
  Serial.print(F(" cfg_evt="));
  Serial.print(vprHost.workflowState().configComplete.configId);
  Serial.print('/');
  Serial.print(vprHost.workflowState().configComplete.action);
  Serial.print(F(" flags="));
  Serial.print(vprHost.vprState().linkSessionOpen ? 'L' : '-');
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" op=0x"));
  Serial.print(vprHost.vprState().lastOpcode, HEX);
  Serial.print(F(" err=0x"));
  Serial.print(vprHost.vprState().lastError, HEX);
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(vprHost.workflowState().phase));
  Serial.print(F(" dist_m="));
  Serial.println(F("na"));
}

void printHciVprSlotDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static constexpr uint8_t kAltChannels[] = {2U, 14U, 26U, 38U};

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready();

  auto sendDirectCreate = [&](const BleCsControllerCreateConfig& config,
                              uint8_t* outStatus) -> bool {
    return vprHost.directCreateConfig(config, outStatus);
  };

  auto sendDirectSecurity = [&](uint8_t* outStatus) -> bool {
    return vprHost.directSecurityEnable(outStatus);
  };

  auto sendDirectSetProc = [&](const BleCsProcedureParameters& params,
                               uint8_t* outStatus) -> bool {
    return vprHost.directSetProcedureParameters(params, outStatus);
  };

  auto sendDirectEnable = [&](uint8_t configId, uint8_t enable,
                              uint8_t* outStatus) -> bool {
    return vprHost.directProcedureEnable(configId, enable != 0U, outStatus);
  };

  auto sendDirectRemove = [&](uint8_t configId, uint8_t* outStatus) -> bool {
    return vprHost.directRemoveConfig(configId, outStatus);
  };

  auto pollUntilStoppedOnConfig = [&](uint8_t targetConfigId,
                                      uint8_t* outPolls) -> bool {
    return vprHost.pollUntilStoppedOnConfig(targetConfigId, 96U, outPolls);
  };

  auto settleDirectIdle = [&](uint8_t* outPolls) -> bool {
    return vprHost.settleDirectIdle(4U, 32U, outPolls);
  };

  auto slotStateMatches = [&](uint8_t activeConfigId, uint8_t slot0ConfigId,
                              uint8_t slot1ConfigId, uint8_t previousConfigId,
                              uint8_t activePrimarySlotIndex,
                              uint8_t freePrimarySlotCount,
                              uint8_t storedConfigCount) -> bool {
    const BleCsControllerVprHostState& state = vprHost.vprState();
    return state.retainedConfigMatchesSlots(activeConfigId, slot0ConfigId,
                                            slot1ConfigId, previousConfigId,
                                            activePrimarySlotIndex,
                                            freePrimarySlotCount,
                                            storedConfigCount);
  };

  auto pollUntilSlotState = [&](uint8_t activeConfigId, uint8_t slot0ConfigId,
                                uint8_t slot1ConfigId, uint8_t previousConfigId,
                                uint8_t activePrimarySlotIndex,
                                uint8_t freePrimarySlotCount,
                                uint8_t storedConfigCount,
                                uint8_t* outPolls) -> bool {
    return vprHost.pollUntilRetainedSlots(activeConfigId, slot0ConfigId,
                                          slot1ConfigId, previousConfigId,
                                          activePrimarySlotIndex,
                                          freePrimarySlotCount,
                                          storedConfigCount, 32U, outPolls);
  };

  const uint8_t baseConfigId = vprHost.workflowState().configComplete.configId;
  BleCsControllerCreateConfig altConfig = hostConfig.session.workflow.createConfig;
  altConfig.configId = static_cast<uint8_t>(baseConfigId + 1U);
  altConfig.rttType = 0U;
  altConfig.minMainModeSteps = 4U;
  altConfig.maxMainModeSteps = 4U;
  memset(altConfig.channelMap, 0, sizeof(altConfig.channelMap));
  for (size_t i = 0U; i < sizeof(kAltChannels) / sizeof(kAltChannels[0]); ++i) {
    const uint8_t channel = kAltChannels[i];
    altConfig.channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
  }

  BleCsProcedureParameters altParams = hostConfig.session.workflow.procedureParameters;
  altParams.configId = altConfig.configId;
  altParams.maxProcedureCount = 1U;
  altParams.maxProcedureLen = 16U;
  altParams.minSubeventLen = 0x000100UL;
  altParams.maxSubeventLen = 0x000100UL;

  uint8_t initPolls = 0U;
  uint8_t createStatus = 0xFFU;
  uint8_t securityStatus = 0xFFU;
  uint8_t setAltStatus = 0xFFU;
  uint8_t runBaseStatus = 0xFFU;
  uint8_t runAltStatus = 0xFFU;
  uint8_t createPolls = 0U;
  uint8_t runAltPolls = 0U;
  uint8_t runAltSettlePolls = 0U;
  uint8_t runBasePolls = 0U;
  uint8_t runBaseSettlePolls = 0U;
  bool baseIdleSettled = false;

  ok = ok && pollUntilSlotState(baseConfigId, baseConfigId, 0U, 0U, 0U, 1U, 1U,
                                &initPolls);
  const BleCsControllerVprHostState initialState = vprHost.vprState();

  ok = ok && sendDirectCreate(altConfig, &createStatus);
  ok = ok && sendDirectSecurity(&securityStatus);
  ok = ok && sendDirectSetProc(altParams, &setAltStatus);
  while (ok && !vprHost.failed() && createPolls < 24U) {
    const bool created =
        createStatus == 0U && securityStatus == 0U && setAltStatus == 0U &&
        vprHost.vprState().linkConfigId == altConfig.configId &&
        vprHost.vprState().linkStoredConfigCount == 2U &&
        vprHost.workflowState().configComplete.configId == altConfig.configId;
    if (created) {
      break;
    }
    ok = vprHost.poll();
    ++createPolls;
  }
  ok = ok &&
       pollUntilSlotState(altConfig.configId, baseConfigId, altConfig.configId,
                          baseConfigId, 1U, 0U, 2U, nullptr);
  const BleCsControllerVprHostState createdState = vprHost.vprState();

  ok = ok && sendDirectEnable(altConfig.configId, 1U, &runAltStatus);
  ok = ok && pollUntilStoppedOnConfig(altConfig.configId, &runAltPolls);
  ok = ok && settleDirectIdle(&runAltSettlePolls);
  ok = ok &&
       pollUntilSlotState(altConfig.configId, baseConfigId, altConfig.configId,
                          baseConfigId, 1U, 0U, 2U, nullptr);
  const BleCsControllerVprHostState altRunState = vprHost.vprState();

  ok = ok && sendDirectEnable(baseConfigId, 1U, &runBaseStatus);
  ok = ok &&
       pollUntilSlotState(baseConfigId, baseConfigId, altConfig.configId,
                          altConfig.configId, 0U, 0U, 2U, &runBasePolls);
  if (ok) {
    baseIdleSettled = settleDirectIdle(&runBaseSettlePolls);
  }
  const BleCsControllerVprHostState baseRunState = vprHost.vprState();

  ok = ok && createStatus == 0U && securityStatus == 0U && setAltStatus == 0U &&
       runBaseStatus == 0U && runAltStatus == 0U;

  Serial.print(F("hcivprslotdemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" create=0x"));
  Serial.print(createStatus, HEX);
  Serial.print(F(" sec=0x"));
  Serial.print(securityStatus, HEX);
  Serial.print(F(" set=0x"));
  Serial.print(setAltStatus, HEX);
  Serial.print(F(" run1=0x"));
  Serial.print(runBaseStatus, HEX);
  Serial.print(F(" run2=0x"));
  Serial.print(runAltStatus, HEX);
  Serial.print(F(" idle="));
  Serial.print(1);
  Serial.print('/');
  Serial.print(baseIdleSettled ? 1 : 0);
  Serial.print(F(" polls="));
  Serial.print(initPolls);
  Serial.print('/');
  Serial.print(createPolls);
  Serial.print('/');
  Serial.print(runAltPolls);
  Serial.print('/');
  Serial.print(runAltSettlePolls);
  Serial.print('/');
  Serial.print(runBasePolls);
  Serial.print('/');
  Serial.print(runBaseSettlePolls);
  Serial.print(F(" slots="));
  Serial.print(initialState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(initialState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(initialState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(createdState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(createdState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(createdState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(altRunState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(altRunState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(altRunState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(baseRunState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(baseRunState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(baseRunState.linkPreviousConfigId);
  Serial.print(F(" active="));
  Serial.print(initialState.linkActivePrimarySlotIndex);
  Serial.print('>');
  Serial.print(createdState.linkActivePrimarySlotIndex);
  Serial.print('>');
  Serial.print(altRunState.linkActivePrimarySlotIndex);
  Serial.print('>');
  Serial.print(baseRunState.linkActivePrimarySlotIndex);
  Serial.print(F(" free="));
  Serial.print(initialState.linkFreePrimarySlotCount);
  Serial.print('>');
  Serial.print(createdState.linkFreePrimarySlotCount);
  Serial.print('>');
  Serial.print(altRunState.linkFreePrimarySlotCount);
  Serial.print('>');
  Serial.print(baseRunState.linkFreePrimarySlotCount);
  Serial.print(F(" count="));
  Serial.print(initialState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(createdState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(altRunState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(baseRunState.linkStoredConfigCount);
  Serial.print(F(" cfg="));
  Serial.print(initialState.linkConfigId);
  Serial.print('>');
  Serial.print(createdState.linkConfigId);
  Serial.print('>');
  Serial.print(altRunState.linkConfigId);
  Serial.print('>');
  Serial.print(baseRunState.linkConfigId);
  Serial.print(F(" prev_used="));
  Serial.print(initialState.linkPreviousSlotInUse ? 1 : 0);
  Serial.print('>');
  Serial.print(createdState.linkPreviousSlotInUse ? 1 : 0);
  Serial.print('>');
  Serial.print(altRunState.linkPreviousSlotInUse ? 1 : 0);
  Serial.print('>');
  Serial.print(baseRunState.linkPreviousSlotInUse ? 1 : 0);
  Serial.print(F(" op=0x"));
  Serial.print(vprHost.vprState().lastOpcode, HEX);
  Serial.print(F(" err=0x"));
  Serial.print(vprHost.vprState().lastError, HEX);
  Serial.print(F(" dist_m="));
  Serial.println(F("na"));
}

void printHciVprSelectDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static constexpr uint8_t kAltChannels[] = {2U, 14U, 26U, 38U};
#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  static constexpr uint32_t kSelectSummaryMagic = 0x56504155UL;
  enum : uint32_t {
    kSelectStageBooting = 1UL,
    kSelectStageInitial = 2UL,
    kSelectStageCreated = 3UL,
    kSelectStageSecured = 4UL,
    kSelectStageArmed = 5UL,
    kSelectStageBaseSelected = 6UL,
    kSelectStageAltSelected = 7UL,
    kSelectStageDone = 8UL,
  };
#endif

#if defined(NRF54L15_CS_VPR_SELECT_SUMMARY) && defined(NRF54L15_CS_VPR_AUTO_SELECT_DEMO)
  const bool emitSerial = false;
#else
  const bool emitSerial = true;
#endif

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);

#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  memset((void*)&gVprSelectDemoSummary, 0, sizeof(gVprSelectDemoSummary));
  gVprSelectDemoSummary.magic = kSelectSummaryMagic;
  gVprSelectDemoSummary.stage = kSelectStageBooting;
#endif

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready();

  auto sendDirectCreate = [&](const BleCsControllerCreateConfig& config,
                              uint8_t* outStatus) -> bool {
    return vprHost.directCreateConfig(config, outStatus);
  };

  auto sendDirectSecurity = [&](uint8_t* outStatus) -> bool {
    return vprHost.directSecurityEnable(outStatus);
  };

  auto sendDirectSetProc = [&](const BleCsProcedureParameters& params,
                               uint8_t* outStatus) -> bool {
    return vprHost.directSetProcedureParameters(params, outStatus);
  };

  auto slotStateMatches = [&](uint8_t activeConfigId, uint8_t slot0ConfigId,
                              uint8_t slot1ConfigId, uint8_t previousConfigId,
                              uint8_t activePrimarySlotIndex,
                              uint8_t freePrimarySlotCount,
                              uint8_t storedConfigCount) -> bool {
    const BleCsControllerVprHostState& state = vprHost.vprState();
    return state.retainedConfigMatchesSlots(activeConfigId, slot0ConfigId,
                                            slot1ConfigId, previousConfigId,
                                            activePrimarySlotIndex,
                                            freePrimarySlotCount,
                                            storedConfigCount);
  };

  auto pollUntilSlotState = [&](uint8_t activeConfigId, uint8_t slot0ConfigId,
                                uint8_t slot1ConfigId, uint8_t previousConfigId,
                                uint8_t activePrimarySlotIndex,
                                uint8_t freePrimarySlotCount,
                                uint8_t storedConfigCount,
                                uint8_t* outPolls) -> bool {
    if (outPolls != nullptr) {
      *outPolls = 0U;
    }
    while (!vprHost.failed()) {
      if (slotStateMatches(activeConfigId, slot0ConfigId, slot1ConfigId, previousConfigId,
                           activePrimarySlotIndex, freePrimarySlotCount,
                           storedConfigCount)) {
        return true;
      }
      if (outPolls != nullptr && *outPolls >= 32U) {
        break;
      }
      if (!vprHost.poll()) {
        return false;
      }
      if (outPolls != nullptr) {
        *outPolls = static_cast<uint8_t>(*outPolls + 1U);
      }
    }
    return false;
  };

  const uint8_t baseConfigId = vprHost.workflowState().configComplete.configId;
  BleCsControllerCreateConfig altConfig = hostConfig.session.workflow.createConfig;
  altConfig.configId = static_cast<uint8_t>(baseConfigId + 1U);
  altConfig.rttType = 0U;
  altConfig.minMainModeSteps = 4U;
  altConfig.maxMainModeSteps = 4U;
  memset(altConfig.channelMap, 0, sizeof(altConfig.channelMap));
  for (size_t i = 0U; i < sizeof(kAltChannels) / sizeof(kAltChannels[0]); ++i) {
    const uint8_t channel = kAltChannels[i];
    altConfig.channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
  }

  BleCsProcedureParameters baseParams = hostConfig.session.workflow.procedureParameters;
  baseParams.maxProcedureCount = 1U;
  baseParams.maxProcedureLen = 16U;
  baseParams.minSubeventLen = 0x000100UL;
  baseParams.maxSubeventLen = 0x000100UL;

  BleCsProcedureParameters altParams = baseParams;
  altParams.configId = altConfig.configId;

  uint8_t initPolls = 0U;
  uint8_t createPolls = 0U;
  uint8_t securityPolls = 0U;
  uint8_t armPolls = 0U;
  uint8_t selectBasePolls = 0U;
  uint8_t selectAltPolls = 0U;
  uint8_t createStatus = 0xFFU;
  uint8_t securityStatus = 0xFFU;
  uint8_t setAltArmStatus = 0xFFU;
  uint8_t setAltStatus = 0xFFU;
  uint8_t setBaseStatus = 0xFFU;
  uint8_t setAltAgainStatus = 0xFFU;

  auto makeRetainedStateExpectation =
      [&](uint8_t activeConfigId, uint8_t slot0ConfigId, uint8_t slot1ConfigId,
          uint8_t previousConfigId, uint8_t activePrimarySlotIndex,
          uint8_t freePrimarySlotCount, uint8_t storedConfigCount,
          bool selectedRunnable, bool slot0Runnable, bool slot1Runnable,
          bool previousRunnable, bool selectedSecurityEnabled,
          bool slot0SecurityEnabled, bool slot1SecurityEnabled,
          bool previousSecurityEnabled, bool selectedProcedureParamsApplied,
          bool slot0ProcedureParamsApplied, bool slot1ProcedureParamsApplied,
          bool previousProcedureParamsApplied)
          -> BleCsControllerVprRetainedStateExpectation {
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
    expected.readiness.slot0ProcedureParamsApplied =
        slot0ProcedureParamsApplied;
    expected.readiness.slot1ProcedureParamsApplied =
        slot1ProcedureParamsApplied;
    expected.readiness.previousProcedureParamsApplied =
        previousProcedureParamsApplied;
    expected.checkRunnability = true;
    expected.checkReadiness = true;
    return expected;
  };

  auto pollUntilState =
      [&](const BleCsControllerVprRetainedStateExpectation& expected,
          uint8_t* outPolls) -> bool {
    return vprHost.pollUntilRetainedState(expected, 32U, outPolls);
  };

  auto packAuthority = [&](const BleCsControllerVprHostState& state) -> uint32_t {
    return state.retainedConfigAuthorityWord();
  };

#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  auto pollForSummary = [&](uint8_t maxPolls, uint8_t* outPolls) -> bool {
    if (outPolls != nullptr) {
      *outPolls = 0U;
    }
    while (!vprHost.failed()) {
      if (outPolls != nullptr && *outPolls >= maxPolls) {
        break;
      }
      delay(20U);
      if (!vprHost.poll()) {
        return false;
      }
      if (outPolls != nullptr) {
        *outPolls = static_cast<uint8_t>(*outPolls + 1U);
      }
    }
    return !vprHost.failed();
  };
#endif

  ok = ok &&
       pollUntilState(makeRetainedStateExpectation(baseConfigId, baseConfigId,
                                                   0U, 0U, 0U, 1U, 1U,
                                                   true, true, false, false,
                                                   true, true, false, false,
                                                   true, true, false, false),
                      &initPolls);
  const BleCsControllerVprHostState initialState = vprHost.vprState();
#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  gVprSelectDemoSummary.stage = kSelectStageInitial;
  gVprSelectDemoSummary.initialAuthority = packAuthority(initialState);
#if defined(NRF54L15_CS_VPR_AUTO_SELECT_DEMO)
  for (uint8_t settlePolls = 0U; ok && !vprHost.failed() && settlePolls < 60U;
       ++settlePolls) {
    delay(20U);
    ok = vprHost.poll();
  }
#endif
#endif

  ok = ok && sendDirectCreate(altConfig, &createStatus);
#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  ok = ok && pollForSummary(16U, &createPolls);
#else
  while (ok && !vprHost.failed() && createPolls < 24U) {
    const bool created =
        createStatus == 0U &&
        vprHost.vprState().linkConfigId == altConfig.configId &&
        vprHost.vprState().linkStoredConfigCount == 2U &&
        vprHost.workflowState().configComplete.configId == altConfig.configId;
    if (created) {
      break;
    }
    ok = vprHost.poll();
    ++createPolls;
  }
  ok = ok &&
       pollUntilState(makeRetainedStateExpectation(
                          altConfig.configId, baseConfigId, altConfig.configId,
                          baseConfigId, 1U, 0U, 2U, false, true, false, true,
                          false, true, false, true, false, true, false, true),
                      nullptr);
#endif
  const BleCsControllerVprHostState createdState = vprHost.vprState();
#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  gVprSelectDemoSummary.stage = kSelectStageCreated;
  gVprSelectDemoSummary.createdAuthority = packAuthority(createdState);
#endif

  ok = ok && sendDirectSecurity(&securityStatus);
#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  ok = ok && pollForSummary(16U, &securityPolls);
#else
  ok = ok &&
       pollUntilState(makeRetainedStateExpectation(
                          altConfig.configId, baseConfigId, altConfig.configId,
                          baseConfigId, 1U, 0U, 2U, false, true, false, true,
                          true, true, true, true, false, true, false, true),
                      &securityPolls);
#endif
  const BleCsControllerVprHostState securedState = vprHost.vprState();
#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  gVprSelectDemoSummary.stage = kSelectStageSecured;
  gVprSelectDemoSummary.securedAuthority = packAuthority(securedState);
#endif
  ok = ok && sendDirectSetProc(altParams, &setAltArmStatus);
#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  ok = ok && pollForSummary(16U, &armPolls);
#else
  ok = ok &&
       pollUntilState(makeRetainedStateExpectation(
                          altConfig.configId, baseConfigId, altConfig.configId,
                          baseConfigId, 1U, 0U, 2U, true, true, true, true,
                          true, true, true, true, true, true, true, true),
                      &armPolls);
#endif
  const BleCsControllerVprHostState armedState = vprHost.vprState();
#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  gVprSelectDemoSummary.stage = kSelectStageArmed;
  gVprSelectDemoSummary.armedAuthority = packAuthority(armedState);
#endif

  setAltStatus = setAltArmStatus;
  ok = ok && sendDirectSetProc(baseParams, &setBaseStatus);
#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  ok = ok && pollForSummary(16U, &selectBasePolls);
#else
  ok = ok &&
       pollUntilState(makeRetainedStateExpectation(
                          baseConfigId, baseConfigId, altConfig.configId,
                          altConfig.configId, 0U, 0U, 2U, true, true, true,
                          true, true, true, true, true, true, true, true,
                          true),
                      &selectBasePolls);
#endif
  const BleCsControllerVprHostState baseSelectedState = vprHost.vprState();
#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  gVprSelectDemoSummary.stage = kSelectStageBaseSelected;
  gVprSelectDemoSummary.baseSelectedAuthority = packAuthority(baseSelectedState);
#endif

  ok = ok && sendDirectSetProc(altParams, &setAltAgainStatus);
#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  ok = ok && pollForSummary(16U, &selectAltPolls);
#else
  ok = ok &&
       pollUntilState(makeRetainedStateExpectation(
                          altConfig.configId, baseConfigId, altConfig.configId,
                          baseConfigId, 1U, 0U, 2U, true, true, true, true,
                          true, true, true, true, true, true, true, true),
                      &selectAltPolls);
#endif
  const BleCsControllerVprHostState altSelectedState = vprHost.vprState();
#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  gVprSelectDemoSummary.stage = kSelectStageAltSelected;
  gVprSelectDemoSummary.altSelectedAuthority = packAuthority(altSelectedState);
#endif

  ok = ok && createStatus == 0U && securityStatus == 0U && setAltStatus == 0U &&
       setBaseStatus == 0U && setAltAgainStatus == 0U &&
       !vprHost.vprState().linkProcedureEnabled &&
       vprHost.vprState().linkProcedureParamsApplied &&
       initialState.retainedConfigMatchesAuthority(baseConfigId, 0U, 0U) &&
       createdState.retainedConfigMatchesAuthority(altConfig.configId,
                                                   baseConfigId, 0U) &&
       securedState.retainedConfigMatchesAuthority(altConfig.configId,
                                                   baseConfigId, 0U) &&
       armedState.retainedConfigMatchesAuthority(altConfig.configId,
                                                 baseConfigId, 0U) &&
       baseSelectedState.retainedConfigMatchesAuthority(baseConfigId,
                                                        altConfig.configId,
                                                        0U) &&
       altSelectedState.retainedConfigMatchesAuthority(altConfig.configId,
                                                       baseConfigId, 0U);

#ifdef NRF54L15_CS_VPR_SELECT_SUMMARY
  gVprSelectDemoSummary.magic = kSelectSummaryMagic;
  gVprSelectDemoSummary.stage = kSelectStageDone;
  gVprSelectDemoSummary.ok = ok ? 1UL : 0UL;
  gVprSelectDemoSummary.pumpCount = pumpCount;
  gVprSelectDemoSummary.statusWord =
      ((uint32_t)createStatus) | ((uint32_t)securityStatus << 8U) |
      ((uint32_t)setAltArmStatus << 16U) | ((uint32_t)setBaseStatus << 24U);
  gVprSelectDemoSummary.initialAuthority = packAuthority(initialState);
  gVprSelectDemoSummary.createdAuthority = packAuthority(createdState);
  gVprSelectDemoSummary.securedAuthority = packAuthority(securedState);
  gVprSelectDemoSummary.armedAuthority = packAuthority(armedState);
  gVprSelectDemoSummary.baseSelectedAuthority = packAuthority(baseSelectedState);
  gVprSelectDemoSummary.altSelectedAuthority = packAuthority(altSelectedState);
#endif

  if (emitSerial) {
    Serial.print(F("hcivprselectdemo ok="));
    Serial.print(ok ? 1 : 0);
    Serial.print(F(" pumped="));
    Serial.print(pumpCount);
    Serial.print(F(" create=0x"));
    Serial.print(createStatus, HEX);
    Serial.print(F(" sec=0x"));
    Serial.print(securityStatus, HEX);
    Serial.print(F(" set=0x"));
    Serial.print(setAltArmStatus, HEX);
    Serial.print('/');
    Serial.print(setAltStatus, HEX);
    Serial.print('/');
    Serial.print(setBaseStatus, HEX);
    Serial.print('/');
    Serial.print(setAltAgainStatus, HEX);
    Serial.print(F(" polls="));
    Serial.print(initPolls);
    Serial.print('/');
    Serial.print(createPolls);
    Serial.print('/');
    Serial.print(securityPolls);
    Serial.print('/');
    Serial.print(armPolls);
    Serial.print('/');
    Serial.print(selectBasePolls);
    Serial.print('/');
    Serial.print(selectAltPolls);
    Serial.print(F(" slots="));
    Serial.print(initialState.linkSlot0ConfigId);
    Serial.print('/');
    Serial.print(initialState.linkSlot1ConfigId);
    Serial.print('/');
    Serial.print(initialState.linkPreviousConfigId);
    Serial.print('>');
    Serial.print(createdState.linkSlot0ConfigId);
    Serial.print('/');
    Serial.print(createdState.linkSlot1ConfigId);
    Serial.print('/');
    Serial.print(createdState.linkPreviousConfigId);
    Serial.print('>');
    Serial.print(securedState.linkSlot0ConfigId);
    Serial.print('/');
    Serial.print(securedState.linkSlot1ConfigId);
    Serial.print('/');
    Serial.print(securedState.linkPreviousConfigId);
    Serial.print('>');
    Serial.print(armedState.linkSlot0ConfigId);
    Serial.print('/');
    Serial.print(armedState.linkSlot1ConfigId);
    Serial.print('/');
    Serial.print(armedState.linkPreviousConfigId);
    Serial.print('>');
    Serial.print(baseSelectedState.linkSlot0ConfigId);
    Serial.print('/');
    Serial.print(baseSelectedState.linkSlot1ConfigId);
    Serial.print('/');
    Serial.print(baseSelectedState.linkPreviousConfigId);
    Serial.print('>');
    Serial.print(altSelectedState.linkSlot0ConfigId);
    Serial.print('/');
    Serial.print(altSelectedState.linkSlot1ConfigId);
    Serial.print('/');
    Serial.print(altSelectedState.linkPreviousConfigId);
    Serial.print(F(" active="));
    Serial.print(initialState.linkActivePrimarySlotIndex);
    Serial.print('>');
    Serial.print(createdState.linkActivePrimarySlotIndex);
    Serial.print('>');
    Serial.print(securedState.linkActivePrimarySlotIndex);
    Serial.print('>');
    Serial.print(armedState.linkActivePrimarySlotIndex);
    Serial.print('>');
    Serial.print(baseSelectedState.linkActivePrimarySlotIndex);
    Serial.print('>');
    Serial.print(altSelectedState.linkActivePrimarySlotIndex);
    Serial.print(F(" free="));
    Serial.print(initialState.linkFreePrimarySlotCount);
    Serial.print('>');
    Serial.print(createdState.linkFreePrimarySlotCount);
    Serial.print('>');
    Serial.print(securedState.linkFreePrimarySlotCount);
    Serial.print('>');
    Serial.print(armedState.linkFreePrimarySlotCount);
    Serial.print('>');
    Serial.print(baseSelectedState.linkFreePrimarySlotCount);
    Serial.print('>');
    Serial.print(altSelectedState.linkFreePrimarySlotCount);
    Serial.print(F(" count="));
    Serial.print(initialState.linkStoredConfigCount);
    Serial.print('>');
    Serial.print(createdState.linkStoredConfigCount);
    Serial.print('>');
    Serial.print(securedState.linkStoredConfigCount);
    Serial.print('>');
    Serial.print(armedState.linkStoredConfigCount);
    Serial.print('>');
    Serial.print(baseSelectedState.linkStoredConfigCount);
    Serial.print('>');
    Serial.print(altSelectedState.linkStoredConfigCount);
    Serial.print(F(" auth="));
    Serial.print(initialState.linkAuthority0ConfigId);
    Serial.print('/');
    Serial.print(initialState.linkAuthority1ConfigId);
    Serial.print('/');
    Serial.print(initialState.linkAuthority2ConfigId);
    Serial.print('>');
    Serial.print(createdState.linkAuthority0ConfigId);
    Serial.print('/');
    Serial.print(createdState.linkAuthority1ConfigId);
    Serial.print('/');
    Serial.print(createdState.linkAuthority2ConfigId);
    Serial.print('>');
    Serial.print(securedState.linkAuthority0ConfigId);
    Serial.print('/');
    Serial.print(securedState.linkAuthority1ConfigId);
    Serial.print('/');
    Serial.print(securedState.linkAuthority2ConfigId);
    Serial.print('>');
    Serial.print(armedState.linkAuthority0ConfigId);
    Serial.print('/');
    Serial.print(armedState.linkAuthority1ConfigId);
    Serial.print('/');
    Serial.print(armedState.linkAuthority2ConfigId);
    Serial.print('>');
    Serial.print(baseSelectedState.linkAuthority0ConfigId);
    Serial.print('/');
    Serial.print(baseSelectedState.linkAuthority1ConfigId);
    Serial.print('/');
    Serial.print(baseSelectedState.linkAuthority2ConfigId);
    Serial.print('>');
    Serial.print(altSelectedState.linkAuthority0ConfigId);
    Serial.print('/');
    Serial.print(altSelectedState.linkAuthority1ConfigId);
    Serial.print('/');
    Serial.print(altSelectedState.linkAuthority2ConfigId);
    Serial.print(F(" cfg="));
    Serial.print(initialState.linkConfigId);
    Serial.print('>');
    Serial.print(createdState.linkConfigId);
    Serial.print('>');
    Serial.print(securedState.linkConfigId);
    Serial.print('>');
    Serial.print(armedState.linkConfigId);
    Serial.print('>');
    Serial.print(baseSelectedState.linkConfigId);
    Serial.print('>');
    Serial.print(altSelectedState.linkConfigId);
    Serial.print(F(" run="));
    Serial.print(initialState.linkSelectedConfigRunnable ? 1 : 0);
    Serial.print('/');
    Serial.print(createdState.linkSelectedConfigRunnable ? 1 : 0);
    Serial.print('/');
    Serial.print(securedState.linkSelectedConfigRunnable ? 1 : 0);
    Serial.print('/');
    Serial.print(armedState.linkSelectedConfigRunnable ? 1 : 0);
    Serial.print('/');
    Serial.print(baseSelectedState.linkSelectedConfigRunnable ? 1 : 0);
    Serial.print('/');
    Serial.print(altSelectedState.linkSelectedConfigRunnable ? 1 : 0);
  Serial.print(F(" slot_run="));
  Serial.print(initialState.linkSlot0Runnable ? 1 : 0);
  Serial.print(initialState.linkSlot1Runnable ? '1' : '0');
  Serial.print(initialState.linkPreviousSlotRunnable ? '1' : '0');
  Serial.print('>');
  Serial.print(createdState.linkSlot0Runnable ? 1 : 0);
  Serial.print(createdState.linkSlot1Runnable ? '1' : '0');
  Serial.print(createdState.linkPreviousSlotRunnable ? '1' : '0');
  Serial.print('>');
  Serial.print(securedState.linkSlot0Runnable ? 1 : 0);
  Serial.print(securedState.linkSlot1Runnable ? '1' : '0');
  Serial.print(securedState.linkPreviousSlotRunnable ? '1' : '0');
  Serial.print('>');
  Serial.print(armedState.linkSlot0Runnable ? 1 : 0);
  Serial.print(armedState.linkSlot1Runnable ? '1' : '0');
  Serial.print(armedState.linkPreviousSlotRunnable ? '1' : '0');
  Serial.print('>');
  Serial.print(baseSelectedState.linkSlot0Runnable ? 1 : 0);
  Serial.print(baseSelectedState.linkSlot1Runnable ? '1' : '0');
  Serial.print(baseSelectedState.linkPreviousSlotRunnable ? '1' : '0');
  Serial.print('>');
  Serial.print(altSelectedState.linkSlot0Runnable ? 1 : 0);
  Serial.print(altSelectedState.linkSlot1Runnable ? '1' : '0');
  Serial.print(altSelectedState.linkPreviousSlotRunnable ? '1' : '0');
  Serial.print(F(" sec="));
  Serial.print(initialState.linkSelectedConfigSecurityEnabled ? 1 : 0);
  Serial.print('/');
  Serial.print(createdState.linkSelectedConfigSecurityEnabled ? 1 : 0);
  Serial.print('/');
  Serial.print(securedState.linkSelectedConfigSecurityEnabled ? 1 : 0);
  Serial.print('/');
  Serial.print(armedState.linkSelectedConfigSecurityEnabled ? 1 : 0);
  Serial.print('/');
  Serial.print(baseSelectedState.linkSelectedConfigSecurityEnabled ? 1 : 0);
  Serial.print('/');
  Serial.print(altSelectedState.linkSelectedConfigSecurityEnabled ? 1 : 0);
  Serial.print(F(" slot_sec="));
  Serial.print(initialState.linkSlot0SecurityEnabled ? 1 : 0);
  Serial.print(initialState.linkSlot1SecurityEnabled ? '1' : '0');
  Serial.print(initialState.linkPreviousSlotSecurityEnabled ? '1' : '0');
  Serial.print('>');
  Serial.print(createdState.linkSlot0SecurityEnabled ? 1 : 0);
  Serial.print(createdState.linkSlot1SecurityEnabled ? '1' : '0');
  Serial.print(createdState.linkPreviousSlotSecurityEnabled ? '1' : '0');
  Serial.print('>');
  Serial.print(securedState.linkSlot0SecurityEnabled ? 1 : 0);
  Serial.print(securedState.linkSlot1SecurityEnabled ? '1' : '0');
  Serial.print(securedState.linkPreviousSlotSecurityEnabled ? '1' : '0');
  Serial.print('>');
  Serial.print(armedState.linkSlot0SecurityEnabled ? 1 : 0);
  Serial.print(armedState.linkSlot1SecurityEnabled ? '1' : '0');
  Serial.print(armedState.linkPreviousSlotSecurityEnabled ? '1' : '0');
  Serial.print('>');
  Serial.print(baseSelectedState.linkSlot0SecurityEnabled ? 1 : 0);
  Serial.print(baseSelectedState.linkSlot1SecurityEnabled ? '1' : '0');
  Serial.print(baseSelectedState.linkPreviousSlotSecurityEnabled ? '1' : '0');
  Serial.print('>');
  Serial.print(altSelectedState.linkSlot0SecurityEnabled ? 1 : 0);
  Serial.print(altSelectedState.linkSlot1SecurityEnabled ? '1' : '0');
  Serial.print(altSelectedState.linkPreviousSlotSecurityEnabled ? '1' : '0');
  Serial.print(F(" pp="));
  Serial.print(initialState.linkSelectedConfigProcedureParamsApplied ? 1 : 0);
  Serial.print('/');
  Serial.print(createdState.linkSelectedConfigProcedureParamsApplied ? 1 : 0);
  Serial.print('/');
  Serial.print(securedState.linkSelectedConfigProcedureParamsApplied ? 1 : 0);
  Serial.print('/');
  Serial.print(armedState.linkSelectedConfigProcedureParamsApplied ? 1 : 0);
  Serial.print('/');
  Serial.print(baseSelectedState.linkSelectedConfigProcedureParamsApplied ? 1 : 0);
  Serial.print('/');
  Serial.print(altSelectedState.linkSelectedConfigProcedureParamsApplied ? 1 : 0);
  Serial.print(F(" slot_pp="));
  Serial.print(initialState.linkSlot0ProcedureParamsApplied ? 1 : 0);
  Serial.print(initialState.linkSlot1ProcedureParamsApplied ? '1' : '0');
  Serial.print(initialState.linkPreviousSlotProcedureParamsApplied ? '1' : '0');
  Serial.print('>');
  Serial.print(createdState.linkSlot0ProcedureParamsApplied ? 1 : 0);
  Serial.print(createdState.linkSlot1ProcedureParamsApplied ? '1' : '0');
  Serial.print(createdState.linkPreviousSlotProcedureParamsApplied ? '1' : '0');
  Serial.print('>');
  Serial.print(securedState.linkSlot0ProcedureParamsApplied ? 1 : 0);
  Serial.print(securedState.linkSlot1ProcedureParamsApplied ? '1' : '0');
  Serial.print(securedState.linkPreviousSlotProcedureParamsApplied ? '1' : '0');
  Serial.print('>');
  Serial.print(armedState.linkSlot0ProcedureParamsApplied ? 1 : 0);
  Serial.print(armedState.linkSlot1ProcedureParamsApplied ? '1' : '0');
  Serial.print(armedState.linkPreviousSlotProcedureParamsApplied ? '1' : '0');
  Serial.print('>');
  Serial.print(baseSelectedState.linkSlot0ProcedureParamsApplied ? 1 : 0);
  Serial.print(baseSelectedState.linkSlot1ProcedureParamsApplied ? '1' : '0');
  Serial.print(baseSelectedState.linkPreviousSlotProcedureParamsApplied ? '1' : '0');
  Serial.print('>');
  Serial.print(altSelectedState.linkSlot0ProcedureParamsApplied ? 1 : 0);
  Serial.print(altSelectedState.linkSlot1ProcedureParamsApplied ? '1' : '0');
  Serial.print(altSelectedState.linkPreviousSlotProcedureParamsApplied ? '1' : '0');
  Serial.print(F(" flags="));
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" prev_used="));
  Serial.print(initialState.linkPreviousSlotInUse ? 1 : 0);
  Serial.print('>');
  Serial.print(createdState.linkPreviousSlotInUse ? 1 : 0);
  Serial.print('>');
  Serial.print(securedState.linkPreviousSlotInUse ? 1 : 0);
  Serial.print('>');
  Serial.print(armedState.linkPreviousSlotInUse ? 1 : 0);
  Serial.print('>');
  Serial.print(baseSelectedState.linkPreviousSlotInUse ? 1 : 0);
  Serial.print('>');
  Serial.print(altSelectedState.linkPreviousSlotInUse ? 1 : 0);
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(vprHost.workflowState().phase));
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" dist_m="));
  Serial.println(F("na"));
  }
}

void printHciVprThirdConfigDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t kAltChannels[] = {6U, 18U, 30U, 39U};
  static const uint8_t kThirdChannels[] = {4U, 12U, 20U, 28U, 36U};

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  hostConfig.session.workflow.procedureEnable.enable = 0U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 16U;
  hostConfig.session.workflow.procedureParameters.minSubeventLen = 0x000100UL;
  hostConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000100UL;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready() && !vprHost.failed();

  auto sendDirectCreate = [&](const BleCsControllerCreateConfig& config,
                              uint8_t* outStatus) -> bool {
    return vprHost.directCreateConfig(config, outStatus);
  };

  auto sendDirectSecurity = [&](uint8_t* outStatus) -> bool {
    return vprHost.directSecurityEnable(outStatus);
  };

  auto sendDirectSetProc = [&](const BleCsProcedureParameters& params,
                               uint8_t* outStatus) -> bool {
    return vprHost.directSetProcedureParameters(params, outStatus);
  };

  auto sendDirectEnable = [&](uint8_t configId, uint8_t enable,
                              uint8_t* outStatus) -> bool {
    return vprHost.directProcedureEnable(configId, enable != 0U, outStatus);
  };

  auto makeRetainedSelectionExpectation =
      [&](uint8_t activeConfigId, uint8_t slot0ConfigId, uint8_t slot1ConfigId,
          uint8_t previousConfigId, uint8_t storedConfigCount,
          bool selectedRunnable, bool previousRunnable)
          -> BleCsControllerVprRetainedSelectionExpectation {
    BleCsControllerVprRetainedSelectionExpectation expected{};
    expected.activeConfigId = activeConfigId;
    expected.slot0ConfigId = slot0ConfigId;
    expected.slot1ConfigId = slot1ConfigId;
    expected.previousConfigId = previousConfigId;
    expected.storedConfigCount = storedConfigCount;
    expected.selectedRunnable = selectedRunnable;
    expected.previousRunnable = previousRunnable;
    return expected;
  };

  auto pollUntilState =
      [&](const BleCsControllerVprRetainedSelectionExpectation& expected,
          uint8_t* outPolls) -> bool {
    return vprHost.pollUntilRetainedSelectionState(expected, 32U, outPolls);
  };

  auto pollUntilStoppedOnConfig = [&](uint8_t targetConfigId,
                                      uint8_t* outPolls) -> bool {
    return vprHost.pollUntilStoppedOnConfig(targetConfigId, 96U, outPolls);
  };

  const uint8_t baseConfigId = vprHost.workflowState().configComplete.configId;
  BleCsControllerCreateConfig altConfig = hostConfig.session.workflow.createConfig;
  altConfig.configId = static_cast<uint8_t>(baseConfigId + 1U);
  altConfig.rttType = 0U;
  altConfig.minMainModeSteps = 4U;
  altConfig.maxMainModeSteps = 4U;
  memset(altConfig.channelMap, 0, sizeof(altConfig.channelMap));
  for (size_t i = 0U; i < sizeof(kAltChannels) / sizeof(kAltChannels[0]); ++i) {
    const uint8_t channel = kAltChannels[i];
    altConfig.channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
  }

  BleCsControllerCreateConfig thirdConfig = hostConfig.session.workflow.createConfig;
  thirdConfig.configId = static_cast<uint8_t>(baseConfigId + 2U);
  thirdConfig.rttType = 0U;
  thirdConfig.minMainModeSteps = 5U;
  thirdConfig.maxMainModeSteps = 5U;
  memset(thirdConfig.channelMap, 0, sizeof(thirdConfig.channelMap));
  for (size_t i = 0U; i < sizeof(kThirdChannels) / sizeof(kThirdChannels[0]); ++i) {
    const uint8_t channel = kThirdChannels[i];
    thirdConfig.channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
  }

  BleCsProcedureParameters baseParams = hostConfig.session.workflow.procedureParameters;
  baseParams.maxProcedureCount = 1U;
  baseParams.maxProcedureLen = 16U;
  baseParams.minSubeventLen = 0x000100UL;
  baseParams.maxSubeventLen = 0x000100UL;

  BleCsProcedureParameters altParams = baseParams;
  altParams.configId = altConfig.configId;

  BleCsProcedureParameters thirdParams = baseParams;
  thirdParams.configId = thirdConfig.configId;
  thirdParams.maxProcedureLen = 18U;

  uint8_t initPolls = 0U;
  uint8_t altCreateStatus = 0xFFU;
  uint8_t altSecurityStatus = 0xFFU;
  uint8_t altSetStatus = 0xFFU;
  uint8_t baseSelectStatus = 0xFFU;
  uint8_t thirdCreateStatus = 0xFFU;
  uint8_t thirdSecurityStatus = 0xFFU;
  uint8_t thirdSetStatus = 0xFFU;
  uint8_t altSelectStatus = 0xFFU;
  uint8_t thirdSelectStatus = 0xFFU;
  uint8_t thirdRunStatus = 0xFFU;
  uint8_t baseRunStatus = 0xFFU;
  uint8_t altCreatePolls = 0U;
  uint8_t altSecurityPolls = 0U;
  uint8_t altSetPolls = 0U;
  uint8_t baseSelectPolls = 0U;
  uint8_t thirdCreatePolls = 0U;
  uint8_t thirdSecurityPolls = 0U;
  uint8_t thirdSetPolls = 0U;
  uint8_t altSelectPolls = 0U;
  uint8_t thirdSelectPolls = 0U;
  uint8_t thirdRunPolls = 0U;
  uint8_t baseRunPolls = 0U;

  ok = ok && pollUntilState(makeRetainedSelectionExpectation(
                                baseConfigId, baseConfigId, 0U, 0U, 1U, true,
                                false),
                            &initPolls);
  const BleCsControllerVprHostState initialState = vprHost.vprState();

  ok = ok && sendDirectCreate(altConfig, &altCreateStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          altConfig.configId, baseConfigId, altConfig.configId,
                          0U, 2U, false, false),
                      &altCreatePolls);
  ok = ok && sendDirectSecurity(&altSecurityStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          altConfig.configId, baseConfigId, altConfig.configId,
                          0U, 2U, false, false),
                      &altSecurityPolls);
  ok = ok && sendDirectSetProc(altParams, &altSetStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          altConfig.configId, baseConfigId, altConfig.configId,
                          0U, 2U, true, false),
                      &altSetPolls);
  const BleCsControllerVprHostState altReadyState = vprHost.vprState();

  ok = ok && sendDirectSetProc(baseParams, &baseSelectStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          baseConfigId, baseConfigId, altConfig.configId, 0U,
                          2U, true, false),
                      &baseSelectPolls);
  const BleCsControllerVprHostState baseSelectedState = vprHost.vprState();

  ok = ok && sendDirectCreate(thirdConfig, &thirdCreateStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          thirdConfig.configId, baseConfigId,
                          altConfig.configId, thirdConfig.configId, 3U, false,
                          false),
                      &thirdCreatePolls);
  ok = ok && sendDirectSecurity(&thirdSecurityStatus);
  ok = ok && thirdSecurityStatus == 0U;
  ok = ok && sendDirectSetProc(thirdParams, &thirdSetStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          thirdConfig.configId, thirdConfig.configId,
                          altConfig.configId, baseConfigId, 3U, true, true),
                      &thirdSetPolls);
  const BleCsControllerVprHostState thirdReadyState = vprHost.vprState();

  ok = ok && sendDirectSetProc(altParams, &altSelectStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          altConfig.configId, thirdConfig.configId,
                          altConfig.configId, baseConfigId, 3U, true, true),
                      &altSelectPolls);
  const BleCsControllerVprHostState altSelectedState = vprHost.vprState();

  ok = ok && sendDirectSetProc(thirdParams, &thirdSelectStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          thirdConfig.configId, thirdConfig.configId,
                          altConfig.configId, baseConfigId, 3U, true, true),
                      &thirdSelectPolls);
  const BleCsControllerVprHostState thirdSelectedState = vprHost.vprState();

  ok = ok && sendDirectEnable(thirdConfig.configId, 1U, &thirdRunStatus);
  ok = ok && pollUntilStoppedOnConfig(thirdConfig.configId, &thirdRunPolls);
  const BleCsSubeventResult thirdLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult thirdPeer = vprHost.completedPeerResult();
  const StepModeCollectContext thirdLocalModes = collectStepModes(thirdLocal);
  const StepModeCollectContext thirdPeerModes = collectStepModes(thirdPeer);
  const bool thirdRunOk =
      thirdRunStatus == 0U && thirdLocal.header.configId == thirdConfig.configId &&
      thirdPeer.header.configId == thirdConfig.configId &&
      thirdLocalModes.mode1Count == 0U && thirdPeerModes.mode1Count == 0U &&
      thirdLocalModes.mode2Count == 5U && thirdPeerModes.mode2Count == 5U;
  ok = ok && thirdRunOk;

  ok = ok && sendDirectEnable(baseConfigId, 1U, &baseRunStatus);
  ok = ok && pollUntilStoppedOnConfig(baseConfigId, &baseRunPolls);
  const BleCsSubeventResult baseLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult basePeer = vprHost.completedPeerResult();
  const StepModeCollectContext baseLocalModes = collectStepModes(baseLocal);
  const StepModeCollectContext basePeerModes = collectStepModes(basePeer);
  const bool baseRunOk =
      baseRunStatus == 0U && baseLocal.header.configId == baseConfigId &&
      basePeer.header.configId == baseConfigId &&
      baseLocalModes.mode2Count >= 3U && basePeerModes.mode2Count >= 3U &&
      vprHost.vprState().linkPreviousConfigId == thirdConfig.configId &&
      vprHost.vprState().linkStoredConfigCount == 3U;
  ok = ok && baseRunOk;

  Serial.print(F("hcivprthirdcfgdemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" st=0x"));
  Serial.print(altCreateStatus, HEX);
  Serial.print('/');
  Serial.print(altSecurityStatus, HEX);
  Serial.print('/');
  Serial.print(altSetStatus, HEX);
  Serial.print('/');
  Serial.print(baseSelectStatus, HEX);
  Serial.print('/');
  Serial.print(thirdCreateStatus, HEX);
  Serial.print('/');
  Serial.print(thirdSecurityStatus, HEX);
  Serial.print('/');
  Serial.print(thirdSetStatus, HEX);
  Serial.print('/');
  Serial.print(altSelectStatus, HEX);
  Serial.print('/');
  Serial.print(thirdSelectStatus, HEX);
  Serial.print('/');
  Serial.print(thirdRunStatus, HEX);
  Serial.print('/');
  Serial.print(baseRunStatus, HEX);
  Serial.print(F(" polls="));
  Serial.print(initPolls);
  Serial.print('/');
  Serial.print(altCreatePolls);
  Serial.print('/');
  Serial.print(altSecurityPolls);
  Serial.print('/');
  Serial.print(altSetPolls);
  Serial.print('/');
  Serial.print(baseSelectPolls);
  Serial.print('/');
  Serial.print(thirdCreatePolls);
  Serial.print('/');
  Serial.print(thirdSecurityPolls);
  Serial.print('/');
  Serial.print(thirdSetPolls);
  Serial.print('/');
  Serial.print(altSelectPolls);
  Serial.print('/');
  Serial.print(thirdSelectPolls);
  Serial.print('/');
  Serial.print(thirdRunPolls);
  Serial.print('/');
  Serial.print(baseRunPolls);
  Serial.print(F(" slots="));
  Serial.print(initialState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(initialState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(initialState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(altReadyState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(altReadyState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(altReadyState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(baseSelectedState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(baseSelectedState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(baseSelectedState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(thirdReadyState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(thirdReadyState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(thirdReadyState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(altSelectedState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(altSelectedState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(altSelectedState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(thirdSelectedState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(thirdSelectedState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(thirdSelectedState.linkPreviousConfigId);
  Serial.print(F(" active="));
  Serial.print(initialState.linkConfigId);
  Serial.print('>');
  Serial.print(altReadyState.linkConfigId);
  Serial.print('>');
  Serial.print(baseSelectedState.linkConfigId);
  Serial.print('>');
  Serial.print(thirdReadyState.linkConfigId);
  Serial.print('>');
  Serial.print(altSelectedState.linkConfigId);
  Serial.print('>');
  Serial.print(thirdSelectedState.linkConfigId);
  Serial.print('>');
  Serial.print(vprHost.vprState().linkConfigId);
  Serial.print(F(" count="));
  Serial.print(initialState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(altReadyState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(baseSelectedState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(thirdReadyState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(altSelectedState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(thirdSelectedState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(vprHost.vprState().linkStoredConfigCount);
  Serial.print(F(" run="));
  Serial.print(initialState.linkSelectedConfigRunnable ? 1 : 0);
  Serial.print('/');
  Serial.print(altReadyState.linkSelectedConfigRunnable ? 1 : 0);
  Serial.print('/');
  Serial.print(baseSelectedState.linkSelectedConfigRunnable ? 1 : 0);
  Serial.print('/');
  Serial.print(thirdReadyState.linkSelectedConfigRunnable ? 1 : 0);
  Serial.print('/');
  Serial.print(altSelectedState.linkSelectedConfigRunnable ? 1 : 0);
  Serial.print('/');
  Serial.print(thirdSelectedState.linkSelectedConfigRunnable ? 1 : 0);
  Serial.print('/');
  Serial.print(vprHost.vprState().linkSelectedConfigRunnable ? 1 : 0);
  Serial.print(F(" prev_run="));
  Serial.print(initialState.linkPreviousSlotRunnable ? 1 : 0);
  Serial.print('>');
  Serial.print(altReadyState.linkPreviousSlotRunnable ? 1 : 0);
  Serial.print('>');
  Serial.print(baseSelectedState.linkPreviousSlotRunnable ? 1 : 0);
  Serial.print('>');
  Serial.print(thirdReadyState.linkPreviousSlotRunnable ? 1 : 0);
  Serial.print('>');
  Serial.print(altSelectedState.linkPreviousSlotRunnable ? 1 : 0);
  Serial.print('>');
  Serial.print(thirdSelectedState.linkPreviousSlotRunnable ? 1 : 0);
  Serial.print('>');
  Serial.print(vprHost.vprState().linkPreviousSlotRunnable ? 1 : 0);
  Serial.print(F(" step3="));
  Serial.print(thirdLocalModes.mode1Count);
  Serial.print('+');
  Serial.print(thirdLocalModes.mode2Count);
  Serial.print('/');
  Serial.print(thirdPeerModes.mode1Count);
  Serial.print('+');
  Serial.print(thirdPeerModes.mode2Count);
  Serial.print(F(" step1="));
  Serial.print(baseLocalModes.mode1Count);
  Serial.print('+');
  Serial.print(baseLocalModes.mode2Count);
  Serial.print('/');
  Serial.print(basePeerModes.mode1Count);
  Serial.print('+');
  Serial.print(basePeerModes.mode2Count);
  Serial.print(F(" flags="));
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprEvictDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t kAltChannels[] = {6U, 18U, 30U, 39U};
  static const uint8_t kThirdChannels[] = {4U, 12U, 20U, 28U, 36U};
  static const uint8_t kFourthChannels[] = {1U, 9U, 17U, 25U, 33U, 37U};

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  hostConfig.session.workflow.procedureEnable.enable = 0U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 16U;
  hostConfig.session.workflow.procedureParameters.minSubeventLen = 0x000100UL;
  hostConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000100UL;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready() && !vprHost.failed();

  auto sendDirectCreate = [&](const BleCsControllerCreateConfig& config,
                              uint8_t* outStatus) -> bool {
    return vprHost.directCreateConfig(config, outStatus);
  };

  auto sendDirectSecurity = [&](uint8_t* outStatus) -> bool {
    return vprHost.directSecurityEnable(outStatus);
  };

  auto sendDirectSetProc = [&](const BleCsProcedureParameters& params,
                               uint8_t* outStatus) -> bool {
    return vprHost.directSetProcedureParameters(params, outStatus);
  };

  auto sendDirectEnable = [&](uint8_t configId, uint8_t enable,
                              uint8_t* outStatus) -> bool {
    return vprHost.directProcedureEnable(configId, enable != 0U, outStatus);
  };

  auto makeRetainedSelectionExpectation =
      [&](uint8_t activeConfigId, uint8_t slot0ConfigId, uint8_t slot1ConfigId,
          uint8_t previousConfigId, uint8_t storedConfigCount,
          bool selectedRunnable, bool previousRunnable,
          uint8_t lastEvictedConfigId)
          -> BleCsControllerVprRetainedSelectionExpectation {
    BleCsControllerVprRetainedSelectionExpectation expected{};
    expected.activeConfigId = activeConfigId;
    expected.slot0ConfigId = slot0ConfigId;
    expected.slot1ConfigId = slot1ConfigId;
    expected.previousConfigId = previousConfigId;
    expected.storedConfigCount = storedConfigCount;
    expected.selectedRunnable = selectedRunnable;
    expected.previousRunnable = previousRunnable;
    expected.lastEvictedConfigId = lastEvictedConfigId;
    expected.checkLastEvictedConfigId = true;
    return expected;
  };

  auto pollUntilState =
      [&](const BleCsControllerVprRetainedSelectionExpectation& expected,
          uint8_t* outPolls) -> bool {
    return vprHost.pollUntilRetainedSelectionState(expected, 32U, outPolls);
  };

  auto pollUntilStoppedOnConfig = [&](uint8_t targetConfigId,
                                      uint8_t* outPolls) -> bool {
    return vprHost.pollUntilStoppedOnConfig(targetConfigId, 96U, outPolls);
  };

  const uint8_t baseConfigId = vprHost.workflowState().configComplete.configId;
  BleCsControllerCreateConfig altConfig = hostConfig.session.workflow.createConfig;
  altConfig.configId = static_cast<uint8_t>(baseConfigId + 1U);
  altConfig.rttType = 0U;
  altConfig.minMainModeSteps = 4U;
  altConfig.maxMainModeSteps = 4U;
  memset(altConfig.channelMap, 0, sizeof(altConfig.channelMap));
  for (size_t i = 0U; i < sizeof(kAltChannels) / sizeof(kAltChannels[0]); ++i) {
    const uint8_t channel = kAltChannels[i];
    altConfig.channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
  }

  BleCsControllerCreateConfig thirdConfig = hostConfig.session.workflow.createConfig;
  thirdConfig.configId = static_cast<uint8_t>(baseConfigId + 2U);
  thirdConfig.rttType = 0U;
  thirdConfig.minMainModeSteps = 5U;
  thirdConfig.maxMainModeSteps = 5U;
  memset(thirdConfig.channelMap, 0, sizeof(thirdConfig.channelMap));
  for (size_t i = 0U; i < sizeof(kThirdChannels) / sizeof(kThirdChannels[0]); ++i) {
    const uint8_t channel = kThirdChannels[i];
    thirdConfig.channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
  }

  BleCsControllerCreateConfig fourthConfig = hostConfig.session.workflow.createConfig;
  fourthConfig.configId = static_cast<uint8_t>(baseConfigId + 3U);
  fourthConfig.rttType = 0U;
  fourthConfig.minMainModeSteps = 6U;
  fourthConfig.maxMainModeSteps = 6U;
  memset(fourthConfig.channelMap, 0, sizeof(fourthConfig.channelMap));
  for (size_t i = 0U; i < sizeof(kFourthChannels) / sizeof(kFourthChannels[0]); ++i) {
    const uint8_t channel = kFourthChannels[i];
    fourthConfig.channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
  }

  BleCsProcedureParameters baseParams = hostConfig.session.workflow.procedureParameters;
  baseParams.maxProcedureCount = 1U;
  baseParams.maxProcedureLen = 16U;
  baseParams.minSubeventLen = 0x000100UL;
  baseParams.maxSubeventLen = 0x000100UL;

  BleCsProcedureParameters altParams = baseParams;
  altParams.configId = altConfig.configId;

  BleCsProcedureParameters thirdParams = baseParams;
  thirdParams.configId = thirdConfig.configId;
  thirdParams.maxProcedureLen = 18U;

  BleCsProcedureParameters fourthParams = baseParams;
  fourthParams.configId = fourthConfig.configId;
  fourthParams.maxProcedureLen = 20U;

  uint8_t initPolls = 0U;
  uint8_t altCreateStatus = 0xFFU;
  uint8_t altSecurityStatus = 0xFFU;
  uint8_t altSetStatus = 0xFFU;
  uint8_t baseSelectStatus = 0xFFU;
  uint8_t thirdCreateStatus = 0xFFU;
  uint8_t thirdSecurityStatus = 0xFFU;
  uint8_t thirdSetStatus = 0xFFU;
  uint8_t baseReselectStatus = 0xFFU;
  uint8_t fourthCreateStatus = 0xFFU;
  uint8_t fourthSecurityStatus = 0xFFU;
  uint8_t fourthSetStatus = 0xFFU;
  uint8_t thirdSelectStatus = 0xFFU;
  uint8_t fourthRunStatus = 0xFFU;
  uint8_t altCreatePolls = 0U;
  uint8_t altSecurityPolls = 0U;
  uint8_t altSetPolls = 0U;
  uint8_t baseSelectPolls = 0U;
  uint8_t thirdCreatePolls = 0U;
  uint8_t thirdSecurityPolls = 0U;
  uint8_t thirdSetPolls = 0U;
  uint8_t baseReselectPolls = 0U;
  uint8_t fourthCreatePolls = 0U;
  uint8_t fourthSecurityPolls = 0U;
  uint8_t fourthSetPolls = 0U;
  uint8_t fourthRunPolls = 0U;

  ok = ok && pollUntilState(makeRetainedSelectionExpectation(
                                baseConfigId, baseConfigId, 0U, 0U, 1U, true,
                                false, 0U),
                            &initPolls);
  const BleCsControllerVprHostState initialState = vprHost.vprState();

  ok = ok && sendDirectCreate(altConfig, &altCreateStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          altConfig.configId, baseConfigId, altConfig.configId,
                          0U, 2U, false, false, 0U),
                      &altCreatePolls);
  ok = ok && sendDirectSecurity(&altSecurityStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          altConfig.configId, baseConfigId, altConfig.configId,
                          0U, 2U, false, false, 0U),
                      &altSecurityPolls);
  ok = ok && sendDirectSetProc(altParams, &altSetStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          altConfig.configId, baseConfigId, altConfig.configId,
                          0U, 2U, true, false, 0U),
                      &altSetPolls);
  const BleCsControllerVprHostState altReadyState = vprHost.vprState();

  ok = ok && sendDirectSetProc(baseParams, &baseSelectStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          baseConfigId, baseConfigId, altConfig.configId, 0U,
                          2U, true, false, 0U),
                      &baseSelectPolls);

  ok = ok && sendDirectCreate(thirdConfig, &thirdCreateStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          thirdConfig.configId, baseConfigId,
                          altConfig.configId, thirdConfig.configId, 3U, false,
                          false, 0U),
                      &thirdCreatePolls);
  ok = ok && sendDirectSecurity(&thirdSecurityStatus);
  ok = ok && thirdSecurityStatus == 0U;
  ok = ok && sendDirectSetProc(thirdParams, &thirdSetStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          thirdConfig.configId, thirdConfig.configId,
                          altConfig.configId, baseConfigId, 3U, true, true,
                          0U),
                      &thirdSetPolls);
  const BleCsControllerVprHostState thirdReadyState = vprHost.vprState();

  ok = ok && sendDirectSetProc(baseParams, &baseReselectStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          baseConfigId, baseConfigId, altConfig.configId,
                          thirdConfig.configId, 3U, true, true, 0U),
                      &baseReselectPolls);
  const BleCsControllerVprHostState baseReselectedState = vprHost.vprState();

  ok = ok && sendDirectCreate(fourthConfig, &fourthCreateStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          fourthConfig.configId, baseConfigId,
                          altConfig.configId, fourthConfig.configId, 3U,
                          false, false, thirdConfig.configId),
                      &fourthCreatePolls);
  const BleCsControllerVprHostState fourthCreatedState = vprHost.vprState();

  ok = ok && sendDirectSecurity(&fourthSecurityStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          fourthConfig.configId, baseConfigId,
                          altConfig.configId, fourthConfig.configId, 3U,
                          false, false, thirdConfig.configId),
                      &fourthSecurityPolls);
  ok = ok && sendDirectSetProc(fourthParams, &fourthSetStatus);
  ok = ok &&
       pollUntilState(makeRetainedSelectionExpectation(
                          fourthConfig.configId, fourthConfig.configId,
                          altConfig.configId, baseConfigId, 3U, true, true,
                          thirdConfig.configId),
                      &fourthSetPolls);
  const BleCsControllerVprHostState fourthReadyState = vprHost.vprState();

  ok = ok && sendDirectSetProc(thirdParams, &thirdSelectStatus);
  ok = ok && thirdSelectStatus == 0x12U;
  ok = ok && vprHost.vprState().linkConfigId == fourthConfig.configId &&
       vprHost.vprState().linkPreviousConfigId == baseConfigId &&
       vprHost.vprState().linkLastEvictedConfigId == thirdConfig.configId;

  ok = ok && sendDirectEnable(fourthConfig.configId, 1U, &fourthRunStatus);
  ok = ok && pollUntilStoppedOnConfig(fourthConfig.configId, &fourthRunPolls);
  const BleCsSubeventResult fourthLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult fourthPeer = vprHost.completedPeerResult();
  const StepModeCollectContext fourthLocalModes = collectStepModes(fourthLocal);
  const StepModeCollectContext fourthPeerModes = collectStepModes(fourthPeer);
  const bool fourthRunOk =
      fourthRunStatus == 0U &&
      fourthLocal.header.configId == fourthConfig.configId &&
      fourthPeer.header.configId == fourthConfig.configId &&
      fourthLocalModes.mode1Count == 0U && fourthPeerModes.mode1Count == 0U &&
      fourthLocalModes.mode2Count == 6U && fourthPeerModes.mode2Count == 6U &&
      vprHost.vprState().linkStoredConfigCount == 3U &&
      vprHost.vprState().linkLastEvictedConfigId == thirdConfig.configId;
  ok = ok && fourthRunOk;

  Serial.print(F("hcivprevictdemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" st=0x"));
  Serial.print(altCreateStatus, HEX);
  Serial.print('/');
  Serial.print(altSecurityStatus, HEX);
  Serial.print('/');
  Serial.print(altSetStatus, HEX);
  Serial.print('/');
  Serial.print(baseSelectStatus, HEX);
  Serial.print('/');
  Serial.print(thirdCreateStatus, HEX);
  Serial.print('/');
  Serial.print(thirdSecurityStatus, HEX);
  Serial.print('/');
  Serial.print(thirdSetStatus, HEX);
  Serial.print('/');
  Serial.print(baseReselectStatus, HEX);
  Serial.print('/');
  Serial.print(fourthCreateStatus, HEX);
  Serial.print('/');
  Serial.print(fourthSecurityStatus, HEX);
  Serial.print('/');
  Serial.print(fourthSetStatus, HEX);
  Serial.print('/');
  Serial.print(thirdSelectStatus, HEX);
  Serial.print('/');
  Serial.print(fourthRunStatus, HEX);
  Serial.print(F(" polls="));
  Serial.print(initPolls);
  Serial.print('/');
  Serial.print(altCreatePolls);
  Serial.print('/');
  Serial.print(altSecurityPolls);
  Serial.print('/');
  Serial.print(altSetPolls);
  Serial.print('/');
  Serial.print(baseSelectPolls);
  Serial.print('/');
  Serial.print(thirdCreatePolls);
  Serial.print('/');
  Serial.print(thirdSecurityPolls);
  Serial.print('/');
  Serial.print(thirdSetPolls);
  Serial.print('/');
  Serial.print(baseReselectPolls);
  Serial.print('/');
  Serial.print(fourthCreatePolls);
  Serial.print('/');
  Serial.print(fourthSecurityPolls);
  Serial.print('/');
  Serial.print(fourthSetPolls);
  Serial.print('/');
  Serial.print(fourthRunPolls);
  Serial.print(F(" slots="));
  Serial.print(initialState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(initialState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(initialState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(altReadyState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(altReadyState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(altReadyState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(thirdReadyState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(thirdReadyState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(thirdReadyState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(baseReselectedState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(baseReselectedState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(baseReselectedState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(fourthCreatedState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(fourthCreatedState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(fourthCreatedState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(fourthReadyState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(fourthReadyState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(fourthReadyState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(vprHost.vprState().linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(vprHost.vprState().linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(vprHost.vprState().linkPreviousConfigId);
  Serial.print(F(" active="));
  Serial.print(initialState.linkConfigId);
  Serial.print('>');
  Serial.print(altReadyState.linkConfigId);
  Serial.print('>');
  Serial.print(thirdReadyState.linkConfigId);
  Serial.print('>');
  Serial.print(baseReselectedState.linkConfigId);
  Serial.print('>');
  Serial.print(fourthCreatedState.linkConfigId);
  Serial.print('>');
  Serial.print(fourthReadyState.linkConfigId);
  Serial.print('>');
  Serial.print(vprHost.vprState().linkConfigId);
  Serial.print(F(" count="));
  Serial.print(initialState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(altReadyState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(thirdReadyState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(baseReselectedState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(fourthCreatedState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(fourthReadyState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(vprHost.vprState().linkStoredConfigCount);
  Serial.print(F(" evict="));
  Serial.print(initialState.linkLastEvictedConfigId);
  Serial.print('>');
  Serial.print(altReadyState.linkLastEvictedConfigId);
  Serial.print('>');
  Serial.print(thirdReadyState.linkLastEvictedConfigId);
  Serial.print('>');
  Serial.print(baseReselectedState.linkLastEvictedConfigId);
  Serial.print('>');
  Serial.print(fourthCreatedState.linkLastEvictedConfigId);
  Serial.print('>');
  Serial.print(fourthReadyState.linkLastEvictedConfigId);
  Serial.print('>');
  Serial.print(vprHost.vprState().linkLastEvictedConfigId);
  Serial.print(F(" run4="));
  Serial.print(fourthLocalModes.mode1Count);
  Serial.print('+');
  Serial.print(fourthLocalModes.mode2Count);
  Serial.print('/');
  Serial.print(fourthPeerModes.mode1Count);
  Serial.print('+');
  Serial.print(fourthPeerModes.mode2Count);
  Serial.print(F(" flags="));
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprPromoteDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static const uint8_t kAltChannels[] = {6U, 18U, 30U, 39U};
  static const uint8_t kThirdChannels[] = {4U, 12U, 20U, 28U, 36U};

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  hostConfig.session.workflow.procedureEnable.enable = 0U;
  hostConfig.session.workflow.procedureParameters.maxProcedureCount = 1U;
  hostConfig.session.workflow.procedureParameters.maxProcedureLen = 16U;
  hostConfig.session.workflow.procedureParameters.minSubeventLen = 0x000100UL;
  hostConfig.session.workflow.procedureParameters.maxSubeventLen = 0x000100UL;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }
  ok = ok && vprHost.ready() && !vprHost.failed();

  auto sendDirectCreate = [&](const BleCsControllerCreateConfig& config,
                              uint8_t* outStatus) -> bool {
    return vprHost.directCreateConfig(config, outStatus);
  };

  auto sendDirectSecurity = [&](uint8_t* outStatus) -> bool {
    return vprHost.directSecurityEnable(outStatus);
  };

  auto sendDirectSetProc = [&](const BleCsProcedureParameters& params,
                               uint8_t* outStatus) -> bool {
    return vprHost.directSetProcedureParameters(params, outStatus);
  };

  auto sendDirectEnable = [&](uint8_t configId, uint8_t enable,
                              uint8_t* outStatus) -> bool {
    return vprHost.directProcedureEnable(configId, enable != 0U, outStatus);
  };

  auto pollUntilStoppedOnConfig = [&](uint8_t targetConfigId,
                                      uint8_t* outPolls) -> bool {
    return vprHost.pollUntilStoppedOnConfig(targetConfigId, 96U, outPolls);
  };

  const uint8_t baseConfigId = vprHost.workflowState().configComplete.configId;
  BleCsControllerCreateConfig altConfig = hostConfig.session.workflow.createConfig;
  altConfig.configId = static_cast<uint8_t>(baseConfigId + 1U);
  altConfig.rttType = 0U;
  altConfig.minMainModeSteps = 4U;
  altConfig.maxMainModeSteps = 4U;
  memset(altConfig.channelMap, 0, sizeof(altConfig.channelMap));
  for (size_t i = 0U; i < sizeof(kAltChannels) / sizeof(kAltChannels[0]); ++i) {
    const uint8_t channel = kAltChannels[i];
    altConfig.channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
  }

  BleCsControllerCreateConfig thirdConfig = hostConfig.session.workflow.createConfig;
  thirdConfig.configId = static_cast<uint8_t>(baseConfigId + 2U);
  thirdConfig.rttType = 0U;
  thirdConfig.minMainModeSteps = 5U;
  thirdConfig.maxMainModeSteps = 5U;
  memset(thirdConfig.channelMap, 0, sizeof(thirdConfig.channelMap));
  for (size_t i = 0U; i < sizeof(kThirdChannels) / sizeof(kThirdChannels[0]); ++i) {
    const uint8_t channel = kThirdChannels[i];
    thirdConfig.channelMap[channel >> 3U] |= static_cast<uint8_t>(1U << (channel & 0x07U));
  }

  BleCsProcedureParameters baseParams = hostConfig.session.workflow.procedureParameters;
  baseParams.maxProcedureCount = 1U;
  baseParams.maxProcedureLen = 16U;
  baseParams.minSubeventLen = 0x000100UL;
  baseParams.maxSubeventLen = 0x000100UL;

  BleCsProcedureParameters altParams = baseParams;
  altParams.configId = altConfig.configId;

  BleCsProcedureParameters thirdParams = baseParams;
  thirdParams.configId = thirdConfig.configId;
  thirdParams.maxProcedureLen = 18U;

  uint8_t altCreateStatus = 0xFFU;
  uint8_t altSecurityStatus = 0xFFU;
  uint8_t altSetStatus = 0xFFU;
  uint8_t baseSelectStatus = 0xFFU;
  uint8_t thirdCreateStatus = 0xFFU;
  uint8_t thirdSecurityStatus = 0xFFU;
  uint8_t thirdSetStatus = 0xFFU;
  uint8_t thirdRunStatus = 0xFFU;
  uint8_t baseRunStatus = 0xFFU;
  uint8_t thirdRunPolls = 0U;
  uint8_t baseRunPolls = 0U;

  ok = ok && sendDirectCreate(altConfig, &altCreateStatus);
  ok = ok && sendDirectSecurity(&altSecurityStatus);
  ok = ok && sendDirectSetProc(altParams, &altSetStatus);
  ok = ok && sendDirectSetProc(baseParams, &baseSelectStatus);
  ok = ok && sendDirectCreate(thirdConfig, &thirdCreateStatus);
  ok = ok && sendDirectSecurity(&thirdSecurityStatus);
  ok = ok && sendDirectSetProc(thirdParams, &thirdSetStatus);

  const BleCsControllerVprHostState thirdReadyState = vprHost.vprState();

  ok = ok && sendDirectEnable(thirdConfig.configId, 1U, &thirdRunStatus);
  ok = ok && pollUntilStoppedOnConfig(thirdConfig.configId, &thirdRunPolls);
  const BleCsControllerVprHostState thirdRunState = vprHost.vprState();
  const BleCsSubeventResult thirdLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult thirdPeer = vprHost.completedPeerResult();
  const StepModeCollectContext thirdLocalModes = collectStepModes(thirdLocal);
  const StepModeCollectContext thirdPeerModes = collectStepModes(thirdPeer);

  ok = ok && sendDirectEnable(baseConfigId, 1U, &baseRunStatus);
  ok = ok && pollUntilStoppedOnConfig(baseConfigId, &baseRunPolls);
  const BleCsControllerVprHostState baseRunState = vprHost.vprState();
  const BleCsSubeventResult baseLocal = vprHost.completedLocalResult();
  const BleCsSubeventResult basePeer = vprHost.completedPeerResult();
  const StepModeCollectContext baseLocalModes = collectStepModes(baseLocal);
  const StepModeCollectContext basePeerModes = collectStepModes(basePeer);

  const bool thirdReadyOk =
      thirdReadyState.linkConfigId == thirdConfig.configId &&
      thirdReadyState.linkSlot0ConfigId == thirdConfig.configId &&
      thirdReadyState.linkSlot1ConfigId == altConfig.configId &&
      thirdReadyState.linkPreviousConfigId == baseConfigId &&
      thirdReadyState.linkActivePrimarySlotIndex == 0U &&
      thirdReadyState.linkStoredConfigCount == 3U &&
      thirdReadyState.linkSelectedConfigRunnable &&
      thirdReadyState.linkPreviousSlotRunnable &&
      thirdReadyState.linkLastEvictedConfigId == 0U;

  const bool thirdRunOk =
      thirdRunStatus == 0U &&
      thirdRunState.linkConfigId == thirdConfig.configId &&
      thirdRunState.linkSlot0ConfigId == thirdConfig.configId &&
      thirdRunState.linkSlot1ConfigId == altConfig.configId &&
      thirdRunState.linkPreviousConfigId == baseConfigId &&
      thirdRunState.linkActivePrimarySlotIndex == 0U &&
      thirdRunState.linkStoredConfigCount == 3U &&
      thirdRunState.linkSelectedConfigRunnable &&
      thirdRunState.linkPreviousSlotRunnable &&
      thirdRunState.linkLastEvictedConfigId == 0U &&
      thirdLocal.header.configId == thirdConfig.configId &&
      thirdPeer.header.configId == thirdConfig.configId &&
      thirdLocalModes.mode1Count == 0U && thirdPeerModes.mode1Count == 0U &&
      thirdLocalModes.mode2Count == 5U && thirdPeerModes.mode2Count == 5U;

  const bool baseRunOk =
      baseRunStatus == 0U &&
      baseRunState.linkConfigId == baseConfigId &&
      baseRunState.linkSlot0ConfigId == baseConfigId &&
      baseRunState.linkSlot1ConfigId == altConfig.configId &&
      baseRunState.linkPreviousConfigId == thirdConfig.configId &&
      baseRunState.linkActivePrimarySlotIndex == 0U &&
      baseRunState.linkStoredConfigCount == 3U &&
      baseRunState.linkSelectedConfigRunnable &&
      baseRunState.linkPreviousSlotRunnable &&
      baseRunState.linkLastEvictedConfigId == 0U &&
      baseLocal.header.configId == baseConfigId &&
      basePeer.header.configId == baseConfigId &&
      baseLocalModes.mode2Count >= 3U && basePeerModes.mode2Count >= 3U;

  ok = ok && altCreateStatus == 0U && altSecurityStatus == 0U &&
       altSetStatus == 0U && baseSelectStatus == 0U &&
       thirdCreateStatus == 0U && thirdSecurityStatus == 0U &&
       thirdSetStatus == 0U && thirdReadyOk && thirdRunOk && baseRunOk;

  Serial.print(F("hcivprpromotedemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print(F(" st=0x"));
  Serial.print(altCreateStatus, HEX);
  Serial.print('/');
  Serial.print(altSecurityStatus, HEX);
  Serial.print('/');
  Serial.print(altSetStatus, HEX);
  Serial.print('/');
  Serial.print(baseSelectStatus, HEX);
  Serial.print('/');
  Serial.print(thirdCreateStatus, HEX);
  Serial.print('/');
  Serial.print(thirdSecurityStatus, HEX);
  Serial.print('/');
  Serial.print(thirdSetStatus, HEX);
  Serial.print('/');
  Serial.print(thirdRunStatus, HEX);
  Serial.print('/');
  Serial.print(baseRunStatus, HEX);
  Serial.print(F(" polls="));
  Serial.print(thirdRunPolls);
  Serial.print('/');
  Serial.print(baseRunPolls);
  Serial.print(F(" slots="));
  Serial.print(thirdReadyState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(thirdReadyState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(thirdReadyState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(thirdRunState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(thirdRunState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(thirdRunState.linkPreviousConfigId);
  Serial.print('>');
  Serial.print(baseRunState.linkSlot0ConfigId);
  Serial.print('/');
  Serial.print(baseRunState.linkSlot1ConfigId);
  Serial.print('/');
  Serial.print(baseRunState.linkPreviousConfigId);
  Serial.print(F(" active="));
  Serial.print(thirdReadyState.linkConfigId);
  Serial.print('>');
  Serial.print(thirdRunState.linkConfigId);
  Serial.print('>');
  Serial.print(baseRunState.linkConfigId);
  Serial.print(F(" pri="));
  Serial.print(thirdReadyState.linkActivePrimarySlotIndex);
  Serial.print('>');
  Serial.print(thirdRunState.linkActivePrimarySlotIndex);
  Serial.print('>');
  Serial.print(baseRunState.linkActivePrimarySlotIndex);
  Serial.print(F(" count="));
  Serial.print(thirdReadyState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(thirdRunState.linkStoredConfigCount);
  Serial.print('>');
  Serial.print(baseRunState.linkStoredConfigCount);
  Serial.print(F(" evict="));
  Serial.print(thirdReadyState.linkLastEvictedConfigId);
  Serial.print('>');
  Serial.print(thirdRunState.linkLastEvictedConfigId);
  Serial.print('>');
  Serial.print(baseRunState.linkLastEvictedConfigId);
  Serial.print(F(" run3="));
  Serial.print(thirdLocalModes.mode1Count);
  Serial.print('+');
  Serial.print(thirdLocalModes.mode2Count);
  Serial.print('/');
  Serial.print(thirdPeerModes.mode1Count);
  Serial.print('+');
  Serial.print(thirdPeerModes.mode2Count);
  Serial.print(F(" run1="));
  Serial.print(baseLocalModes.mode1Count);
  Serial.print('+');
  Serial.print(baseLocalModes.mode2Count);
  Serial.print('/');
  Serial.print(basePeerModes.mode1Count);
  Serial.print('+');
  Serial.print(basePeerModes.mode2Count);
  Serial.print(F(" flags="));
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" dist_m="));
  if (vprHost.estimateValid()) {
    Serial.println(vprHost.sessionState().estimate.distanceMeters, 4);
  } else {
    Serial.println(F("na"));
  }
}

void printHciVprLinkDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;
  static constexpr uint16_t kWrongConnHandle = 0x0041U;

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);
  hostConfig.session.workflow.procedureEnable.enable = 0U;

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();
  ok = ok && vprHost.beginHost(kDemoConnHandle, hostConfig);

  uint8_t pumpCount = 0U;
  while (ok && !vprHost.ready() && !vprHost.failed() && pumpCount < 48U) {
    ok = vprHost.loopOnce();
    ++pumpCount;
  }

  ok = ok && vprHost.ready();

  const bool firstRefresh = ok && vprHost.refreshLinkSession();
  uint8_t wrongStatus = 0xFFU;
  bool wrongRejected = false;
  bool removed = false;
  bool closedRefresh = false;
  bool reopened = false;
  bool reopenedRefresh = false;
  uint8_t repumpCount = 0U;
  auto drainTransport = [&]() {
    while (vprHost.transport().available() > 0) {
      (void)vprHost.transport().read();
    }
  };

  if (ok && firstRefresh) {
    VprControllerServiceHost directHost(&vprHost.transport());
    drainTransport();
    BleCsHciCommand readCapsCommand{};
    ok = BleChannelSoundingRadio::buildHciReadRemoteSupportedCapabilitiesCommand(
        kWrongConnHandle, &readCapsCommand);
    if (ok) {
      uint8_t response[64];
      size_t responseLen = 0U;
      BleCsHciCommandStatusEvent statusEvent{};
      wrongRejected =
          directHost.sendHciCommand(readCapsCommand.opcode, readCapsCommand.payload,
                                    readCapsCommand.payloadLen, response,
                                    sizeof(response), &responseLen) &&
          BleChannelSoundingRadio::parseHciCommandStatusEvent(
              response, responseLen, &statusEvent) &&
          statusEvent.opcode == readCapsCommand.opcode && statusEvent.status != 0U;
      if (wrongRejected) {
        wrongStatus = statusEvent.status;
      }
    }

    drainTransport();
    BleCsHciCommand removeCommand{};
    ok = ok && BleChannelSoundingRadio::buildHciRemoveConfigCommand(
                   kDemoConnHandle, vprHost.workflowState().configComplete.configId,
                   &removeCommand);
    if (ok) {
      uint8_t response[64];
      size_t responseLen = 0U;
      BleCsHciCommandCompleteEvent complete{};
      removed = vprHost.sendDirectHciCommand(removeCommand.opcode, removeCommand.payload,
                                             removeCommand.payloadLen, response,
                                             sizeof(response), &responseLen) &&
                BleChannelSoundingRadio::parseHciCommandCompleteEvent(
                    response, responseLen, &complete) &&
                complete.opcode == removeCommand.opcode && complete.status == 0U;
    }
  }

  closedRefresh = removed && vprHost.refreshLinkSession() && !vprHost.vprState().linkSessionOpen;

  reopened = closedRefresh && vprHost.beginHost(kWrongConnHandle, hostConfig);
  while (reopened && !vprHost.ready() && !vprHost.failed() && repumpCount < 48U) {
    reopened = vprHost.loopOnce();
    ++repumpCount;
  }
  reopened = reopened && vprHost.ready();
  reopenedRefresh = reopened && vprHost.refreshLinkSession();

  ok = ok && firstRefresh && wrongRejected && removed && closedRefresh && reopened &&
       reopenedRefresh && vprHost.vprState().linkSessionOpen &&
       vprHost.vprState().linkConnHandle == kWrongConnHandle &&
       vprHost.vprState().linkConfigCreated &&
       vprHost.vprState().linkSecurityEnabled &&
       vprHost.vprState().linkProcedureParamsApplied &&
       !vprHost.vprState().linkProcedureEnabled;

  Serial.print(F("hcivprlinkdemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" wrong_status=0x"));
  Serial.print(wrongStatus, HEX);
  Serial.print(F(" wrong_reject="));
  Serial.print(wrongRejected ? 1 : 0);
  Serial.print(F(" removed="));
  Serial.print(removed ? 1 : 0);
  Serial.print(F(" closed="));
  Serial.print(closedRefresh ? 1 : 0);
  Serial.print(F(" reopened="));
  Serial.print(reopened ? 1 : 0);
  Serial.print(F(" refresh="));
  Serial.print(reopenedRefresh ? 1 : 0);
  Serial.print(F(" link_open="));
  Serial.print(vprHost.vprState().linkSessionOpen ? 1 : 0);
  Serial.print(F(" link_conn=0x"));
  Serial.print(vprHost.vprState().linkConnHandle, HEX);
  Serial.print(F(" cfg="));
  Serial.print(vprHost.vprState().linkConfigId);
  Serial.print(F(" flags="));
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" last=0x"));
  Serial.print(vprHost.workflowState().lastStatus, HEX);
  Serial.print(F(" proc="));
  Serial.print(vprHost.sessionState().completedProcedureCounter);
  Serial.print(F(" pumped="));
  Serial.print(pumpCount);
  Serial.print('/');
  Serial.print(repumpCount);
  Serial.print(F(" phase="));
  Serial.print(BleCsControllerWorkflow::phaseName(vprHost.workflowState().phase));
  Serial.print(F(" dist_m="));
  Serial.println(F("na"));
}

void printHciVprTraceDemo() {
  static constexpr uint16_t kDemoConnHandle = 0x0040U;

  BleCsControllerVprHost vprHost;
  BleCsControllerVprHostConfig hostConfig{};
  BleCsControllerVprHost::fillDemoConfig(&hostConfig);

  bool ok = vprHost.resetTransport(true);
  ok = ok && vprHost.loadDefaultTransportImage();
  ok = ok && vprHost.bootTransport();

  uint8_t remoteStatus = 0xFFU;
  uint8_t createStatus = 0xFFU;
  uint8_t securityStatus = 0xFFU;
  uint8_t setProcStatus = 0xFFU;
  uint8_t procEnableStatus = 0xFFU;
  uint8_t stateAfterRemote = 0U;
  uint8_t stateAfterCreate = 0U;
  uint8_t stateAfterSecurity = 0U;
  uint8_t stateAfterSetProc = 0U;
  uint8_t stateAfterProcEnable = 0U;
  uint8_t errAfterRemote = 0U;
  uint8_t errAfterCreate = 0U;
  uint8_t errAfterSecurity = 0U;
  uint8_t errAfterSetProc = 0U;
  uint8_t errAfterProcEnable = 0U;

  auto sendCommandStatus = [&](const BleCsHciCommand& command,
                               uint8_t* outStatus) -> bool {
    VprControllerServiceHost directHost(&vprHost.transport());
    uint8_t response[96];
    size_t responseLen = 0U;
    if (!directHost.sendHciCommand(command.opcode, command.payload, command.payloadLen,
                                   response, sizeof(response), &responseLen)) {
      return false;
    }
    BleCsHciCommandStatusEvent statusEvent{};
    if (BleChannelSoundingRadio::parseHciCommandStatusEvent(response, responseLen,
                                                            &statusEvent) &&
        statusEvent.opcode == command.opcode) {
      *outStatus = statusEvent.status;
      return true;
    }
    BleCsHciCommandCompleteEvent completeEvent{};
    if (BleChannelSoundingRadio::parseHciCommandCompleteEvent(response, responseLen,
                                                              &completeEvent) &&
        completeEvent.opcode == command.opcode) {
      *outStatus = completeEvent.status;
      return true;
    }
    return false;
  };
  auto drainControllerResidual = [&]() {
    while (vprHost.transport().available() > 0) {
      (void)vprHost.transport().read();
    }
  };
  auto encodeLinkState = [&]() -> uint8_t {
    return static_cast<uint8_t>((vprHost.vprState().linkSessionOpen ? 0x10U : 0U) |
                                (vprHost.vprState().linkConfigCreated ? 0x01U : 0U) |
                                (vprHost.vprState().linkSecurityEnabled ? 0x02U : 0U) |
                                (vprHost.vprState().linkProcedureParamsApplied ? 0x04U : 0U) |
                                (vprHost.vprState().linkProcedureEnabled ? 0x08U : 0U));
  };

  BleCsHciCommand command{};
  ok = ok && BleChannelSoundingRadio::buildHciReadRemoteSupportedCapabilitiesCommand(
                 kDemoConnHandle, &command);
  ok = ok && sendCommandStatus(command, &remoteStatus);
  (void)vprHost.refreshLinkSession();
  stateAfterRemote = encodeLinkState();
  errAfterRemote = static_cast<uint8_t>(vprHost.vprState().lastError & 0xFFU);
  drainControllerResidual();

  ok = ok && BleChannelSoundingRadio::buildHciCreateConfigCommand(
                 kDemoConnHandle, hostConfig.session.workflow.createConfig, &command);
  ok = ok && sendCommandStatus(command, &createStatus);
  (void)vprHost.refreshLinkSession();
  stateAfterCreate = encodeLinkState();
  errAfterCreate = static_cast<uint8_t>(vprHost.vprState().lastError & 0xFFU);
  drainControllerResidual();

  ok = ok && BleChannelSoundingRadio::buildHciSecurityEnableCommand(
                 kDemoConnHandle, &command);
  ok = ok && sendCommandStatus(command, &securityStatus);
  (void)vprHost.refreshLinkSession();
  stateAfterSecurity = encodeLinkState();
  errAfterSecurity = static_cast<uint8_t>(vprHost.vprState().lastError & 0xFFU);
  drainControllerResidual();

  ok = ok && BleChannelSoundingRadio::buildHciSetProcedureParametersCommand(
                 kDemoConnHandle, hostConfig.session.workflow.procedureParameters, &command);
  ok = ok && sendCommandStatus(command, &setProcStatus);
  (void)vprHost.refreshLinkSession();
  stateAfterSetProc = encodeLinkState();
  errAfterSetProc = static_cast<uint8_t>(vprHost.vprState().lastError & 0xFFU);
  drainControllerResidual();

  ok = ok && BleChannelSoundingRadio::buildHciProcedureEnableCommand(
                 kDemoConnHandle, hostConfig.session.workflow.procedureEnable, &command);
  ok = ok && sendCommandStatus(command, &procEnableStatus);
  (void)vprHost.refreshLinkSession();
  stateAfterProcEnable = encodeLinkState();
  errAfterProcEnable = static_cast<uint8_t>(vprHost.vprState().lastError & 0xFFU);
  drainControllerResidual();

  Serial.print(F("hcivprtracedemo ok="));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(" remote=0x"));
  Serial.print(remoteStatus, HEX);
  Serial.print(F(" create=0x"));
  Serial.print(createStatus, HEX);
  Serial.print(F(" security=0x"));
  Serial.print(securityStatus, HEX);
  Serial.print(F(" setproc=0x"));
  Serial.print(setProcStatus, HEX);
  Serial.print(F(" procen=0x"));
  Serial.print(procEnableStatus, HEX);
  Serial.print(F(" states=0x"));
  Serial.print(stateAfterRemote, HEX);
  Serial.print('/');
  Serial.print(stateAfterCreate, HEX);
  Serial.print('/');
  Serial.print(stateAfterSecurity, HEX);
  Serial.print('/');
  Serial.print(stateAfterSetProc, HEX);
  Serial.print('/');
  Serial.print(stateAfterProcEnable, HEX);
  Serial.print(F(" errs=0x"));
  Serial.print(errAfterRemote, HEX);
  Serial.print('/');
  Serial.print(errAfterCreate, HEX);
  Serial.print('/');
  Serial.print(errAfterSecurity, HEX);
  Serial.print('/');
  Serial.print(errAfterSetProc, HEX);
  Serial.print('/');
  Serial.print(errAfterProcEnable, HEX);
  Serial.print(F(" link_open="));
  Serial.print(vprHost.vprState().linkSessionOpen ? 1 : 0);
  Serial.print(F(" link_conn=0x"));
  Serial.print(vprHost.vprState().linkConnHandle, HEX);
  Serial.print(F(" flags="));
  Serial.print(vprHost.vprState().linkConfigCreated ? 'C' : '-');
  Serial.print(vprHost.vprState().linkSecurityEnabled ? 'S' : '-');
  Serial.print(vprHost.vprState().linkProcedureParamsApplied ? 'P' : '-');
  Serial.print(vprHost.vprState().linkProcedureEnabled ? 'E' : '-');
  Serial.print(F(" err=0x"));
  Serial.print(vprHost.vprState().lastError, HEX);
  Serial.println();
}

void handleCalibrationCommand(const char* command) {
  if (command == nullptr) {
    return;
  }

  while (*command == ' ' || *command == '\t') {
    ++command;
  }
  if (*command == '\0') {
    return;
  }

  if (strcmp(command, "status") == 0) {
    printCalibrationStatus();
    return;
  }

  if (strcmp(command, "raw") == 0) {
    printDfeStatus();
    return;
  }

  if (strcmp(command, "stepdemo") == 0) {
    printStepParserDemo();
    return;
  }

  if (strcmp(command, "stepestdemo") == 0) {
    printStepEstimateDemo();
    return;
  }

  if (strcmp(command, "hcidemo") == 0) {
    printHciEstimateDemo();
    return;
  }

  if (strcmp(command, "hcirttdemo") == 0) {
    printHciRttDemo();
    return;
  }

  if (strcmp(command, "hcipktdemo") == 0) {
    printHciPacketDemo();
    return;
  }

  if (strcmp(command, "hciworkflowdemo") == 0) {
    printHciWorkflowDemo();
    return;
  }

  if (strcmp(command, "hcih4demo") == 0) {
    printHciH4Demo();
    return;
  }

  if (strcmp(command, "hcisessiondemo") == 0) {
    printHciSessionDemo();
    return;
  }

  if (strcmp(command, "hcimixdemo") == 0) {
    printHciMixedStreamDemo();
    return;
  }

  if (strcmp(command, "hcihostdemo") == 0) {
    printHciHostDemo();
    return;
  }

  if (strcmp(command, "hcistreamdemo") == 0) {
    printHciStreamDemo();
    return;
  }

  if (strcmp(command, "hcivprtransportdemo") == 0) {
    printHciVprTransportDemo();
    return;
  }

  if (strcmp(command, "hcivprdumpdemo") == 0) {
    printHciVprDumpDemo();
    return;
  }

  if (strcmp(command, "hcivprrttoffdemo") == 0) {
    printHciVprRttOffDemo();
    return;
  }

  if (strcmp(command, "hcivprstatedemo") == 0) {
    printHciVprStateDemo();
    return;
  }

  if (strcmp(command, "hcivprmultidemo") == 0) {
    printHciVprMultiDemo();
    return;
  }

  if (strcmp(command, "hcivprchunkdemo") == 0) {
    printHciVprChunkDemo();
    return;
  }

  if (strcmp(command, "hcivprcontinuedemo") == 0) {
    printHciVprContinueDemo();
    return;
  }

  if (strcmp(command, "hcivprsubeventdemo") == 0) {
    printHciVprSubeventDemo();
    return;
  }

  if (strcmp(command, "hcivprmultisubdemo") == 0) {
    printHciVprMultiSubeventDemo();
    return;
  }

  if (strcmp(command, "hcivprsubcountdemo") == 0) {
    printHciVprSubcountDemo();
    return;
  }

  if (strcmp(command, "hcivprabortdemo") == 0) {
    printHciVprAbortDemo();
    return;
  }

  if (strcmp(command, "hcivprmanualdemo") == 0) {
    printHciVprManualDemo();
    return;
  }

  if (strcmp(command, "hcivprreconfigdemo") == 0) {
    printHciVprReconfigDemo();
    return;
  }

  if (strcmp(command, "hcivprcfgswapdemo") == 0) {
    printHciVprConfigSwapDemo();
    return;
  }

  if (strcmp(command, "hcivprmulticfgdemo") == 0) {
    printHciVprMultiConfigDemo();
    return;
  }

  if (strcmp(command, "hcivprrmstoredemo") == 0) {
    printHciVprStoredRemoveDemo();
    return;
  }

  if (strcmp(command, "hcivprrmactivedemo") == 0) {
    printHciVprActiveRemoveDemo();
    return;
  }

  if (strcmp(command, "hcivprinventorydemo") == 0) {
    printHciVprInventoryDemo();
    return;
  }

  if (strcmp(command, "hcivprslotdemo") == 0) {
    printHciVprSlotDemo();
    return;
  }

  if (strcmp(command, "hcivprselectdemo") == 0) {
    printHciVprSelectDemo();
    return;
  }

  if (strcmp(command, "hcivprthirdcfgdemo") == 0) {
    printHciVprThirdConfigDemo();
    return;
  }

  if (strcmp(command, "hcivprpromotedemo") == 0) {
    printHciVprPromoteDemo();
    return;
  }

  if (strcmp(command, "hcivprevictdemo") == 0) {
    printHciVprEvictDemo();
    return;
  }

  if (strcmp(command, "hcivprlinkdemo") == 0) {
    printHciVprLinkDemo();
    return;
  }

  if (strcmp(command, "hcivprtracedemo") == 0) {
    printHciVprTraceDemo();
    return;
  }

  if (strcmp(command, "clear") == 0) {
    gCalibrationScale = 1.0f;
    gCalibrationOffsetMeters = 0.0f;
    Serial.println(F("calibration=cleared"));
    printCalibrationStatus();
    return;
  }

  if (strcmp(command, "zero") == 0) {
    if (!gLastEstimateValid || !isfinite(gLastEstimate.phaseSlopeDistanceMeters)) {
      Serial.println(F("calibration=unavailable"));
      return;
    }

    gCalibrationOffsetMeters = -(gLastEstimate.phaseSlopeDistanceMeters * gCalibrationScale);
    Serial.println(F("calibration=zeroed"));
    printCalibrationStatus();
    return;
  }

  if (strncmp(command, "offset ", 7) == 0) {
    float offsetMeters = 0.0f;
    if (!parseCommandFloat(command + 7, &offsetMeters)) {
      Serial.println(F("calibration=invalid_offset"));
      return;
    }

    gCalibrationOffsetMeters = offsetMeters;
    printCalibrationStatus();
    return;
  }

  if (strncmp(command, "scale ", 6) == 0) {
    float scale = 0.0f;
    if (!parseCommandFloat(command + 6, &scale) || scale <= 0.0f) {
      Serial.println(F("calibration=invalid_scale"));
      return;
    }

    gCalibrationScale = scale;
    printCalibrationStatus();
    return;
  }

  if (strncmp(command, "ref ", 4) == 0) {
    float referenceMeters = 0.0f;
    if (!parseCommandFloat(command + 4, &referenceMeters)) {
      Serial.println(F("calibration=invalid_reference"));
      return;
    }
    if (!gLastEstimateValid || !isfinite(gLastEstimate.phaseSlopeDistanceMeters)) {
      Serial.println(F("calibration=unavailable"));
      return;
    }

    gCalibrationOffsetMeters =
        referenceMeters - (gLastEstimate.phaseSlopeDistanceMeters * gCalibrationScale);
    Serial.print(F("calibration=referenced reference_m="));
    Serial.println(referenceMeters, 4);
    printCalibrationStatus();
    return;
  }

  Serial.println(
      F("commands=status|raw|stepdemo|stepestdemo|hcidemo|hcirttdemo|hcipktdemo|hciworkflowdemo|hcih4demo|hcisessiondemo|hcimixdemo|hcihostdemo|hcistreamdemo|hcivprtransportdemo|hcivprdumpdemo|hcivprrttoffdemo|hcivprstatedemo|hcivprmultidemo|hcivprchunkdemo|hcivprcontinuedemo|hcivprsubeventdemo|hcivprmultisubdemo|hcivprsubcountdemo|hcivprabortdemo|hcivprmanualdemo|hcivprreconfigdemo|hcivprcfgswapdemo|hcivprmulticfgdemo|hcivprrmstoredemo|hcivprrmactivedemo|hcivprinventorydemo|hcivprslotdemo|hcivprselectdemo|hcivprthirdcfgdemo|hcivprpromotedemo|hcivprevictdemo|hcivprlinkdemo|hcivprtracedemo|clear|zero|ref <m>|offset <m>|scale <factor>"));
}

void pollSerialCommands() {
  while (Serial.available() > 0) {
    const int raw = Serial.read();
    if (raw < 0) {
      return;
    }

    const char ch = static_cast<char>(raw);
    if (ch == '\r' || ch == '\n') {
      if (gCommandLength > 0U) {
        gCommandBuffer[gCommandLength] = '\0';
        handleCalibrationCommand(gCommandBuffer);
        gCommandLength = 0U;
      }
      continue;
    }

    if (gCommandLength + 1U >= sizeof(gCommandBuffer)) {
      gCommandLength = 0U;
      Serial.println(F("calibration=command_too_long"));
      continue;
    }

    gCommandBuffer[gCommandLength++] = ch;
  }
}

long metersToCentimeters(float meters) {
  return lroundf(meters * 100.0f);
}

long metersToMillimeters(float meters) {
  return lroundf(meters * 1000.0f);
}

const BleCsChannelMeasurement* firstValidMeasurement() {
  for (uint8_t i = 0U; i < kSweepChannelCount; ++i) {
    if (gMeasurements[i].localTone.valid || gMeasurements[i].peerTone.valid ||
        gMeasurements[i].localRtt.valid || gMeasurements[i].peerRtt.valid) {
      return &gMeasurements[i];
    }
  }
  return nullptr;
}

uint8_t sweepChannelAt(uint8_t order) {
  const uint8_t center = kSweepChannelCount / 2U;
  if (order == 0U) {
    return center;
  }

  const uint8_t step = static_cast<uint8_t>((order + 1U) / 2U);
  if ((order & 0x1U) != 0U) {
    return static_cast<uint8_t>(center - step);
  }
  return static_cast<uint8_t>(center + step);
}

}  // namespace

void setup() {
  configureBoard();

  BleCsConfig config;
  config.txPowerDbm = -8;           // TX power for both control and probe packets.
  config.controlChannel = 37U;      // BLE advertising channel used for CS control messages.
  config.controlToProbeDelayUs = 2400U; // Time from control ack to first probe tone (us).
  config.probeToReportDelayUs = 1200U;  // Time from last probe to report message (us).
  config.probeRetries = 4U;         // How many times to retry a failed probe tone.
  config.probeListenWindowUs = 8000U;   // How long to listen for the reflector's tone (us).
  config.responseListenWindowUs = 12000U; // How long to listen for the reflector's response (us).
  config.maxPayloadLength = 32U;    // Keep the stable payload size on the raw-radio path.
  config.minToneMagnitude = 16U;    // Discard tones weaker than this (0–255 scale).
  config.enableRtt = false;         // Raw RADIO RTT AUXDATA is still experimental.
  config.enableRawDfeCapture = true; // Keep raw DFE packet bytes for bring-up and debug.

  // The raw-radio CS path is optional for the controller/VPR transport demos.
  // Keep the sketch interactive even if raw CS init fails on a given build.
  gCsReady = gCs.begin(config);

  Serial.println(F("CoreBleChannelSoundingInitiator start"));
  Serial.print(F("raw_cs_init="));
  Serial.println(gCsReady ? F("ok") : F("failed"));
  Serial.println(F("mode=phase_sounding"));
  Serial.println(F("distance_method=median_filtered_phase_slope"));
  Serial.println(F("rtt=controller_hci_decode_ready_raw_aux_disabled"));
  Serial.println(F("dfe_raw_capture=enabled"));
  Serial.println(F("control_channel=37"));
  Serial.println(F("pair_with=CoreBleChannelSoundingReflector"));
  Serial.println(F("commands=status|raw|stepdemo|stepestdemo|hcidemo|hcirttdemo|hcipktdemo|hciworkflowdemo|hcih4demo|hcisessiondemo|hcimixdemo|hcihostdemo|hcistreamdemo|hcivprtransportdemo|hcivprdumpdemo|hcivprrttoffdemo|hcivprstatedemo|hcivprmultidemo|hcivprchunkdemo|hcivprcontinuedemo|hcivprsubeventdemo|hcivprmultisubdemo|hcivprsubcountdemo|hcivprabortdemo|hcivprmanualdemo|hcivprreconfigdemo|hcivprcfgswapdemo|hcivprmulticfgdemo|hcivprrmstoredemo|hcivprrmactivedemo|hcivprinventorydemo|hcivprslotdemo|hcivprselectdemo|hcivprthirdcfgdemo|hcivprpromotedemo|hcivprevictdemo|hcivprlinkdemo|hcivprtracedemo|clear|zero|ref <m>|offset <m>|scale <factor>"));
  uint8_t csChannelMap[kBleCsChannelMapBytes] = {0};
  BleChannelSoundingRadio::fillValidChannelMap(csChannelMap);
  Serial.print(F("cs_chmap[0..2]="));
  Serial.print(csChannelMap[0], HEX);
  Serial.print(':');
  Serial.print(csChannelMap[1], HEX);
  Serial.print(':');
  Serial.println(csChannelMap[2], HEX);
  printStepParserDemo();
  printStepEstimateDemo();
  printHciEstimateDemo();
  printHciRttDemo();
  printHciPacketDemo();
  printHciWorkflowDemo();
  printHciH4Demo();
  printHciSessionDemo();
  printHciMixedStreamDemo();
  printHciHostDemo();
  printHciStreamDemo();
#ifdef NRF54L15_CS_VPR_AUTO_DEMO
  delay(1500U);
  printHciVprTransportDemo();
#endif
#ifdef NRF54L15_CS_VPR_AUTO_SELECT_DEMO
  delay(1500U);
  printHciVprSelectDemo();
#endif
  pulse(1U, 45U, 80U);
}

void loop() {
  pollSerialCommands();

  uint8_t validChannels = 0U;
  if (gCsReady) {
    // Sweep all 37 channels in a centre-out order (least frequency-selective
    // fading first) to improve estimate quality.
    for (uint8_t order = 0U; order < kSweepChannelCount; ++order) {
      const uint8_t ch = sweepChannelAt(order);
      BleCsChannelMeasurement measurement{};
      // measureChannel() exchanges one probe tone pair with the reflector on
      // channel ch. gSequence is a monotonically increasing sequence number
      // used by the reflector to reject duplicate probes.
      const bool ok = gCs.measureChannel(ch, gSequence++, &measurement);
      gMeasurements[ch] = measurement;
      gLastDfeInfo = gCs.lastDfeCaptureInfo();
      if (ok && measurement.valid) {
        ++validChannels;
      }
      delayMicroseconds(120U);  // Brief inter-channel guard time.
    }

    ++gSweepCount;
    gLastValidChannels = validChannels;
    BleCsEstimate estimate{};
    // estimateDistancePhaseSlope() fits a line to the unwrapped phase-vs-frequency
    // data across all channels. The slope gives the one-way time-of-flight, which
    // is converted to a distance in metres (speed of light / 2).
    gLastEstimateValid =
        BleChannelSoundingRadio::estimateDistancePhaseSlope(gMeasurements,
                                                            kSweepChannelCount,
                                                            &estimate);
    if (gLastEstimateValid) {
      gLastEstimate = estimate;
      ++gValidSweepCount;
      if (estimateAcceptableForDisplay(estimate)) {
        storeDistance(estimate.distanceMeters);
        ++gAcceptedSweepCount;
      }
      pulse(1U, 10U, 0U);
    }
  } else {
    gLastEstimateValid = false;
    gLastValidChannels = 0U;
    gLastDfeInfo = BleCsDfeCaptureInfo{};
  }

  const uint32_t now = millis();
  if ((now - gLastLogMs) >= 1000U) {
    gLastLogMs = now;
    Serial.print(F("t="));
    Serial.print(now);
    Serial.print(F(" sweep="));
    Serial.print(gSweepCount);
    Serial.print(F(" valid_sweeps="));
    Serial.print(gValidSweepCount);
    Serial.print(F(" accepted_sweeps="));
    Serial.print(gAcceptedSweepCount);
    Serial.print(F(" valid_channels="));
    Serial.print(gLastValidChannels);
    Serial.print(F(" raw_cs_ready="));
    Serial.print(gCsReady ? 1 : 0);
    if (gLastEstimateValid) {
      const float medianMeters = medianDistance();
      const bool displayAccepted = estimateAcceptableForDisplay(gLastEstimate);
      const float rawDisplayMeters = isfinite(medianMeters)
                                         ? medianMeters
                                         : (displayAccepted
                                                ? gLastEstimate.phaseSlopeDistanceMeters
                                                : NAN);
      const float displayMeters = applyCalibration(rawDisplayMeters);
      const float calibratedPhaseMeters =
          applyCalibration(gLastEstimate.phaseSlopeDistanceMeters);
      printDistanceField(F(" dist_m="), displayMeters);
      printDistanceIntegerField(F(" dist_cm="), displayMeters, false);
      printDistanceIntegerField(F(" dist_mm="), displayMeters, true);
      printDistanceField(F(" phase_raw_m="), gLastEstimate.phaseSlopeDistanceMeters);
      printDistanceField(F(" phase_cal_m="), calibratedPhaseMeters);
      Serial.print(F(" rtt_m="));
      Serial.print(F("na"));
      printDistanceField(F(" median_raw_m="), medianMeters);
      printDistanceField(F(" median_cal_m="), applyCalibration(medianMeters));
      Serial.print(F(" slope="));
      Serial.print(gLastEstimate.slopeRadPerHz, 8);
      Serial.print(F(" residual="));
      Serial.print(gLastEstimate.residualVariance, 6);
      Serial.print(F(" tone_quality="));
      Serial.print(gLastEstimate.medianToneQuality, 4);
      Serial.print(F(" tone_total="));
      Serial.print(gLastEstimate.totalToneChannels);
      Serial.print(F(" tone_used="));
      Serial.print(gLastEstimate.usedChannels);
      Serial.print(F(" reject_quality="));
      Serial.print(gLastEstimate.rejectedLowQualityChannels);
      Serial.print(F(" reject_residual="));
      Serial.print(gLastEstimate.rejectedResidualChannels);
      Serial.print(F(" fit_delta_m="));
      Serial.print(gLastEstimate.fitDeltaMeters, 4);
      Serial.print(F(" display_ok="));
      Serial.print(displayAccepted ? 1 : 0);
      Serial.print(F(" cal_offset_m="));
      Serial.print(gCalibrationOffsetMeters, 4);
      Serial.print(F(" cal_scale="));
      Serial.print(gCalibrationScale, 6);
      Serial.print(F(" rtt_channels="));
      Serial.print(0U);
      Serial.print(F(" rtt_var="));
      Serial.print(F("na"));
      Serial.print(F(" dfe_bytes="));
      Serial.print(gLastDfeInfo.amountBytes);
      Serial.print(F(" dfe_zero="));
      Serial.print(gLastDfeInfo.allZero ? 1 : 0);
    } else {
      Serial.print(F(" dist_m=na"));
    }

    const BleCsChannelMeasurement* sample = firstValidMeasurement();
    if (sample != nullptr) {
      Serial.print(F(" sample_ch="));
      Serial.print(sample->channelIndex);
      Serial.print(F(" li="));
      Serial.print(sample->localTone.i);
      Serial.print(F(" lq="));
      Serial.print(sample->localTone.q);
      Serial.print(F(" ri="));
      Serial.print(sample->peerTone.i);
      Serial.print(F(" rq="));
      Serial.print(sample->peerTone.q);
      Serial.print(F(" mag="));
      Serial.print(sample->localTone.magnitude);
      if (sample->localRtt.valid || sample->peerRtt.valid) {
        Serial.print(F(" lrtt="));
        Serial.print(sample->localRtt.timeDifferenceHalfNs);
        Serial.print(F(" lrp="));
        Serial.print(sample->localRtt.present ? 1 : 0);
        Serial.print(F(" lrv="));
        Serial.print(sample->localRtt.valid ? 1 : 0);
        Serial.print(F(" lrl="));
        Serial.print(sample->localRtt.rawLen);
        Serial.print(F(" laa="));
        Serial.print(sample->localRtt.aaCheckQuality);
        Serial.print(F(" prtt="));
        Serial.print(sample->peerRtt.timeDifferenceHalfNs);
        Serial.print(F(" prp="));
        Serial.print(sample->peerRtt.present ? 1 : 0);
        Serial.print(F(" prv="));
        Serial.print(sample->peerRtt.valid ? 1 : 0);
        Serial.print(F(" prl="));
        Serial.print(sample->peerRtt.rawLen);
        Serial.print(F(" paa="));
        Serial.print(sample->peerRtt.aaCheckQuality);
      } else if (sample->localRtt.present || sample->peerRtt.present) {
        printRttRawBytes(F(" lrtt_raw"), sample->localRtt);
        printRttRawBytes(F(" prtt_raw"), sample->peerRtt);
      }
    }
    Serial.println();
  }

  pollSerialCommands();
  delay(25U);
}
