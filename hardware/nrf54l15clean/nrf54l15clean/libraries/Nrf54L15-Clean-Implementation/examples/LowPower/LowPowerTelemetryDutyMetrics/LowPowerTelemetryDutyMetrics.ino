#include <Arduino.h>
#include <nrf54l15.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

// Low-power telemetry window:
// - Track CPU active vs WFI-sleep time in microseconds.
// - Duty-cycle SAADC and VBAT divider only in sampling windows.
// - Emit rolling reports for quick current-profile tuning.
static constexpr uint32_t kSamplePeriodMs = 5000UL;
static constexpr uint32_t kReportPeriodMs = 30000UL;
static constexpr uint32_t kLedPulseMs = 5UL;

static Saadc g_adc(nrf54l15::SAADC_BASE);
static PowerManager g_power;

static uint64_t g_activeUs = 0ULL;
static uint64_t g_sleepUs = 0ULL;
static uint32_t g_sampleCount = 0U;
static uint32_t g_sampleFailures = 0U;
static int32_t g_lastA0mV = -1;
static int32_t g_lastVbatmV = -1;
static uint32_t g_nextSampleMs = 0U;
static uint32_t g_nextReportMs = 0U;

static inline uint32_t elapsedUs(uint32_t startUs, uint32_t endUs) {
  return static_cast<uint32_t>(endUs - startUs);
}

static inline void cpuIdleWfi() { __asm volatile("wfi"); }

static bool sampleA0MilliVolts(int32_t* outMv) {
  if (outMv == nullptr) {
    return false;
  }
  if (!g_adc.begin(AdcResolution::k12bit, 400000UL)) {
    return false;
  }
  const bool ok = g_adc.configureSingleEnded(0U, kPinA0, AdcGain::k2over8) &&
                  g_adc.sampleMilliVolts(outMv, 400000UL);
  g_adc.end();
  return ok;
}

static bool sampleVbatMilliVolts(int32_t* outMv) {
  if (outMv == nullptr) {
    return false;
  }
  if (!Gpio::configure(kPinVbatEnable, GpioDirection::kOutput, GpioPull::kDisabled)) {
    return false;
  }

  (void)Gpio::write(kPinVbatEnable, true);
  delayMicroseconds(200);

  bool ok = g_adc.begin(AdcResolution::k12bit, 400000UL) &&
            g_adc.configureSingleEnded(0U, kPinVbatSense, AdcGain::k2over8);
  int32_t halfMv = -1;
  if (ok) {
    ok = g_adc.sampleMilliVolts(&halfMv, 400000UL);
  }
  g_adc.end();
  (void)Gpio::write(kPinVbatEnable, false);
  if (!ok) {
    return false;
  }
  *outMv = halfMv * 2;
  return true;
}

static void printWindowReport() {
  const uint64_t totalUs = g_activeUs + g_sleepUs;
  const uint32_t activePermille =
      (totalUs > 0ULL) ? static_cast<uint32_t>((g_activeUs * 1000ULL) / totalUs) : 0U;
  const uint32_t sleepPermille = 1000U - activePermille;

  char line[200];
  snprintf(line, sizeof(line),
           "window samples=%lu fail=%lu active=%lu.%01lu%% sleep=%lu.%01lu%% A0=%ldmV VBAT=%ldmV\r\n",
           static_cast<unsigned long>(g_sampleCount),
           static_cast<unsigned long>(g_sampleFailures),
           static_cast<unsigned long>(activePermille / 10U),
           static_cast<unsigned long>(activePermille % 10U),
           static_cast<unsigned long>(sleepPermille / 10U),
           static_cast<unsigned long>(sleepPermille % 10U),
           static_cast<long>(g_lastA0mV),
           static_cast<long>(g_lastVbatmV));
  Serial.print(line);

  g_activeUs = 0ULL;
  g_sleepUs = 0ULL;
  g_sampleCount = 0U;
  g_sampleFailures = 0U;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::configure(kPinUserButton, GpioDirection::kInput, GpioPull::kPullUp);
  (void)Gpio::write(kPinUserLed, true);  // LED off (active-low)

  // Keep CPU at 64 MHz for this telemetry workload.
  NRF_OSCILLATORS->PLL.FREQ = OSCILLATORS_PLL_FREQ_FREQ_CK64M;
  g_power.setLatencyMode(PowerLatencyMode::kLowPower);

  const uint32_t now = millis();
  g_nextSampleMs = now + 1000UL;
  g_nextReportMs = now + kReportPeriodMs;

  Serial.println("LowPowerTelemetryDutyMetrics: started");
  Serial.println("Press user button for immediate report.");
}

void loop() {
  const uint32_t loopStartUs = micros();
  const uint32_t nowMs = millis();

  bool buttonHigh = true;
  (void)Gpio::read(kPinUserButton, &buttonHigh);
  const bool forceReport = !buttonHigh;  // Active low.

  if (static_cast<int32_t>(nowMs - g_nextSampleMs) >= 0) {
    (void)Gpio::write(kPinUserLed, false);
    delay(kLedPulseMs);
    (void)Gpio::write(kPinUserLed, true);

    int32_t a0mV = -1;
    int32_t vbatmV = -1;
    const bool a0Ok = sampleA0MilliVolts(&a0mV);
    const bool vbatOk = sampleVbatMilliVolts(&vbatmV);
    if (a0Ok) {
      g_lastA0mV = a0mV;
    }
    if (vbatOk) {
      g_lastVbatmV = vbatmV;
    }
    if (!a0Ok || !vbatOk) {
      ++g_sampleFailures;
    }
    ++g_sampleCount;
    g_nextSampleMs += kSamplePeriodMs;
  }

  if (forceReport || static_cast<int32_t>(nowMs - g_nextReportMs) >= 0) {
    printWindowReport();
    g_nextReportMs = nowMs + kReportPeriodMs;
  }

  // This accounting is only approximate, but it is useful for relative tuning:
  // active work before WFI vs time spent asleep between wakeups.
  const uint32_t preSleepUs = micros();
  g_activeUs += elapsedUs(loopStartUs, preSleepUs);
  cpuIdleWfi();
  const uint32_t postSleepUs = micros();
  g_sleepUs += elapsedUs(preSleepUs, postSleepUs);
}
