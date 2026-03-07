#include <Arduino.h>
#include <nrf54l15.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

// Datasheet-guided low-power strategy:
// - Keep CPU in WFI between sampling windows.
// - Duty-cycle SAADC and external divider path (VBAT enable).
// - Keep average activity low by sampling infrequently.

static constexpr uint32_t kSamplePeriodMs = 10000UL;
static constexpr uint32_t kLedPulseMs = 6UL;

static Saadc g_adc(nrf54l15::SAADC_BASE);

static inline void cpuIdleWfi() {
  __asm volatile("wfi");
}

static void sleepUntilMs(uint32_t deadlineMs) {
  while (static_cast<int32_t>(millis() - deadlineMs) < 0) {
    cpuIdleWfi();
  }
}

static bool sampleA0(int32_t* outMv) {
  if (outMv == nullptr) {
    return false;
  }
  if (!g_adc.begin(AdcResolution::k12bit, 400000UL)) {
    return false;
  }
  const bool ok = g_adc.configureSingleEnded(0, kPinA0, AdcGain::k2over8) &&
                  g_adc.sampleMilliVolts(outMv, 400000UL);
  g_adc.end();
  return ok;
}

static bool sampleVbat(int32_t* outMv) {
  if (outMv == nullptr) {
    return false;
  }

  if (!Gpio::configure(kPinVbatEnable, GpioDirection::kOutput, GpioPull::kDisabled)) {
    return false;
  }

  (void)Gpio::write(kPinVbatEnable, true);
  delayMicroseconds(200);

  bool ok = g_adc.begin(AdcResolution::k12bit, 400000UL) &&
            g_adc.configureSingleEnded(0, kPinVbatSense, AdcGain::k2over8);

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

void setup() {
  Serial.begin(115200);
  delay(250);

  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::write(kPinUserLed, true);  // LED off

  // Lower CPU frequency for low-power sampling workload.
  NRF_OSCILLATORS->PLL.FREQ = OSCILLATORS_PLL_FREQ_FREQ_CK64M;

  Serial.println("LowPowerDutyCycleAdc: started");
}

void loop() {
  const uint32_t t0 = millis();

  (void)Gpio::write(kPinUserLed, false);
  delay(kLedPulseMs);
  (void)Gpio::write(kPinUserLed, true);

  int32_t a0mV = -1;
  int32_t vbatmV = -1;
  const bool a0Ok = sampleA0(&a0mV);
  const bool vbOk = sampleVbat(&vbatmV);

  char line[128];
  snprintf(line, sizeof(line), "adc-window t=%lu A0=%s(%ldmV) VBAT=%s(%ldmV)\r\n",
           static_cast<unsigned long>(t0),
           a0Ok ? "OK" : "FAIL", static_cast<long>(a0mV),
           vbOk ? "OK" : "FAIL", static_cast<long>(vbatmV));
  Serial.print(line);

  sleepUntilMs(t0 + kSamplePeriodMs);
}
