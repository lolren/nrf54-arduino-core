#include <nrf54l15_hal.h>

using namespace xiao_nrf54l15;

// Best demonstration path:
// - Tools -> CPU Frequency -> 128 MHz (Turbo)
// - Tools -> Power Profile -> Low Power (WFI Idle)
//
// The sketch runs active work at 128 MHz, but delay()/yield() temporarily
// down-clock the CPU to 64 MHz before WFI and restore the previous speed after
// wake.

static constexpr unsigned long kBlinkOnMs = 20UL;
static constexpr unsigned long kIdleMs = 1000UL;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  (void)ClockControl::setCpuFrequency(CpuFrequency::k128MHz);
  (void)ClockControl::enableIdleCpuScaling(CpuFrequency::k64MHz);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(kBlinkOnMs);
  digitalWrite(LED_BUILTIN, LOW);

  delay(kIdleMs);
}
