#include "nrf54l15_hal_timebase_internal.h"

#include <Arduino.h>
#include <cmsis.h>
#include "nrf54l15_hal_support_internal.h"

extern uint32_t SystemCoreClock;
extern "C" void nrf54l15_core_prepare_system_off_wake_timebase(void)
    __attribute__((weak));
extern "C" void nrf54l15_core_prepare_system_off(void) __attribute__((weak));
extern "C" void nrf54l15_core_disable_system_off_retention(void)
    __attribute__((weak));

namespace xiao_nrf54l15::hal_internal {
using namespace nrf54l15;

namespace {

static constexpr uint16_t kSystemOffTimeoutLfclk = 5U;
static constexpr uint8_t kSystemOffWakeLeadLfclk = 4U;
static constexpr uint32_t kLfclkFrequencyHz = 32768UL;
static constexpr uint32_t kMaxCcLatchWaitUs = 77UL;
static constexpr uint32_t kSystemOffMinimumLatencyGuardUs = 1000UL;
static constexpr Pin kGrtcPwmPin{0U, 3U};
#if defined(ARDUINO_XIAO_NRF54L15)
static constexpr uint32_t kZephyrAllowedCcMaskXiao = 0x67UL;
static constexpr uint8_t kZephyrMainCcChannelXiao = 1U;
#endif

uint32_t lfclkStatSource(NRF_CLOCK_Type* clock) {
  return (clock->LFCLK.STAT & CLOCK_LFCLK_STAT_SRC_Msk) >>
         CLOCK_LFCLK_STAT_SRC_Pos;
}

bool lfclkRunningFrom(NRF_CLOCK_Type* clock, uint32_t src) {
  if (clock == nullptr) {
    return false;
  }

  const uint32_t stat = clock->LFCLK.STAT;
  const bool running =
      ((stat & CLOCK_LFCLK_STAT_STATE_Msk) >> CLOCK_LFCLK_STAT_STATE_Pos) ==
      CLOCK_LFCLK_STAT_STATE_Running;
  return running && (lfclkStatSource(clock) == src);
}

bool waitForLfclkStartedInternal(NRF_CLOCK_Type* clock, uint32_t expectedSrc,
                                 uint32_t spinLimit) {
  if (clock == nullptr) {
    return false;
  }

  while (spinLimit-- > 0U) {
    if (clock->EVENTS_LFCLKSTARTED != 0U &&
        lfclkRunningFrom(clock, expectedSrc)) {
      return true;
    }
  }
  return false;
}

void startLfclkSource(NRF_CLOCK_Type* clock, uint32_t src) {
  if (clock == nullptr) {
    return;
  }

  clock->EVENTS_LFCLKSTARTED = 0U;
  clock->LFCLK.SRC =
      ((src << CLOCK_LFCLK_SRC_SRC_Pos) & CLOCK_LFCLK_SRC_SRC_Msk);
  clock->TASKS_LFCLKSTART = CLOCK_TASKS_LFCLKSTART_TASKS_LFCLKSTART_Trigger;
}

bool lfclkStartAlreadyRequested(NRF_CLOCK_Type* clock, uint32_t expectedSrcCopy) {
  if (clock == nullptr) {
    return false;
  }

  const uint32_t srcCopy = clock->LFCLK.SRCCOPY;
  const uint32_t currentSrc =
      (srcCopy & CLOCK_LFCLK_SRCCOPY_SRC_Msk) >> CLOCK_LFCLK_SRCCOPY_SRC_Pos;
  return currentSrc == expectedSrcCopy;
}

bool restoreGrtcActiveAfterRead(NRF_GRTC_Type* grtc) {
  const uint32_t active =
      NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE & GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Msk;
  return active == (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_NotActive
                    << GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
}

void waitForSystemOffWakeLatch() {
  const uint32_t waitUs =
      (static_cast<uint32_t>(kSystemOffTimeoutLfclk) * 1000000UL) /
          kLfclkFrequencyHz +
      kMaxCcLatchWaitUs;
  busyWaitApproxUs(waitUs);
}

uint8_t systemOffWakeChannel() {
#if defined(ARDUINO_XIAO_NRF54L15)
  const uint32_t available =
      kZephyrAllowedCcMaskXiao & ~(1UL << kZephyrMainCcChannelXiao);
  uint8_t channel = kZephyrMainCcChannelXiao;
  if (tryAllocateHighestSetBit(available, &channel)) {
    return channel;
  }
  return kZephyrMainCcChannelXiao;
#else
  return 1U;
#endif
}

uint32_t systemOffMinimumLatencyUs() {
  return ((((uint32_t)kSystemOffTimeoutLfclk +
            (uint32_t)kSystemOffWakeLeadLfclk) *
           1000000UL) /
          kLfclkFrequencyHz) +
         kSystemOffMinimumLatencyGuardUs;
}

uint32_t clampSystemOffDelayUs(uint32_t delayUs) {
  const uint32_t minimumLatencyUs = systemOffMinimumLatencyUs();
  if (delayUs < minimumLatencyUs) {
    return minimumLatencyUs;
  }
  return delayUs;
}

bool grtcPwmPinMatches(const Pin& pin) {
  return isConnected(pin) && pin.port == kGrtcPwmPin.port &&
         pin.pin == kGrtcPwmPin.pin;
}

bool resolveArduinoPin(uint8_t arduinoPin, Pin* outPin) {
  if (outPin == nullptr) {
    return false;
  }

  uint8_t port = 0U;
  uint8_t pin = 0U;
  if (!pinToPortPin(arduinoPin, &port, &pin)) {
    return false;
  }

  outPin->port = port;
  outPin->pin = pin;
  return true;
}

bool grtcPwmReady(const NRF_GRTC_Type* grtc) {
  if (grtc == nullptr) {
    return false;
  }

  return ((grtc->STATUS.PWM & GRTC_STATUS_PWM_READY_Msk) >>
          GRTC_STATUS_PWM_READY_Pos) == GRTC_STATUS_PWM_READY_Ready;
}

bool waitForGrtcPwmReady(const NRF_GRTC_Type* grtc, uint32_t spinLimit) {
  while (spinLimit-- > 0U) {
    if (grtcPwmReady(grtc)) {
      return true;
    }
  }
  return false;
}

void configureGrtcClockAndMode(NRF_GRTC_Type* grtc, GrtcClockSource clockSource) {
  if (grtc == nullptr) {
    return;
  }

  if (clockSource == GrtcClockSource::kLfxo) {
    ensureLfxoRunning();
  }

  uint32_t clkcfg = grtc->CLKCFG;
  clkcfg &= ~GRTC_CLKCFG_CLKSEL_Msk;
  clkcfg |= (static_cast<uint32_t>(clockSource) << GRTC_CLKCFG_CLKSEL_Pos) &
            GRTC_CLKCFG_CLKSEL_Msk;
  grtc->CLKCFG = clkcfg;

  uint32_t mode = grtc->MODE;
  mode &= ~(GRTC_MODE_AUTOEN_Msk | GRTC_MODE_SYSCOUNTEREN_Msk);
  mode |= (GRTC_MODE_AUTOEN_Default << GRTC_MODE_AUTOEN_Pos);
  mode |= (GRTC_MODE_SYSCOUNTEREN_Enabled << GRTC_MODE_SYSCOUNTEREN_Pos);
  grtc->MODE = mode;
}

void configureSystemOffWakeSleep(NRF_GRTC_Type* grtc) {
  uint32_t mode = grtc->MODE;
  mode &= ~GRTC_MODE_AUTOEN_Msk;
  mode &= ~GRTC_MODE_SYSCOUNTEREN_Msk;
  mode |= (GRTC_MODE_AUTOEN_Default << GRTC_MODE_AUTOEN_Pos);
  mode |= (GRTC_MODE_SYSCOUNTEREN_Disabled << GRTC_MODE_SYSCOUNTEREN_Pos);
  grtc->MODE = mode;
  __asm volatile("dsb 0xF" ::: "memory");

  uint32_t clkcfg = grtc->CLKCFG;
  clkcfg &= ~GRTC_CLKCFG_CLKSEL_Msk;
  clkcfg |= (GRTC_CLKCFG_CLKSEL_LFXO << GRTC_CLKCFG_CLKSEL_Pos);
  grtc->CLKCFG = clkcfg;

  grtc->TIMEOUT = ((static_cast<uint32_t>(kSystemOffTimeoutLfclk)
                    << GRTC_TIMEOUT_VALUE_Pos) &
                   GRTC_TIMEOUT_VALUE_Msk);
  grtc->WAKETIME = ((static_cast<uint32_t>(kSystemOffWakeLeadLfclk)
                     << GRTC_WAKETIME_VALUE_Pos) &
                    GRTC_WAKETIME_VALUE_Msk);

  mode &= ~GRTC_MODE_SYSCOUNTEREN_Msk;
  mode |= (GRTC_MODE_SYSCOUNTEREN_Enabled << GRTC_MODE_SYSCOUNTEREN_Pos);
  grtc->MODE = mode;
  __asm volatile("dsb 0xF" ::: "memory");
}

void armSystemOffWakeCompare(NRF_GRTC_Type* grtc, uint8_t wakeChannel,
                             uint64_t wakeTimestamp) {
  grtc->EVENTS_COMPARE[wakeChannel] = 0U;
  grtc->CC[wakeChannel].CCEN =
      (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
  grtc->CC[wakeChannel].CCL =
      static_cast<uint32_t>(wakeTimestamp & 0xFFFFFFFFULL);
  grtc->CC[wakeChannel].CCH =
      (static_cast<uint32_t>((wakeTimestamp >> 32U) & 0xFFFFFUL)
       << GRTC_CC_CCH_CCH_Pos) &
      GRTC_CC_CCH_CCH_Msk;
  NRF54L15_GRTC_INTENSET_REG(grtc) = (1UL << static_cast<uint32_t>(wakeChannel));
  grtc->CC[wakeChannel].CCEN =
      (GRTC_CC_CCEN_ACTIVE_Enable << GRTC_CC_CCEN_ACTIVE_Pos);
}

void clearSystemOffVprRetention() {
  if (MEMCONF_POWER_MaxCount > 1U) {
    NRF_MEMCONF->POWER[1U].RET &= ~MEMCONF_POWER_RET_MEM0_Msk;
  }
}

[[noreturn]] void enterSystemOff(NRF_RESET_Type* reset,
                                 NRF_REGULATORS_Type* regulators,
                                 bool disableRamRetention) {
  if (nrf54l15_core_prepare_system_off != nullptr) {
    nrf54l15_core_prepare_system_off();
  }
  clearSystemOffVprRetention();
  if (disableRamRetention &&
      nrf54l15_core_disable_system_off_retention != nullptr) {
    nrf54l15_core_disable_system_off_retention();
  }

  __asm volatile("cpsid i" ::: "memory");
  __asm volatile("dsb 0xF" ::: "memory");
  __asm volatile("isb 0xF" ::: "memory");
  reset->RESETREAS = 0xFFFFFFFFUL;
  __asm volatile("dsb 0xF" ::: "memory");

  regulators->SYSTEMOFF = REGULATORS_SYSTEMOFF_SYSTEMOFF_Enter;
  __asm volatile("dsb 0xF" ::: "memory");
  while (true) {
    __asm volatile("wfe");
  }
}

}  // namespace

bool bleHfxoRunning() {
  return (((NRF_CLOCK->XO.STAT & CLOCK_XO_STAT_STATE_Msk) >>
           CLOCK_XO_STAT_STATE_Pos) == CLOCK_XO_STAT_STATE_Running);
}

bool grtcSyscounterReady(NRF_GRTC_Type* grtc) {
  if (grtc == nullptr) {
    return false;
  }

  (void)NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERL;
  __asm volatile("dsb 0xF" ::: "memory");
  const uint32_t high = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERH;
  return ((high & GRTC_SYSCOUNTER_SYSCOUNTERH_BUSY_Msk) >>
          GRTC_SYSCOUNTER_SYSCOUNTERH_BUSY_Pos) ==
         GRTC_SYSCOUNTER_SYSCOUNTERH_BUSY_Ready;
}

uint64_t readGrtcCounterPreserveActive(NRF_GRTC_Type* grtc) {
  if (grtc == nullptr) {
    return 0ULL;
  }

  const bool restoreActive = restoreGrtcActiveAfterRead(grtc);
  if (restoreActive) {
    grtc->TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;
    __asm volatile("dsb 0xF" ::: "memory");
    NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE =
        (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Active
         << GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
    __asm volatile("dsb 0xF" ::: "memory");
    while (!grtcSyscounterReady(grtc)) {
      __asm volatile("nop");
    }
  }

  uint64_t value = 0ULL;
  for (uint8_t i = 0; i < 32U; ++i) {
    const uint32_t hi0 = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERH;
    const uint32_t lo = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERL;
    const uint32_t hi1 = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERH;

    if ((hi0 & GRTC_SYSCOUNTER_SYSCOUNTERH_BUSY_Msk) != 0U ||
        (hi1 & GRTC_SYSCOUNTER_SYSCOUNTERH_BUSY_Msk) != 0U ||
        (hi1 & GRTC_SYSCOUNTER_SYSCOUNTERH_OVERFLOW_Msk) != 0U) {
      continue;
    }

    const uint32_t high0 = hi0 & GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk;
    const uint32_t high1 = hi1 & GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk;
    if (high0 != high1) {
      continue;
    }

    value = (static_cast<uint64_t>(high1) << 32U) | static_cast<uint64_t>(lo);
    break;
  }

  if (value == 0ULL) {
    const uint32_t hi = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERH &
                        GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk;
    const uint32_t lo = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERL;
    value = (static_cast<uint64_t>(hi) << 32U) | static_cast<uint64_t>(lo);
  }

  if (restoreActive) {
    NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE =
        (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_NotActive
         << GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
    __asm volatile("dsb 0xF" ::: "memory");
  }

  return value;
}

void ensureGrtcReady(NRF_GRTC_Type* grtc) {
  if (grtc == nullptr) {
    return;
  }

  const bool restoreActive = restoreGrtcActiveAfterRead(grtc);
  if (!restoreActive && grtcSyscounterReady(grtc)) {
    return;
  }

  grtc->TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  if (restoreActive) {
    NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE =
        (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Active
         << GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
    __asm volatile("dsb 0xF" ::: "memory");
  }

  while (!grtcSyscounterReady(grtc)) {
    __asm volatile("nop");
  }

  if (restoreActive) {
    NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE =
        (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_NotActive
         << GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
    __asm volatile("dsb 0xF" ::: "memory");
  }
}

uint64_t readGrtcCounter(NRF_GRTC_Type* grtc) {
  return readGrtcCounterPreserveActive(grtc);
}

void ensureLfxoRunning() {
  NRF_CLOCK_Type* const clock = NRF_CLOCK;
  if (clock == nullptr) {
    return;
  }

  if (lfclkRunningFrom(clock, CLOCK_LFCLK_STAT_SRC_LFXO)) {
    return;
  }

  static constexpr uint32_t kLfclkStartSpinLimit = 2000000UL;
  if (!lfclkStartAlreadyRequested(clock, CLOCK_LFCLK_SRCCOPY_SRC_LFXO)) {
    startLfclkSource(clock, CLOCK_LFCLK_SRC_SRC_LFXO);
  }
  (void)waitForLfclkStartedInternal(clock, CLOCK_LFCLK_STAT_SRC_LFXO,
                                    kLfclkStartSpinLimit);
}

void busyWaitApproxUs(uint32_t us) {
  uint32_t cyclesPerUs = SystemCoreClock / 1000000UL;
  if (cyclesPerUs == 0UL) {
    cyclesPerUs = 64UL;
  }

  uint32_t iterations = cyclesPerUs * us;
  if (iterations == 0UL) {
    iterations = 1UL;
  }

  while (iterations-- > 0UL) {
    __NOP();
  }
}

void programSystemOffWake(uint32_t delayUs) {
  delayUs = clampSystemOffDelayUs(delayUs);

  NRF_GRTC_Type* const grtc = NRF_GRTC;
  if (nrf54l15_core_prepare_system_off_wake_timebase != nullptr) {
    nrf54l15_core_prepare_system_off_wake_timebase();
  }
  ensureLfxoRunning();
  configureSystemOffWakeSleep(grtc);

  const uint8_t wakeChannel = systemOffWakeChannel();

  for (uint8_t channel = 0; channel < GRTC_CC_MaxCount; ++channel) {
    NRF54L15_GRTC_INTENCLR_REG(grtc) = (1UL << channel);
    if (channel != wakeChannel) {
      grtc->CC[channel].CCEN =
          (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
    }
  }

  ensureGrtcReady(grtc);
  const uint32_t minimumLatencyUs = systemOffMinimumLatencyUs();
  uint32_t wakeDelayUs = delayUs;

  for (uint8_t attempt = 0U; attempt < 2U; ++attempt) {
    const uint64_t wakeTimestamp = readGrtcCounter(grtc) + wakeDelayUs;
    armSystemOffWakeCompare(grtc, wakeChannel, wakeTimestamp);
    waitForSystemOffWakeLatch();

    if (grtc->EVENTS_COMPARE[wakeChannel] == 0U) {
      return;
    }

    const uint64_t now = readGrtcCounter(grtc);
    if (wakeTimestamp > now) {
      grtc->EVENTS_COMPARE[wakeChannel] = 0U;
      return;
    }

    wakeDelayUs += minimumLatencyUs;
  }
}

}  // namespace xiao_nrf54l15::hal_internal

namespace xiao_nrf54l15 {
using namespace hal_internal;
using namespace nrf54l15;

namespace {

GrtcPwm* g_activeGrtcPwm = nullptr;

IRQn_Type grtcPwmIrqNumber() {
#if NRF54L15_GRTC_IRQ_GROUP == 2U
  return GRTC_2_IRQn;
#elif NRF54L15_GRTC_IRQ_GROUP == 1U
  return GRTC_1_IRQn;
#else
  return GRTC_0_IRQn;
#endif
}

uint32_t grtcPwmPendingInterruptMask(NRF_GRTC_Type* grtc) {
  if (grtc == nullptr) {
    return 0U;
  }
#if NRF54L15_GRTC_IRQ_GROUP == 2U
  return grtc->INTPEND2;
#elif NRF54L15_GRTC_IRQ_GROUP == 1U
  return grtc->INTPEND1;
#else
  return grtc->INTPEND0;
#endif
}

}  // namespace

bool ClockControl::startHfxo(bool waitForTuned, uint32_t spinLimit) {
  auto* clock =
      reinterpret_cast<NRF_CLOCK_Type*>(static_cast<uintptr_t>(nrf54l15::CLOCK_BASE));
  if (clock == nullptr) {
    return false;
  }

  const bool alreadyRunning =
      (((clock->XO.STAT & CLOCK_XO_STAT_STATE_Msk) >> CLOCK_XO_STAT_STATE_Pos) ==
       CLOCK_XO_STAT_STATE_Running);
  if (alreadyRunning) {
    return true;
  }

  clock->EVENTS_XOSTARTED = 0U;
  clock->EVENTS_XOTUNED = 0U;
  clock->EVENTS_XOTUNEFAILED = 0U;
  clock->TASKS_XOSTART = CLOCK_TASKS_XOSTART_TASKS_XOSTART_Trigger;

  if (!waitForTuned) {
    return true;
  }

  if (spinLimit == 0U) {
    spinLimit = 1000000UL;
  }

  while (spinLimit-- > 0U) {
    const bool running =
        (((clock->XO.STAT & CLOCK_XO_STAT_STATE_Msk) >> CLOCK_XO_STAT_STATE_Pos) ==
         CLOCK_XO_STAT_STATE_Running);
    if (running || (clock->EVENTS_XOSTARTED != 0U) || (clock->EVENTS_XOTUNED != 0U)) {
      return true;
    }
    if (clock->EVENTS_XOTUNEFAILED != 0U) {
      return false;
    }
  }

  return (((clock->XO.STAT & CLOCK_XO_STAT_STATE_Msk) >> CLOCK_XO_STAT_STATE_Pos) ==
          CLOCK_XO_STAT_STATE_Running);
}

void ClockControl::stopHfxo() {
  auto* clock =
      reinterpret_cast<NRF_CLOCK_Type*>(static_cast<uintptr_t>(nrf54l15::CLOCK_BASE));
  if (clock == nullptr) {
    return;
  }
  clock->TASKS_XOSTOP = CLOCK_TASKS_XOSTOP_TASKS_XOSTOP_Trigger;
}

bool ClockControl::setCpuFrequency(CpuFrequency frequency) {
  return nrf54l15_core_set_cpu_frequency_hz(static_cast<uint32_t>(frequency));
}

CpuFrequency ClockControl::cpuFrequency() {
  return (nrf54l15_core_get_cpu_frequency_hz() >=
          static_cast<uint32_t>(CpuFrequency::k128MHz))
             ? CpuFrequency::k128MHz
             : CpuFrequency::k64MHz;
}

bool ClockControl::enableIdleCpuScaling(CpuFrequency idleFrequency) {
  return nrf54l15_core_set_idle_cpu_scaling_hz(
      static_cast<uint32_t>(idleFrequency), true);
}

void ClockControl::disableIdleCpuScaling() {
  (void)nrf54l15_core_set_idle_cpu_scaling_hz(0UL, false);
}

bool ClockControl::idleCpuScalingEnabled() {
  return nrf54l15_core_get_idle_cpu_scaling_enabled();
}

CpuFrequency ClockControl::idleCpuFrequency() {
  return (nrf54l15_core_get_idle_cpu_frequency_hz() >=
          static_cast<uint32_t>(CpuFrequency::k128MHz))
             ? CpuFrequency::k128MHz
             : CpuFrequency::k64MHz;
}

[[noreturn]] void PowerManager::systemOff() {
  enterSystemOff(reset_, regulators_, false);
}

[[noreturn]] void PowerManager::systemOffNoRetention() {
  enterSystemOff(reset_, regulators_, true);
}

[[noreturn]] void PowerManager::systemOffTimedWakeMs(uint32_t delayMs) {
  uint32_t delayUs = delayMs;
  if (delayUs > (0xFFFFFFFFUL / 1000UL)) {
    delayUs = 0xFFFFFFFFUL / 1000UL;
  }
  systemOffTimedWakeUs(delayUs * 1000UL);
}

[[noreturn]] void PowerManager::systemOffTimedWakeUs(uint32_t delayUs) {
  setLatencyMode(PowerLatencyMode::kLowPower);
  programSystemOffWake(delayUs);
  systemOff();
}

[[noreturn]] void PowerManager::systemOffTimedWakeMsNoRetention(
    uint32_t delayMs) {
  uint32_t delayUs = delayMs;
  if (delayUs > (0xFFFFFFFFUL / 1000UL)) {
    delayUs = 0xFFFFFFFFUL / 1000UL;
  }
  systemOffTimedWakeUsNoRetention(delayUs * 1000UL);
}

[[noreturn]] void PowerManager::systemOffTimedWakeUsNoRetention(
    uint32_t delayUs) {
  setLatencyMode(PowerLatencyMode::kLowPower);
  programSystemOffWake(delayUs);
  systemOffNoRetention();
}

Grtc::Grtc(uint32_t base, uint8_t compareChannelCount)
    : grtc_(reinterpret_cast<NRF_GRTC_Type*>(static_cast<uintptr_t>(base))),
      compareChannelCount_(compareChannelCount) {
  if (compareChannelCount_ > 12U) {
    compareChannelCount_ = 12U;
  }
}

bool Grtc::begin(GrtcClockSource clockSource) {
  configureGrtcClockAndMode(grtc_, clockSource);

  for (uint8_t ch = 0; ch < compareChannelCount_; ++ch) {
    grtc_->CC[ch].CCEN = (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
    grtc_->EVENTS_COMPARE[ch] = 0U;
  }

  grtc_->TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;
  return true;
}

void Grtc::end() {
  stop();
  for (uint8_t ch = 0; ch < compareChannelCount_; ++ch) {
    grtc_->CC[ch].CCEN = (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
    grtc_->EVENTS_COMPARE[ch] = 0U;
  }
}

void Grtc::start() { grtc_->TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger; }

void Grtc::stop() { grtc_->TASKS_STOP = GRTC_TASKS_STOP_TASKS_STOP_Trigger; }

void Grtc::clear() { grtc_->TASKS_CLEAR = GRTC_TASKS_CLEAR_TASKS_CLEAR_Trigger; }

uint64_t Grtc::counter() const {
  return readGrtcCounterPreserveActive(grtc_);
}

bool Grtc::setWakeLeadLfclk(uint8_t cycles) {
  if (cycles == 0U) {
    cycles = 1U;
  }
  grtc_->WAKETIME = static_cast<uint32_t>(cycles);
  return true;
}

bool Grtc::setCompareOffsetUs(uint8_t channel, uint32_t offsetUs,
                              bool enableChannel) {
  if (channel >= compareChannelCount_) {
    return false;
  }
  if (offsetUs == 0U) {
    offsetUs = 1U;
  }

  grtc_->EVENTS_COMPARE[channel] = 0U;
  grtc_->CC[channel].CCEN = (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);

  uint32_t value = offsetUs;
  if (value > (GRTC_CC_CCADD_VALUE_Msk >> GRTC_CC_CCADD_VALUE_Pos)) {
    value = (GRTC_CC_CCADD_VALUE_Msk >> GRTC_CC_CCADD_VALUE_Pos);
  }

  grtc_->CC[channel].CCADD =
      ((value << GRTC_CC_CCADD_VALUE_Pos) & GRTC_CC_CCADD_VALUE_Msk) |
      ((GRTC_CC_CCADD_REFERENCE_SYSCOUNTER << GRTC_CC_CCADD_REFERENCE_Pos) &
       GRTC_CC_CCADD_REFERENCE_Msk);

  if (enableChannel) {
    grtc_->CC[channel].CCEN = (GRTC_CC_CCEN_ACTIVE_Enable << GRTC_CC_CCEN_ACTIVE_Pos);
  }

  return true;
}

bool Grtc::setCompareAbsoluteUs(uint8_t channel, uint64_t timestampUs,
                                bool enableChannel) {
  if (channel >= compareChannelCount_) {
    return false;
  }

  const uint32_t lo = static_cast<uint32_t>(timestampUs & 0xFFFFFFFFULL);
  const uint32_t hi = static_cast<uint32_t>((timestampUs >> 32U) & 0xFFFFFUL);

  grtc_->EVENTS_COMPARE[channel] = 0U;
  grtc_->CC[channel].CCEN = (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
  grtc_->CC[channel].CCL = lo;
  grtc_->CC[channel].CCH = (hi << GRTC_CC_CCH_CCH_Pos) & GRTC_CC_CCH_CCH_Msk;

  if (enableChannel) {
    grtc_->CC[channel].CCEN = (GRTC_CC_CCEN_ACTIVE_Enable << GRTC_CC_CCEN_ACTIVE_Pos);
  }

  return true;
}

bool Grtc::enableCompareChannel(uint8_t channel, bool enable) {
  if (channel >= compareChannelCount_) {
    return false;
  }
  grtc_->CC[channel].CCEN = (enable ? GRTC_CC_CCEN_ACTIVE_Enable
                                     : GRTC_CC_CCEN_ACTIVE_Disable)
                            << GRTC_CC_CCEN_ACTIVE_Pos;
  return true;
}

void Grtc::enableCompareInterrupt(uint8_t channel, bool enable) {
  if (channel >= compareChannelCount_) {
    return;
  }
  const uint32_t mask = (1UL << static_cast<uint32_t>(channel));
  if (enable) {
    NRF54L15_GRTC_INTENSET_REG(grtc_) = mask;
  } else {
    NRF54L15_GRTC_INTENCLR_REG(grtc_) = mask;
  }
}

bool Grtc::pollCompare(uint8_t channel, bool clearEventFlag) {
  if (channel >= compareChannelCount_) {
    return false;
  }

  const bool fired = (grtc_->EVENTS_COMPARE[channel] != 0U);
  if (fired && clearEventFlag) {
    grtc_->EVENTS_COMPARE[channel] = 0U;
  }
  return fired;
}

bool Grtc::clearCompareEvent(uint8_t channel) {
  if (channel >= compareChannelCount_) {
    return false;
  }
  grtc_->EVENTS_COMPARE[channel] = 0U;
  return true;
}

GrtcPwm::GrtcPwm(uint32_t base)
    : grtc_(reinterpret_cast<NRF_GRTC_Type*>(static_cast<uintptr_t>(base))),
      outPin_(kPinDisconnected),
      savedPinCnf_(0U),
      savedOutputHigh_(false),
      pinOwned_(false),
      running_(false),
      duty8_(0U),
      irqCallback_(nullptr),
      irqContext_(nullptr) {}

bool GrtcPwm::supportsPin(const Pin& outPin) {
  return grtcPwmPinMatches(outPin);
}

bool GrtcPwm::supportsArduinoPin(uint8_t arduinoPin) {
  Pin pin = kPinDisconnected;
  return resolveArduinoPin(arduinoPin, &pin) && supportsPin(pin);
}

bool GrtcPwm::takePin(const Pin& outPin) {
  if (!supportsPin(outPin)) {
    return false;
  }

  const uint32_t base = gpioBaseForPort(outPin.port);
  if (base == 0U) {
    return false;
  }

  if (pinOwned_) {
    const bool samePin = outPin_.port == outPin.port && outPin_.pin == outPin.pin;
    if (!samePin) {
      restorePin();
    } else {
      return true;
    }
  }

  const uint32_t pinBit = (1UL << outPin.pin);
  const uint32_t cnfAddr =
      base + gpio::PIN_CNF + (static_cast<uint32_t>(outPin.pin) * sizeof(uint32_t));
  savedPinCnf_ = reg32(cnfAddr);
  savedOutputHigh_ = (reg32(base + gpio::OUT) & pinBit) != 0U;

  uint32_t cnf = savedPinCnf_;
  cnf &= ~(GPIO_PIN_CNF_DIR_Msk | GPIO_PIN_CNF_INPUT_Msk |
           GPIO_PIN_CNF_PULL_Msk | GPIO_PIN_CNF_CTRLSEL_Msk);
  cnf |= (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos);
  cnf |= GPIO_PIN_CNF_INPUT_Disconnect;
  cnf |= GPIO_PIN_CNF_PULL_Disabled;
  cnf |= ((GPIO_PIN_CNF_CTRLSEL_GRTC << GPIO_PIN_CNF_CTRLSEL_Pos) &
          GPIO_PIN_CNF_CTRLSEL_Msk);

  reg32(base + gpio::DIRSET) = pinBit;
  reg32(cnfAddr) = cnf;

  outPin_ = outPin;
  pinOwned_ = true;
  return true;
}

void GrtcPwm::restorePin() {
  if (!pinOwned_) {
    return;
  }

  const uint32_t base = gpioBaseForPort(outPin_.port);
  if (base != 0U) {
    const uint32_t pinBit = (1UL << outPin_.pin);
    if (savedOutputHigh_) {
      reg32(base + gpio::OUTSET) = pinBit;
    } else {
      reg32(base + gpio::OUTCLR) = pinBit;
    }
    const uint32_t cnfAddr = base + gpio::PIN_CNF +
                             (static_cast<uint32_t>(outPin_.pin) *
                              sizeof(uint32_t));
    reg32(cnfAddr) = savedPinCnf_;
  }

  outPin_ = kPinDisconnected;
  pinOwned_ = false;
}

bool GrtcPwm::waitReady(uint32_t spinLimit) const {
  return waitForGrtcPwmReady(grtc_, spinLimit);
}

bool GrtcPwm::begin(const Pin& outPin, uint8_t duty8,
                    GrtcClockSource clockSource, bool startNow,
                    uint32_t spinLimit) {
  end(spinLimit);

  if (!takePin(outPin)) {
    return false;
  }

  configureGrtcClockAndMode(grtc_, clockSource);
  grtc_->TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  grtc_->EVENTS_PWMPERIODEND = 0U;
  grtc_->EVENTS_PWMREADY = 0U;

  duty8_ = duty8;
  running_ = false;
  if (!setDuty8(duty8)) {
    restorePin();
    return false;
  }

  if (!startNow) {
    return true;
  }

  if (!start(spinLimit)) {
    restorePin();
    return false;
  }
  return true;
}

bool GrtcPwm::beginArduinoPin(uint8_t arduinoPin, uint8_t duty8,
                              GrtcClockSource clockSource, bool startNow,
                              uint32_t spinLimit) {
  Pin pin = kPinDisconnected;
  if (!resolveArduinoPin(arduinoPin, &pin)) {
    return false;
  }
  return begin(pin, duty8, clockSource, startNow, spinLimit);
}

bool GrtcPwm::setDuty8(uint8_t duty8) {
  if (!pinOwned_) {
    return false;
  }

  grtc_->PWMCONFIG =
      (static_cast<uint32_t>(duty8) << GRTC_PWMCONFIG_COMPAREVALUE_Pos) &
      GRTC_PWMCONFIG_COMPAREVALUE_Msk;
  duty8_ = duty8;
  return true;
}

bool GrtcPwm::setDutyPermille(uint16_t dutyPermille) {
  if (dutyPermille > 1000U) {
    dutyPermille = 1000U;
  }

  const uint32_t scaled = static_cast<uint32_t>(dutyPermille) * 255UL + 500UL;
  return setDuty8(static_cast<uint8_t>(scaled / 1000UL));
}

uint8_t GrtcPwm::duty8() const { return duty8_; }

bool GrtcPwm::ready() const { return grtcPwmReady(grtc_); }

bool GrtcPwm::pollReadyEvent(bool clearEventFlag) {
  const bool fired = (grtc_->EVENTS_PWMREADY != 0U);
  if (fired && clearEventFlag) {
    grtc_->EVENTS_PWMREADY = 0U;
  }
  return fired;
}

void GrtcPwm::enablePeriodEndEvent(bool enable) {
  if (enable) {
    grtc_->EVTENSET = GRTC_EVTENSET_PWMPERIODEND_Msk;
  } else {
    grtc_->EVTENCLR = GRTC_EVTENCLR_PWMPERIODEND_Msk;
  }
  grtc_->EVENTS_PWMPERIODEND = 0U;
}

volatile uint32_t* GrtcPwm::publishReadyConfigRegister() const {
  return &grtc_->PUBLISH_PWMREADY;
}

uint32_t GrtcPwm::pendingInterruptMask() const {
  if (!pinOwned_) {
    return 0U;
  }
  return grtcPwmPendingInterruptMask(grtc_) & irqSupportedMask();
}

void GrtcPwm::clearInterruptEvents(uint32_t mask) {
  if (!pinOwned_) {
    return;
  }

  mask &= irqSupportedMask();
  if ((mask & kIrqPeriodEnd) != 0U) {
    grtc_->EVENTS_PWMPERIODEND = 0U;
  }
  if ((mask & kIrqReady) != 0U) {
    grtc_->EVENTS_PWMREADY = 0U;
  }
}

void GrtcPwm::enableInterruptMask(uint32_t mask, bool enable) {
  if (!pinOwned_) {
    return;
  }

  mask &= irqSupportedMask();
  if (mask == 0U) {
    return;
  }

  if (enable) {
    if ((mask & kIrqPeriodEnd) != 0U) {
      enablePeriodEndEvent(true);
    }
    clearInterruptEvents(mask);
    NRF54L15_GRTC_INTENSET_REG(grtc_) = mask;
  } else {
    NRF54L15_GRTC_INTENCLR_REG(grtc_) = mask;
    if ((mask & kIrqPeriodEnd) != 0U) {
      enablePeriodEndEvent(false);
    }
  }
}

void GrtcPwm::setIrqCallback(IrqCallback callback, void* context) {
  irqCallback_ = callback;
  irqContext_ = context;
}

bool GrtcPwm::makeActive(uint8_t irqPriority) {
  if (!pinOwned_) {
    return false;
  }

  g_activeGrtcPwm = this;
  NVIC_ClearPendingIRQ(grtcPwmIrqNumber());
  NVIC_SetPriority(grtcPwmIrqNumber(), irqPriority);
  NVIC_EnableIRQ(grtcPwmIrqNumber());
  return true;
}

void GrtcPwm::onIrq() {
  if (!pinOwned_) {
    return;
  }

  const uint32_t pending = pendingInterruptMask();
  if (pending == 0U) {
    return;
  }

  clearInterruptEvents(pending);
  if (irqCallback_ != nullptr) {
    irqCallback_(pending, irqContext_);
  }
}

bool GrtcPwm::start(uint32_t spinLimit) {
  if (!pinOwned_ || !waitReady(spinLimit)) {
    return false;
  }

  grtc_->EVENTS_PWMREADY = 0U;
  grtc_->TASKS_PWMSTART = GRTC_TASKS_PWMSTART_TASKS_PWMSTART_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  if (!waitReady(spinLimit)) {
    return false;
  }

  running_ = true;
  return true;
}

bool GrtcPwm::stop(uint32_t spinLimit) {
  if (!pinOwned_) {
    return false;
  }
  if (!running_) {
    return waitReady(spinLimit);
  }
  if (!waitReady(spinLimit)) {
    return false;
  }

  grtc_->EVENTS_PWMREADY = 0U;
  grtc_->TASKS_PWMSTOP = GRTC_TASKS_PWMSTOP_TASKS_PWMSTOP_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  if (!waitReady(spinLimit)) {
    return false;
  }

  running_ = false;
  return true;
}

bool GrtcPwm::pollPeriodEnd(bool clearEventFlag) {
  const bool fired = (grtc_->EVENTS_PWMPERIODEND != 0U);
  if (fired && clearEventFlag) {
    grtc_->EVENTS_PWMPERIODEND = 0U;
  }
  return fired;
}

void GrtcPwm::end(uint32_t spinLimit) {
  if (!pinOwned_) {
    return;
  }

  enableInterruptMask(irqSupportedMask(), false);
  clearInterruptEvents(irqSupportedMask());
  enablePeriodEndEvent(false);
  (void)stop(spinLimit);
  if (g_activeGrtcPwm == this) {
    g_activeGrtcPwm = nullptr;
  }
  restorePin();
  running_ = false;
}

extern "C" void nrf54l15_grtc_pwm_irq_service(void) {
  if (g_activeGrtcPwm != nullptr) {
    g_activeGrtcPwm->onIrq();
  }
}

}  // namespace xiao_nrf54l15
