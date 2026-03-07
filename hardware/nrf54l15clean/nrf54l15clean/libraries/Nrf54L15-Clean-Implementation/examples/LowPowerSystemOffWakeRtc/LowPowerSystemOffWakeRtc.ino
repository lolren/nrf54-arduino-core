#include <Arduino.h>
#include <nrf54l15.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

// Datasheet-guided low-power strategy:
// - Keep CPU at 64 MHz and variable-latency low-power mode in System ON.
// - Program GRTC compare as wake source.
// - Enter true System OFF through REGULATORS.

static constexpr uint8_t kSystemOffRtcMagic = 0x5CU;
static constexpr uint8_t kGrtcWakeChannel = 0U;
static constexpr uint32_t kEnterSystemOffAfterMs = 4000UL;
static constexpr uint32_t kWakeDelayMs = 12000UL;
static constexpr uint32_t kBlinkPeriodMs = 1200UL;
static constexpr uint32_t kBlinkOnMs = 6UL;

#ifdef NRF_TRUSTZONE_NONSECURE
static constexpr uintptr_t kPowerBase = 0x4010E000UL;
static constexpr uintptr_t kResetBase = 0x4010E000UL;
static constexpr uintptr_t kRegulatorsBase = 0x40120000UL;
static constexpr uintptr_t kGrtcBase = 0x400E2000UL;
#else
static constexpr uintptr_t kPowerBase = 0x5010E000UL;
static constexpr uintptr_t kResetBase = 0x5010E000UL;
static constexpr uintptr_t kRegulatorsBase = 0x50120000UL;
static constexpr uintptr_t kGrtcBase = 0x500E2000UL;
#endif

static NRF_POWER_Type* const g_power =
    reinterpret_cast<NRF_POWER_Type*>(kPowerBase);
static NRF_RESET_Type* const g_reset =
    reinterpret_cast<NRF_RESET_Type*>(kResetBase);
static NRF_REGULATORS_Type* const g_regulators =
    reinterpret_cast<NRF_REGULATORS_Type*>(kRegulatorsBase);
static NRF_GRTC_Type* const g_grtc =
    reinterpret_cast<NRF_GRTC_Type*>(kGrtcBase);

static uint32_t g_bootMs = 0UL;
static uint32_t g_lastBlinkMs = 0UL;

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

static void requestLowPowerLatencyMode() {
  g_power->TASKS_LOWPWR = POWER_TASKS_LOWPWR_TASKS_LOWPWR_Trigger;
}

static uint8_t readGpregret0() {
  return static_cast<uint8_t>(g_power->GPREGRET[0] &
                              POWER_GPREGRET_GPREGRET_Msk);
}

static void writeGpregret0(uint8_t value) {
  g_power->GPREGRET[0] = static_cast<uint32_t>(value);
}

static void scheduleGrtcWakeFromNowUs(uint32_t delayUs) {
  uint32_t clampedUs = delayUs;
  if (clampedUs > GRTC_CC_CCADD_VALUE_Msk) {
    clampedUs = GRTC_CC_CCADD_VALUE_Msk;
  }

  // Keep default divider and pick a low-frequency source for wake scheduling.
  uint32_t clkcfg = g_grtc->CLKCFG;
  clkcfg &= ~GRTC_CLKCFG_CLKSEL_Msk;
  clkcfg |= (GRTC_CLKCFG_CLKSEL_SystemLFCLK << GRTC_CLKCFG_CLKSEL_Pos);
  g_grtc->CLKCFG = clkcfg;

  // Program compare channel as an offset from current SYSCOUNTER.
  g_grtc->CC[kGrtcWakeChannel].CCEN =
      (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
  g_grtc->EVENTS_COMPARE[kGrtcWakeChannel] = 0U;
  g_grtc->WAKETIME = 4U;

  g_grtc->MODE = (GRTC_MODE_AUTOEN_Default << GRTC_MODE_AUTOEN_Pos) |
                 (GRTC_MODE_SYSCOUNTEREN_Enabled << GRTC_MODE_SYSCOUNTEREN_Pos);
  g_grtc->TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;

  g_grtc->CC[kGrtcWakeChannel].CCADD =
      ((clampedUs << GRTC_CC_CCADD_VALUE_Pos) & GRTC_CC_CCADD_VALUE_Msk) |
      (GRTC_CC_CCADD_REFERENCE_SYSCOUNTER << GRTC_CC_CCADD_REFERENCE_Pos);
  g_grtc->CC[kGrtcWakeChannel].CCEN =
      (GRTC_CC_CCEN_ACTIVE_Enable << GRTC_CC_CCEN_ACTIVE_Pos);

  // Keep compare interrupt armed as the canonical compare signaling path.
  g_grtc->INTENSET0 = (GRTC_INTENSET0_COMPARE0_Set << kGrtcWakeChannel);
}

static void printResetSummary(uint32_t resetReason, uint8_t gpregret0) {
  Serial.print("resetreas=0x");
  Serial.print(resetReason, HEX);
  Serial.print(" gpregret0=0x");
  Serial.println(gpregret0, HEX);

  if ((resetReason & RESET_RESETREAS_GRTC_Msk) != 0U &&
      gpregret0 == kSystemOffRtcMagic) {
    Serial.println("Wake source: SYSTEM OFF -> GRTC compare");
    return;
  }

  if ((resetReason & RESET_RESETREAS_GRTC_Msk) != 0U) {
    Serial.println("Wake source: GRTC without marker");
    return;
  }

  if (resetReason == 0U) {
    Serial.println("Reset reason: power-on or reason already cleared");
    return;
  }

  Serial.println("Reset reason: non-GRTC reset path");
}

static void blinkOnce() {
  (void)Gpio::write(kPinUserLed, false);
  delay(kBlinkOnMs);
  (void)Gpio::write(kPinUserLed, true);
}

static void enterSystemOffWithRtcWake(uint32_t wakeDelayMs) {
  const uint32_t wakeDelayUs = wakeDelayMs * 1000UL;
  scheduleGrtcWakeFromNowUs(wakeDelayUs);

  writeGpregret0(kSystemOffRtcMagic);
  requestLowPowerLatencyMode();

  Serial.print("Entering SYSTEM OFF. GRTC wake in ");
  Serial.print(wakeDelayMs);
  Serial.println(" ms.");
  Serial.flush();
  delay(2);

  g_regulators->SYSTEMOFF = REGULATORS_SYSTEMOFF_SYSTEMOFF_Enter;
  __asm volatile("dsb 0xF" ::: "memory");
  while (true) {
    cpuIdleWfi();
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::write(kPinUserLed, true);  // LED off (active-low)
  (void)Gpio::configure(kPinUserButton, GpioDirection::kInput, GpioPull::kDisabled);

  const uint32_t resetReason = g_reset->RESETREAS;
  const uint8_t gpregret0 = readGpregret0();

  g_reset->RESETREAS = resetReason;
  writeGpregret0(0U);

  const bool freqOk = setCpuFreqRaw(OSCILLATORS_PLL_FREQ_FREQ_CK64M);
  requestLowPowerLatencyMode();

  Serial.println("LowPowerSystemOffWakeRtc: started");
  Serial.print("cpu64=");
  Serial.println(freqOk ? "OK" : "FAIL");
  printResetSummary(resetReason, gpregret0);
  Serial.print("Will enter SYSTEM OFF in ");
  Serial.print(kEnterSystemOffAfterMs);
  Serial.print(" ms and wake via GRTC after ");
  Serial.print(kWakeDelayMs);
  Serial.println(" ms.");

  g_bootMs = millis();
  g_lastBlinkMs = g_bootMs;
}

void loop() {
  const uint32_t now = millis();

  if ((now - g_lastBlinkMs) >= kBlinkPeriodMs) {
    g_lastBlinkMs = now;
    blinkOnce();
  }

  if ((now - g_bootMs) >= kEnterSystemOffAfterMs) {
    enterSystemOffWithRtcWake(kWakeDelayMs);
  }

  cpuIdleWfi();
}
