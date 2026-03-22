/*
 * LPCOMP System OFF Wake Example
 *
 * This example demonstrates how to use the Low Power Comparator (LPCOMP)
 * to wake the nRF54L15 from System OFF mode (the deepest sleep state)
 * when an analog voltage crosses a threshold.
 *
 * Key Configuration:
 * - Wake Pin: A0 (analog input)
 * - Wake Threshold: set kVddMv (supply voltage) and kWakeThresholdMv in mV.
 *   The core converts mV to the nearest of the 16 fixed LPCOMP reference levels.
 *   Example: kVddMv=3300, kWakeThresholdMv=200 → wakes when A0 crosses ~200 mV.
 * - Detection Edge: Rising (LpcompDetect::kUp)
 * - Hysteresis: Enabled (prevents chatter)
 *
 * Hardware Setup:
 * 1. Connect a voltage source to Pin A0
 * 2. Keep A0 below threshold when entering sleep
 * 3. Raise A0 above threshold to wake the device
 *
 * LED Indicators:
 * - 3 pulses: Woke from LPCOMP trigger
 * - 1 pulse:  Woke from other reset source
 *
 * Retained Memory:
 * - Boot count survives System OFF and increments each wake
 */

#include <Arduino.h>
#include <nrf54l15.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static PowerManager g_powerManager;
static Lpcomp g_lpcomp;

__attribute__((section(".noinit"))) static uint32_t g_retainedMagic;
__attribute__((section(".noinit"))) static uint32_t g_retainedBootCount;

// Configuration constants
static constexpr uint32_t kRetentionMagic = 0x4C50434DU;  // "LPCM" - Magic value to validate retained memory
// Supply voltage. Set this to match your actual VDD (USB: 3300, LiPo: 3700, 1.8V rail: 1800).
// The LPCOMP threshold is always a fraction of VDD, so specifying VDD lets you
// think in millivolts rather than permille.
static constexpr uint16_t kVddMv = 3300U;        // VDD in mV (3.3 V via USB/regulator)
static constexpr uint16_t kWakeThresholdMv = 200U;  // Wake when A0 exceeds this voltage (mV)
static constexpr uint32_t kArmDelayMs = 2000UL;     // Delay before entering System OFF (ms)

// Check if the wake-up was triggered by LPCOMP (Low Power Comparator)
static bool wokeFromLpcomp(uint32_t resetReason) {
  return (resetReason & RESET_RESETREAS_LPCOMP_Msk) != 0U;
}

// Pulse the user LED the specified number of times
// LED on kPinUserLed (active low)
static void pulseLed(uint8_t count) {
  for (uint8_t i = 0U; i < count; ++i) {
    (void)Gpio::write(kPinUserLed, false);  // LED on (active low)
    delay(60);
    (void)Gpio::write(kPinUserLed, true);   // LED off
    delay(120);
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  // Configure user LED (active low)
  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::write(kPinUserLed, true);  // LED off initially

  // Initialize retained variables (survive System OFF)
  if (g_retainedMagic != kRetentionMagic) {
    g_retainedMagic = kRetentionMagic;
    g_retainedBootCount = 0U;
  }
  ++g_retainedBootCount;

  // Get and clear reset reason
  const uint32_t resetReason = g_powerManager.resetReason();
  g_powerManager.clearResetReason(resetReason);

  Serial.println("LpcompSystemOffWake");
  Serial.println("Wire A0 low at sleep entry, then drive it above the threshold to wake.");
  Serial.print("boot count: ");
  Serial.println(g_retainedBootCount);
  Serial.print("resetreas=0x");
  Serial.println(resetReason, HEX);

  // Indicate wake source via LED pulses
  if (wokeFromLpcomp(resetReason)) {
    Serial.println("Wake source: LPCOMP analog detect");
    pulseLed(3U);  // 3 pulses for LPCOMP wake
  } else {
    Serial.println("Wake source: non-LPCOMP reset path");
    pulseLed(1U);  // 1 pulse for other reset
  }

  // Configure LPCOMP (Low Power Comparator) for analog wake detection.
  // beginThresholdMv() converts kWakeThresholdMv to the nearest of the 16
  // fixed hardware reference levels (fractions of VDD).
  //   kPinA0              - Analog input pin to monitor (A0 = P1.04)
  //   kVddMv              - Your supply voltage in mV
  //   kWakeThresholdMv    - Desired wake threshold in mV
  //   true                - Enable hysteresis to prevent chatter
  //   LpcompDetect::kUp   - Trigger on rising edge (voltage going above threshold)
  Serial.print("Configuring LPCOMP on A0: VDD=");
  Serial.print(kVddMv);
  Serial.print("mV threshold=");
  Serial.print(kWakeThresholdMv);
  Serial.println("mV");
  if (!g_lpcomp.beginThresholdMv(kPinA0, kVddMv, kWakeThresholdMv, true,
                                 LpcompDetect::kUp)) {
    Serial.println("LPCOMP begin failed");
    while (true) {
      delay(1000);
    }
  }
  Serial.println("LPCOMP configured successfully");

  // Enter System OFF mode - lowest power state
  // Device will only wake when LPCOMP detects voltage above threshold on Pin A0
  Serial.print("Arming LPCOMP and entering SYSTEM OFF in ");
  Serial.print(kArmDelayMs);
  Serial.println(" ms");
  Serial.flush();
  delay(kArmDelayMs);

  g_powerManager.systemOff();
}

void loop() {}
