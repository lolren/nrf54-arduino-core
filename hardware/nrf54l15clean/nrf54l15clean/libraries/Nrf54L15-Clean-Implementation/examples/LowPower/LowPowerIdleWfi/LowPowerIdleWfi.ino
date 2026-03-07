#include <Arduino.h>
#include <nrf54l15.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

// Datasheet-guided low-power strategy:
// - Keep CPU in System ON idle using WFI whenever no work is pending.
// - Use lower CPU frequency (64 MHz) when workload is light.
// - Keep peripherals inactive unless needed.

static constexpr uint32_t kHeartbeatPeriodMs = 3000UL;
static constexpr uint32_t kLedPulseMs = 8UL;

static inline void cpuIdleWfi() {
  __asm volatile("wfi");
}

static bool setCpuFreqRaw(uint32_t raw, uint32_t spinLimit = 500000UL) {
  NRF_OSCILLATORS->PLL.FREQ = raw;
  while (spinLimit-- > 0U) {
    if ((NRF_OSCILLATORS->PLL.CURRENTFREQ & 0x3UL) == raw) {
      return true;
    }
  }
  return false;
}

static const char* cpuFreqName(uint32_t raw) {
  if (raw == OSCILLATORS_PLL_FREQ_FREQ_CK64M) {
    return "64MHz";
  }
  if (raw == OSCILLATORS_PLL_FREQ_FREQ_CK128M) {
    return "128MHz";
  }
  return "other";
}

static void sleepUntilMs(uint32_t deadlineMs) {
  while (static_cast<int32_t>(millis() - deadlineMs) < 0) {
    cpuIdleWfi();
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::write(kPinUserLed, true);  // LED off (active-low)
  (void)Gpio::configure(kPinUserButton, GpioDirection::kInput, GpioPull::kDisabled);

  const bool freqOk = setCpuFreqRaw(OSCILLATORS_PLL_FREQ_FREQ_CK64M);
  const uint32_t current = (NRF_OSCILLATORS->PLL.CURRENTFREQ & 0x3UL);

  Serial.print("LowPowerIdleWfi: freq-set=");
  Serial.print(freqOk ? "OK" : "FAIL");
  Serial.print(" current=");
  Serial.println(cpuFreqName(current));
}

void loop() {
  const uint32_t t0 = millis();

  // Short active pulse, then return to idle sleep.
  (void)Gpio::write(kPinUserLed, false);
  delay(kLedPulseMs);
  (void)Gpio::write(kPinUserLed, true);

  bool buttonHigh = true;
  (void)Gpio::read(kPinUserButton, &buttonHigh);
  if (!buttonHigh) {
    // Button held: switch temporarily to performance mode.
    (void)setCpuFreqRaw(OSCILLATORS_PLL_FREQ_FREQ_CK128M);
  } else {
    (void)setCpuFreqRaw(OSCILLATORS_PLL_FREQ_FREQ_CK64M);
  }

  const uint32_t tNext = t0 + kHeartbeatPeriodMs;
  sleepUntilMs(tNext);
}
