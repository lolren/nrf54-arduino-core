#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

static constexpr Pin kPins[] = {kPinD0, kPinD1, kPinD2, kPinD3};
static constexpr uint32_t kPwmFrequencyHz = 2000UL;
static constexpr uint32_t kStepIntervalMs = 1000UL;
static constexpr uint16_t kDutyPermille[] = {
    100U, 250U, 400U, 550U, 700U, 850U, 600U, 300U};
static constexpr uint8_t kDppiChannel = 3U;

static Pwm g_pwm(nrf54l15::PWM20_BASE);
static Timer g_timer(nrf54l15::TIMER22_BASE);
static Dppic g_dppic(nrf54l15::DPPIC20_BASE);
static uint16_t g_sequenceWords[sizeof(kDutyPermille) / sizeof(kDutyPermille[0])];
static uint8_t g_expectedIndex = 0U;
static uint32_t g_lastPrintMs = 0U;

static void buildSequenceWords() {
  const uint16_t top = g_pwm.countertop();
  for (uint8_t i = 0U; i < (sizeof(kDutyPermille) / sizeof(kDutyPermille[0])); ++i) {
    g_sequenceWords[i] =
        Pwm::encodeSequenceWordPermille(kDutyPermille[i], top, true);
  }
}

static void printExpectedStep(uint8_t index) {
  Serial.print("step=");
  Serial.print(index);
  Serial.print(" duty_permille=");
  Serial.println(kDutyPermille[index]);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println("PwmDppiTimerStepper");
  Serial.println("TIMER22 compare drives PWM20 NEXTSTEP over DPPIC20.");
  Serial.println("D0-D3 share one common duty word that steps once per second.");

  if (!g_pwm.beginRaw(kPins, 4U, kPwmFrequencyHz, Pwm::DecoderLoad::kCommon,
                      Pwm::DecoderMode::kNextStep)) {
    Serial.println("PWM beginRaw failed");
    while (true) {
      delay(1000);
    }
  }

  buildSequenceWords();
  if (!g_pwm.setSequence(
          0U, g_sequenceWords,
          static_cast<uint16_t>(sizeof(g_sequenceWords) / sizeof(g_sequenceWords[0])))) {
    Serial.println("PWM setSequence failed");
    while (true) {
      delay(1000);
    }
  }

  if (!g_timer.begin(TimerBitWidth::k32bit, 4U, false)) {
    Serial.println("TIMER22 begin failed");
    while (true) {
      delay(1000);
    }
  }

  const uint32_t compareTicks = g_timer.ticksFromMicros(kStepIntervalMs * 1000UL);
  if (!g_timer.setCompare(0U, compareTicks, true, false, false, false)) {
    Serial.println("TIMER22 compare failed");
    while (true) {
      delay(1000);
    }
  }

  if (!g_dppic.connect(g_timer.publishCompareConfigRegister(0U),
                       g_pwm.subscribeNextStepConfigRegister(), kDppiChannel)) {
    Serial.println("DPPIC connect failed");
    while (true) {
      delay(1000);
    }
  }

  g_timer.clear();
  g_timer.start();
  if (!g_pwm.start(0U)) {
    Serial.println("PWM start failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.print("pwm_hz=");
  Serial.println(kPwmFrequencyHz);
  Serial.print("step_interval_ms=");
  Serial.println(kStepIntervalMs);
  Serial.print("timer22_hz=");
  Serial.println(g_timer.timerHz());
  printExpectedStep(g_expectedIndex);
  g_lastPrintMs = millis();
}

void loop() {
  const uint32_t now = millis();
  if ((now - g_lastPrintMs) >= kStepIntervalMs) {
    g_lastPrintMs += kStepIntervalMs;
    g_expectedIndex = static_cast<uint8_t>(
        (g_expectedIndex + 1U) % (sizeof(kDutyPermille) / sizeof(kDutyPermille[0])));
    printExpectedStep(g_expectedIndex);
  }

  if (g_pwm.pollRamUnderflow(true)) {
    Serial.println("ram_underflow");
  }
  if (g_pwm.pollSequenceEnd(0U, true)) {
    Serial.println("sequence_end");
  }
}
