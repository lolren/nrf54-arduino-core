#include <Arduino.h>
#include <nrf54l15.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static PowerManager g_powerManager;
static Lpcomp g_lpcomp;

__attribute__((section(".noinit"))) static uint32_t g_retainedMagic;
__attribute__((section(".noinit"))) static uint32_t g_retainedBootCount;

static constexpr uint32_t kRetentionMagic = 0x4C50434DU;  // "LPCM"
static constexpr uint16_t kWakeThresholdPermille = 500U;
static constexpr uint32_t kArmDelayMs = 2000UL;

static bool wokeFromLpcomp(uint32_t resetReason) {
  return (resetReason & RESET_RESETREAS_LPCOMP_Msk) != 0U;
}

static void pulseLed(uint8_t count) {
  for (uint8_t i = 0U; i < count; ++i) {
    (void)Gpio::write(kPinUserLed, false);
    delay(60);
    (void)Gpio::write(kPinUserLed, true);
    delay(120);
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::write(kPinUserLed, true);

  if (g_retainedMagic != kRetentionMagic) {
    g_retainedMagic = kRetentionMagic;
    g_retainedBootCount = 0U;
  }
  ++g_retainedBootCount;

  const uint32_t resetReason = g_powerManager.resetReason();
  g_powerManager.clearResetReason(resetReason);

  Serial.println("LpcompSystemOffWake");
  Serial.println("Wire A0 low at sleep entry, then drive it above the threshold to wake.");
  Serial.print("boot count: ");
  Serial.println(g_retainedBootCount);
  Serial.print("resetreas=0x");
  Serial.println(resetReason, HEX);

  if (wokeFromLpcomp(resetReason)) {
    Serial.println("Wake source: LPCOMP analog detect");
    pulseLed(3U);
  } else {
    Serial.println("Wake source: non-LPCOMP reset path");
    pulseLed(1U);
  }

  if (!g_lpcomp.beginThreshold(kPinA0, kWakeThresholdPermille, true,
                               LpcompDetect::kUp)) {
    Serial.println("LPCOMP begin failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.print("Arming LPCOMP and entering SYSTEM OFF in ");
  Serial.print(kArmDelayMs);
  Serial.println(" ms");
  Serial.flush();
  delay(kArmDelayMs);

  g_powerManager.systemOff();
}

void loop() {}
