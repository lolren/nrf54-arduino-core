#include <Arduino.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ble_channel_sounding.h"
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

static constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
static constexpr uint8_t kSweepChannelCount = 37U;
static constexpr uint8_t kMedianWindow = 5U;
static constexpr float kCalibrationScaleDefault = 1.0f;
static constexpr float kCalibrationOffsetMetersDefault = 0.0f;

static BleChannelSoundingRadio gCs;
static BleCsChannelMeasurement gMeasurements[kSweepChannelCount];
static BleCsEstimate gLastEstimate{};
static bool gLastEstimateValid = false;
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
      F("commands=status|clear|zero|ref <m>|offset <m>|scale <factor>"));
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
  config.txPowerDbm = -8;
  config.controlChannel = 37U;
  config.controlToProbeDelayUs = 2400U;
  config.probeToReportDelayUs = 1200U;
  config.probeRetries = 4U;
  config.probeListenWindowUs = 8000U;
  config.responseListenWindowUs = 12000U;
  config.minToneMagnitude = 16U;

  if (!gCs.begin(config)) {
    failStage(2);
  }

  Serial.println(F("CoreBleChannelSoundingInitiator start"));
  Serial.println(F("mode=phase_sounding"));
  Serial.println(F("distance_method=median_filtered_phase_slope"));
  Serial.println(F("rtt=disabled_clean_core"));
  Serial.println(F("control_channel=37"));
  Serial.println(F("pair_with=CoreBleChannelSoundingReflector"));
  Serial.println(F("commands=status|clear|zero|ref <m>|offset <m>|scale <factor>"));
  pulse(1U, 45U, 80U);
}

void loop() {
  pollSerialCommands();

  uint8_t validChannels = 0U;
  for (uint8_t order = 0U; order < kSweepChannelCount; ++order) {
    const uint8_t ch = sweepChannelAt(order);
    BleCsChannelMeasurement measurement{};
    const bool ok = gCs.measureChannel(ch, gSequence++, &measurement);
    gMeasurements[ch] = measurement;
    if (ok && measurement.valid) {
      ++validChannels;
    }
    delayMicroseconds(120U);
  }

  ++gSweepCount;
  gLastValidChannels = validChannels;
  BleCsEstimate estimate{};
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
      }
    }
    Serial.println();
  }

  pollSerialCommands();
  delay(25U);
}
