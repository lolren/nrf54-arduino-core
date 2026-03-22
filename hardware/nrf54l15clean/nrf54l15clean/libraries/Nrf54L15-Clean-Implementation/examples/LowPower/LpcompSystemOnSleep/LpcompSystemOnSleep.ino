/*
 * LpcompSystemOnSleep
 *
 * Low-power sleep loop that keeps the CPU in System ON idle (WFI), waking when
 * an analog voltage on A0 rises above a threshold. Full RAM is retained across
 * every sleep — no .noinit or retained-magic tricks needed.
 *
 * Contrast with LpcompSystemOffWake:
 *   LpcompSystemOffWake  — deepest possible sleep; RAM lost (except .noinit);
 *                           wakes as a full reset with ~1 ms startup time.
 *   LpcompSystemOnSleep  — lighter sleep; all RAM retained; wake latency ~1 ms
 *                           (one SysTick period); suitable for many wakes/second
 *                           or when global state must survive the sleep window.
 *
 * How it works:
 *   1. LPCOMP is started and runs continuously off VDD — independent of the CPU.
 *   2. The main loop calls WFI (Wait For Interrupt). The CPU halts and the
 *      regulator/clock network gates as much as possible (PowerLatencyMode::kLowPower).
 *   3. SysTick fires every 1 ms, waking the CPU for a few cycles. loop() polls
 *      LPCOMP EVENTS_UP and sleeps again if nothing happened.
 *   4. When EVENTS_UP is set (A0 crossed the threshold upward), loop() records
 *      the event, blinks the LED, prints stats, and returns to sleep.
 *
 * Power budget (approximate, 3.3 V):
 *   - WFI idle with kLowPower: ~20–50 µA system (dominated by regulator quiescent)
 *   - LPCOMP running: +~1 µA
 *   - SysTick duty-cycle overhead: negligible (~1 µs active / 1 ms period)
 *
 * Hardware:
 *   - Connect a voltage source to A0.
 *   - Drive A0 below kWakeThresholdMv before the sketch starts.
 *   - Raise A0 above kWakeThresholdMv to trigger a wake event.
 *
 * LED indicators:
 *   - 3 fast pulses: LPCOMP threshold crossing detected.
 *   - 1 slow pulse:  Heartbeat (every kHeartbeatMs), confirming the loop runs.
 *
 * Configuration:
 *   - kVddMv           — your supply voltage in mV (3300 for USB/3.3 V regulator)
 *   - kWakeThresholdMv — voltage level on A0 that triggers a wake (mV)
 */

#include <Arduino.h>
#include <nrf54l15.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static PowerManager g_power;
static Lpcomp g_lpcomp;

// Supply and threshold — set kVddMv to match your actual VDD.
// The LPCOMP snaps to the nearest of 16 fixed fractions of VDD; beginThresholdMv()
// picks the closest one automatically.
static constexpr uint16_t kVddMv           = 3300U;  // VDD in mV (3.3 V via USB/regulator)
static constexpr uint16_t kWakeThresholdMv = 200U;   // Wake when A0 exceeds this (mV)

// How often to print the alive heartbeat even while sleeping.
static constexpr uint32_t kHeartbeatMs = 5000UL;

// ---- RAM-retained state (normal variables — no .noinit needed in System ON) ----
static uint32_t g_wakeCount  = 0U;  // LPCOMP threshold crossings since power-on
static uint32_t g_lastWakeMs = 0U;  // millis() at the most recent wake event

static inline void cpuSleep() {
  __asm volatile("wfi");
}

static void pulseLed(uint8_t count, uint32_t onMs = 30UL, uint32_t offMs = 80UL) {
  for (uint8_t i = 0U; i < count; ++i) {
    Gpio::write(kPinUserLed, false);  // LED on (active-low)
    delay(onMs);
    Gpio::write(kPinUserLed, true);   // LED off
    if (i + 1U < count) {
      delay(offMs);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.print("\r\nLpcompSystemOnSleep\r\n");
  Serial.print("System ON WFI sleep — RAM fully retained across sleeps.\r\n");

  // kLowPower: allow the DCDC and clock network to gate aggressively during WFI.
  // Do not use kConstantLatency here — that keeps clocks running and wastes power.
  g_power.setLatencyMode(PowerLatencyMode::kLowPower);

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);  // LED off initially

  // Configure LPCOMP for continuous analog monitoring on A0.
  // hysteresis=true prevents false triggers from noise near the threshold.
  // LpcompDetect::kUp fires EVENTS_UP on a rising edge (low→high crossing).
  Serial.print("LPCOMP: VDD=");
  Serial.print(kVddMv);
  Serial.print("mV threshold=");
  Serial.print(kWakeThresholdMv);
  Serial.print("mV detect=rising...");
  const bool ok = g_lpcomp.beginThresholdMv(kPinA0, kVddMv, kWakeThresholdMv,
                                             true, LpcompDetect::kUp);
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");
  if (!ok) {
    while (true) {
      delay(1000);
    }
  }

  // Clear any stale event that may have been set before LPCOMP settled.
  g_lpcomp.clearEvents();

  Serial.print("Sleeping. Drive A0 above ");
  Serial.print(kWakeThresholdMv);
  Serial.print("mV to wake. Heartbeat every ");
  Serial.print(kHeartbeatMs);
  Serial.print("ms.\r\n");
  Serial.flush();
}

void loop() {
  static uint32_t s_lastHeartbeatMs = 0U;

  // --- Check for LPCOMP threshold crossing ---
  // EVENTS_UP is set by hardware the moment A0 crosses the threshold upward.
  // The CPU was asleep; SysTick woke it; we read the event here.
  if (g_lpcomp.pollUp(/*clearEvent=*/true)) {
    g_lastWakeMs = millis();
    ++g_wakeCount;

    // Sample the instantaneous comparator state: is A0 still above threshold?
    // Useful for detecting brief pulses vs. sustained high levels.
    g_lpcomp.sample(200000UL);
    const bool stillAbove = g_lpcomp.resultAbove();

    pulseLed(3U);

    Serial.print("[WAKE] #");
    Serial.print(g_wakeCount);
    Serial.print(" t=");
    Serial.print(g_lastWakeMs);
    Serial.print("ms A0=");
    Serial.print(stillAbove ? "above" : "below");
    Serial.print(" threshold\r\n");
    Serial.flush();
  }

  // --- Periodic heartbeat (demonstrates RAM counter is live across sleeps) ---
  const uint32_t now = millis();
  if ((now - s_lastHeartbeatMs) >= kHeartbeatMs) {
    s_lastHeartbeatMs = now;
    pulseLed(1U, 8UL, 0UL);  // single short pulse — minimal LED overhead
    Serial.print("alive t=");
    Serial.print(now);
    Serial.print("ms wakes=");
    Serial.print(g_wakeCount);
    if (g_wakeCount > 0U) {
      Serial.print(" last_wake=");
      Serial.print(g_lastWakeMs);
      Serial.print("ms");
    }
    Serial.print("\r\n");
    Serial.flush();
  }

  // --- Sleep until next SysTick (~1 ms) ---
  // LPCOMP keeps comparing while the CPU is halted. When A0 crosses the
  // threshold, LPCOMP sets EVENTS_UP in hardware; the next SysTick wakes the
  // CPU and pollUp() will return true on the very next loop iteration.
  cpuSleep();
}
