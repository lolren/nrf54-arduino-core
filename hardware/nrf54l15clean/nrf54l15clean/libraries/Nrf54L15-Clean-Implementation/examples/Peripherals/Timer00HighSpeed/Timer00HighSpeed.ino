/*
 * Timer00 High-Speed — MCU-Domain Timer at 128 MHz
 *
 * TIMER00 runs on the MCU bus at 128 MHz (7.8125 ns/tick at prescaler 0).
 * This example demonstrates:
 *   - High-resolution timing using 128 MHz prescaler-0 mode
 *   - Compare event measurement with one-shot mode
 *   - Benchmarking a known code block at nanosecond precision
 *
 * Hardware: XIAO nRF54L15 (TIMER00 is MCU-domain, 6 channels)
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal_timer00.h"

using namespace xiao_nrf54l15;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  TIMER00 — MCU-Domain High-Speed Timer"));
  Serial.println(F("======================================"));
  Serial.println();

  // ---- Configure TIMER00 at 128 MHz (prescaler 0) ----
  Timer00 timer;
  timer.begin(3, 0, false);  // 32-bit, prescaler 0, timer mode
  uint32_t freq = timer.timerHz();

  Serial.print(F("  Timer frequency: "));
  Serial.print(freq / 1000000);
  Serial.println(F(" MHz"));
  Serial.print(F("  Tick period: "));
  Serial.print(1000000.0 / (freq / 1000.0), 1);
  Serial.println(F(" ns"));
  Serial.println();

  // ---- Benchmark: measure a loop of 1000 NOPs ----
  Serial.println(F("--- Benchmarking 1000 NOP loops ---"));

  uint32_t totalTicks = 0;
  uint8_t iterations = 10;

  for (uint8_t i = 0; i < iterations; i++) {
    timer.clear();
    timer.start();

    // Code to benchmark: 1000 NOPs
    for (int j = 0; j < 1000; j++) {
      __asm volatile("nop");
    }

    timer.stop();
    uint32_t ticks = timer.counterValue();
    totalTicks += ticks;

    Serial.print(F("  Iteration "));
    Serial.print(i + 1);
    Serial.print(F(": "));
    Serial.print(ticks);
    Serial.print(F(" ticks ("));
    Serial.print(static_cast<double>(ticks) * 1000.0 / (freq / 1000.0), 0);
    Serial.println(F(" ns)"));
  }

  uint32_t avgTicks = totalTicks / iterations;
  Serial.print(F("  Average: "));
  Serial.print(avgTicks);
  Serial.print(F(" ticks ("));
  Serial.print(static_cast<double>(avgTicks) * 1000.0 / (freq / 1000.0), 1);
  Serial.println(F(" ns)"));
  Serial.println();

  // ---- Compare event timing ----
  Serial.println(F("--- Compare Channel Timing ---"));

  // Set up compare at 1ms with one-shot
  uint32_t ms1Ticks = timer.ticksFromMicros(1000);
  timer.setOneShot(0, true);
  timer.setCompare(0, ms1Ticks);

  Serial.print(F("  Compare[0] set to "));
  Serial.print(ms1Ticks);
  Serial.print(F(" ticks (~1 ms)"));
  Serial.println();

  timer.clear();
  timer.start();

  // Poll for compare event
  uint32_t spinCount = 0;
  while (!timer.pollCompare(0, false)) {
    spinCount++;
    if (spinCount > 10000000) break;  // Safety timeout
  }

  uint32_t actualTicks = timer.counterValue();
  timer.stop();

  Serial.print(F("  Actual ticks at match: "));
  Serial.print(actualTicks);
  Serial.print(F(" (expected ~"));
  Serial.print(ms1Ticks);
  Serial.println(F(")"));

  // Disable one-shot for cleanup
  timer.setOneShot(0, false);
  Serial.println();

  // ---- Timer resolution test: measure pin toggle ----
  Serial.println(F("--- Pin Toggle Overhead ---"));

  pinMode(LED_BUILTIN, OUTPUT);
  uint32_t toggleTicks = 0;

  for (uint8_t i = 0; i < 10; i++) {
    timer.clear();
    timer.start();
    digitalWrite(LED_BUILTIN, HIGH);
    digitalWrite(LED_BUILTIN, LOW);
    timer.stop();
    toggleTicks = timer.counterValue();

    Serial.print(F("  Toggle: "));
    Serial.print(toggleTicks);
    Serial.print(F(" ticks ("));
    Serial.print(static_cast<double>(toggleTicks) * 1000.0 / (freq / 1000.0), 1);
    Serial.println(F(" ns)"));
  }
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  TIMER00 demo complete."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
