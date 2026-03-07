#include <Arduino.h>
#include <nrf54l15.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static PowerManager g_powerManager;

// Datasheet-guided low-power strategy:
// - Stay in variable-latency low-power mode in System ON.
// - Run CPU at 64 MHz for this control workload.
// - Enter System OFF and wake from GPIO DETECT (user button).

static constexpr uint8_t kSystemOffMagic = 0xA5U;
static constexpr uint32_t kAutoSystemOffDelayMs = 5000UL;
static constexpr uint32_t kHeartbeatMs = 1500UL;
static constexpr uint32_t kLedPulseMs = 5UL;

#ifdef NRF_TRUSTZONE_NONSECURE
static constexpr uintptr_t kPowerBase = 0x4010E000UL;
static constexpr uintptr_t kResetBase = 0x4010E000UL;
#else
static constexpr uintptr_t kPowerBase = 0x5010E000UL;
static constexpr uintptr_t kResetBase = 0x5010E000UL;
#endif

static NRF_POWER_Type* const g_power =
    reinterpret_cast<NRF_POWER_Type*>(kPowerBase);
static NRF_RESET_Type* const g_reset =
    reinterpret_cast<NRF_RESET_Type*>(kResetBase);

static uint32_t g_bootMs = 0UL;
static uint32_t g_lastPulseMs = 0UL;

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

static uint32_t readResetReason() {
  return g_reset->RESETREAS;
}

static void clearResetReason(uint32_t mask) {
  g_reset->RESETREAS = mask;
}

static uint8_t readGpregret0() {
  return static_cast<uint8_t>(g_power->GPREGRET[0] &
                              POWER_GPREGRET_GPREGRET_Msk);
}

static void writeGpregret0(uint8_t value) {
  g_power->GPREGRET[0] = static_cast<uint32_t>(value);
}

static bool buttonPressed() {
  bool high = true;
  if (!Gpio::read(kPinUserButton, &high)) {
    return false;
  }
  return !high;
}

static void configureButtonSenseLowWake() {
  if (kPinUserButton.port != 0U) {
    return;
  }

  (void)Gpio::configure(kPinUserButton, GpioDirection::kInput, GpioPull::kDisabled);

  uint32_t cnf = NRF_P0->PIN_CNF[kPinUserButton.pin];
  cnf &= ~GPIO_PIN_CNF_SENSE_Msk;
  cnf |= (GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);
  NRF_P0->PIN_CNF[kPinUserButton.pin] = cnf;
}

static void enterSystemOff() {
  (void)Gpio::write(kPinUserLed, true);  // LED off (active-low)

  configureButtonSenseLowWake();
  requestLowPowerLatencyMode();
  writeGpregret0(kSystemOffMagic);

  Serial.println("Entering SYSTEM OFF. Wake by pressing USER button.");
  Serial.flush();
  delay(2);

  g_powerManager.systemOffNoRetention();
}

static void printResetSummary(uint32_t resetReason, uint8_t gpregret0) {
  Serial.print("resetreas=0x");
  Serial.print(resetReason, HEX);
  Serial.print(" gpregret0=0x");
  Serial.println(gpregret0, HEX);

  if ((resetReason & RESET_RESETREAS_OFF_Msk) != 0U &&
      gpregret0 == kSystemOffMagic) {
    Serial.println("Wake source: SYSTEM OFF -> GPIO DETECT (button)");
    return;
  }

  if ((resetReason & RESET_RESETREAS_OFF_Msk) != 0U) {
    Serial.println("Wake source: SYSTEM OFF (GPIO) without marker");
    return;
  }

  if (resetReason == 0U) {
    Serial.println("Reset reason: power-on or reason already cleared");
    return;
  }

  Serial.println("Reset reason: non-SYSTEM OFF reset path");
}

void setup() {
  Serial.begin(115200);
  delay(250);

  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::write(kPinUserLed, true);  // LED off (active-low)
  (void)Gpio::configure(kPinUserButton, GpioDirection::kInput, GpioPull::kDisabled);

  const uint32_t resetReason = readResetReason();
  const uint8_t gpregret0 = readGpregret0();

  clearResetReason(resetReason);
  writeGpregret0(0U);

  const bool freqOk = setCpuFreqRaw(OSCILLATORS_PLL_FREQ_FREQ_CK64M);
  requestLowPowerLatencyMode();

  Serial.println("LowPowerSystemOffWakeButton: started");
  Serial.print("cpu64=");
  Serial.println(freqOk ? "OK" : "FAIL");
  printResetSummary(resetReason, gpregret0);
  Serial.print("Auto SYSTEM OFF in ");
  Serial.print(kAutoSystemOffDelayMs);
  Serial.println(" ms, or press USER button now.");

  g_bootMs = millis();
  g_lastPulseMs = g_bootMs;
}

void loop() {
  const uint32_t now = millis();

  if (buttonPressed()) {
    delay(20);
    if (buttonPressed()) {
      enterSystemOff();
    }
  }

  if ((now - g_lastPulseMs) >= kHeartbeatMs) {
    g_lastPulseMs = now;
    (void)Gpio::write(kPinUserLed, false);
    delay(kLedPulseMs);
    (void)Gpio::write(kPinUserLed, true);
  }

  if ((now - g_bootMs) >= kAutoSystemOffDelayMs) {
    enterSystemOff();
  }

  cpuIdleWfi();
}
