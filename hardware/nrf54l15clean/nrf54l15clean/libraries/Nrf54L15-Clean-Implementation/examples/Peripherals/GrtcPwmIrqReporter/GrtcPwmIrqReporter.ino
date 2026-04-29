#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

#if defined(PIN_WIRE1_SCL)
static constexpr uint8_t kGrtcPwmPin = PIN_WIRE1_SCL;
static constexpr char kGrtcPwmPinLabel[] = "PIN_WIRE1_SCL / P0.03";
#elif defined(PIN_D11)
static constexpr uint8_t kGrtcPwmPin = PIN_D11;
static constexpr char kGrtcPwmPinLabel[] = "PIN_D11 / P0.03";
#else
#error "This board variant does not expose a known Arduino alias for the fixed GRTC PWM pin."
#endif

static constexpr uint8_t kDutyCodes[] = {32U, 96U, 160U, 224U};
static constexpr uint32_t kReportMs = 1000UL;
static constexpr uint8_t kDutyAdvanceEveryReports = 3U;
static constexpr uint8_t kRestartEveryReports = 6U;

static GrtcPwm g_pwm;
static volatile uint32_t g_irqPeriodEndCount = 0U;
static volatile uint32_t g_irqReadyCount = 0U;
static volatile uint32_t g_lastIrqMask = 0U;
static uint8_t g_dutyIndex = 0U;
static bool g_running = false;
static uint32_t g_lastReportMs = 0U;
static uint32_t g_lastPeriodEndCount = 0U;
static uint32_t g_lastReadyCount = 0U;
static uint8_t g_reportIndex = 0U;

static uint32_t dutyPercent(uint8_t duty8) {
  return (static_cast<uint32_t>(duty8) * 100UL + 127UL) / 255UL;
}

static void onGrtcPwmIrq(uint32_t irqMask, void*) {
  if ((irqMask & GrtcPwm::kIrqPeriodEnd) != 0U) {
    ++g_irqPeriodEndCount;
  }
  if ((irqMask & GrtcPwm::kIrqReady) != 0U) {
    ++g_irqReadyCount;
  }
  g_lastIrqMask = irqMask;
}

static void snapshotIrqState(uint32_t* outPeriodEndCount,
                             uint32_t* outReadyCount,
                             uint32_t* outLastIrqMask) {
  if (outPeriodEndCount == nullptr || outReadyCount == nullptr ||
      outLastIrqMask == nullptr) {
    return;
  }

  noInterrupts();
  *outPeriodEndCount = g_irqPeriodEndCount;
  *outReadyCount = g_irqReadyCount;
  *outLastIrqMask = g_lastIrqMask;
  interrupts();
}

static void toggleRunState() {
  if (g_running) {
    if (g_pwm.stop()) {
      g_running = false;
    }
  } else {
    g_dutyIndex =
        static_cast<uint8_t>((g_dutyIndex + 1U) %
                             (sizeof(kDutyCodes) / sizeof(kDutyCodes[0])));
    (void)g_pwm.setDuty8(kDutyCodes[g_dutyIndex]);
    if (g_pwm.start()) {
      g_running = true;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println("GrtcPwmIrqReporter");
  Serial.println("Fixed-pin GRTC PWM with shared GRTC IRQ callback reporting.");
  Serial.print("pin=");
  Serial.println(kGrtcPwmPinLabel);
  Serial.print("frequency_hz=");
  Serial.println(GrtcPwm::frequencyHz());
  Serial.println("Reports period-end IRQ rate once per second.");
  Serial.println("Every 3 reports the duty changes, every 6 reports PWM stops/starts.");
  Serial.println("On XIAO Sense this pin is shared with the Wire1/IMU route.");

  (void)BoardControl::setBatterySenseEnabled(false);
  (void)BoardControl::setImuMicEnabled(false);
  delay(5);

  if (!g_pwm.beginArduinoPin(kGrtcPwmPin, kDutyCodes[g_dutyIndex],
                             GrtcClockSource::kLfxo, false)) {
    Serial.println("GRTC PWM begin failed");
    while (true) {
      delay(1000);
    }
  }

  g_pwm.setIrqCallback(onGrtcPwmIrq, nullptr);
  if (!g_pwm.makeActive()) {
    Serial.println("GRTC PWM makeActive failed");
    while (true) {
      delay(1000);
    }
  }
  g_pwm.enableInterruptMask(GrtcPwm::irqSupportedMask(), true);
  if (!g_pwm.start()) {
    Serial.println("GRTC PWM start failed");
    while (true) {
      delay(1000);
    }
  }

  g_running = true;
  g_lastReportMs = millis();
}

void loop() {
  const uint32_t nowMs = millis();
  if ((nowMs - g_lastReportMs) < kReportMs) {
    delay(10);
    return;
  }
  g_lastReportMs = nowMs;

  uint32_t periodEndCount = 0U;
  uint32_t readyCount = 0U;
  uint32_t lastIrqMask = 0U;
  snapshotIrqState(&periodEndCount, &readyCount, &lastIrqMask);

  const uint32_t periodDelta = periodEndCount - g_lastPeriodEndCount;
  const uint32_t readyDelta = readyCount - g_lastReadyCount;
  g_lastPeriodEndCount = periodEndCount;
  g_lastReadyCount = readyCount;

  Serial.print("running=");
  Serial.print(g_running ? 1 : 0);
  Serial.print(" duty8=");
  Serial.print(kDutyCodes[g_dutyIndex]);
  Serial.print(" duty_pct=");
  Serial.print(dutyPercent(kDutyCodes[g_dutyIndex]));
  Serial.print(" period_delta=");
  Serial.print(periodDelta);
  Serial.print(" ready_delta=");
  Serial.print(readyDelta);
  Serial.print(" totals=");
  Serial.print(periodEndCount);
  Serial.print('/');
  Serial.print(readyCount);
  Serial.print(" last_mask=0x");
  Serial.println(lastIrqMask, HEX);

  ++g_reportIndex;
  if ((g_reportIndex % kDutyAdvanceEveryReports) == 0U && g_running) {
    g_dutyIndex =
        static_cast<uint8_t>((g_dutyIndex + 1U) %
                             (sizeof(kDutyCodes) / sizeof(kDutyCodes[0])));
    (void)g_pwm.setDuty8(kDutyCodes[g_dutyIndex]);
  }
  if ((g_reportIndex % kRestartEveryReports) == 0U) {
    toggleRunState();
  }
}
