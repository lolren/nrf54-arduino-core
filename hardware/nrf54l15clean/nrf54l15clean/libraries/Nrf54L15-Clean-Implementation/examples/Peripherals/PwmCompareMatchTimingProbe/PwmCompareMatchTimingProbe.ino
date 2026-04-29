#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

struct CompareProbeCase {
  const char* name;
  bool (*configure)();
};

static constexpr Pin kPins4[] = {kPinD0, kPinD1, kPinD2, kPinD3};
static constexpr Pin kPins3[] = {kPinD0, kPinD1, kPinD2};
static constexpr uint32_t kPwmFrequencyHz = 2000UL;
static constexpr uint8_t kPeriodClearDppiChannel = 15U;
static constexpr uint8_t kCompare0CaptureDppiChannel = 16U;
static constexpr uint8_t kCompare1CaptureDppiChannel = 17U;
static constexpr uint8_t kCompare2CaptureDppiChannel = 18U;
static constexpr uint32_t kSettleMs = 20UL;

static Pwm g_pwm(nrf54l15::PWM20_BASE);
static Timer g_captureTimer(nrf54l15::TIMER21_BASE);
static Dppic g_dppic(nrf54l15::DPPIC20_BASE);
static uint16_t g_sequenceWords[4];

static bool configureCaptureTimer() {
  return g_captureTimer.begin(TimerBitWidth::k32bit, 0U, false);
}

static bool configureCaptureDppi() {
  return g_dppic.connect(g_pwm.publishPeriodEndConfigRegister(),
                         g_captureTimer.subscribeClearConfigRegister(),
                         kPeriodClearDppiChannel, true) &&
         g_dppic.connect(g_pwm.publishCompareMatchConfigRegister(0U),
                         g_captureTimer.subscribeCaptureConfigRegister(0U),
                         kCompare0CaptureDppiChannel, true) &&
         g_dppic.connect(g_pwm.publishCompareMatchConfigRegister(1U),
                         g_captureTimer.subscribeCaptureConfigRegister(1U),
                         kCompare1CaptureDppiChannel, true) &&
         g_dppic.connect(g_pwm.publishCompareMatchConfigRegister(2U),
                         g_captureTimer.subscribeCaptureConfigRegister(2U),
                         kCompare2CaptureDppiChannel, true);
}

static bool configureIndividualUp() {
  Pwm::ChannelConfig channels[4];
  channels[0] = {kPins4[0], 200U, true};
  channels[1] = {kPins4[1], 400U, true};
  channels[2] = {kPins4[2], 650U, true};
  channels[3] = {kPins4[3], 850U, true};
  return g_pwm.beginChannels(channels, 4U, kPwmFrequencyHz);
}

static bool configureRawIndividualUp() {
  if (!g_pwm.beginRaw(kPins4, 4U, kPwmFrequencyHz, Pwm::DecoderLoad::kIndividual,
                      Pwm::DecoderMode::kRefreshCount, 0U,
                      Pwm::CounterMode::kUp)) {
    return false;
  }

  const uint16_t top = g_pwm.countertop();
  g_sequenceWords[0] = Pwm::encodeSequenceWordPermille(200U, top, true);
  g_sequenceWords[1] = Pwm::encodeSequenceWordPermille(400U, top, true);
  g_sequenceWords[2] = Pwm::encodeSequenceWordPermille(650U, top, true);
  g_sequenceWords[3] = Pwm::encodeSequenceWordPermille(850U, top, true);
  return g_pwm.setSequence(0U, g_sequenceWords, 4U) &&
         g_pwm.setSequence(1U, g_sequenceWords, 4U);
}

static bool configureIndividualUpDown() {
  if (!g_pwm.beginRaw(kPins4, 4U, kPwmFrequencyHz, Pwm::DecoderLoad::kIndividual,
                      Pwm::DecoderMode::kRefreshCount, 0U,
                      Pwm::CounterMode::kUpDown)) {
    return false;
  }

  const uint16_t top = g_pwm.countertop();
  g_sequenceWords[0] = Pwm::encodeSequenceWordPermille(200U, top, true);
  g_sequenceWords[1] = Pwm::encodeSequenceWordPermille(400U, top, true);
  g_sequenceWords[2] = Pwm::encodeSequenceWordPermille(650U, top, true);
  g_sequenceWords[3] = Pwm::encodeSequenceWordPermille(850U, top, true);
  return g_pwm.setSequence(0U, g_sequenceWords, 4U) &&
         g_pwm.setSequence(1U, g_sequenceWords, 4U);
}

static bool configureCommonUp() {
  if (!g_pwm.beginRaw(kPins4, 4U, kPwmFrequencyHz, Pwm::DecoderLoad::kCommon,
                      Pwm::DecoderMode::kRefreshCount, 0U,
                      Pwm::CounterMode::kUp)) {
    return false;
  }

  g_sequenceWords[0] =
      Pwm::encodeSequenceWordPermille(350U, g_pwm.countertop(), true);
  return g_pwm.setSequence(0U, g_sequenceWords, 1U) &&
         g_pwm.setSequence(1U, g_sequenceWords, 1U);
}

static bool configureGroupedUp() {
  if (!g_pwm.beginRaw(kPins4, 4U, kPwmFrequencyHz, Pwm::DecoderLoad::kGrouped,
                      Pwm::DecoderMode::kRefreshCount, 0U,
                      Pwm::CounterMode::kUp)) {
    return false;
  }

  const uint16_t top = g_pwm.countertop();
  g_sequenceWords[0] = Pwm::encodeSequenceWordPermille(250U, top, true);
  g_sequenceWords[1] = Pwm::encodeSequenceWordPermille(750U, top, true);
  return g_pwm.setSequence(0U, g_sequenceWords, 2U) &&
         g_pwm.setSequence(1U, g_sequenceWords, 2U);
}

static bool configureWaveformUp() {
  if (!g_pwm.beginRaw(kPins3, 3U, kPwmFrequencyHz, Pwm::DecoderLoad::kWaveForm,
                      Pwm::DecoderMode::kRefreshCount, 0U,
                      Pwm::CounterMode::kUp)) {
    return false;
  }

  const uint16_t top = g_pwm.countertop();
  g_sequenceWords[0] = Pwm::encodeSequenceWordPermille(150U, top, true);
  g_sequenceWords[1] = Pwm::encodeSequenceWordPermille(500U, top, true);
  g_sequenceWords[2] = Pwm::encodeSequenceWordPermille(850U, top, true);
  g_sequenceWords[3] = top;
  return g_pwm.setSequence(0U, g_sequenceWords, 4U) &&
         g_pwm.setSequence(1U, g_sequenceWords, 4U);
}

static const CompareProbeCase kCases[] = {
    {"individual_up", configureIndividualUp},
    {"raw_individual_up", configureRawIndividualUp},
    {"individual_updown", configureIndividualUpDown},
    {"common_up", configureCommonUp},
    {"grouped_up", configureGroupedUp},
    {"waveform_up", configureWaveformUp},
};

static void printCaptureCase(const CompareProbeCase& probeCase) {
  const uint32_t cmp0 = g_captureTimer.ccValue(0U);
  const uint32_t cmp1 = g_captureTimer.ccValue(1U);
  const uint32_t cmp2 = g_captureTimer.ccValue(2U);
  const uint32_t timerHz = g_captureTimer.timerHz();

  Serial.print("case=");
  Serial.print(probeCase.name);
  Serial.print(" pwm_top=");
  Serial.print(g_pwm.countertop());
  Serial.print(" pwm_prescaler=");
  Serial.print(g_pwm.prescaler());
  Serial.print(" timer_hz=");
  Serial.print(timerHz);
  Serial.print(" cmp_ticks=");
  Serial.print(cmp0);
  Serial.print(',');
  Serial.print(cmp1);
  Serial.print(',');
  Serial.print(cmp2);
  Serial.print(" evt=");
  Serial.print(g_pwm.pollCompareMatch(0U, false) ? 1 : 0);
  Serial.print(',');
  Serial.print(g_pwm.pollCompareMatch(1U, false) ? 1 : 0);
  Serial.print(',');
  Serial.println(g_pwm.pollCompareMatch(2U, false) ? 1 : 0);
}

static void runProbeCase(const CompareProbeCase& probeCase) {
  g_pwm.end();

  if (!probeCase.configure()) {
    Serial.print("case=");
    Serial.print(probeCase.name);
    Serial.println(" configure_failed");
    return;
  }

  if (!configureCaptureTimer() || !configureCaptureDppi()) {
    Serial.print("case=");
    Serial.print(probeCase.name);
    Serial.println(" capture_setup_failed");
    g_pwm.end();
    return;
  }

  g_captureTimer.clear();
  g_captureTimer.start();
  if (!g_pwm.start()) {
    Serial.print("case=");
    Serial.print(probeCase.name);
    Serial.println(" start_failed");
    g_pwm.end();
    return;
  }

  delay(kSettleMs);
  printCaptureCase(probeCase);
  g_pwm.end();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println("PwmCompareMatchTimingProbe");
  Serial.println("PWM20 comparematch timing capture over DPPI into TIMER21.");
  Serial.println("D0-D3 are used, waveform mode uses D0-D2.");
}

void loop() {
  for (uint8_t i = 0U; i < (sizeof(kCases) / sizeof(kCases[0])); ++i) {
    runProbeCase(kCases[i]);
    delay(250);
  }

  Serial.println("timing_done");
  delay(1500);
}
