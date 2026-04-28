#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

static constexpr Pin kPwmPin = kPinD0;
static constexpr uint32_t kRequestedPwmHz = 2000UL;
static constexpr uint16_t kDutyPermille = 250U;
static constexpr uint8_t kPeriodEndDppiChannel = 11U;
static constexpr uint8_t kSnapshotChannel = 0U;
static constexpr uint32_t kReportPeriodMs = 1000UL;

static Pwm g_pwm(nrf54l15::PWM20_BASE);
static Timer g_periodCounter(nrf54l15::TIMER21_BASE);
static Dppic g_dppic(nrf54l15::DPPIC20_BASE);
static uint32_t g_lastReportMs = 0U;
static uint32_t g_lastPeriodCount = 0U;

static uint32_t absDiff(uint32_t a, uint32_t b) {
  return (a > b) ? (a - b) : (b - a);
}

static uint32_t actualPwmHz() {
  const uint8_t pwmPrescaler = g_pwm.prescaler();
  if (pwmPrescaler > 31U || g_pwm.countertop() == 0U) {
    return 0U;
  }
  return (16000000UL >> pwmPrescaler) / static_cast<uint32_t>(g_pwm.countertop());
}

static bool setupPeriodCounterPath() {
  volatile uint32_t* const periodPublish = g_pwm.publishPeriodEndConfigRegister();
  return (periodPublish != nullptr) &&
         g_dppic.connect(periodPublish, g_periodCounter.subscribeCountConfigRegister(),
                         kPeriodEndDppiChannel, true);
}

static void printMeasurement(uint32_t elapsedMs) {
  const uint32_t periodCount = g_periodCounter.capture(kSnapshotChannel);
  const uint32_t periodDelta = periodCount - g_lastPeriodCount;
  g_lastPeriodCount = periodCount;

  const uint32_t expectedHz = actualPwmHz();
  const uint32_t expectedDelta = static_cast<uint32_t>(
      (static_cast<uint64_t>(expectedHz) * elapsedMs + 500ULL) / 1000ULL);
  static constexpr uint32_t kToleranceCounts = 2U;
  const bool ok = absDiff(periodDelta, expectedDelta) <= kToleranceCounts;

  Serial.print("elapsed_ms=");
  Serial.print(elapsedMs);
  Serial.print(" period_delta=");
  Serial.print(periodDelta);
  Serial.print(" expected_delta=");
  Serial.print(expectedDelta);
  Serial.print(" status=");
  Serial.println(ok ? "ok" : "mismatch");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println("PwmDppiPeriodCounter");
  Serial.println("PWM20 exports PWMPERIODEND over DPPI into TIMER21 COUNT.");

  if (!g_periodCounter.begin(TimerBitWidth::k32bit, 0U, true)) {
    Serial.println("period counter begin failed");
    while (true) {
      delay(1000);
    }
  }

  if (!g_pwm.beginSingle(kPwmPin, kRequestedPwmHz, kDutyPermille, true)) {
    Serial.println("pwm begin failed");
    while (true) {
      delay(1000);
    }
  }

  if (!setupPeriodCounterPath()) {
    Serial.println("dppic period counter setup failed");
    while (true) {
      delay(1000);
    }
  }

  g_periodCounter.clear();
  g_periodCounter.start();

  if (!g_pwm.start()) {
    Serial.println("pwm start failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.print("pin=D0 requested_pwm_hz=");
  Serial.print(kRequestedPwmHz);
  Serial.print(" duty_permille=");
  Serial.print(kDutyPermille);
  Serial.print(" pwm_countertop=");
  Serial.print(g_pwm.countertop());
  Serial.print(" pwm_prescaler=");
  Serial.print(g_pwm.prescaler());
  Serial.print(" actual_pwm_hz=");
  Serial.println(actualPwmHz());
  g_lastReportMs = millis();
}

void loop() {
  const uint32_t now = millis();
  if ((now - g_lastReportMs) >= kReportPeriodMs) {
    const uint32_t elapsedMs = now - g_lastReportMs;
    g_lastReportMs = now;
    printMeasurement(elapsedMs);
  }
}
