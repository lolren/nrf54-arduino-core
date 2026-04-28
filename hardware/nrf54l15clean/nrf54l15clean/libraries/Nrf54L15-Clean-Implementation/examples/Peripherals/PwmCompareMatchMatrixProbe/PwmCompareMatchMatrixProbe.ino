#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

struct CompareProbeCase {
  const char* name;
  uint8_t measuredCompareMask;
  bool (*configure)();
};

static constexpr Pin kPins4[] = {kPinD0, kPinD1, kPinD2, kPinD3};
static constexpr Pin kPins3[] = {kPinD0, kPinD1, kPinD2};
static constexpr uint32_t kPwmFrequencyHz = 2000UL;
static constexpr uint32_t kMeasureWindowMs = 300UL;
static constexpr uint8_t kPeriodDppiChannel = 11U;
static constexpr uint8_t kCompare0DppiChannel = 12U;
static constexpr uint8_t kCompare1DppiChannel = 13U;
static constexpr uint8_t kCompare2DppiChannel = 14U;
static constexpr uint8_t kSnapshotChannel = 0U;
static constexpr uint32_t kPwmIntenMask =
    (1UL << PWM_INTENSET_PWMPERIODEND_Pos) |
    (1UL << PWM_INTENSET_COMPAREMATCH0_Pos) |
    (1UL << PWM_INTENSET_COMPAREMATCH1_Pos) |
    (1UL << PWM_INTENSET_COMPAREMATCH2_Pos);
static constexpr uint32_t kPwmIntpendMask =
    (1UL << PWM_INTPEND_PWMPERIODEND_Pos) |
    (1UL << PWM_INTPEND_COMPAREMATCH0_Pos) |
    (1UL << PWM_INTPEND_COMPAREMATCH1_Pos) |
    (1UL << PWM_INTPEND_COMPAREMATCH2_Pos);

static Pwm g_pwm(nrf54l15::PWM20_BASE);
static Timer g_periodCounter(nrf54l15::TIMER21_BASE);
static Timer g_compare0Counter(nrf54l15::TIMER22_BASE);
static Timer g_compare1Counter(nrf54l15::TIMER23_BASE);
static Timer g_compare2Counter(nrf54l15::TIMER24_BASE);
static Dppic g_dppic(nrf54l15::DPPIC20_BASE);
static uint16_t g_sequenceWords[4];

static void clearPwmCompareEvents() {
  nrf54l15::reg32(nrf54l15::PWM20_BASE + nrf54l15::pwm::EVENTS_PWMPERIODEND) = 0U;
  for (uint8_t ch = 0U; ch < 4U; ++ch) {
    nrf54l15::reg32(nrf54l15::PWM20_BASE + nrf54l15::pwm::EVENTS_COMPAREMATCH +
                    (static_cast<uint32_t>(ch) * nrf54l15::pwm::REGISTER_STRIDE)) = 0U;
  }
}

static void enablePwmInterruptPendingBits() {
  nrf54l15::reg32(nrf54l15::PWM20_BASE + nrf54l15::pwm::INTENCLR) = 0xFFFFFFFFUL;
  nrf54l15::reg32(nrf54l15::PWM20_BASE + nrf54l15::pwm::INTENSET) = kPwmIntenMask;
}

static void clearCounters() {
  g_periodCounter.clear();
  g_compare0Counter.clear();
  g_compare1Counter.clear();
  g_compare2Counter.clear();
}

static bool configureDppiCounters() {
  return g_dppic.connect(g_pwm.publishPeriodEndConfigRegister(),
                         g_periodCounter.subscribeCountConfigRegister(),
                         kPeriodDppiChannel, true) &&
         g_dppic.connect(g_pwm.publishCompareMatchConfigRegister(0U),
                         g_compare0Counter.subscribeCountConfigRegister(),
                         kCompare0DppiChannel, true) &&
         g_dppic.connect(g_pwm.publishCompareMatchConfigRegister(1U),
                         g_compare1Counter.subscribeCountConfigRegister(),
                         kCompare1DppiChannel, true) &&
         g_dppic.connect(g_pwm.publishCompareMatchConfigRegister(2U),
                         g_compare2Counter.subscribeCountConfigRegister(),
                         kCompare2DppiChannel, true);
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
    {"individual_up", 0x07U, configureIndividualUp},
    {"raw_individual_up", 0x07U, configureRawIndividualUp},
    {"individual_updown", 0x07U, configureIndividualUpDown},
    {"common_up", 0x01U, configureCommonUp},
    {"grouped_up", 0x03U, configureGroupedUp},
    {"waveform_up", 0x07U, configureWaveformUp},
};

static bool beginCounters() {
  return g_periodCounter.begin(TimerBitWidth::k32bit, 0U, true) &&
         g_compare0Counter.begin(TimerBitWidth::k32bit, 0U, true) &&
         g_compare1Counter.begin(TimerBitWidth::k32bit, 0U, true) &&
         g_compare2Counter.begin(TimerBitWidth::k32bit, 0U, true);
}

static void startCounters() {
  g_periodCounter.start();
  g_compare0Counter.start();
  g_compare1Counter.start();
  g_compare2Counter.start();
}

static void printCaseResult(const CompareProbeCase& probeCase,
                            uint32_t periodCount,
                            uint32_t compare0Count,
                            uint32_t compare1Count,
                            uint32_t compare2Count) {
  const uint32_t intpend =
      nrf54l15::reg32(nrf54l15::PWM20_BASE + nrf54l15::pwm::INTPEND) &
      kPwmIntpendMask;

  Serial.print("case=");
  Serial.print(probeCase.name);
  Serial.print(" mask=0x");
  Serial.print(probeCase.measuredCompareMask, HEX);
  Serial.print(" period=");
  Serial.print(periodCount);
  Serial.print(" cmp0=");
  Serial.print(compare0Count);
  Serial.print(" cmp1=");
  Serial.print(compare1Count);
  Serial.print(" cmp2=");
  Serial.print(compare2Count);
  Serial.print(" evt_period=");
  Serial.print(g_pwm.pollPeriodEnd(false) ? 1 : 0);
  Serial.print(" evt_cmp0=");
  Serial.print(g_pwm.pollCompareMatch(0U, false) ? 1 : 0);
  Serial.print(" evt_cmp1=");
  Serial.print(g_pwm.pollCompareMatch(1U, false) ? 1 : 0);
  Serial.print(" evt_cmp2=");
  Serial.print(g_pwm.pollCompareMatch(2U, false) ? 1 : 0);
  Serial.print(" intpend=0x");
  Serial.println(intpend, HEX);
}

static void runProbeCase(const CompareProbeCase& probeCase) {
  g_pwm.end();
  clearPwmCompareEvents();
  clearCounters();

  if (!probeCase.configure()) {
    Serial.print("case=");
    Serial.print(probeCase.name);
    Serial.println(" configure_failed");
    return;
  }

  enablePwmInterruptPendingBits();
  clearPwmCompareEvents();
  clearCounters();
  if (!configureDppiCounters()) {
    Serial.print("case=");
    Serial.print(probeCase.name);
    Serial.println(" dppi_connect_failed");
    g_pwm.end();
    return;
  }

  if (!g_pwm.start()) {
    Serial.print("case=");
    Serial.print(probeCase.name);
    Serial.println(" start_failed");
    g_pwm.end();
    return;
  }

  delay(kMeasureWindowMs);

  const uint32_t periodCount = g_periodCounter.capture(kSnapshotChannel);
  const uint32_t compare0Count = g_compare0Counter.capture(kSnapshotChannel);
  const uint32_t compare1Count = g_compare1Counter.capture(kSnapshotChannel);
  const uint32_t compare2Count = g_compare2Counter.capture(kSnapshotChannel);
  printCaseResult(probeCase, periodCount, compare0Count, compare1Count, compare2Count);

  g_pwm.end();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println("PwmCompareMatchMatrixProbe");
  Serial.println("PWM20 comparematch matrix over DPPI into TIMER21..24 counters.");
  Serial.println("D0-D3 are used, waveform mode uses D0-D2.");

  if (!beginCounters()) {
    Serial.println("counter begin failed");
    while (true) {
      delay(1000);
    }
  }

  startCounters();
}

void loop() {
  for (uint8_t i = 0U; i < (sizeof(kCases) / sizeof(kCases[0])); ++i) {
    runProbeCase(kCases[i]);
    delay(250);
  }

  Serial.println("matrix_done");
  delay(1500);
}
