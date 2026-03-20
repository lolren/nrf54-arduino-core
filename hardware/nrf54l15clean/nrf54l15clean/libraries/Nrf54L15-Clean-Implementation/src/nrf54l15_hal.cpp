#include "nrf54l15_hal.h"

#include <Arduino.h>
#include <string.h>
#include <cmsis.h>
#include "variant.h"

extern uint32_t SystemCoreClock;
extern "C" void nrf54l15_core_prepare_system_off_wake_timebase(void)
    __attribute__((weak));
extern "C" void nrf54l15_core_prepare_system_off(void) __attribute__((weak));
extern "C" void nrf54l15_core_disable_system_off_retention(void)
    __attribute__((weak));
namespace xiao_nrf54l15 {
class I2sTx;
class I2sRx;
class I2sDuplex;
}

namespace {

using namespace nrf54l15;
using namespace xiao_nrf54l15;

static constexpr uint16_t kSystemOffTimeoutLfclk = 5U;
static constexpr uint8_t kSystemOffWakeLeadLfclk = 4U;
static constexpr uint32_t kLfclkFrequencyHz = 32768UL;
static constexpr uint32_t kMaxCcLatchWaitUs = 77UL;
static constexpr uint32_t kSystemOffMinimumLatencyGuardUs = 1000UL;
static constexpr uint32_t kGrtcStartSettleUs = 93UL;
#if defined(ARDUINO_XIAO_NRF54L15)
static constexpr uint32_t kZephyrAllowedCcMaskXiao = 0x67UL;
static constexpr uint8_t kZephyrMainCcChannelXiao = 1U;
#endif
xiao_nrf54l15::I2sTx* g_activeI2sTx = nullptr;
xiao_nrf54l15::I2sRx* g_activeI2sRx = nullptr;
xiao_nrf54l15::I2sDuplex* g_activeI2sDuplex = nullptr;

uint32_t gpioBaseForPort(uint8_t port) {
  switch (port) {
    case 0:
      return GPIO_P0_BASE;
    case 1:
      return GPIO_P1_BASE;
    case 2:
      return GPIO_P2_BASE;
    default:
      return 0;
  }
}

bool waitForEvent(uint32_t base, uint32_t eventOffset, uint32_t spinLimit) {
  while (spinLimit-- > 0) {
    if (reg32(base + eventOffset) != 0U) {
      return true;
    }
  }
  return false;
}

bool waitForEventOrError(uint32_t base, uint32_t eventOffset,
                         uint32_t errorOffset, uint32_t spinLimit) {
  while (spinLimit-- > 0) {
    if (reg32(base + eventOffset) != 0U) {
      return true;
    }
    if (reg32(base + errorOffset) != 0U) {
      return false;
    }
  }
  return false;
}

void clearEvent(uint32_t base, uint32_t eventOffset) {
  reg32(base + eventOffset) = 0;
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

bool waitForNonZero(volatile uint32_t* reg, uint32_t spinLimit) {
  if (reg == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    if (*reg != 0U) {
      return true;
    }
  }
  return false;
}

uint8_t allocateHighestSetBit(uint32_t mask) {
  return static_cast<uint8_t>(31U - static_cast<uint32_t>(__builtin_clz(mask)));
}

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

bool waitForLfclkStarted(NRF_CLOCK_Type* clock, uint32_t expectedSrc,
                         uint32_t spinLimit) {
  if (clock == nullptr) {
    return false;
  }

  while (spinLimit-- > 0U) {
    if (clock->EVENTS_LFCLKSTARTED != 0U && lfclkRunningFrom(clock, expectedSrc)) {
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
  clock->LFCLK.SRC = (src << CLOCK_LFCLK_SRC_SRC_Pos) & CLOCK_LFCLK_SRC_SRC_Msk;
  __asm volatile("dsb 0xF" ::: "memory");
  clock->TASKS_LFCLKSTART = CLOCK_TASKS_LFCLKSTART_TASKS_LFCLKSTART_Trigger;
}

bool lfclkStartAlreadyRequested(NRF_CLOCK_Type* clock, uint32_t src) {
  if (clock == nullptr) {
    return false;
  }

  const uint32_t run = (clock->LFCLK.RUN & CLOCK_LFCLK_RUN_STATUS_Msk) >>
                       CLOCK_LFCLK_RUN_STATUS_Pos;
  const uint32_t requestedSrc =
      (clock->LFCLK.SRCCOPY & CLOCK_LFCLK_SRCCOPY_SRC_Msk) >>
      CLOCK_LFCLK_SRCCOPY_SRC_Pos;
  return (run == CLOCK_LFCLK_RUN_STATUS_Triggered) && (requestedSrc == src);
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

void ensureGrtcReady(NRF_GRTC_Type* grtc) {
  if (grtc == nullptr) {
    return;
  }

  grtc->TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  delayMicroseconds(kGrtcStartSettleUs);

  const uint32_t active =
      NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE & GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Msk;
  const bool restoreActive =
      active == (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_NotActive
                 << GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
  if (restoreActive) {
    NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE =
        (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Active << GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
    __asm volatile("dsb 0xF" ::: "memory");
  }

  while (!grtcSyscounterReady(grtc)) {
    __asm volatile("nop");
  }

  if (restoreActive) {
    NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE =
        (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_NotActive << GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
    __asm volatile("dsb 0xF" ::: "memory");
  }
}

uint64_t readGrtcCounter(NRF_GRTC_Type* grtc) {
  if (grtc == nullptr) {
    return 0U;
  }

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

    return (static_cast<uint64_t>(high1) << 32U) | static_cast<uint64_t>(lo);
  }

  const uint32_t hi = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERH &
                      GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk;
  const uint32_t lo = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERL;
  return (static_cast<uint64_t>(hi) << 32U) | static_cast<uint64_t>(lo);
}

void ensureLfxoRunning() {
  NRF_CLOCK_Type* const clock = NRF_CLOCK;

  if (lfclkRunningFrom(clock, CLOCK_LFCLK_STAT_SRC_LFXO)) {
    return;
  }

  static constexpr uint32_t kLfclkStartSpinLimit = 2000000UL;
  if (!lfclkStartAlreadyRequested(clock, CLOCK_LFCLK_SRCCOPY_SRC_LFXO)) {
    startLfclkSource(clock, CLOCK_LFCLK_SRC_SRC_LFXO);
  }
  if (!waitForLfclkStarted(clock, CLOCK_LFCLK_STAT_SRC_LFXO,
                           kLfclkStartSpinLimit)) {
    return;
  }
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

void waitForSystemOffWakeLatch() {
  const uint32_t waitUs =
      (static_cast<uint32_t>(kSystemOffTimeoutLfclk) * 1000000UL) /
          kLfclkFrequencyHz +
      kMaxCcLatchWaitUs;
  busyWaitApproxUs(waitUs);
}

uint8_t systemOffWakeChannel() {
#if defined(ARDUINO_XIAO_NRF54L15)
  const uint32_t available = kZephyrAllowedCcMaskXiao & ~(1UL << kZephyrMainCcChannelXiao);
  return allocateHighestSetBit(available);
#else
  return 1U;
#endif
}

uint32_t systemOffMinimumLatencyUs() {
  return ((((uint32_t)kSystemOffTimeoutLfclk +
            (uint32_t)kSystemOffWakeLeadLfclk) * 1000000UL) /
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

void configureSystemOffWakeSleep(NRF_GRTC_Type* grtc) {
  uint32_t mode = grtc->MODE;
  mode &= ~(GRTC_MODE_AUTOEN_Msk | GRTC_MODE_SYSCOUNTEREN_Msk);
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
  NRF54L15_GRTC_INTENSET_REG(grtc) = (1UL << wakeChannel);
  grtc->CC[wakeChannel].CCEN =
      (GRTC_CC_CCEN_ACTIVE_Enable << GRTC_CC_CCEN_ACTIVE_Pos);
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

double adcGainValue(xiao_nrf54l15::AdcGain gain) {
  using xiao_nrf54l15::AdcGain;
  switch (gain) {
    case AdcGain::k2:
      return 2.0;
    case AdcGain::k1:
      return 1.0;
    case AdcGain::k2over3:
      return 2.0 / 3.0;
    case AdcGain::k2over4:
      return 2.0 / 4.0;
    case AdcGain::k2over5:
      return 2.0 / 5.0;
    case AdcGain::k2over6:
      return 2.0 / 6.0;
    case AdcGain::k2over7:
      return 2.0 / 7.0;
    case AdcGain::k2over8:
    default:
      return 2.0 / 8.0;
  }
}

uint8_t adcResolutionBits(xiao_nrf54l15::AdcResolution resolution) {
  return static_cast<uint8_t>(8U + (static_cast<uint8_t>(resolution) * 2U));
}

int32_t adcRawToMilliVolts(int16_t raw, xiao_nrf54l15::AdcResolution resolution,
                           xiao_nrf54l15::AdcGain gain, bool differential) {
  const uint32_t bits = adcResolutionBits(resolution);
  uint32_t exponent = bits;
  if (differential && exponent > 0U) {
    --exponent;
  }

  const double scale = static_cast<double>(1UL << exponent);
  const double mv =
      (static_cast<double>(raw) * 900.0) / (adcGainValue(gain) * scale);
  return static_cast<int32_t>(mv >= 0.0 ? (mv + 0.5) : (mv - 0.5));
}

uint32_t saadcPselValue(const xiao_nrf54l15::Pin& pin) {
  uint32_t psel = 0;
  psel |= (static_cast<uint32_t>(pin.pin) << saadc::CH_PSEL_PIN_Pos);
  psel |= (static_cast<uint32_t>(pin.port) << saadc::CH_PSEL_PORT_Pos);
  psel |= (saadc::CH_PSEL_CONNECT_ANALOG << saadc::CH_PSEL_CONNECT_Pos);
  return psel;
}

uint32_t spimPrescaler(uint32_t coreHz, uint32_t targetHz, uint32_t minDivisor) {
  if (targetHz == 0U) {
    targetHz = 1000000U;
  }

  uint32_t divisor = coreHz / targetHz;
  if ((coreHz % targetHz) != 0U) {
    ++divisor;
  }

  if (divisor < minDivisor) {
    divisor = minDivisor;
  }
  if ((divisor & 1U) != 0U) {
    ++divisor;
  }
  if (divisor > 126U) {
    divisor = 126U;
  }

  return divisor;
}

void clearTwimState(uint32_t base) {
  clearEvent(base, twim::EVENTS_STOPPED);
  clearEvent(base, twim::EVENTS_ERROR);
  clearEvent(base, twim::EVENTS_LASTRX);
  clearEvent(base, twim::EVENTS_LASTTX);
  clearEvent(base, twim::EVENTS_DMA_RX_END);
  clearEvent(base, twim::EVENTS_DMA_TX_END);
  reg32(base + twim::ERRORSRC) = twim::ERRORSRC_ALL;
}

uint32_t absDiffU32(uint32_t a, uint32_t b) {
  return (a >= b) ? (a - b) : (b - a);
}

uint32_t timerCompareEventOffset(uint8_t channel) {
  return timer::EVENTS_COMPARE + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t timerCaptureTaskOffset(uint8_t channel) {
  return timer::TASKS_CAPTURE + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t timerCcOffset(uint8_t channel) {
  return timer::CC + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t timerOneShotOffset(uint8_t channel) {
  return timer::ONESHOTEN + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t timerPublishCompareOffset(uint8_t channel) {
  return timer::PUBLISH_COMPARE +
         (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t timerSubscribeCaptureOffset(uint8_t channel) {
  return timer::SUBSCRIBE_CAPTURE +
         (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t timerCompareIntMask(uint8_t channel) {
  return (1UL << (16U + static_cast<uint32_t>(channel)));
}

struct PwmTiming {
  uint8_t prescaler;
  uint16_t countertop;
  uint32_t actualHz;
};

bool computePwmTiming(uint32_t targetHz, PwmTiming* timing) {
  if (timing == nullptr || targetHz == 0U) {
    return false;
  }

  uint32_t bestError = 0xFFFFFFFFUL;
  PwmTiming best{0, 0, 0};
  bool found = false;

  for (uint8_t prescaler = 0; prescaler <= 7U; ++prescaler) {
    const uint32_t pwmClk = 16000000UL >> prescaler;
    if (pwmClk == 0U) {
      continue;
    }

    uint32_t top = (pwmClk + (targetHz / 2U)) / targetHz;
    if (top < 3U) {
      top = 3U;
    }
    if (top > 32767U) {
      continue;
    }

    const uint32_t actualHz = pwmClk / top;
    const uint32_t error = absDiffU32(actualHz, targetHz);

    if (!found || error < bestError) {
      found = true;
      bestError = error;
      best.prescaler = prescaler;
      best.countertop = static_cast<uint16_t>(top);
      best.actualHz = actualHz;
      if (error == 0U) {
        break;
      }
    }
  }

  if (!found) {
    return false;
  }

  *timing = best;
  return true;
}

uint32_t pwmTaskSeqStartOffset(uint8_t sequence) {
  return pwm::TASKS_DMA_SEQ_START + (static_cast<uint32_t>(sequence) * 8U);
}

uint32_t pwmEventSeqStartedOffset(uint8_t sequence) {
  return pwm::EVENTS_SEQSTARTED + (static_cast<uint32_t>(sequence) * sizeof(uint32_t));
}

uint32_t pwmEventSeqEndOffset(uint8_t sequence) {
  return pwm::EVENTS_SEQEND + (static_cast<uint32_t>(sequence) * sizeof(uint32_t));
}

uint32_t pwmEventDmaSeqEndOffset(uint8_t sequence) {
  return pwm::EVENTS_DMA_SEQ_END + (static_cast<uint32_t>(sequence) * 0x0CU);
}

uint32_t pwmDmaSeqPtrOffset(uint8_t sequence) {
  return pwm::DMA_SEQ_PTR + (static_cast<uint32_t>(sequence) * 8U);
}

uint32_t pwmDmaSeqMaxCntOffset(uint8_t sequence) {
  return pwm::DMA_SEQ_MAXCNT + (static_cast<uint32_t>(sequence) * 8U);
}

uint32_t gpioteInEventOffset(uint8_t channel) {
  return gpiote::EVENTS_IN + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteTaskOutOffset(uint8_t channel) {
  return gpiote::TASKS_OUT + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteTaskSetOffset(uint8_t channel) {
  return gpiote::TASKS_SET + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteTaskClrOffset(uint8_t channel) {
  return gpiote::TASKS_CLR + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteConfigOffset(uint8_t channel) {
  return gpiote::CONFIG + (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteSubscribeOutOffset(uint8_t channel) {
  return gpiote::SUBSCRIBE_OUT +
         (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteSubscribeSetOffset(uint8_t channel) {
  return gpiote::SUBSCRIBE_SET +
         (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

uint32_t gpioteSubscribeClrOffset(uint8_t channel) {
  return gpiote::SUBSCRIBE_CLR +
         (static_cast<uint32_t>(channel) * sizeof(uint32_t));
}

enum class ComparatorOwner : uint8_t {
  kNone = 0U,
  kComp = 1U,
  kLpcomp = 2U,
};

ComparatorOwner g_comparatorOwner = ComparatorOwner::kNone;

bool claimComparator(ComparatorOwner owner) {
  if (g_comparatorOwner != ComparatorOwner::kNone &&
      g_comparatorOwner != owner) {
    return false;
  }
  g_comparatorOwner = owner;
  return true;
}

void releaseComparator(ComparatorOwner owner) {
  if (g_comparatorOwner == owner) {
    g_comparatorOwner = ComparatorOwner::kNone;
  }
}

bool pinSupportsAnalogInput(const Pin& pin) {
  return isConnected(pin) && (xiao_nrf54l15::saadcInputForPin(pin) >= 0);
}

bool configureAnalogPeripheralPin(const Pin& pin) {
  if (!pinSupportsAnalogInput(pin)) {
    return false;
  }

  const uint32_t base = gpioBaseForPort(pin.port);
  if (base == 0U) {
    return false;
  }

  uint32_t cnf = 0U;
  cnf |= GPIO_PIN_CNF_INPUT_Disconnect;
  cnf |= GPIO_PIN_CNF_PULL_Disabled;
  cnf |= GPIO_PIN_CNF_SENSE_Disabled;
  cnf |= ((GPIO_PIN_CNF_CTRLSEL_GPIO << GPIO_PIN_CNF_CTRLSEL_Pos) &
          GPIO_PIN_CNF_CTRLSEL_Msk);
  reg32(base + gpio::PIN_CNF +
        (static_cast<uint32_t>(pin.pin) * sizeof(uint32_t))) = cnf;
  return true;
}

uint32_t makeCompPinSelect(const Pin& pin) {
  return ((static_cast<uint32_t>(pin.pin) << COMP_PSEL_PIN_Pos) &
          COMP_PSEL_PIN_Msk) |
         ((static_cast<uint32_t>(pin.port) << COMP_PSEL_PORT_Pos) &
          COMP_PSEL_PORT_Msk);
}

uint32_t makeLpcompPinSelect(const Pin& pin) {
  return ((static_cast<uint32_t>(pin.pin) << LPCOMP_PSEL_PIN_Pos) &
          LPCOMP_PSEL_PIN_Msk) |
         ((static_cast<uint32_t>(pin.port) << LPCOMP_PSEL_PORT_Pos) &
          LPCOMP_PSEL_PORT_Msk);
}

uint32_t makeQdecConnectedPinSelect(const Pin& pin) {
  if (!isConnected(pin)) {
    return PSEL_DISCONNECTED;
  }

  return ((static_cast<uint32_t>(pin.pin) << QDEC_PSEL_A_PIN_Pos) &
          QDEC_PSEL_A_PIN_Msk) |
         ((static_cast<uint32_t>(pin.port) << QDEC_PSEL_A_PORT_Pos) &
          QDEC_PSEL_A_PORT_Msk) |
         ((QDEC_PSEL_A_CONNECT_Connected << QDEC_PSEL_A_CONNECT_Pos) &
          QDEC_PSEL_A_CONNECT_Msk);
}

uint16_t clampPermille(uint16_t permille) {
  if (permille > 1000U) {
    return 1000U;
  }
  return permille;
}

uint32_t compThresholdCodeFromPermille(uint16_t permille) {
  permille = clampPermille(permille);
  uint32_t scaled = (static_cast<uint32_t>(permille) * 64U + 500U) / 1000U;
  if (scaled < 1U) {
    scaled = 1U;
  }
  if (scaled > 64U) {
    scaled = 64U;
  }
  return scaled - 1U;
}

uint32_t compThresholdRegValue(uint16_t lowPermille, uint16_t highPermille) {
  const uint32_t lowCode = compThresholdCodeFromPermille(lowPermille);
  const uint32_t highCode = compThresholdCodeFromPermille(highPermille);
  return ((lowCode << COMP_TH_THDOWN_Pos) & COMP_TH_THDOWN_Msk) |
         ((highCode << COMP_TH_THUP_Pos) & COMP_TH_THUP_Msk);
}

bool waitForCompReady(volatile uint32_t* readyEvent, uint32_t spinLimit) {
  return waitForNonZero(readyEvent, spinLimit);
}

LpcompReference nearestLpcompReference(uint16_t thresholdPermille) {
  thresholdPermille = clampPermille(thresholdPermille);
  struct Candidate {
    uint16_t permille;
    LpcompReference ref;
  };
  static constexpr Candidate kCandidates[] = {
      {63U, LpcompReference::k1over16Vdd},
      {125U, LpcompReference::k1over8Vdd},
      {188U, LpcompReference::k3over16Vdd},
      {250U, LpcompReference::k2over8Vdd},
      {313U, LpcompReference::k5over16Vdd},
      {375U, LpcompReference::k3over8Vdd},
      {438U, LpcompReference::k7over16Vdd},
      {500U, LpcompReference::k4over8Vdd},
      {563U, LpcompReference::k9over16Vdd},
      {625U, LpcompReference::k5over8Vdd},
      {688U, LpcompReference::k11over16Vdd},
      {750U, LpcompReference::k6over8Vdd},
      {813U, LpcompReference::k13over16Vdd},
      {875U, LpcompReference::k7over8Vdd},
      {938U, LpcompReference::k15over16Vdd},
  };

  uint32_t bestDiff = 0xFFFFFFFFUL;
  LpcompReference best = LpcompReference::k4over8Vdd;
  for (const Candidate& candidate : kCandidates) {
    const uint32_t diff = absDiffU32(candidate.permille, thresholdPermille);
    if (diff < bestDiff) {
      bestDiff = diff;
      best = candidate.ref;
      if (diff == 0U) {
        break;
      }
    }
  }
  return best;
}

GpioPull gpioPullFromQdecInputPull(QdecInputPull pull) {
  switch (pull) {
    case QdecInputPull::kPullDown:
      return GpioPull::kPullDown;
    case QdecInputPull::kPullUp:
      return GpioPull::kPullUp;
    case QdecInputPull::kDisabled:
    default:
      return GpioPull::kDisabled;
  }
}

#if defined(NRF54L15_CLEAN_ANTENNA_EXTERNAL)
constexpr xiao_nrf54l15::BoardAntennaPath kDefaultBoardAntennaPath =
    xiao_nrf54l15::BoardAntennaPath::kExternal;
#else
constexpr xiao_nrf54l15::BoardAntennaPath kDefaultBoardAntennaPath =
    xiao_nrf54l15::BoardAntennaPath::kCeramic;
#endif

xiao_nrf54l15::BoardAntennaPath g_boardAntennaPath = kDefaultBoardAntennaPath;

constexpr uint32_t kBleAccessAddress = 0x8E89BED6UL;
constexpr uint32_t kBleAdvertisingCrcInit = 0x555555UL;
constexpr uint32_t kBleCrcPolynomial = 0x00065BUL;
constexpr uint8_t kBlePduScanReq = 0x03U;
constexpr uint8_t kBlePduAdvExtInd = 0x07U;
constexpr uint8_t kBleExtendedAdvModeNonConnNonScan = 0x00U;
constexpr uint8_t kBleExtendedAdvModeConnNonScan = 0x01U;
constexpr uint8_t kBleExtendedAdvModeNonConnScan = 0x02U;
constexpr uint8_t kBleExtendedPrimaryHeaderFlags = 0x19U;  // AdvA + ADI + AuxPtr
constexpr uint8_t kBleExtendedAuxHeaderFlags = 0x08U;      // ADI
constexpr uint8_t kBleExtendedAuxPhy1M = 0x00U;
constexpr uint8_t kBleExtendedAuxOffsetUnit30Us = 30U;
constexpr uint16_t kBleExtendedAuxOffsetUnit300Us = 300U;
constexpr uint16_t kBleExtendedAuxOffsetMaxUnits = 0x1FFFU;
constexpr uint32_t kBleExtendedAuxRxLeadUs = 220U;
constexpr uint8_t kBleExtendedClockAccuracy500Ppm = 0x00U;
constexpr uint8_t kBlePduScanRsp = 0x04U;
constexpr uint8_t kBlePduConnectInd = 0x05U;
constexpr uint8_t kBlePduLlControl = 0x03U;
constexpr uint8_t kBlePduDataStartOrComplete = 0x02U;
constexpr uint8_t kBleLlCtrlTerminateInd = 0x02U;
constexpr uint8_t kBleLlCtrlEncReq = 0x03U;
constexpr uint8_t kBleLlCtrlEncRsp = 0x04U;
constexpr uint8_t kBleLlCtrlStartEncReq = 0x05U;
constexpr uint8_t kBleLlCtrlStartEncRsp = 0x06U;
constexpr uint8_t kBleLlCtrlConnectionUpdateInd = 0x00U;
constexpr uint8_t kBleLlCtrlChannelMapInd = 0x01U;

struct BleExtendedAuxPtrView {
  bool valid;
  uint8_t channel;
  uint8_t phy;
  uint32_t offsetUs;
};

struct BleExtendedHeaderView {
  bool valid;
  bool hasAdvA;
  bool hasAdi;
  bool hasAuxPtr;
  uint8_t advMode;
  const uint8_t* advA;
  uint16_t did;
  uint8_t sid;
  BleExtendedAuxPtrView auxPtr;
  const uint8_t* advData;
  uint8_t advDataLength;
};

bool parseBleExtendedAuxPtr(const uint8_t auxPtr[3], BleExtendedAuxPtrView* outView) {
  if (auxPtr == nullptr || outView == nullptr) {
    return false;
  }

  memset(outView, 0, sizeof(*outView));
  const uint8_t channel = static_cast<uint8_t>(auxPtr[0] & 0x3FU);
  const bool offsetUnit300Us = ((auxPtr[0] >> 7U) & 0x1U) != 0U;
  const uint16_t offsetUnits = static_cast<uint16_t>(
      static_cast<uint16_t>(auxPtr[1]) |
      (static_cast<uint16_t>(auxPtr[2] & 0x1FU) << 8U));
  const uint8_t phy = static_cast<uint8_t>((auxPtr[2] >> 5U) & 0x07U);
  if (channel > 36U) {
    return false;
  }

  outView->valid = true;
  outView->channel = channel;
  outView->phy = phy;
  outView->offsetUs = static_cast<uint32_t>(offsetUnits) *
                      (offsetUnit300Us ? kBleExtendedAuxOffsetUnit300Us
                                       : kBleExtendedAuxOffsetUnit30Us);
  return true;
}

bool parseBleExtendedHeader(const uint8_t* payload, uint8_t payloadLength,
                            BleExtendedHeaderView* outView) {
  if (payload == nullptr || outView == nullptr || payloadLength < 1U) {
    return false;
  }

  memset(outView, 0, sizeof(*outView));

  outView->advMode = static_cast<uint8_t>((payload[0] >> 6U) & 0x03U);
  const uint8_t extHdrLen = static_cast<uint8_t>(payload[0] & 0x3FU);
  if ((payloadLength < 2U) || (payloadLength < static_cast<uint8_t>(1U + extHdrLen)) ||
      (extHdrLen < 1U)) {
    return false;
  }

  const uint8_t flags = payload[1];
  const uint8_t* cursor = &payload[2];
  uint8_t remainingHdr = static_cast<uint8_t>(extHdrLen - 1U);

  if ((flags & 0x01U) != 0U) {
    if (remainingHdr < 6U) {
      return false;
    }
    outView->hasAdvA = true;
    outView->advA = cursor;
    cursor += 6U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 6U);
  }
  if ((flags & 0x02U) != 0U) {
    if (remainingHdr < 6U) {
      return false;
    }
    cursor += 6U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 6U);
  }
  if ((flags & 0x04U) != 0U) {
    if (remainingHdr < 1U) {
      return false;
    }
    cursor += 1U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 1U);
  }
  if ((flags & 0x08U) != 0U) {
    if (remainingHdr < 2U) {
      return false;
    }
    outView->hasAdi = true;
    outView->did = static_cast<uint16_t>(cursor[0]) |
                   (static_cast<uint16_t>(cursor[1] & 0x0FU) << 8U);
    outView->sid = static_cast<uint8_t>((cursor[1] >> 4U) & 0x0FU);
    cursor += 2U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 2U);
  }
  if ((flags & 0x10U) != 0U) {
    if (remainingHdr < 3U) {
      return false;
    }
    if (!parseBleExtendedAuxPtr(cursor, &outView->auxPtr)) {
      return false;
    }
    outView->hasAuxPtr = true;
    cursor += 3U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 3U);
  }
  if ((flags & 0x20U) != 0U) {
    if (remainingHdr < 18U) {
      return false;
    }
    cursor += 18U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 18U);
  }
  if ((flags & 0x40U) != 0U) {
    if (remainingHdr < 1U) {
      return false;
    }
    cursor += 1U;
    remainingHdr = static_cast<uint8_t>(remainingHdr - 1U);
  }
  if (remainingHdr != 0U) {
    return false;
  }

  const uint8_t consumed = static_cast<uint8_t>(1U + extHdrLen);
  outView->advData = &payload[consumed];
  outView->advDataLength = static_cast<uint8_t>(payloadLength - consumed);
  outView->valid = true;
  return true;
}
constexpr uint8_t kBleLlCtrlUnknownRsp = 0x07U;
constexpr uint8_t kBleLlCtrlFeatureReq = 0x08U;
constexpr uint8_t kBleLlCtrlFeatureRsp = 0x09U;
constexpr uint8_t kBleLlCtrlPauseEncReq = 0x0AU;
constexpr uint8_t kBleLlCtrlPauseEncRsp = 0x0BU;
constexpr uint8_t kBleLlCtrlVersionInd = 0x0CU;
constexpr uint8_t kBleLlCtrlRejectInd = 0x0DU;
constexpr uint8_t kBleLlCtrlSlaveFeatureReq = 0x0EU;
constexpr uint8_t kBleLlCtrlConnectionParamReq = 0x0FU;
constexpr uint8_t kBleLlCtrlConnectionParamRsp = 0x10U;
constexpr uint8_t kBleLlCtrlRejectExtInd = 0x11U;
constexpr uint8_t kBleLlCtrlPingReq = 0x12U;
constexpr uint8_t kBleLlCtrlPingRsp = 0x13U;
constexpr uint8_t kBleLlCtrlLengthReq = 0x14U;
constexpr uint8_t kBleLlCtrlLengthRsp = 0x15U;
constexpr uint8_t kBleLlCtrlPhyReq = 0x16U;
constexpr uint8_t kBleLlCtrlPhyRsp = 0x17U;
constexpr uint8_t kBleLlCtrlPhyUpdateInd = 0x18U;
constexpr uint8_t kBleLlCtrlMinUsedChannelsInd = 0x19U;
constexpr uint8_t kBleLlCtrlCteReq = 0x1AU;
constexpr uint8_t kBleLlCtrlCteRsp = 0x1BU;
constexpr uint8_t kBleLlCtrlPeriodicSyncInd = 0x1CU;
constexpr uint8_t kBleLlCtrlClockAccuracyReq = 0x1DU;
constexpr uint8_t kBleLlCtrlClockAccuracyRsp = 0x1EU;
constexpr uint8_t kBleLlCtrlCisReq = 0x1FU;
constexpr uint8_t kBleLlCtrlCisRsp = 0x20U;
constexpr uint8_t kBleLlCtrlCisInd = 0x21U;
constexpr uint8_t kBleLlCtrlCisTerminateInd = 0x22U;
constexpr uint8_t kBleLlErrorUnsupportedLlParamValue = 0x20U;
constexpr uint8_t kBleLlErrorUnsupportedRemoteFeature = 0x1AU;
constexpr uint8_t kBleLlErrorPinOrKeyMissing = 0x06U;
constexpr uint8_t kBleLlErrorConnectionTimeout = 0x08U;
constexpr uint8_t kBleLlErrorCommandDisallowed = 0x0CU;
constexpr uint8_t kBleLlErrorLlProcedureCollision = 0x23U;
constexpr uint8_t kBleLlErrorMicFailure = 0x3DU;
constexpr uint8_t kBleLlFeatureMaskOctet0 = 0x1FU;
constexpr uint64_t kBleEncPacketCounterMask = 0x7FFFFFFFFFULL;

constexpr uint16_t kBleL2capCidAtt = 0x0004U;
constexpr uint16_t kBleL2capCidLeSignaling = 0x0005U;
constexpr uint16_t kBleL2capCidSmp = 0x0006U;
constexpr uint8_t kBleL2capHeaderLen = 4U;
constexpr uint8_t kBleDataPduMaxPayload = 27U;
constexpr uint8_t kBleMicLen = 4U;
constexpr uint16_t kBleDefaultAttMtu = 23U;
constexpr uint8_t kL2capSigCodeCommandRejectRsp = 0x01U;
constexpr uint8_t kL2capSigCodeConnParamUpdateReq = 0x12U;
constexpr uint8_t kL2capSigCodeConnParamUpdateRsp = 0x13U;
constexpr uint8_t kL2capSigCodeLeCreditConnReq = 0x14U;
constexpr uint8_t kL2capSigCodeLeCreditConnRsp = 0x15U;
constexpr uint8_t kL2capSigCodeLeFlowControlCredit = 0x16U;
constexpr uint16_t kL2capCmdRejectReasonCmdNotUnderstood = 0x0000U;
constexpr uint16_t kL2capCmdRejectReasonSignalingMtuExceeded = 0x0001U;
constexpr uint16_t kL2capCmdRejectReasonInvalidCid = 0x0002U;
constexpr uint16_t kL2capConnParamResultRejected = 0x0001U;
constexpr uint16_t kL2capLeCreditConnResultPsmNotSupported = 0x0002U;
constexpr uint16_t kBleL2capLeSignalingMtu = 23U;

constexpr uint32_t kBleConnSlaveScaPpm = 50UL;
constexpr uint8_t kBleConnMicrosPollDivider = 32U;
// With fast ramp-up enabled, trigger TXEN ~115 us after RX end so packet start
// lands near the BLE 150 us inter-frame spacing.
constexpr uint32_t kBleConnTxenAfterRxUs = 95U;
// Legacy advertising request/response exchanges should complete within hundreds
// of microseconds around T_IFS. Bound RX listen windows so advertising cadence
// does not become host-CPU-spin dependent.
constexpr uint32_t kBleAdvRequestListenMaxUs = 2500U;
constexpr uint32_t kBleScanRspListenMaxUs = 2500U;

void initBleGrtc() {
  static bool initialized = false;
  if (initialized) {
    return;
  }

  NRF_GRTC_Type* const grtc = NRF_GRTC;
  if (grtc == nullptr) {
    return;
  }

  uint32_t mode = grtc->MODE;
  mode &= ~GRTC_MODE_SYSCOUNTEREN_Msk;
  mode |= (GRTC_MODE_SYSCOUNTEREN_Enabled << GRTC_MODE_SYSCOUNTEREN_Pos);
  grtc->MODE = mode;
  __asm volatile("dsb 0xF" ::: "memory");

  grtc->TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE =
      (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Active <<
       GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
  __asm volatile("dsb 0xF" ::: "memory");
  initialized = true;
}

static inline uint32_t bleTimingUs() {
  return NRF54L15_GRTC_SYSCOUNTER(NRF_GRTC).SYSCOUNTERL;
}

#if defined(NRF54L15_CLEAN_BLE_TIMING_AGGRESSIVE) && (NRF54L15_CLEAN_BLE_TIMING_AGGRESSIVE == 1)
constexpr uint32_t kBleConnAnchorPrewaitUs = 120U;
constexpr uint32_t kBleConnRxStartLeadUs = 1200U;
constexpr uint32_t kBleConnRxListenBaseUs = 420U;
constexpr uint32_t kBleConnRxListenMaxUs = 2200U;
constexpr uint32_t kBleConnDisableWaitUs = 450U;
constexpr uint32_t kBleConnTxDisableWaitUs = 450U;
constexpr uint32_t kBleConnFirstEventFallbackUs = 1000U;
constexpr uint16_t kBlePreferredConnIntervalMinUnits = 80U;
constexpr uint16_t kBlePreferredConnIntervalMaxUnits = 160U;
constexpr uint16_t kBlePreferredConnLatency = 4U;
constexpr uint16_t kBlePreferredConnTimeoutUnits = 600U;
#elif defined(NRF54L15_CLEAN_BLE_TIMING_BALANCED) && (NRF54L15_CLEAN_BLE_TIMING_BALANCED == 1)
constexpr uint32_t kBleConnAnchorPrewaitUs = 220U;
constexpr uint32_t kBleConnRxStartLeadUs = 1200U;
constexpr uint32_t kBleConnRxListenBaseUs = 560U;
constexpr uint32_t kBleConnRxListenMaxUs = 3000U;
constexpr uint32_t kBleConnDisableWaitUs = 550U;
constexpr uint32_t kBleConnTxDisableWaitUs = 550U;
constexpr uint32_t kBleConnFirstEventFallbackUs = 1200U;
constexpr uint16_t kBlePreferredConnIntervalMinUnits = 40U;
constexpr uint16_t kBlePreferredConnIntervalMaxUnits = 80U;
constexpr uint16_t kBlePreferredConnLatency = 0U;
constexpr uint16_t kBlePreferredConnTimeoutUnits = 500U;
#else
constexpr uint32_t kBleConnAnchorPrewaitUs = 360U;
constexpr uint32_t kBleConnRxStartLeadUs = 1200U;
constexpr uint32_t kBleConnRxListenBaseUs = 900U;
constexpr uint32_t kBleConnRxListenMaxUs = 4500U;
constexpr uint32_t kBleConnDisableWaitUs = 750U;
constexpr uint32_t kBleConnTxDisableWaitUs = 750U;
constexpr uint32_t kBleConnFirstEventFallbackUs = 1500U;
constexpr uint16_t kBlePreferredConnIntervalMinUnits = 24U;
constexpr uint16_t kBlePreferredConnIntervalMaxUnits = 40U;
constexpr uint16_t kBlePreferredConnLatency = 0U;
constexpr uint16_t kBlePreferredConnTimeoutUnits = 500U;
#endif

// When link layer control PDUs are exchanged, the central may send multiple packets in the
// same connection event (e.g., ENC_REQ -> ENC_RSP -> START_ENC_REQ -> START_ENC_RSP). We
// keep a short post-TX receive window to catch the follow-up without holding RX for the
// full event budget.
// Post-ENC_RSP follow-up listen is only to catch a same-event START_ENC_REQ.
// Keep this bounded, but long enough to tolerate controller/event jitter.
constexpr uint32_t kBleConnFollowupRxListenMaxUs = 4000U;

constexpr uint8_t kSmpCodePairingRequest = 0x01U;
constexpr uint8_t kSmpCodePairingResponse = 0x02U;
constexpr uint8_t kSmpCodePairingConfirm = 0x03U;
constexpr uint8_t kSmpCodePairingRandom = 0x04U;
constexpr uint8_t kSmpCodePairingFailed = 0x05U;
constexpr uint8_t kSmpCodeEncryptionInformation = 0x06U;
constexpr uint8_t kSmpCodeMasterIdentification = 0x07U;
constexpr uint8_t kSmpCodeSecurityRequest = 0x0BU;
constexpr uint8_t kSmpPairingRequestLen = 7U;
constexpr uint8_t kSmpPairingResponseLen = 7U;
constexpr uint8_t kSmpPairingConfirmLen = 17U;
constexpr uint8_t kSmpPairingRandomLen = 17U;
constexpr uint8_t kSmpEncryptionInformationLen = 17U;
constexpr uint8_t kSmpMasterIdentificationLen = 11U;
constexpr uint8_t kSmpIoCapNoInputNoOutput = 0x03U;
constexpr uint8_t kSmpAuthReqBondingMask = 0x03U;
constexpr uint8_t kSmpKeyDistEncKeyMask = 0x01U;
constexpr uint8_t kSmpReasonConfirmValueFailed = 0x04U;
constexpr uint8_t kSmpReasonPairingNotSupported = 0x05U;
constexpr uint8_t kSmpReasonEncryptionKeySize = 0x06U;
constexpr uint8_t kSmpReasonCommandNotSupported = 0x07U;
constexpr uint8_t kSmpReasonUnspecified = 0x08U;
constexpr uint8_t kSmpReasonInvalidParameters = 0x0AU;
constexpr uint8_t kSmpReasonOobNotAvailable = 0x02U;
constexpr uint8_t kSmpPairingStateIdle = 0U;
constexpr uint8_t kSmpPairingStateRspSent = 1U;
constexpr uint8_t kSmpPairingStateConfirmSent = 2U;
constexpr uint8_t kSmpPairingStateRandomSent = 3U;

constexpr uint32_t kBleBondRetentionMagic = 0x444E4F42U;  // "BOND"
constexpr uint16_t kBleBondRetentionVersion = 1U;
constexpr uint32_t kBleBondRramcBase = 0x5004B000UL;
constexpr uint32_t kBleBondRramcSpinLimit = 600000UL;

struct BleBondRetentionBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t recordLength;
  uint32_t crc32;
  xiao_nrf54l15::BleBondRecord record;
};

__attribute__((section(".noinit"))) BleBondRetentionBlob g_bleBondRetention;
__attribute__((section(".bond_storage"), aligned(4)))
volatile BleBondRetentionBlob g_bleBondFlashStorage;

constexpr uint8_t kAttOpErrorRsp = 0x01U;
constexpr uint8_t kAttOpExchangeMtuReq = 0x02U;
constexpr uint8_t kAttOpExchangeMtuRsp = 0x03U;
constexpr uint8_t kAttOpFindInfoReq = 0x04U;
constexpr uint8_t kAttOpFindInfoRsp = 0x05U;
constexpr uint8_t kAttOpFindByTypeValueReq = 0x06U;
constexpr uint8_t kAttOpFindByTypeValueRsp = 0x07U;
constexpr uint8_t kAttOpReadByTypeReq = 0x08U;
constexpr uint8_t kAttOpReadByTypeRsp = 0x09U;
constexpr uint8_t kAttOpReadReq = 0x0AU;
constexpr uint8_t kAttOpReadRsp = 0x0BU;
constexpr uint8_t kAttOpReadBlobReq = 0x0CU;
constexpr uint8_t kAttOpReadBlobRsp = 0x0DU;
constexpr uint8_t kAttOpReadMultipleReq = 0x0EU;
constexpr uint8_t kAttOpReadMultipleRsp = 0x0FU;
constexpr uint8_t kAttOpReadByGroupTypeReq = 0x10U;
constexpr uint8_t kAttOpReadByGroupTypeRsp = 0x11U;
constexpr uint8_t kAttOpWriteReq = 0x12U;
constexpr uint8_t kAttOpWriteRsp = 0x13U;
constexpr uint8_t kAttOpPrepareWriteReq = 0x16U;
constexpr uint8_t kAttOpPrepareWriteRsp = 0x17U;
constexpr uint8_t kAttOpExecuteWriteReq = 0x18U;
constexpr uint8_t kAttOpExecuteWriteRsp = 0x19U;
constexpr uint8_t kAttOpHandleValueNtf = 0x1BU;
constexpr uint8_t kAttOpHandleValueInd = 0x1DU;
constexpr uint8_t kAttOpHandleValueCfm = 0x1EU;
constexpr uint8_t kAttOpWriteCmd = 0x52U;

constexpr uint8_t kAttErrInvalidHandle = 0x01U;
constexpr uint8_t kAttErrReadNotPermitted = 0x02U;
constexpr uint8_t kAttErrWriteNotPermitted = 0x03U;
constexpr uint8_t kAttErrInvalidPdu = 0x04U;
constexpr uint8_t kAttErrRequestNotSupported = 0x06U;
constexpr uint8_t kAttErrInvalidOffset = 0x07U;
constexpr uint8_t kAttErrPrepareQueueFull = 0x09U;
constexpr uint8_t kAttErrInvalidAttrValueLen = 0x0DU;
constexpr uint8_t kAttErrUnsupportedGroupType = 0x10U;
constexpr uint8_t kAttErrAttributeNotFound = 0x0AU;

constexpr uint16_t kUuidPrimaryService = 0x2800U;
constexpr uint16_t kUuidCharacteristic = 0x2803U;
constexpr uint16_t kUuidClientCharacteristicConfig = 0x2902U;
constexpr uint16_t kUuidGapService = 0x1800U;
constexpr uint16_t kUuidGattService = 0x1801U;
constexpr uint16_t kUuidBatteryService = 0x180FU;
constexpr uint16_t kUuidDeviceName = 0x2A00U;
constexpr uint16_t kUuidAppearance = 0x2A01U;
constexpr uint16_t kUuidPpcp = 0x2A04U;
constexpr uint16_t kUuidServiceChanged = 0x2A05U;
constexpr uint16_t kUuidBatteryLevel = 0x2A19U;

constexpr uint16_t kHandleGapService = 0x0001U;
constexpr uint16_t kHandleGapDeviceNameDecl = 0x0002U;
constexpr uint16_t kHandleGapDeviceNameValue = 0x0003U;
constexpr uint16_t kHandleGapAppearanceDecl = 0x0004U;
constexpr uint16_t kHandleGapAppearanceValue = 0x0005U;
constexpr uint16_t kHandleGapPpcpDecl = 0x0006U;
constexpr uint16_t kHandleGapPpcpValue = 0x0007U;
constexpr uint16_t kHandleGattService = 0x0008U;
constexpr uint16_t kHandleGattServiceChangedDecl = 0x0009U;
constexpr uint16_t kHandleGattServiceChangedValue = 0x000AU;
constexpr uint16_t kHandleGattServiceChangedCccd = 0x000BU;
constexpr uint16_t kHandleBatteryService = 0x0010U;
constexpr uint16_t kHandleBatteryLevelDecl = 0x0011U;
constexpr uint16_t kHandleBatteryLevelValue = 0x0012U;
constexpr uint16_t kHandleBatteryLevelCccd = 0x0013U;

constexpr uint8_t kGattCharacteristicPropRead = 0x02U;
constexpr uint8_t kGattCharacteristicPropNotify = 0x10U;
constexpr uint8_t kGattCharacteristicPropIndicate = 0x20U;
constexpr uint8_t kAttrReadInvalidHandleLen = 0xFFU;
constexpr uint8_t kAttrReadInvalidOffsetLen = 0xFEU;
constexpr uint8_t kAttrReadNotPermittedLen = 0xFDU;
constexpr uint8_t kAttFindInfoFormat16Bit = 0x01U;
constexpr uint8_t kAttFindInfoFormat128Bit = 0x02U;
constexpr uint8_t kBleUuidLength16 = 2U;
constexpr uint8_t kBleUuidLength128 = 16U;

uint16_t readLe16(const uint8_t* p);
void writeLe16(uint8_t* p, uint16_t v);

struct BlePrimaryServiceRecord {
  uint16_t startHandle;
  uint16_t endHandle;
  uint16_t uuid16;
};

struct BleCharacteristicRecord {
  uint16_t declarationHandle;
  uint8_t properties;
  uint16_t valueHandle;
  uint16_t uuid16;
};

struct BleAttributeUuidRecord {
  uint16_t handle;
  uint16_t uuid16;
};

uint8_t customUuidLength(uint8_t uuidLength) {
  if (uuidLength == kBleUuidLength16 || uuidLength == kBleUuidLength128) {
    return uuidLength;
  }
  return 0U;
}

void setCustomUuid16(uint8_t* uuidLength, uint8_t* uuidBytes, uint16_t uuid16) {
  if (uuidLength == nullptr || uuidBytes == nullptr) {
    return;
  }

  memset(uuidBytes, 0, kBleUuidLength128);
  *uuidLength = kBleUuidLength16;
  writeLe16(uuidBytes, uuid16);
}

bool setCustomUuid128(uint8_t* uuidLength, uint8_t* uuidBytes,
                      const uint8_t* uuid128Canonical) {
  if (uuidLength == nullptr || uuidBytes == nullptr || uuid128Canonical == nullptr) {
    return false;
  }

  memset(uuidBytes, 0, kBleUuidLength128);
  *uuidLength = kBleUuidLength128;
  // Store 128-bit UUIDs in ATT/BLE little-endian order so wire encoding and
  // incoming ATT request matching do not need extra byte swapping later.
  for (uint8_t i = 0U; i < kBleUuidLength128; ++i) {
    uuidBytes[i] = uuid128Canonical[kBleUuidLength128 - 1U - i];
  }
  return true;
}

bool customUuidMatchesRaw(const uint8_t* uuidBytes,
                          uint8_t uuidLength,
                          const uint8_t* rawUuid,
                          uint8_t rawUuidLength) {
  uuidLength = customUuidLength(uuidLength);
  if (uuidLength == 0U || rawUuid == nullptr || rawUuidLength != uuidLength) {
    return false;
  }
  return memcmp(uuidBytes, rawUuid, uuidLength) == 0;
}

uint8_t customCharacteristicDeclarationValueLength(uint8_t uuidLength) {
  uuidLength = customUuidLength(uuidLength);
  return uuidLength == 0U ? 0U : static_cast<uint8_t>(3U + uuidLength);
}

constexpr BlePrimaryServiceRecord kBlePrimaryServices[] = {
    {kHandleGapService, kHandleGapPpcpValue, kUuidGapService},
    {kHandleGattService, kHandleGattServiceChangedCccd, kUuidGattService},
    {kHandleBatteryService, kHandleBatteryLevelCccd, kUuidBatteryService},
};

constexpr BleCharacteristicRecord kBleCharacteristics[] = {
    {kHandleGapDeviceNameDecl, kGattCharacteristicPropRead,
     kHandleGapDeviceNameValue, kUuidDeviceName},
    {kHandleGapAppearanceDecl, kGattCharacteristicPropRead,
     kHandleGapAppearanceValue, kUuidAppearance},
    {kHandleGapPpcpDecl, kGattCharacteristicPropRead,
     kHandleGapPpcpValue, kUuidPpcp},
    {kHandleGattServiceChangedDecl, kGattCharacteristicPropIndicate,
     kHandleGattServiceChangedValue, kUuidServiceChanged},
    {kHandleBatteryLevelDecl, static_cast<uint8_t>(kGattCharacteristicPropRead |
                                                   kGattCharacteristicPropNotify),
     kHandleBatteryLevelValue, kUuidBatteryLevel},
};

constexpr BleAttributeUuidRecord kBleAttributeUuids[] = {
    {kHandleGapService, kUuidPrimaryService},
    {kHandleGapDeviceNameDecl, kUuidCharacteristic},
    {kHandleGapDeviceNameValue, kUuidDeviceName},
    {kHandleGapAppearanceDecl, kUuidCharacteristic},
    {kHandleGapAppearanceValue, kUuidAppearance},
    {kHandleGapPpcpDecl, kUuidCharacteristic},
    {kHandleGapPpcpValue, kUuidPpcp},
    {kHandleGattService, kUuidPrimaryService},
    {kHandleGattServiceChangedDecl, kUuidCharacteristic},
    {kHandleGattServiceChangedValue, kUuidServiceChanged},
    {kHandleGattServiceChangedCccd, kUuidClientCharacteristicConfig},
    {kHandleBatteryService, kUuidPrimaryService},
    {kHandleBatteryLevelDecl, kUuidCharacteristic},
    {kHandleBatteryLevelValue, kUuidBatteryLevel},
    {kHandleBatteryLevelCccd, kUuidClientCharacteristicConfig},
};

uint8_t bleChannelToFrequency(xiao_nrf54l15::BleAdvertisingChannel channel) {
  switch (channel) {
    case xiao_nrf54l15::BleAdvertisingChannel::k37:
      return 2U;
    case xiao_nrf54l15::BleAdvertisingChannel::k38:
      return 26U;
    case xiao_nrf54l15::BleAdvertisingChannel::k39:
    default:
      return 80U;
  }
}

uint8_t bleChannelToIndex(xiao_nrf54l15::BleAdvertisingChannel channel) {
  switch (channel) {
    case xiao_nrf54l15::BleAdvertisingChannel::k37:
      return 37U;
    case xiao_nrf54l15::BleAdvertisingChannel::k38:
      return 38U;
    case xiao_nrf54l15::BleAdvertisingChannel::k39:
    default:
      return 39U;
  }
}

uint8_t bleDataChannelToFrequency(uint8_t dataChannel) {
  if (dataChannel > 36U) {
    return 4U;
  }
  if (dataChannel <= 10U) {
    return static_cast<uint8_t>(4U + (2U * dataChannel));
  }
  return static_cast<uint8_t>(6U + (2U * dataChannel));
}

bool ieee802154ChannelValid(uint8_t channel) {
  return (channel >= 11U) && (channel <= 26U);
}

uint8_t ieee802154ChannelToFrequency(uint8_t channel) {
  return static_cast<uint8_t>(5U + ((channel - 11U) * 5U));
}

uint32_t radioTxPowerRegFromDbm(int8_t dbm) {
  if (dbm >= 8) {
    return RADIO_TXPOWER_TXPOWER_Pos8dBm;
  }
  if (dbm >= 7) {
    return RADIO_TXPOWER_TXPOWER_Pos7dBm;
  }
  if (dbm >= 6) {
    return RADIO_TXPOWER_TXPOWER_Pos6dBm;
  }
  if (dbm >= 5) {
    return RADIO_TXPOWER_TXPOWER_Pos5dBm;
  }
  if (dbm >= 4) {
    return RADIO_TXPOWER_TXPOWER_Pos4dBm;
  }
  if (dbm >= 3) {
    return RADIO_TXPOWER_TXPOWER_Pos3dBm;
  }
  if (dbm >= 2) {
    return RADIO_TXPOWER_TXPOWER_Pos2dBm;
  }
  if (dbm >= 1) {
    return RADIO_TXPOWER_TXPOWER_Pos1dBm;
  }
  if (dbm >= 0) {
    return RADIO_TXPOWER_TXPOWER_0dBm;
  }
  if (dbm >= -1) {
    return RADIO_TXPOWER_TXPOWER_Neg1dBm;
  }
  if (dbm >= -2) {
    return RADIO_TXPOWER_TXPOWER_Neg2dBm;
  }
  if (dbm >= -3) {
    return RADIO_TXPOWER_TXPOWER_Neg3dBm;
  }
  if (dbm >= -4) {
    return RADIO_TXPOWER_TXPOWER_Neg4dBm;
  }
  if (dbm >= -5) {
    return RADIO_TXPOWER_TXPOWER_Neg5dBm;
  }
  if (dbm >= -6) {
    return RADIO_TXPOWER_TXPOWER_Neg6dBm;
  }
  if (dbm >= -7) {
    return RADIO_TXPOWER_TXPOWER_Neg7dBm;
  }
  if (dbm >= -8) {
    return RADIO_TXPOWER_TXPOWER_Neg8dBm;
  }
  if (dbm >= -9) {
    return RADIO_TXPOWER_TXPOWER_Neg9dBm;
  }
  if (dbm >= -10) {
    return RADIO_TXPOWER_TXPOWER_Neg10dBm;
  }
  if (dbm >= -12) {
    return RADIO_TXPOWER_TXPOWER_Neg12dBm;
  }
  if (dbm >= -14) {
    return RADIO_TXPOWER_TXPOWER_Neg14dBm;
  }
  if (dbm >= -16) {
    return RADIO_TXPOWER_TXPOWER_Neg16dBm;
  }
  if (dbm >= -18) {
    return RADIO_TXPOWER_TXPOWER_Neg18dBm;
  }
  if (dbm >= -20) {
    return RADIO_TXPOWER_TXPOWER_Neg20dBm;
  }
  if (dbm >= -28) {
    return RADIO_TXPOWER_TXPOWER_Neg28dBm;
  }
  if (dbm >= -40) {
    return RADIO_TXPOWER_TXPOWER_Neg40dBm;
  }
  return RADIO_TXPOWER_TXPOWER_Neg46dBm;
}

uint16_t readLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         (static_cast<uint16_t>(p[1]) << 8U);
}

uint32_t readLe24(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8U) |
         (static_cast<uint32_t>(p[2]) << 16U);
}

uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8U) |
         (static_cast<uint32_t>(p[2]) << 16U) |
         (static_cast<uint32_t>(p[3]) << 24U);
}

void writeLe16(uint8_t* p, uint16_t v) {
  if (p == nullptr) {
    return;
  }
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
}

void fillPseudoRandomBytes(uint8_t* out, uint8_t len, uint32_t seed) {
  if (out == nullptr || len == 0U) {
    return;
  }

  uint32_t state = seed ^ 0xA55AA55AUL;
  if (state == 0U) {
    state = 0x1F123BB5UL;
  }
  for (uint8_t i = 0U; i < len; ++i) {
    // Xorshift32 for lightweight deterministic byte generation.
    state ^= (state << 13U);
    state ^= (state >> 17U);
    state ^= (state << 5U);
    out[i] = static_cast<uint8_t>(state & 0xFFU);
  }
}

uint32_t crc32(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0U) {
    return 0U;
  }

  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0U; i < length; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      const uint32_t mask = (crc & 1U) ? 0xFFFFFFFFUL : 0UL;
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

bool bleBondRecordLooksSane(const xiao_nrf54l15::BleBondRecord& record) {
  if (record.peerAddressRandom > 1U || record.localAddressRandom > 1U) {
    return false;
  }
  if (record.keySize < 7U || record.keySize > 16U) {
    return false;
  }
  bool keyNonZero = false;
  for (uint8_t i = 0U; i < 16U; ++i) {
    if (record.ltk[i] != 0U) {
      keyNonZero = true;
      break;
    }
  }
  return keyNonZero;
}

NRF_RRAMC_Type* bondRramc() {
  return reinterpret_cast<NRF_RRAMC_Type*>(kBleBondRramcBase);
}

bool waitForBondRramcReady(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
  if (rramc == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    if (((rramc->READY & RRAMC_READY_READY_Msk) >> RRAMC_READY_READY_Pos) ==
        RRAMC_READY_READY_Ready) {
      return true;
    }
  }
  return false;
}

bool waitForBondRramcReadyNext(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
  if (rramc == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    if (((rramc->READYNEXT & RRAMC_READYNEXT_READYNEXT_Msk) >>
         RRAMC_READYNEXT_READYNEXT_Pos) == RRAMC_READYNEXT_READYNEXT_Ready) {
      return true;
    }
  }
  return false;
}

void copyFromVolatileMemory(const volatile uint8_t* src, uint8_t* dst, size_t len) {
  if (src == nullptr || dst == nullptr) {
    return;
  }
  for (size_t i = 0U; i < len; ++i) {
    dst[i] = src[i];
  }
}

bool writeBondBlobToRram(const BleBondRetentionBlob& blob) {
  NRF_RRAMC_Type* const rramc = bondRramc();
  if (rramc == nullptr) {
    return false;
  }

  const uint32_t targetAddress =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_bleBondFlashStorage));
  const uint8_t* const src = reinterpret_cast<const uint8_t*>(&blob);
  const uint32_t prevConfig = rramc->CONFIG;
  const uint32_t writeConfig = prevConfig | RRAMC_CONFIG_WEN_Msk;
  bool writeOk = true;

  rramc->CONFIG = writeConfig;
  if (!waitForBondRramcReady(rramc, kBleBondRramcSpinLimit)) {
    writeOk = false;
  }

  if (writeOk) {
    rramc->EVENTS_ACCESSERROR = 0U;
    for (size_t i = 0U; i < sizeof(blob); ++i) {
      if (!waitForBondRramcReadyNext(rramc, kBleBondRramcSpinLimit)) {
        writeOk = false;
        break;
      }
      *reinterpret_cast<volatile uint8_t*>(targetAddress + static_cast<uint32_t>(i)) = src[i];
    }
    if (rramc->EVENTS_ACCESSERROR != 0U) {
      writeOk = false;
    }
  }

  if (writeOk) {
    rramc->EVENTS_READY = 0U;
    rramc->TASKS_COMMITWRITEBUF = 1U;
    writeOk = waitForBondRramcReady(rramc, kBleBondRramcSpinLimit);
  }

  rramc->CONFIG = prevConfig;
  if (!waitForBondRramcReady(rramc, kBleBondRramcSpinLimit)) {
    writeOk = false;
  }
  return writeOk;
}

void clearFlashBondBlobImage(BleBondRetentionBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  memset(blob, 0, sizeof(*blob));
}

void buildFlashBondBlob(const xiao_nrf54l15::BleBondRecord& record,
                        BleBondRetentionBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  memset(blob, 0, sizeof(*blob));
  blob->magic = kBleBondRetentionMagic;
  blob->version = kBleBondRetentionVersion;
  blob->recordLength = static_cast<uint16_t>(sizeof(record));
  memcpy(&blob->record, &record, sizeof(record));
  blob->crc32 = crc32(reinterpret_cast<const uint8_t*>(&blob->record), sizeof(blob->record));
}

bool readFlashBondBlob(BleBondRetentionBlob* outBlob) {
  if (outBlob == nullptr) {
    return false;
  }
  copyFromVolatileMemory(reinterpret_cast<const volatile uint8_t*>(&g_bleBondFlashStorage),
                         reinterpret_cast<uint8_t*>(outBlob), sizeof(*outBlob));
  return true;
}

bool writeFlashBondBlob(const BleBondRetentionBlob& blob) {
  if (!writeBondBlobToRram(blob)) {
    return false;
  }
  BleBondRetentionBlob verify{};
  if (!readFlashBondBlob(&verify)) {
    return false;
  }
  return (memcmp(&verify, &blob, sizeof(blob)) == 0);
}

bool clearFlashBondRecord() {
  BleBondRetentionBlob blob{};
  clearFlashBondBlobImage(&blob);
  return writeFlashBondBlob(blob);
}

bool writeFlashBondRecord(const xiao_nrf54l15::BleBondRecord& record) {
  if (!bleBondRecordLooksSane(record)) {
    return false;
  }
  BleBondRetentionBlob blob{};
  buildFlashBondBlob(record, &blob);
  return writeFlashBondBlob(blob);
}

bool readFlashBondRecord(xiao_nrf54l15::BleBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return false;
  }

  BleBondRetentionBlob blob{};
  if (!readFlashBondBlob(&blob)) {
    return false;
  }
  if (blob.magic != kBleBondRetentionMagic ||
      blob.version != kBleBondRetentionVersion ||
      blob.recordLength != sizeof(blob.record)) {
    return false;
  }

  const uint32_t computedCrc =
      crc32(reinterpret_cast<const uint8_t*>(&blob.record), sizeof(blob.record));
  if (computedCrc != blob.crc32) {
    return false;
  }
  if (!bleBondRecordLooksSane(blob.record)) {
    return false;
  }

  memcpy(outRecord, &blob.record, sizeof(*outRecord));
  return true;
}

void clearRetainedBondBlob() {
  memset(&g_bleBondRetention, 0, sizeof(g_bleBondRetention));
}

bool writeRetainedBondRecord(const xiao_nrf54l15::BleBondRecord& record) {
  if (!bleBondRecordLooksSane(record)) {
    return false;
  }
  g_bleBondRetention.magic = kBleBondRetentionMagic;
  g_bleBondRetention.version = kBleBondRetentionVersion;
  g_bleBondRetention.recordLength = static_cast<uint16_t>(sizeof(record));
  memcpy(&g_bleBondRetention.record, &record, sizeof(record));
  g_bleBondRetention.crc32 =
      crc32(reinterpret_cast<const uint8_t*>(&g_bleBondRetention.record),
            sizeof(g_bleBondRetention.record));
  return true;
}

bool readRetainedBondRecord(xiao_nrf54l15::BleBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return false;
  }
  if (g_bleBondRetention.magic != kBleBondRetentionMagic ||
      g_bleBondRetention.version != kBleBondRetentionVersion ||
      g_bleBondRetention.recordLength != sizeof(g_bleBondRetention.record)) {
    return false;
  }
  const uint32_t computedCrc =
      crc32(reinterpret_cast<const uint8_t*>(&g_bleBondRetention.record),
            sizeof(g_bleBondRetention.record));
  if (computedCrc != g_bleBondRetention.crc32) {
    return false;
  }
  if (!bleBondRecordLooksSane(g_bleBondRetention.record)) {
    return false;
  }
  memcpy(outRecord, &g_bleBondRetention.record, sizeof(*outRecord));
  return true;
}

constexpr uint8_t kAesSbox[256] = {
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B,
    0xFE, 0xD7, 0xAB, 0x76, 0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0,
    0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0, 0xB7, 0xFD, 0x93, 0x26,
    0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2,
    0xEB, 0x27, 0xB2, 0x75, 0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0,
    0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84, 0x53, 0xD1, 0x00, 0xED,
    0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F,
    0x50, 0x3C, 0x9F, 0xA8, 0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5,
    0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2, 0xCD, 0x0C, 0x13, 0xEC,
    0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14,
    0xDE, 0x5E, 0x0B, 0xDB, 0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C,
    0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79, 0xE7, 0xC8, 0x37, 0x6D,
    0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F,
    0x4B, 0xBD, 0x8B, 0x8A, 0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E,
    0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E, 0xE1, 0xF8, 0x98, 0x11,
    0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F,
    0xB0, 0x54, 0xBB, 0x16};

constexpr uint8_t kAesRcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10,
                                   0x20, 0x40, 0x80, 0x1B, 0x36};

using AesState = uint8_t[4][4];

uint8_t aesXtime(uint8_t x) {
  return static_cast<uint8_t>((x << 1U) ^ (((x >> 7U) & 0x01U) * 0x1BU));
}

void aesAddRoundKey(uint8_t round, AesState* state, const uint8_t* roundKey) {
  for (uint8_t i = 0U; i < 4U; ++i) {
    for (uint8_t j = 0U; j < 4U; ++j) {
      (*state)[i][j] ^= roundKey[(round * 16U) + (i * 4U) + j];
    }
  }
}

void aesSubBytes(AesState* state) {
  for (uint8_t i = 0U; i < 4U; ++i) {
    for (uint8_t j = 0U; j < 4U; ++j) {
      (*state)[j][i] = kAesSbox[(*state)[j][i]];
    }
  }
}

void aesShiftRows(AesState* state) {
  uint8_t temp = (*state)[0][1];
  (*state)[0][1] = (*state)[1][1];
  (*state)[1][1] = (*state)[2][1];
  (*state)[2][1] = (*state)[3][1];
  (*state)[3][1] = temp;

  temp = (*state)[0][2];
  (*state)[0][2] = (*state)[2][2];
  (*state)[2][2] = temp;
  temp = (*state)[1][2];
  (*state)[1][2] = (*state)[3][2];
  (*state)[3][2] = temp;

  temp = (*state)[0][3];
  (*state)[0][3] = (*state)[3][3];
  (*state)[3][3] = (*state)[2][3];
  (*state)[2][3] = (*state)[1][3];
  (*state)[1][3] = temp;
}

void aesMixColumns(AesState* state) {
  for (uint8_t i = 0U; i < 4U; ++i) {
    uint8_t t = (*state)[i][0];
    uint8_t tmp = static_cast<uint8_t>((*state)[i][0] ^ (*state)[i][1] ^
                                       (*state)[i][2] ^ (*state)[i][3]);
    uint8_t tm = static_cast<uint8_t>((*state)[i][0] ^ (*state)[i][1]);
    tm = aesXtime(tm);
    (*state)[i][0] ^= static_cast<uint8_t>(tm ^ tmp);
    tm = static_cast<uint8_t>((*state)[i][1] ^ (*state)[i][2]);
    tm = aesXtime(tm);
    (*state)[i][1] ^= static_cast<uint8_t>(tm ^ tmp);
    tm = static_cast<uint8_t>((*state)[i][2] ^ (*state)[i][3]);
    tm = aesXtime(tm);
    (*state)[i][2] ^= static_cast<uint8_t>(tm ^ tmp);
    tm = static_cast<uint8_t>((*state)[i][3] ^ t);
    tm = aesXtime(tm);
    (*state)[i][3] ^= static_cast<uint8_t>(tm ^ tmp);
  }
}

void aesKeyExpansion128(const uint8_t key[16], uint8_t roundKey[176]) {
  for (uint8_t i = 0U; i < 16U; ++i) {
    roundKey[i] = key[i];
  }

  uint8_t temp[4] = {0U, 0U, 0U, 0U};
  uint16_t bytesGenerated = 16U;
  uint8_t rconIndex = 1U;
  while (bytesGenerated < 176U) {
    for (uint8_t i = 0U; i < 4U; ++i) {
      temp[i] = roundKey[static_cast<uint16_t>(bytesGenerated - 4U + i)];
    }

    if ((bytesGenerated % 16U) == 0U) {
      const uint8_t rot = temp[0];
      temp[0] = temp[1];
      temp[1] = temp[2];
      temp[2] = temp[3];
      temp[3] = rot;
      temp[0] = kAesSbox[temp[0]];
      temp[1] = kAesSbox[temp[1]];
      temp[2] = kAesSbox[temp[2]];
      temp[3] = kAesSbox[temp[3]];
      temp[0] ^= kAesRcon[rconIndex];
      ++rconIndex;
    }

    for (uint8_t i = 0U; i < 4U; ++i) {
      roundKey[bytesGenerated] =
          static_cast<uint8_t>(roundKey[bytesGenerated - 16U] ^ temp[i]);
      ++bytesGenerated;
    }
  }
}

// AES-128 encryption path adapted from tiny-AES-c (Unlicense), reduced to ECB block encrypt.
void aesEncryptBlockBe(const uint8_t key[16], const uint8_t plaintext[16], uint8_t out[16]) {
  uint8_t roundKey[176] = {0};
  uint8_t stateBuffer[16] = {0};
  aesKeyExpansion128(key, roundKey);
  memcpy(stateBuffer, plaintext, 16U);

  auto* state = reinterpret_cast<AesState*>(&stateBuffer[0]);
  aesAddRoundKey(0U, state, roundKey);
  for (uint8_t round = 1U; round < 10U; ++round) {
    aesSubBytes(state);
    aesShiftRows(state);
    aesMixColumns(state);
    aesAddRoundKey(round, state, roundKey);
  }
  aesSubBytes(state);
  aesShiftRows(state);
  aesAddRoundKey(10U, state, roundKey);
  memcpy(out, stateBuffer, 16U);
}

void reverseBytes(const uint8_t* in, uint8_t* out, uint8_t len) {
  if (in == nullptr || out == nullptr) {
    return;
  }
  for (uint8_t i = 0U; i < len; ++i) {
    out[i] = in[static_cast<uint8_t>(len - 1U - i)];
  }
}

void xor16(const uint8_t* a, const uint8_t* b, uint8_t* out) {
  if (a == nullptr || b == nullptr || out == nullptr) {
    return;
  }
  for (uint8_t i = 0U; i < 16U; ++i) {
    out[i] = static_cast<uint8_t>(a[i] ^ b[i]);
  }
}

bool aesEncryptLe(const uint8_t keyLe[16], const uint8_t plaintextLe[16], uint8_t outLe[16]) {
  if (keyLe == nullptr || plaintextLe == nullptr || outLe == nullptr) {
    return false;
  }

  uint8_t keyBe[16] = {0};
  uint8_t plainBe[16] = {0};
  uint8_t encBe[16] = {0};
  reverseBytes(keyLe, keyBe, 16U);
  reverseBytes(plaintextLe, plainBe, 16U);
  aesEncryptBlockBe(keyBe, plainBe, encBe);
  reverseBytes(encBe, outLe, 16U);
  return true;
}

bool aesEncryptCcmBlock(const uint8_t keyLe[16], const uint8_t plaintext[16], uint8_t out[16]) {
  if (keyLe == nullptr || plaintext == nullptr || out == nullptr) {
    return false;
  }

  // Link-layer CCM operates on byte strings as-is (AES big-endian block
  // convention). Session keys are held in Bluetooth little-endian order, so
  // only the key bytes are swapped before the block encrypt.
  uint8_t keyBe[16] = {0};
  reverseBytes(keyLe, keyBe, 16U);
  aesEncryptBlockBe(keyBe, plaintext, out);
  return true;
}

bool smpC1(const uint8_t tk[16], const uint8_t r[16], const uint8_t preq[7],
           const uint8_t pres[7], uint8_t iat, const uint8_t ia[6], uint8_t rat,
           const uint8_t ra[6], uint8_t out[16]) {
  if (tk == nullptr || r == nullptr || preq == nullptr || pres == nullptr ||
      ia == nullptr || ra == nullptr || out == nullptr) {
    return false;
  }

  uint8_t p1[16] = {0};
  uint8_t p2[16] = {0};
  uint8_t tmp[16] = {0};
  p1[0] = iat;
  p1[1] = rat;
  memcpy(&p1[2], preq, 7U);
  memcpy(&p1[9], pres, 7U);

  xor16(r, p1, tmp);
  if (!aesEncryptLe(tk, tmp, tmp)) {
    return false;
  }

  memcpy(&p2[0], ra, 6U);
  memcpy(&p2[6], ia, 6U);
  memset(&p2[12], 0, 4U);
  xor16(tmp, p2, tmp);
  return aesEncryptLe(tk, tmp, out);
}

bool smpS1(const uint8_t tk[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t out[16]) {
  if (tk == nullptr || r1 == nullptr || r2 == nullptr || out == nullptr) {
    return false;
  }
  uint8_t plaintext[16] = {0};
  // In this stack's SMP call flow, r1 is the initiator random and r2 is the
  // responder random. Build plaintext as initiator || responder (LSB64 halves).
  memcpy(&plaintext[0], r1, 8U);
  memcpy(&plaintext[8], r2, 8U);
  return aesEncryptLe(tk, plaintext, out);
}

void buildBleCcmNonce(const uint8_t iv[8], uint64_t counter, uint8_t direction,
                      uint8_t nonce[13]) {
  if (iv == nullptr || nonce == nullptr) {
    return;
  }

  const uint64_t ctr = counter & 0x7FFFFFFFFFULL;
  nonce[0] = static_cast<uint8_t>(ctr & 0xFFU);
  nonce[1] = static_cast<uint8_t>((ctr >> 8U) & 0xFFU);
  nonce[2] = static_cast<uint8_t>((ctr >> 16U) & 0xFFU);
  nonce[3] = static_cast<uint8_t>((ctr >> 24U) & 0xFFU);
  nonce[4] = static_cast<uint8_t>(((ctr >> 32U) & 0x7FU) | ((direction & 0x01U) << 7U));
  memcpy(&nonce[5], iv, 8U);
}

void buildCcmCtrBlock(const uint8_t nonce[13], uint16_t ctr, uint8_t out[16]) {
  if (nonce == nullptr || out == nullptr) {
    return;
  }

  memset(out, 0, 16U);
  out[0] = 0x01U;  // L = 2 bytes for BLE CCM.
  memcpy(&out[1], nonce, 13U);
  out[14] = static_cast<uint8_t>((ctr >> 8U) & 0xFFU);
  out[15] = static_cast<uint8_t>(ctr & 0xFFU);
}

bool bleComputeCcmMic(const uint8_t key[16], const uint8_t nonce[13], uint8_t header,
                      const uint8_t* payload, uint8_t payloadLen, uint8_t outMic[4]) {
  if (key == nullptr || nonce == nullptr || outMic == nullptr) {
    return false;
  }
  if (payloadLen > 0U && payload == nullptr) {
    return false;
  }

  uint8_t x[16] = {0};
  uint8_t block[16] = {0};
  uint8_t tmp[16] = {0};

  // B0 flags: Adata present + M=4 + L=2.
  block[0] = 0x49U;
  memcpy(&block[1], nonce, 13U);
  block[14] = 0x00U;
  block[15] = payloadLen;
  xor16(x, block, tmp);
  if (!aesEncryptCcmBlock(key, tmp, x)) {
    return false;
  }

  // AAD is one-byte header with SN/NESN/MD masked to zero (0xE3).
  memset(block, 0, sizeof(block));
  block[0] = 0x00U;
  block[1] = 0x01U;
  block[2] = static_cast<uint8_t>(header & 0xE3U);
  xor16(x, block, tmp);
  if (!aesEncryptCcmBlock(key, tmp, x)) {
    return false;
  }

  uint8_t offset = 0U;
  while (offset < payloadLen) {
    memset(block, 0, sizeof(block));
    const uint8_t remaining = static_cast<uint8_t>(payloadLen - offset);
    const uint8_t chunk = (remaining < 16U) ? remaining : 16U;
    memcpy(block, &payload[offset], chunk);
    xor16(x, block, tmp);
    if (!aesEncryptCcmBlock(key, tmp, x)) {
      return false;
    }
    offset = static_cast<uint8_t>(offset + chunk);
  }

  uint8_t s0[16] = {0};
  buildCcmCtrBlock(nonce, 0U, block);
  if (!aesEncryptCcmBlock(key, block, s0)) {
    return false;
  }

  for (uint8_t i = 0U; i < 4U; ++i) {
    outMic[i] = static_cast<uint8_t>(x[i] ^ s0[i]);
  }
  return true;
}

bool bleCcmEncryptPayload(const uint8_t key[16], const uint8_t iv[8], uint64_t counter,
                          uint8_t direction, uint8_t header,
                          const uint8_t* plaintext, uint8_t plaintextLen,
                          uint8_t* outCipherWithMic, uint8_t* outCipherWithMicLen) {
  if (key == nullptr || iv == nullptr || outCipherWithMic == nullptr ||
      outCipherWithMicLen == nullptr) {
    return false;
  }
  if (plaintextLen > 0U && plaintext == nullptr) {
    return false;
  }
  if (plaintextLen > kBleDataPduMaxPayload) {
    return false;
  }

  uint8_t nonce[13] = {0};
  buildBleCcmNonce(iv, counter, direction, nonce);

  uint8_t offset = 0U;
  uint16_t ctr = 1U;
  while (offset < plaintextLen) {
    uint8_t stream[16] = {0};
    uint8_t ctrBlock[16] = {0};
    buildCcmCtrBlock(nonce, ctr, ctrBlock);
    if (!aesEncryptCcmBlock(key, ctrBlock, stream)) {
      return false;
    }

    const uint8_t remaining = static_cast<uint8_t>(plaintextLen - offset);
    const uint8_t chunk = (remaining < 16U) ? remaining : 16U;
    for (uint8_t i = 0U; i < chunk; ++i) {
      outCipherWithMic[offset + i] =
          static_cast<uint8_t>(plaintext[offset + i] ^ stream[i]);
    }
    offset = static_cast<uint8_t>(offset + chunk);
    ++ctr;
  }

  uint8_t mic[4] = {0};
  if (!bleComputeCcmMic(key, nonce, header, plaintext, plaintextLen, mic)) {
    return false;
  }
  memcpy(&outCipherWithMic[plaintextLen], mic, sizeof(mic));
  *outCipherWithMicLen = static_cast<uint8_t>(plaintextLen + kBleMicLen);
  return true;
}

bool bleCcmDecryptPayload(const uint8_t key[16], const uint8_t iv[8], uint64_t counter,
                          uint8_t direction, uint8_t header,
                          const uint8_t* cipherWithMic, uint8_t cipherWithMicLen,
                          uint8_t* outPlaintext, uint8_t* outPlaintextLen) {
  if (key == nullptr || iv == nullptr || cipherWithMic == nullptr ||
      outPlaintext == nullptr || outPlaintextLen == nullptr) {
    return false;
  }
  if (cipherWithMicLen < kBleMicLen) {
    return false;
  }

  const uint8_t payloadLen = static_cast<uint8_t>(cipherWithMicLen - kBleMicLen);
  if (payloadLen > kBleDataPduMaxPayload) {
    return false;
  }

  uint8_t nonce[13] = {0};
  buildBleCcmNonce(iv, counter, direction, nonce);

  uint8_t offset = 0U;
  uint16_t ctr = 1U;
  while (offset < payloadLen) {
    uint8_t stream[16] = {0};
    uint8_t ctrBlock[16] = {0};
    buildCcmCtrBlock(nonce, ctr, ctrBlock);
    if (!aesEncryptCcmBlock(key, ctrBlock, stream)) {
      return false;
    }

    const uint8_t remaining = static_cast<uint8_t>(payloadLen - offset);
    const uint8_t chunk = (remaining < 16U) ? remaining : 16U;
    for (uint8_t i = 0U; i < chunk; ++i) {
      outPlaintext[offset + i] =
          static_cast<uint8_t>(cipherWithMic[offset + i] ^ stream[i]);
    }
    offset = static_cast<uint8_t>(offset + chunk);
    ++ctr;
  }

  uint8_t expectedMic[4] = {0};
  if (!bleComputeCcmMic(key, nonce, header, outPlaintext, payloadLen, expectedMic)) {
    return false;
  }

  uint8_t diff = 0U;
  for (uint8_t i = 0U; i < 4U; ++i) {
    diff |= static_cast<uint8_t>(expectedMic[i] ^ cipherWithMic[payloadLen + i]);
  }
  if (diff != 0U) {
    return false;
  }

  *outPlaintextLen = payloadLen;
  return true;
}

uint8_t bitCount37(const uint8_t map[5]) {
  if (map == nullptr) {
    return 0U;
  }

  uint8_t count = 0;
  for (uint8_t ch = 0; ch < 37U; ++ch) {
    if ((map[ch >> 3U] & (1U << (ch & 7U))) != 0U) {
      ++count;
    }
  }
  return count;
}

uint8_t remapChannelByIndex(const uint8_t map[5], uint8_t usedIndex) {
  uint8_t channel = 0;
  for (uint8_t byte = 0; byte < 5U; ++byte) {
    uint8_t bits = map[byte];
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      if (channel >= 37U) {
        break;
      }
      if ((bits & 0x01U) != 0U) {
        if (usedIndex == 0U) {
          return channel;
        }
        --usedIndex;
      }
      bits >>= 1U;
      ++channel;
    }
  }
  return 0U;
}

bool timeReachedUs(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

uint32_t bleMasterScaPpmUpperBound(uint8_t scaCode) {
  switch (scaCode & 0x07U) {
    case 0U:
      return 500UL;
    case 1U:
      return 250UL;
    case 2U:
      return 150UL;
    case 3U:
      return 100UL;
    case 4U:
      return 75UL;
    case 5U:
      return 50UL;
    case 6U:
      return 30UL;
    case 7U:
    default:
      return 20UL;
  }
}

uint32_t bleConnectionWindowWideningUs(uint16_t intervalUnits, uint8_t masterScaCode) {
  if (intervalUnits == 0U) {
    return 0U;
  }

  const uint32_t intervalUs = static_cast<uint32_t>(intervalUnits) * 1250UL;
  const uint32_t totalPpm =
      bleMasterScaPpmUpperBound(masterScaCode) + kBleConnSlaveScaPpm;
  return (intervalUs * totalPpm + 999999UL) / 1000000UL;
}

uint32_t bleConnectionRxListenUs(uint16_t intervalUnits, uint8_t masterScaCode) {
  const uint32_t wideningUs = bleConnectionWindowWideningUs(intervalUnits, masterScaCode);
  uint32_t postAnchorListenUs = kBleConnRxListenBaseUs + wideningUs;
  if (postAnchorListenUs > kBleConnRxListenMaxUs) {
    postAnchorListenUs = kBleConnRxListenMaxUs;
  }
  // RX is armed kBleConnRxStartLeadUs before the nominal anchor. The radio
  // budget must include this lead, otherwise the listen window can expire
  // before the expected packet arrival.
  return kBleConnRxStartLeadUs + postAnchorListenUs;
}

uint32_t bleDataPduAirTimeUs(uint8_t payloadLength) {
  return static_cast<uint32_t>(10U + payloadLength) * 8U;
}

uint8_t bleReverse8(uint8_t v) {
  v = static_cast<uint8_t>(((v & 0xF0U) >> 4U) | ((v & 0x0FU) << 4U));
  v = static_cast<uint8_t>(((v & 0xCCU) >> 2U) | ((v & 0x33U) << 2U));
  v = static_cast<uint8_t>(((v & 0xAAU) >> 1U) | ((v & 0x55U) << 1U));
  return v;
}

uint16_t bleCsa2Permute(uint16_t v) {
  const uint8_t msb = bleReverse8(static_cast<uint8_t>((v >> 8U) & 0xFFU));
  const uint8_t lsb = bleReverse8(static_cast<uint8_t>(v & 0xFFU));
  return static_cast<uint16_t>((static_cast<uint16_t>(msb) << 8U) | lsb);
}

uint16_t bleCsa2Mam(uint16_t a, uint16_t b) {
  return static_cast<uint16_t>((static_cast<uint32_t>(17U) * a + b) & 0xFFFFU);
}

uint16_t bleCsa2PrnE(uint16_t eventCounter, uint16_t channelId) {
  uint16_t prn = static_cast<uint16_t>(eventCounter ^ channelId);
  for (uint8_t i = 0U; i < 3U; ++i) {
    prn = bleCsa2Permute(prn);
    prn = bleCsa2Mam(prn, channelId);
  }
  return static_cast<uint16_t>(prn ^ channelId);
}

bool waitRadioEndBudgeted(NRF_RADIO_Type* radio, uint32_t budgetUs, uint32_t spinLimit) {
  if (radio == nullptr || spinLimit == 0U) {
    return false;
  }

  const uint32_t startUs = bleTimingUs();
  uint8_t divider = kBleConnMicrosPollDivider;
  while (spinLimit-- > 0U) {
    if (radio->EVENTS_PHYEND != 0U || radio->EVENTS_END != 0U) {
      return true;
    }

    if (--divider == 0U) {
      divider = kBleConnMicrosPollDivider;
      if ((budgetUs > 0U) &&
          (static_cast<uint32_t>(bleTimingUs() - startUs) >= budgetUs)) {
        break;
      }
    }
  }

  return (radio->EVENTS_PHYEND != 0U || radio->EVENTS_END != 0U);
}

bool waitRadioDisabledBudgeted(NRF_RADIO_Type* radio, uint32_t budgetUs, uint32_t spinLimit) {
  if (radio == nullptr || spinLimit == 0U) {
    return false;
  }

  const uint32_t startUs = bleTimingUs();
  uint8_t divider = kBleConnMicrosPollDivider;
  while (spinLimit-- > 0U) {
    if (radio->EVENTS_DISABLED != 0U) {
      return true;
    }

    const uint32_t state =
        (radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (state == RADIO_STATE_STATE_Disabled) {
      return true;
    }

    if (--divider == 0U) {
      divider = kBleConnMicrosPollDivider;
      if ((budgetUs > 0U) &&
          (static_cast<uint32_t>(bleTimingUs() - startUs) >= budgetUs)) {
        break;
      }
    }
  }

  if (radio->EVENTS_DISABLED != 0U) {
    return true;
  }
  const uint32_t state =
      (radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  return (state == RADIO_STATE_STATE_Disabled);
}

bool waitRadioRxDoneBudgeted(NRF_RADIO_Type* radio, uint32_t budgetUs, uint32_t spinLimit) {
  if (radio == nullptr || spinLimit == 0U) {
    return false;
  }

  const uint32_t startUs = bleTimingUs();
  uint8_t divider = kBleConnMicrosPollDivider;
  while (spinLimit-- > 0U) {
    if ((radio->EVENTS_CRCOK != 0U) || (radio->EVENTS_CRCERROR != 0U)) {
      return true;
    }

    if (--divider == 0U) {
      divider = kBleConnMicrosPollDivider;
      if ((budgetUs > 0U) &&
          (static_cast<uint32_t>(bleTimingUs() - startUs) >= budgetUs)) {
        break;
      }
    }
  }

  return ((radio->EVENTS_CRCOK != 0U) || (radio->EVENTS_CRCERROR != 0U));
}

bool llEventInstantReached(uint16_t currentEventCounter, uint16_t instant) {
  return static_cast<uint16_t>(currentEventCounter - instant) < 0x8000U;
}

uint32_t bleDataWhiteValue(uint8_t channelIndex) {
  const uint32_t iv = static_cast<uint32_t>(0x40U | (channelIndex & 0x3FU));
  const uint32_t poly = 0x89UL;  // x^7 + x^4 + 1
  return ((poly << RADIO_DATAWHITE_POLY_Pos) & RADIO_DATAWHITE_POLY_Msk) |
         ((iv << RADIO_DATAWHITE_IV_Pos) & RADIO_DATAWHITE_IV_Msk);
}

uint32_t bleAccessAddressBase(uint32_t accessAddress) {
  return (accessAddress << 8U);
}

uint32_t bleAccessAddressPrefix(uint32_t accessAddress) {
  return ((accessAddress >> 24U) & 0xFFU);
}

int8_t radioRssiDbm(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return 0;
  }
  const uint8_t raw =
      static_cast<uint8_t>((radio->RSSISAMPLE & RADIO_RSSISAMPLE_RSSISAMPLE_Msk) >>
                           RADIO_RSSISAMPLE_RSSISAMPLE_Pos);
  return -static_cast<int8_t>(raw);
}

void clearRadioCoreEvents(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return;
  }

  radio->EVENTS_READY = 0U;
  radio->EVENTS_TXREADY = 0U;
  radio->EVENTS_RXREADY = 0U;
  radio->EVENTS_ADDRESS = 0U;
  radio->EVENTS_PAYLOAD = 0U;
  radio->EVENTS_END = 0U;
  radio->EVENTS_PHYEND = 0U;
  radio->EVENTS_DISABLED = 0U;
  radio->EVENTS_CRCOK = 0U;
  radio->EVENTS_CRCERROR = 0U;
  radio->EVENTS_RXADDRESS = 0U;
}

bool bleAddressEqual(const uint8_t* a, const uint8_t* b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  return memcmp(a, b, 6U) == 0;
}

uint8_t minU8(uint8_t a, uint8_t b) { return (a < b) ? a : b; }

uint16_t minU16(uint16_t a, uint16_t b) { return (a < b) ? a : b; }

bool inHandleRange(uint16_t handle, uint16_t start, uint16_t end) {
  return (handle >= start) && (handle <= end);
}

}  // namespace

namespace xiao_nrf54l15 {

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

bool Gpio::configure(const Pin& pin, GpioDirection direction, GpioPull pull) {
  if (!isConnected(pin)) {
    return false;
  }

  const uint32_t base = gpioBaseForPort(pin.port);
  if (base == 0U) {
    return false;
  }

  const uint32_t bit = (1UL << pin.pin);
  const uint32_t cnfAddr = base + gpio::PIN_CNF +
                           (static_cast<uint32_t>(pin.pin) * sizeof(uint32_t));

  uint32_t cnf = reg32(cnfAddr);
  cnf &= ~(gpio::PIN_CNF_DIR_Msk | gpio::PIN_CNF_INPUT_Msk |
           gpio::PIN_CNF_PULL_Msk);

  if (direction == GpioDirection::kOutput) {
    cnf |= gpio::PIN_CNF_DIR_Msk;
    // Disconnect input buffer while pin is output.
    cnf |= gpio::PIN_CNF_INPUT_Msk;
    reg32(base + gpio::DIRSET) = bit;
  } else {
    // Input buffer connected for reads.
    cnf &= ~gpio::PIN_CNF_INPUT_Msk;
    reg32(base + gpio::DIRCLR) = bit;
  }

  cnf |= ((static_cast<uint32_t>(pull) << gpio::PIN_CNF_PULL_Pos) &
          gpio::PIN_CNF_PULL_Msk);

  reg32(cnfAddr) = cnf;
  return true;
}

bool Gpio::write(const Pin& pin, bool high) {
  if (!isConnected(pin)) {
    return false;
  }

  const uint32_t base = gpioBaseForPort(pin.port);
  if (base == 0U) {
    return false;
  }

  const uint32_t bit = (1UL << pin.pin);
  reg32(base + (high ? gpio::OUTSET : gpio::OUTCLR)) = bit;
  return true;
}

bool Gpio::read(const Pin& pin, bool* high) {
  if (!isConnected(pin) || high == nullptr) {
    return false;
  }

  const uint32_t base = gpioBaseForPort(pin.port);
  if (base == 0U) {
    return false;
  }

  const uint32_t bit = (1UL << pin.pin);
  *high = (reg32(base + gpio::IN) & bit) != 0U;
  return true;
}

bool Gpio::toggle(const Pin& pin) {
  bool state = false;
  if (!read(pin, &state)) {
    return false;
  }
  return write(pin, !state);
}

bool Gpio::setDriveS0D1(const Pin& pin) {
  if (!isConnected(pin)) {
    return false;
  }

  const uint32_t base = gpioBaseForPort(pin.port);
  if (base == 0U) {
    return false;
  }

  const uint32_t cnfAddr = base + gpio::PIN_CNF +
                           (static_cast<uint32_t>(pin.pin) * sizeof(uint32_t));
  uint32_t cnf = reg32(cnfAddr);

  cnf &= ~(gpio::PIN_CNF_DRIVE0_Msk | gpio::PIN_CNF_DRIVE1_Msk);
  cnf |= (gpio::DRIVE0_S0 << gpio::PIN_CNF_DRIVE0_Pos);
  cnf |= (gpio::DRIVE1_D1 << gpio::PIN_CNF_DRIVE1_Pos);

  reg32(cnfAddr) = cnf;
  return true;
}

Spim::Spim(uint32_t base, uint32_t coreClockHz)
    : base_(base), coreClockHz_(coreClockHz), cs_(kPinDisconnected) {}

bool Spim::begin(const Pin& sck, const Pin& mosi, const Pin& miso,
                 const Pin& cs, uint32_t hz, SpiMode mode, bool lsbFirst) {
  if (!isConnected(sck) || !isConnected(mosi) || !isConnected(miso)) {
    return false;
  }

  cs_ = cs;

  if (!Gpio::configure(sck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(mosi, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(miso, GpioDirection::kInput, GpioPull::kDisabled)) {
    return false;
  }

  if (isConnected(cs_) &&
      (!Gpio::configure(cs_, GpioDirection::kOutput, GpioPull::kDisabled) ||
       !Gpio::write(cs_, true))) {
    return false;
  }

  reg32(base_ + spim::ENABLE) = spim::ENABLE_DISABLED;

  reg32(base_ + spim::PSEL_SCK) = make_psel(sck.port, sck.pin);
  reg32(base_ + spim::PSEL_MOSI) = make_psel(mosi.port, mosi.pin);
  reg32(base_ + spim::PSEL_MISO) = make_psel(miso.port, miso.pin);
  reg32(base_ + spim::PSEL_DCX) = PSEL_DISCONNECTED;
  reg32(base_ + spim::PSEL_CSN) = PSEL_DISCONNECTED;

  if (!setFrequency(hz)) {
    return false;
  }

  uint32_t config = 0;
  if (lsbFirst) {
    config |= spim::CONFIG_ORDER_LSB_FIRST;
  }

  if (mode == SpiMode::kMode1 || mode == SpiMode::kMode3) {
    config |= spim::CONFIG_CPHA_TRAILING;
  }
  if (mode == SpiMode::kMode2 || mode == SpiMode::kMode3) {
    config |= spim::CONFIG_CPOL_ACTIVE_LOW;
  }

  reg32(base_ + spim::CONFIG) = config;
  reg32(base_ + spim::ORC) = 0xFF;
  reg32(base_ + spim::ENABLE) = spim::ENABLE_ENABLED;

  return true;
}

bool Spim::setFrequency(uint32_t hz) {
  if (base_ == 0U) {
    return false;
  }
  const uint32_t minDivisor = (base_ == SPIM00_BASE) ? 4U : 2U;
  reg32(base_ + spim::PRESCALER) = spimPrescaler(coreClockHz_, hz, minDivisor);
  return true;
}

void Spim::end() {
  reg32(base_ + spim::ENABLE) = spim::ENABLE_DISABLED;
  reg32(base_ + spim::PSEL_SCK) = PSEL_DISCONNECTED;
  reg32(base_ + spim::PSEL_MOSI) = PSEL_DISCONNECTED;
  reg32(base_ + spim::PSEL_MISO) = PSEL_DISCONNECTED;
  reg32(base_ + spim::PSEL_CSN) = PSEL_DISCONNECTED;
}

bool Spim::transfer(const uint8_t* tx, uint8_t* rx, size_t len,
                    uint32_t spinLimit) {
  if (len == 0U) {
    return true;
  }
  if (len > 0xFFFFU) {
    return false;
  }

  uint8_t txFallback = 0xFF;
  uint8_t rxFallback = 0;

  const uint8_t* txPtr = (tx != nullptr) ? tx : &txFallback;
  uint8_t* rxPtr = (rx != nullptr) ? rx : &rxFallback;

  const uint32_t txLen = (tx != nullptr) ? static_cast<uint32_t>(len) : 0U;
  const uint32_t rxLen = (rx != nullptr) ? static_cast<uint32_t>(len) : 0U;

  clearEvent(base_, spim::EVENTS_STARTED);
  clearEvent(base_, spim::EVENTS_END);
  clearEvent(base_, spim::EVENTS_STOPPED);

  reg32(base_ + spim::DMA_TX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(txPtr));
  reg32(base_ + spim::DMA_TX_MAXCNT) = txLen;

  reg32(base_ + spim::DMA_RX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rxPtr));
  reg32(base_ + spim::DMA_RX_MAXCNT) = rxLen;

  if (isConnected(cs_)) {
    Gpio::write(cs_, false);
  }

  reg32(base_ + spim::TASKS_START) = 1;
  const bool endOk = waitForEvent(base_, spim::EVENTS_END, spinLimit);

  reg32(base_ + spim::TASKS_STOP) = 1;
  const bool stopOk = waitForEvent(base_, spim::EVENTS_STOPPED, spinLimit);

  if (isConnected(cs_)) {
    Gpio::write(cs_, true);
  }

  return endOk && stopOk;
}

Spis::Spis(uint32_t base)
    : spis_(reinterpret_cast<NRF_SPIS_Type*>(static_cast<uintptr_t>(base))),
      active_(false) {}

bool Spis::begin(const Pin& sck, const Pin& mosi, const Pin& miso,
                 const Pin& csn, SpiMode mode, bool lsbFirst,
                 uint8_t defaultChar, uint8_t overReadChar,
                 bool autoAcquireAfterEnd) {
  if (!isConnected(sck) || !isConnected(mosi) || !isConnected(miso) ||
      !isConnected(csn)) {
    return false;
  }

  if (!Gpio::configure(sck, GpioDirection::kInput, GpioPull::kDisabled) ||
      !Gpio::configure(mosi, GpioDirection::kInput, GpioPull::kDisabled) ||
      !Gpio::configure(miso, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(csn, GpioDirection::kInput, GpioPull::kPullUp) ||
      !Gpio::write(miso, true)) {
    return false;
  }

  spis_->ENABLE = (SPIS_ENABLE_ENABLE_Disabled << SPIS_ENABLE_ENABLE_Pos);
  spis_->PSEL.SCK = make_psel(sck.port, sck.pin);
  spis_->PSEL.MOSI = make_psel(mosi.port, mosi.pin);
  spis_->PSEL.MISO = make_psel(miso.port, miso.pin);
  spis_->PSEL.CSN = make_psel(csn.port, csn.pin);

  uint32_t config = 0U;
  if (lsbFirst) {
    config |= (SPIS_CONFIG_ORDER_LsbFirst << SPIS_CONFIG_ORDER_Pos) &
              SPIS_CONFIG_ORDER_Msk;
  }
  if (mode == SpiMode::kMode1 || mode == SpiMode::kMode3) {
    config |= (SPIS_CONFIG_CPHA_Trailing << SPIS_CONFIG_CPHA_Pos) &
              SPIS_CONFIG_CPHA_Msk;
  }
  if (mode == SpiMode::kMode2 || mode == SpiMode::kMode3) {
    config |= (SPIS_CONFIG_CPOL_ActiveLow << SPIS_CONFIG_CPOL_Pos) &
              SPIS_CONFIG_CPOL_Msk;
  }

  spis_->CONFIG = config;
  spis_->DEF = ((static_cast<uint32_t>(defaultChar) << SPIS_DEF_DEF_Pos) &
                SPIS_DEF_DEF_Msk);
  spis_->ORC = ((static_cast<uint32_t>(overReadChar) << SPIS_ORC_ORC_Pos) &
                SPIS_ORC_ORC_Msk);
  spis_->SHORTS =
      autoAcquireAfterEnd
          ? ((SPIS_SHORTS_END_ACQUIRE_Enabled << SPIS_SHORTS_END_ACQUIRE_Pos) &
             SPIS_SHORTS_END_ACQUIRE_Msk)
          : 0U;
  spis_->INTENCLR = 0xFFFFFFFFUL;
  spis_->EVENTS_END = 0U;
  spis_->EVENTS_ACQUIRED = 0U;
  spis_->EVENTS_DMA.RX.END = 0U;
  spis_->EVENTS_DMA.TX.END = 0U;

  spis_->ENABLE = ((SPIS_ENABLE_ENABLE_Enabled << SPIS_ENABLE_ENABLE_Pos) &
                   SPIS_ENABLE_ENABLE_Msk);
  active_ = true;
  clearStatus();
  return true;
}

bool Spis::acquire(uint32_t spinLimit) {
  if (!active_) {
    return false;
  }
  if ((spis_->SEMSTAT & SPIS_SEMSTAT_SEMSTAT_Msk) ==
      (SPIS_SEMSTAT_SEMSTAT_CPU << SPIS_SEMSTAT_SEMSTAT_Pos)) {
    return true;
  }

  spis_->EVENTS_ACQUIRED = 0U;
  spis_->TASKS_ACQUIRE = SPIS_TASKS_ACQUIRE_TASKS_ACQUIRE_Trigger;
  while (spinLimit-- > 0U) {
    if ((spis_->SEMSTAT & SPIS_SEMSTAT_SEMSTAT_Msk) ==
        (SPIS_SEMSTAT_SEMSTAT_CPU << SPIS_SEMSTAT_SEMSTAT_Pos)) {
      return true;
    }
    if (spis_->EVENTS_ACQUIRED != 0U) {
      spis_->EVENTS_ACQUIRED = 0U;
      return true;
    }
  }

  return (spis_->SEMSTAT & SPIS_SEMSTAT_SEMSTAT_Msk) ==
         (SPIS_SEMSTAT_SEMSTAT_CPU << SPIS_SEMSTAT_SEMSTAT_Pos);
}

bool Spis::setBuffers(uint8_t* rx, size_t rxLen, const uint8_t* tx, size_t txLen,
                      uint32_t spinLimit) {
  if (!active_ || rx == nullptr || tx == nullptr || rxLen == 0U || txLen == 0U ||
      rxLen > 0xFFFFU || txLen > 0xFFFFU) {
    return false;
  }
  if (!acquire(spinLimit)) {
    return false;
  }

  spis_->DMA.RX.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rx));
  spis_->DMA.RX.MAXCNT =
      (static_cast<uint32_t>(rxLen) << SPIS_DMA_RX_MAXCNT_MAXCNT_Pos) &
      SPIS_DMA_RX_MAXCNT_MAXCNT_Msk;
  spis_->DMA.TX.PTR = static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(const_cast<uint8_t*>(tx)));
  spis_->DMA.TX.MAXCNT =
      (static_cast<uint32_t>(txLen) << SPIS_DMA_TX_MAXCNT_MAXCNT_Pos) &
      SPIS_DMA_TX_MAXCNT_MAXCNT_Msk;

  spis_->EVENTS_END = 0U;
  spis_->EVENTS_DMA.RX.END = 0U;
  spis_->EVENTS_DMA.TX.END = 0U;
  clearStatus();
  return true;
}

bool Spis::releaseTransaction() {
  if (!active_) {
    return false;
  }
  spis_->TASKS_RELEASE = SPIS_TASKS_RELEASE_TASKS_RELEASE_Trigger;
  return true;
}

bool Spis::pollAcquired(bool clearEventFlag) {
  const bool fired = active_ && (spis_->EVENTS_ACQUIRED != 0U);
  if (fired && clearEventFlag) {
    spis_->EVENTS_ACQUIRED = 0U;
  }
  return fired;
}

bool Spis::pollEnd(bool clearEventFlag) {
  const bool fired = active_ && (spis_->EVENTS_END != 0U);
  if (fired && clearEventFlag) {
    spis_->EVENTS_END = 0U;
  }
  return fired;
}

size_t Spis::receivedBytes() const {
  if (!active_) {
    return 0U;
  }
  return static_cast<size_t>((spis_->DMA.RX.AMOUNT & SPIS_DMA_RX_AMOUNT_AMOUNT_Msk) >>
                             SPIS_DMA_RX_AMOUNT_AMOUNT_Pos);
}

size_t Spis::transmittedBytes() const {
  if (!active_) {
    return 0U;
  }
  return static_cast<size_t>((spis_->DMA.TX.AMOUNT & SPIS_DMA_TX_AMOUNT_AMOUNT_Msk) >>
                             SPIS_DMA_TX_AMOUNT_AMOUNT_Pos);
}

bool Spis::overflowed() const {
  return active_ && ((spis_->STATUS & SPIS_STATUS_OVERFLOW_Msk) != 0U);
}

bool Spis::overread() const {
  return active_ && ((spis_->STATUS & SPIS_STATUS_OVERREAD_Msk) != 0U);
}

void Spis::clearStatus() {
  if (spis_ == nullptr) {
    return;
  }
  spis_->STATUS = SPIS_STATUS_OVERREAD_Clear | SPIS_STATUS_OVERFLOW_Clear;
}

void Spis::end() {
  if (spis_ == nullptr) {
    return;
  }

  spis_->SHORTS = 0U;
  spis_->ENABLE = (SPIS_ENABLE_ENABLE_Disabled << SPIS_ENABLE_ENABLE_Pos);
  spis_->PSEL.SCK = PSEL_DISCONNECTED;
  spis_->PSEL.MOSI = PSEL_DISCONNECTED;
  spis_->PSEL.MISO = PSEL_DISCONNECTED;
  spis_->PSEL.CSN = PSEL_DISCONNECTED;
  active_ = false;
}

Twim::Twim(uint32_t base) : base_(base) {}

bool Twim::begin(const Pin& scl, const Pin& sda, TwimFrequency frequency) {
  if (!isConnected(scl) || !isConnected(sda)) {
    return false;
  }

  // Per datasheet, configure SCL/SDA for input and S0D1 drive before enabling TWIM.
  if (!Gpio::configure(scl, GpioDirection::kInput, GpioPull::kDisabled) ||
      !Gpio::configure(sda, GpioDirection::kInput, GpioPull::kDisabled) ||
      !Gpio::setDriveS0D1(scl) || !Gpio::setDriveS0D1(sda)) {
    return false;
  }

  reg32(base_ + twim::ENABLE) = twim::ENABLE_DISABLED;

  reg32(base_ + twim::PSEL_SCL) = make_psel(scl.port, scl.pin);
  reg32(base_ + twim::PSEL_SDA) = make_psel(sda.port, sda.pin);
  if (!setFrequency(frequency)) {
    return false;
  }

  reg32(base_ + twim::ENABLE) = twim::ENABLE_ENABLED;
  return true;
}

bool Twim::setFrequency(TwimFrequency frequency) {
  if (base_ == 0U) {
    return false;
  }
  reg32(base_ + twim::FREQUENCY) = static_cast<uint32_t>(frequency);
  return true;
}

void Twim::end() {
  reg32(base_ + twim::ENABLE) = twim::ENABLE_DISABLED;
  reg32(base_ + twim::PSEL_SCL) = PSEL_DISCONNECTED;
  reg32(base_ + twim::PSEL_SDA) = PSEL_DISCONNECTED;
}

bool Twim::write(uint8_t address7, const uint8_t* data, size_t len,
                 uint32_t spinLimit) {
  if (len > 0xFFFFU) {
    return false;
  }

  uint8_t dummy = 0;
  const uint8_t* tx = (data != nullptr) ? data : &dummy;

  clearTwimState(base_);
  reg32(base_ + twim::ADDRESS) = (address7 & 0x7FU);

  reg32(base_ + twim::DMA_TX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tx));
  reg32(base_ + twim::DMA_TX_MAXCNT) = static_cast<uint32_t>(len);

  reg32(base_ + twim::TASKS_DMA_TX_START) = 1;
  const uint32_t doneEvent = (len > 0U) ? twim::EVENTS_LASTTX
                                        : twim::EVENTS_DMA_TX_END;
  const bool writeOk = waitForEventOrError(base_, doneEvent, twim::EVENTS_ERROR,
                                           spinLimit);

  reg32(base_ + twim::TASKS_STOP) = 1;
  const bool stopOk = waitForEvent(base_, twim::EVENTS_STOPPED, spinLimit);

  const bool hadError = (reg32(base_ + twim::EVENTS_ERROR) != 0U);
  reg32(base_ + twim::ERRORSRC) = twim::ERRORSRC_ALL;

  return writeOk && stopOk && !hadError;
}

bool Twim::read(uint8_t address7, uint8_t* data, size_t len,
                uint32_t spinLimit) {
  if (data == nullptr || len == 0U || len > 0xFFFFU) {
    return false;
  }

  clearTwimState(base_);
  reg32(base_ + twim::ADDRESS) = (address7 & 0x7FU);

  reg32(base_ + twim::DMA_RX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
  reg32(base_ + twim::DMA_RX_MAXCNT) = static_cast<uint32_t>(len);

  reg32(base_ + twim::TASKS_DMA_RX_START) = 1;
  const bool readOk = waitForEventOrError(base_, twim::EVENTS_LASTRX,
                                          twim::EVENTS_ERROR, spinLimit);

  reg32(base_ + twim::TASKS_STOP) = 1;
  const bool stopOk = waitForEvent(base_, twim::EVENTS_STOPPED, spinLimit);

  const bool hadError = (reg32(base_ + twim::EVENTS_ERROR) != 0U);
  reg32(base_ + twim::ERRORSRC) = twim::ERRORSRC_ALL;

  return readOk && stopOk && !hadError;
}

bool Twim::writeRead(uint8_t address7, const uint8_t* tx, size_t txLen,
                     uint8_t* rx, size_t rxLen, uint32_t spinLimit) {
  if (txLen == 0U) {
    return read(address7, rx, rxLen, spinLimit);
  }
  if (rxLen == 0U) {
    return write(address7, tx, txLen, spinLimit);
  }
  if (tx == nullptr || rx == nullptr || txLen > 0xFFFFU || rxLen > 0xFFFFU) {
    return false;
  }

  clearTwimState(base_);
  reg32(base_ + twim::ADDRESS) = (address7 & 0x7FU);

  reg32(base_ + twim::DMA_TX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tx));
  reg32(base_ + twim::DMA_TX_MAXCNT) = static_cast<uint32_t>(txLen);

  reg32(base_ + twim::DMA_RX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rx));
  reg32(base_ + twim::DMA_RX_MAXCNT) = static_cast<uint32_t>(rxLen);

  reg32(base_ + twim::TASKS_DMA_TX_START) = 1;
  const bool writePhaseOk = waitForEventOrError(base_, twim::EVENTS_LASTTX,
                                                 twim::EVENTS_ERROR, spinLimit);

  if (!writePhaseOk) {
    reg32(base_ + twim::TASKS_STOP) = 1;
    waitForEvent(base_, twim::EVENTS_STOPPED, spinLimit);
    reg32(base_ + twim::ERRORSRC) = twim::ERRORSRC_ALL;
    return false;
  }

  reg32(base_ + twim::TASKS_DMA_RX_START) = 1;
  const bool readPhaseOk = waitForEventOrError(base_, twim::EVENTS_LASTRX,
                                                twim::EVENTS_ERROR, spinLimit);

  reg32(base_ + twim::TASKS_STOP) = 1;
  const bool stopOk = waitForEvent(base_, twim::EVENTS_STOPPED, spinLimit);

  const bool hadError = (reg32(base_ + twim::EVENTS_ERROR) != 0U);
  reg32(base_ + twim::ERRORSRC) = twim::ERRORSRC_ALL;

  return writePhaseOk && readPhaseOk && stopOk && !hadError;
}

Uarte::Uarte(uint32_t base) : base_(base) {}

bool Uarte::begin(const Pin& txd, const Pin& rxd, UarteBaud baud,
                  bool hwFlowControl, const Pin& cts, const Pin& rts) {
  if (!isConnected(txd) || !isConnected(rxd)) {
    return false;
  }
  if (hwFlowControl && (!isConnected(cts) || !isConnected(rts))) {
    return false;
  }

  if (!Gpio::configure(txd, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(rxd, GpioDirection::kInput, GpioPull::kDisabled)) {
    return false;
  }
  Gpio::write(txd, true);

  if (hwFlowControl) {
    if (!Gpio::configure(cts, GpioDirection::kInput, GpioPull::kDisabled) ||
        !Gpio::configure(rts, GpioDirection::kOutput, GpioPull::kDisabled)) {
      return false;
    }
    Gpio::write(rts, true);
  }

  reg32(base_ + uarte::ENABLE) = uarte::ENABLE_DISABLED;

  reg32(base_ + uarte::PSEL_TXD) = make_psel(txd.port, txd.pin);
  reg32(base_ + uarte::PSEL_RXD) = make_psel(rxd.port, rxd.pin);
  reg32(base_ + uarte::PSEL_CTS) =
      hwFlowControl ? make_psel(cts.port, cts.pin) : PSEL_DISCONNECTED;
  reg32(base_ + uarte::PSEL_RTS) =
      hwFlowControl ? make_psel(rts.port, rts.pin) : PSEL_DISCONNECTED;

  uint32_t config = reg32(base_ + uarte::CONFIG);
  if (hwFlowControl) {
    config |= uarte::CONFIG_HWFC_Msk;
  } else {
    config &= ~uarte::CONFIG_HWFC_Msk;
  }
  reg32(base_ + uarte::CONFIG) = config;

  reg32(base_ + uarte::BAUDRATE) = static_cast<uint32_t>(baud);
  reg32(base_ + uarte::ENABLE) = uarte::ENABLE_ENABLED;

  return true;
}

void Uarte::end() {
  reg32(base_ + uarte::ENABLE) = uarte::ENABLE_DISABLED;
  reg32(base_ + uarte::PSEL_TXD) = PSEL_DISCONNECTED;
  reg32(base_ + uarte::PSEL_RXD) = PSEL_DISCONNECTED;
  reg32(base_ + uarte::PSEL_CTS) = PSEL_DISCONNECTED;
  reg32(base_ + uarte::PSEL_RTS) = PSEL_DISCONNECTED;
}

bool Uarte::write(const uint8_t* data, size_t len, uint32_t spinLimit) {
  if (data == nullptr) {
    return false;
  }
  if (len == 0U) {
    return true;
  }
  if (len > 0xFFFFU) {
    return false;
  }

  clearEvent(base_, uarte::EVENTS_DMA_TX_END);
  clearEvent(base_, uarte::EVENTS_TXSTOPPED);
  clearEvent(base_, uarte::EVENTS_ERROR);
  reg32(base_ + uarte::ERRORSRC) = uarte::ERRORSRC_ALL;

  reg32(base_ + uarte::DMA_TX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
  reg32(base_ + uarte::DMA_TX_MAXCNT) = static_cast<uint32_t>(len);

  reg32(base_ + uarte::TASKS_DMA_TX_START) = 1;
  const bool txOk = waitForEvent(base_, uarte::EVENTS_DMA_TX_END, spinLimit);

  reg32(base_ + uarte::TASKS_DMA_TX_STOP) = 1;
  const bool stopped =
      waitForEvent(base_, uarte::EVENTS_TXSTOPPED, spinLimit);

  return txOk && stopped;
}

size_t Uarte::read(uint8_t* data, size_t len, uint32_t spinLimit) {
  if (data == nullptr || len == 0U || len > 0xFFFFU) {
    return 0;
  }

  clearEvent(base_, uarte::EVENTS_DMA_RX_END);
  clearEvent(base_, uarte::EVENTS_RXTO);
  clearEvent(base_, uarte::EVENTS_ERROR);
  reg32(base_ + uarte::ERRORSRC) = uarte::ERRORSRC_ALL;

  reg32(base_ + uarte::DMA_RX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
  reg32(base_ + uarte::DMA_RX_MAXCNT) = static_cast<uint32_t>(len);

  reg32(base_ + uarte::TASKS_DMA_RX_START) = 1;

  bool rxDone = false;
  while (spinLimit-- > 0U) {
    if (reg32(base_ + uarte::EVENTS_DMA_RX_END) != 0U) {
      rxDone = true;
      break;
    }
    if (reg32(base_ + uarte::EVENTS_ERROR) != 0U) {
      break;
    }
  }

  reg32(base_ + uarte::TASKS_DMA_RX_STOP) = 1;
  waitForEvent(base_, uarte::EVENTS_RXTO, 2000000UL);

  const size_t amount = static_cast<size_t>(reg32(base_ + uarte::DMA_RX_AMOUNT));
  if (!rxDone && reg32(base_ + uarte::EVENTS_ERROR) != 0U) {
    reg32(base_ + uarte::ERRORSRC) = uarte::ERRORSRC_ALL;
  }

  return amount;
}

Timer::Timer(uint32_t base, uint32_t pclkHz, uint8_t channelCount)
    : base_(base),
      pclkHz_(pclkHz),
      channelCount_(channelCount),
      prescaler_(4),
      callbacks_{},
      callbackContext_{} {
  if (channelCount_ > 8U) {
    channelCount_ = 8U;
  }
}

bool Timer::begin(TimerBitWidth bitWidth, uint8_t prescaler, bool counterMode) {
  if (channelCount_ == 0U || prescaler > 9U) {
    return false;
  }

  stop();
  clear();

  reg32(base_ + timer::MODE) = counterMode ? timer::MODE_COUNTER : timer::MODE_TIMER;
  reg32(base_ + timer::BITMODE) = static_cast<uint32_t>(bitWidth);
  reg32(base_ + timer::PRESCALER) = static_cast<uint32_t>(prescaler);
  prescaler_ = prescaler;

  reg32(base_ + timer::SHORTS) = 0;
  reg32(base_ + timer::INTENCLR) = 0xFFFFFFFFUL;

  for (uint8_t ch = 0; ch < channelCount_; ++ch) {
    clearEvent(base_, timerCompareEventOffset(ch));
    reg32(base_ + timerOneShotOffset(ch)) = 0;
  }

  return true;
}

bool Timer::setFrequency(uint32_t targetHz) {
  if (targetHz == 0U || pclkHz_ == 0U) {
    return false;
  }

  uint8_t bestPrescaler = 0;
  uint32_t bestError = 0xFFFFFFFFUL;
  bool found = false;

  for (uint8_t prescaler = 0; prescaler <= 9U; ++prescaler) {
    const uint32_t hz = pclkHz_ >> prescaler;
    if (hz == 0U) {
      continue;
    }
    const uint32_t error = absDiffU32(hz, targetHz);
    if (!found || error < bestError) {
      found = true;
      bestError = error;
      bestPrescaler = prescaler;
      if (error == 0U) {
        break;
      }
    }
  }

  if (!found) {
    return false;
  }

  reg32(base_ + timer::PRESCALER) = bestPrescaler;
  prescaler_ = bestPrescaler;
  return true;
}

uint32_t Timer::timerHz() const {
  if (prescaler_ > 31U) {
    return 0;
  }
  return pclkHz_ >> prescaler_;
}

uint32_t Timer::ticksFromMicros(uint32_t us) const {
  const uint32_t hz = timerHz();
  if (hz == 0U || us == 0U) {
    return 0U;
  }

  const uint64_t ticks =
      (static_cast<uint64_t>(hz) * static_cast<uint64_t>(us) + 999999ULL) /
      1000000ULL;
  return static_cast<uint32_t>(ticks);
}

void Timer::start() { reg32(base_ + timer::TASKS_START) = 1; }

void Timer::stop() { reg32(base_ + timer::TASKS_STOP) = 1; }

void Timer::clear() { reg32(base_ + timer::TASKS_CLEAR) = 1; }

bool Timer::setCompare(uint8_t channel, uint32_t ccValue, bool autoClear,
                       bool autoStop, bool oneShot, bool enableInterruptFlag) {
  if (channel >= channelCount_) {
    return false;
  }

  reg32(base_ + timerCcOffset(channel)) = ccValue;
  reg32(base_ + timerOneShotOffset(channel)) = oneShot ? 1U : 0U;

  uint32_t shorts = reg32(base_ + timer::SHORTS);
  const uint32_t clearMask = (1UL << channel);
  const uint32_t stopMask = (1UL << (16U + channel));

  if (autoClear) {
    shorts |= clearMask;
  } else {
    shorts &= ~clearMask;
  }

  if (autoStop) {
    shorts |= stopMask;
  } else {
    shorts &= ~stopMask;
  }

  reg32(base_ + timer::SHORTS) = shorts;
  clearEvent(base_, timerCompareEventOffset(channel));
  enableInterrupt(channel, enableInterruptFlag);

  return true;
}

uint32_t Timer::capture(uint8_t channel) {
  if (channel >= channelCount_) {
    return 0U;
  }
  reg32(base_ + timerCaptureTaskOffset(channel)) = 1;
  return reg32(base_ + timerCcOffset(channel));
}

bool Timer::pollCompare(uint8_t channel, bool clearEventFlag) {
  if (channel >= channelCount_) {
    return false;
  }

  const uint32_t offset = timerCompareEventOffset(channel);
  const bool fired = (reg32(base_ + offset) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, offset);
  }
  return fired;
}

volatile uint32_t* Timer::publishCompareConfigRegister(uint8_t channel) const {
  if (channel >= channelCount_) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + timerPublishCompareOffset(channel)));
}

volatile uint32_t* Timer::subscribeStartConfigRegister() const {
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + timer::SUBSCRIBE_START));
}

volatile uint32_t* Timer::subscribeStopConfigRegister() const {
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + timer::SUBSCRIBE_STOP));
}

volatile uint32_t* Timer::subscribeClearConfigRegister() const {
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + timer::SUBSCRIBE_CLEAR));
}

volatile uint32_t* Timer::subscribeCaptureConfigRegister(uint8_t channel) const {
  if (channel >= channelCount_) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + timerSubscribeCaptureOffset(channel)));
}

void Timer::enableInterrupt(uint8_t channel, bool enable) {
  if (channel >= channelCount_) {
    return;
  }

  const uint32_t mask = timerCompareIntMask(channel);
  reg32(base_ + (enable ? timer::INTENSET : timer::INTENCLR)) = mask;
}

bool Timer::attachCompareCallback(uint8_t channel, CompareCallback callback,
                                  void* context) {
  if (channel >= channelCount_) {
    return false;
  }
  callbacks_[channel] = callback;
  callbackContext_[channel] = context;
  return true;
}

void Timer::service() {
  for (uint8_t ch = 0; ch < channelCount_; ++ch) {
    const uint32_t eventOffset = timerCompareEventOffset(ch);
    if (reg32(base_ + eventOffset) == 0U) {
      continue;
    }

    clearEvent(base_, eventOffset);
    if (callbacks_[ch] != nullptr) {
      callbacks_[ch](ch, callbackContext_[ch]);
    }
  }
}

Pwm::Pwm(uint32_t base)
    : base_(base),
      outPin_(kPinDisconnected),
      dutyPermille_(500),
      countertop_(1000),
      prescaler_(0),
      activeHigh_(true),
      configured_(false),
      sequence_{0, 0, 0, 0} {}

bool Pwm::beginSingle(const Pin& outPin, uint32_t frequencyHz, uint16_t dutyPermille,
                      bool activeHigh) {
  if (!isConnected(outPin)) {
    return false;
  }

  outPin_ = outPin;
  dutyPermille_ = (dutyPermille > 1000U) ? 1000U : dutyPermille;
  activeHigh_ = activeHigh;

  if (!Gpio::configure(outPin_, GpioDirection::kOutput, GpioPull::kDisabled)) {
    return false;
  }
  Gpio::write(outPin_, activeHigh_ ? false : true);

  reg32(base_ + pwm::ENABLE) = pwm::ENABLE_DISABLED;
  reg32(base_ + pwm::SHORTS) = 0;

  for (uint8_t ch = 0; ch < 4U; ++ch) {
    reg32(base_ + pwm::PSEL_OUT + (static_cast<uint32_t>(ch) * sizeof(uint32_t))) =
        PSEL_DISCONNECTED;
  }
  reg32(base_ + pwm::PSEL_OUT) = make_psel(outPin_.port, outPin_.pin);

  reg32(base_ + pwm::MODE) = pwm::MODE_UP;

  if (!configureClockAndTop(frequencyHz)) {
    return false;
  }

  reg32(base_ + pwm::DECODER) =
      (pwm::DECODER_LOAD_COMMON << 0U) | (pwm::DECODER_MODE_REFRESHCOUNT << 8U);
  reg32(base_ + pwm::LOOP) = 0;

  // Keep the channel idle level in the "off" state between periods/stops.
  reg32(base_ + pwm::IDLEOUT) = activeHigh_ ? 0U : 1U;

  reg32(base_ + pwm::SEQ_REFRESH + 0U) = 0;
  reg32(base_ + pwm::SEQ_REFRESH + sizeof(uint32_t)) = 0;
  reg32(base_ + pwm::SEQ_ENDDELAY + 0U) = 0;
  reg32(base_ + pwm::SEQ_ENDDELAY + sizeof(uint32_t)) = 0;

  updateSequenceWord();

  for (uint8_t seq = 0; seq < 2U; ++seq) {
    reg32(base_ + pwmDmaSeqPtrOffset(seq)) =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&sequence_[0]));
    reg32(base_ + pwmDmaSeqMaxCntOffset(seq)) = 2U;  // One 16-bit value in bytes.
  }

  reg32(base_ + pwm::ENABLE) = pwm::ENABLE_ENABLED;
  configured_ = true;
  return true;
}

bool Pwm::setDutyPermille(uint16_t dutyPermille) {
  dutyPermille_ = (dutyPermille > 1000U) ? 1000U : dutyPermille;
  if (!configured_) {
    return false;
  }
  updateSequenceWord();
  return true;
}

bool Pwm::setFrequency(uint32_t frequencyHz) {
  if (!configured_) {
    return false;
  }

  if (!configureClockAndTop(frequencyHz)) {
    return false;
  }

  updateSequenceWord();
  return true;
}

bool Pwm::start(uint8_t sequence, uint32_t spinLimit) {
  if (!configured_ || sequence > 1U) {
    return false;
  }

  uint32_t shorts = 0;
  if (sequence == 0U) {
    shorts |= (1UL << 2U);  // LOOPSDONE -> TASKS_DMA.SEQ[0].START
    shorts |= (1UL << 6U);  // DMA_SEQ0 bus error -> STOP
  } else {
    shorts |= (1UL << 3U);  // LOOPSDONE -> TASKS_DMA.SEQ[1].START
    shorts |= (1UL << 7U);  // DMA_SEQ1 bus error -> STOP
  }
  reg32(base_ + pwm::SHORTS) = shorts;

  clearEvent(base_, pwm::EVENTS_STOPPED);
  clearEvent(base_, pwm::EVENTS_LOOPSDONE);
  clearEvent(base_, pwm::EVENTS_PWMPERIODEND);
  clearEvent(base_, pwmEventSeqStartedOffset(sequence));
  clearEvent(base_, pwmEventSeqEndOffset(sequence));
  clearEvent(base_, pwmEventDmaSeqEndOffset(sequence));

  reg32(base_ + pwmTaskSeqStartOffset(sequence)) = 1;
  return waitForEvent(base_, pwmEventSeqStartedOffset(sequence), spinLimit);
}

bool Pwm::stop(uint32_t spinLimit) {
  if (!configured_) {
    return false;
  }

  clearEvent(base_, pwm::EVENTS_STOPPED);
  reg32(base_ + pwm::TASKS_STOP) = 1;
  reg32(base_ + pwm::SHORTS) = 0;

  return waitForEvent(base_, pwm::EVENTS_STOPPED, spinLimit);
}

void Pwm::end() {
  if (configured_) {
    stop(200000UL);
  }

  reg32(base_ + pwm::ENABLE) = pwm::ENABLE_DISABLED;
  reg32(base_ + pwm::SHORTS) = 0;

  for (uint8_t ch = 0; ch < 4U; ++ch) {
    reg32(base_ + pwm::PSEL_OUT + (static_cast<uint32_t>(ch) * sizeof(uint32_t))) =
        PSEL_DISCONNECTED;
  }

  configured_ = false;
}

bool Pwm::pollPeriodEnd(bool clearEventFlag) {
  const bool fired = (reg32(base_ + pwm::EVENTS_PWMPERIODEND) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, pwm::EVENTS_PWMPERIODEND);
  }
  return fired;
}

bool Pwm::configureClockAndTop(uint32_t frequencyHz) {
  PwmTiming timing{};
  if (!computePwmTiming(frequencyHz, &timing)) {
    return false;
  }

  prescaler_ = timing.prescaler;
  countertop_ = timing.countertop;

  reg32(base_ + pwm::PRESCALER) = prescaler_;
  reg32(base_ + pwm::COUNTERTOP) = countertop_;
  return true;
}

void Pwm::updateSequenceWord() {
  if (countertop_ < 3U) {
    countertop_ = 3U;
  }

  uint32_t pulse = (static_cast<uint32_t>(countertop_) * dutyPermille_ + 500U) / 1000U;
  if (pulse > countertop_) {
    pulse = countertop_;
  }

  uint16_t word = static_cast<uint16_t>(pulse & 0x7FFFU);
  if (!activeHigh_) {
    word |= 0x8000U;
  }

  sequence_[0] = word;
  sequence_[1] = word;
  sequence_[2] = word;
  sequence_[3] = word;
}

Gpiote::Gpiote(uint32_t base, uint8_t channelCount)
    : base_(base), channelCount_(channelCount), callbacks_{}, callbackContext_{} {
  if (channelCount_ > 8U) {
    channelCount_ = 8U;
  }
}

bool Gpiote::configureEvent(uint8_t channel, const Pin& pin, GpiotePolarity polarity,
                            bool enableInterruptFlag) {
  if (channel >= channelCount_ || !isConnected(pin)) {
    return false;
  }

  if (!Gpio::configure(pin, GpioDirection::kInput, GpioPull::kDisabled)) {
    return false;
  }

  uint32_t config = 0;
  config |= (gpiote::MODE_EVENT << gpiote::CONFIG_MODE_Pos);
  config |= (static_cast<uint32_t>(pin.pin) << gpiote::CONFIG_PSEL_Pos);
  config |= (static_cast<uint32_t>(pin.port) << gpiote::CONFIG_PORT_Pos);
  config |= (static_cast<uint32_t>(polarity) << gpiote::CONFIG_POLARITY_Pos);

  reg32(base_ + gpioteConfigOffset(channel)) = config;
  clearEvent(base_, gpioteInEventOffset(channel));

  if (enableInterruptFlag) {
    enableInterrupt(channel, true);
  }
  return true;
}

bool Gpiote::configureTask(uint8_t channel, const Pin& pin, GpiotePolarity polarity,
                           bool initialHigh) {
  if (channel >= channelCount_ || !isConnected(pin)) {
    return false;
  }

  if (!Gpio::configure(pin, GpioDirection::kOutput, GpioPull::kDisabled)) {
    return false;
  }
  Gpio::write(pin, initialHigh);

  uint32_t config = 0;
  config |= (gpiote::MODE_TASK << gpiote::CONFIG_MODE_Pos);
  config |= (static_cast<uint32_t>(pin.pin) << gpiote::CONFIG_PSEL_Pos);
  config |= (static_cast<uint32_t>(pin.port) << gpiote::CONFIG_PORT_Pos);
  config |= (static_cast<uint32_t>(polarity) << gpiote::CONFIG_POLARITY_Pos);
  if (initialHigh) {
    config |= (1UL << gpiote::CONFIG_OUTINIT_Pos);
  }

  reg32(base_ + gpioteConfigOffset(channel)) = config;
  return true;
}

void Gpiote::disableChannel(uint8_t channel) {
  if (channel >= channelCount_) {
    return;
  }
  reg32(base_ + gpioteConfigOffset(channel)) = 0;
  enableInterrupt(channel, false);
  clearEvent(base_, gpioteInEventOffset(channel));
}

bool Gpiote::triggerTaskOut(uint8_t channel) {
  if (channel >= channelCount_) {
    return false;
  }
  reg32(base_ + gpioteTaskOutOffset(channel)) = 1;
  return true;
}

bool Gpiote::triggerTaskSet(uint8_t channel) {
  if (channel >= channelCount_) {
    return false;
  }
  reg32(base_ + gpioteTaskSetOffset(channel)) = 1;
  return true;
}

bool Gpiote::triggerTaskClr(uint8_t channel) {
  if (channel >= channelCount_) {
    return false;
  }
  reg32(base_ + gpioteTaskClrOffset(channel)) = 1;
  return true;
}

bool Gpiote::pollInEvent(uint8_t channel, bool clearEventFlag) {
  if (channel >= channelCount_) {
    return false;
  }

  const uint32_t offset = gpioteInEventOffset(channel);
  const bool fired = (reg32(base_ + offset) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, offset);
  }
  return fired;
}

bool Gpiote::pollPortEvent(bool clearEventFlag) {
  const bool fired = (reg32(base_ + gpiote::EVENTS_PORT_NONSECURE) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, gpiote::EVENTS_PORT_NONSECURE);
  }
  return fired;
}

volatile uint32_t* Gpiote::subscribeTaskOutConfigRegister(uint8_t channel) const {
  if (channel >= channelCount_) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + gpioteSubscribeOutOffset(channel)));
}

volatile uint32_t* Gpiote::subscribeTaskSetConfigRegister(uint8_t channel) const {
  if (channel >= channelCount_) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + gpioteSubscribeSetOffset(channel)));
}

volatile uint32_t* Gpiote::subscribeTaskClrConfigRegister(uint8_t channel) const {
  if (channel >= channelCount_) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + gpioteSubscribeClrOffset(channel)));
}

void Gpiote::enableInterrupt(uint8_t channel, bool enable) {
  if (channel >= channelCount_) {
    return;
  }

  const uint32_t mask = (1UL << channel);
  reg32(base_ + (enable ? gpiote::INTENSET0 : gpiote::INTENCLR0)) = mask;
}

bool Gpiote::attachInCallback(uint8_t channel, InCallback callback, void* context) {
  if (channel >= channelCount_) {
    return false;
  }
  callbacks_[channel] = callback;
  callbackContext_[channel] = context;
  return true;
}

void Gpiote::service() {
  for (uint8_t ch = 0; ch < channelCount_; ++ch) {
    const uint32_t offset = gpioteInEventOffset(ch);
    if (reg32(base_ + offset) == 0U) {
      continue;
    }

    clearEvent(base_, offset);
    if (callbacks_[ch] != nullptr) {
      callbacks_[ch](ch, callbackContext_[ch]);
    }
  }
}

Dppic::Dppic(uint32_t base)
    : dppic_(reinterpret_cast<NRF_DPPIC_Type*>(static_cast<uintptr_t>(base))) {}

bool Dppic::enableChannel(uint8_t channel, bool enable) {
  if (channel >= 24U || dppic_ == nullptr) {
    return false;
  }

  const uint32_t mask = (1UL << channel);
  if (enable) {
    dppic_->CHENSET = mask;
  } else {
    dppic_->CHENCLR = mask;
  }
  return true;
}

bool Dppic::channelEnabled(uint8_t channel) const {
  if (channel >= 24U || dppic_ == nullptr) {
    return false;
  }
  return (dppic_->CHEN & (1UL << channel)) != 0U;
}

bool Dppic::configurePublish(volatile uint32_t* publishRegister, uint8_t channel,
                             bool enable) const {
  if (publishRegister == nullptr || channel >= 24U) {
    return false;
  }
  *publishRegister = enable ? nrf54l15::dppic::configValue(channel) : 0U;
  return true;
}

bool Dppic::configureSubscribe(volatile uint32_t* subscribeRegister, uint8_t channel,
                               bool enable) const {
  if (subscribeRegister == nullptr || channel >= 24U) {
    return false;
  }
  *subscribeRegister = enable ? nrf54l15::dppic::configValue(channel) : 0U;
  return true;
}

bool Dppic::connect(volatile uint32_t* publishRegister,
                    volatile uint32_t* subscribeRegister,
                    uint8_t channel,
                    bool enableChannelFlag) const {
  if (!configurePublish(publishRegister, channel, true) ||
      !configureSubscribe(subscribeRegister, channel, true)) {
    return false;
  }

  if (enableChannelFlag && dppic_ != nullptr) {
    dppic_->CHENSET = (1UL << channel);
  }
  return true;
}

bool Dppic::disconnectPublish(volatile uint32_t* publishRegister) const {
  if (publishRegister == nullptr) {
    return false;
  }
  *publishRegister = 0U;
  return true;
}

bool Dppic::disconnectSubscribe(volatile uint32_t* subscribeRegister) const {
  if (subscribeRegister == nullptr) {
    return false;
  }
  *subscribeRegister = 0U;
  return true;
}

namespace {

constexpr uint32_t kVdmaDescriptorAttrEcbData =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_EcbData
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrAarHash =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_AarHash
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrAarPrand =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_AarPrand
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrAarIrk =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_AarIrk
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
// Nordic confirmed the output job list needs a dedicated 0x11 descriptor byte,
// not the generic AarHash value that the datasheet implies.
constexpr uint32_t kVdmaDescriptorAttrAarResolvedIndex =
    (0x11UL << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrCcmAlen =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_CcmAlen
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrCcmMlen =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_CcmMlen
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrCcmAdata =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_CcmAdata
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrCcmMdata =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_CcmMdata
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr size_t kAarMaxIrksScratch = 64U;
constexpr uint32_t kCracenRngFailureMask =
    CRACENCORE_RNGCONTROL_STATUS_REPFAIL_Msk |
    CRACENCORE_RNGCONTROL_STATUS_PROPFAIL_Msk |
    CRACENCORE_RNGCONTROL_STATUS_STARTUPFAIL_Msk |
    CRACENCORE_RNGCONTROL_STATUS_FIFOACCFAIL_Msk;

alignas(4) uint8_t g_aarAddressScratch[6] = {};
alignas(4) uint8_t g_aarIrkScratch[kAarMaxIrksScratch * 16U] = {};
alignas(4) uint16_t g_aarResolvedIndexScratch = 0xFFFFU;
alignas(4) NRF_VDMADESCRIPTOR_Type g_aarInJobs[kAarMaxIrksScratch + 3U] = {};
alignas(4) NRF_VDMADESCRIPTOR_Type g_aarOutJobs[2] = {};

uint32_t packBigEndianWord(const uint8_t* bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24U) |
         (static_cast<uint32_t>(bytes[1]) << 16U) |
         (static_cast<uint32_t>(bytes[2]) << 8U) |
         static_cast<uint32_t>(bytes[3]);
}

void configureVdmaDescriptor(NRF_VDMADESCRIPTOR_Type* descriptor,
                             const uint8_t* data,
                             size_t length,
                             uint32_t attrBits) {
  if (descriptor == nullptr) {
    return;
  }

  descriptor->PTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
  descriptor->CONFIG =
      (static_cast<uint32_t>(length) & VDMADESCRIPTOR_CONFIG_CNT_Msk) | attrBits;
}

void zeroVdmaDescriptor(NRF_VDMADESCRIPTOR_Type* descriptor) {
  if (descriptor == nullptr) {
    return;
  }
  descriptor->PTR = 0U;
  descriptor->CONFIG = 0U;
}

void stageBleCcmNonceWords(const uint8_t iv[8], uint64_t counter,
                           uint8_t direction, uint32_t outWords[4]) {
  if (iv == nullptr || outWords == nullptr) {
    return;
  }

  uint8_t nonce[13] = {};
  uint8_t nonceRegisterBytes[16] = {};

  buildBleCcmNonce(iv, counter, direction, nonce);
  memcpy(&nonceRegisterBytes[3], nonce, sizeof(nonce));

  for (uint8_t i = 0U; i < 4U; ++i) {
    outWords[i] = packBigEndianWord(&nonceRegisterBytes[(3U - i) * 4U]);
  }
}

}  // namespace

CracenRng::CracenRng(uint32_t controlBase, uint32_t coreBase)
    : cracen_(reinterpret_cast<NRF_CRACEN_Type*>(
          static_cast<uintptr_t>(controlBase))),
      core_(reinterpret_cast<NRF_CRACENCORE_Type*>(
          static_cast<uintptr_t>(coreBase))),
      active_(false) {}

bool CracenRng::begin(uint32_t spinLimit) {
  if (cracen_ == nullptr || core_ == nullptr) {
    return false;
  }

  const uint32_t nextEnable = cracen_->ENABLE | CRACEN_ENABLE_RNG_Msk;
  cracen_->ENABLE = nextEnable;
  clearEvent();

  uint32_t control = core_->RNGCONTROL.CONTROL;
  control &= ~CRACENCORE_RNGCONTROL_CONTROL_ENABLE_Msk;
  core_->RNGCONTROL.CONTROL = control;
  __asm volatile("dsb 0xF" ::: "memory");

  control |= CRACENCORE_RNGCONTROL_CONTROL_ENABLE_Msk;
  core_->RNGCONTROL.CONTROL = control;
  __asm volatile("dsb 0xF" ::: "memory");

  active_ = true;
  return ensureDataAvailable(spinLimit);
}

void CracenRng::end() {
  if (cracen_ == nullptr || core_ == nullptr) {
    active_ = false;
    return;
  }

  uint32_t control = core_->RNGCONTROL.CONTROL;
  control &= ~CRACENCORE_RNGCONTROL_CONTROL_ENABLE_Msk;
  core_->RNGCONTROL.CONTROL = control;
  __asm volatile("dsb 0xF" ::: "memory");

  cracen_->ENABLE &= ~CRACEN_ENABLE_RNG_Msk;
  clearEvent();
  active_ = false;
}

bool CracenRng::fill(void* data, size_t length, uint32_t spinLimit) {
  if (data == nullptr) {
    return false;
  }

  const bool temporaryBegin = !active_;
  if (temporaryBegin && !begin(spinLimit)) {
    return false;
  }

  uint8_t* out = static_cast<uint8_t*>(data);
  size_t remaining = length;
  bool ok = true;
  while (remaining > 0U) {
    uint32_t word = 0U;
    if (!randomWord(&word, spinLimit)) {
      ok = false;
      break;
    }

    const size_t chunk = (remaining < sizeof(word)) ? remaining : sizeof(word);
    memcpy(out, &word, chunk);
    out += chunk;
    remaining -= chunk;
  }

  if (temporaryBegin) {
    end();
  }
  return ok;
}

bool CracenRng::randomWord(uint32_t* outWord, uint32_t spinLimit) {
  if (outWord == nullptr) {
    return false;
  }

  const bool temporaryBegin = !active_;
  if (temporaryBegin && !begin(spinLimit)) {
    return false;
  }

  const bool ready = ensureDataAvailable(spinLimit);
  if (ready) {
    *outWord = core_->RNGCONTROL.FIFO[0];
  }

  if (temporaryBegin) {
    end();
  }
  return ready;
}

uint32_t CracenRng::availableWords() const {
  if (core_ == nullptr) {
    return 0U;
  }
  return core_->RNGCONTROL.FIFOLEVEL &
         CRACENCORE_RNGCONTROL_FIFOLEVEL_FIFOLEVEL_Msk;
}

uint32_t CracenRng::status() const {
  if (core_ == nullptr) {
    return 0xFFFFFFFFUL;
  }
  return core_->RNGCONTROL.STATUS;
}

bool CracenRng::healthy() const {
  const uint32_t currentStatus = status();
  const uint32_t state =
      (currentStatus & CRACENCORE_RNGCONTROL_STATUS_STATE_Msk) >>
      CRACENCORE_RNGCONTROL_STATUS_STATE_Pos;
  return (currentStatus != 0xFFFFFFFFUL) &&
         ((currentStatus & kCracenRngFailureMask) == 0U) &&
         (state != CRACENCORE_RNGCONTROL_STATUS_STATE_ERROR);
}

bool CracenRng::active() const { return active_; }

void CracenRng::clearEvent() {
  if (cracen_ == nullptr) {
    return;
  }
  cracen_->EVENTS_RNG = 0U;
}

bool CracenRng::ensureDataAvailable(uint32_t spinLimit) {
  if (!active_ || core_ == nullptr) {
    return false;
  }

  while (spinLimit-- > 0U) {
    if (availableWords() != 0U) {
      return healthy();
    }
    if (!healthy()) {
      return false;
    }
  }
  return false;
}

Aar::Aar(uint32_t base)
    : aar_(reinterpret_cast<NRF_AAR_Type*>(static_cast<uintptr_t>(base))) {}

bool Aar::resolveFirst(const uint8_t address[6], const uint8_t* irks,
                       size_t irkCount, bool* outResolved, uint16_t* outIndex,
                       uint32_t spinLimit) {
  if (aar_ == nullptr || address == nullptr || irks == nullptr ||
      outResolved == nullptr || irkCount == 0U ||
      irkCount > kAarMaxIrksScratch) {
    return false;
  }

  *outResolved = false;
  if (outIndex != nullptr) {
    *outIndex = 0xFFFFU;
  }

  uint8_t* const irkRam = g_aarIrkScratch;
  NRF_VDMADESCRIPTOR_Type* const inJobs = g_aarInJobs;
  NRF_VDMADESCRIPTOR_Type* const outJobs = g_aarOutJobs;

  memset(g_aarAddressScratch, 0, sizeof(g_aarAddressScratch));
  memset(g_aarIrkScratch, 0, irkCount * 16U);
  memset(g_aarInJobs, 0, (irkCount + 3U) * sizeof(NRF_VDMADESCRIPTOR_Type));
  memset(g_aarOutJobs, 0, sizeof(g_aarOutJobs));
  g_aarResolvedIndexScratch = 0xFFFFU;

  memcpy(g_aarAddressScratch, address, sizeof(g_aarAddressScratch));
  // Zephyr pre-swaps nRF54L resolving-list IRKs before feeding AAR.
  for (size_t irkIndex = 0; irkIndex < irkCount; ++irkIndex) {
    const uint8_t* src = irks + (irkIndex * 16U);
    uint8_t* dst = irkRam + (irkIndex * 16U);
    for (size_t byteIndex = 0; byteIndex < 16U; ++byteIndex) {
      dst[byteIndex] = src[15U - byteIndex];
    }
  }

  configureVdmaDescriptor(&inJobs[0], &g_aarAddressScratch[0], 3U,
                          kVdmaDescriptorAttrAarHash);
  configureVdmaDescriptor(&inJobs[1], &g_aarAddressScratch[3], 3U,
                          kVdmaDescriptorAttrAarPrand);
  for (size_t irkIndex = 0; irkIndex < irkCount; ++irkIndex) {
    configureVdmaDescriptor(&inJobs[2U + irkIndex], irkRam + (irkIndex * 16U),
                            16U, kVdmaDescriptorAttrAarIrk);
  }
  zeroVdmaDescriptor(&inJobs[2U + irkCount]);

  configureVdmaDescriptor(&outJobs[0],
                          reinterpret_cast<const uint8_t*>(&g_aarResolvedIndexScratch),
                          sizeof(g_aarResolvedIndexScratch),
                          kVdmaDescriptorAttrAarResolvedIndex);
  zeroVdmaDescriptor(&outJobs[1]);

  aar_->ENABLE = AAR_ENABLE_ENABLE_Disabled;
  aar_->INTENCLR = 0xFFFFFFFFUL;
  clearEvents();
  NVIC_ClearPendingIRQ(AAR00_IRQn);
  aar_->ENABLE = AAR_ENABLE_ENABLE_Enabled;
  aar_->MAXRESOLVED = 1U;
  aar_->IN.PTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&inJobs[0]));
  aar_->OUT.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&outJobs[0]));
  __asm volatile("dsb 0xF" ::: "memory");

  aar_->TASKS_START = AAR_TASKS_START_TASKS_START_Trigger;

  bool completed = false;
  while (spinLimit-- > 0U) {
    if (aar_->EVENTS_ERROR != 0U) {
      completed = false;
      break;
    }
    if (aar_->EVENTS_END != 0U) {
      completed = true;
      break;
    }
  }

  const uint32_t finalAmount = aar_->OUT.AMOUNT & AAR_OUT_AMOUNT_AMOUNT_Msk;
  const bool resolved = (aar_->EVENTS_RESOLVED != 0U) &&
                        (aar_->EVENTS_NOTRESOLVED == 0U);
  const uint16_t resolvedIndex = g_aarResolvedIndexScratch;

  if (!completed) {
    aar_->TASKS_STOP = AAR_TASKS_STOP_TASKS_STOP_Trigger;
  }
  *outResolved = completed && resolved;

  aar_->ENABLE = AAR_ENABLE_ENABLE_Disabled;
  NVIC_DisableIRQ(AAR00_IRQn);
  NVIC_ClearPendingIRQ(AAR00_IRQn);

  if (completed && resolved && outIndex != nullptr && finalAmount >= 2U) {
    *outIndex = resolvedIndex;
  }

  return completed && errorStatus() == AAR_ERRORSTATUS_ERRORSTATUS_NoError;
}

bool Aar::resolveSingle(const uint8_t address[6], const uint8_t irk[16],
                        bool* outResolved, uint32_t spinLimit) {
  return resolveFirst(address, irk, 1U, outResolved, nullptr, spinLimit);
}

uint32_t Aar::errorStatus() const {
  if (aar_ == nullptr) {
    return AAR_ERRORSTATUS_ERRORSTATUS_DmaError;
  }
  return aar_->ERRORSTATUS & AAR_ERRORSTATUS_ERRORSTATUS_Msk;
}

uint32_t Aar::resolvedAmountBytes() const {
  if (aar_ == nullptr) {
    return 0U;
  }
  return aar_->OUT.AMOUNT & AAR_OUT_AMOUNT_AMOUNT_Msk;
}

void Aar::clearEvents() {
  if (aar_ == nullptr) {
    return;
  }
  aar_->EVENTS_END = 0U;
  aar_->EVENTS_RESOLVED = 0U;
  aar_->EVENTS_NOTRESOLVED = 0U;
  aar_->EVENTS_ERROR = 0U;
}

Ecb::Ecb(uint32_t base)
    : ecb_(reinterpret_cast<NRF_ECB_Type*>(static_cast<uintptr_t>(base))) {}

bool Ecb::encryptBlock(const uint8_t key[16], const uint8_t plaintext[16],
                       uint8_t ciphertext[16], uint32_t spinLimit) {
  if (ecb_ == nullptr || key == nullptr || plaintext == nullptr ||
      ciphertext == nullptr) {
    return false;
  }

  alignas(4) uint8_t inputBlock[16];
  alignas(4) NRF_VDMADESCRIPTOR_Type inJobs[2] = {};
  alignas(4) NRF_VDMADESCRIPTOR_Type outJobs[2] = {};

  memcpy(inputBlock, plaintext, sizeof(inputBlock));
  configureVdmaDescriptor(&inJobs[0], inputBlock, sizeof(inputBlock),
                          kVdmaDescriptorAttrEcbData);
  configureVdmaDescriptor(&outJobs[0], ciphertext, 16U,
                          kVdmaDescriptorAttrEcbData);
  zeroVdmaDescriptor(&inJobs[1]);
  zeroVdmaDescriptor(&outJobs[1]);

  clearEvents();

  for (uint8_t i = 0U; i < 4U; ++i) {
    ecb_->KEY.VALUE[i] = packBigEndianWord(&key[(3U - i) * 4U]);
  }

  ecb_->IN.PTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&inJobs[0]));
  ecb_->OUT.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&outJobs[0]));
  __asm volatile("dsb 0xF" ::: "memory");

  ecb_->TASKS_START = ECB_TASKS_START_TASKS_START_Trigger;
  while (spinLimit-- > 0U) {
    if (ecb_->EVENTS_END != 0U) {
      ecb_->EVENTS_END = 0U;
      return (ecb_->ERRORSTATUS & ECB_ERRORSTATUS_ERRORSTATUS_Msk) ==
             ECB_ERRORSTATUS_ERRORSTATUS_NoError;
    }
    if (ecb_->EVENTS_ERROR != 0U) {
      ecb_->EVENTS_ERROR = 0U;
      return false;
    }
  }

  ecb_->TASKS_STOP = ECB_TASKS_STOP_TASKS_STOP_Trigger;
  return false;
}

bool Ecb::encryptBlockInPlace(const uint8_t key[16], uint8_t block[16],
                              uint32_t spinLimit) {
  return encryptBlock(key, block, block, spinLimit);
}

uint32_t Ecb::errorStatus() const {
  if (ecb_ == nullptr) {
    return ECB_ERRORSTATUS_ERRORSTATUS_DmaError;
  }
  return ecb_->ERRORSTATUS & ECB_ERRORSTATUS_ERRORSTATUS_Msk;
}

void Ecb::clearEvents() {
  if (ecb_ == nullptr) {
    return;
  }
  ecb_->EVENTS_END = 0U;
  ecb_->EVENTS_ERROR = 0U;
}

Ccm::Ccm(uint32_t base)
    : ccm_(reinterpret_cast<NRF_CCM_Type*>(static_cast<uintptr_t>(base))) {}

bool Ccm::encryptBlePacket(const uint8_t key[16], const uint8_t iv[8],
                           uint64_t counter, uint8_t direction, uint8_t header,
                           const uint8_t* plaintext, uint8_t plaintextLen,
                           uint8_t* outCipherWithMic,
                           uint8_t* outCipherWithMicLen,
                           CcmBleDataRate dataRate, uint8_t adataMask,
                           uint32_t spinLimit) {
  if (ccm_ == nullptr || key == nullptr || iv == nullptr ||
      outCipherWithMic == nullptr || outCipherWithMicLen == nullptr) {
    return false;
  }
  if (plaintextLen > 0U && plaintext == nullptr) {
    return false;
  }
  if (plaintextLen > kBleDataPduMaxPayload) {
    return false;
  }

  *outCipherWithMicLen = 0U;

  alignas(4) uint8_t inputPayloadScratch[kBleDataPduMaxPayload + kBleMicLen] = {};
  alignas(4) uint8_t outputPayloadScratch[kBleDataPduMaxPayload + kBleMicLen] = {};
  alignas(4) uint8_t adataScratch[1] = {header};
  alignas(4) uint8_t adataOutScratch[1] = {};
  alignas(4) NRF_VDMADESCRIPTOR_Type inJobs[5] = {};
  alignas(4) NRF_VDMADESCRIPTOR_Type outJobs[5] = {};
  uint16_t inAlen = 1U;
  uint16_t inMlen = plaintextLen;
  uint16_t outAlen = 0xFFFFU;
  uint16_t outMlen = 0xFFFFU;
  uint32_t nonceWords[4] = {};

  if (plaintextLen > 0U) {
    memcpy(inputPayloadScratch, plaintext, plaintextLen);
  }

  configureVdmaDescriptor(
      &inJobs[0], reinterpret_cast<const uint8_t*>(&inAlen), sizeof(inAlen),
      kVdmaDescriptorAttrCcmAlen);
  configureVdmaDescriptor(
      &inJobs[1], reinterpret_cast<const uint8_t*>(&inMlen), sizeof(inMlen),
      kVdmaDescriptorAttrCcmMlen);
  configureVdmaDescriptor(&inJobs[2], adataScratch, sizeof(adataScratch),
                          kVdmaDescriptorAttrCcmAdata);
  configureVdmaDescriptor(&inJobs[3], inputPayloadScratch, plaintextLen,
                          kVdmaDescriptorAttrCcmMdata);
  zeroVdmaDescriptor(&inJobs[4]);

  configureVdmaDescriptor(
      &outJobs[0], reinterpret_cast<const uint8_t*>(&outAlen), sizeof(outAlen),
      kVdmaDescriptorAttrCcmAlen);
  configureVdmaDescriptor(
      &outJobs[1], reinterpret_cast<const uint8_t*>(&outMlen), sizeof(outMlen),
      kVdmaDescriptorAttrCcmMlen);
  configureVdmaDescriptor(&outJobs[2], adataOutScratch, sizeof(adataOutScratch),
                          kVdmaDescriptorAttrCcmAdata);
  configureVdmaDescriptor(&outJobs[3], outputPayloadScratch,
                          static_cast<size_t>(plaintextLen + kBleMicLen),
                          kVdmaDescriptorAttrCcmMdata);
  zeroVdmaDescriptor(&outJobs[4]);

  stageBleCcmNonceWords(iv, counter, direction, nonceWords);

  ccm_->ENABLE = CCM_ENABLE_ENABLE_Disabled;
  ccm_->INTENCLR = 0xFFFFFFFFUL;
  clearEvents();
  NVIC_ClearPendingIRQ(CCM00_IRQn);
  ccm_->ENABLE = CCM_ENABLE_ENABLE_Enabled;
  ccm_->MODE = (CCM_MODE_MODE_Encryption << CCM_MODE_MODE_Pos) |
               (CCM_MODE_PROTOCOL_Ble << CCM_MODE_PROTOCOL_Pos) |
               (static_cast<uint32_t>(dataRate) << CCM_MODE_DATARATE_Pos) |
               (CCM_MODE_MACLEN_M4 << CCM_MODE_MACLEN_Pos);
  ccm_->RATEOVERRIDE = static_cast<uint32_t>(dataRate);
  ccm_->ADATAMASK = static_cast<uint32_t>(adataMask);
  for (uint8_t i = 0U; i < 4U; ++i) {
    ccm_->KEY.VALUE[i] = packBigEndianWord(&key[(3U - i) * 4U]);
    ccm_->NONCE.VALUE[i] = nonceWords[i];
  }
  ccm_->IN.PTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&inJobs[0]));
  ccm_->OUT.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&outJobs[0]));
  __asm volatile("dsb 0xF" ::: "memory");

  bool finished = false;
  ccm_->TASKS_START = CCM_TASKS_START_TASKS_START_Trigger;
  while (spinLimit-- > 0U) {
    if (ccm_->EVENTS_ERROR != 0U) {
      break;
    }
    if (ccm_->EVENTS_END != 0U) {
      finished = true;
      break;
    }
  }

  const uint32_t error = errorStatus();
  if (!finished) {
    ccm_->TASKS_STOP = CCM_TASKS_STOP_TASKS_STOP_Trigger;
  }
  const uint16_t actualOutMlen =
      (outMlen != 0U && outMlen != 0xFFFFU)
          ? outMlen
          : static_cast<uint16_t>(plaintextLen + kBleMicLen);
  const bool ok = finished &&
                  ((ccm_->EVENTS_ERROR & CCM_EVENTS_ERROR_EVENTS_ERROR_Msk) == 0U) &&
                  (error == CCM_ERRORSTATUS_ERRORSTATUS_NoError) &&
                  (actualOutMlen <= sizeof(outputPayloadScratch));

  if (ok) {
    memcpy(outCipherWithMic, outputPayloadScratch, actualOutMlen);
    *outCipherWithMicLen = static_cast<uint8_t>(actualOutMlen);
  }

  ccm_->ENABLE = CCM_ENABLE_ENABLE_Disabled;
  NVIC_ClearPendingIRQ(CCM00_IRQn);
  return ok;
}

bool Ccm::decryptBlePacket(const uint8_t key[16], const uint8_t iv[8],
                           uint64_t counter, uint8_t direction, uint8_t header,
                           const uint8_t* cipherWithMic, uint8_t cipherWithMicLen,
                           uint8_t* outPlaintext, uint8_t* outPlaintextLen,
                           bool* outMacValid, CcmBleDataRate dataRate,
                           uint8_t adataMask, uint32_t spinLimit) {
  if (ccm_ == nullptr || key == nullptr || iv == nullptr ||
      cipherWithMic == nullptr || outPlaintext == nullptr ||
      outPlaintextLen == nullptr) {
    return false;
  }
  if (outMacValid != nullptr) {
    *outMacValid = false;
  }
  *outPlaintextLen = 0U;

  if (cipherWithMicLen < kBleMicLen) {
    return false;
  }

  const uint8_t payloadLen = static_cast<uint8_t>(cipherWithMicLen - kBleMicLen);
  if (payloadLen > kBleDataPduMaxPayload) {
    return false;
  }

  alignas(4) uint8_t inputPayloadScratch[kBleDataPduMaxPayload + kBleMicLen] = {};
  alignas(4) uint8_t outputPayloadScratch[kBleDataPduMaxPayload + kBleMicLen] = {};
  alignas(4) uint8_t adataScratch[1] = {header};
  alignas(4) uint8_t adataOutScratch[1] = {};
  alignas(4) NRF_VDMADESCRIPTOR_Type inJobs[5] = {};
  alignas(4) NRF_VDMADESCRIPTOR_Type outJobs[5] = {};
  uint16_t inAlen = 1U;
  uint16_t inMlen = cipherWithMicLen;
  uint16_t outAlen = 0xFFFFU;
  uint16_t outMlen = 0xFFFFU;
  uint32_t nonceWords[4] = {};

  memcpy(inputPayloadScratch, cipherWithMic, cipherWithMicLen);

  configureVdmaDescriptor(
      &inJobs[0], reinterpret_cast<const uint8_t*>(&inAlen), sizeof(inAlen),
      kVdmaDescriptorAttrCcmAlen);
  configureVdmaDescriptor(
      &inJobs[1], reinterpret_cast<const uint8_t*>(&inMlen), sizeof(inMlen),
      kVdmaDescriptorAttrCcmMlen);
  configureVdmaDescriptor(&inJobs[2], adataScratch, sizeof(adataScratch),
                          kVdmaDescriptorAttrCcmAdata);
  configureVdmaDescriptor(&inJobs[3], inputPayloadScratch, cipherWithMicLen,
                          kVdmaDescriptorAttrCcmMdata);
  zeroVdmaDescriptor(&inJobs[4]);

  configureVdmaDescriptor(
      &outJobs[0], reinterpret_cast<const uint8_t*>(&outAlen), sizeof(outAlen),
      kVdmaDescriptorAttrCcmAlen);
  configureVdmaDescriptor(
      &outJobs[1], reinterpret_cast<const uint8_t*>(&outMlen), sizeof(outMlen),
      kVdmaDescriptorAttrCcmMlen);
  configureVdmaDescriptor(&outJobs[2], adataOutScratch, sizeof(adataOutScratch),
                          kVdmaDescriptorAttrCcmAdata);
  configureVdmaDescriptor(&outJobs[3], outputPayloadScratch, payloadLen,
                          kVdmaDescriptorAttrCcmMdata);
  zeroVdmaDescriptor(&outJobs[4]);

  stageBleCcmNonceWords(iv, counter, direction, nonceWords);

  ccm_->ENABLE = CCM_ENABLE_ENABLE_Disabled;
  ccm_->INTENCLR = 0xFFFFFFFFUL;
  clearEvents();
  NVIC_ClearPendingIRQ(CCM00_IRQn);
  ccm_->ENABLE = CCM_ENABLE_ENABLE_Enabled;
  ccm_->MODE = (CCM_MODE_MODE_Decryption << CCM_MODE_MODE_Pos) |
               (CCM_MODE_PROTOCOL_Ble << CCM_MODE_PROTOCOL_Pos) |
               (static_cast<uint32_t>(dataRate) << CCM_MODE_DATARATE_Pos) |
               (CCM_MODE_MACLEN_M4 << CCM_MODE_MACLEN_Pos);
  ccm_->RATEOVERRIDE = static_cast<uint32_t>(dataRate);
  ccm_->ADATAMASK = static_cast<uint32_t>(adataMask);
  for (uint8_t i = 0U; i < 4U; ++i) {
    ccm_->KEY.VALUE[i] = packBigEndianWord(&key[(3U - i) * 4U]);
    ccm_->NONCE.VALUE[i] = nonceWords[i];
  }
  ccm_->IN.PTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&inJobs[0]));
  ccm_->OUT.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&outJobs[0]));
  __asm volatile("dsb 0xF" ::: "memory");

  bool finished = false;
  ccm_->TASKS_START = CCM_TASKS_START_TASKS_START_Trigger;
  while (spinLimit-- > 0U) {
    if (ccm_->EVENTS_ERROR != 0U) {
      break;
    }
    if (ccm_->EVENTS_END != 0U) {
      finished = true;
      break;
    }
  }

  const uint32_t error = errorStatus();
  const bool macValid = macStatus();
  if (outMacValid != nullptr) {
    *outMacValid = macValid;
  }
  if (!finished) {
    ccm_->TASKS_STOP = CCM_TASKS_STOP_TASKS_STOP_Trigger;
  }
  const bool noError = finished &&
                       ((ccm_->EVENTS_ERROR & CCM_EVENTS_ERROR_EVENTS_ERROR_Msk) == 0U) &&
                       (error == CCM_ERRORSTATUS_ERRORSTATUS_NoError) &&
                       (outMlen <= payloadLen);

  if (noError) {
    memcpy(outPlaintext, outputPayloadScratch, outMlen);
    *outPlaintextLen = static_cast<uint8_t>(outMlen);
  }

  ccm_->ENABLE = CCM_ENABLE_ENABLE_Disabled;
  NVIC_ClearPendingIRQ(CCM00_IRQn);
  return noError && macValid;
}

uint32_t Ccm::errorStatus() const {
  if (ccm_ == nullptr) {
    return CCM_ERRORSTATUS_ERRORSTATUS_DmaError;
  }
  return ccm_->ERRORSTATUS & CCM_ERRORSTATUS_ERRORSTATUS_Msk;
}

bool Ccm::macStatus() const {
  if (ccm_ == nullptr) {
    return false;
  }
  return (ccm_->MACSTATUS & CCM_MACSTATUS_MACSTATUS_Msk) ==
         CCM_MACSTATUS_MACSTATUS_CheckPassed;
}

void Ccm::clearEvents() {
  if (ccm_ == nullptr) {
    return;
  }
  ccm_->EVENTS_END = 0U;
  ccm_->EVENTS_ERROR = 0U;
}

Comp::Comp(uint32_t base)
    : comp_(reinterpret_cast<NRF_COMP_Type*>(static_cast<uintptr_t>(base))),
      active_(false) {}

bool Comp::beginThreshold(const Pin& inputPin, uint16_t thresholdPermille,
                          uint16_t hysteresisPermille, CompReference reference,
                          CompSpeedMode speed, const Pin& externalReferencePin,
                          CompCurrentSource currentSource, uint32_t spinLimit) {
  return beginSingleEnded(inputPin, reference, thresholdPermille,
                          hysteresisPermille, speed, externalReferencePin,
                          currentSource, spinLimit);
}

bool Comp::beginSingleEnded(const Pin& inputPin, CompReference reference,
                            uint16_t thresholdPermille,
                            uint16_t hysteresisPermille, CompSpeedMode speed,
                            const Pin& externalReferencePin,
                            CompCurrentSource currentSource,
                            uint32_t spinLimit) {
  if (active_) {
    end();
  }

  if (!pinSupportsAnalogInput(inputPin)) {
    return false;
  }
  if (reference == CompReference::kExternalAref &&
      !pinSupportsAnalogInput(externalReferencePin)) {
    return false;
  }
  if (!claimComparator(ComparatorOwner::kComp)) {
    return false;
  }
  if (!configureAnalogPeripheralPin(inputPin) ||
      (reference == CompReference::kExternalAref &&
       !configureAnalogPeripheralPin(externalReferencePin))) {
    releaseComparator(ComparatorOwner::kComp);
    return false;
  }

  thresholdPermille = clampPermille(thresholdPermille);
  hysteresisPermille = clampPermille(hysteresisPermille);
  const uint16_t halfHysteresis = hysteresisPermille / 2U;
  uint16_t lowPermille =
      (thresholdPermille > halfHysteresis) ? (thresholdPermille - halfHysteresis) : 0U;
  uint16_t highPermille =
      thresholdPermille + static_cast<uint16_t>(hysteresisPermille - halfHysteresis);
  if (highPermille > 1000U) {
    highPermille = 1000U;
  }
  if (lowPermille > highPermille) {
    lowPermille = highPermille;
  }

  comp_->TASKS_STOP = COMP_TASKS_STOP_TASKS_STOP_Trigger;
  comp_->ENABLE = (COMP_ENABLE_ENABLE_Disabled << COMP_ENABLE_ENABLE_Pos);
  comp_->INTENCLR = 0xFFFFFFFFUL;
  comp_->SHORTS = 0U;
  clearEvents();

  comp_->PSEL = makeCompPinSelect(inputPin);
  comp_->REFSEL = ((static_cast<uint32_t>(reference) << COMP_REFSEL_REFSEL_Pos) &
                   COMP_REFSEL_REFSEL_Msk);
  comp_->EXTREFSEL = (reference == CompReference::kExternalAref)
                         ? makeCompPinSelect(externalReferencePin)
                         : 0U;
  comp_->TH = compThresholdRegValue(lowPermille, highPermille);
  comp_->MODE = ((static_cast<uint32_t>(speed) << COMP_MODE_SP_Pos) &
                 COMP_MODE_SP_Msk) |
                ((COMP_MODE_MAIN_SE << COMP_MODE_MAIN_Pos) &
                 COMP_MODE_MAIN_Msk);
  comp_->HYST = ((COMP_HYST_HYST_NoHyst << COMP_HYST_HYST_Pos) &
                 COMP_HYST_HYST_Msk);
  setCurrentSource(currentSource);

  comp_->ENABLE = ((COMP_ENABLE_ENABLE_Enabled << COMP_ENABLE_ENABLE_Pos) &
                   COMP_ENABLE_ENABLE_Msk);
  clearEvents();
  comp_->TASKS_START = COMP_TASKS_START_TASKS_START_Trigger;
  if (!waitForCompReady(&comp_->EVENTS_READY, spinLimit)) {
    comp_->TASKS_STOP = COMP_TASKS_STOP_TASKS_STOP_Trigger;
    comp_->ENABLE = (COMP_ENABLE_ENABLE_Disabled << COMP_ENABLE_ENABLE_Pos);
    releaseComparator(ComparatorOwner::kComp);
    return false;
  }

  active_ = true;
  return true;
}

bool Comp::beginDifferential(const Pin& positivePin, const Pin& negativePin,
                             CompSpeedMode speed, bool hysteresis,
                             CompCurrentSource currentSource,
                             uint32_t spinLimit) {
  if (active_) {
    end();
  }

  if (!pinSupportsAnalogInput(positivePin) ||
      !pinSupportsAnalogInput(negativePin)) {
    return false;
  }
  if ((positivePin.port == negativePin.port) &&
      (positivePin.pin == negativePin.pin)) {
    return false;
  }
  if (!claimComparator(ComparatorOwner::kComp)) {
    return false;
  }
  if (!configureAnalogPeripheralPin(positivePin) ||
      !configureAnalogPeripheralPin(negativePin)) {
    releaseComparator(ComparatorOwner::kComp);
    return false;
  }

  comp_->TASKS_STOP = COMP_TASKS_STOP_TASKS_STOP_Trigger;
  comp_->ENABLE = (COMP_ENABLE_ENABLE_Disabled << COMP_ENABLE_ENABLE_Pos);
  comp_->INTENCLR = 0xFFFFFFFFUL;
  comp_->SHORTS = 0U;
  clearEvents();

  comp_->PSEL = makeCompPinSelect(positivePin);
  comp_->EXTREFSEL = makeCompPinSelect(negativePin);
  comp_->REFSEL = 0U;
  comp_->TH = compThresholdRegValue(500U, 500U);
  comp_->MODE = ((static_cast<uint32_t>(speed) << COMP_MODE_SP_Pos) &
                 COMP_MODE_SP_Msk) |
                ((COMP_MODE_MAIN_Diff << COMP_MODE_MAIN_Pos) &
                 COMP_MODE_MAIN_Msk);
  comp_->HYST = (((hysteresis ? COMP_HYST_HYST_Hyst40mV
                              : COMP_HYST_HYST_NoHyst)
                  << COMP_HYST_HYST_Pos) &
                 COMP_HYST_HYST_Msk);
  setCurrentSource(currentSource);

  comp_->ENABLE = ((COMP_ENABLE_ENABLE_Enabled << COMP_ENABLE_ENABLE_Pos) &
                   COMP_ENABLE_ENABLE_Msk);
  clearEvents();
  comp_->TASKS_START = COMP_TASKS_START_TASKS_START_Trigger;
  if (!waitForCompReady(&comp_->EVENTS_READY, spinLimit)) {
    comp_->TASKS_STOP = COMP_TASKS_STOP_TASKS_STOP_Trigger;
    comp_->ENABLE = (COMP_ENABLE_ENABLE_Disabled << COMP_ENABLE_ENABLE_Pos);
    releaseComparator(ComparatorOwner::kComp);
    return false;
  }

  active_ = true;
  return true;
}

bool Comp::setThresholdWindowPermille(uint16_t lowPermille,
                                      uint16_t highPermille) {
  lowPermille = clampPermille(lowPermille);
  highPermille = clampPermille(highPermille);
  if (lowPermille > highPermille) {
    return false;
  }
  comp_->TH = compThresholdRegValue(lowPermille, highPermille);
  return true;
}

void Comp::setCurrentSource(CompCurrentSource currentSource) {
  comp_->ISOURCE = ((static_cast<uint32_t>(currentSource)
                     << COMP_ISOURCE_ISOURCE_Pos) &
                    COMP_ISOURCE_ISOURCE_Msk);
}

bool Comp::sample(uint32_t spinLimit) const {
  (void)spinLimit;
  if (!active_) {
    return false;
  }
  comp_->TASKS_SAMPLE = COMP_TASKS_SAMPLE_TASKS_SAMPLE_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  return true;
}

bool Comp::resultAbove() const {
  return (comp_->RESULT & COMP_RESULT_RESULT_Msk) != 0U;
}

bool Comp::pollReady(bool clearEventFlag) {
  const bool fired = (comp_->EVENTS_READY != 0U);
  if (fired && clearEventFlag) {
    comp_->EVENTS_READY = 0U;
  }
  return fired;
}

bool Comp::pollUp(bool clearEventFlag) {
  const bool fired = (comp_->EVENTS_UP != 0U);
  if (fired && clearEventFlag) {
    comp_->EVENTS_UP = 0U;
  }
  return fired;
}

bool Comp::pollDown(bool clearEventFlag) {
  const bool fired = (comp_->EVENTS_DOWN != 0U);
  if (fired && clearEventFlag) {
    comp_->EVENTS_DOWN = 0U;
  }
  return fired;
}

bool Comp::pollCross(bool clearEventFlag) {
  const bool fired = (comp_->EVENTS_CROSS != 0U);
  if (fired && clearEventFlag) {
    comp_->EVENTS_CROSS = 0U;
  }
  return fired;
}

void Comp::clearEvents() {
  comp_->EVENTS_READY = 0U;
  comp_->EVENTS_UP = 0U;
  comp_->EVENTS_DOWN = 0U;
  comp_->EVENTS_CROSS = 0U;
}

void Comp::end() {
  comp_->SHORTS = 0U;
  comp_->INTENCLR = 0xFFFFFFFFUL;
  clearEvents();
  comp_->TASKS_STOP = COMP_TASKS_STOP_TASKS_STOP_Trigger;
  comp_->ENABLE = ((COMP_ENABLE_ENABLE_Disabled << COMP_ENABLE_ENABLE_Pos) &
                   COMP_ENABLE_ENABLE_Msk);
  active_ = false;
  releaseComparator(ComparatorOwner::kComp);
}

Lpcomp::Lpcomp(uint32_t base)
    : lpcomp_(reinterpret_cast<NRF_LPCOMP_Type*>(static_cast<uintptr_t>(base))),
      active_(false) {}

bool Lpcomp::begin(const Pin& inputPin, LpcompReference reference, bool hysteresis,
                   LpcompDetect detect, const Pin& externalReferencePin,
                   uint32_t spinLimit) {
  if (active_) {
    end();
  }

  if (!pinSupportsAnalogInput(inputPin)) {
    return false;
  }
  if (reference == LpcompReference::kExternalAref &&
      !pinSupportsAnalogInput(externalReferencePin)) {
    return false;
  }
  if (!claimComparator(ComparatorOwner::kLpcomp)) {
    return false;
  }
  if (!configureAnalogPeripheralPin(inputPin) ||
      (reference == LpcompReference::kExternalAref &&
       !configureAnalogPeripheralPin(externalReferencePin))) {
    releaseComparator(ComparatorOwner::kLpcomp);
    return false;
  }

  lpcomp_->TASKS_STOP = LPCOMP_TASKS_STOP_TASKS_STOP_Trigger;
  lpcomp_->ENABLE = ((LPCOMP_ENABLE_ENABLE_Disabled << LPCOMP_ENABLE_ENABLE_Pos) &
                     LPCOMP_ENABLE_ENABLE_Msk);
  lpcomp_->INTENCLR = 0xFFFFFFFFUL;
  lpcomp_->SHORTS = 0U;
  clearEvents();

  lpcomp_->PSEL = makeLpcompPinSelect(inputPin);
  lpcomp_->REFSEL = ((static_cast<uint32_t>(reference) << LPCOMP_REFSEL_REFSEL_Pos) &
                     LPCOMP_REFSEL_REFSEL_Msk);
  lpcomp_->EXTREFSEL = (reference == LpcompReference::kExternalAref)
                           ? makeLpcompPinSelect(externalReferencePin)
                           : 0U;
  configureAnalogDetect(detect);
  lpcomp_->HYST = (((hysteresis ? LPCOMP_HYST_HYST_Enabled
                                : LPCOMP_HYST_HYST_Disabled)
                    << LPCOMP_HYST_HYST_Pos) &
                   LPCOMP_HYST_HYST_Msk);

  lpcomp_->ENABLE = ((LPCOMP_ENABLE_ENABLE_Enabled << LPCOMP_ENABLE_ENABLE_Pos) &
                     LPCOMP_ENABLE_ENABLE_Msk);
  clearEvents();
  lpcomp_->TASKS_START = LPCOMP_TASKS_START_TASKS_START_Trigger;
  if (!waitForCompReady(&lpcomp_->EVENTS_READY, spinLimit)) {
    lpcomp_->TASKS_STOP = LPCOMP_TASKS_STOP_TASKS_STOP_Trigger;
    lpcomp_->ENABLE =
        ((LPCOMP_ENABLE_ENABLE_Disabled << LPCOMP_ENABLE_ENABLE_Pos) &
         LPCOMP_ENABLE_ENABLE_Msk);
    releaseComparator(ComparatorOwner::kLpcomp);
    return false;
  }

  active_ = true;
  return true;
}

bool Lpcomp::beginThreshold(const Pin& inputPin, uint16_t thresholdPermille,
                            bool hysteresis, LpcompDetect detect,
                            const Pin& externalReferencePin,
                            uint32_t spinLimit) {
  thresholdPermille = clampPermille(thresholdPermille);
  if (isConnected(externalReferencePin)) {
    if (thresholdPermille != 1000U) {
      return false;
    }
    return begin(inputPin, LpcompReference::kExternalAref, hysteresis, detect,
                 externalReferencePin, spinLimit);
  }
  return begin(inputPin, nearestLpcompReference(thresholdPermille), hysteresis,
               detect, kPinDisconnected, spinLimit);
}

void Lpcomp::configureAnalogDetect(LpcompDetect detect) {
  lpcomp_->ANADETECT =
      ((static_cast<uint32_t>(detect) << LPCOMP_ANADETECT_ANADETECT_Pos) &
       LPCOMP_ANADETECT_ANADETECT_Msk);
}

bool Lpcomp::sample(uint32_t spinLimit) const {
  (void)spinLimit;
  if (!active_) {
    return false;
  }
  lpcomp_->TASKS_SAMPLE = LPCOMP_TASKS_SAMPLE_TASKS_SAMPLE_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  return true;
}

bool Lpcomp::resultAbove() const {
  return (lpcomp_->RESULT & LPCOMP_RESULT_RESULT_Msk) != 0U;
}

bool Lpcomp::pollReady(bool clearEventFlag) {
  const bool fired = (lpcomp_->EVENTS_READY != 0U);
  if (fired && clearEventFlag) {
    lpcomp_->EVENTS_READY = 0U;
  }
  return fired;
}

bool Lpcomp::pollUp(bool clearEventFlag) {
  const bool fired = (lpcomp_->EVENTS_UP != 0U);
  if (fired && clearEventFlag) {
    lpcomp_->EVENTS_UP = 0U;
  }
  return fired;
}

bool Lpcomp::pollDown(bool clearEventFlag) {
  const bool fired = (lpcomp_->EVENTS_DOWN != 0U);
  if (fired && clearEventFlag) {
    lpcomp_->EVENTS_DOWN = 0U;
  }
  return fired;
}

bool Lpcomp::pollCross(bool clearEventFlag) {
  const bool fired = (lpcomp_->EVENTS_CROSS != 0U);
  if (fired && clearEventFlag) {
    lpcomp_->EVENTS_CROSS = 0U;
  }
  return fired;
}

void Lpcomp::clearEvents() {
  lpcomp_->EVENTS_READY = 0U;
  lpcomp_->EVENTS_UP = 0U;
  lpcomp_->EVENTS_DOWN = 0U;
  lpcomp_->EVENTS_CROSS = 0U;
}

void Lpcomp::end() {
  lpcomp_->SHORTS = 0U;
  lpcomp_->INTENCLR = 0xFFFFFFFFUL;
  clearEvents();
  lpcomp_->TASKS_STOP = LPCOMP_TASKS_STOP_TASKS_STOP_Trigger;
  lpcomp_->ENABLE =
      ((LPCOMP_ENABLE_ENABLE_Disabled << LPCOMP_ENABLE_ENABLE_Pos) &
       LPCOMP_ENABLE_ENABLE_Msk);
  active_ = false;
  releaseComparator(ComparatorOwner::kLpcomp);
}

Qdec::Qdec(uint32_t base)
    : qdec_(reinterpret_cast<NRF_QDEC_Type*>(static_cast<uintptr_t>(base))),
      configured_(false) {}

bool Qdec::begin(const Pin& pinA, const Pin& pinB, QdecSamplePeriod samplePeriod,
                 QdecReportPeriod reportPeriod, bool debounce,
                 QdecInputPull inputPull, const Pin& ledPin,
                 QdecLedPolarity ledPolarity, uint16_t ledPreUs) {
  if (configured_) {
    end();
  }

  if (!isConnected(pinA) || !isConnected(pinB)) {
    return false;
  }
  if ((pinA.port == pinB.port) && (pinA.pin == pinB.pin)) {
    return false;
  }

  const GpioPull pull = gpioPullFromQdecInputPull(inputPull);
  if (!Gpio::configure(pinA, GpioDirection::kInput, pull) ||
      !Gpio::configure(pinB, GpioDirection::kInput, pull)) {
    return false;
  }
  if (isConnected(ledPin) &&
      !Gpio::configure(ledPin, GpioDirection::kOutput, GpioPull::kDisabled)) {
    return false;
  }

  if (ledPreUs < QDEC_LEDPRE_LEDPRE_Min) {
    ledPreUs = QDEC_LEDPRE_LEDPRE_Min;
  }
  if (ledPreUs > QDEC_LEDPRE_LEDPRE_Max) {
    ledPreUs = QDEC_LEDPRE_LEDPRE_Max;
  }

  qdec_->TASKS_STOP = QDEC_TASKS_STOP_TASKS_STOP_Trigger;
  qdec_->ENABLE = QDEC_ENABLE_ENABLE_Disabled;
  qdec_->INTENCLR = 0xFFFFFFFFUL;
  qdec_->SHORTS = 0U;
  qdec_->EVENTS_SAMPLERDY = 0U;
  qdec_->EVENTS_REPORTRDY = 0U;
  qdec_->EVENTS_ACCOF = 0U;
  qdec_->EVENTS_DBLRDY = 0U;
  qdec_->EVENTS_STOPPED = 0U;

  qdec_->PSEL.A = makeQdecConnectedPinSelect(pinA);
  qdec_->PSEL.B = makeQdecConnectedPinSelect(pinB);
  qdec_->PSEL.LED =
      isConnected(ledPin) ? makeQdecConnectedPinSelect(ledPin) : PSEL_DISCONNECTED;
  qdec_->LEDPOL = ((static_cast<uint32_t>(ledPolarity) << QDEC_LEDPOL_LEDPOL_Pos) &
                   QDEC_LEDPOL_LEDPOL_Msk);
  qdec_->SAMPLEPER =
      ((static_cast<uint32_t>(samplePeriod) << QDEC_SAMPLEPER_SAMPLEPER_Pos) &
       QDEC_SAMPLEPER_SAMPLEPER_Msk);
  qdec_->REPORTPER =
      ((static_cast<uint32_t>(reportPeriod) << QDEC_REPORTPER_REPORTPER_Pos) &
       QDEC_REPORTPER_REPORTPER_Msk);
  qdec_->DBFEN = (((debounce ? QDEC_DBFEN_DBFEN_Enabled
                             : QDEC_DBFEN_DBFEN_Disabled)
                   << QDEC_DBFEN_DBFEN_Pos) &
                  QDEC_DBFEN_DBFEN_Msk);
  qdec_->LEDPRE = ((static_cast<uint32_t>(ledPreUs) << QDEC_LEDPRE_LEDPRE_Pos) &
                   QDEC_LEDPRE_LEDPRE_Msk);

  qdec_->ENABLE = ((QDEC_ENABLE_ENABLE_Enabled << QDEC_ENABLE_ENABLE_Pos) &
                   QDEC_ENABLE_ENABLE_Msk);
  configured_ = true;
  return true;
}

void Qdec::end() {
  qdec_->TASKS_STOP = QDEC_TASKS_STOP_TASKS_STOP_Trigger;
  qdec_->ENABLE = QDEC_ENABLE_ENABLE_Disabled;
  qdec_->PSEL.A = PSEL_DISCONNECTED;
  qdec_->PSEL.B = PSEL_DISCONNECTED;
  qdec_->PSEL.LED = PSEL_DISCONNECTED;
  qdec_->EVENTS_SAMPLERDY = 0U;
  qdec_->EVENTS_REPORTRDY = 0U;
  qdec_->EVENTS_ACCOF = 0U;
  qdec_->EVENTS_DBLRDY = 0U;
  qdec_->EVENTS_STOPPED = 0U;
  configured_ = false;
}

void Qdec::start() {
  if (configured_) {
    qdec_->TASKS_START = QDEC_TASKS_START_TASKS_START_Trigger;
  }
}

void Qdec::stop() {
  if (configured_) {
    qdec_->TASKS_STOP = QDEC_TASKS_STOP_TASKS_STOP_Trigger;
  }
}

int32_t Qdec::sampleValue() const { return qdec_->SAMPLE; }

int32_t Qdec::accumulator() const { return qdec_->ACC; }

int32_t Qdec::readAndClearAccumulator() {
  if (!configured_) {
    return 0;
  }
  qdec_->TASKS_RDCLRACC = QDEC_TASKS_RDCLRACC_TASKS_RDCLRACC_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  return qdec_->ACCREAD;
}

uint32_t Qdec::doubleTransitions() const { return qdec_->ACCDBL; }

uint32_t Qdec::readAndClearDoubleTransitions() {
  if (!configured_) {
    return 0U;
  }
  qdec_->TASKS_RDCLRDBL = QDEC_TASKS_RDCLRDBL_TASKS_RDCLRDBL_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  return qdec_->ACCDBLREAD;
}

bool Qdec::pollSampleReady(bool clearEventFlag) {
  const bool fired = (qdec_->EVENTS_SAMPLERDY != 0U);
  if (fired && clearEventFlag) {
    qdec_->EVENTS_SAMPLERDY = 0U;
  }
  return fired;
}

bool Qdec::pollReportReady(bool clearEventFlag) {
  const bool fired = (qdec_->EVENTS_REPORTRDY != 0U);
  if (fired && clearEventFlag) {
    qdec_->EVENTS_REPORTRDY = 0U;
  }
  return fired;
}

bool Qdec::pollOverflow(bool clearEventFlag) {
  const bool fired = (qdec_->EVENTS_ACCOF != 0U);
  if (fired && clearEventFlag) {
    qdec_->EVENTS_ACCOF = 0U;
  }
  return fired;
}

bool Qdec::pollDoubleReady(bool clearEventFlag) {
  const bool fired = (qdec_->EVENTS_DBLRDY != 0U);
  if (fired && clearEventFlag) {
    qdec_->EVENTS_DBLRDY = 0U;
  }
  return fired;
}

bool Qdec::pollStopped(bool clearEventFlag) {
  const bool fired = (qdec_->EVENTS_STOPPED != 0U);
  if (fired && clearEventFlag) {
    qdec_->EVENTS_STOPPED = 0U;
  }
  return fired;
}

Saadc::Saadc(uint32_t base)
    : base_(base),
      resolution_(AdcResolution::k12bit),
      gain_(AdcGain::k2over8),
      oversample_(AdcOversample::kBypass),
      differential_(false),
      configured_(false) {}

bool Saadc::begin(AdcResolution resolution, uint32_t spinLimit) {
  return begin(resolution, AdcOversample::kBypass, spinLimit);
}

bool Saadc::begin(AdcResolution resolution, AdcOversample oversample,
                  uint32_t spinLimit) {
  resolution_ = resolution;
  oversample_ = oversample;
  differential_ = false;
  configured_ = false;

  reg32(base_ + saadc::ENABLE) = saadc::ENABLE_DISABLED;

  reg32(base_ + saadc::RESOLUTION) = static_cast<uint32_t>(resolution_);
  reg32(base_ + saadc::OVERSAMPLE) = static_cast<uint32_t>(oversample_);
  reg32(base_ + saadc::SAMPLERATE) =
      (saadc::SAMPLERATE_MODE_TASK << saadc::SAMPLERATE_MODE_Pos);
  reg32(base_ + saadc::NOISESHAPE) = saadc::NOISESHAPE_DISABLED;

  reg32(base_ + saadc::ENABLE) = saadc::ENABLE_ENABLED;

  return calibrate(spinLimit);
}

bool Saadc::calibrate(uint32_t spinLimit) {
  clearEvent(base_, saadc::EVENTS_STARTED);
  clearEvent(base_, saadc::EVENTS_END);
  clearEvent(base_, saadc::EVENTS_STOPPED);

  clearEvent(base_, saadc::EVENTS_CALIBRATEDONE);
  reg32(base_ + saadc::TASKS_CALIBRATEOFFSET) = 1;

  return waitForEvent(base_, saadc::EVENTS_CALIBRATEDONE, spinLimit);
}

void Saadc::end() {
  reg32(base_ + saadc::ENABLE) = saadc::ENABLE_DISABLED;
  differential_ = false;
  configured_ = false;
}

bool Saadc::configureSingleEnded(uint8_t channel, const Pin& pin, AdcGain gain,
                                 uint16_t tacq, uint8_t tconv, bool burst) {
  if (channel > 7U || !isConnected(pin) || saadcInputForPin(pin) < 0) {
    return false;
  }

  if (tacq < 1U) {
    tacq = 1U;
  }
  if (tacq > 319U) {
    tacq = 319U;
  }
  if (tconv < 1U) {
    tconv = 1U;
  }
  if (tconv > 7U) {
    tconv = 7U;
  }

  // Keep this HAL simple and deterministic: one active channel at a time.
  for (uint8_t i = 0; i < 8U; ++i) {
    reg32(base_ + saadc::CH_PSELP +
          (static_cast<uint32_t>(i) * saadc::CH_STRIDE)) = 0;
    reg32(base_ + saadc::CH_PSELN +
          (static_cast<uint32_t>(i) * saadc::CH_STRIDE)) = 0;
  }

  reg32(base_ + saadc::CH_PSELP +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) =
      saadcPselValue(pin);
  reg32(base_ + saadc::CH_PSELN +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) =
      (saadc::CH_PSEL_CONNECT_NC << saadc::CH_PSEL_CONNECT_Pos);

  uint32_t config = 0;
  config |= (static_cast<uint32_t>(gain) << saadc::CH_CONFIG_GAIN_Pos);
  config |= ((burst ? saadc::CH_CONFIG_BURST_ENABLED
                    : saadc::CH_CONFIG_BURST_DISABLED)
             << saadc::CH_CONFIG_BURST_Pos);
  config |= (saadc::REFSEL_INTERNAL << saadc::CH_CONFIG_REFSEL_Pos);
  config |= (saadc::MODE_SINGLE_ENDED << saadc::CH_CONFIG_MODE_Pos);
  config |= (static_cast<uint32_t>(tacq) << saadc::CH_CONFIG_TACQ_Pos);
  config |= (static_cast<uint32_t>(tconv) << saadc::CH_CONFIG_TCONV_Pos);

  reg32(base_ + saadc::CH_CONFIG +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) = config;

  gain_ = gain;
  differential_ = false;
  configured_ = true;
  return true;
}

bool Saadc::configureDifferential(uint8_t channel, const Pin& positivePin,
                                  const Pin& negativePin, AdcGain gain,
                                  uint16_t tacq, uint8_t tconv, bool burst) {
  if (channel > 7U || !isConnected(positivePin) ||
      !isConnected(negativePin) || saadcInputForPin(positivePin) < 0 ||
      saadcInputForPin(negativePin) < 0 ||
      (positivePin.port == negativePin.port &&
       positivePin.pin == negativePin.pin)) {
    return false;
  }

  if (tacq < 1U) {
    tacq = 1U;
  }
  if (tacq > 319U) {
    tacq = 319U;
  }
  if (tconv < 1U) {
    tconv = 1U;
  }
  if (tconv > 7U) {
    tconv = 7U;
  }

  for (uint8_t i = 0; i < 8U; ++i) {
    reg32(base_ + saadc::CH_PSELP +
          (static_cast<uint32_t>(i) * saadc::CH_STRIDE)) = 0;
    reg32(base_ + saadc::CH_PSELN +
          (static_cast<uint32_t>(i) * saadc::CH_STRIDE)) = 0;
  }

  reg32(base_ + saadc::CH_PSELP +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) =
      saadcPselValue(positivePin);
  reg32(base_ + saadc::CH_PSELN +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) =
      saadcPselValue(negativePin);

  uint32_t config = 0;
  config |= (static_cast<uint32_t>(gain) << saadc::CH_CONFIG_GAIN_Pos);
  config |= ((burst ? saadc::CH_CONFIG_BURST_ENABLED
                    : saadc::CH_CONFIG_BURST_DISABLED)
             << saadc::CH_CONFIG_BURST_Pos);
  config |= (saadc::REFSEL_INTERNAL << saadc::CH_CONFIG_REFSEL_Pos);
  config |= (saadc::MODE_DIFFERENTIAL << saadc::CH_CONFIG_MODE_Pos);
  config |= (static_cast<uint32_t>(tacq) << saadc::CH_CONFIG_TACQ_Pos);
  config |= (static_cast<uint32_t>(tconv) << saadc::CH_CONFIG_TCONV_Pos);

  reg32(base_ + saadc::CH_CONFIG +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) = config;

  gain_ = gain;
  differential_ = true;
  configured_ = true;
  return true;
}

bool Saadc::sampleRaw(int16_t* outRaw, uint32_t spinLimit) const {
  if (!configured_ || outRaw == nullptr) {
    return false;
  }

  // EasyDMA writes this value asynchronously, keep it volatile.
  volatile int16_t sample = 0;

  clearEvent(base_, saadc::EVENTS_STARTED);
  clearEvent(base_, saadc::EVENTS_END);
  clearEvent(base_, saadc::EVENTS_STOPPED);

  reg32(base_ + saadc::RESULT_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&sample));
  // On nRF54 SAADC this counter is in bytes.
  reg32(base_ + saadc::RESULT_MAXCNT) = sizeof(sample);

  reg32(base_ + saadc::TASKS_START) = 1;
  if (!waitForEvent(base_, saadc::EVENTS_STARTED, spinLimit)) {
    return false;
  }

  reg32(base_ + saadc::TASKS_SAMPLE) = 1;
  if (!waitForEvent(base_, saadc::EVENTS_END, spinLimit)) {
    return false;
  }

  reg32(base_ + saadc::TASKS_STOP) = 1;
  if (!waitForEvent(base_, saadc::EVENTS_STOPPED, spinLimit)) {
    return false;
  }

  if (reg32(base_ + saadc::RESULT_AMOUNT) < sizeof(sample)) {
    return false;
  }

  *outRaw = sample;
  return true;
}

bool Saadc::sampleMilliVolts(int32_t* outMilliVolts, uint32_t spinLimit) const {
  if (outMilliVolts == nullptr) {
    return false;
  }

  int16_t raw = 0;
  if (!sampleRaw(&raw, spinLimit)) {
    return false;
  }

  if (!differential_ && raw < 0) {
    raw = 0;
  }

  *outMilliVolts = adcRawToMilliVolts(raw, resolution_, gain_, differential_);
  return true;
}

bool Saadc::sampleMilliVoltsSigned(int32_t* outMilliVolts,
                                   uint32_t spinLimit) const {
  if (outMilliVolts == nullptr) {
    return false;
  }

  int16_t raw = 0;
  if (!sampleRaw(&raw, spinLimit)) {
    return false;
  }

  *outMilliVolts = adcRawToMilliVolts(raw, resolution_, gain_, differential_);
  return true;
}

bool BoardControl::setAntennaPath(BoardAntennaPath path) {
  xiao_nrf54l15_antenna_t selection = XIAO_NRF54L15_ANTENNA_CERAMIC;
  switch (path) {
    case BoardAntennaPath::kExternal:
      selection = XIAO_NRF54L15_ANTENNA_EXTERNAL;
      break;
    case BoardAntennaPath::kControlHighImpedance:
      selection = XIAO_NRF54L15_ANTENNA_CONTROL_HIZ;
      break;
    case BoardAntennaPath::kCeramic:
    default:
      selection = XIAO_NRF54L15_ANTENNA_CERAMIC;
      break;
  }
  xiaoNrf54l15SetAntenna(selection);
  g_boardAntennaPath = path;
  return true;
}

BoardAntennaPath BoardControl::antennaPath() {
  switch (xiaoNrf54l15GetAntenna()) {
    case XIAO_NRF54L15_ANTENNA_EXTERNAL:
      g_boardAntennaPath = BoardAntennaPath::kExternal;
      break;
    case XIAO_NRF54L15_ANTENNA_CONTROL_HIZ:
      g_boardAntennaPath = BoardAntennaPath::kControlHighImpedance;
      break;
    case XIAO_NRF54L15_ANTENNA_CERAMIC:
    default:
      g_boardAntennaPath = BoardAntennaPath::kCeramic;
      break;
  }
  return g_boardAntennaPath;
}

bool BoardControl::setImuMicEnabled(bool enable) {
  return arduinoXiaoNrf54l15SetImuMicEnable(enable ? 1U : 0U) != 0U;
}

bool BoardControl::imuMicEnabled() {
  return arduinoXiaoNrf54l15GetImuMicEnable() != 0U;
}

bool BoardControl::setRfSwitchPowerEnabled(bool enable) {
  return arduinoXiaoNrf54l15SetRfSwitchPower(enable ? 1U : 0U) != 0U;
}

bool BoardControl::rfSwitchPowerEnabled() {
  return arduinoXiaoNrf54l15GetRfSwitchPower() != 0U;
}

bool BoardControl::enableRfPath(BoardAntennaPath path) {
  return setRfSwitchPowerEnabled(true) && setAntennaPath(path);
}

bool BoardControl::collapseRfPathIdle(BoardAntennaPath idlePath,
                                      bool disablePower) {
  const bool pathOk = setAntennaPath(idlePath);
  if (!disablePower) {
    return pathOk;
  }
  return setRfSwitchPowerEnabled(false) && pathOk;
}

void BoardControl::enterLowestPowerState() {
  xiaoNrf54l15EnterLowestPowerBoardState();
}

bool BoardControl::setBatterySenseEnabled(bool enable) {
  if (!Gpio::configure(kPinVbatEnable, GpioDirection::kOutput,
                       GpioPull::kDisabled)) {
    return false;
  }
  return Gpio::write(kPinVbatEnable, enable);
}

bool BoardControl::sampleBatteryMilliVolts(int32_t* outMilliVolts,
                                           uint32_t settleDelayUs,
                                           uint32_t spinLimit) {
  if (outMilliVolts == nullptr) {
    return false;
  }

  Saadc adc(nrf54l15::SAADC_BASE);
  if (!adc.begin(AdcResolution::k12bit, spinLimit)) {
    return false;
  }

  bool ok = setBatterySenseEnabled(true);
  if (ok && settleDelayUs > 0U) {
    delayMicroseconds(settleDelayUs);
  }

  int32_t halfMilliVolts = 0;
  if (ok) {
    ok = adc.configureSingleEnded(0U, kPinVbatSense, AdcGain::k2over8) &&
         adc.sampleMilliVolts(&halfMilliVolts, spinLimit);
  }

  const bool disableOk = setBatterySenseEnabled(false);
  adc.end();

  if (!ok || !disableOk) {
    return false;
  }

  *outMilliVolts = halfMilliVolts * 2;
  return true;
}

bool BoardControl::sampleBatteryPercent(uint8_t* outPercent,
                                        int32_t emptyMilliVolts,
                                        int32_t fullMilliVolts,
                                        uint32_t settleDelayUs,
                                        uint32_t spinLimit) {
  if (outPercent == nullptr || fullMilliVolts <= emptyMilliVolts) {
    return false;
  }

  int32_t batteryMv = 0;
  if (!sampleBatteryMilliVolts(&batteryMv, settleDelayUs, spinLimit)) {
    return false;
  }

  if (batteryMv <= emptyMilliVolts) {
    *outPercent = 0U;
    return true;
  }
  if (batteryMv >= fullMilliVolts) {
    *outPercent = 100U;
    return true;
  }

  const int32_t span = fullMilliVolts - emptyMilliVolts;
  const int32_t scaled = (batteryMv - emptyMilliVolts) * 100 + (span / 2);
  const int32_t percent = scaled / span;
  *outPercent = static_cast<uint8_t>(percent);
  return true;
}

PowerManager::PowerManager(uint32_t powerBase, uint32_t resetBase,
                           uint32_t regulatorsBase)
    : power_(reinterpret_cast<NRF_POWER_Type*>(static_cast<uintptr_t>(powerBase))),
      reset_(reinterpret_cast<NRF_RESET_Type*>(static_cast<uintptr_t>(resetBase))),
      regulators_(reinterpret_cast<NRF_REGULATORS_Type*>(
          static_cast<uintptr_t>(regulatorsBase))) {}

void PowerManager::setLatencyMode(PowerLatencyMode mode) {
  if (mode == PowerLatencyMode::kConstantLatency) {
    power_->TASKS_CONSTLAT = POWER_TASKS_CONSTLAT_TASKS_CONSTLAT_Trigger;
  } else {
    power_->TASKS_LOWPWR = POWER_TASKS_LOWPWR_TASKS_LOWPWR_Trigger;
  }
}

bool PowerManager::isConstantLatency() const {
  return (power_->CONSTLATSTAT & 0x1UL) != 0U;
}

bool PowerManager::setRetention(uint8_t index, uint8_t value) {
  if (index >= 2U) {
    return false;
  }
  power_->GPREGRET[index] = static_cast<uint32_t>(value);
  return true;
}

bool PowerManager::getRetention(uint8_t index, uint8_t* value) const {
  if (index >= 2U || value == nullptr) {
    return false;
  }
  *value = static_cast<uint8_t>(power_->GPREGRET[index] & POWER_GPREGRET_GPREGRET_Msk);
  return true;
}

uint32_t PowerManager::resetReason() const { return reset_->RESETREAS; }

void PowerManager::clearResetReason(uint32_t mask) { reset_->RESETREAS = mask; }

bool PowerManager::enableMainDcdc(bool enable) {
  regulators_->VREGMAIN.DCDCEN =
      enable ? REGULATORS_VREGMAIN_DCDCEN_VAL_Enabled
             : REGULATORS_VREGMAIN_DCDCEN_VAL_Disabled;
  const bool isEnabled = (regulators_->VREGMAIN.DCDCEN &
                          REGULATORS_VREGMAIN_DCDCEN_VAL_Msk) != 0U;
  return (isEnabled == enable);
}

bool PowerManager::configurePowerFailComparator(PowerFailThreshold threshold,
                                                bool enableWarningEvent) {
  uint32_t config = regulators_->POFCON;
  config &= ~(REGULATORS_POFCON_POF_Msk | REGULATORS_POFCON_THRESHOLD_Msk |
              REGULATORS_POFCON_EVENTDISABLE_Msk);
  config |= (REGULATORS_POFCON_POF_Enabled << REGULATORS_POFCON_POF_Pos) &
            REGULATORS_POFCON_POF_Msk;
  config |= ((static_cast<uint32_t>(threshold)
              << REGULATORS_POFCON_THRESHOLD_Pos) &
             REGULATORS_POFCON_THRESHOLD_Msk);
  config |= (((enableWarningEvent ? REGULATORS_POFCON_EVENTDISABLE_Enabled
                                  : REGULATORS_POFCON_EVENTDISABLE_Disabled)
              << REGULATORS_POFCON_EVENTDISABLE_Pos) &
             REGULATORS_POFCON_EVENTDISABLE_Msk);
  regulators_->POFCON = config;
  power_->EVENTS_POFWARN = POWER_EVENTS_POFWARN_EVENTS_POFWARN_NotGenerated;
  return powerFailComparatorEnabled() &&
         (powerFailThreshold() == threshold) &&
         (powerFailWarningEventEnabled() == enableWarningEvent);
}

void PowerManager::disablePowerFailComparator() {
  regulators_->POFCON &= ~REGULATORS_POFCON_POF_Msk;
  clearPowerFailWarning();
}

bool PowerManager::powerFailComparatorEnabled() const {
  return (regulators_->POFCON & REGULATORS_POFCON_POF_Msk) != 0U;
}

PowerFailThreshold PowerManager::powerFailThreshold() const {
  return static_cast<PowerFailThreshold>(
      (regulators_->POFCON & REGULATORS_POFCON_THRESHOLD_Msk) >>
      REGULATORS_POFCON_THRESHOLD_Pos);
}

bool PowerManager::powerBelowPowerFailThreshold() const {
  return (regulators_->POFSTAT & REGULATORS_POFSTAT_COMPARATOR_Msk) != 0U;
}

bool PowerManager::powerFailWarningEventEnabled() const {
  return (regulators_->POFCON & REGULATORS_POFCON_EVENTDISABLE_Msk) == 0U;
}

bool PowerManager::pollPowerFailWarning(bool clearEvent) {
  const bool fired =
      (power_->EVENTS_POFWARN & POWER_EVENTS_POFWARN_EVENTS_POFWARN_Msk) != 0U;
  if (fired && clearEvent) {
    clearPowerFailWarning();
  }
  return fired;
}

void PowerManager::clearPowerFailWarning() {
  power_->EVENTS_POFWARN = POWER_EVENTS_POFWARN_EVENTS_POFWARN_NotGenerated;
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
  if (clockSource == GrtcClockSource::kLfxo) {
    ensureLfxoRunning();
  }

  uint32_t clkcfg = grtc_->CLKCFG;
  clkcfg &= ~GRTC_CLKCFG_CLKSEL_Msk;
  clkcfg |= (static_cast<uint32_t>(clockSource) << GRTC_CLKCFG_CLKSEL_Pos) &
            GRTC_CLKCFG_CLKSEL_Msk;
  grtc_->CLKCFG = clkcfg;

  uint32_t mode = grtc_->MODE;
  mode &= ~(GRTC_MODE_AUTOEN_Msk | GRTC_MODE_SYSCOUNTEREN_Msk);
  mode |= (GRTC_MODE_AUTOEN_Default << GRTC_MODE_AUTOEN_Pos);
  mode |= (GRTC_MODE_SYSCOUNTEREN_Enabled << GRTC_MODE_SYSCOUNTEREN_Pos);
  grtc_->MODE = mode;

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
  for (uint8_t i = 0; i < 32U; ++i) {
    const uint32_t hi0 = NRF54L15_GRTC_SYSCOUNTER(grtc_).SYSCOUNTERH;
    const uint32_t lo = NRF54L15_GRTC_SYSCOUNTER(grtc_).SYSCOUNTERL;
    const uint32_t hi1 = NRF54L15_GRTC_SYSCOUNTER(grtc_).SYSCOUNTERH;

    if ((hi0 & GRTC_SYSCOUNTER_SYSCOUNTERH_BUSY_Msk) != 0U ||
        (hi1 & GRTC_SYSCOUNTER_SYSCOUNTERH_BUSY_Msk) != 0U ||
        (hi1 & GRTC_SYSCOUNTER_SYSCOUNTERH_OVERFLOW_Msk) != 0U) {
      continue;
    }

    const uint32_t high0 = (hi0 & GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk);
    const uint32_t high1 = (hi1 & GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk);
    if (high0 != high1) {
      continue;
    }

    return (static_cast<uint64_t>(high1) << 32U) | static_cast<uint64_t>(lo);
  }

  // Fall back to best-effort read if hardware did not provide a clean sample.
  const uint32_t hi = NRF54L15_GRTC_SYSCOUNTER(grtc_).SYSCOUNTERH &
                      GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk;
  const uint32_t lo = NRF54L15_GRTC_SYSCOUNTER(grtc_).SYSCOUNTERL;
  return (static_cast<uint64_t>(hi) << 32U) | static_cast<uint64_t>(lo);
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

TempSensor::TempSensor(uint32_t base)
    : temp_(reinterpret_cast<NRF_TEMP_Type*>(static_cast<uintptr_t>(base))) {}

bool TempSensor::sampleQuarterDegreesC(int32_t* outQuarterDegreesC,
                                       uint32_t spinLimit) const {
  if (outQuarterDegreesC == nullptr) {
    return false;
  }

  temp_->EVENTS_DATARDY = 0U;
  temp_->TASKS_START = TEMP_TASKS_START_TASKS_START_Trigger;
  const bool ready = waitForNonZero(&temp_->EVENTS_DATARDY, spinLimit);
  temp_->TASKS_STOP = TEMP_TASKS_STOP_TASKS_STOP_Trigger;
  if (!ready) {
    return false;
  }

  *outQuarterDegreesC = temp_->TEMP;
  temp_->EVENTS_DATARDY = 0U;
  return true;
}

bool TempSensor::sampleMilliDegreesC(int32_t* outMilliDegreesC,
                                     uint32_t spinLimit) const {
  if (outMilliDegreesC == nullptr) {
    return false;
  }

  int32_t quarter = 0;
  if (!sampleQuarterDegreesC(&quarter, spinLimit)) {
    return false;
  }

  *outMilliDegreesC = quarter * 250;
  return true;
}

Watchdog::Watchdog(uint32_t base)
    : wdt_(reinterpret_cast<NRF_WDT_Type*>(static_cast<uintptr_t>(base))),
      defaultReloadRegister_(0),
      allowStop_(false) {}

bool Watchdog::configure(uint32_t timeoutMs, uint8_t reloadRegister, bool runInSleep,
                         bool runInDebugHalt, bool allowStop) {
  if (reloadRegister > 7U || timeoutMs == 0U) {
    return false;
  }
  if (isRunning()) {
    return false;
  }

  uint64_t cycles64 =
      (static_cast<uint64_t>(timeoutMs) * 32768ULL + 999ULL) / 1000ULL;
  if (cycles64 < WDT_CRV_CRV_Min) {
    cycles64 = WDT_CRV_CRV_Min;
  }
  if (cycles64 > WDT_CRV_CRV_Max) {
    cycles64 = WDT_CRV_CRV_Max;
  }

  uint32_t cfg = 0;
  cfg |= ((runInSleep ? WDT_CONFIG_SLEEP_Run : WDT_CONFIG_SLEEP_Pause)
          << WDT_CONFIG_SLEEP_Pos);
  cfg |= ((runInDebugHalt ? WDT_CONFIG_HALT_Run : WDT_CONFIG_HALT_Pause)
          << WDT_CONFIG_HALT_Pos);
  cfg |= ((allowStop ? WDT_CONFIG_STOPEN_Enable : WDT_CONFIG_STOPEN_Disable)
          << WDT_CONFIG_STOPEN_Pos);

  wdt_->CRV = static_cast<uint32_t>(cycles64);
  wdt_->RREN = (1UL << reloadRegister);
  wdt_->CONFIG = cfg;
  if (allowStop) {
    wdt_->TSEN = WDT_TSEN_TSEN_Enable;
  }

  defaultReloadRegister_ = reloadRegister;
  allowStop_ = allowStop;
  return true;
}

void Watchdog::start() { wdt_->TASKS_START = WDT_TASKS_START_TASKS_START_Trigger; }

bool Watchdog::stop(uint32_t spinLimit) {
  if (!allowStop_) {
    return false;
  }

  wdt_->EVENTS_STOPPED = 0U;
  wdt_->TASKS_STOP = WDT_TASKS_STOP_TASKS_STOP_Trigger;
  return waitForNonZero(&wdt_->EVENTS_STOPPED, spinLimit);
}

bool Watchdog::feed(uint8_t reloadRegister) {
  if (reloadRegister > 7U) {
    reloadRegister = defaultReloadRegister_;
  }
  wdt_->RR[reloadRegister] = WDT_RR_RR_Reload;
  return true;
}

bool Watchdog::isRunning() const {
  return (wdt_->RUNSTATUS & WDT_RUNSTATUS_RUNSTATUSWDT_Msk) != 0U;
}

uint32_t Watchdog::requestStatus() const { return wdt_->REQSTATUS; }

Pdm::Pdm(uint32_t base)
    : pdm_(reinterpret_cast<NRF_PDM_Type*>(static_cast<uintptr_t>(base))),
      configured_(false) {}

bool Pdm::begin(const Pin& clk, const Pin& din, bool mono, uint8_t prescalerDiv,
                uint8_t ratio, PdmEdge edge) {
  if (!isConnected(clk) || !isConnected(din)) {
    return false;
  }
  if (prescalerDiv < PDM_PRESCALER_DIVISOR_Min) {
    prescalerDiv = PDM_PRESCALER_DIVISOR_Min;
  }
  if (prescalerDiv > PDM_PRESCALER_DIVISOR_Max) {
    prescalerDiv = PDM_PRESCALER_DIVISOR_Max;
  }
  if (ratio > PDM_RATIO_RATIO_Max) {
    ratio = PDM_RATIO_RATIO_Ratio64;
  }

  if (!Gpio::configure(clk, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(din, GpioDirection::kInput, GpioPull::kDisabled)) {
    return false;
  }

  pdm_->ENABLE = PDM_ENABLE_ENABLE_Disabled;
  pdm_->TASKS_STOP = PDM_TASKS_STOP_TASKS_STOP_Trigger;
  pdm_->EVENTS_STOPPED = 0U;

  pdm_->PSEL.CLK = make_psel(clk.port, clk.pin);
  pdm_->PSEL.DIN = make_psel(din.port, din.pin);

  uint32_t mode = 0;
  mode |= ((mono ? PDM_MODE_OPERATION_Mono : PDM_MODE_OPERATION_Stereo)
           << PDM_MODE_OPERATION_Pos);
  mode |= ((static_cast<uint32_t>(edge) << PDM_MODE_EDGE_Pos) & PDM_MODE_EDGE_Msk);
  pdm_->MODE = mode;

  pdm_->GAINL = PDM_GAINL_GAINL_DefaultGain;
  pdm_->GAINR = PDM_GAINR_GAINR_DefaultGain;
  pdm_->RATIO = ((static_cast<uint32_t>(ratio) << PDM_RATIO_RATIO_Pos) &
                 PDM_RATIO_RATIO_Msk);
  pdm_->PRESCALER = (static_cast<uint32_t>(prescalerDiv) << PDM_PRESCALER_DIVISOR_Pos);
  pdm_->DMA.TERMINATEONBUSERROR = PDM_DMA_TERMINATEONBUSERROR_ENABLE_Enabled;

  pdm_->ENABLE = PDM_ENABLE_ENABLE_Enabled;
  configured_ = true;
  return true;
}

void Pdm::end() {
  if (!configured_) {
    return;
  }

  pdm_->TASKS_STOP = PDM_TASKS_STOP_TASKS_STOP_Trigger;
  waitForNonZero(&pdm_->EVENTS_STOPPED, 300000UL);

  pdm_->ENABLE = PDM_ENABLE_ENABLE_Disabled;
  pdm_->PSEL.CLK = PSEL_DISCONNECTED;
  pdm_->PSEL.DIN = PSEL_DISCONNECTED;
  configured_ = false;
}

bool Pdm::capture(int16_t* samples, size_t sampleCount, uint32_t spinLimit) {
  if (!configured_ || samples == nullptr || sampleCount == 0U) {
    return false;
  }

  const uintptr_t ptr = reinterpret_cast<uintptr_t>(samples);
  if ((ptr & 0x3U) != 0U) {
    return false;
  }

  const uint32_t bytes = static_cast<uint32_t>(sampleCount * sizeof(int16_t));
  if (bytes > PDM_SAMPLE_MAXCNT_BUFFSIZE_Max) {
    return false;
  }

  pdm_->EVENTS_STARTED = 0U;
  pdm_->EVENTS_END = 0U;
  pdm_->EVENTS_STOPPED = 0U;
  pdm_->EVENTS_DMA.BUSERROR = 0U;

  pdm_->SAMPLE.PTR = static_cast<uint32_t>(ptr);
  pdm_->SAMPLE.MAXCNT = bytes;

  pdm_->TASKS_START = PDM_TASKS_START_TASKS_START_Trigger;
  if (!waitForNonZero(&pdm_->EVENTS_STARTED, spinLimit)) {
    return false;
  }

  bool endSeen = false;
  uint32_t spins = spinLimit;
  while (spins-- > 0U) {
    if (pdm_->EVENTS_END != 0U) {
      endSeen = true;
      break;
    }
    if (pdm_->EVENTS_DMA.BUSERROR != 0U) {
      break;
    }
  }

  pdm_->TASKS_STOP = PDM_TASKS_STOP_TASKS_STOP_Trigger;
  const bool stopped = waitForNonZero(&pdm_->EVENTS_STOPPED, 300000UL);
  const bool busError = (pdm_->EVENTS_DMA.BUSERROR != 0U);
  pdm_->EVENTS_DMA.BUSERROR = 0U;

  return endSeen && stopped && !busError;
}

I2sTx::I2sTx(uint32_t base)
    : i2s_(reinterpret_cast<NRF_I2S_Type*>(static_cast<uintptr_t>(base))),
      config_(),
      buffers_{nullptr, nullptr},
      wordCount_(0U),
      refillCallback_(nullptr),
      refillContext_(nullptr),
      nextBufferIndex_(1U),
      configured_(false),
      running_(false),
      restartPending_(false),
      txPtrUpdCount_(0U),
      stoppedCount_(0U),
      restartCount_(0U),
      manualStopCount_(0U) {}

bool I2sTx::setBuffers(uint32_t* buffer0, uint32_t* buffer1, uint32_t wordCount) {
  if (buffer0 == nullptr || buffer1 == nullptr || wordCount == 0U) {
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(buffer0) & 0x3U) != 0U ||
      (reinterpret_cast<uintptr_t>(buffer1) & 0x3U) != 0U) {
    return false;
  }
  if (wordCount >
      (I2S_RXTXD_MAXCNT_MAXCNT_Msk >> I2S_RXTXD_MAXCNT_MAXCNT_Pos)) {
    return false;
  }

  buffers_[0] = buffer0;
  buffers_[1] = buffer1;
  wordCount_ = wordCount;
  return true;
}

void I2sTx::setRefillCallback(RefillCallback callback, void* context) {
  refillCallback_ = callback;
  refillContext_ = context;
}

bool I2sTx::begin(const I2sTxConfig& config, uint32_t* buffer0, uint32_t* buffer1,
                  uint32_t wordCount) {
  if (i2s_ == nullptr || !setBuffers(buffer0, buffer1, wordCount)) {
    return false;
  }
  if (!isConnected(config.mck) || !isConnected(config.sck) ||
      !isConnected(config.lrck) || !isConnected(config.sdout)) {
    return false;
  }
  if (!Gpio::configure(config.mck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.lrck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sdout, GpioDirection::kOutput, GpioPull::kDisabled)) {
    return false;
  }
  if (isConnected(config.sdin) &&
      !Gpio::configure(config.sdin, GpioDirection::kInput, GpioPull::kDisabled)) {
    return false;
  }

  config_ = config;

  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Disabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;

  i2s_->PSEL.MCK = make_psel(config_.mck.port, config_.mck.pin);
  i2s_->PSEL.SCK = make_psel(config_.sck.port, config_.sck.pin);
  i2s_->PSEL.LRCK = make_psel(config_.lrck.port, config_.lrck.pin);
  i2s_->PSEL.SDIN =
      isConnected(config_.sdin) ? make_psel(config_.sdin.port, config_.sdin.pin)
                                : PSEL_DISCONNECTED;
  i2s_->PSEL.SDOUT = make_psel(config_.sdout.port, config_.sdout.pin);

  i2s_->CONFIG.MODE = (I2S_CONFIG_MODE_MODE_Master << I2S_CONFIG_MODE_MODE_Pos) &
                      I2S_CONFIG_MODE_MODE_Msk;
  i2s_->CONFIG.RXEN =
      (I2S_CONFIG_RXEN_RXEN_Disabled << I2S_CONFIG_RXEN_RXEN_Pos) &
      I2S_CONFIG_RXEN_RXEN_Msk;
  i2s_->CONFIG.TXEN =
      (I2S_CONFIG_TXEN_TXEN_Enabled << I2S_CONFIG_TXEN_TXEN_Pos) &
      I2S_CONFIG_TXEN_TXEN_Msk;
  i2s_->CONFIG.MCKEN =
      ((config_.enableMasterClock ? I2S_CONFIG_MCKEN_MCKEN_Enabled
                                  : I2S_CONFIG_MCKEN_MCKEN_Disabled)
       << I2S_CONFIG_MCKEN_MCKEN_Pos) &
      I2S_CONFIG_MCKEN_MCKEN_Msk;
  i2s_->CONFIG.MCKFREQ = config_.mckFreq;
  i2s_->CONFIG.RATIO =
      (config_.ratio << I2S_CONFIG_RATIO_RATIO_Pos) & I2S_CONFIG_RATIO_RATIO_Msk;
  i2s_->CONFIG.SWIDTH =
      (config_.sampleWidth << I2S_CONFIG_SWIDTH_SWIDTH_Pos) &
      I2S_CONFIG_SWIDTH_SWIDTH_Msk;
  i2s_->CONFIG.ALIGN =
      (config_.align << I2S_CONFIG_ALIGN_ALIGN_Pos) & I2S_CONFIG_ALIGN_ALIGN_Msk;
  i2s_->CONFIG.FORMAT =
      (config_.format << I2S_CONFIG_FORMAT_FORMAT_Pos) & I2S_CONFIG_FORMAT_FORMAT_Msk;
  i2s_->CONFIG.CHANNELS =
      (config_.channels << I2S_CONFIG_CHANNELS_CHANNELS_Pos) &
      I2S_CONFIG_CHANNELS_CHANNELS_Msk;

  i2s_->RXTXD.MAXCNT = (wordCount_ << I2S_RXTXD_MAXCNT_MAXCNT_Pos) &
                       I2S_RXTXD_MAXCNT_MAXCNT_Msk;
  i2s_->INTENCLR = I2S_INTENCLR_TXPTRUPD_Msk | I2S_INTENCLR_STOPPED_Msk;
  clearEvents();
  if (refillCallback_ != nullptr) {
    refillCallback_(buffers_[0], wordCount_, refillContext_);
    refillCallback_(buffers_[1], wordCount_, refillContext_);
  }
  armBuffer(0U);

  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Enabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;

  NVIC_SetPriority(I2S20_IRQn, config_.irqPriority);
  NVIC_EnableIRQ(I2S20_IRQn);

  nextBufferIndex_ = 1U;
  configured_ = true;
  running_ = false;
  restartPending_ = false;
  txPtrUpdCount_ = 0U;
  stoppedCount_ = 0U;
  restartCount_ = 0U;
  manualStopCount_ = 0U;
  return true;
}

void I2sTx::end() {
  if (!configured_ || i2s_ == nullptr) {
    return;
  }

  i2s_->INTENCLR = I2S_INTENCLR_TXPTRUPD_Msk | I2S_INTENCLR_STOPPED_Msk;
  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  waitForNonZero(&i2s_->EVENTS_STOPPED, 300000UL);
  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Disabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;
  i2s_->PSEL.MCK = PSEL_DISCONNECTED;
  i2s_->PSEL.SCK = PSEL_DISCONNECTED;
  i2s_->PSEL.LRCK = PSEL_DISCONNECTED;
  i2s_->PSEL.SDIN = PSEL_DISCONNECTED;
  i2s_->PSEL.SDOUT = PSEL_DISCONNECTED;
  if (g_activeI2sTx == this) {
    g_activeI2sTx = nullptr;
  }
  configured_ = false;
  running_ = false;
  restartPending_ = false;
}

bool I2sTx::start() {
  if (!configured_ || i2s_ == nullptr) {
    return false;
  }

  clearEvents();
  nextBufferIndex_ = 1U;
  armBuffer(0U);
  i2s_->INTENSET = I2S_INTENSET_TXPTRUPD_Msk | I2S_INTENSET_STOPPED_Msk;
  running_ = true;
  restartPending_ = false;
  i2s_->TASKS_START = I2S_TASKS_START_TASKS_START_Trigger;
  return true;
}

bool I2sTx::stop() {
  if (!configured_ || !running_ || i2s_ == nullptr) {
    return false;
  }

  ++manualStopCount_;
  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  return true;
}

void I2sTx::service() {
  if (restartPending_ && config_.autoRestart) {
    restartPending_ = false;
    ++restartCount_;
    (void)start();
  }
}

void I2sTx::onIrq() {
  if (!configured_ || i2s_ == nullptr) {
    return;
  }

  if (i2s_->EVENTS_TXPTRUPD != 0U) {
    i2s_->EVENTS_TXPTRUPD = 0U;
    const uint8_t queuedIndex = nextBufferIndex_;
    armBuffer(queuedIndex);
    nextBufferIndex_ ^= 1U;
    if (refillCallback_ != nullptr) {
      refillCallback_(buffers_[nextBufferIndex_], wordCount_, refillContext_);
    }
    ++txPtrUpdCount_;
  }

  if (i2s_->EVENTS_STOPPED != 0U) {
    i2s_->EVENTS_STOPPED = 0U;
    running_ = false;
    restartPending_ = true;
    ++stoppedCount_;
  }
}

bool I2sTx::makeActive() {
  if (!configured_) {
    return false;
  }
  g_activeI2sDuplex = nullptr;
  g_activeI2sRx = nullptr;
  g_activeI2sTx = this;
  return true;
}

void I2sTx::irqHandler() {
  if (g_activeI2sTx != nullptr) {
    g_activeI2sTx->onIrq();
  }
}

bool I2sTx::configured() const { return configured_; }

bool I2sTx::running() const { return running_; }

bool I2sTx::restartPending() const { return restartPending_; }

uint32_t I2sTx::txPtrUpdCount() const { return txPtrUpdCount_; }

uint32_t I2sTx::stoppedCount() const { return stoppedCount_; }

uint32_t I2sTx::restartCount() const { return restartCount_; }

uint32_t I2sTx::manualStopCount() const { return manualStopCount_; }

void I2sTx::clearEvents() {
  i2s_->EVENTS_TXPTRUPD = 0U;
  i2s_->EVENTS_STOPPED = 0U;
}

void I2sTx::armBuffer(uint8_t bufferIndex) {
  const uint8_t index = bufferIndex & 1U;
  i2s_->TXD.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffers_[index])) &
      I2S_TXD_PTR_PTR_Msk;
}

I2sRx::I2sRx(uint32_t base)
    : i2s_(reinterpret_cast<NRF_I2S_Type*>(static_cast<uintptr_t>(base))),
      config_(),
      buffers_{nullptr, nullptr},
      wordCount_(0U),
      receiveCallback_(nullptr),
      receiveContext_(nullptr),
      nextBufferIndex_(1U),
      configured_(false),
      running_(false),
      restartPending_(false),
      rxPtrUpdCount_(0U),
      stoppedCount_(0U),
      restartCount_(0U),
      manualStopCount_(0U) {}

bool I2sRx::setBuffers(uint32_t* buffer0, uint32_t* buffer1, uint32_t wordCount) {
  if (buffer0 == nullptr || buffer1 == nullptr || wordCount == 0U) {
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(buffer0) & 0x3U) != 0U ||
      (reinterpret_cast<uintptr_t>(buffer1) & 0x3U) != 0U) {
    return false;
  }
  if (wordCount >
      (I2S_RXTXD_MAXCNT_MAXCNT_Msk >> I2S_RXTXD_MAXCNT_MAXCNT_Pos)) {
    return false;
  }

  buffers_[0] = buffer0;
  buffers_[1] = buffer1;
  wordCount_ = wordCount;
  return true;
}

void I2sRx::setReceiveCallback(ReceiveCallback callback, void* context) {
  receiveCallback_ = callback;
  receiveContext_ = context;
}

bool I2sRx::begin(const I2sRxConfig& config, uint32_t* buffer0, uint32_t* buffer1,
                  uint32_t wordCount) {
  if (i2s_ == nullptr || !setBuffers(buffer0, buffer1, wordCount)) {
    return false;
  }
  if (!isConnected(config.mck) || !isConnected(config.sck) ||
      !isConnected(config.lrck) || !isConnected(config.sdin)) {
    return false;
  }
  if (!Gpio::configure(config.mck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.lrck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sdin, GpioDirection::kInput, GpioPull::kDisabled)) {
    return false;
  }
  if (isConnected(config.sdout) &&
      !Gpio::configure(config.sdout, GpioDirection::kOutput, GpioPull::kDisabled)) {
    return false;
  }

  config_ = config;

  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Disabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;

  i2s_->PSEL.MCK = make_psel(config_.mck.port, config_.mck.pin);
  i2s_->PSEL.SCK = make_psel(config_.sck.port, config_.sck.pin);
  i2s_->PSEL.LRCK = make_psel(config_.lrck.port, config_.lrck.pin);
  i2s_->PSEL.SDIN = make_psel(config_.sdin.port, config_.sdin.pin);
  i2s_->PSEL.SDOUT =
      isConnected(config_.sdout) ? make_psel(config_.sdout.port, config_.sdout.pin)
                                 : PSEL_DISCONNECTED;

  i2s_->CONFIG.MODE = (I2S_CONFIG_MODE_MODE_Master << I2S_CONFIG_MODE_MODE_Pos) &
                      I2S_CONFIG_MODE_MODE_Msk;
  i2s_->CONFIG.RXEN =
      (I2S_CONFIG_RXEN_RXEN_Enabled << I2S_CONFIG_RXEN_RXEN_Pos) &
      I2S_CONFIG_RXEN_RXEN_Msk;
  i2s_->CONFIG.TXEN =
      (I2S_CONFIG_TXEN_TXEN_Disabled << I2S_CONFIG_TXEN_TXEN_Pos) &
      I2S_CONFIG_TXEN_TXEN_Msk;
  i2s_->CONFIG.MCKEN =
      ((config_.enableMasterClock ? I2S_CONFIG_MCKEN_MCKEN_Enabled
                                  : I2S_CONFIG_MCKEN_MCKEN_Disabled)
       << I2S_CONFIG_MCKEN_MCKEN_Pos) &
      I2S_CONFIG_MCKEN_MCKEN_Msk;
  i2s_->CONFIG.MCKFREQ = config_.mckFreq;
  i2s_->CONFIG.RATIO =
      (config_.ratio << I2S_CONFIG_RATIO_RATIO_Pos) & I2S_CONFIG_RATIO_RATIO_Msk;
  i2s_->CONFIG.SWIDTH =
      (config_.sampleWidth << I2S_CONFIG_SWIDTH_SWIDTH_Pos) &
      I2S_CONFIG_SWIDTH_SWIDTH_Msk;
  i2s_->CONFIG.ALIGN =
      (config_.align << I2S_CONFIG_ALIGN_ALIGN_Pos) & I2S_CONFIG_ALIGN_ALIGN_Msk;
  i2s_->CONFIG.FORMAT =
      (config_.format << I2S_CONFIG_FORMAT_FORMAT_Pos) & I2S_CONFIG_FORMAT_FORMAT_Msk;
  i2s_->CONFIG.CHANNELS =
      (config_.channels << I2S_CONFIG_CHANNELS_CHANNELS_Pos) &
      I2S_CONFIG_CHANNELS_CHANNELS_Msk;

  i2s_->RXTXD.MAXCNT = (wordCount_ << I2S_RXTXD_MAXCNT_MAXCNT_Pos) &
                       I2S_RXTXD_MAXCNT_MAXCNT_Msk;
  i2s_->INTENCLR = I2S_INTENCLR_RXPTRUPD_Msk | I2S_INTENCLR_STOPPED_Msk;
  clearEvents();
  armBuffer(0U);

  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Enabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;

  NVIC_SetPriority(I2S20_IRQn, config_.irqPriority);
  NVIC_EnableIRQ(I2S20_IRQn);

  nextBufferIndex_ = 1U;
  configured_ = true;
  running_ = false;
  restartPending_ = false;
  rxPtrUpdCount_ = 0U;
  stoppedCount_ = 0U;
  restartCount_ = 0U;
  manualStopCount_ = 0U;
  return true;
}

void I2sRx::end() {
  if (!configured_ || i2s_ == nullptr) {
    return;
  }

  i2s_->INTENCLR = I2S_INTENCLR_RXPTRUPD_Msk | I2S_INTENCLR_STOPPED_Msk;
  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  waitForNonZero(&i2s_->EVENTS_STOPPED, 300000UL);
  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Disabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;
  i2s_->PSEL.MCK = PSEL_DISCONNECTED;
  i2s_->PSEL.SCK = PSEL_DISCONNECTED;
  i2s_->PSEL.LRCK = PSEL_DISCONNECTED;
  i2s_->PSEL.SDIN = PSEL_DISCONNECTED;
  i2s_->PSEL.SDOUT = PSEL_DISCONNECTED;
  if (g_activeI2sRx == this) {
    g_activeI2sRx = nullptr;
  }
  configured_ = false;
  running_ = false;
  restartPending_ = false;
}

bool I2sRx::start() {
  if (!configured_ || i2s_ == nullptr) {
    return false;
  }

  clearEvents();
  nextBufferIndex_ = 1U;
  armBuffer(0U);
  i2s_->INTENSET = I2S_INTENSET_RXPTRUPD_Msk | I2S_INTENSET_STOPPED_Msk;
  running_ = true;
  restartPending_ = false;
  i2s_->TASKS_START = I2S_TASKS_START_TASKS_START_Trigger;
  return true;
}

bool I2sRx::stop() {
  if (!configured_ || !running_ || i2s_ == nullptr) {
    return false;
  }

  ++manualStopCount_;
  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  return true;
}

void I2sRx::service() {
  if (restartPending_ && config_.autoRestart) {
    restartPending_ = false;
    ++restartCount_;
    (void)start();
  }
}

void I2sRx::onIrq() {
  if (!configured_ || i2s_ == nullptr) {
    return;
  }

  if (i2s_->EVENTS_RXPTRUPD != 0U) {
    i2s_->EVENTS_RXPTRUPD = 0U;
    const uint8_t queuedIndex = nextBufferIndex_;
    armBuffer(queuedIndex);
    nextBufferIndex_ ^= 1U;
    if (receiveCallback_ != nullptr) {
      receiveCallback_(buffers_[nextBufferIndex_], wordCount_, receiveContext_);
    }
    ++rxPtrUpdCount_;
  }

  if (i2s_->EVENTS_STOPPED != 0U) {
    i2s_->EVENTS_STOPPED = 0U;
    running_ = false;
    restartPending_ = true;
    ++stoppedCount_;
  }
}

bool I2sRx::makeActive() {
  if (!configured_) {
    return false;
  }
  g_activeI2sDuplex = nullptr;
  g_activeI2sTx = nullptr;
  g_activeI2sRx = this;
  return true;
}

void I2sRx::irqHandler() {
  if (g_activeI2sRx != nullptr) {
    g_activeI2sRx->onIrq();
  }
}

bool I2sRx::configured() const { return configured_; }

bool I2sRx::running() const { return running_; }

bool I2sRx::restartPending() const { return restartPending_; }

uint32_t I2sRx::rxPtrUpdCount() const { return rxPtrUpdCount_; }

uint32_t I2sRx::stoppedCount() const { return stoppedCount_; }

uint32_t I2sRx::restartCount() const { return restartCount_; }

uint32_t I2sRx::manualStopCount() const { return manualStopCount_; }

void I2sRx::clearEvents() {
  i2s_->EVENTS_RXPTRUPD = 0U;
  i2s_->EVENTS_STOPPED = 0U;
}

void I2sRx::armBuffer(uint8_t bufferIndex) {
  const uint8_t index = bufferIndex & 1U;
  i2s_->RXD.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffers_[index])) &
      I2S_RXD_PTR_PTR_Msk;
}

I2sDuplex::I2sDuplex(uint32_t base)
    : i2s_(reinterpret_cast<NRF_I2S_Type*>(static_cast<uintptr_t>(base))),
      config_(),
      txBuffers_{nullptr, nullptr},
      rxBuffers_{nullptr, nullptr},
      wordCount_(0U),
      txRefillCallback_(nullptr),
      txRefillContext_(nullptr),
      rxReceiveCallback_(nullptr),
      rxReceiveContext_(nullptr),
      nextTxBufferIndex_(1U),
      nextRxBufferIndex_(1U),
      configured_(false),
      running_(false),
      restartPending_(false),
      txPtrUpdCount_(0U),
      rxPtrUpdCount_(0U),
      stoppedCount_(0U),
      restartCount_(0U),
      manualStopCount_(0U) {}

bool I2sDuplex::setTxBuffers(uint32_t* buffer0, uint32_t* buffer1,
                             uint32_t wordCount) {
  if (buffer0 == nullptr || buffer1 == nullptr || wordCount == 0U) {
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(buffer0) & 0x3U) != 0U ||
      (reinterpret_cast<uintptr_t>(buffer1) & 0x3U) != 0U) {
    return false;
  }
  if (wordCount >
      (I2S_RXTXD_MAXCNT_MAXCNT_Msk >> I2S_RXTXD_MAXCNT_MAXCNT_Pos)) {
    return false;
  }

  txBuffers_[0] = buffer0;
  txBuffers_[1] = buffer1;
  wordCount_ = wordCount;
  return true;
}

bool I2sDuplex::setRxBuffers(uint32_t* buffer0, uint32_t* buffer1,
                             uint32_t wordCount) {
  if (buffer0 == nullptr || buffer1 == nullptr || wordCount == 0U) {
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(buffer0) & 0x3U) != 0U ||
      (reinterpret_cast<uintptr_t>(buffer1) & 0x3U) != 0U) {
    return false;
  }
  if (wordCount >
      (I2S_RXTXD_MAXCNT_MAXCNT_Msk >> I2S_RXTXD_MAXCNT_MAXCNT_Pos)) {
    return false;
  }
  if (wordCount_ != 0U && wordCount_ != wordCount) {
    return false;
  }

  rxBuffers_[0] = buffer0;
  rxBuffers_[1] = buffer1;
  wordCount_ = wordCount;
  return true;
}

void I2sDuplex::setTxRefillCallback(TxRefillCallback callback, void* context) {
  txRefillCallback_ = callback;
  txRefillContext_ = context;
}

void I2sDuplex::setRxReceiveCallback(RxReceiveCallback callback, void* context) {
  rxReceiveCallback_ = callback;
  rxReceiveContext_ = context;
}

bool I2sDuplex::begin(const I2sDuplexConfig& config, uint32_t* txBuffer0,
                      uint32_t* txBuffer1, uint32_t* rxBuffer0,
                      uint32_t* rxBuffer1, uint32_t wordCount) {
  if (i2s_ == nullptr ||
      !setTxBuffers(txBuffer0, txBuffer1, wordCount) ||
      !setRxBuffers(rxBuffer0, rxBuffer1, wordCount)) {
    return false;
  }
  if (!isConnected(config.mck) || !isConnected(config.sck) ||
      !isConnected(config.lrck) || !isConnected(config.sdin) ||
      !isConnected(config.sdout)) {
    return false;
  }
  if (!Gpio::configure(config.mck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.lrck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sdin, GpioDirection::kInput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sdout, GpioDirection::kOutput, GpioPull::kDisabled)) {
    return false;
  }

  config_ = config;

  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Disabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;

  i2s_->PSEL.MCK = make_psel(config_.mck.port, config_.mck.pin);
  i2s_->PSEL.SCK = make_psel(config_.sck.port, config_.sck.pin);
  i2s_->PSEL.LRCK = make_psel(config_.lrck.port, config_.lrck.pin);
  i2s_->PSEL.SDIN = make_psel(config_.sdin.port, config_.sdin.pin);
  i2s_->PSEL.SDOUT = make_psel(config_.sdout.port, config_.sdout.pin);

  i2s_->CONFIG.MODE = (I2S_CONFIG_MODE_MODE_Master << I2S_CONFIG_MODE_MODE_Pos) &
                      I2S_CONFIG_MODE_MODE_Msk;
  i2s_->CONFIG.RXEN =
      (I2S_CONFIG_RXEN_RXEN_Enabled << I2S_CONFIG_RXEN_RXEN_Pos) &
      I2S_CONFIG_RXEN_RXEN_Msk;
  i2s_->CONFIG.TXEN =
      (I2S_CONFIG_TXEN_TXEN_Enabled << I2S_CONFIG_TXEN_TXEN_Pos) &
      I2S_CONFIG_TXEN_TXEN_Msk;
  i2s_->CONFIG.MCKEN =
      ((config_.enableMasterClock ? I2S_CONFIG_MCKEN_MCKEN_Enabled
                                  : I2S_CONFIG_MCKEN_MCKEN_Disabled)
       << I2S_CONFIG_MCKEN_MCKEN_Pos) &
      I2S_CONFIG_MCKEN_MCKEN_Msk;
  i2s_->CONFIG.MCKFREQ = config_.mckFreq;
  i2s_->CONFIG.RATIO =
      (config_.ratio << I2S_CONFIG_RATIO_RATIO_Pos) & I2S_CONFIG_RATIO_RATIO_Msk;
  i2s_->CONFIG.SWIDTH =
      (config_.sampleWidth << I2S_CONFIG_SWIDTH_SWIDTH_Pos) &
      I2S_CONFIG_SWIDTH_SWIDTH_Msk;
  i2s_->CONFIG.ALIGN =
      (config_.align << I2S_CONFIG_ALIGN_ALIGN_Pos) & I2S_CONFIG_ALIGN_ALIGN_Msk;
  i2s_->CONFIG.FORMAT =
      (config_.format << I2S_CONFIG_FORMAT_FORMAT_Pos) & I2S_CONFIG_FORMAT_FORMAT_Msk;
  i2s_->CONFIG.CHANNELS =
      (config_.channels << I2S_CONFIG_CHANNELS_CHANNELS_Pos) &
      I2S_CONFIG_CHANNELS_CHANNELS_Msk;

  i2s_->RXTXD.MAXCNT = (wordCount_ << I2S_RXTXD_MAXCNT_MAXCNT_Pos) &
                       I2S_RXTXD_MAXCNT_MAXCNT_Msk;
  i2s_->INTENCLR = I2S_INTENCLR_TXPTRUPD_Msk | I2S_INTENCLR_RXPTRUPD_Msk |
                   I2S_INTENCLR_STOPPED_Msk;
  clearEvents();
  armTxBuffer(0U);
  armRxBuffer(0U);

  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Enabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;

  NVIC_SetPriority(I2S20_IRQn, config_.irqPriority);
  NVIC_EnableIRQ(I2S20_IRQn);

  nextTxBufferIndex_ = 1U;
  nextRxBufferIndex_ = 1U;
  configured_ = true;
  running_ = false;
  restartPending_ = false;
  txPtrUpdCount_ = 0U;
  rxPtrUpdCount_ = 0U;
  stoppedCount_ = 0U;
  restartCount_ = 0U;
  manualStopCount_ = 0U;
  return true;
}

void I2sDuplex::end() {
  if (!configured_ || i2s_ == nullptr) {
    return;
  }

  i2s_->INTENCLR = I2S_INTENCLR_TXPTRUPD_Msk | I2S_INTENCLR_RXPTRUPD_Msk |
                   I2S_INTENCLR_STOPPED_Msk;
  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  waitForNonZero(&i2s_->EVENTS_STOPPED, 300000UL);
  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Disabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;
  i2s_->PSEL.MCK = PSEL_DISCONNECTED;
  i2s_->PSEL.SCK = PSEL_DISCONNECTED;
  i2s_->PSEL.LRCK = PSEL_DISCONNECTED;
  i2s_->PSEL.SDIN = PSEL_DISCONNECTED;
  i2s_->PSEL.SDOUT = PSEL_DISCONNECTED;
  if (g_activeI2sDuplex == this) {
    g_activeI2sDuplex = nullptr;
  }
  configured_ = false;
  running_ = false;
  restartPending_ = false;
}

bool I2sDuplex::start() {
  if (!configured_ || i2s_ == nullptr) {
    return false;
  }

  clearEvents();
  nextTxBufferIndex_ = 1U;
  nextRxBufferIndex_ = 1U;
  armTxBuffer(0U);
  armRxBuffer(0U);
  i2s_->INTENSET = I2S_INTENSET_TXPTRUPD_Msk | I2S_INTENSET_RXPTRUPD_Msk |
                   I2S_INTENSET_STOPPED_Msk;
  running_ = true;
  restartPending_ = false;
  i2s_->TASKS_START = I2S_TASKS_START_TASKS_START_Trigger;
  return true;
}

bool I2sDuplex::stop() {
  if (!configured_ || !running_ || i2s_ == nullptr) {
    return false;
  }

  ++manualStopCount_;
  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  return true;
}

void I2sDuplex::service() {
  if (restartPending_ && config_.autoRestart) {
    restartPending_ = false;
    ++restartCount_;
    (void)start();
  }
}

void I2sDuplex::onIrq() {
  if (!configured_ || i2s_ == nullptr) {
    return;
  }

  if (i2s_->EVENTS_TXPTRUPD != 0U) {
    i2s_->EVENTS_TXPTRUPD = 0U;
    const uint8_t queuedIndex = nextTxBufferIndex_;
    armTxBuffer(queuedIndex);
    nextTxBufferIndex_ ^= 1U;
    if (txRefillCallback_ != nullptr) {
      txRefillCallback_(txBuffers_[nextTxBufferIndex_], wordCount_,
                        txRefillContext_);
    }
    ++txPtrUpdCount_;
  }

  if (i2s_->EVENTS_RXPTRUPD != 0U) {
    i2s_->EVENTS_RXPTRUPD = 0U;
    const uint8_t queuedIndex = nextRxBufferIndex_;
    armRxBuffer(queuedIndex);
    nextRxBufferIndex_ ^= 1U;
    if (rxReceiveCallback_ != nullptr) {
      rxReceiveCallback_(rxBuffers_[nextRxBufferIndex_], wordCount_,
                         rxReceiveContext_);
    }
    ++rxPtrUpdCount_;
  }

  if (i2s_->EVENTS_STOPPED != 0U) {
    i2s_->EVENTS_STOPPED = 0U;
    running_ = false;
    restartPending_ = true;
    ++stoppedCount_;
  }
}

bool I2sDuplex::makeActive() {
  if (!configured_) {
    return false;
  }
  g_activeI2sTx = nullptr;
  g_activeI2sRx = nullptr;
  g_activeI2sDuplex = this;
  return true;
}

void I2sDuplex::irqHandler() {
  if (g_activeI2sDuplex != nullptr) {
    g_activeI2sDuplex->onIrq();
  }
}

bool I2sDuplex::configured() const { return configured_; }

bool I2sDuplex::running() const { return running_; }

bool I2sDuplex::restartPending() const { return restartPending_; }

uint32_t I2sDuplex::txPtrUpdCount() const { return txPtrUpdCount_; }

uint32_t I2sDuplex::rxPtrUpdCount() const { return rxPtrUpdCount_; }

uint32_t I2sDuplex::stoppedCount() const { return stoppedCount_; }

uint32_t I2sDuplex::restartCount() const { return restartCount_; }

uint32_t I2sDuplex::manualStopCount() const { return manualStopCount_; }

void I2sDuplex::clearEvents() {
  i2s_->EVENTS_TXPTRUPD = 0U;
  i2s_->EVENTS_RXPTRUPD = 0U;
  i2s_->EVENTS_STOPPED = 0U;
}

void I2sDuplex::armTxBuffer(uint8_t bufferIndex) {
  const uint8_t index = bufferIndex & 1U;
  i2s_->TXD.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(txBuffers_[index])) &
      I2S_TXD_PTR_PTR_Msk;
}

void I2sDuplex::armRxBuffer(uint8_t bufferIndex) {
  const uint8_t index = bufferIndex & 1U;
  i2s_->RXD.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rxBuffers_[index])) &
      I2S_RXD_PTR_PTR_Msk;
}

ZigbeeRadio::ZigbeeRadio(uint32_t radioBase)
    : radio_(reinterpret_cast<NRF_RADIO_Type*>(static_cast<uintptr_t>(radioBase))),
      initialized_(false),
      channel_(15U),
      txPacket_{0},
      rxPacket_{0} {}

bool ZigbeeRadio::configureIeee802154() {
  if (radio_ == nullptr) {
    return false;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitRadioDisabledBudgeted(radio_, 0U, 200000UL);
  radio_->TASKS_SOFTRESET = RADIO_TASKS_SOFTRESET_TASKS_SOFTRESET_Trigger;

  radio_->MODE = ((RADIO_MODE_MODE_Ieee802154_250Kbit << RADIO_MODE_MODE_Pos) &
                  RADIO_MODE_MODE_Msk);
  radio_->TIMING = ((RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) &
                    RADIO_TIMING_RU_Msk);

  uint32_t pcnf0 = 0U;
  pcnf0 |= (8UL << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk;
  pcnf0 |= (RADIO_PCNF0_S1INCL_Automatic << RADIO_PCNF0_S1INCL_Pos) &
           RADIO_PCNF0_S1INCL_Msk;
  pcnf0 |= (RADIO_PCNF0_PLEN_32bitZero << RADIO_PCNF0_PLEN_Pos) &
           RADIO_PCNF0_PLEN_Msk;
  pcnf0 |= (RADIO_PCNF0_CRCINC_Exclude << RADIO_PCNF0_CRCINC_Pos) &
           RADIO_PCNF0_CRCINC_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_TERMLEN_Pos) & RADIO_PCNF0_TERMLEN_Msk;
  radio_->PCNF0 = pcnf0;

  uint32_t pcnf1 = 0U;
  pcnf1 |= (127UL << RADIO_PCNF1_MAXLEN_Pos) & RADIO_PCNF1_MAXLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk;
  pcnf1 |= (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) &
           RADIO_PCNF1_ENDIAN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos) &
           RADIO_PCNF1_WHITEEN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEOFFSET_Include << RADIO_PCNF1_WHITEOFFSET_Pos) &
           RADIO_PCNF1_WHITEOFFSET_Msk;
  radio_->PCNF1 = pcnf1;

  uint32_t crccnf = 0U;
  crccnf |= (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) &
            RADIO_CRCCNF_LEN_Msk;
  crccnf |= (RADIO_CRCCNF_SKIPADDR_Ieee802154 << RADIO_CRCCNF_SKIPADDR_Pos) &
            RADIO_CRCCNF_SKIPADDR_Msk;
  radio_->CRCCNF = crccnf;
  radio_->CRCPOLY = (0x11021UL & RADIO_CRCPOLY_CRCPOLY_Msk);
  radio_->CRCINIT = 0U;

  // IEEE 802.15.4 SFD, CCA and ED defaults.
  radio_->SFD = 0xA7U;
  radio_->EDCTRL = RADIO_EDCTRL_ResetValue;
  radio_->CCACTRL = RADIO_CCACTRL_ResetValue;

  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->EVENTS_MHRMATCH = 0U;
  radio_->EVENTS_EDEND = 0U;
  radio_->EVENTS_CCAIDLE = 0U;
  radio_->EVENTS_CCABUSY = 0U;
  radio_->EVENTS_CCASTOPPED = 0U;
  return true;
}

bool ZigbeeRadio::begin(uint8_t channel, int8_t txPowerDbm) {
  if (radio_ == nullptr) {
    initialized_ = false;
    return false;
  }

  if (!ClockControl::startHfxo(true, 1500000UL)) {
    initialized_ = false;
    return false;
  }
  if (!configureIeee802154()) {
    initialized_ = false;
    return false;
  }
  if (!setChannel(channel)) {
    initialized_ = false;
    return false;
  }
  if (!setTxPowerDbm(txPowerDbm)) {
    initialized_ = false;
    return false;
  }

  initialized_ = true;
  return true;
}

void ZigbeeRadio::end() {
  if (radio_ == nullptr) {
    initialized_ = false;
    return;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitRadioDisabledBudgeted(radio_, 0U, 200000UL);
  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->EVENTS_MHRMATCH = 0U;
  radio_->EVENTS_EDEND = 0U;
  radio_->EVENTS_CCAIDLE = 0U;
  radio_->EVENTS_CCABUSY = 0U;
  radio_->EVENTS_CCASTOPPED = 0U;
  initialized_ = false;
}

bool ZigbeeRadio::setChannel(uint8_t channel) {
  if (radio_ == nullptr || !ieee802154ChannelValid(channel)) {
    return false;
  }

  const uint8_t freq = ieee802154ChannelToFrequency(channel);
  radio_->FREQUENCY =
      ((static_cast<uint32_t>(freq) << RADIO_FREQUENCY_FREQUENCY_Pos) &
       RADIO_FREQUENCY_FREQUENCY_Msk) |
      (0UL << RADIO_FREQUENCY_MAP_Pos);

  channel_ = channel;
  return true;
}

uint8_t ZigbeeRadio::channel() const { return channel_; }

bool ZigbeeRadio::setTxPowerDbm(int8_t dbm) {
  if (radio_ == nullptr) {
    return false;
  }
  const uint32_t regValue = radioTxPowerRegFromDbm(dbm);
  radio_->TXPOWER = ((regValue << RADIO_TXPOWER_TXPOWER_Pos) &
                     RADIO_TXPOWER_TXPOWER_Msk);
  return true;
}

bool ZigbeeRadio::performCcaCheck(uint32_t spinLimit) {
  if (radio_ == nullptr) {
    return false;
  }
  if (spinLimit == 0U) {
    spinLimit = 1U;
  }

  clearRadioCoreEvents(radio_);
  radio_->EVENTS_CCAIDLE = 0U;
  radio_->EVENTS_CCABUSY = 0U;
  radio_->EVENTS_CCASTOPPED = 0U;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk);
  radio_->EVENTS_READY = 0U;
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  if (!waitForNonZero(&radio_->EVENTS_READY, spinLimit)) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitRadioDisabledBudgeted(radio_, 0U, spinLimit);
    radio_->SHORTS = 0U;
    return false;
  }

  radio_->TASKS_CCASTART = RADIO_TASKS_CCASTART_TASKS_CCASTART_Trigger;
  bool idle = false;
  uint32_t spins = spinLimit;
  while (spins-- > 0U) {
    if (radio_->EVENTS_CCAIDLE != 0U) {
      idle = true;
      break;
    }
    if (radio_->EVENTS_CCABUSY != 0U || radio_->EVENTS_CCASTOPPED != 0U) {
      idle = false;
      break;
    }
  }

  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitRadioDisabledBudgeted(radio_, 0U, spinLimit);
  radio_->SHORTS = 0U;
  return idle;
}

bool ZigbeeRadio::transmit(const uint8_t* psdu, uint8_t length, bool performCca,
                           uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || psdu == nullptr) {
    return false;
  }
  if (length == 0U || length > 127U) {
    return false;
  }

  if (performCca && !performCcaCheck(spinLimit / 2U + 1U)) {
    return false;
  }

  txPacket_[0] = length;
  memcpy(&txPacket_[1], psdu, length);

  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(txPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool endSeen = waitRadioEndBudgeted(radio_, 12000U, spinLimit);

  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  clearRadioCoreEvents(radio_);
  return endSeen && disabled;
}

bool ZigbeeRadio::receive(ZigbeeFrame* frame, uint32_t listenWindowUs,
                          uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || frame == nullptr) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk);

  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  const bool rxDone = waitRadioRxDoneBudgeted(radio_, listenWindowUs, spinLimit);
  const int8_t rssiDbm = radioRssiDbm(radio_);

  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  if (!disabled || !rxDone) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOkEvent = (radio_->EVENTS_CRCOK != 0U);
  const bool crcOk = crcOkEvent || (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  if (!crcOk) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  const uint8_t length = rxPacket_[0];
  if (length == 0U || length > 127U) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  frame->channel = channel_;
  frame->rssiDbm = rssiDbm;
  frame->length = length;
  memcpy(frame->psdu, &rxPacket_[1], length);
  clearRadioCoreEvents(radio_);
  return true;
}

bool ZigbeeRadio::sampleEnergyDetect(uint8_t* outEdLevel, uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || outEdLevel == nullptr) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  radio_->EVENTS_EDEND = 0U;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk);
  radio_->EVENTS_READY = 0U;
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  if (!waitForNonZero(&radio_->EVENTS_READY, spinLimit)) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitRadioDisabledBudgeted(radio_, 0U, spinLimit);
    radio_->SHORTS = 0U;
    return false;
  }

  radio_->TASKS_EDSTART = RADIO_TASKS_EDSTART_TASKS_EDSTART_Trigger;
  if (!waitForNonZero(&radio_->EVENTS_EDEND, spinLimit)) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitRadioDisabledBudgeted(radio_, 0U, spinLimit);
    radio_->SHORTS = 0U;
    return false;
  }

  const uint8_t edLevel =
      static_cast<uint8_t>((radio_->EDSAMPLE & RADIO_EDSAMPLE_EDLVL_Msk) >>
                           RADIO_EDSAMPLE_EDLVL_Pos);
  *outEdLevel = edLevel;

  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitRadioDisabledBudgeted(radio_, 0U, spinLimit);
  radio_->SHORTS = 0U;
  return true;
}

RawRadioLink::RawRadioLink(uint32_t radioBase)
    : radio_(reinterpret_cast<NRF_RADIO_Type*>(static_cast<uintptr_t>(radioBase))),
      power_(),
      config_(),
      initialized_(false),
      receiverArmed_(false),
      txPacket_{0},
      rxPacket_{0} {}

bool RawRadioLink::configureProprietary1M() {
  if (radio_ == nullptr) {
    return false;
  }
  if (config_.maxPayloadLength == 0U) {
    return false;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitRadioDisabledBudgeted(radio_, 0U, 200000UL);
  radio_->TASKS_SOFTRESET = RADIO_TASKS_SOFTRESET_TASKS_SOFTRESET_Trigger;

  radio_->MODE = ((RADIO_MODE_MODE_Nrf_1Mbit << RADIO_MODE_MODE_Pos) &
                  RADIO_MODE_MODE_Msk);
  radio_->TIMING = ((RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) &
                    RADIO_TIMING_RU_Msk);

  uint32_t pcnf0 = 0U;
  pcnf0 |= (8UL << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk;
  pcnf0 |= (RADIO_PCNF0_S1INCL_Automatic << RADIO_PCNF0_S1INCL_Pos) &
           RADIO_PCNF0_S1INCL_Msk;
  pcnf0 |= (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos) &
           RADIO_PCNF0_PLEN_Msk;
  pcnf0 |= (RADIO_PCNF0_CRCINC_Exclude << RADIO_PCNF0_CRCINC_Pos) &
           RADIO_PCNF0_CRCINC_Msk;
  radio_->PCNF0 = pcnf0;

  uint32_t pcnf1 = 0U;
  pcnf1 |= (static_cast<uint32_t>(config_.maxPayloadLength)
            << RADIO_PCNF1_MAXLEN_Pos) & RADIO_PCNF1_MAXLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk;
  pcnf1 |= (4UL << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk;
  pcnf1 |= ((config_.bigEndian ? RADIO_PCNF1_ENDIAN_Big
                               : RADIO_PCNF1_ENDIAN_Little)
            << RADIO_PCNF1_ENDIAN_Pos) & RADIO_PCNF1_ENDIAN_Msk;
  pcnf1 |= ((config_.whiteningEnabled ? RADIO_PCNF1_WHITEEN_Enabled
                                      : RADIO_PCNF1_WHITEEN_Disabled)
            << RADIO_PCNF1_WHITEEN_Pos) & RADIO_PCNF1_WHITEEN_Msk;
  radio_->PCNF1 = pcnf1;

  radio_->BASE0 = (config_.addressBase0 & RADIO_BASE0_BASE0_Msk);
  radio_->PREFIX0 =
      ((static_cast<uint32_t>(config_.addressPrefix0) << RADIO_PREFIX0_AP0_Pos) &
       RADIO_PREFIX0_AP0_Msk);
  radio_->TXADDRESS =
      (0UL << RADIO_TXADDRESS_TXADDRESS_Pos) & RADIO_TXADDRESS_TXADDRESS_Msk;
  radio_->RXADDRESSES =
      (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos) &
      RADIO_RXADDRESSES_ADDR0_Msk;

  uint32_t crccnf = 0U;
  crccnf |= (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) &
            RADIO_CRCCNF_LEN_Msk;
  crccnf |= (RADIO_CRCCNF_SKIPADDR_Include << RADIO_CRCCNF_SKIPADDR_Pos) &
            RADIO_CRCCNF_SKIPADDR_Msk;
  radio_->CRCCNF = crccnf;
  radio_->CRCPOLY = (config_.crcPolynomial & RADIO_CRCPOLY_CRCPOLY_Msk);
  radio_->CRCINIT = (config_.crcInit & RADIO_CRCINIT_CRCINIT_Msk);

  return setFrequencyOffsetMhz(config_.frequencyOffsetMhz) &&
         setTxPowerDbm(config_.txPowerDbm);
}

bool RawRadioLink::begin(const RawRadioConfig& config) {
  if (radio_ == nullptr || config.maxPayloadLength == 0U) {
    initialized_ = false;
    receiverArmed_ = false;
    return false;
  }

  if (!ClockControl::startHfxo(true, 1500000UL)) {
    initialized_ = false;
    receiverArmed_ = false;
    return false;
  }

  config_ = config;
  power_.setLatencyMode(PowerLatencyMode::kConstantLatency);

  if (!configureProprietary1M()) {
    initialized_ = false;
    receiverArmed_ = false;
    return false;
  }

  clearRadioCoreEvents(radio_);
  memset(txPacket_, 0, sizeof(txPacket_));
  memset(rxPacket_, 0, sizeof(rxPacket_));
  initialized_ = true;
  receiverArmed_ = false;
  return true;
}

void RawRadioLink::end() {
  if (radio_ == nullptr) {
    initialized_ = false;
    receiverArmed_ = false;
    return;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitRadioDisabledBudgeted(radio_, 0U, 200000UL);
  clearRadioCoreEvents(radio_);
  initialized_ = false;
  receiverArmed_ = false;
}

bool RawRadioLink::setFrequencyOffsetMhz(uint8_t frequencyOffsetMhz) {
  if (radio_ == nullptr) {
    return false;
  }

  config_.frequencyOffsetMhz = frequencyOffsetMhz;
  radio_->FREQUENCY =
      ((static_cast<uint32_t>(config_.frequencyOffsetMhz)
        << RADIO_FREQUENCY_FREQUENCY_Pos) &
       RADIO_FREQUENCY_FREQUENCY_Msk) |
      (0UL << RADIO_FREQUENCY_MAP_Pos);
  return true;
}

bool RawRadioLink::setPipe(uint32_t addressBase0, uint8_t addressPrefix0) {
  if (radio_ == nullptr) {
    return false;
  }

  config_.addressBase0 = addressBase0;
  config_.addressPrefix0 = addressPrefix0;
  radio_->BASE0 = (config_.addressBase0 & RADIO_BASE0_BASE0_Msk);
  radio_->PREFIX0 =
      ((static_cast<uint32_t>(config_.addressPrefix0) << RADIO_PREFIX0_AP0_Pos) &
       RADIO_PREFIX0_AP0_Msk);
  return true;
}

bool RawRadioLink::setTxPowerDbm(int8_t dbm) {
  if (radio_ == nullptr) {
    return false;
  }

  config_.txPowerDbm = dbm;
  const uint32_t regValue = radioTxPowerRegFromDbm(dbm);
  radio_->TXPOWER = ((regValue << RADIO_TXPOWER_TXPOWER_Pos) &
                     RADIO_TXPOWER_TXPOWER_Msk);
  return true;
}

bool RawRadioLink::transmit(const uint8_t* payload, uint8_t length,
                            uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || payload == nullptr) {
    return false;
  }
  if (length == 0U || length > config_.maxPayloadLength) {
    return false;
  }

  txPacket_[0] = length;
  memcpy(&txPacket_[1], payload, length);

  clearRadioCoreEvents(radio_);
  receiverArmed_ = false;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(txPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool endSeen = waitRadioEndBudgeted(radio_, 12000U, spinLimit);
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  clearRadioCoreEvents(radio_);
  return endSeen && disabled;
}

bool RawRadioLink::armReceive() {
  if (!initialized_ || radio_ == nullptr) {
    return false;
  }

  memset(rxPacket_, 0, sizeof(rxPacket_));
  clearRadioCoreEvents(radio_);
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk);
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  receiverArmed_ = true;
  return true;
}

RawRadioReceiveStatus RawRadioLink::finishReceive(RawRadioPacket* outPacket,
                                                  uint32_t spinLimit,
                                                  bool timedOut) {
  if (radio_ == nullptr || !receiverArmed_) {
    return RawRadioReceiveStatus::kError;
  }

  const int8_t rssiDbm = radioRssiDbm(radio_);
  const bool disabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  receiverArmed_ = false;
  if (!disabled) {
    clearRadioCoreEvents(radio_);
    return RawRadioReceiveStatus::kError;
  }

  if (timedOut ||
      ((radio_->EVENTS_CRCOK == 0U) && (radio_->EVENTS_CRCERROR == 0U))) {
    clearRadioCoreEvents(radio_);
    return RawRadioReceiveStatus::kIdle;
  }

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOkEvent = (radio_->EVENTS_CRCOK != 0U);
  const bool crcOk = crcOkEvent || (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  if (!crcOk) {
    clearRadioCoreEvents(radio_);
    return RawRadioReceiveStatus::kCrcError;
  }

  const uint8_t length = minU8(rxPacket_[0], config_.maxPayloadLength);
  if (length == 0U) {
    clearRadioCoreEvents(radio_);
    return RawRadioReceiveStatus::kError;
  }

  if (outPacket != nullptr) {
    outPacket->length = length;
    outPacket->rssiDbm = rssiDbm;
    memcpy(outPacket->payload, &rxPacket_[1], length);
  }

  clearRadioCoreEvents(radio_);
  return RawRadioReceiveStatus::kPacket;
}

RawRadioReceiveStatus RawRadioLink::pollReceive(RawRadioPacket* outPacket,
                                                uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return RawRadioReceiveStatus::kError;
  }
  if (!receiverArmed_) {
    return RawRadioReceiveStatus::kIdle;
  }

  if ((radio_->EVENTS_CRCOK == 0U) && (radio_->EVENTS_CRCERROR == 0U) &&
      (radio_->EVENTS_END == 0U)) {
    return RawRadioReceiveStatus::kIdle;
  }

  return finishReceive(outPacket, spinLimit, false);
}

RawRadioReceiveStatus RawRadioLink::waitForReceive(RawRadioPacket* outPacket,
                                                   uint32_t listenWindowUs,
                                                   uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return RawRadioReceiveStatus::kError;
  }
  if (!receiverArmed_ && !armReceive()) {
    return RawRadioReceiveStatus::kError;
  }

  const bool rxDone = waitRadioRxDoneBudgeted(radio_, listenWindowUs, spinLimit);
  if (!rxDone) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  }

  return finishReceive(outPacket, spinLimit, !rxDone);
}

bool RawRadioLink::initialized() const { return initialized_; }

bool RawRadioLink::receiverArmed() const { return receiverArmed_; }

uint8_t RawRadioLink::maxPayloadLength() const { return config_.maxPayloadLength; }

const RawRadioConfig& RawRadioLink::config() const { return config_; }

bool ZigbeeRadio::buildDataFrameShort(uint8_t sequence, uint16_t panId,
                                      uint16_t destinationShort,
                                      uint16_t sourceShort, const uint8_t* payload,
                                      uint8_t payloadLength, uint8_t* outPsdu,
                                      uint8_t* outLength, bool requestAck) {
  if (outPsdu == nullptr || outLength == nullptr) {
    return false;
  }
  if (payloadLength > 0U && payload == nullptr) {
    return false;
  }

  // Data frame, PAN compression, destination short, source short.
  // Header layout: FCF(2), Seq(1), DstPAN(2), DstShort(2), SrcShort(2).
  constexpr uint8_t kHeaderLen = 9U;
  if (payloadLength > static_cast<uint8_t>(127U - kHeaderLen)) {
    return false;
  }

  uint16_t frameControl = 0x9841U;
  if (requestAck) {
    frameControl |= (1U << 5U);
  }

  writeLe16(&outPsdu[0], frameControl);
  outPsdu[2] = sequence;
  writeLe16(&outPsdu[3], panId);
  writeLe16(&outPsdu[5], destinationShort);
  writeLe16(&outPsdu[7], sourceShort);
  if (payloadLength > 0U) {
    memcpy(&outPsdu[kHeaderLen], payload, payloadLength);
  }
  *outLength = static_cast<uint8_t>(kHeaderLen + payloadLength);
  return true;
}

bool ZigbeeRadio::buildMacCommandFrameShort(
    uint8_t sequence, uint16_t panId, uint16_t destinationShort,
    uint16_t sourceShort, uint8_t commandId, const uint8_t* payload,
    uint8_t payloadLength, uint8_t* outPsdu, uint8_t* outLength,
    bool requestAck) {
  if (outPsdu == nullptr || outLength == nullptr) {
    return false;
  }
  if (payloadLength > 0U && payload == nullptr) {
    return false;
  }

  // MAC command frame with PAN compression, destination short, source short.
  // Header layout: FCF(2), Seq(1), DstPAN(2), DstShort(2), SrcShort(2),
  // CommandId(1).
  constexpr uint8_t kHeaderLen = 10U;
  if (payloadLength > static_cast<uint8_t>(127U - kHeaderLen)) {
    return false;
  }

  uint16_t frameControl = 0x9843U;
  if (requestAck) {
    frameControl |= (1U << 5U);
  }

  writeLe16(&outPsdu[0], frameControl);
  outPsdu[2] = sequence;
  writeLe16(&outPsdu[3], panId);
  writeLe16(&outPsdu[5], destinationShort);
  writeLe16(&outPsdu[7], sourceShort);
  outPsdu[9] = commandId;
  if (payloadLength > 0U) {
    memcpy(&outPsdu[kHeaderLen], payload, payloadLength);
  }
  *outLength = static_cast<uint8_t>(kHeaderLen + payloadLength);
  return true;
}

bool ZigbeeRadio::parseDataFrameShort(const uint8_t* psdu, uint8_t length,
                                      ZigbeeDataFrameView* outView) {
  if (outView != nullptr) {
    memset(outView, 0, sizeof(*outView));
  }
  if (psdu == nullptr || outView == nullptr || length < 9U) {
    return false;
  }

  const uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  const bool panCompression = ((frameControl >> 6U) & 0x1U) != 0U;
  const uint8_t dstAddrMode = static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  const uint8_t srcAddrMode = static_cast<uint8_t>((frameControl >> 14U) & 0x03U);

  if (frameType != 0x01U || dstAddrMode != 0x02U || srcAddrMode != 0x02U) {
    return false;
  }

  uint8_t offset = 3U;
  if (length < static_cast<uint8_t>(offset + 2U + 2U)) {
    return false;
  }
  const uint16_t destinationPan = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);
  const uint16_t destinationShort = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);

  uint16_t sourcePan = destinationPan;
  if (!panCompression) {
    if (length < static_cast<uint8_t>(offset + 2U)) {
      return false;
    }
    sourcePan = readLe16(&psdu[offset]);
    offset = static_cast<uint8_t>(offset + 2U);
  }

  if (length < static_cast<uint8_t>(offset + 2U)) {
    return false;
  }
  const uint16_t sourceShort = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);
  if (length < offset) {
    return false;
  }

  outView->valid = true;
  outView->ackRequested = ((frameControl >> 5U) & 0x1U) != 0U;
  outView->sequence = psdu[2];
  outView->panId = sourcePan;
  outView->destinationShort = destinationShort;
  outView->sourceShort = sourceShort;
  outView->payloadLength = static_cast<uint8_t>(length - offset);
  outView->payload = &psdu[offset];
  return true;
}

bool ZigbeeRadio::parseMacCommandFrameShort(const uint8_t* psdu, uint8_t length,
                                            ZigbeeMacCommandView* outView) {
  if (outView != nullptr) {
    memset(outView, 0, sizeof(*outView));
  }
  if (psdu == nullptr || outView == nullptr || length < 10U) {
    return false;
  }

  const uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  const bool panCompression = ((frameControl >> 6U) & 0x1U) != 0U;
  const uint8_t dstAddrMode = static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  const uint8_t srcAddrMode = static_cast<uint8_t>((frameControl >> 14U) & 0x03U);

  if (frameType != 0x03U || dstAddrMode != 0x02U || srcAddrMode != 0x02U) {
    return false;
  }

  uint8_t offset = 3U;
  if (length < static_cast<uint8_t>(offset + 2U + 2U)) {
    return false;
  }
  const uint16_t destinationPan = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);
  const uint16_t destinationShort = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);

  uint16_t sourcePan = destinationPan;
  if (!panCompression) {
    if (length < static_cast<uint8_t>(offset + 2U)) {
      return false;
    }
    sourcePan = readLe16(&psdu[offset]);
    offset = static_cast<uint8_t>(offset + 2U);
  }

  if (length < static_cast<uint8_t>(offset + 2U + 1U)) {
    return false;
  }
  const uint16_t sourceShort = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);
  const uint8_t commandId = psdu[offset];
  offset = static_cast<uint8_t>(offset + 1U);

  outView->valid = true;
  outView->ackRequested = ((frameControl >> 5U) & 0x1U) != 0U;
  outView->sequence = psdu[2];
  outView->panId = sourcePan;
  outView->destinationShort = destinationShort;
  outView->sourceShort = sourceShort;
  outView->commandId = commandId;
  outView->payloadLength = static_cast<uint8_t>(length - offset);
  outView->payload = &psdu[offset];
  return true;
}

BleRadio::BleRadio(uint32_t radioBase, uint32_t ficrBase)
    : radio_(reinterpret_cast<NRF_RADIO_Type*>(static_cast<uintptr_t>(radioBase))),
      ficr_(reinterpret_cast<NRF_FICR_Type*>(static_cast<uintptr_t>(ficrBase))),
      initialized_(false),
      addressType_(BleAddressType::kRandomStatic),
      pduType_(BleAdvPduType::kAdvInd),
      useChSel2_(true),
      externalAntenna_(false),
      address_{0},
      advData_{0},
      advDataLen_(0),
      extendedAdvData_{0},
      extendedAdvDataLen_(0),
      extendedScanRspData_{0},
      extendedScanRspDataLen_(0),
      extendedAdvSid_(0U),
      extendedAdvDid_(0U),
      extendedAdvAuxChannel_(20U),
      extendedSecondaryPacketCount_(0U),
      extendedScanRspPacketCount_(0U),
      scanRspData_{0},
      scanRspDataLen_(0),
      txPacket_{0},
      extendedPrimaryPacket_{0},
      extendedAuxAdvPacket_{0},
      extendedSecondaryPackets_{0},
      extendedScanRspPackets_{0},
      scanRspPacket_{0},
      rxPacket_{0},
      connectionTxPayload_{0},
      connected_(false),
      rfPathOwnedByBle_(false),
      connectionPeerAddress_{0},
      connectionPeerAddressRandom_(false),
      connectionAccessAddress_(0),
      connectionCrcInit_(0),
      connectionIntervalUnits_(0),
      connectionLatency_(0),
      connectionTimeoutUnits_(0),
      connectionChannelMap_{0},
      connectionChannelCount_(0),
      connectionHop_(0),
      connectionUseChSel2_(false),
      connectionChannelId_(0U),
      connectionSca_(0),
      connectionChanUse_(0),
      connectionExpectedRxSn_(0),
      connectionTxSn_(0),
      connectionLastTxSn_(0),
      connectionTxHistoryValid_(false),
      connectionEventCounter_(0),
      connectionMissedEventCount_(0U),
      connectionNextEventUs_(0),
      connectionFirstEventListenUs_(0),
      connectionSyncAttemptsRemaining_(0U),
      connectionAttMtu_(kBleDefaultAttMtu),
      connectionLastTxLlid_(0x01U),
      connectionLastTxLength_(0),
      connectionLastTxPlainLlid_(0x01U),
      connectionLastTxPlainLength_(0U),
      connectionLastTxPlainPayload_{0},
      connectionPendingTxLlid_(0x01U),
      connectionPendingTxLength_(0U),
      connectionPendingTxValid_(false),
      connectionPendingTxPayload_{0},
      connectionUpdatePending_(false),
      connectionUpdateInstant_(0U),
      connectionPendingWinSize_(0U),
      connectionPendingWinOffset_(0U),
      connectionPendingIntervalUnits_(0U),
      connectionPendingLatency_(0U),
      connectionPendingTimeoutUnits_(0U),
      connectionChannelMapPending_(false),
      connectionChannelMapInstant_(0U),
      connectionPendingChannelMap_{0},
      connectionPendingChannelCount_(0U),
      connectionServiceChangedIndicationsEnabled_(false),
      connectionServiceChangedIndicationPending_(false),
      connectionServiceChangedIndicationAwaitingConfirm_(false),
      connectionBatteryNotificationsEnabled_(false),
      connectionBatteryNotificationPending_(false),
      connectionPreparedWriteActive_(false),
      connectionPreparedWriteHandle_(0U),
      connectionPreparedWriteValue_{0},
      connectionPreparedWriteMask_(0U),
      lastDisconnectReason_(0U),
      lastDisconnectReasonValid_(false),
      lastDisconnectReasonRemote_(false),
      smpPairingState_(kSmpPairingStateIdle),
      smpPairingReq_{0},
      smpPairingRsp_{0},
      smpPeerConfirm_{0},
      smpPeerRandom_{0},
      smpLocalRandom_{0},
      smpLocalConfirm_{0},
      smpStk_{0},
      smpStkValid_(false),
      connectionEncSessionValid_(false),
      connectionEncRxEnabled_(false),
      connectionEncTxEnabled_(false),
      connectionEncStartReqPending_(false),
      connectionEncStartReqTxPending_(false),
      connectionEncAwaitingStartRsp_(false),
      connectionEncEnableTxOnNextEvent_(false),
      connectionEncRxCounter_(0ULL),
      connectionEncTxCounter_(0ULL),
      connectionEncKeyDerivationPending_(false),
      connectionEncSkd_{0},
      connectionEncSessionKey_{0},
      connectionEncSessionKeyAlt_{0},
      connectionEncAltKeyValid_(false),
      connectionEncRxDirection_(1U),
      connectionEncTxDirection_(0U),
      connectionEncIv_{0},
      connectionLastTxWasEncrypted_(false),
      connectionLastTxEncryptedLength_(0U),
      connectionLastTxEncryptedPayload_{0},
      connectionEncPrecomputedEmptyValid_(false),
      connectionEncPrecomputedCounter_(0ULL),
      connectionEncPrecomputedPayload_{0},
      connectionEncPrecomputedStartRspValid_(false),
      connectionEncPrecomputedStartRsp_{0},
      connectionEncPrecomputedStartRspTxValid_(false),
      connectionEncPrecomputedStartRspTxCounter_(0ULL),
      connectionEncPrecomputedStartRspTx_{0},
      bondLoadCallback_(nullptr),
      bondSaveCallback_(nullptr),
      bondClearCallback_(nullptr),
      bondCallbackContext_(nullptr),
      bondRecord_{},
      bondRecordValid_(false),
      bondStorageLoaded_(false),
      bondKeyPrimedForConnection_(false),
      traceCallback_(nullptr),
      traceCallbackContext_(nullptr),
      smpBondingRequested_(false),
      smpExpectInitiatorEncKey_(false),
      smpPeerLtkValid_(false),
      smpPeerLtkAwaitMasterId_(false),
      smpPeerLtk_{0},
      smpEncReqRand_{0},
      smpEncReqEdiv_(0U),
      smpKeySize_(16U),
      advCycleStartIndex_(0U),
      scanCycleStartIndex_(0U),
      gapDeviceName_{0},
      gapDeviceNameLen_(0),
      gapAppearance_(0),
      gapPpcpIntervalMin_(kBlePreferredConnIntervalMinUnits),
      gapPpcpIntervalMax_(kBlePreferredConnIntervalMaxUnits),
      gapPpcpLatency_(kBlePreferredConnLatency),
      gapPpcpTimeout_(kBlePreferredConnTimeoutUnits),
      gapBatteryLevel_(100U),
      customGattServices_{},
      customGattCharacteristics_{},
      customGattWriteHandlers_{},
      customGattServiceCount_(0U),
      customGattCharacteristicCount_(0U),
      customGattNextHandle_(kCustomGattHandleStart),
      connectionCustomNotificationPending_(false),
      connectionCustomPendingCharIndex_(0xFFU),
      connectionCustomPendingIndication_(false),
      connectionCustomIndicationAwaitingHandle_(0U),
      customGattWriteCallback_(nullptr),
      customGattWriteContext_(nullptr),
      encDebug_{} {}

bool BleRadio::ensureRfPathActiveForBle() {
  const bool rfPowerEnabled = BoardControl::rfSwitchPowerEnabled();
  const BoardAntennaPath currentPath = BoardControl::antennaPath();
  if (rfPowerEnabled && currentPath == g_boardAntennaPath) {
    return true;
  }
  if (!BoardControl::enableRfPath(g_boardAntennaPath)) {
    return false;
  }
  if (!rfPowerEnabled) {
    rfPathOwnedByBle_ = true;
  }
  return true;
}

void BleRadio::releaseRfPathForBle() {
  if (!rfPathOwnedByBle_) {
    return;
  }
  BoardControl::collapseRfPathIdle();
  rfPathOwnedByBle_ = false;
}

bool BleRadio::beginUnconnectedRadioActivity(uint32_t spinLimit) {
  if (connected_) {
    return true;
  }
  if (!ensureRfPathActiveForBle()) {
    return false;
  }
  if (ClockControl::startHfxo(true, spinLimit)) {
    return true;
  }
  releaseRfPathForBle();
  return false;
}

void BleRadio::endUnconnectedRadioActivity() {
  if (!connected_) {
    ClockControl::stopHfxo();
    releaseRfPathForBle();
  }
}

bool BleRadio::begin(int8_t txPowerDbm) {
  connected_ = false;
  advCycleStartIndex_ = 0U;
  scanCycleStartIndex_ = 0U;
  clearCustomGattConnectionState();

#if defined(NRF54L15_CLEAN_BLE_ENABLED) && (NRF54L15_CLEAN_BLE_ENABLED == 0)
  (void)txPowerDbm;
  initialized_ = false;
  return false;
#endif

  // Observe the current RF switch routing without overriding sketch-managed
  // GPIO state. Tools > Antenna still sets the startup default in initVariant().
  g_boardAntennaPath = BoardControl::antennaPath();
  externalAntenna_ = (g_boardAntennaPath == BoardAntennaPath::kExternal);
  initBleGrtc();

  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  if (!configureBle1M()) {
    endUnconnectedRadioActivity();
    return false;
  }
  if (!setTxPowerDbm(txPowerDbm)) {
    endUnconnectedRadioActivity();
    return false;
  }

  uint8_t fallbackAddress[6] = {0xC0, 0x54, 0x15, 0x00, 0x00, 0x00};
  if (!loadAddressFromFicr(true)) {
    if (!setDeviceAddress(fallbackAddress, BleAddressType::kRandomStatic)) {
      endUnconnectedRadioActivity();
      return false;
    }
  }

  if (advDataLen_ == 0U) {
    if (!setAdvertisingName("XIAO-nRF54L15", true)) {
      endUnconnectedRadioActivity();
      return false;
    }
  } else if (!buildAdvertisingPacket()) {
    endUnconnectedRadioActivity();
    return false;
  }

  if (scanRspDataLen_ == 0U) {
    if (!setScanResponseName("XIAO-nRF54L15")) {
      endUnconnectedRadioActivity();
      return false;
    }
  } else if (!buildScanResponsePacket()) {
    endUnconnectedRadioActivity();
    return false;
  }

  if (gapDeviceNameLen_ == 0U) {
    if (!setGattDeviceName("XIAO-nRF54L15")) {
      endUnconnectedRadioActivity();
      return false;
    }
  }

  loadBondRecordFromPersistence();
  initialized_ = true;
  endUnconnectedRadioActivity();
  return true;
}

void BleRadio::end() {
  if (radio_ == nullptr) {
    connected_ = false;
    initialized_ = false;
    clearCustomGattConnectionState();
    endUnconnectedRadioActivity();
    return;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitDisabled(120000UL);
  clearRadioCoreEvents(radio_);
  connected_ = false;
  initialized_ = false;
  clearCustomGattConnectionState();
  endUnconnectedRadioActivity();
}

bool BleRadio::setTxPowerDbm(int8_t dbm) {
  if (radio_ == nullptr) {
    return false;
  }
  const uint32_t regValue = txPowerRegFromDbm(dbm);
  radio_->TXPOWER = ((regValue << RADIO_TXPOWER_TXPOWER_Pos) &
                     RADIO_TXPOWER_TXPOWER_Msk);
  return true;
}

bool BleRadio::selectExternalAntenna(bool external) {
  if (!Gpio::configure(kPinRfSwitchCtl, GpioDirection::kOutput,
                       GpioPull::kDisabled)) {
    return false;
  }
  if (!Gpio::write(kPinRfSwitchCtl, external)) {
    return false;
  }

  externalAntenna_ = external;
  return true;
}

bool BleRadio::loadAddressFromFicr(bool forceRandomStatic) {
  if (ficr_ == nullptr) {
    return false;
  }

  const uint32_t lo = ficr_->DEVICEADDR[0];
  const uint32_t hi = ficr_->DEVICEADDR[1];

  uint8_t address[6];
  address[0] = static_cast<uint8_t>(lo & 0xFFU);
  address[1] = static_cast<uint8_t>((lo >> 8U) & 0xFFU);
  address[2] = static_cast<uint8_t>((lo >> 16U) & 0xFFU);
  address[3] = static_cast<uint8_t>((lo >> 24U) & 0xFFU);
  address[4] = static_cast<uint8_t>(hi & 0xFFU);
  address[5] = static_cast<uint8_t>((hi >> 8U) & 0xFFU);

  BleAddressType type = ((ficr_->DEVICEADDRTYPE & 0x1UL) != 0U)
                            ? BleAddressType::kRandomStatic
                            : BleAddressType::kPublic;
  if (forceRandomStatic) {
    type = BleAddressType::kRandomStatic;
  }

  return setDeviceAddress(address, type);
}

bool BleRadio::setDeviceAddress(const uint8_t address[6], BleAddressType type) {
  if (address == nullptr) {
    return false;
  }

  memcpy(address_, address, sizeof(address_));
  if (type == BleAddressType::kRandomStatic) {
    address_[5] = static_cast<uint8_t>((address_[5] & 0x3FU) | 0xC0U);
  }
  addressType_ = type;
  return buildAdvertisingPacket() && buildScanResponsePacket();
}

bool BleRadio::getDeviceAddress(uint8_t addressOut[6], BleAddressType* typeOut) const {
  if (addressOut == nullptr) {
    return false;
  }
  memcpy(addressOut, address_, sizeof(address_));
  if (typeOut != nullptr) {
    *typeOut = addressType_;
  }
  return true;
}

bool BleRadio::setAdvertisingPduType(BleAdvPduType type) {
  const uint8_t raw = static_cast<uint8_t>(type);
  if (raw > 0x0FU) {
    return false;
  }
  pduType_ = type;
  return buildAdvertisingPacket();
}

bool BleRadio::setAdvertisingChannelSelectionAlgorithm2(bool enabled) {
  useChSel2_ = enabled;
  return buildAdvertisingPacket();
}

bool BleRadio::setAdvertisingData(const uint8_t* data, size_t len) {
  if (len > sizeof(advData_)) {
    return false;
  }
  if (len > 0U && data == nullptr) {
    return false;
  }

  if (len > 0U) {
    memcpy(advData_, data, len);
  }
  advDataLen_ = len;
  return buildAdvertisingPacket();
}

bool BleRadio::setAdvertisingName(const char* name, bool includeFlags) {
  if (name == nullptr) {
    return false;
  }

  uint8_t payload[kBleLegacyAdDataMaxLength];
  size_t used = 0;

  if (includeFlags) {
    payload[used++] = 2U;
    payload[used++] = 0x01U;
    payload[used++] = 0x06U;
  }

  const size_t nameLen = strlen(name);
  if (used >= sizeof(payload)) {
    return false;
  }

  const size_t remaining = sizeof(payload) - used;
  if (remaining < 2U) {
    return false;
  }

  size_t copyLen = nameLen;
  uint8_t adType = 0x09U;  // Complete local name.
  if ((copyLen + 2U) > remaining) {
    copyLen = remaining - 2U;
    adType = 0x08U;  // Shortened local name.
  }

  payload[used++] = static_cast<uint8_t>(copyLen + 1U);
  payload[used++] = adType;
  if (copyLen > 0U) {
    memcpy(&payload[used], name, copyLen);
    used += copyLen;
  }
  const bool ok = setAdvertisingData(payload, used);
  if (ok && gapDeviceNameLen_ == 0U) {
    setGattDeviceName(name);
  }
  return ok;
}

bool BleRadio::setExtendedAdvertisingSid(uint8_t sid) {
  if (sid >= 16U) {
    return false;
  }
  extendedAdvSid_ = sid;
  return true;
}

bool BleRadio::setExtendedAdvertisingAuxChannel(uint8_t dataChannel) {
  if (dataChannel > 36U) {
    return false;
  }
  extendedAdvAuxChannel_ = dataChannel;
  return true;
}

bool BleRadio::setExtendedAdvertisingData(const uint8_t* data, size_t len) {
  if (len > sizeof(extendedAdvData_)) {
    return false;
  }
  if ((len > 0U) && (data == nullptr)) {
    return false;
  }

  if (len > 0U) {
    memcpy(extendedAdvData_, data, len);
  }
  extendedAdvDataLen_ = len;
  extendedAdvDid_ = static_cast<uint16_t>((extendedAdvDid_ + 1U) & 0x0FFFU);
  return true;
}

bool BleRadio::setExtendedAdvertisingName(const char* name, bool includeFlags) {
  if (name == nullptr) {
    return false;
  }

  uint8_t payload[kBleExtendedAdvDataMaxLength];
  size_t used = 0U;

  if (includeFlags) {
    payload[used++] = 2U;
    payload[used++] = 0x01U;
    payload[used++] = 0x06U;
  }

  const size_t nameLen = strlen(name);
  if (used >= sizeof(payload)) {
    return false;
  }

  const size_t remaining = sizeof(payload) - used;
  if (remaining < 2U) {
    return false;
  }

  size_t copyLen = nameLen;
  uint8_t adType = 0x09U;
  if ((copyLen + 2U) > remaining) {
    copyLen = remaining - 2U;
    adType = 0x08U;
  }

  payload[used++] = static_cast<uint8_t>(copyLen + 1U);
  payload[used++] = adType;
  if (copyLen > 0U) {
    memcpy(&payload[used], name, copyLen);
    used += copyLen;
  }

  const bool ok = setExtendedAdvertisingData(payload, used);
  if (ok && (gapDeviceNameLen_ == 0U)) {
    setGattDeviceName(name);
  }
  return ok;
}

bool BleRadio::setExtendedScanResponseData(const uint8_t* data, size_t len) {
  if (len > sizeof(extendedScanRspData_)) {
    return false;
  }
  if ((len > 0U) && (data == nullptr)) {
    return false;
  }

  if (len > 0U) {
    memcpy(extendedScanRspData_, data, len);
  }
  extendedScanRspDataLen_ = len;
  extendedAdvDid_ = static_cast<uint16_t>((extendedAdvDid_ + 1U) & 0x0FFFU);
  return true;
}

bool BleRadio::setExtendedScanResponseName(const char* name) {
  if (name == nullptr) {
    return false;
  }

  uint8_t payload[kBleExtendedAdvDataMaxLength];
  size_t used = 0U;
  size_t copyLen = strlen(name);
  uint8_t adType = 0x09U;
  if (copyLen > 254U) {
    copyLen = 254U;
    adType = 0x08U;
  }

  payload[used++] = static_cast<uint8_t>(copyLen + 1U);
  payload[used++] = adType;
  if (copyLen > 0U) {
    memcpy(&payload[used], name, copyLen);
    used += copyLen;
  }
  return setExtendedScanResponseData(payload, used);
}

bool BleRadio::setGattDeviceName(const char* name) {
  if (name == nullptr) {
    return false;
  }

  const size_t len = strlen(name);
  if (len > sizeof(gapDeviceName_)) {
    return false;
  }
  if (len > 0U) {
    memcpy(gapDeviceName_, name, len);
  }
  gapDeviceNameLen_ = static_cast<uint8_t>(len);
  return true;
}

bool BleRadio::setGattBatteryLevel(uint8_t percent) {
  if (percent > 100U) {
    return false;
  }
  if (gapBatteryLevel_ != percent) {
    gapBatteryLevel_ = percent;
    if (connected_ && connectionBatteryNotificationsEnabled_) {
      connectionBatteryNotificationPending_ = true;
    }
    return true;
  }
  gapBatteryLevel_ = percent;
  return true;
}

bool BleRadio::clearCustomGatt() {
  if (connected_) {
    return false;
  }
  memset(customGattServices_, 0, sizeof(customGattServices_));
  memset(customGattCharacteristics_, 0, sizeof(customGattCharacteristics_));
  memset(customGattWriteHandlers_, 0, sizeof(customGattWriteHandlers_));
  customGattServiceCount_ = 0U;
  customGattCharacteristicCount_ = 0U;
  customGattNextHandle_ = kCustomGattHandleStart;
  clearCustomGattConnectionState();
  return true;
}

BleRadio::BleCustomServiceState* BleRadio::findCustomServiceByHandle(
    uint16_t serviceHandle) {
  for (uint8_t i = 0U; i < customGattServiceCount_; ++i) {
    if (customGattServices_[i].serviceHandle == serviceHandle) {
      return &customGattServices_[i];
    }
  }
  return nullptr;
}

const BleRadio::BleCustomServiceState* BleRadio::findCustomServiceByHandle(
    uint16_t serviceHandle) const {
  for (uint8_t i = 0U; i < customGattServiceCount_; ++i) {
    if (customGattServices_[i].serviceHandle == serviceHandle) {
      return &customGattServices_[i];
    }
  }
  return nullptr;
}

BleRadio::BleCustomCharacteristicState*
BleRadio::findCustomCharacteristicByValueHandle(uint16_t valueHandle) {
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattCharacteristics_[i].valueHandle == valueHandle) {
      return &customGattCharacteristics_[i];
    }
  }
  return nullptr;
}

const BleRadio::BleCustomCharacteristicState*
BleRadio::findCustomCharacteristicByValueHandle(uint16_t valueHandle) const {
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattCharacteristics_[i].valueHandle == valueHandle) {
      return &customGattCharacteristics_[i];
    }
  }
  return nullptr;
}

BleRadio::BleCustomWriteHandlerState*
BleRadio::findCustomGattWriteHandler(uint16_t valueHandle) {
  if (valueHandle == 0U) {
    return nullptr;
  }
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattWriteHandlers_[i].valueHandle == valueHandle) {
      return &customGattWriteHandlers_[i];
    }
  }
  return nullptr;
}

const BleRadio::BleCustomWriteHandlerState*
BleRadio::findCustomGattWriteHandler(uint16_t valueHandle) const {
  if (valueHandle == 0U) {
    return nullptr;
  }
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattWriteHandlers_[i].valueHandle == valueHandle) {
      return &customGattWriteHandlers_[i];
    }
  }
  return nullptr;
}

BleRadio::BleCustomCharacteristicState*
BleRadio::findCustomCharacteristicByCccdHandle(uint16_t cccdHandle) {
  if (cccdHandle == 0U) {
    return nullptr;
  }
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattCharacteristics_[i].cccdHandle == cccdHandle) {
      return &customGattCharacteristics_[i];
    }
  }
  return nullptr;
}

const BleRadio::BleCustomCharacteristicState*
BleRadio::findCustomCharacteristicByCccdHandle(uint16_t cccdHandle) const {
  if (cccdHandle == 0U) {
    return nullptr;
  }
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattCharacteristics_[i].cccdHandle == cccdHandle) {
      return &customGattCharacteristics_[i];
    }
  }
  return nullptr;
}

bool BleRadio::addCustomGattService(uint16_t uuid16, uint16_t* outServiceHandle) {
  uint8_t uuidBytes[kBleUuidLength16] = {0U, 0U};
  writeLe16(uuidBytes, uuid16);
  return addCustomGattServiceUuid(uuidBytes, kBleUuidLength16, outServiceHandle);
}

bool BleRadio::addCustomGattService128(
    const uint8_t uuid128[kCustomGattUuid128Length], uint16_t* outServiceHandle) {
  return addCustomGattServiceUuid(uuid128, kBleUuidLength128, outServiceHandle);
}

bool BleRadio::addCustomGattServiceUuid(const uint8_t* uuidBytes, uint8_t uuidLength,
                                        uint16_t* outServiceHandle) {
  if (connected_ || uuidBytes == nullptr ||
      customGattServiceCount_ >= kCustomGattMaxServices) {
    return false;
  }
  if (uuidLength != kBleUuidLength16 && uuidLength != kBleUuidLength128) {
    return false;
  }
  if (customGattNextHandle_ > kCustomGattHandleEnd) {
    return false;
  }

  BleCustomServiceState& service = customGattServices_[customGattServiceCount_];
  memset(&service, 0, sizeof(service));
  if (uuidLength == kBleUuidLength16) {
    if (readLe16(uuidBytes) == 0U) {
      return false;
    }
    setCustomUuid16(&service.uuid.length, service.uuid.bytes, readLe16(uuidBytes));
  } else if (!setCustomUuid128(&service.uuid.length, service.uuid.bytes, uuidBytes)) {
    return false;
  }
  service.serviceHandle = customGattNextHandle_;
  service.endHandle = customGattNextHandle_;
  ++customGattNextHandle_;
  ++customGattServiceCount_;

  if (outServiceHandle != nullptr) {
    *outServiceHandle = service.serviceHandle;
  }
  return true;
}

bool BleRadio::addCustomGattCharacteristic(uint16_t serviceHandle, uint16_t uuid16,
                                           uint8_t properties,
                                           const uint8_t* initialValue,
                                           uint8_t initialValueLength,
                                           uint16_t* outValueHandle,
                                           uint16_t* outCccdHandle) {
  uint8_t uuidBytes[kBleUuidLength16] = {0U, 0U};
  writeLe16(uuidBytes, uuid16);
  return addCustomGattCharacteristicUuid(serviceHandle, uuidBytes, kBleUuidLength16,
                                         properties, initialValue,
                                         initialValueLength, outValueHandle,
                                         outCccdHandle);
}

bool BleRadio::addCustomGattCharacteristic128(
    uint16_t serviceHandle, const uint8_t uuid128[kCustomGattUuid128Length],
    uint8_t properties, const uint8_t* initialValue, uint8_t initialValueLength,
    uint16_t* outValueHandle, uint16_t* outCccdHandle) {
  return addCustomGattCharacteristicUuid(serviceHandle, uuid128,
                                         kBleUuidLength128, properties,
                                         initialValue, initialValueLength,
                                         outValueHandle, outCccdHandle);
}

bool BleRadio::addCustomGattCharacteristicUuid(uint16_t serviceHandle,
                                               const uint8_t* uuidBytes,
                                               uint8_t uuidLength,
                                               uint8_t properties,
                                               const uint8_t* initialValue,
                                               uint8_t initialValueLength,
                                               uint16_t* outValueHandle,
                                               uint16_t* outCccdHandle) {
  constexpr uint8_t kAllowedProps = static_cast<uint8_t>(
      kBleGattPropRead | kBleGattPropWriteNoRsp | kBleGattPropWrite |
      kBleGattPropNotify | kBleGattPropIndicate);
  if (connected_ || uuidBytes == nullptr ||
      customGattCharacteristicCount_ >= kCustomGattMaxCharacteristics ||
      (properties == 0U) || ((properties & ~kAllowedProps) != 0U)) {
    return false;
  }
  if (uuidLength != kBleUuidLength16 && uuidLength != kBleUuidLength128) {
    return false;
  }
  if (uuidLength == kBleUuidLength16 && readLe16(uuidBytes) == 0U) {
    return false;
  }
  if (initialValueLength > kCustomGattMaxValueLength ||
      (initialValueLength > 0U && initialValue == nullptr)) {
    return false;
  }

  uint8_t serviceIndex = 0xFFU;
  for (uint8_t i = 0U; i < customGattServiceCount_; ++i) {
    if (customGattServices_[i].serviceHandle == serviceHandle) {
      serviceIndex = i;
      break;
    }
  }
  if (serviceIndex == 0xFFU) {
    return false;
  }
  // Keep service ranges monotonic by only appending to the newest service.
  if (serviceIndex != static_cast<uint8_t>(customGattServiceCount_ - 1U)) {
    return false;
  }

  const bool hasCccd = ((properties & (kBleGattPropNotify | kBleGattPropIndicate)) != 0U);
  const uint16_t requiredHandles = hasCccd ? 3U : 2U;
  if (customGattNextHandle_ > kCustomGattHandleEnd ||
      (static_cast<uint16_t>(kCustomGattHandleEnd - customGattNextHandle_ + 1U) <
       requiredHandles)) {
    return false;
  }

  BleCustomCharacteristicState& characteristic =
      customGattCharacteristics_[customGattCharacteristicCount_];
  memset(&characteristic, 0, sizeof(characteristic));
  characteristic.serviceHandle = serviceHandle;
  if (uuidLength == kBleUuidLength16) {
    setCustomUuid16(&characteristic.uuid.length, characteristic.uuid.bytes,
                    readLe16(uuidBytes));
  } else if (!setCustomUuid128(&characteristic.uuid.length, characteristic.uuid.bytes,
                               uuidBytes)) {
    return false;
  }
  characteristic.properties = properties;
  characteristic.declarationHandle = customGattNextHandle_++;
  characteristic.valueHandle = customGattNextHandle_++;
  characteristic.cccdHandle = hasCccd ? customGattNextHandle_++ : 0U;
  characteristic.cccdValue = 0U;
  characteristic.valueLength = initialValueLength;
  if (initialValueLength > 0U) {
    memcpy(characteristic.value, initialValue, initialValueLength);
  }
  ++customGattCharacteristicCount_;

  BleCustomServiceState& service = customGattServices_[serviceIndex];
  service.endHandle = (characteristic.cccdHandle != 0U) ? characteristic.cccdHandle
                                                         : characteristic.valueHandle;

  if (outValueHandle != nullptr) {
    *outValueHandle = characteristic.valueHandle;
  }
  if (outCccdHandle != nullptr) {
    *outCccdHandle = characteristic.cccdHandle;
  }
  return true;
}

bool BleRadio::setCustomGattCharacteristicValue(uint16_t valueHandle,
                                                const uint8_t* value,
                                                uint8_t valueLength) {
  if (valueLength > kCustomGattMaxValueLength ||
      (valueLength > 0U && value == nullptr)) {
    return false;
  }

  BleCustomCharacteristicState* characteristic =
      findCustomCharacteristicByValueHandle(valueHandle);
  if (characteristic == nullptr) {
    return false;
  }
  characteristic->valueLength = valueLength;
  memset(characteristic->value, 0, sizeof(characteristic->value));
  if (valueLength > 0U) {
    memcpy(characteristic->value, value, valueLength);
  }
  return true;
}

bool BleRadio::getCustomGattCharacteristicValue(uint16_t valueHandle, uint8_t* outValue,
                                                uint8_t* inOutValueLength) const {
  if (outValue == nullptr || inOutValueLength == nullptr) {
    return false;
  }
  const BleCustomCharacteristicState* characteristic =
      findCustomCharacteristicByValueHandle(valueHandle);
  if (characteristic == nullptr) {
    return false;
  }
  if (*inOutValueLength < characteristic->valueLength) {
    *inOutValueLength = characteristic->valueLength;
    return false;
  }
  if (characteristic->valueLength > 0U) {
    memcpy(outValue, characteristic->value, characteristic->valueLength);
  }
  *inOutValueLength = characteristic->valueLength;
  return true;
}

bool BleRadio::notifyCustomGattCharacteristic(uint16_t valueHandle, bool indicate) {
  if (!connected_) {
    return false;
  }
  BleCustomCharacteristicState* characteristic =
      findCustomCharacteristicByValueHandle(valueHandle);
  if (characteristic == nullptr) {
    return false;
  }

  const bool notifyEnabled = ((characteristic->properties & kBleGattPropNotify) != 0U) &&
                             ((characteristic->cccdValue & 0x0001U) != 0U);
  const bool indicateEnabled =
      ((characteristic->properties & kBleGattPropIndicate) != 0U) &&
      ((characteristic->cccdValue & 0x0002U) != 0U);
  if (indicate) {
    if (!indicateEnabled || connectionCustomIndicationAwaitingHandle_ != 0U) {
      return false;
    }
  } else if (!notifyEnabled) {
    return false;
  }

  const ptrdiff_t idx = (characteristic - &customGattCharacteristics_[0]);
  if (idx < 0 ||
      static_cast<uint8_t>(idx) >= customGattCharacteristicCount_) {
    return false;
  }
  if (connectionCustomNotificationPending_ &&
      connectionCustomPendingCharIndex_ != static_cast<uint8_t>(idx)) {
    return false;
  }

  connectionCustomNotificationPending_ = true;
  connectionCustomPendingCharIndex_ = static_cast<uint8_t>(idx);
  connectionCustomPendingIndication_ = indicate;
  return true;
}

bool BleRadio::isCustomGattCccdEnabled(uint16_t valueHandle, bool indication) const {
  const BleCustomCharacteristicState* characteristic =
      findCustomCharacteristicByValueHandle(valueHandle);
  if (characteristic == nullptr || characteristic->cccdHandle == 0U) {
    return false;
  }
  const uint16_t mask = indication ? 0x0002U : 0x0001U;
  return ((characteristic->cccdValue & mask) != 0U);
}

bool BleRadio::setCustomGattWriteHandler(uint16_t valueHandle,
                                         BleGattWriteCallback callback,
                                         void* context) {
  if (findCustomCharacteristicByValueHandle(valueHandle) == nullptr) {
    return false;
  }

  BleCustomWriteHandlerState* existing = findCustomGattWriteHandler(valueHandle);
  if (existing != nullptr) {
    if (callback == nullptr) {
      memset(existing, 0, sizeof(*existing));
    } else {
      existing->callback = callback;
      existing->context = context;
    }
    return true;
  }

  if (callback == nullptr) {
    return true;
  }

  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattWriteHandlers_[i].valueHandle == 0U) {
      customGattWriteHandlers_[i].valueHandle = valueHandle;
      customGattWriteHandlers_[i].callback = callback;
      customGattWriteHandlers_[i].context = context;
      return true;
    }
  }
  return false;
}

void BleRadio::setCustomGattWriteCallback(BleGattWriteCallback callback, void* context) {
  customGattWriteCallback_ = callback;
  customGattWriteContext_ = context;
}

bool BleRadio::writeCustomGattCharacteristic(uint16_t handle, const uint8_t* value,
                                             uint16_t valueLength, bool withResponse,
                                             uint8_t* outErrorCode) {
  if (outErrorCode != nullptr) {
    *outErrorCode = kAttErrWriteNotPermitted;
  }

  BleCustomCharacteristicState* valueTarget =
      findCustomCharacteristicByValueHandle(handle);
  if (valueTarget != nullptr) {
    if ((valueLength > kCustomGattMaxValueLength) ||
        (valueLength > 0U && value == nullptr)) {
      if (outErrorCode != nullptr) {
        *outErrorCode = kAttErrInvalidAttrValueLen;
      }
      return false;
    }
    const bool allowWriteReq = ((valueTarget->properties & kBleGattPropWrite) != 0U);
    const bool allowWriteCmd =
        ((valueTarget->properties & kBleGattPropWriteNoRsp) != 0U);
    if ((withResponse && !allowWriteReq) || (!withResponse && !allowWriteCmd)) {
      if (outErrorCode != nullptr) {
        *outErrorCode = kAttErrWriteNotPermitted;
      }
      return false;
    }

    valueTarget->valueLength = static_cast<uint8_t>(valueLength);
    memset(valueTarget->value, 0, sizeof(valueTarget->value));
    if (valueLength > 0U) {
      memcpy(valueTarget->value, value, valueLength);
    }
    BleCustomWriteHandlerState* handler =
        findCustomGattWriteHandler(valueTarget->valueHandle);
    if (handler != nullptr && handler->callback != nullptr) {
      handler->callback(valueTarget->valueHandle, valueTarget->value,
                        valueTarget->valueLength, withResponse,
                        handler->context);
    } else if (customGattWriteCallback_ != nullptr) {
      customGattWriteCallback_(valueTarget->valueHandle, valueTarget->value,
                               valueTarget->valueLength, withResponse,
                               customGattWriteContext_);
    }
    return true;
  }

  BleCustomCharacteristicState* cccdTarget =
      findCustomCharacteristicByCccdHandle(handle);
  if (cccdTarget == nullptr) {
    if (outErrorCode != nullptr) {
      *outErrorCode = kAttErrWriteNotPermitted;
    }
    return false;
  }
  if (valueLength != 2U || value == nullptr) {
    if (outErrorCode != nullptr) {
      *outErrorCode = kAttErrInvalidAttrValueLen;
    }
    return false;
  }

  uint16_t allowedMask = 0U;
  if ((cccdTarget->properties & kBleGattPropNotify) != 0U) {
    allowedMask |= 0x0001U;
  }
  if ((cccdTarget->properties & kBleGattPropIndicate) != 0U) {
    allowedMask |= 0x0002U;
  }
  if (allowedMask == 0U) {
    if (outErrorCode != nullptr) {
      *outErrorCode = kAttErrWriteNotPermitted;
    }
    return false;
  }

  const uint16_t cccd = readLe16(value);
  if ((cccd & ~allowedMask) != 0U) {
    if (outErrorCode != nullptr) {
      *outErrorCode = kAttErrWriteNotPermitted;
    }
    return false;
  }
  cccdTarget->cccdValue = cccd;
  if ((cccdTarget->cccdValue & 0x0002U) == 0U &&
      connectionCustomIndicationAwaitingHandle_ == cccdTarget->valueHandle) {
    connectionCustomIndicationAwaitingHandle_ = 0U;
  }
  if (connectionCustomNotificationPending_) {
    const uint8_t idx = connectionCustomPendingCharIndex_;
    if (idx < customGattCharacteristicCount_ &&
        &customGattCharacteristics_[idx] == cccdTarget) {
      const bool pendingModeEnabled =
          connectionCustomPendingIndication_
              ? ((cccdTarget->cccdValue & 0x0002U) != 0U)
              : ((cccdTarget->cccdValue & 0x0001U) != 0U);
      if (!pendingModeEnabled) {
        connectionCustomNotificationPending_ = false;
        connectionCustomPendingCharIndex_ = 0xFFU;
        connectionCustomPendingIndication_ = false;
      }
    }
  }
  return true;
}

void BleRadio::clearCustomGattConnectionState() {
  connectionCustomNotificationPending_ = false;
  connectionCustomPendingCharIndex_ = 0xFFU;
  connectionCustomPendingIndication_ = false;
  connectionCustomIndicationAwaitingHandle_ = 0U;
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    customGattCharacteristics_[i].cccdValue = 0U;
  }
}

bool BleRadio::buildAdvertisingPacket() {
  const size_t payloadLen = sizeof(address_) + advDataLen_;
  if (payloadLen > kBleLegacyRawPayloadMaxLength) {
    return false;
  }

  uint8_t header = static_cast<uint8_t>(pduType_) & 0x0FU;
  if (useChSel2_) {
    header |= (1U << 5U);
  }
  if (addressType_ == BleAddressType::kRandomStatic) {
    header |= (1U << 6U);
  }

  txPacket_[0] = header;
  txPacket_[1] = static_cast<uint8_t>(payloadLen & 0x3FU);

  memcpy(&txPacket_[2], address_, sizeof(address_));
  if (advDataLen_ > 0U) {
    memcpy(&txPacket_[2 + sizeof(address_)], advData_, advDataLen_);
  }

  return true;
}

bool BleRadio::setScanResponseData(const uint8_t* data, size_t len) {
  if (len > sizeof(scanRspData_)) {
    return false;
  }
  if (len > 0U && data == nullptr) {
    return false;
  }

  if (len > 0U) {
    memcpy(scanRspData_, data, len);
  }
  scanRspDataLen_ = len;
  return buildScanResponsePacket();
}

bool BleRadio::setScanResponseName(const char* name) {
  if (name == nullptr) {
    return false;
  }

  uint8_t payload[kBleLegacyAdDataMaxLength];
  size_t used = 0;

  const size_t nameLen = strlen(name);
  if (nameLen + 2U > sizeof(payload)) {
    payload[used++] = static_cast<uint8_t>(sizeof(payload) - 1U);
    payload[used++] = 0x08U;  // Shortened local name.
    memcpy(&payload[used], name, sizeof(payload) - used);
    used = sizeof(payload);
  } else {
    payload[used++] = static_cast<uint8_t>(nameLen + 1U);
    payload[used++] = 0x09U;  // Complete local name.
    memcpy(&payload[used], name, nameLen);
    used += nameLen;
  }

  return setScanResponseData(payload, used);
}

bool BleRadio::buildScanResponsePacket() {
  const size_t payloadLen = sizeof(address_) + scanRspDataLen_;
  if (payloadLen > kBleLegacyRawPayloadMaxLength) {
    return false;
  }

  uint8_t header = kBlePduScanRsp;
  if (addressType_ == BleAddressType::kRandomStatic) {
    header |= (1U << 6U);
  }

  scanRspPacket_[0] = header;
  scanRspPacket_[1] = static_cast<uint8_t>(payloadLen & 0x3FU);
  memcpy(&scanRspPacket_[2], address_, sizeof(address_));
  if (scanRspDataLen_ > 0U) {
    memcpy(&scanRspPacket_[2 + sizeof(address_)], scanRspData_, scanRspDataLen_);
  }

  return true;
}

bool BleRadio::buildExtendedAdvertisingPackets(uint32_t auxOffsetUs,
                                               uint32_t* actualAuxOffsetUs) {
  uint32_t auxOffsetUnits =
      (auxOffsetUs + kBleExtendedAuxOffsetUnit30Us - 1U) /
      kBleExtendedAuxOffsetUnit30Us;
  if (auxOffsetUnits == 0U) {
    auxOffsetUnits = 1U;
  }
  if (auxOffsetUnits > kBleExtendedAuxOffsetMaxUnits) {
    return false;
  }

  const uint32_t encodedAuxOffsetUs =
      auxOffsetUnits * kBleExtendedAuxOffsetUnit30Us;
  if (actualAuxOffsetUs != nullptr) {
    *actualAuxOffsetUs = encodedAuxOffsetUs;
  }

  const uint8_t txAdd =
      (addressType_ == BleAddressType::kRandomStatic) ? (1U << 6U) : 0U;

  constexpr uint8_t kPrimaryExtHdrLen =
      static_cast<uint8_t>(1U + kBleLegacyAddressLength + 2U + 3U);
  constexpr uint8_t kPrimaryPayloadLen =
      static_cast<uint8_t>(1U + kPrimaryExtHdrLen);
  constexpr uint8_t kAuxExtHdrLenNoChain = static_cast<uint8_t>(1U + 2U);
  constexpr uint8_t kAuxExtHdrLenWithChain = static_cast<uint8_t>(1U + 2U + 3U);
  constexpr size_t kSecondaryDataCapacityNoChain = 255U - (1U + kAuxExtHdrLenNoChain);
  constexpr size_t kSecondaryDataCapacityWithChain = 255U - (1U + kAuxExtHdrLenWithChain);

  uint8_t secondaryPacketCount = 1U;
  size_t remainingForCount = extendedAdvDataLen_;
  while (remainingForCount > kSecondaryDataCapacityNoChain) {
    if (secondaryPacketCount >= kBleExtendedSecondaryPacketCountMax) {
      return false;
    }
    remainingForCount -= kSecondaryDataCapacityWithChain;
    ++secondaryPacketCount;
  }
  extendedSecondaryPacketCount_ = secondaryPacketCount;

  size_t offset = 0U;
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(kBlePduAdvExtInd | txAdd);
  extendedPrimaryPacket_[offset++] = kPrimaryPayloadLen;
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      (kBleExtendedAdvModeNonConnNonScan << 6U) | kPrimaryExtHdrLen);
  extendedPrimaryPacket_[offset++] = kBleExtendedPrimaryHeaderFlags;
  memcpy(&extendedPrimaryPacket_[offset], address_, sizeof(address_));
  offset += sizeof(address_);
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(extendedAdvDid_ & 0xFFU);
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      ((extendedAdvDid_ >> 8U) & 0x0FU) | (static_cast<uint16_t>(extendedAdvSid_) << 4U));
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      (extendedAdvAuxChannel_ & 0x3FU) |
      (kBleExtendedClockAccuracy500Ppm << 6U));
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(auxOffsetUnits & 0xFFU);
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      ((auxOffsetUnits >> 8U) & 0x1FU) | (kBleExtendedAuxPhy1M << 5U));

  size_t dataCursor = 0U;
  size_t remaining = extendedAdvDataLen_;
  for (uint8_t packetIndex = 0U; packetIndex < secondaryPacketCount; ++packetIndex) {
    const bool hasFollowingPacket = ((packetIndex + 1U) < secondaryPacketCount);
    const size_t dataLen = hasFollowingPacket ? kSecondaryDataCapacityWithChain : remaining;
    const uint8_t extHdrLen =
        hasFollowingPacket ? kAuxExtHdrLenWithChain : kAuxExtHdrLenNoChain;
    const uint8_t headerFlags =
        hasFollowingPacket ? static_cast<uint8_t>(kBleExtendedAuxHeaderFlags | 0x10U)
                           : kBleExtendedAuxHeaderFlags;
    const size_t payloadLen = 1U + extHdrLen + dataLen;
    uint8_t* packet = &extendedSecondaryPackets_[packetIndex][0];

    offset = 0U;
    packet[offset++] = kBlePduAdvExtInd;
    packet[offset++] = static_cast<uint8_t>(payloadLen);
    packet[offset++] = static_cast<uint8_t>(
        (kBleExtendedAdvModeNonConnNonScan << 6U) | extHdrLen);
    packet[offset++] = headerFlags;
    packet[offset++] = static_cast<uint8_t>(extendedAdvDid_ & 0xFFU);
    packet[offset++] = static_cast<uint8_t>(
        ((extendedAdvDid_ >> 8U) & 0x0FU) | (static_cast<uint16_t>(extendedAdvSid_) << 4U));

    if (hasFollowingPacket) {
      packet[offset++] = static_cast<uint8_t>(
          (extendedAdvAuxChannel_ & 0x3FU) |
          (kBleExtendedClockAccuracy500Ppm << 6U));
      packet[offset++] = static_cast<uint8_t>(auxOffsetUnits & 0xFFU);
      packet[offset++] = static_cast<uint8_t>(
          ((auxOffsetUnits >> 8U) & 0x1FU) | (kBleExtendedAuxPhy1M << 5U));
    }

    if (dataLen > 0U) {
      memcpy(&packet[offset], &extendedAdvData_[dataCursor], dataLen);
    }
    dataCursor += dataLen;
    remaining -= dataLen;
  }

  return true;
}

bool BleRadio::buildExtendedScannableAdvertisingPackets(
    uint32_t auxOffsetUs,
    uint32_t* actualAuxOffsetUs,
    uint32_t* actualScanRspChainOffsetUs) {
  if (extendedScanRspDataLen_ == 0U) {
    return false;
  }

  uint32_t auxOffsetUnits =
      (auxOffsetUs + kBleExtendedAuxOffsetUnit30Us - 1U) /
      kBleExtendedAuxOffsetUnit30Us;
  if (auxOffsetUnits == 0U) {
    auxOffsetUnits = 1U;
  }
  if (auxOffsetUnits > kBleExtendedAuxOffsetMaxUnits) {
    return false;
  }

  const uint32_t encodedAuxOffsetUs =
      auxOffsetUnits * kBleExtendedAuxOffsetUnit30Us;
  if (actualAuxOffsetUs != nullptr) {
    *actualAuxOffsetUs = encodedAuxOffsetUs;
  }
  if (actualScanRspChainOffsetUs != nullptr) {
    *actualScanRspChainOffsetUs = encodedAuxOffsetUs;
  }

  const uint8_t txAdd =
      (addressType_ == BleAddressType::kRandomStatic) ? (1U << 6U) : 0U;

  constexpr uint8_t kScannablePrimaryHeaderFlags = 0x18U;   // ADI + AuxPtr
  constexpr uint8_t kScannableAuxAdvHeaderFlags = 0x09U;    // AdvA + ADI
  constexpr uint8_t kScannableScanRspHeaderFlags = 0x09U;   // AdvA + ADI
  constexpr uint8_t kChainHeaderFlags = kBleExtendedAuxHeaderFlags;
  constexpr uint8_t kPrimaryExtHdrLen = static_cast<uint8_t>(1U + 2U + 3U);
  constexpr uint8_t kPrimaryPayloadLen = static_cast<uint8_t>(1U + kPrimaryExtHdrLen);
  constexpr uint8_t kAuxAdvExtHdrLen = static_cast<uint8_t>(1U + 6U + 2U);
  constexpr uint8_t kAuxAdvPayloadLen = static_cast<uint8_t>(1U + kAuxAdvExtHdrLen);
  constexpr uint8_t kScanRspExtHdrLenNoChain = static_cast<uint8_t>(1U + 6U + 2U);
  constexpr uint8_t kScanRspExtHdrLenWithChain =
      static_cast<uint8_t>(1U + 6U + 2U + 3U);
  constexpr size_t kScanRspDataCapacityNoChain =
      255U - (1U + kScanRspExtHdrLenNoChain);
  constexpr size_t kScanRspDataCapacityWithChain =
      255U - (1U + kScanRspExtHdrLenWithChain);
  constexpr uint8_t kChainExtHdrLenNoChain = static_cast<uint8_t>(1U + 2U);
  constexpr uint8_t kChainExtHdrLenWithChain = static_cast<uint8_t>(1U + 2U + 3U);
  constexpr size_t kChainDataCapacityNoChain =
      255U - (1U + kChainExtHdrLenNoChain);
  constexpr size_t kChainDataCapacityWithChain =
      255U - (1U + kChainExtHdrLenWithChain);

  size_t offset = 0U;
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(kBlePduAdvExtInd | txAdd);
  extendedPrimaryPacket_[offset++] = kPrimaryPayloadLen;
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      (kBleExtendedAdvModeNonConnScan << 6U) | kPrimaryExtHdrLen);
  extendedPrimaryPacket_[offset++] = kScannablePrimaryHeaderFlags;
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(extendedAdvDid_ & 0xFFU);
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      ((extendedAdvDid_ >> 8U) & 0x0FU) | (static_cast<uint16_t>(extendedAdvSid_) << 4U));
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      (extendedAdvAuxChannel_ & 0x3FU) |
      (kBleExtendedClockAccuracy500Ppm << 6U));
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(auxOffsetUnits & 0xFFU);
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      ((auxOffsetUnits >> 8U) & 0x1FU) | (kBleExtendedAuxPhy1M << 5U));

  offset = 0U;
  extendedAuxAdvPacket_[offset++] = static_cast<uint8_t>(kBlePduAdvExtInd | txAdd);
  extendedAuxAdvPacket_[offset++] = kAuxAdvPayloadLen;
  extendedAuxAdvPacket_[offset++] = static_cast<uint8_t>(
      (kBleExtendedAdvModeNonConnScan << 6U) | kAuxAdvExtHdrLen);
  extendedAuxAdvPacket_[offset++] = kScannableAuxAdvHeaderFlags;
  memcpy(&extendedAuxAdvPacket_[offset], address_, sizeof(address_));
  offset += sizeof(address_);
  extendedAuxAdvPacket_[offset++] = static_cast<uint8_t>(extendedAdvDid_ & 0xFFU);
  extendedAuxAdvPacket_[offset++] = static_cast<uint8_t>(
      ((extendedAdvDid_ >> 8U) & 0x0FU) | (static_cast<uint16_t>(extendedAdvSid_) << 4U));

  extendedScanRspPacketCount_ = 0U;
  size_t remaining = extendedScanRspDataLen_;
  size_t dataCursor = 0U;
  bool firstPacket = true;
  while (remaining > 0U) {
    if (extendedScanRspPacketCount_ >= kBleExtendedSecondaryPacketCountMax) {
      return false;
    }

    const bool useFirstPacket = firstPacket;
    const size_t capacityNoChain =
        useFirstPacket ? kScanRspDataCapacityNoChain : kChainDataCapacityNoChain;
    const size_t capacityWithChain =
        useFirstPacket ? kScanRspDataCapacityWithChain : kChainDataCapacityWithChain;
    const bool hasFollowingPacket = (remaining > capacityNoChain);
    const size_t dataLen = hasFollowingPacket ? capacityWithChain : remaining;
    uint8_t* packet =
        &extendedScanRspPackets_[extendedScanRspPacketCount_][0];

    offset = 0U;
    packet[offset++] =
        useFirstPacket ? static_cast<uint8_t>(kBlePduAdvExtInd | txAdd) : kBlePduAdvExtInd;
    if (useFirstPacket) {
      const uint8_t extHdrLen = hasFollowingPacket ? kScanRspExtHdrLenWithChain
                                                   : kScanRspExtHdrLenNoChain;
      const uint8_t headerFlags = hasFollowingPacket
                                      ? static_cast<uint8_t>(kScannableScanRspHeaderFlags |
                                                             0x10U)
                                      : kScannableScanRspHeaderFlags;
      packet[offset++] = static_cast<uint8_t>(1U + extHdrLen + dataLen);
      packet[offset++] = static_cast<uint8_t>(
          (kBleExtendedAdvModeNonConnNonScan << 6U) | extHdrLen);
      packet[offset++] = headerFlags;
      memcpy(&packet[offset], address_, sizeof(address_));
      offset += sizeof(address_);
      packet[offset++] = static_cast<uint8_t>(extendedAdvDid_ & 0xFFU);
      packet[offset++] = static_cast<uint8_t>(
          ((extendedAdvDid_ >> 8U) & 0x0FU) |
          (static_cast<uint16_t>(extendedAdvSid_) << 4U));
      if (hasFollowingPacket) {
        packet[offset++] = static_cast<uint8_t>(
            (extendedAdvAuxChannel_ & 0x3FU) |
            (kBleExtendedClockAccuracy500Ppm << 6U));
        packet[offset++] = static_cast<uint8_t>(auxOffsetUnits & 0xFFU);
        packet[offset++] = static_cast<uint8_t>(
            ((auxOffsetUnits >> 8U) & 0x1FU) | (kBleExtendedAuxPhy1M << 5U));
      }
    } else {
      const uint8_t extHdrLen =
          hasFollowingPacket ? kChainExtHdrLenWithChain : kChainExtHdrLenNoChain;
      const uint8_t headerFlags = hasFollowingPacket
                                      ? static_cast<uint8_t>(kChainHeaderFlags | 0x10U)
                                      : kChainHeaderFlags;
      packet[offset++] = static_cast<uint8_t>(1U + extHdrLen + dataLen);
      packet[offset++] = static_cast<uint8_t>(
          (kBleExtendedAdvModeNonConnNonScan << 6U) | extHdrLen);
      packet[offset++] = headerFlags;
      packet[offset++] = static_cast<uint8_t>(extendedAdvDid_ & 0xFFU);
      packet[offset++] = static_cast<uint8_t>(
          ((extendedAdvDid_ >> 8U) & 0x0FU) |
          (static_cast<uint16_t>(extendedAdvSid_) << 4U));
      if (hasFollowingPacket) {
        packet[offset++] = static_cast<uint8_t>(
            (extendedAdvAuxChannel_ & 0x3FU) |
            (kBleExtendedClockAccuracy500Ppm << 6U));
        packet[offset++] = static_cast<uint8_t>(auxOffsetUnits & 0xFFU);
        packet[offset++] = static_cast<uint8_t>(
            ((auxOffsetUnits >> 8U) & 0x1FU) | (kBleExtendedAuxPhy1M << 5U));
      }
    }

    memcpy(&packet[offset], &extendedScanRspData_[dataCursor], dataLen);
    dataCursor += dataLen;
    remaining -= dataLen;
    ++extendedScanRspPacketCount_;
    firstPacket = false;
  }

  return (extendedScanRspPacketCount_ > 0U);
}

bool BleRadio::transmitPreparedPacketOnCurrentChannel(const uint8_t* packet,
                                                      uint32_t spinLimit,
                                                      uint32_t* txReadyUs) {
  if (!initialized_ || radio_ == nullptr || connected_ || packet == nullptr) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  radio_->PACKETPTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(packet));
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;

  bool readySeen = false;
  bool disabledSeen = false;
  uint32_t remaining = spinLimit;
  while (remaining-- > 0U) {
    if (!readySeen && (radio_->EVENTS_READY != 0U)) {
      readySeen = true;
      if (txReadyUs != nullptr) {
        *txReadyUs = bleTimingUs();
      }
    }

    if (radio_->EVENTS_DISABLED != 0U) {
      disabledSeen = true;
      break;
    }

    const uint32_t state =
        (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (state == RADIO_STATE_STATE_Disabled) {
      disabledSeen = true;
      break;
    }
  }

  if (!disabledSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
  }

  radio_->SHORTS = 0U;
  clearRadioCoreEvents(radio_);
  return disabledSeen;
}

bool BleRadio::advertiseOncePrepared(BleAdvertisingChannel channel,
                                     uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || connected_) {
    return false;
  }
  if (!setAdvertisingChannel(channel)) {
    return false;
  }
  return transmitPreparedPacketOnCurrentChannel(&txPacket_[0], spinLimit);
}

bool BleRadio::advertiseOnce(BleAdvertisingChannel channel, uint32_t spinLimit) {
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  const bool ok = advertiseOncePrepared(channel, spinLimit);
  endUnconnectedRadioActivity();
  return ok;
}

bool BleRadio::advertiseEvent(uint32_t interChannelDelayUs, uint32_t spinLimit) {
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  // Add spec-compliant random delay (0-10ms) to prevent persistent collisions.
  {
    static uint32_t seed = 0U;
    if (seed == 0U) {
      seed = micros() ^ (static_cast<uint32_t>(address_[0]) << 24U);
    }
    seed = seed * 1103515245U + 12345U;
    const uint32_t advDelayUs = (seed % 10001U);
    if (advDelayUs > 0U) {
      delayMicroseconds(advDelayUs);
    }
  }

  if (!advertiseOncePrepared(BleAdvertisingChannel::k37, spinLimit)) {
    endUnconnectedRadioActivity();
    return false;
  }
  if (interChannelDelayUs > 0U) {
    delayMicroseconds(interChannelDelayUs);
  }

  if (!advertiseOncePrepared(BleAdvertisingChannel::k38, spinLimit)) {
    endUnconnectedRadioActivity();
    return false;
  }
  if (interChannelDelayUs > 0U) {
    delayMicroseconds(interChannelDelayUs);
  }

  const bool ok = advertiseOncePrepared(BleAdvertisingChannel::k39, spinLimit);
  endUnconnectedRadioActivity();
  return ok;
}

bool BleRadio::advertiseExtendedEvent(uint32_t auxOffsetUs,
                                      uint32_t interPrimaryDelayUs,
                                      uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || connected_) {
    return false;
  }
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  uint32_t actualAuxOffsetUs = 0U;
  if (!buildExtendedAdvertisingPackets(auxOffsetUs, &actualAuxOffsetUs)) {
    endUnconnectedRadioActivity();
    return false;
  }

  {
    static uint32_t seed = 0U;
    if (seed == 0U) {
      seed = micros() ^ (static_cast<uint32_t>(address_[0]) << 24U);
    }
    seed = seed * 1103515245U + 12345U;
    const uint32_t advDelayUs = (seed % 10001U);
    if (advDelayUs > 0U) {
      delayMicroseconds(advDelayUs);
    }
  }

  const BleAdvertisingChannel channels[] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };
  const uint8_t startIndex = static_cast<uint8_t>(advCycleStartIndex_ % 3U);

  for (size_t i = 0U; i < 3U; ++i) {
    const uint8_t idx = static_cast<uint8_t>((startIndex + i) % 3U);
    if (!setAdvertisingChannel(channels[idx])) {
      endUnconnectedRadioActivity();
      return false;
    }

    uint32_t primaryReadyUs = 0U;
    if (!transmitPreparedPacketOnCurrentChannel(&extendedPrimaryPacket_[0], spinLimit,
                                                &primaryReadyUs)) {
      endUnconnectedRadioActivity();
      return false;
    }

    if (!setDataChannel(extendedAdvAuxChannel_)) {
      endUnconnectedRadioActivity();
      return false;
    }

    const uint32_t auxTargetUs = primaryReadyUs + actualAuxOffsetUs;
    uint32_t nowUs = bleTimingUs();
    while (!timeReachedUs(nowUs, auxTargetUs)) {
      nowUs = bleTimingUs();
    }

    uint32_t secondaryReadyUs = 0U;
    for (uint8_t secondaryIndex = 0U;
         secondaryIndex < extendedSecondaryPacketCount_;
         ++secondaryIndex) {
      if (!transmitPreparedPacketOnCurrentChannel(
              &extendedSecondaryPackets_[secondaryIndex][0],
              spinLimit,
              ((secondaryIndex + 1U) < extendedSecondaryPacketCount_) ? &secondaryReadyUs
                                                                      : nullptr)) {
        endUnconnectedRadioActivity();
        return false;
      }

      if ((secondaryIndex + 1U) < extendedSecondaryPacketCount_) {
        const uint32_t nextTargetUs = secondaryReadyUs + actualAuxOffsetUs;
        nowUs = bleTimingUs();
        while (!timeReachedUs(nowUs, nextTargetUs)) {
          nowUs = bleTimingUs();
        }
      }
    }

    if (((i + 1U) < 3U) && (interPrimaryDelayUs > 0U)) {
      delayMicroseconds(interPrimaryDelayUs);
    }
  }

  advCycleStartIndex_ = static_cast<uint8_t>((startIndex + 1U) % 3U);
  endUnconnectedRadioActivity();
  return true;
}

bool BleRadio::advertiseExtendedScannableEvent(uint32_t auxOffsetUs,
                                               uint32_t interPrimaryDelayUs,
                                               uint32_t requestListenSpinLimit,
                                               uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || connected_) {
    return false;
  }
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  uint32_t actualAuxOffsetUs = 0U;
  uint32_t actualScanRspChainOffsetUs = 0U;
  if (!buildExtendedScannableAdvertisingPackets(auxOffsetUs, &actualAuxOffsetUs,
                                                &actualScanRspChainOffsetUs)) {
    endUnconnectedRadioActivity();
    return false;
  }

  {
    static uint32_t seed = 0U;
    if (seed == 0U) {
      seed = micros() ^ (static_cast<uint32_t>(address_[0]) << 24U);
    }
    seed = seed * 1103515245U + 12345U;
    const uint32_t advDelayUs = (seed % 10001U);
    if (advDelayUs > 0U) {
      delayMicroseconds(advDelayUs);
    }
  }

  const BleAdvertisingChannel channels[] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };
  const uint8_t startIndex = static_cast<uint8_t>(advCycleStartIndex_ % 3U);

  for (size_t i = 0U; i < 3U; ++i) {
    const uint8_t idx = static_cast<uint8_t>((startIndex + i) % 3U);
    if (!setAdvertisingChannel(channels[idx])) {
      endUnconnectedRadioActivity();
      return false;
    }

    uint32_t primaryReadyUs = 0U;
    if (!transmitPreparedPacketOnCurrentChannel(&extendedPrimaryPacket_[0], spinLimit,
                                                &primaryReadyUs)) {
      endUnconnectedRadioActivity();
      return false;
    }

    if (!setDataChannel(extendedAdvAuxChannel_)) {
      endUnconnectedRadioActivity();
      return false;
    }

    const uint32_t auxTargetUs = primaryReadyUs + actualAuxOffsetUs;
    uint32_t nowUs = bleTimingUs();
    while (!timeReachedUs(nowUs, auxTargetUs)) {
      nowUs = bleTimingUs();
    }

    if (!handleExtendedScanRequestAndMaybeRespond(requestListenSpinLimit,
                                                  actualScanRspChainOffsetUs,
                                                  spinLimit)) {
      endUnconnectedRadioActivity();
      return false;
    }

    if (((i + 1U) < 3U) && (interPrimaryDelayUs > 0U)) {
      delayMicroseconds(interPrimaryDelayUs);
    }
  }

  advCycleStartIndex_ = static_cast<uint8_t>((startIndex + 1U) % 3U);
  endUnconnectedRadioActivity();
  return true;
}

bool BleRadio::handleExtendedScanRequestAndMaybeRespond(
    uint32_t requestListenSpinLimit,
    uint32_t scanRspChainOffsetUs,
    uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || connected_) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  memset(rxPacket_, 0, sizeof(rxPacket_));
  memcpy(rxPacket_, extendedAuxAdvPacket_, sizeof(extendedAuxAdvPacket_));
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk) |
      ((RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos) &
       RADIO_SHORTS_DISABLED_RXEN_Msk) |
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool txEndSeen = waitForEnd(spinLimit);
  if (!txEndSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return false;
  }

  bool txDisabled = waitDisabled(spinLimit / 2U + 1U);
  if (!txDisabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    txDisabled = waitDisabled(spinLimit / 2U + 1U);
  }
  if (!txDisabled) {
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return false;
  }

  radio_->EVENTS_DISABLED = 0U;
  radio_->EVENTS_END = 0U;
  radio_->EVENTS_PHYEND = 0U;
  radio_->EVENTS_CRCOK = 0U;
  radio_->EVENTS_CRCERROR = 0U;
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  const uint32_t state =
      (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  if (state == RADIO_STATE_STATE_Disabled) {
    radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  }

  const bool endSeen = waitRadioEndBudgeted(
      radio_, kBleAdvRequestListenMaxUs, requestListenSpinLimit);
  if (!endSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return true;
  }
  const uint32_t rxEndUs = bleTimingUs();

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOk = (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  const uint8_t hdr = rxPacket_[0];
  const uint8_t pduType = static_cast<uint8_t>(hdr & 0x0FU);
  const uint8_t length = static_cast<uint8_t>(rxPacket_[1] & 0x3FU);
  const bool rxAddrRandom = ((hdr >> 7U) & 0x1U) != 0U;
  const uint8_t* payload = &rxPacket_[2];

  bool disabled = waitDisabled(spinLimit / 2U + 1U);
  if (!disabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    disabled = waitDisabled(spinLimit / 2U + 1U);
  }
  radio_->SHORTS = 0U;
  if (!disabled) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  bool hasScanReq = false;
  if (crcOk && (length >= 12U) && (pduType == kBlePduScanReq)) {
    const uint8_t* advertiserAddress = &payload[6];
    hasScanReq = bleAddressEqual(advertiserAddress, &address_[0]) &&
                 (rxAddrRandom ==
                  (addressType_ == BleAddressType::kRandomStatic));
  }

  if (!hasScanReq || (extendedScanRspPacketCount_ == 0U)) {
    clearRadioCoreEvents(radio_);
    return true;
  }

  clearRadioCoreEvents(radio_);
  radio_->PACKETPTR = static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(&extendedScanRspPackets_[0][0]));
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  const uint32_t txTriggerTargetUs = rxEndUs + kBleConnTxenAfterRxUs;
  uint32_t txTriggerNowUs = bleTimingUs();
  while (!timeReachedUs(txTriggerNowUs, txTriggerTargetUs)) {
    txTriggerNowUs = bleTimingUs();
  }

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  uint32_t scanRspReadyUs = 0U;
  bool readySeen = false;
  bool responseDisabled = false;
  uint32_t remaining = spinLimit;
  while (remaining-- > 0U) {
    if (!readySeen && (radio_->EVENTS_READY != 0U)) {
      readySeen = true;
      scanRspReadyUs = bleTimingUs();
    }
    if (radio_->EVENTS_DISABLED != 0U) {
      responseDisabled = true;
      break;
    }

    const uint32_t currentState =
        (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (currentState == RADIO_STATE_STATE_Disabled) {
      responseDisabled = true;
      break;
    }
  }
  if (!responseDisabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    responseDisabled = waitDisabled(spinLimit / 2U + 1U);
  }
  radio_->SHORTS = 0U;
  if (!responseDisabled || !readySeen) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  uint32_t previousPacketStartUs = scanRspReadyUs;
  for (uint8_t packetIndex = 1U; packetIndex < extendedScanRspPacketCount_;
       ++packetIndex) {
    const uint32_t nextTargetUs = previousPacketStartUs + scanRspChainOffsetUs;
    uint32_t nowUs = bleTimingUs();
    while (!timeReachedUs(nowUs, nextTargetUs)) {
      nowUs = bleTimingUs();
    }

    uint32_t nextReadyUs = 0U;
    if (!transmitPreparedPacketOnCurrentChannel(
            &extendedScanRspPackets_[packetIndex][0], spinLimit, &nextReadyUs)) {
      clearRadioCoreEvents(radio_);
      return false;
    }
    previousPacketStartUs = nextReadyUs;
  }

  clearRadioCoreEvents(radio_);
  return true;
}

bool BleRadio::advertiseInteractOncePrepared(BleAdvertisingChannel channel,
                                             BleAdvInteraction* interaction,
                                             uint32_t requestListenSpinLimit,
                                             uint32_t spinLimit) {
  if (interaction != nullptr) {
    interaction->channel = channel;
    interaction->receivedScanRequest = false;
    interaction->scanResponseTransmitted = false;
    interaction->receivedConnectInd = false;
    interaction->connectIndChSel2 = false;
    interaction->peerAddressRandom = false;
    interaction->rssiDbm = 0;
    memset(interaction->peerAddress, 0, sizeof(interaction->peerAddress));
  }

  if (!initialized_ || radio_ == nullptr || connected_) {
    return false;
  }
  if (!setAdvertisingChannel(channel)) {
    return false;
  }

  // Keep TX->RX turnaround in hardware to catch requests sent at T_IFS timing.
  clearRadioCoreEvents(radio_);
  memset(rxPacket_, 0, sizeof(rxPacket_));
  {
    const size_t copyLen =
        (sizeof(rxPacket_) < sizeof(txPacket_)) ? sizeof(rxPacket_) : sizeof(txPacket_);
    memcpy(rxPacket_, txPacket_, copyLen);
  }
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk) |
      ((RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos) &
       RADIO_SHORTS_DISABLED_RXEN_Msk) |
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool txEndSeen = waitForEnd(spinLimit);
  if (!txEndSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return false;
  }

  bool txDisabled = waitDisabled(spinLimit / 2U + 1U);
  if (!txDisabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    txDisabled = waitDisabled(spinLimit / 2U + 1U);
  }
  if (!txDisabled) {
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return false;
  }

  // Clear TX completion events and remove DISABLED->RX shortcut so RX disable
  // completes cleanly once the listen window ends.
  radio_->EVENTS_DISABLED = 0U;
  radio_->EVENTS_END = 0U;
  radio_->EVENTS_PHYEND = 0U;
  radio_->EVENTS_CRCOK = 0U;
  radio_->EVENTS_CRCERROR = 0U;
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  // Defensive fallback: if shortcut path did not enter RX, arm RX manually.
  const uint32_t state =
      (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  if (state == RADIO_STATE_STATE_Disabled) {
    radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  }

  const bool endSeen = waitRadioEndBudgeted(
      radio_, kBleAdvRequestListenMaxUs, requestListenSpinLimit);
  if (!endSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return true;
  }
  const uint32_t rxEndUs = bleTimingUs();

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOk = (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  const uint8_t hdr = rxPacket_[0];
  const uint8_t pduType = static_cast<uint8_t>(hdr & 0x0FU);
  const bool pduChSel2 = ((hdr >> 5U) & 0x1U) != 0U;
  const uint8_t length = static_cast<uint8_t>(rxPacket_[1] & 0x3FU);
  const bool txAddrRandom = ((hdr >> 6U) & 0x1U) != 0U;
  const bool rxAddrRandom = ((hdr >> 7U) & 0x1U) != 0U;
  const uint8_t* payload = &rxPacket_[2];

  bool disabled = waitDisabled(spinLimit / 2U + 1U);
  if (!disabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    disabled = waitDisabled(spinLimit / 2U + 1U);
  }
  radio_->SHORTS = 0U;
  if (!disabled) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  bool hasScanReq = false;
  bool hasConnectInd = false;
  bool connectAccepted = false;
  bool addressMatch = false;
  const bool connectable = (pduType_ == BleAdvPduType::kAdvInd);
  if (crcOk && length >= 12U) {
    const uint8_t* scannerOrInitiator = payload;
    const uint8_t* advertiserAddress = &payload[6];
    addressMatch = bleAddressEqual(advertiserAddress, &address_[0]) &&
                   (rxAddrRandom ==
                    (addressType_ == BleAddressType::kRandomStatic));

    if (addressMatch && pduType == kBlePduScanReq) {
      hasScanReq = true;
      if (interaction != nullptr) {
        memcpy(&interaction->peerAddress[0], scannerOrInitiator, 6U);
      }
    } else if (addressMatch && pduType == kBlePduConnectInd && connectable &&
               (length >= 34U)) {
      hasConnectInd = true;
      if (interaction != nullptr) {
        memcpy(&interaction->peerAddress[0], scannerOrInitiator, 6U);
      }
      connectAccepted = startConnectionFromConnectInd(payload, length, txAddrRandom,
                                                      pduChSel2, rxEndUs);
    }
  }

  if (interaction != nullptr) {
    interaction->channel = channel;
    interaction->rssiDbm = radioRssiDbm(radio_);
    interaction->peerAddressRandom = txAddrRandom;
    interaction->receivedScanRequest = hasScanReq;
    interaction->receivedConnectInd = hasConnectInd;
    interaction->connectIndChSel2 = hasConnectInd && useChSel2_ && pduChSel2;
  }

  bool txScanRspOk = false;
  const bool scannable = (pduType_ == BleAdvPduType::kAdvInd) ||
                         (pduType_ == BleAdvPduType::kAdvScanInd);
  if (hasScanReq && scannable && !connectAccepted) {
    clearRadioCoreEvents(radio_);
    radio_->PACKETPTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&scanRspPacket_[0]));
    radio_->SHORTS =
        ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
         RADIO_SHORTS_TXREADY_START_Msk) |
        ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
          << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
         RADIO_SHORTS_PHYEND_DISABLE_Msk);

    // Wait for T_IFS turnaround (150us nominal).
    const uint32_t txTriggerTargetUs = rxEndUs + kBleConnTxenAfterRxUs;
    uint32_t txTriggerNowUs = bleTimingUs();
    while (!timeReachedUs(txTriggerNowUs, txTriggerTargetUs)) {
      txTriggerNowUs = bleTimingUs();
    }

    radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
    txScanRspOk = waitDisabled(spinLimit);
    radio_->SHORTS = 0U;
  }

  if (interaction != nullptr) {
    interaction->scanResponseTransmitted = txScanRspOk;
    if (hasConnectInd && !connectAccepted) {
      interaction->receivedConnectInd = false;
      interaction->connectIndChSel2 = false;
    }
  }

  clearRadioCoreEvents(radio_);
  return true;
}

bool BleRadio::advertiseInteractOnce(BleAdvertisingChannel channel,
                                     BleAdvInteraction* interaction,
                                     uint32_t requestListenSpinLimit,
                                     uint32_t spinLimit) {
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  const bool ok = advertiseInteractOncePrepared(channel, interaction,
                                                requestListenSpinLimit, spinLimit);
  endUnconnectedRadioActivity();
  return ok;
}

bool BleRadio::advertiseInteractEvent(BleAdvInteraction* interaction,
                                      uint32_t interChannelDelayUs,
                                      uint32_t requestListenSpinLimit,
                                      uint32_t spinLimit) {
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  // Add spec-compliant random delay (0-10ms) to prevent persistent collisions.
  {
    static uint32_t seed = 0U;
    if (seed == 0U) {
      seed = micros() ^ (static_cast<uint32_t>(address_[0]) << 24U);
    }
    seed = seed * 1103515245U + 12345U;
    const uint32_t advDelayUs = (seed % 10001U);
    if (advDelayUs > 0U) {
      delayMicroseconds(advDelayUs);
    }
  }

  BleAdvInteraction tmp{};
  bool allOk = true;
  const BleAdvertisingChannel channels[] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };
  const uint8_t startIndex = static_cast<uint8_t>(advCycleStartIndex_ % 3U);
  if (interaction != nullptr) {
    interaction->channel = channels[startIndex];
    interaction->receivedScanRequest = false;
    interaction->scanResponseTransmitted = false;
    interaction->receivedConnectInd = false;
    interaction->connectIndChSel2 = false;
    interaction->peerAddressRandom = false;
    interaction->rssiDbm = 0;
    memset(interaction->peerAddress, 0, sizeof(interaction->peerAddress));
  }

  for (size_t i = 0; i < 3U; ++i) {
    const uint8_t idx = static_cast<uint8_t>((startIndex + i) % 3U);
    tmp = {};
    const bool ok = advertiseInteractOncePrepared(channels[idx], &tmp,
                                                  requestListenSpinLimit, spinLimit);
    allOk = allOk && ok;

    if (interaction != nullptr) {
      if (tmp.receivedConnectInd || tmp.receivedScanRequest) {
        *interaction = tmp;
      }
    }

    if (tmp.receivedConnectInd) {
      // Stop advertising this event immediately if a connection indication
      // was observed.
      break;
    }

    if ((i < 2U) && (interChannelDelayUs > 0U)) {
      delayMicroseconds(interChannelDelayUs);
    }
  }

  advCycleStartIndex_ = static_cast<uint8_t>((startIndex + 1U) % 3U);

  endUnconnectedRadioActivity();
  return allOk;
}

bool BleRadio::isConnected() const { return connected_; }

bool BleRadio::isConnectionEncrypted() const {
  return connected_ &&
         connectionEncSessionValid_ &&
         connectionEncRxEnabled_ &&
         connectionEncTxEnabled_;
}

bool BleRadio::getConnectionInfo(BleConnectionInfo* info) const {
  if (info == nullptr || !connected_) {
    return false;
  }

  memcpy(info->peerAddress, connectionPeerAddress_, sizeof(info->peerAddress));
  info->peerAddressRandom = connectionPeerAddressRandom_;
  info->accessAddress = connectionAccessAddress_;
  info->crcInit = connectionCrcInit_;
  info->intervalUnits = connectionIntervalUnits_;
  info->latency = connectionLatency_;
  info->supervisionTimeoutUnits = connectionTimeoutUnits_;
  memcpy(info->channelMap, connectionChannelMap_, sizeof(info->channelMap));
  info->channelCount = connectionChannelCount_;
  info->hopIncrement = connectionHop_;
  info->sleepClockAccuracy = connectionSca_;
  return true;
}

void BleRadio::getEncryptionDebugCounters(BleEncryptionDebugCounters* out) const {
  if (out == nullptr) {
    return;
  }
  *out = encDebug_;
}

void BleRadio::clearEncryptionDebugCounters() {
  memset(&encDebug_, 0, sizeof(encDebug_));
}

bool BleRadio::hasBondRecord() const { return bondRecordValid_; }

bool BleRadio::getBondRecord(BleBondRecord* outRecord) const {
  if (outRecord == nullptr || !bondRecordValid_) {
    return false;
  }
  memcpy(outRecord, &bondRecord_, sizeof(*outRecord));
  return true;
}

bool BleRadio::clearBondRecord(bool clearPersistentStorage) {
  memset(&bondRecord_, 0, sizeof(bondRecord_));
  bondRecordValid_ = false;
  bondStorageLoaded_ = true;
  bondKeyPrimedForConnection_ = false;
  if (clearPersistentStorage) {
    return clearPersistentBondRecord();
  }
  return true;
}

void BleRadio::setBondPersistenceCallbacks(BleBondLoadCallback loadCallback,
                                           BleBondSaveCallback saveCallback,
                                           BleBondClearCallback clearCallback,
                                           void* context) {
  bondLoadCallback_ = loadCallback;
  bondSaveCallback_ = saveCallback;
  bondClearCallback_ = clearCallback;
  bondCallbackContext_ = context;
  bondStorageLoaded_ = false;
  loadBondRecordFromPersistence();
}

void BleRadio::setTraceCallback(BleTraceCallback callback, void* context) {
  traceCallback_ = callback;
  traceCallbackContext_ = context;
}

bool BleRadio::getLastDisconnectReason(uint8_t* outReason, bool* outRemote) const {
  if (outReason == nullptr || !lastDisconnectReasonValid_) {
    return false;
  }
  *outReason = lastDisconnectReason_;
  if (outRemote != nullptr) {
    *outRemote = lastDisconnectReasonRemote_;
  }
  return true;
}

void BleRadio::rememberDisconnectReason(uint8_t reason, bool remote) {
  lastDisconnectReason_ = reason;
  lastDisconnectReasonValid_ = true;
  lastDisconnectReasonRemote_ = remote;
}

bool BleRadio::disconnect(uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return false;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitDisabled(spinLimit);
  connected_ = false;
  connectionLastTxLlid_ = 0x01U;
  connectionLastTxLength_ = 0U;
  connectionLastTxSn_ = 0U;
  connectionTxHistoryValid_ = false;
  connectionLastTxPlainLlid_ = 0x01U;
  connectionLastTxPlainLength_ = 0U;
  memset(connectionLastTxPlainPayload_, 0, sizeof(connectionLastTxPlainPayload_));
  connectionPendingTxLlid_ = 0x01U;
  connectionPendingTxLength_ = 0U;
  connectionPendingTxValid_ = false;
  memset(connectionPendingTxPayload_, 0, sizeof(connectionPendingTxPayload_));
  connectionAttMtu_ = kBleDefaultAttMtu;
  connectionUseChSel2_ = false;
  connectionChannelId_ = 0U;
  connectionMissedEventCount_ = 0U;
  connectionFirstEventListenUs_ = 0U;
  connectionSyncAttemptsRemaining_ = 0U;
  connectionUpdatePending_ = false;
  connectionUpdateInstant_ = 0U;
  connectionPendingWinSize_ = 0U;
  connectionPendingWinOffset_ = 0U;
  connectionPendingIntervalUnits_ = 0U;
  connectionPendingLatency_ = 0U;
  connectionPendingTimeoutUnits_ = 0U;
  connectionChannelMapPending_ = false;
  connectionChannelMapInstant_ = 0U;
  memset(connectionPendingChannelMap_, 0, sizeof(connectionPendingChannelMap_));
  connectionPendingChannelCount_ = 0U;
  connectionServiceChangedIndicationsEnabled_ = false;
  connectionServiceChangedIndicationPending_ = false;
  connectionServiceChangedIndicationAwaitingConfirm_ = false;
  connectionBatteryNotificationsEnabled_ = false;
  connectionBatteryNotificationPending_ = false;
  connectionPreparedWriteActive_ = false;
  connectionPreparedWriteHandle_ = 0U;
  connectionPreparedWriteValue_[0] = 0U;
  connectionPreparedWriteValue_[1] = 0U;
  connectionPreparedWriteMask_ = 0U;
  lastDisconnectReason_ = 0U;
  lastDisconnectReasonValid_ = false;
  lastDisconnectReasonRemote_ = false;
  clearCustomGattConnectionState();
  clearConnectionSecurityState();
  restoreAdvertisingLinkDefaults();
  clearRadioCoreEvents(radio_);
  endUnconnectedRadioActivity();
  emitBleTrace("DISCONNECT");
  return true;
}

bool BleRadio::pollConnectionEvent(BleConnectionEvent* event, uint32_t spinLimit) {
  if (event != nullptr) {
    event->eventStarted = false;
    event->packetReceived = false;
    event->crcOk = false;
    event->emptyAckTransmitted = false;
    event->packetIsNew = false;
    event->peerAckedLastTx = false;
    event->freshTxAllowed = false;
    event->implicitEmptyAck = false;
    event->terminateInd = false;
    event->disconnectReasonValid = false;
    event->disconnectReasonRemote = false;
    event->llControlPacket = false;
    event->attPacket = false;
    event->txPacketSent = false;
    event->eventCounter = connectionEventCounter_;
    event->dataChannel = 0;
    event->rssiDbm = 0;
    event->llid = 0;
    event->rxNesn = 0U;
    event->rxSn = 0U;
    event->disconnectReason = 0U;
    event->llControlOpcode = 0;
    event->attOpcode = 0;
    event->payloadLength = 0;
    event->txLlid = 0x01U;
    event->txNesn = 0U;
    event->txSn = 0U;
    event->txPayloadLength = 0;
    event->payload = nullptr;
    event->txPayload = nullptr;
  }

  if (!initialized_ || radio_ == nullptr || !connected_) {
    return false;
  }

  const uint32_t intervalUsForListen =
      (connectionIntervalUnits_ > 0U)
          ? (static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL)
          : 1250UL;

  uint32_t nowUs = bleTimingUs();
  // If application code stalled long enough that the current event window is
  // already behind us, align channel selection to the next event to avoid
  // staying one event behind indefinitely.
  uint32_t lateAllowanceUs = 0U;
  if (connectionSyncAttemptsRemaining_ > 0U) {
    // During initial sync after CONNECT_IND, the central may transmit anywhere
    // within the transmit window; allow a wider late window before declaring
    // the event missed.
    lateAllowanceUs = connectionFirstEventListenUs_ + 400U;
  } else {
    // In steady-state, use the post-anchor RX listen window to decide whether
    // we've missed the current event. A small late threshold (e.g. a few
    // hundred microseconds) can cause channel selection to advance even while
    // we're still within the valid RX window, leading to cascaded timeouts.
    const uint32_t listenBudgetUs =
        bleConnectionRxListenUs(connectionIntervalUnits_, connectionSca_);
    lateAllowanceUs =
        (listenBudgetUs > kBleConnRxStartLeadUs)
            ? static_cast<uint32_t>(listenBudgetUs - kBleConnRxStartLeadUs)
            : 0U;
  }
  const bool useCurrentEventCounterForChannel =
      timeReachedUs(nowUs, connectionNextEventUs_ + lateAllowanceUs);

  const uint32_t rxStartUs = connectionNextEventUs_ - kBleConnRxStartLeadUs;
  if (!timeReachedUs(nowUs, rxStartUs)) {
    const uint32_t deltaUs = static_cast<uint32_t>(rxStartUs - nowUs);
    if (deltaUs > kBleConnAnchorPrewaitUs) {
      return false;
    }
    // Short, bounded pre-wait near anchor reduces event jitter while
    // allowing RX ramp-up before the nominal event anchor.
    while (!timeReachedUs(nowUs, rxStartUs)) {
      nowUs = bleTimingUs();
    }
  }

  const uint32_t currentEventAnchorUs = connectionNextEventUs_;
  updateNextConnectionEventTime();

  // `updateNextConnectionEventTime()` advances `connectionEventCounter_`
  // before servicing this event. Use the same "current event" counter basis
  // as channel selection so LL instants are applied on the intended event.
  const uint16_t currentEventCounter =
      useCurrentEventCounterForChannel
          ? connectionEventCounter_
          : ((connectionEventCounter_ > 0U)
                 ? static_cast<uint16_t>(connectionEventCounter_ - 1U)
                 : 0U);

  if (connectionUpdatePending_ &&
      llEventInstantReached(currentEventCounter, connectionUpdateInstant_)) {
    if ((connectionPendingIntervalUnits_ >= 6U) && (connectionPendingIntervalUnits_ <= 3200U) &&
        (connectionPendingTimeoutUnits_ >= 10U)) {
      connectionIntervalUnits_ = connectionPendingIntervalUnits_;
      connectionLatency_ = connectionPendingLatency_;
      connectionTimeoutUnits_ = connectionPendingTimeoutUnits_;
      const uint32_t newIntervalUs =
          static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL;
      if (newIntervalUs > 0U) {
        const uint32_t winOffsetUs =
            static_cast<uint32_t>(connectionPendingWinOffset_) * 1250UL;
        const uint32_t winSizeUs =
            static_cast<uint32_t>((connectionPendingWinSize_ > 0U)
                                      ? connectionPendingWinSize_
                                      : 1U) * 1250UL;
        connectionNextEventUs_ = currentEventAnchorUs + winOffsetUs;
        // At the instant event, the central may transmit anywhere inside the
        // new transmit window that starts `winOffset` after the old anchor.
        // Keep RX open across the full offset + window span for re-sync.
        connectionFirstEventListenUs_ = winOffsetUs + winSizeUs;
        connectionSyncAttemptsRemaining_ = 4U;
        const uint32_t nowUsAfterUpdate = bleTimingUs();
        uint8_t guard = 8U;
        while (guard-- > 0U && timeReachedUs(nowUsAfterUpdate, connectionNextEventUs_)) {
          connectionNextEventUs_ += newIntervalUs;
          ++connectionEventCounter_;
          // For CSA#1, advance channel-use pointer to stay in sync with
          // the central's channel sequence across skipped events.
          if (!connectionUseChSel2_) {
            connectionChanUse_ =
                static_cast<uint8_t>((connectionChanUse_ + connectionHop_) % 37U);
          }
        }
      }
    }
    connectionUpdatePending_ = false;
    connectionPendingWinSize_ = 0U;
    connectionPendingWinOffset_ = 0U;
  }
  if (connectionChannelMapPending_ &&
      llEventInstantReached(currentEventCounter, connectionChannelMapInstant_)) {
    if (connectionPendingChannelCount_ >= 2U) {
      memcpy(connectionChannelMap_, connectionPendingChannelMap_,
             sizeof(connectionChannelMap_));
      connectionChannelCount_ = connectionPendingChannelCount_;
    }
    connectionChannelMapPending_ = false;
  }

  const uint8_t dataChannel =
      selectNextDataChannel(useCurrentEventCounterForChannel);
  if (!setDataChannel(dataChannel)) {
    return false;
  }

  if (event != nullptr) {
    event->eventStarted = true;
    event->eventCounter = connectionEventCounter_;
    event->dataChannel = dataChannel;
  }

  clearRadioCoreEvents(radio_);
  memset(rxPacket_, 0, sizeof(rxPacket_));
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;

  uint32_t rxListenUs =
      bleConnectionRxListenUs(connectionIntervalUnits_, connectionSca_);
  if (rxListenUs == 0U) {
    rxListenUs = intervalUsForListen;
  }
  if (connectionSyncAttemptsRemaining_ > 0U) {
    // During initial sync after CONNECT_IND, keep RX active from the early
    // pre-anchor lead through the full transmit window (plus margin).
    const uint32_t syncBudgetUs =
        kBleConnRxStartLeadUs + connectionFirstEventListenUs_ + 800U;
    if (syncBudgetUs > rxListenUs) {
      rxListenUs = syncBudgetUs;
    }
    if (rxListenUs > 50000U) {
      rxListenUs = 50000U;
    }
  }
  const bool endSeen = waitRadioEndBudgeted(radio_, rxListenUs, spinLimit);
  if (!endSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs, spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    if (connectionSyncAttemptsRemaining_ > 0U) {
      --connectionSyncAttemptsRemaining_;
    }
    ++connectionMissedEventCount_;
    {
      const uint32_t intervalUs =
          (connectionIntervalUnits_ > 0U)
              ? (static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL)
              : 1250UL;
      const uint32_t timeoutUs =
          static_cast<uint32_t>(connectionTimeoutUnits_) * 10000UL;
      uint32_t supervisionEvents = timeoutUs / intervalUs;
      if (supervisionEvents < 1U) {
        supervisionEvents = 1U;
      }
      if (connectionMissedEventCount_ >= supervisionEvents) {
        if (event != nullptr) {
          event->terminateInd = true;
          event->disconnectReasonValid = true;
          event->disconnectReasonRemote = false;
          event->disconnectReason = kBleLlErrorConnectionTimeout;
        }
        rememberDisconnectReason(kBleLlErrorConnectionTimeout, false);
        emitBleTrace("SUPERVISION_TIMEOUT");
        disconnect(spinLimit / 2U + 1U);
      }
    }
    emitBleTrace("EVT_RX_TIMEOUT");
    return true;
  }
  const uint32_t rxEndTimestampUs = bleTimingUs();

  bool disabled =
      waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs, spinLimit / 2U + 1U);
  if (!disabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    disabled = waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs,
                                         spinLimit / 2U + 1U);
  }
  radio_->SHORTS = 0U;
  if (!disabled) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOk = (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);

  if (event != nullptr) {
    event->packetReceived = true;
    event->crcOk = crcOk;
    event->rssiDbm = radioRssiDbm(radio_);
    event->payload = &rxPacket_[2];
  }

  if (!crcOk) {
    if (connectionSyncAttemptsRemaining_ > 0U) {
      --connectionSyncAttemptsRemaining_;
    }
    ++connectionMissedEventCount_;
    {
      const uint32_t intervalUs =
          (connectionIntervalUnits_ > 0U)
              ? (static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL)
              : 1250UL;
      const uint32_t timeoutUs =
          static_cast<uint32_t>(connectionTimeoutUnits_) * 10000UL;
      uint32_t supervisionEvents = timeoutUs / intervalUs;
      if (supervisionEvents < 1U) {
        supervisionEvents = 1U;
      }
      if (connectionMissedEventCount_ >= supervisionEvents) {
        if (event != nullptr) {
          event->terminateInd = true;
          event->disconnectReasonValid = true;
          event->disconnectReasonRemote = false;
          event->disconnectReason = kBleLlErrorConnectionTimeout;
        }
        rememberDisconnectReason(kBleLlErrorConnectionTimeout, false);
        emitBleTrace("SUPERVISION_TIMEOUT");
        disconnect(spinLimit / 2U + 1U);
      }
    }
    clearRadioCoreEvents(radio_);
    return true;
  }
  connectionMissedEventCount_ = 0U;

  const uint8_t hdr0 = rxPacket_[0];
  const uint8_t llid = hdr0 & 0x03U;
  const uint8_t nesn = (hdr0 >> 2U) & 0x01U;
  const uint8_t sn = (hdr0 >> 3U) & 0x01U;
  const uint8_t rxLengthRaw = rxPacket_[1];
  const uint32_t intervalUs =
      (connectionIntervalUnits_ > 0U)
          ? (static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL)
          : 1250UL;
  const uint32_t rxPacketStartUs =
      rxEndTimestampUs - bleDataPduAirTimeUs(rxLengthRaw);
  connectionNextEventUs_ = rxPacketStartUs + intervalUs;
  if (connectionSyncAttemptsRemaining_ > 0U) {
    connectionSyncAttemptsRemaining_ = 0U;
  }

  bool peerAckedLastTx =
      connectionTxHistoryValid_ && (nesn != connectionLastTxSn_);
  const bool snMatchesExpected = (sn == connectionExpectedRxSn_);
  // RX payload freshness is keyed only by SN. NESN/ACK applies to our TX path
  // independently and must not block consuming valid peer packets.
  const bool canConsumeNewPayload = snMatchesExpected;

  bool packetIsNew = false;
  if (canConsumeNewPayload) {
    connectionExpectedRxSn_ ^= 0x01U;
    packetIsNew = true;
  }
  bool implicitEmptyAck = false;
  const bool lastTxWasEarlyLlControlResponse =
      !connectionEncSessionValid_ &&
      connectionTxHistoryValid_ &&
      (connectionLastTxLlid_ == kBlePduLlControl) &&
      (connectionLastTxPlainLength_ >= 1U) &&
      ((connectionLastTxPlainPayload_[0] == kBleLlCtrlFeatureRsp) ||
       (connectionLastTxPlainPayload_[0] == kBleLlCtrlVersionInd) ||
       (connectionLastTxPlainPayload_[0] == kBleLlCtrlLengthRsp) ||
       (connectionLastTxPlainPayload_[0] == kBleLlCtrlPhyRsp) ||
       (connectionLastTxPlainPayload_[0] == kBleLlCtrlPingRsp) ||
       (connectionLastTxPlainPayload_[0] == kBleLlCtrlConnectionParamRsp));
  const bool pendingTxIsAttResponse =
      connectionPendingTxValid_ &&
      (connectionPendingTxLlid_ == kBlePduDataStartOrComplete) &&
      (connectionPendingTxLength_ >= kBleL2capHeaderLen) &&
      (readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidAtt);
  if (!peerAckedLastTx &&
      connectionTxHistoryValid_ &&
      packetIsNew &&
      (((connectionLastTxLlid_ == 0x01U) &&
        (connectionLastTxLength_ == 0U)) ||
       lastTxWasEarlyLlControlResponse ||
       pendingTxIsAttResponse)) {
    // Interop guard: some centrals advance SN while holding NESN stable after
    // empty-PDU ACK transitions, immediate early LL control responses, or
    // queued ATT replies. Treat peer progress as an implicit ACK so queued TX
    // payloads do not deadlock on duplicate NESN.
    peerAckedLastTx = true;
    implicitEmptyAck = true;
  }
  if (peerAckedLastTx) {
    connectionTxSn_ ^= 0x01U;
  }

  bool terminateInd = false;
  bool terminateMicFailure = false;
  uint8_t rxLength = rxLengthRaw;

  bool rxWasDecrypted = false;
  auto derivePendingEncSessionKey = [&]() {
    if (terminateInd || !connectionEncKeyDerivationPending_) {
      return;
    }

    if (!aesEncryptLe(smpStk_, connectionEncSkd_, connectionEncSessionKey_)) {
      connectionEncKeyDerivationPending_ = false;
      encDebug_.encLastSessionKeyValid = 0U;
      encDebug_.encLastSessionAltKeyValid = 0U;
      terminateInd = true;
      emitBleTrace("ENC_SESSION_KEY_FAIL");
      return;
    }

    uint8_t skdAlt[16] = {0};
    memcpy(&skdAlt[0], &connectionEncSkd_[8], 8U);
    memcpy(&skdAlt[8], &connectionEncSkd_[0], 8U);
    connectionEncAltKeyValid_ = aesEncryptLe(smpStk_, skdAlt, connectionEncSessionKeyAlt_);
    connectionEncKeyDerivationPending_ = false;
    memcpy(encDebug_.encLastSessionKey, connectionEncSessionKey_,
           sizeof(encDebug_.encLastSessionKey));
    memcpy(encDebug_.encLastSessionAltKey, connectionEncSessionKeyAlt_,
           sizeof(encDebug_.encLastSessionAltKey));
    encDebug_.encLastSessionKeyValid = 1U;
    encDebug_.encLastSessionAltKeyValid = connectionEncAltKeyValid_ ? 1U : 0U;
    encDebug_.encLastRxDir = connectionEncRxDirection_;
    encDebug_.encLastTxDir = connectionEncTxDirection_;
  };

  const bool deferEncryptedDataDecrypt =
      connectionEncSessionValid_ &&
      connectionEncRxEnabled_ &&
      !connectionEncStartReqPending_ &&
      !connectionEncStartReqTxPending_ &&
      !connectionEncAwaitingStartRsp_ &&
      !connectionEncKeyDerivationPending_ &&
      packetIsNew &&
      (llid == kBlePduDataStartOrComplete) &&
      (rxLengthRaw >= kBleMicLen);

  const bool sessionEncryptionPossible =
      connectionEncSessionValid_ &&
      (connectionEncRxEnabled_ || connectionEncTxEnabled_ || connectionEncAwaitingStartRsp_);
  const bool attemptDecryptStartEncReq =
      connectionEncSessionValid_ &&
      connectionEncStartReqPending_ &&
      (llid == kBlePduLlControl) &&
      (rxLengthRaw == static_cast<uint8_t>(1U + kBleMicLen)) &&
      !connectionEncKeyDerivationPending_;
  const bool deferAwaitingStartRspDecrypt =
      connectionEncAwaitingStartRsp_ &&
      (llid == kBlePduLlControl) &&
      (rxLengthRaw >= kBleMicLen);
  const bool shouldAttemptRxDecrypt =
      packetIsNew || attemptDecryptStartEncReq || deferAwaitingStartRspDecrypt;
  bool fastStartEncRspMatched = false;
  if (deferAwaitingStartRspDecrypt &&
      packetIsNew &&
      connectionEncPrecomputedStartRspValid_ &&
      (rxLengthRaw == (1U + kBleMicLen)) &&
      (memcmp(&rxPacket_[2], connectionEncPrecomputedStartRsp_, (1U + kBleMicLen)) == 0)) {
    rxWasDecrypted = true;
    rxLength = 1U;
    rxPacket_[2] = kBleLlCtrlStartEncRsp;
    connectionEncRxCounter_ =
        (connectionEncRxCounter_ + 1ULL) & kBleEncPacketCounterMask;
    fastStartEncRspMatched = true;
    emitBleTrace("LL_START_ENC_RSP_FAST");
  }
  if ((sessionEncryptionPossible || attemptDecryptStartEncReq) &&
      shouldAttemptRxDecrypt &&
      !deferEncryptedDataDecrypt &&
      !deferAwaitingStartRspDecrypt &&
      !fastStartEncRspMatched) {
    const bool allowPlainStartEncRsp =
        connectionEncAwaitingStartRsp_ &&
        (llid == kBlePduLlControl) &&
        (rxLengthRaw == 1U) &&
        (rxPacket_[2] == kBleLlCtrlStartEncRsp);

    if (!allowPlainStartEncRsp) {
      if (rxLengthRaw < kBleMicLen) {
        const bool finalStartRspQueued =
            connectionPendingTxValid_ &&
            (connectionPendingTxLlid_ == kBlePduLlControl) &&
            (connectionPendingTxLength_ >= 1U) &&
            (connectionPendingTxPayload_[0] == kBleLlCtrlStartEncRsp);
        const bool allowStartProcedurePlainZeroLen =
            connectionEncRxEnabled_ &&
            (rxLengthRaw == 0U) &&
            (connectionEncAwaitingStartRsp_ ||
             connectionEncStartReqPending_ ||
             connectionEncStartReqTxPending_ ||
             finalStartRspQueued) &&
            (connectionEncRxCounter_ <= 1ULL);
        // Some centrals emit a small burst of plaintext empty data PDUs right
        // after encryption transitions to ON. Keep this window bounded so we
        // tolerate interop behavior without accepting plaintext payload data.
        const bool allowPostStartPlainZeroLen =
            connectionEncRxEnabled_ &&
            connectionEncTxEnabled_ &&
            (rxLengthRaw == 0U) &&
            ((llid == kBlePduDataStartOrComplete) || (llid == 0x01U)) &&
            (connectionEncRxCounter_ <= 3ULL);
        if (connectionEncRxEnabled_ &&
            !allowStartProcedurePlainZeroLen &&
            !allowPostStartPlainZeroLen) {
          terminateInd = true;
          terminateMicFailure = true;
          ++encDebug_.encRxShortPduCount;
          encDebug_.encRxLastMicFailHdr = hdr0;
          encDebug_.encRxLastMicFailLenRaw = rxLengthRaw;
          encDebug_.encRxLastMicFailDir = connectionEncRxDirection_;
          encDebug_.encRxLastMicFailCounterLo =
              static_cast<uint32_t>(connectionEncRxCounter_ & 0xFFFFFFFFULL);
          encDebug_.encRxLastMicFailState =
              static_cast<uint8_t>((connectionEncSessionValid_ ? 0x01U : 0U) |
                                   (connectionEncRxEnabled_ ? 0x02U : 0U) |
                                   (connectionEncTxEnabled_ ? 0x04U : 0U) |
                                   (connectionEncStartReqPending_ ? 0x08U : 0U) |
                                   (connectionEncStartReqTxPending_ ? 0x10U : 0U) |
                                   (connectionEncAwaitingStartRsp_ ? 0x20U : 0U));
          encDebug_.encRxLastMicFailData0 = 0U;
          encDebug_.encRxLastMicFailData1 = 0U;
          encDebug_.encRxLastMicFailData2 = 0U;
          encDebug_.encRxLastMicFailData3 = 0U;
          encDebug_.encRxLastMicFailData4 = 0U;
          emitBleTrace("ENC_RX_SHORT_PDU");
        }
      } else {
        uint64_t rxCounter = connectionEncRxCounter_;
        if (!packetIsNew && (rxCounter > 0ULL)) {
          rxCounter -= 1ULL;
        }
        uint64_t decryptCounterUsed = rxCounter;

        uint8_t plaintext[kBleDataPduMaxPayload] = {0};
        uint8_t plaintextLen = 0U;
        bool decryptOk = bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                              rxCounter, connectionEncRxDirection_, hdr0,
                                              &rxPacket_[2], rxLengthRaw,
                                              plaintext, &plaintextLen);
        if (!decryptOk &&
            (connectionEncRxEnabled_ || attemptDecryptStartEncReq) &&
            (rxCounter == 0ULL) &&
            (connectionEncAwaitingStartRsp_ || connectionEncStartReqPending_)) {
          const uint8_t swappedRxDirection =
              static_cast<uint8_t>(connectionEncRxDirection_ ^ 0x01U);
          if (bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                   rxCounter, swappedRxDirection, hdr0,
                                   &rxPacket_[2], rxLengthRaw,
                                   plaintext, &plaintextLen)) {
            decryptOk = true;
            connectionEncRxDirection_ = swappedRxDirection;
            connectionEncTxDirection_ = static_cast<uint8_t>(swappedRxDirection ^ 0x01U);
            connectionEncPrecomputedStartRspValid_ = false;
            connectionEncPrecomputedStartRspTxValid_ = false;
            emitBleTrace("ENC_DIR_SWAP");
          } else if (connectionEncAltKeyValid_ &&
                     bleCcmDecryptPayload(connectionEncSessionKeyAlt_, connectionEncIv_,
                                          rxCounter, connectionEncRxDirection_, hdr0,
                                          &rxPacket_[2], rxLengthRaw,
                                          plaintext, &plaintextLen)) {
            decryptOk = true;
            memcpy(connectionEncSessionKey_, connectionEncSessionKeyAlt_,
                   sizeof(connectionEncSessionKey_));
            connectionEncAltKeyValid_ = false;
            connectionEncPrecomputedStartRspValid_ = false;
            connectionEncPrecomputedStartRspTxValid_ = false;
            emitBleTrace("ENC_KEY_SWAP");
          } else if (connectionEncAltKeyValid_ &&
                     bleCcmDecryptPayload(connectionEncSessionKeyAlt_, connectionEncIv_,
                                          rxCounter, swappedRxDirection, hdr0,
                                          &rxPacket_[2], rxLengthRaw,
                                          plaintext, &plaintextLen)) {
            decryptOk = true;
            memcpy(connectionEncSessionKey_, connectionEncSessionKeyAlt_,
                   sizeof(connectionEncSessionKey_));
            connectionEncAltKeyValid_ = false;
            connectionEncRxDirection_ = swappedRxDirection;
            connectionEncTxDirection_ = static_cast<uint8_t>(swappedRxDirection ^ 0x01U);
            connectionEncPrecomputedStartRspValid_ = false;
            connectionEncPrecomputedStartRspTxValid_ = false;
            emitBleTrace("ENC_KEY_DIR_SWAP");
          }
        }
        if (!decryptOk &&
            (connectionEncRxEnabled_ || attemptDecryptStartEncReq) &&
            packetIsNew &&
            (connectionEncRxCounter_ < 4ULL)) {
          for (uint8_t delta = 1U; delta <= 2U; ++delta) {
            const uint64_t trialCounter =
                (rxCounter + static_cast<uint64_t>(delta)) & kBleEncPacketCounterMask;
            if (bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                     trialCounter, connectionEncRxDirection_, hdr0,
                                     &rxPacket_[2], rxLengthRaw,
                                     plaintext, &plaintextLen)) {
              decryptOk = true;
              decryptCounterUsed = trialCounter;
              emitBleTrace("ENC_RX_COUNTER_RESYNC");
              break;
            }
          }
        }
        if (!decryptOk) {
          if (connectionEncRxEnabled_) {
            terminateInd = true;
            terminateMicFailure = true;
            ++encDebug_.encRxMicFailCount;
            encDebug_.encRxLastMicFailHdr = hdr0;
            encDebug_.encRxLastMicFailLenRaw = rxLengthRaw;
            encDebug_.encRxLastMicFailDir = connectionEncRxDirection_;
            encDebug_.encRxLastMicFailCounterLo =
                static_cast<uint32_t>(rxCounter & 0xFFFFFFFFULL);
            encDebug_.encRxLastMicFailState =
                static_cast<uint8_t>((connectionEncSessionValid_ ? 0x01U : 0U) |
                                     (connectionEncRxEnabled_ ? 0x02U : 0U) |
                                     (connectionEncTxEnabled_ ? 0x04U : 0U) |
                                     (connectionEncStartReqPending_ ? 0x08U : 0U) |
                                     (connectionEncStartReqTxPending_ ? 0x10U : 0U) |
                                     (connectionEncAwaitingStartRsp_ ? 0x20U : 0U));
            encDebug_.encRxLastMicFailData0 = rxPacket_[2];
            encDebug_.encRxLastMicFailData1 =
                (rxLengthRaw > 1U) ? rxPacket_[3] : 0U;
            encDebug_.encRxLastMicFailData2 =
                (rxLengthRaw > 2U) ? rxPacket_[4] : 0U;
            encDebug_.encRxLastMicFailData3 =
                (rxLengthRaw > 3U) ? rxPacket_[5] : 0U;
            encDebug_.encRxLastMicFailData4 =
                (rxLengthRaw > 4U) ? rxPacket_[6] : 0U;
            emitBleTrace("ENC_RX_MIC_FAIL");
          }
        } else {
          rxWasDecrypted = true;
          rxLength = plaintextLen;
          if (plaintextLen > 0U) {
            memcpy(&rxPacket_[2], plaintext, plaintextLen);
          }
          if (packetIsNew) {
            connectionEncRxCounter_ =
                (decryptCounterUsed + 1ULL) & kBleEncPacketCounterMask;
          }
        }
      }
    }
  }

  if (deferEncryptedDataDecrypt) {
    // Data PDUs in encrypted mode are decrypted after TX so we can keep ACK
    // timing deterministic at T_IFS.
    rxLength = 0U;
  }

  if ((llid == kBlePduLlControl) && (rxLength >= 2U) &&
      (rxPacket_[2] == kBleLlCtrlTerminateInd)) {
    rememberDisconnectReason(rxPacket_[3], true);
    if (event != nullptr) {
      event->disconnectReasonValid = true;
      event->disconnectReasonRemote = true;
      event->disconnectReason = rxPacket_[3];
    }
    terminateInd = true;
  }

  if ((llid == kBlePduLlControl) &&
      (rxLength >= 1U)) {
    const uint8_t opcode = rxPacket_[2];
    if (connectionEncStartReqPending_) {
      ++encDebug_.startPendingControlRxSeen;
      encDebug_.startPendingLastHdr = hdr0;
      encDebug_.startPendingLastLenRaw = rxLengthRaw;
      encDebug_.startPendingLastByte0 = opcode;
      encDebug_.startPendingLastDecrypted = rxWasDecrypted ? 1U : 0U;
    }

    if (packetIsNew) {
      if (opcode == kBleLlCtrlEncReq) {
        ++encDebug_.mainEncReqSeen;
      } else if (opcode == kBleLlCtrlStartEncReq) {
        ++encDebug_.mainStartEncReqSeen;
        if (rxWasDecrypted) {
          ++encDebug_.mainStartEncReqSeenDecrypted;
        }
      } else if (opcode == kBleLlCtrlStartEncRsp) {
        encDebug_.encStartRspLastRawLen = rxLengthRaw;
        encDebug_.encStartRspLastDecrypted = rxWasDecrypted ? 1U : 0U;
        encDebug_.encStartRspLastHdr = hdr0;
      }
    }
  }

  if (event != nullptr) {
    event->packetIsNew = packetIsNew;
    event->peerAckedLastTx = peerAckedLastTx;
    event->freshTxAllowed = (!connectionTxHistoryValid_ || peerAckedLastTx);
    event->implicitEmptyAck = implicitEmptyAck;
    event->terminateInd = terminateInd;
    event->llid = llid;
    event->rxNesn = nesn;
    event->rxSn = sn;
    event->payloadLength = rxLength;
    if (llid == kBlePduLlControl && rxLength >= 1U) {
      event->llControlPacket = true;
      event->llControlOpcode = rxPacket_[2];
    }
    if (llid == kBlePduDataStartOrComplete && rxLength >= 5U) {
      const uint16_t l2capLen = readLe16(&rxPacket_[2]);
      const uint16_t cid = readLe16(&rxPacket_[4]);
      const uint8_t availableAttLen = (rxLength > kBleL2capHeaderLen)
                                          ? static_cast<uint8_t>(rxLength - kBleL2capHeaderLen)
                                          : 0U;
      if (cid == kBleL2capCidAtt && l2capLen > 0U && availableAttLen > 0U) {
        event->attPacket = true;
        event->attOpcode = rxPacket_[2 + kBleL2capHeaderLen];
      }
    }
  }

  bool llControlHandledImmediate = false;
  bool immediateLlControlResponseValid = false;
  uint8_t immediateLlControlResponseLength = 0U;
  // If the peer has not ACKed our last TX PDU, keep retransmitting that exact
  // packet/SN until ACKed. This is required for encrypted empty PDUs as well,
  // since each fresh encrypted transmission consumes a new CCM packet counter.
  const bool canSendNewPayloadThisEvent =
      !connectionTxHistoryValid_ || peerAckedLastTx;
  // Several controllers retransmit the same LL control request until they see
  // a matching control response (not just an empty ACK). Build and transmit LL
  // control responses in this event whenever SN/NESN allows a fresh TX payload.
  if (packetIsNew && !terminateInd &&
      (llid == kBlePduLlControl) &&
      !deferAwaitingStartRspDecrypt &&
      canSendNewPayloadThisEvent) {
    llControlHandledImmediate = true;
    if (buildLlControlResponse(&rxPacket_[2], rxLength, connectionTxPayload_,
                               &immediateLlControlResponseLength, &terminateInd) &&
        immediateLlControlResponseLength > 0U) {
      immediateLlControlResponseValid = true;
    }
  }

  // If we decrypted LL_START_ENC_REQ (it arrived encrypted), some controllers
  // expect LL_START_ENC_RSP to be encrypted as well. Enable TX encryption
  // immediately for this response in that case.
  if (immediateLlControlResponseValid &&
      (llid == kBlePduLlControl) &&
      (rxLength >= 1U) &&
      (rxPacket_[2] == kBleLlCtrlStartEncReq) &&
      rxWasDecrypted) {
    connectionEncTxEnabled_ = true;
    connectionEncEnableTxOnNextEvent_ = false;
  }

  uint8_t txLlid = 0x01U;
  uint8_t txLength = 0U;
  const bool txCanUseFreshPayload =
      !terminateInd && canSendNewPayloadThisEvent;
  const bool pendingTxIsAttResponseForRetry =
      connectionPendingTxValid_ &&
      (connectionPendingTxLlid_ == kBlePduDataStartOrComplete) &&
      (connectionPendingTxLength_ >= kBleL2capHeaderLen) &&
      (readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidAtt);
  const bool promotePendingTxAfterEmptyAck =
      !txCanUseFreshPayload &&
      !terminateInd &&
      pendingTxIsAttResponseForRetry &&
      (connectionLastTxLlid_ == 0x01U) &&
      (connectionLastTxLength_ == 0U);
  if (txCanUseFreshPayload || promotePendingTxAfterEmptyAck) {
    // Time-critical path: transmit either a queued response from a previous
    // event or an empty ACK in this event, then build next responses after TX.
    // Some centrals retransmit the same ATT request if they saw only our empty
    // PDU. In that case, promote the queued ATT response instead of deadlocking
    // on repeated empty-PDU retransmits.
    if (immediateLlControlResponseValid) {
      txLlid = kBlePduLlControl;
      txLength = immediateLlControlResponseLength;
    } else if (connectionPendingTxValid_) {
      txLlid = connectionPendingTxLlid_;
      txLength = connectionPendingTxLength_;
      if (txLength > 0U) {
        memcpy(connectionTxPayload_, connectionPendingTxPayload_, txLength);
      }
      connectionPendingTxValid_ = false;
      connectionPendingTxLlid_ = 0x01U;
      connectionPendingTxLength_ = 0U;
    } else {
      txLlid = 0x01U;
      txLength = 0U;
    }

    connectionLastTxLlid_ = txLlid;
    connectionLastTxLength_ = txLength;
  } else {
    // Retransmit the last payload verbatim until it is ACKed.
    txLlid = connectionLastTxLlid_;
    txLength = connectionLastTxLength_;
  }

  if (terminateInd && terminateMicFailure) {
    txLlid = kBlePduLlControl;
    txLength = 2U;
    connectionTxPayload_[0] = kBleLlCtrlTerminateInd;
    connectionTxPayload_[1] = kBleLlErrorMicFailure;
    ++encDebug_.encClearCount;
    encDebug_.encLastClearReason = 0x05U;  // MIC failure terminate.
    connectionEncRxEnabled_ = false;
    connectionEncTxEnabled_ = false;
    connectionLastTxLlid_ = txLlid;
    connectionLastTxLength_ = txLength;
    connectionLastTxWasEncrypted_ = false;
    connectionLastTxEncryptedLength_ = 0U;
  }

  // Preserve a snapshot of the plaintext payload for retransmissions and for
  // `BleConnectionEvent::txPayload` visibility (the working buffer is reused later).
  const bool txPayloadIsNewPlain =
      txCanUseFreshPayload || promotePendingTxAfterEmptyAck ||
      (terminateInd && terminateMicFailure);
  if (txPayloadIsNewPlain) {
    connectionLastTxPlainLlid_ = txLlid;
    connectionLastTxPlainLength_ = txLength;
    if (txLength > 0U) {
      memcpy(connectionLastTxPlainPayload_, connectionTxPayload_, txLength);
    }
  }

  txPacket_[0] = static_cast<uint8_t>((txLlid & 0x03U) |
                                      ((connectionExpectedRxSn_ & 0x01U) << 2U) |
                                      ((connectionTxSn_ & 0x01U) << 3U));
  const uint8_t txNesnBit = static_cast<uint8_t>((txPacket_[0] >> 2U) & 0x01U);
  const uint8_t txSnBit = static_cast<uint8_t>((txPacket_[0] >> 3U) & 0x01U);

  const uint8_t* const txPlainPayloadForCurrentTx =
      txPayloadIsNewPlain ? &connectionTxPayload_[0] : &connectionLastTxPlainPayload_[0];
  const bool txIsEncRspPlain =
      (txLlid == kBlePduLlControl) &&
      (txLength >= 1U) &&
      (txPlainPayloadForCurrentTx[0] == kBleLlCtrlEncRsp);
  const bool txIsStartEncReqPlain =
      (txLlid == kBlePduLlControl) &&
      (txLength >= 1U) &&
      (txPlainPayloadForCurrentTx[0] == kBleLlCtrlStartEncReq);
  const bool txIsStartEncRspPlain =
      (txLlid == kBlePduLlControl) &&
      (txLength >= 1U) &&
      (txPlainPayloadForCurrentTx[0] == kBleLlCtrlStartEncRsp);
  const bool txIsEmptyAckPlain = (txLlid == 0x01U) && (txLength == 0U);

  const bool sessionEncryptionActive = connectionEncTxEnabled_ && connectionEncSessionValid_;
  const bool encryptCurrentTx = sessionEncryptionActive &&
                                (txCanUseFreshPayload || connectionLastTxWasEncrypted_);
  uint8_t txLengthOnAir = txLength;
  uint64_t txCounterUsedForCurrentTx = 0ULL;
  bool txCounterUsedValid = false;
  if (!txCanUseFreshPayload && sessionEncryptionActive && connectionLastTxWasEncrypted_) {
    txLengthOnAir = connectionLastTxEncryptedLength_;
    txPacket_[1] = txLengthOnAir;
    if (txLengthOnAir > 0U) {
      memcpy(&txPacket_[2], connectionLastTxEncryptedPayload_, txLengthOnAir);
    }
  } else {
    const uint8_t* const txPlainPayload = txPlainPayloadForCurrentTx;
    txPacket_[1] = txLength;
    if (txLength > 0U) {
      memcpy(&txPacket_[2], txPlainPayload, txLength);
    }

    if (encryptCurrentTx) {
      uint8_t encryptedLength = 0U;
      bool encryptedOk = false;
      // `connectionEncTxCounter_` tracks the next TX packet counter to use.
      // Every fresh encrypted data-channel PDU (including zero-length/MIC-only)
      // consumes one counter value. Retransmissions reuse the cached ciphertext.
      uint64_t txCounterToUse = connectionEncTxCounter_;
      if (txCanUseFreshPayload &&
          (txLlid == 0x01U) &&
          (txLength == 0U) &&
          connectionEncPrecomputedEmptyValid_ &&
          (connectionEncPrecomputedCounter_ == txCounterToUse)) {
        encryptedLength = kBleMicLen;
        memcpy(connectionLastTxEncryptedPayload_, connectionEncPrecomputedPayload_,
               kBleMicLen);
        encryptedOk = true;
      } else if (txCanUseFreshPayload &&
                 txIsStartEncRspPlain &&
                 connectionEncPrecomputedStartRspTxValid_ &&
                 (connectionEncPrecomputedStartRspTxCounter_ == txCounterToUse)) {
        encryptedLength = static_cast<uint8_t>(1U + kBleMicLen);
        memcpy(connectionLastTxEncryptedPayload_, connectionEncPrecomputedStartRspTx_,
               encryptedLength);
        encryptedOk = true;
      } else {
        encryptedOk = bleCcmEncryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                           txCounterToUse, connectionEncTxDirection_,
                                           txPacket_[0],
                                           txPlainPayload, txLength,
                                           connectionLastTxEncryptedPayload_, &encryptedLength);
      }

      if (!encryptedOk) {
        txLengthOnAir = 0U;
        txPacket_[1] = 0U;
        connectionLastTxWasEncrypted_ = false;
        connectionLastTxEncryptedLength_ = 0U;
        connectionEncPrecomputedEmptyValid_ = false;
        terminateInd = true;
      } else {
        txLengthOnAir = encryptedLength;
        txPacket_[1] = encryptedLength;
        memcpy(&txPacket_[2], connectionLastTxEncryptedPayload_, encryptedLength);
        connectionLastTxWasEncrypted_ = true;
        connectionLastTxEncryptedLength_ = encryptedLength;
        txCounterUsedForCurrentTx = txCounterToUse;
        txCounterUsedValid = true;
        if (txCanUseFreshPayload) {
          connectionEncTxCounter_ =
              (txCounterToUse + 1ULL) & kBleEncPacketCounterMask;
          if (connectionEncPrecomputedCounter_ != connectionEncTxCounter_) {
            connectionEncPrecomputedEmptyValid_ = false;
          }
          if (txIsStartEncRspPlain) {
            connectionEncPrecomputedStartRspTxValid_ = false;
          }
        }
      }
    } else if (txCanUseFreshPayload) {
      connectionLastTxWasEncrypted_ = false;
      connectionLastTxEncryptedLength_ = 0U;
      memset(connectionLastTxEncryptedPayload_, 0, sizeof(connectionLastTxEncryptedPayload_));
    }
  }

  encDebug_.encLastTxHdr = txPacket_[0];
  encDebug_.encLastTxPlainLen = txLength;
  encDebug_.encLastTxAirLen = txLengthOnAir;
  encDebug_.encLastTxWasFresh = txCanUseFreshPayload ? 1U : 0U;
  encDebug_.encLastTxWasEncrypted =
      (sessionEncryptionActive && connectionLastTxWasEncrypted_) ? 1U : 0U;
  if (encDebug_.encLastTxWasEncrypted != 0U) {
    uint64_t txCounterObserved = 0ULL;
    if (txCounterUsedValid) {
      txCounterObserved = txCounterUsedForCurrentTx;
    } else {
      txCounterObserved = (connectionEncTxCounter_ - 1ULL) & kBleEncPacketCounterMask;
    }
    encDebug_.encLastTxCounterLo =
        static_cast<uint32_t>(txCounterObserved & 0xFFFFFFFFULL);
  }

  // During start encryption, some controllers send LL_START_ENC_REQ as a second
  // packet in a later connection event. Some Linux centrals also send early LL
  // control PDUs immediately after the first empty ACK. Keep TX->RX turnaround
  // in hardware for those cases so we can catch same-event follow-up packets
  // without CPU latency.
  const bool doPostTxRxTurnaround =
      !terminateInd &&
      ((connectionEncStartReqPending_ && txIsEncRspPlain) ||
       txIsStartEncReqPlain ||
       txIsEmptyAckPlain);

  clearRadioCoreEvents(radio_);
  if (doPostTxRxTurnaround) {
    // Use a single buffer for TX and immediate follow-up RX so DISABLED->RXEN
    // shortcut can arm the receiver exactly at T_IFS timing.
    const size_t txCopyLen = 2U + static_cast<size_t>(txLengthOnAir);
    const size_t copyLen = (txCopyLen <= sizeof(rxPacket_)) ? txCopyLen : sizeof(rxPacket_);
    memcpy(rxPacket_, txPacket_, copyLen);
    radio_->PACKETPTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
    radio_->SHORTS =
        ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
         RADIO_SHORTS_TXREADY_START_Msk) |
        ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
         RADIO_SHORTS_PHYEND_DISABLE_Msk) |
        ((RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos) &
         RADIO_SHORTS_DISABLED_RXEN_Msk) |
        ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
         RADIO_SHORTS_RXREADY_START_Msk) |
        ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
          << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
         RADIO_SHORTS_ADDRESS_RSSISTART_Msk);
  } else {
    radio_->PACKETPTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&txPacket_[0]));
    radio_->SHORTS =
        ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
         RADIO_SHORTS_TXREADY_START_Msk) |
        ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
         RADIO_SHORTS_PHYEND_DISABLE_Msk);
  }
  const uint32_t txTriggerTargetUs = rxEndTimestampUs + kBleConnTxenAfterRxUs;
  uint32_t txTriggerNowUs = bleTimingUs();
  while (!timeReachedUs(txTriggerNowUs, txTriggerTargetUs)) {
    txTriggerNowUs = bleTimingUs();
  }
  const uint32_t txLagUs = static_cast<uint32_t>(txTriggerNowUs - txTriggerTargetUs);
  encDebug_.txenLagLastUs = txLagUs;
  if (txLagUs > encDebug_.txenLagMaxUs) {
    encDebug_.txenLagMaxUs = txLagUs;
  }
  if (encryptCurrentTx) {
    ++encDebug_.encTxPacketCount;
    encDebug_.encTxenLagLastUs = txLagUs;
    if (txLagUs > encDebug_.encTxenLagMaxUs) {
      encDebug_.encTxenLagMaxUs = txLagUs;
    }
  }
  if (txIsEncRspPlain) {
    encDebug_.encRspTxenLagLastUs = txLagUs;
    if (txLagUs > encDebug_.encRspTxenLagMaxUs) {
      encDebug_.encRspTxenLagMaxUs = txLagUs;
    }
  }
  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  // Keep ENC_RSP timing deterministic while ensuring encrypted follow-up PDUs
  // (e.g. LL_START_ENC_REQ) can be decrypted in the same event window.
  derivePendingEncSessionKey();
  const uint32_t txEndBudgetUs = kBleConnTxDisableWaitUs + 1200U;
  const bool txEndSeen =
      waitRadioEndBudgeted(radio_, txEndBudgetUs, spinLimit / 2U + 1U);
  bool txOk = false;
  if (txEndSeen) {
    if (doPostTxRxTurnaround) {
      // Clear TX END/PHYEND immediately after TX completion so the follow-up
      // listener observes only RX completion events.
      radio_->EVENTS_END = 0U;
      radio_->EVENTS_PHYEND = 0U;
    }
    txOk = waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs,
                                     spinLimit / 2U + 1U);
  }
  if (!txOk) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs, spinLimit / 2U + 1U);
  }
  if (txOk) {
    connectionTxHistoryValid_ = true;
    connectionLastTxSn_ = txSnBit;
  }
  if (!doPostTxRxTurnaround || !txOk) {
    radio_->SHORTS = 0U;
  } else {
    // Clear TX completion events and remove DISABLED->RX shortcut so RX disable
    // completes cleanly once the follow-up listen window ends.
    radio_->EVENTS_DISABLED = 0U;
    radio_->EVENTS_CRCOK = 0U;
    radio_->EVENTS_CRCERROR = 0U;
    radio_->SHORTS =
        ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
         RADIO_SHORTS_RXREADY_START_Msk) |
        ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
          << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
         RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
        ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
          << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
         RADIO_SHORTS_PHYEND_DISABLE_Msk);

    // Defensive fallback: if shortcut path did not enter RX, arm RX manually.
    const uint32_t state =
        (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (state == RADIO_STATE_STATE_Disabled) {
      radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
    }
  }
  if (!txOk) {
    emitBleTrace("EVT_TX_TIMEOUT");
  }

  if (txOk &&
      (connectionLastTxPlainLlid_ == kBlePduLlControl) &&
      (connectionLastTxPlainLength_ >= 1U)) {
    const uint8_t opcode = connectionLastTxPlainPayload_[0];
    if (opcode == kBleLlCtrlEncRsp) {
      ++encDebug_.mainEncRspTxOk;
      // Some centrals do not send LL_START_ENC_REQ in the same event as
      // LL_ENC_RSP. Arm a next-event fallback so the procedure still advances
      // when the immediate follow-up listener sees nothing.
      if (connectionEncStartReqPending_) {
        connectionEncStartReqTxPending_ = true;
      }
    } else if (opcode == kBleLlCtrlStartEncReq) {
      connectionEncStartReqTxPending_ = false;
      connectionEncAwaitingStartRsp_ = true;
      connectionEncRxEnabled_ = true;
      // Tx encryption is enabled only after final LL_START_ENC_RSP is sent.
      connectionEncTxEnabled_ = false;
      connectionEncEnableTxOnNextEvent_ = false;
      uint8_t preLen = 0U;
      connectionEncPrecomputedCounter_ = connectionEncTxCounter_;
      connectionEncPrecomputedEmptyValid_ =
          bleCcmEncryptPayload(connectionEncSessionKey_, connectionEncIv_,
                               connectionEncPrecomputedCounter_,
                               connectionEncTxDirection_,
                               0x01U,  // LLID=1, SN/NESN/MD are masked in MIC.
                               nullptr, 0U,
                               connectionEncPrecomputedPayload_, &preLen) &&
          (preLen == kBleMicLen);
      const uint8_t startRspPlain[1] = {kBleLlCtrlStartEncRsp};
      uint8_t startRspLen = 0U;
      connectionEncPrecomputedStartRspValid_ =
          bleCcmEncryptPayload(connectionEncSessionKey_, connectionEncIv_,
                               connectionEncRxCounter_,
                               connectionEncRxDirection_,
                               0x03U,  // LLID=3, SN/NESN/MD are masked in MIC.
                               startRspPlain, sizeof(startRspPlain),
                               connectionEncPrecomputedStartRsp_, &startRspLen) &&
          (startRspLen == (1U + kBleMicLen));
      connectionEncPrecomputedStartRspTxCounter_ = connectionEncTxCounter_;
      uint8_t startRspTxLen = 0U;
      connectionEncPrecomputedStartRspTxValid_ =
          bleCcmEncryptPayload(connectionEncSessionKey_, connectionEncIv_,
                               connectionEncPrecomputedStartRspTxCounter_,
                               connectionEncTxDirection_,
                               0x03U,  // LLID=3, SN/NESN/MD are masked in MIC.
                               startRspPlain, sizeof(startRspPlain),
                               connectionEncPrecomputedStartRspTx_, &startRspTxLen) &&
          (startRspTxLen == (1U + kBleMicLen));
      emitBleTrace("LL_START_ENC_REQ_TX");
    } else if (opcode == kBleLlCtrlStartEncRsp) {
      ++encDebug_.mainStartEncRspTxOk;
      connectionEncPrecomputedCounter_ = connectionEncTxCounter_;
      uint8_t preLen = 0U;
      connectionEncPrecomputedEmptyValid_ =
          bleCcmEncryptPayload(connectionEncSessionKey_, connectionEncIv_,
                               connectionEncPrecomputedCounter_,
                               connectionEncTxDirection_,
                               0x01U,  // LLID=1, SN/NESN/MD are masked in MIC.
                               nullptr, 0U,
                               connectionEncPrecomputedPayload_, &preLen) &&
          (preLen == kBleMicLen);
    }
  }

  bool lastTxOkForEncEnable = txOk;

  // While awaiting LL_START_ENC_REQ, listen briefly for a post-TX follow-up packet
  // and respond immediately (T_IFS). When doPostTxRxTurnaround is true, RX is already
  // armed in hardware at the TX->RX boundary.
  if (doPostTxRxTurnaround && txOk && !terminateInd) {
    ++encDebug_.followupArmed;
    uint32_t followListenUs = kBleConnFollowupRxListenMaxUs;
    const uint32_t intervalUs =
        static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL;
    if (intervalUs > 3000U) {
      const uint32_t capUs = intervalUs - 3000U;
      if (followListenUs > capUs) {
        followListenUs = capUs;
      }
    }
    const uint32_t followStartUs = bleTimingUs();
    uint8_t followPackets = 0U;
    while (!terminateInd) {
      const uint32_t followNowUs = bleTimingUs();
      const uint32_t elapsedUs =
          static_cast<uint32_t>(followNowUs - followStartUs);
      if ((elapsedUs >= followListenUs) || (followPackets >= 8U)) {
        break;
      }
      const uint32_t remainingUs = followListenUs - elapsedUs;

      const bool followEndSeen =
          waitRadioRxDoneBudgeted(radio_, remainingUs, spinLimit / 2U + 1U);
      if (!followEndSeen) {
        radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
        waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs, spinLimit / 2U + 1U);
        radio_->SHORTS = 0U;
        break;
      }

      ++followPackets;
      ++encDebug_.followupEndSeen;
      const uint32_t followRxEndTimestampUs = bleTimingUs();
      auto transmitImmediateFollowupResponse = [&](uint8_t txFollowLlid,
                                                   uint8_t txFollowLength) -> bool {
        connectionLastTxLlid_ = txFollowLlid;
        connectionLastTxLength_ = txFollowLength;
        connectionLastTxWasEncrypted_ = false;
        connectionLastTxEncryptedLength_ = 0U;
        memset(connectionLastTxEncryptedPayload_, 0,
               sizeof(connectionLastTxEncryptedPayload_));
        connectionLastTxPlainLlid_ = txFollowLlid;
        connectionLastTxPlainLength_ = txFollowLength;
        if (txFollowLength > 0U) {
          memcpy(connectionLastTxPlainPayload_, connectionTxPayload_, txFollowLength);
        }

        txPacket_[0] = static_cast<uint8_t>((txFollowLlid & 0x03U) |
                                            ((connectionExpectedRxSn_ & 0x01U) << 2U) |
                                            ((connectionTxSn_ & 0x01U) << 3U));
        txPacket_[1] = txFollowLength;
        memcpy(&txPacket_[2], connectionTxPayload_, txFollowLength);

        clearRadioCoreEvents(radio_);
        radio_->PACKETPTR =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&txPacket_[0]));
        radio_->SHORTS =
            ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
             RADIO_SHORTS_TXREADY_START_Msk) |
            ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
             RADIO_SHORTS_PHYEND_DISABLE_Msk);

        const uint32_t txFollowTriggerTargetUs =
            followRxEndTimestampUs + kBleConnTxenAfterRxUs;
        uint32_t txFollowTriggerNowUs = bleTimingUs();
        while (!timeReachedUs(txFollowTriggerNowUs, txFollowTriggerTargetUs)) {
          txFollowTriggerNowUs = bleTimingUs();
        }
        radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;

        const uint32_t txFollowEndBudgetUs = kBleConnTxDisableWaitUs + 1200U;
        const bool txFollowEndSeen =
            waitRadioEndBudgeted(radio_, txFollowEndBudgetUs, spinLimit / 2U + 1U);
        bool txFollowOk = false;
        if (txFollowEndSeen) {
          txFollowOk =
              waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs,
                                        spinLimit / 2U + 1U);
        }
        if (!txFollowOk) {
          radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
          waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs,
                                    spinLimit / 2U + 1U);
        }
        radio_->SHORTS = 0U;
        if (txFollowOk) {
          connectionTxHistoryValid_ = true;
          connectionLastTxSn_ =
              static_cast<uint8_t>((txPacket_[0] >> 3U) & 0x01U);
        }
        lastTxOkForEncEnable = txFollowOk;
        return txFollowOk;
      };

      bool followDisabled =
          waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs, spinLimit / 2U + 1U);
      if (!followDisabled) {
        radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
        followDisabled = waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs,
                                                   spinLimit / 2U + 1U);
      }
      radio_->SHORTS = 0U;
      if (!followDisabled) {
        break;
      }

      const uint32_t followCrcStatus =
          (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
          RADIO_CRCSTATUS_CRCSTATUS_Pos;
      const bool followCrcOk =
          (followCrcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
      if (followCrcOk) {
        ++encDebug_.followupCrcOk;
        const uint8_t hdrFollow = rxPacket_[0];
        const uint8_t llidFollow = hdrFollow & 0x03U;
        const uint8_t nesnFollow = (hdrFollow >> 2U) & 0x01U;
        const uint8_t snFollow = (hdrFollow >> 3U) & 0x01U;
        const uint8_t rxLengthFollowRaw = rxPacket_[1];

        const bool peerAckedLastTxFollow =
            connectionTxHistoryValid_ && (nesnFollow != connectionLastTxSn_);
        const bool snMatchesExpectedFollow = (snFollow == connectionExpectedRxSn_);
        const bool canConsumeNewPayloadFollow = snMatchesExpectedFollow;

        bool peerAckedLastTxFollowMutable = peerAckedLastTxFollow;
        bool packetIsNewFollow = false;
        if (canConsumeNewPayloadFollow) {
          connectionExpectedRxSn_ ^= 0x01U;
          packetIsNewFollow = true;
        }
        const bool pendingTxIsAttResponseFollow =
            connectionPendingTxValid_ &&
            (connectionPendingTxLlid_ == kBlePduDataStartOrComplete) &&
            (connectionPendingTxLength_ >= kBleL2capHeaderLen) &&
            (readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidAtt);
        if (!peerAckedLastTxFollowMutable &&
            connectionTxHistoryValid_ &&
            packetIsNewFollow &&
            (((connectionLastTxLlid_ == 0x01U) &&
              (connectionLastTxLength_ == 0U)) ||
             pendingTxIsAttResponseFollow)) {
          peerAckedLastTxFollowMutable = true;
        }
        if (peerAckedLastTxFollowMutable) {
          connectionTxSn_ ^= 0x01U;
        }

        uint8_t rxLengthFollow = rxLengthFollowRaw;
        if (connectionEncSessionValid_ &&
            (connectionEncStartReqPending_ || connectionEncAwaitingStartRsp_) &&
            (llidFollow == kBlePduLlControl) &&
            (rxLengthFollowRaw == static_cast<uint8_t>(1U + kBleMicLen))) {
          uint64_t rxCounter = connectionEncRxCounter_;
          if (!packetIsNewFollow && (rxCounter > 0ULL)) {
            rxCounter -= 1ULL;
          }

          uint8_t plaintext[kBleDataPduMaxPayload] = {0};
          uint8_t plaintextLen = 0U;
          bool followDecryptOk =
              bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                   rxCounter, connectionEncRxDirection_, hdrFollow,
                                   &rxPacket_[2], rxLengthFollowRaw,
                                   plaintext, &plaintextLen);
          if (!followDecryptOk) {
            const uint8_t swappedRxDirection =
                static_cast<uint8_t>(connectionEncRxDirection_ ^ 0x01U);
            if (bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                     rxCounter, swappedRxDirection, hdrFollow,
                                     &rxPacket_[2], rxLengthFollowRaw,
                                     plaintext, &plaintextLen)) {
              followDecryptOk = true;
              connectionEncRxDirection_ = swappedRxDirection;
              connectionEncTxDirection_ = static_cast<uint8_t>(swappedRxDirection ^ 0x01U);
              connectionEncPrecomputedStartRspValid_ = false;
              connectionEncPrecomputedStartRspTxValid_ = false;
              emitBleTrace("ENC_DIR_SWAP");
            } else if (connectionEncAltKeyValid_ &&
                       bleCcmDecryptPayload(connectionEncSessionKeyAlt_, connectionEncIv_,
                                            rxCounter, connectionEncRxDirection_, hdrFollow,
                                            &rxPacket_[2], rxLengthFollowRaw,
                                            plaintext, &plaintextLen)) {
              followDecryptOk = true;
              memcpy(connectionEncSessionKey_, connectionEncSessionKeyAlt_,
                     sizeof(connectionEncSessionKey_));
              connectionEncAltKeyValid_ = false;
              connectionEncPrecomputedStartRspValid_ = false;
              connectionEncPrecomputedStartRspTxValid_ = false;
              emitBleTrace("ENC_KEY_SWAP");
            } else if (connectionEncAltKeyValid_ &&
                       bleCcmDecryptPayload(connectionEncSessionKeyAlt_, connectionEncIv_,
                                            rxCounter, swappedRxDirection, hdrFollow,
                                            &rxPacket_[2], rxLengthFollowRaw,
                                            plaintext, &plaintextLen)) {
              followDecryptOk = true;
              memcpy(connectionEncSessionKey_, connectionEncSessionKeyAlt_,
                     sizeof(connectionEncSessionKey_));
              connectionEncAltKeyValid_ = false;
              connectionEncRxDirection_ = swappedRxDirection;
              connectionEncTxDirection_ = static_cast<uint8_t>(swappedRxDirection ^ 0x01U);
              connectionEncPrecomputedStartRspValid_ = false;
              connectionEncPrecomputedStartRspTxValid_ = false;
              emitBleTrace("ENC_KEY_DIR_SWAP");
            }
          }
          if (followDecryptOk) {
            rxLengthFollow = plaintextLen;
            if (plaintextLen > 0U) {
              memcpy(&rxPacket_[2], plaintext, plaintextLen);
            }
            if (packetIsNewFollow) {
              connectionEncRxCounter_ =
                  (rxCounter + 1ULL) & kBleEncPacketCounterMask;
            }
          }
        }

        if (llidFollow == 0x01U) {
          ++encDebug_.followupRxLlid1;
        } else if (llidFollow == 0x02U) {
          ++encDebug_.followupRxLlid2;
        } else if (llidFollow == 0x03U) {
          ++encDebug_.followupRxLlid3;
        }
        encDebug_.lastFollowHdr = hdrFollow;
        encDebug_.lastFollowLlid = llidFollow;
        encDebug_.lastFollowLen = rxLengthFollow;
        encDebug_.lastFollowByte0 =
            (rxLengthFollow >= 1U) ? rxPacket_[2] : 0U;
        if (packetIsNewFollow && !terminateInd &&
            (llidFollow == kBlePduLlControl) &&
            (rxLengthFollow >= 1U) &&
            (rxPacket_[2] == kBleLlCtrlStartEncReq)) {
          ++encDebug_.followupStartEncReqSeen;
          uint8_t responseLength = 0U;
          bool followTerminate = false;
          if (buildLlControlResponse(&rxPacket_[2], rxLengthFollow,
                                     connectionTxPayload_, &responseLength,
                                     &followTerminate) &&
              responseLength > 0U) {
            if (transmitImmediateFollowupResponse(kBlePduLlControl,
                                                  responseLength)) {
              ++encDebug_.followupStartEncRspTxOk;
            }

            if (followTerminate) {
              terminateInd = true;
            }
          }

          // We handled LL_START_ENC_REQ; don't keep listening in this event.
          break;
        }

        if (packetIsNewFollow && !terminateInd && !connectionPendingTxValid_) {
          uint8_t deferredFollowLlid = 0U;
          uint8_t deferredFollowLength = 0U;
          const bool encryptionProcedureBusyFollow =
              connectionEncSessionValid_ &&
              (connectionEncKeyDerivationPending_ ||
               connectionEncStartReqTxPending_ ||
               connectionEncAwaitingStartRsp_ ||
               connectionEncEnableTxOnNextEvent_ ||
               connectionEncStartReqPending_);

          if (!encryptionProcedureBusyFollow &&
              (llidFollow == kBlePduLlControl) &&
              (rxLengthFollow >= 1U)) {
            if (buildLlControlResponse(&rxPacket_[2], rxLengthFollow,
                                       connectionTxPayload_,
                                       &deferredFollowLength,
                                       &terminateInd) &&
                (deferredFollowLength > 0U)) {
              transmitImmediateFollowupResponse(kBlePduLlControl,
                                                deferredFollowLength);
              break;
            }
          } else if (!encryptionProcedureBusyFollow &&
                     (llidFollow == kBlePduDataStartOrComplete) &&
                     (rxLengthFollow >= kBleL2capHeaderLen)) {
            if (buildL2capResponse(&rxPacket_[2], rxLengthFollow,
                                   connectionTxPayload_,
                                   &deferredFollowLength) &&
                (deferredFollowLength > 0U)) {
              deferredFollowLlid = kBlePduDataStartOrComplete;
            }
          }

          if (!terminateInd && (deferredFollowLength > 0U)) {
            connectionPendingTxLlid_ = deferredFollowLlid;
            connectionPendingTxLength_ = deferredFollowLength;
            memcpy(connectionPendingTxPayload_, connectionTxPayload_,
                   deferredFollowLength);
            connectionPendingTxValid_ = true;
          }
        }
      }

      // Re-arm RX for another follow-up packet in the same event.
      clearRadioCoreEvents(radio_);
      radio_->PACKETPTR =
          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
      radio_->SHORTS =
          ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
           RADIO_SHORTS_RXREADY_START_Msk) |
          ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
            << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
           RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
          ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
           RADIO_SHORTS_PHYEND_DISABLE_Msk);
      radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
    }

    clearRadioCoreEvents(radio_);
  }

  // Fallback in case TX path did not run to completion in this event.
  derivePendingEncSessionKey();

  // Awaiting LL_START_ENC_RSP is latency-critical for the ACK path: transmit the
  // ACK first, then validate/decode the encrypted control PDU.
  if (!terminateInd &&
      deferAwaitingStartRspDecrypt &&
      !fastStartEncRspMatched &&
      packetIsNew &&
      connectionEncSessionValid_ &&
      connectionEncAwaitingStartRsp_) {
    uint8_t plaintext[kBleDataPduMaxPayload] = {0};
    uint8_t plaintextLen = 0U;
    const uint64_t rxCounter = connectionEncRxCounter_;
    bool decryptOk = bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                          rxCounter, connectionEncRxDirection_, hdr0,
                                          &rxPacket_[2], rxLengthRaw,
                                          plaintext, &plaintextLen);
    if (!decryptOk) {
      const uint8_t swappedRxDirection =
          static_cast<uint8_t>(connectionEncRxDirection_ ^ 0x01U);
      if (bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                               rxCounter, swappedRxDirection, hdr0,
                               &rxPacket_[2], rxLengthRaw,
                               plaintext, &plaintextLen)) {
        decryptOk = true;
        connectionEncRxDirection_ = swappedRxDirection;
        connectionEncTxDirection_ = static_cast<uint8_t>(swappedRxDirection ^ 0x01U);
        connectionEncPrecomputedStartRspValid_ = false;
        connectionEncPrecomputedStartRspTxValid_ = false;
        emitBleTrace("ENC_DIR_SWAP");
      } else if (connectionEncAltKeyValid_ &&
                 bleCcmDecryptPayload(connectionEncSessionKeyAlt_, connectionEncIv_,
                                      rxCounter, connectionEncRxDirection_, hdr0,
                                      &rxPacket_[2], rxLengthRaw,
                                      plaintext, &plaintextLen)) {
        decryptOk = true;
        memcpy(connectionEncSessionKey_, connectionEncSessionKeyAlt_,
               sizeof(connectionEncSessionKey_));
        connectionEncAltKeyValid_ = false;
        connectionEncPrecomputedStartRspValid_ = false;
        connectionEncPrecomputedStartRspTxValid_ = false;
        emitBleTrace("ENC_KEY_SWAP");
      } else if (connectionEncAltKeyValid_ &&
                 bleCcmDecryptPayload(connectionEncSessionKeyAlt_, connectionEncIv_,
                                      rxCounter, swappedRxDirection, hdr0,
                                      &rxPacket_[2], rxLengthRaw,
                                      plaintext, &plaintextLen)) {
        decryptOk = true;
        memcpy(connectionEncSessionKey_, connectionEncSessionKeyAlt_,
               sizeof(connectionEncSessionKey_));
        connectionEncAltKeyValid_ = false;
        connectionEncRxDirection_ = swappedRxDirection;
        connectionEncTxDirection_ = static_cast<uint8_t>(swappedRxDirection ^ 0x01U);
        connectionEncPrecomputedStartRspValid_ = false;
        connectionEncPrecomputedStartRspTxValid_ = false;
        emitBleTrace("ENC_KEY_DIR_SWAP");
      }
    }

    if (!decryptOk) {
      terminateInd = true;
      terminateMicFailure = true;
      ++encDebug_.encRxMicFailCount;
      encDebug_.encRxLastMicFailHdr = hdr0;
      encDebug_.encRxLastMicFailLenRaw = rxLengthRaw;
      encDebug_.encRxLastMicFailDir = connectionEncRxDirection_;
      encDebug_.encRxLastMicFailCounterLo =
          static_cast<uint32_t>(rxCounter & 0xFFFFFFFFULL);
      emitBleTrace("ENC_RX_MIC_FAIL_POSTTX");
    } else {
      rxWasDecrypted = true;
      rxLength = plaintextLen;
      if (plaintextLen > 0U) {
        memcpy(&rxPacket_[2], plaintext, plaintextLen);
      }
      if (packetIsNew) {
        connectionEncRxCounter_ = (rxCounter + 1ULL) & kBleEncPacketCounterMask;
      }

      if ((llid == kBlePduLlControl) &&
          (rxLength >= 1U) &&
          (rxPacket_[2] == kBleLlCtrlStartEncRsp)) {
        // Keep procedure state unchanged here so the normal LL control
        // response path can queue/transmit final LL_START_ENC_RSP.
        emitBleTrace("LL_START_ENC_RSP_RX_POSTTX");
      }
    }
  }

  if (deferEncryptedDataDecrypt && !terminateInd && packetIsNew) {
    const uint64_t rxCounter = connectionEncRxCounter_;
    uint8_t plaintext[kBleDataPduMaxPayload] = {0};
    uint8_t plaintextLen = 0U;
    const bool decryptOk =
        bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                            rxCounter, connectionEncRxDirection_, hdr0,
                            &rxPacket_[2], rxLengthRaw,
                            plaintext, &plaintextLen);

    if (!decryptOk) {
      terminateInd = true;
      terminateMicFailure = true;
      ++encDebug_.encRxMicFailCount;
      encDebug_.encRxLastMicFailHdr = hdr0;
      encDebug_.encRxLastMicFailLenRaw = rxLengthRaw;
      encDebug_.encRxLastMicFailDir = connectionEncRxDirection_;
      encDebug_.encRxLastMicFailCounterLo =
          static_cast<uint32_t>(rxCounter & 0xFFFFFFFFULL);
      emitBleTrace("ENC_RX_MIC_FAIL_POSTTX_DATA");
    } else {
      rxWasDecrypted = true;
      rxLength = plaintextLen;
      if (plaintextLen > 0U) {
        memcpy(&rxPacket_[2], plaintext, plaintextLen);
      }
      connectionEncRxCounter_ = (rxCounter + 1ULL) & kBleEncPacketCounterMask;
    }
  }

  encDebug_.encLastRxHdr = hdr0;
  encDebug_.encLastRxLenRaw = rxLengthRaw;
  encDebug_.encLastRxWasNew = packetIsNew ? 1U : 0U;
  encDebug_.encLastRxWasDecrypted = rxWasDecrypted ? 1U : 0U;
  encDebug_.encLastRxCounterLo =
      static_cast<uint32_t>(connectionEncRxCounter_ & 0xFFFFFFFFULL);

  if (packetIsNew && !terminateInd) {
    uint8_t deferredLlid = 0x01U;
    uint8_t deferredLength = 0U;
    const bool encryptionProcedureBusy =
        connectionEncSessionValid_ &&
        (connectionEncKeyDerivationPending_ ||
         connectionEncStartReqTxPending_ ||
         connectionEncAwaitingStartRsp_ ||
         connectionEncEnableTxOnNextEvent_ ||
         connectionEncStartReqPending_);

    if (connectionEncSessionValid_ &&
        connectionEncStartReqPending_ &&
        connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionPendingTxValid_) {
      connectionTxPayload_[0] = kBleLlCtrlStartEncReq;
      deferredLlid = kBlePduLlControl;
      deferredLength = 1U;
    } else if (!llControlHandledImmediate &&
        llid == kBlePduLlControl && rxLength >= 1U) {
      uint8_t responseLength = 0U;
      if (buildLlControlResponse(&rxPacket_[2], rxLength, connectionTxPayload_,
                                 &responseLength, &terminateInd) &&
          responseLength > 0U) {
        deferredLlid = kBlePduLlControl;
        deferredLength = responseLength;
      }
    } else if (!encryptionProcedureBusy &&
               llid == kBlePduDataStartOrComplete &&
               rxLength >= kBleL2capHeaderLen) {
      uint8_t responseLength = 0U;
      if (buildL2capResponse(&rxPacket_[2], rxLength, connectionTxPayload_,
                             &responseLength) &&
          responseLength > 0U) {
        deferredLlid = kBlePduDataStartOrComplete;
        deferredLength = responseLength;
      }
    }

    if ((deferredLength == 0U) &&
        !encryptionProcedureBusy &&
        !connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionEncStartReqPending_ &&
        connectionServiceChangedIndicationsEnabled_ &&
        connectionServiceChangedIndicationPending_ &&
        !connectionServiceChangedIndicationAwaitingConfirm_) {
      writeLe16(&connectionTxPayload_[0], 7U);
      writeLe16(&connectionTxPayload_[2], kBleL2capCidAtt);
      connectionTxPayload_[4] = kAttOpHandleValueInd;
      writeLe16(&connectionTxPayload_[5], kHandleGattServiceChangedValue);
      writeLe16(&connectionTxPayload_[7], 0x0001U);
      writeLe16(&connectionTxPayload_[9], 0xFFFFU);
      deferredLlid = kBlePduDataStartOrComplete;
      deferredLength = 11U;
      connectionServiceChangedIndicationPending_ = false;
      connectionServiceChangedIndicationAwaitingConfirm_ = true;
    }

    if ((deferredLength == 0U) &&
        !encryptionProcedureBusy &&
        !connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionEncStartReqPending_ &&
        connectionCustomNotificationPending_) {
      if (connectionCustomPendingCharIndex_ < customGattCharacteristicCount_) {
        BleCustomCharacteristicState& custom =
            customGattCharacteristics_[connectionCustomPendingCharIndex_];
        const bool modeEnabled = connectionCustomPendingIndication_
                                     ? (((custom.properties & kBleGattPropIndicate) != 0U) &&
                                        ((custom.cccdValue & 0x0002U) != 0U))
                                     : (((custom.properties & kBleGattPropNotify) != 0U) &&
                                        ((custom.cccdValue & 0x0001U) != 0U));
        const bool indicationFlowReady =
            !connectionServiceChangedIndicationAwaitingConfirm_ &&
            (connectionCustomIndicationAwaitingHandle_ == 0U);
        const bool canTransmit =
            modeEnabled &&
            (!connectionCustomPendingIndication_ || indicationFlowReady);
        if (canTransmit) {
          writeLe16(&connectionTxPayload_[0],
                    static_cast<uint16_t>(3U + custom.valueLength));
          writeLe16(&connectionTxPayload_[2], kBleL2capCidAtt);
          connectionTxPayload_[4] = connectionCustomPendingIndication_
                                        ? kAttOpHandleValueInd
                                        : kAttOpHandleValueNtf;
          writeLe16(&connectionTxPayload_[5], custom.valueHandle);
          if (custom.valueLength > 0U) {
            memcpy(&connectionTxPayload_[7], custom.value, custom.valueLength);
          }
          deferredLlid = kBlePduDataStartOrComplete;
          deferredLength = static_cast<uint8_t>(7U + custom.valueLength);
          if (connectionCustomPendingIndication_) {
            connectionCustomIndicationAwaitingHandle_ = custom.valueHandle;
          }
          connectionCustomNotificationPending_ = false;
          connectionCustomPendingCharIndex_ = 0xFFU;
          connectionCustomPendingIndication_ = false;
        } else if (!modeEnabled) {
          connectionCustomNotificationPending_ = false;
          connectionCustomPendingCharIndex_ = 0xFFU;
          connectionCustomPendingIndication_ = false;
        }
      } else {
        connectionCustomNotificationPending_ = false;
        connectionCustomPendingCharIndex_ = 0xFFU;
        connectionCustomPendingIndication_ = false;
      }
    }

    if ((deferredLength == 0U) &&
        !encryptionProcedureBusy &&
        !connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionEncStartReqPending_ &&
        connectionBatteryNotificationsEnabled_ &&
        connectionBatteryNotificationPending_) {
      writeLe16(&connectionTxPayload_[0], 4U);
      writeLe16(&connectionTxPayload_[2], kBleL2capCidAtt);
      connectionTxPayload_[4] = kAttOpHandleValueNtf;
      writeLe16(&connectionTxPayload_[5], kHandleBatteryLevelValue);
      connectionTxPayload_[7] = gapBatteryLevel_;
      deferredLlid = kBlePduDataStartOrComplete;
      deferredLength = 8U;
      connectionBatteryNotificationPending_ = false;
    }

    if (!terminateInd && deferredLength > 0U) {
      connectionPendingTxLlid_ = deferredLlid;
      connectionPendingTxLength_ = deferredLength;
      memcpy(connectionPendingTxPayload_, connectionTxPayload_, deferredLength);
      connectionPendingTxValid_ = true;
    }
  }

  if (event != nullptr) {
    event->txPacketSent = txOk;
    event->txLlid = txLlid;
    event->txNesn = txNesnBit;
    event->txSn = txSnBit;
    event->txPayloadLength = txLength;
    event->txPayload =
        (txLength > 0U) ? &connectionLastTxPlainPayload_[0] : nullptr;
    event->emptyAckTransmitted = txOk && (txLlid == 0x01U) && (txLength == 0U);
    event->terminateInd = terminateInd;
  }

  if (connectionEncEnableTxOnNextEvent_ && lastTxOkForEncEnable && !terminateInd) {
    connectionEncTxEnabled_ = true;
    connectionEncEnableTxOnNextEvent_ = false;
  }

  if (terminateInd) {
    disconnect(spinLimit / 2U + 1U);
  }

  clearRadioCoreEvents(radio_);
  return true;
}

bool BleRadio::scanOnce(BleAdvertisingChannel channel, BleScanPacket* packet,
                        uint32_t spinLimit) {
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }
  if (!initialized_ || radio_ == nullptr || connected_) {
    endUnconnectedRadioActivity();
    return false;
  }
  if (!setAdvertisingChannel(channel)) {
    endUnconnectedRadioActivity();
    return false;
  }

  clearRadioCoreEvents(radio_);
  memset(rxPacket_, 0, sizeof(rxPacket_));
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));

  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;

  const bool endSeen = waitForEnd(spinLimit);
  if (!endSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    endUnconnectedRadioActivity();
    return false;
  }

  const bool disabled = waitDisabled(spinLimit / 2U + 1U);
  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOk = (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);

  radio_->SHORTS = 0U;
  if (!disabled || !crcOk) {
    clearRadioCoreEvents(radio_);
    endUnconnectedRadioActivity();
    return false;
  }

  const uint8_t payloadLen = static_cast<uint8_t>(rxPacket_[1] & 0x3FU);
  if (packet != nullptr) {
    packet->channel = channel;
    packet->rssiDbm = radioRssiDbm(radio_);
    packet->pduHeader = rxPacket_[0];
    packet->length = payloadLen;
    packet->payload = &rxPacket_[2];
  }

  clearRadioCoreEvents(radio_);
  endUnconnectedRadioActivity();
  return true;
}

bool BleRadio::scanCycle(BleScanPacket* packet, uint32_t perChannelSpinLimit) {
  static constexpr BleAdvertisingChannel kChannels[3] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };

  const uint32_t firstPassSpin = perChannelSpinLimit;
  const uint32_t secondPassSpin =
      (perChannelSpinLimit > 1U) ? (perChannelSpinLimit / 2U) : perChannelSpinLimit;

  for (uint8_t pass = 0U; pass < 2U; ++pass) {
    const uint32_t dwell = (pass == 0U) ? firstPassSpin : secondPassSpin;
    for (uint8_t i = 0U; i < 3U; ++i) {
      const uint8_t idx = static_cast<uint8_t>((scanCycleStartIndex_ + i) % 3U);
      if (scanOnce(kChannels[idx], packet, dwell)) {
        scanCycleStartIndex_ = static_cast<uint8_t>((idx + 1U) % 3U);
        return true;
      }
    }
  }

  scanCycleStartIndex_ = static_cast<uint8_t>((scanCycleStartIndex_ + 1U) % 3U);
  return false;
}

bool BleRadio::receivePacketOnCurrentChannel(uint32_t listenSpinLimit,
                                             uint32_t spinLimit,
                                             uint8_t* outHeader,
                                             uint8_t* outPayloadLength,
                                             const uint8_t** outPayload,
                                             int8_t* outRssiDbm,
                                             uint32_t* outStartUs,
                                             uint32_t* outEndUs) {
  if (!initialized_ || radio_ == nullptr || connected_) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  memset(rxPacket_, 0, sizeof(rxPacket_));
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;

  bool addressSeen = false;
  bool endSeen = false;
  uint32_t packetStartUs = 0U;
  uint32_t remaining = listenSpinLimit;
  while (remaining-- > 0U) {
    if (!addressSeen && (radio_->EVENTS_ADDRESS != 0U)) {
      addressSeen = true;
      packetStartUs = bleTimingUs();
    }
    if ((radio_->EVENTS_END != 0U) || (radio_->EVENTS_PHYEND != 0U)) {
      endSeen = true;
      break;
    }
  }
  if (!endSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return false;
  }
  if ((outStartUs != nullptr) && addressSeen) {
    *outStartUs = packetStartUs;
  }
  if (outEndUs != nullptr) {
    *outEndUs = bleTimingUs();
  }

  bool disabled = waitDisabled(spinLimit / 2U + 1U);
  if (!disabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    disabled = waitDisabled(spinLimit / 2U + 1U);
  }
  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOk = (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  const int8_t rssiDbm = radioRssiDbm(radio_);
  radio_->SHORTS = 0U;
  if (!disabled || !crcOk) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  if (outHeader != nullptr) {
    *outHeader = rxPacket_[0];
  }
  if (outPayloadLength != nullptr) {
    *outPayloadLength = rxPacket_[1];
  }
  if (outPayload != nullptr) {
    *outPayload = &rxPacket_[2];
  }
  if (outRssiDbm != nullptr) {
    *outRssiDbm = rssiDbm;
  }

  clearRadioCoreEvents(radio_);
  return true;
}

bool BleRadio::scanActiveOnce(BleAdvertisingChannel channel,
                              BleActiveScanResult* result,
                              uint32_t advListenSpinLimit,
                              uint32_t scanRspListenSpinLimit,
                              uint32_t spinLimit) {
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }
  if (!initialized_ || radio_ == nullptr || connected_) {
    endUnconnectedRadioActivity();
    return false;
  }
  if (!setAdvertisingChannel(channel)) {
    endUnconnectedRadioActivity();
    return false;
  }

  if (result != nullptr) {
    memset(result, 0, sizeof(*result));
    result->channel = channel;
    result->scanRspRssiDbm = -127;
  }

  clearRadioCoreEvents(radio_);
  memset(rxPacket_, 0, sizeof(rxPacket_));
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;

  const bool endSeen = waitForEnd(advListenSpinLimit);
  if (!endSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    endUnconnectedRadioActivity();
    return false;
  }
  const uint32_t advEndUs = bleTimingUs();

  bool disabled = waitDisabled(spinLimit / 2U + 1U);
  if (!disabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    disabled = waitDisabled(spinLimit / 2U + 1U);
  }
  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOk = (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  const int8_t advRssi = radioRssiDbm(radio_);
  radio_->SHORTS = 0U;
  if (!disabled || !crcOk) {
    clearRadioCoreEvents(radio_);
    endUnconnectedRadioActivity();
    return false;
  }

  const uint8_t advHeader = rxPacket_[0];
  const uint8_t advPduType = static_cast<uint8_t>(advHeader & 0x0FU);
  const uint8_t advPayloadLen = static_cast<uint8_t>(rxPacket_[1] & 0x3FU);
  const bool advertiserAddressRandom = ((advHeader >> 6U) & 0x1U) != 0U;
  const uint8_t* advPayload = &rxPacket_[2];

  const bool isAdvertisingPdu =
      (advPduType == static_cast<uint8_t>(BleAdvPduType::kAdvInd)) ||
      (advPduType == static_cast<uint8_t>(BleAdvPduType::kAdvDirectInd)) ||
      (advPduType == static_cast<uint8_t>(BleAdvPduType::kAdvNonConnInd)) ||
      (advPduType == static_cast<uint8_t>(BleAdvPduType::kAdvScanInd));
  if (!isAdvertisingPdu || (advPayloadLen < 6U)) {
    clearRadioCoreEvents(radio_);
    endUnconnectedRadioActivity();
    return false;
  }

  const uint8_t copyAdvLen =
      (advPayloadLen <= static_cast<uint8_t>(sizeof(result->advPayload)))
          ? advPayloadLen
          : static_cast<uint8_t>(sizeof(result->advPayload));
  if (result != nullptr) {
    result->advRssiDbm = advRssi;
    result->advHeader = advHeader;
    result->advPayloadLength = copyAdvLen;
    result->advertiserAddressRandom = advertiserAddressRandom;
    memcpy(result->advertiserAddress, advPayload, sizeof(result->advertiserAddress));
    if (copyAdvLen > 0U) {
      memcpy(result->advPayload, advPayload, copyAdvLen);
    }
  }

  const bool scannable =
      (advPduType == static_cast<uint8_t>(BleAdvPduType::kAdvInd)) ||
      (advPduType == static_cast<uint8_t>(BleAdvPduType::kAdvScanInd));
  if (!scannable) {
    clearRadioCoreEvents(radio_);
    endUnconnectedRadioActivity();
    return true;
  }

  // Active scan request: [ScanA (local)] [AdvA (remote)]
  const bool scannerAddressRandom = (addressType_ == BleAddressType::kRandomStatic);
  txPacket_[0] = static_cast<uint8_t>(kBlePduScanReq |
                                      ((scannerAddressRandom ? 1U : 0U) << 6U) |
                                      ((advertiserAddressRandom ? 1U : 0U) << 7U));
  txPacket_[1] = 12U;
  memcpy(&txPacket_[2], address_, 6U);
  memcpy(&txPacket_[8], advPayload, 6U);

  clearRadioCoreEvents(radio_);
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&txPacket_[0]));
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  const uint32_t txTriggerTargetUs = advEndUs + kBleConnTxenAfterRxUs;
  uint32_t txTriggerNowUs = bleTimingUs();
  while (!timeReachedUs(txTriggerNowUs, txTriggerTargetUs)) {
    txTriggerNowUs = bleTimingUs();
  }

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool txEndSeen = waitForEnd(spinLimit / 2U + 1U);
  bool txDisabled = false;
  if (txEndSeen) {
    txDisabled = waitDisabled(spinLimit / 2U + 1U);
  }
  if (!txDisabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    txDisabled = waitDisabled(spinLimit / 2U + 1U);
  }
  radio_->SHORTS = 0U;
  if (!txDisabled) {
    clearRadioCoreEvents(radio_);
    endUnconnectedRadioActivity();
    return true;
  }

  // Wait for SCAN_RSP on the same channel.
  clearRadioCoreEvents(radio_);
  memset(rxPacket_, 0, sizeof(rxPacket_));
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;

  const bool rspEndSeen = waitRadioEndBudgeted(
      radio_, kBleScanRspListenMaxUs, scanRspListenSpinLimit);
  if (!rspEndSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    endUnconnectedRadioActivity();
    return true;
  }

  bool rspDisabled = waitDisabled(spinLimit / 2U + 1U);
  if (!rspDisabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    rspDisabled = waitDisabled(spinLimit / 2U + 1U);
  }
  const uint32_t rspCrcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool rspCrcOk = (rspCrcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  const int8_t rspRssi = radioRssiDbm(radio_);
  radio_->SHORTS = 0U;
  if (!rspDisabled || !rspCrcOk) {
    clearRadioCoreEvents(radio_);
    endUnconnectedRadioActivity();
    return true;
  }

  const uint8_t rspHeader = rxPacket_[0];
  const uint8_t rspPduType = static_cast<uint8_t>(rspHeader & 0x0FU);
  const uint8_t rspPayloadLen = static_cast<uint8_t>(rxPacket_[1] & 0x3FU);
  const bool rspAddrRandom = ((rspHeader >> 6U) & 0x1U) != 0U;
  const uint8_t* rspPayload = &rxPacket_[2];

  const bool looksLikeMatchingScanRsp =
      (rspPduType == kBlePduScanRsp) &&
      (rspPayloadLen >= 6U) &&
      (rspAddrRandom == advertiserAddressRandom) &&
      bleAddressEqual(rspPayload, advPayload);
  if (looksLikeMatchingScanRsp && (result != nullptr)) {
    const uint8_t copyRspLen =
        (rspPayloadLen <= static_cast<uint8_t>(sizeof(result->scanRspPayload)))
            ? rspPayloadLen
            : static_cast<uint8_t>(sizeof(result->scanRspPayload));
    result->scanResponseReceived = true;
    result->scanRspRssiDbm = rspRssi;
    result->scanRspHeader = rspHeader;
    result->scanRspPayloadLength = copyRspLen;
    if (copyRspLen > 0U) {
      memcpy(result->scanRspPayload, rspPayload, copyRspLen);
    }
  }

  clearRadioCoreEvents(radio_);
  endUnconnectedRadioActivity();
  return true;
}

bool BleRadio::scanActiveCycle(BleActiveScanResult* result,
                               uint32_t perChannelAdvListenSpinLimit,
                               uint32_t scanRspListenSpinLimit) {
  static constexpr BleAdvertisingChannel kChannels[3] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };

  const uint32_t firstPassSpin = perChannelAdvListenSpinLimit;
  const uint32_t secondPassSpin =
      (perChannelAdvListenSpinLimit > 1U)
          ? (perChannelAdvListenSpinLimit / 2U)
          : perChannelAdvListenSpinLimit;

  for (uint8_t pass = 0U; pass < 2U; ++pass) {
    const uint32_t dwell = (pass == 0U) ? firstPassSpin : secondPassSpin;
    for (uint8_t i = 0U; i < 3U; ++i) {
      const uint8_t idx = static_cast<uint8_t>((scanCycleStartIndex_ + i) % 3U);
      if (scanActiveOnce(kChannels[idx], result, dwell, scanRspListenSpinLimit,
                         dwell + scanRspListenSpinLimit + 200000UL)) {
        scanCycleStartIndex_ = static_cast<uint8_t>((idx + 1U) % 3U);
        return true;
      }
    }
  }

  scanCycleStartIndex_ = static_cast<uint8_t>((scanCycleStartIndex_ + 1U) % 3U);
  return false;
}

bool BleRadio::scanExtendedOnce(BleAdvertisingChannel channel,
                                BleExtendedScanResult* result,
                                uint32_t primaryListenSpinLimit,
                                uint32_t secondaryListenSpinLimit,
                                uint32_t spinLimit) {
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }
  if (!initialized_ || radio_ == nullptr || connected_) {
    endUnconnectedRadioActivity();
    return false;
  }
  if (!setAdvertisingChannel(channel)) {
    endUnconnectedRadioActivity();
    return false;
  }

  if (result != nullptr) {
    memset(result, 0, sizeof(*result));
    result->primaryChannel = channel;
  }

  uint8_t primaryHeader = 0U;
  uint8_t primaryPayloadLength = 0U;
  const uint8_t* primaryPayload = nullptr;
  int8_t primaryRssi = -127;
  uint32_t primaryStartUs = 0U;
  if (!receivePacketOnCurrentChannel(primaryListenSpinLimit, spinLimit, &primaryHeader,
                                     &primaryPayloadLength, &primaryPayload,
                                     &primaryRssi, &primaryStartUs,
                                     nullptr)) {
    endUnconnectedRadioActivity();
    return false;
  }

  if (static_cast<uint8_t>(primaryHeader & 0x0FU) != kBlePduAdvExtInd) {
    endUnconnectedRadioActivity();
    return false;
  }

  BleExtendedHeaderView primaryView{};
  if (!parseBleExtendedHeader(primaryPayload, primaryPayloadLength, &primaryView) ||
      !primaryView.valid ||
      (primaryView.advMode != kBleExtendedAdvModeNonConnNonScan) ||
      !primaryView.hasAdvA || !primaryView.hasAuxPtr || !primaryView.hasAdi ||
      (primaryView.auxPtr.phy != kBleExtendedAuxPhy1M)) {
    endUnconnectedRadioActivity();
    return false;
  }

  if (result != nullptr) {
    result->primaryRssiDbm = primaryRssi;
    result->primaryHeader = primaryHeader;
    result->primaryPayloadLength = primaryPayloadLength;
    result->advMode = primaryView.advMode;
    result->advertiserAddressRandom = ((primaryHeader >> 6U) & 0x1U) != 0U;
    memcpy(result->advertiserAddress, primaryView.advA, sizeof(result->advertiserAddress));
    result->did = primaryView.did;
    result->sid = primaryView.sid;
    result->auxChannel = primaryView.auxPtr.channel;
    result->auxPhy = primaryView.auxPtr.phy;
    result->auxOffsetUs = primaryView.auxPtr.offsetUs;
    result->secondaryPacketCount = 0U;
    result->dataLength = 0U;
  }

  uint16_t dataLength = 0U;
  if (primaryView.advDataLength > 0U) {
    dataLength = primaryView.advDataLength;
    if (result != nullptr) {
      memcpy(result->data, primaryView.advData, primaryView.advDataLength);
      result->dataLength = dataLength;
    }
  }

  BleExtendedAuxPtrView nextAux = primaryView.auxPtr;
  uint32_t previousPacketStartUs = primaryStartUs;
  uint8_t secondaryPacketCount = 0U;
  bool expectAnotherPacket = true;

  while (expectAnotherPacket) {
    if (secondaryPacketCount >= kBleExtendedSecondaryPacketCountMax) {
      endUnconnectedRadioActivity();
      return false;
    }
    if (!setDataChannel(nextAux.channel)) {
      endUnconnectedRadioActivity();
      return false;
    }

    const uint32_t targetUs = previousPacketStartUs + nextAux.offsetUs;
    const uint32_t armUs = targetUs - kBleExtendedAuxRxLeadUs;
    uint32_t nowUs = bleTimingUs();
    while (!timeReachedUs(nowUs, armUs)) {
      nowUs = bleTimingUs();
    }

    uint8_t secondaryHeader = 0U;
    uint8_t secondaryPayloadLength = 0U;
    const uint8_t* secondaryPayload = nullptr;
    int8_t secondaryRssi = -127;
    uint32_t secondaryStartUs = 0U;
    if (!receivePacketOnCurrentChannel(secondaryListenSpinLimit, spinLimit,
                                       &secondaryHeader, &secondaryPayloadLength,
                                       &secondaryPayload, &secondaryRssi,
                                       &secondaryStartUs,
                                       nullptr)) {
      endUnconnectedRadioActivity();
      return false;
    }
    if (static_cast<uint8_t>(secondaryHeader & 0x0FU) != kBlePduAdvExtInd) {
      endUnconnectedRadioActivity();
      return false;
    }

    BleExtendedHeaderView secondaryView{};
    if (!parseBleExtendedHeader(secondaryPayload, secondaryPayloadLength, &secondaryView) ||
        !secondaryView.valid || !secondaryView.hasAdi ||
        (secondaryView.did != primaryView.did) ||
        (secondaryView.sid != primaryView.sid)) {
      endUnconnectedRadioActivity();
      return false;
    }

    if ((static_cast<uint32_t>(dataLength) + secondaryView.advDataLength) >
        kBleExtendedAdvDataMaxLength) {
      endUnconnectedRadioActivity();
      return false;
    }
    if ((secondaryView.advDataLength > 0U) && (result != nullptr)) {
      memcpy(&result->data[dataLength], secondaryView.advData,
             secondaryView.advDataLength);
    }
    dataLength = static_cast<uint16_t>(dataLength + secondaryView.advDataLength);
    ++secondaryPacketCount;
    if (result != nullptr) {
      result->dataLength = dataLength;
      result->secondaryPacketCount = secondaryPacketCount;
      result->auxChannel = nextAux.channel;
      result->auxPhy = nextAux.phy;
      result->auxOffsetUs = nextAux.offsetUs;
      result->primaryRssiDbm = primaryRssi;
      (void)secondaryRssi;
    }

    if (!secondaryView.hasAuxPtr) {
      expectAnotherPacket = false;
      break;
    }
    if (secondaryView.auxPtr.phy != kBleExtendedAuxPhy1M) {
      endUnconnectedRadioActivity();
      return false;
    }

    nextAux = secondaryView.auxPtr;
    previousPacketStartUs = secondaryStartUs;
  }

  endUnconnectedRadioActivity();
  return (secondaryPacketCount > 0U);
}

bool BleRadio::scanExtendedCycle(BleExtendedScanResult* result,
                                 uint32_t perChannelPrimaryListenSpinLimit,
                                 uint32_t secondaryListenSpinLimit) {
  static constexpr BleAdvertisingChannel kChannels[3] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };

  const uint32_t firstPassSpin = perChannelPrimaryListenSpinLimit;
  const uint32_t secondPassSpin =
      (perChannelPrimaryListenSpinLimit > 1U)
          ? (perChannelPrimaryListenSpinLimit / 2U)
          : perChannelPrimaryListenSpinLimit;

  for (uint8_t pass = 0U; pass < 2U; ++pass) {
    const uint32_t dwell = (pass == 0U) ? firstPassSpin : secondPassSpin;
    for (uint8_t i = 0U; i < 3U; ++i) {
      const uint8_t idx = static_cast<uint8_t>((scanCycleStartIndex_ + i) % 3U);
      if (scanExtendedOnce(kChannels[idx], result, dwell, secondaryListenSpinLimit,
                           dwell + secondaryListenSpinLimit + 300000UL)) {
        scanCycleStartIndex_ = static_cast<uint8_t>((idx + 1U) % 3U);
        return true;
      }
    }
  }

  scanCycleStartIndex_ = static_cast<uint8_t>((scanCycleStartIndex_ + 1U) % 3U);
  return false;
}

bool BleRadio::scanExtendedActiveOnce(BleAdvertisingChannel channel,
                                      BleExtendedScanResult* result,
                                      uint32_t primaryListenSpinLimit,
                                      uint32_t secondaryListenSpinLimit,
                                      uint32_t scanRspListenSpinLimit,
                                      uint32_t spinLimit) {
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }
  if (!initialized_ || radio_ == nullptr || connected_) {
    endUnconnectedRadioActivity();
    return false;
  }
  if (!setAdvertisingChannel(channel)) {
    endUnconnectedRadioActivity();
    return false;
  }

  if (result != nullptr) {
    memset(result, 0, sizeof(*result));
    result->primaryChannel = channel;
  }

  uint8_t primaryHeader = 0U;
  uint8_t primaryPayloadLength = 0U;
  const uint8_t* primaryPayload = nullptr;
  int8_t primaryRssi = -127;
  uint32_t primaryStartUs = 0U;
  uint32_t primaryEndUs = 0U;
  if (!receivePacketOnCurrentChannel(primaryListenSpinLimit, spinLimit, &primaryHeader,
                                     &primaryPayloadLength, &primaryPayload,
                                     &primaryRssi, &primaryStartUs,
                                     &primaryEndUs)) {
    endUnconnectedRadioActivity();
    return false;
  }

  if (static_cast<uint8_t>(primaryHeader & 0x0FU) != kBlePduAdvExtInd) {
    endUnconnectedRadioActivity();
    return false;
  }

  BleExtendedHeaderView primaryView{};
  if (!parseBleExtendedHeader(primaryPayload, primaryPayloadLength, &primaryView) ||
      !primaryView.valid ||
      (primaryView.advMode != kBleExtendedAdvModeNonConnScan) ||
      !primaryView.hasAuxPtr || !primaryView.hasAdi ||
      (primaryView.auxPtr.phy != kBleExtendedAuxPhy1M)) {
    endUnconnectedRadioActivity();
    return false;
  }

  if (result != nullptr) {
    result->primaryRssiDbm = primaryRssi;
    result->primaryHeader = primaryHeader;
    result->primaryPayloadLength = primaryPayloadLength;
    result->advMode = primaryView.advMode;
    result->did = primaryView.did;
    result->sid = primaryView.sid;
    result->auxChannel = primaryView.auxPtr.channel;
    result->auxPhy = primaryView.auxPtr.phy;
    result->auxOffsetUs = primaryView.auxPtr.offsetUs;
  }

  if (!setDataChannel(primaryView.auxPtr.channel)) {
    endUnconnectedRadioActivity();
    return false;
  }

  const uint32_t auxTargetUs = primaryStartUs + primaryView.auxPtr.offsetUs;
  const uint32_t auxArmUs = auxTargetUs - kBleExtendedAuxRxLeadUs;
  uint32_t nowUs = bleTimingUs();
  while (!timeReachedUs(nowUs, auxArmUs)) {
    nowUs = bleTimingUs();
  }

  uint8_t secondaryHeader = 0U;
  uint8_t secondaryPayloadLength = 0U;
  const uint8_t* secondaryPayload = nullptr;
  int8_t secondaryRssi = -127;
  uint32_t secondaryStartUs = 0U;
  uint32_t secondaryEndUs = 0U;
  if (!receivePacketOnCurrentChannel(secondaryListenSpinLimit, spinLimit,
                                     &secondaryHeader, &secondaryPayloadLength,
                                     &secondaryPayload, &secondaryRssi,
                                     &secondaryStartUs, &secondaryEndUs)) {
    endUnconnectedRadioActivity();
    return false;
  }

  if (static_cast<uint8_t>(secondaryHeader & 0x0FU) != kBlePduAdvExtInd) {
    endUnconnectedRadioActivity();
    return false;
  }

  BleExtendedHeaderView secondaryView{};
  if (!parseBleExtendedHeader(secondaryPayload, secondaryPayloadLength, &secondaryView) ||
      !secondaryView.valid ||
      (secondaryView.advMode != kBleExtendedAdvModeNonConnScan) ||
      !secondaryView.hasAdi || !secondaryView.hasAdvA ||
      (secondaryView.did != primaryView.did) ||
      (secondaryView.sid != primaryView.sid)) {
    endUnconnectedRadioActivity();
    return false;
  }

  const bool advertiserAddressRandom = ((secondaryHeader >> 6U) & 0x1U) != 0U;
  if (result != nullptr) {
    result->advertiserAddressRandom = advertiserAddressRandom;
    memcpy(result->advertiserAddress, secondaryView.advA,
           sizeof(result->advertiserAddress));
    result->secondaryPacketCount = 1U;
    result->dataLength = 0U;
  }
  if ((secondaryView.advDataLength > 0U) && (result != nullptr)) {
    memcpy(result->data, secondaryView.advData, secondaryView.advDataLength);
    result->dataLength = secondaryView.advDataLength;
  }

  const bool scannerAddressRandom =
      (addressType_ == BleAddressType::kRandomStatic);
  txPacket_[0] = static_cast<uint8_t>(kBlePduScanReq |
                                      ((scannerAddressRandom ? 1U : 0U) << 6U) |
                                      ((advertiserAddressRandom ? 1U : 0U) << 7U));
  txPacket_[1] = 12U;
  memcpy(&txPacket_[2], address_, 6U);
  memcpy(&txPacket_[8], secondaryView.advA, 6U);

  clearRadioCoreEvents(radio_);
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&txPacket_[0]));
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  const uint32_t txTriggerTargetUs = secondaryEndUs + kBleConnTxenAfterRxUs;
  uint32_t txTriggerNowUs = bleTimingUs();
  while (!timeReachedUs(txTriggerNowUs, txTriggerTargetUs)) {
    txTriggerNowUs = bleTimingUs();
  }

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  bool txDisabled = waitDisabled(spinLimit / 2U + 1U);
  if (!txDisabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    txDisabled = waitDisabled(spinLimit / 2U + 1U);
  }
  radio_->SHORTS = 0U;
  if (!txDisabled) {
    clearRadioCoreEvents(radio_);
    endUnconnectedRadioActivity();
    return false;
  }

  uint8_t scanRspHeader = 0U;
  uint8_t scanRspPayloadLength = 0U;
  const uint8_t* scanRspPayload = nullptr;
  int8_t scanRspRssi = -127;
  uint32_t scanRspStartUs = 0U;
  uint32_t scanRspEndUs = 0U;
  if (!receivePacketOnCurrentChannel(scanRspListenSpinLimit, spinLimit, &scanRspHeader,
                                     &scanRspPayloadLength, &scanRspPayload,
                                     &scanRspRssi, &scanRspStartUs,
                                     &scanRspEndUs)) {
    endUnconnectedRadioActivity();
    return false;
  }

  if (static_cast<uint8_t>(scanRspHeader & 0x0FU) != kBlePduAdvExtInd) {
    endUnconnectedRadioActivity();
    return false;
  }

  BleExtendedHeaderView scanRspView{};
  if (!parseBleExtendedHeader(scanRspPayload, scanRspPayloadLength, &scanRspView) ||
      !scanRspView.valid ||
      (scanRspView.advMode != kBleExtendedAdvModeNonConnNonScan) ||
      !scanRspView.hasAdvA || !scanRspView.hasAdi ||
      (scanRspView.did != primaryView.did) ||
      (scanRspView.sid != primaryView.sid) ||
      !bleAddressEqual(scanRspView.advA, secondaryView.advA)) {
    endUnconnectedRadioActivity();
    return false;
  }

  uint16_t scanRspDataLength = 0U;
  if (scanRspView.advDataLength > 0U) {
    scanRspDataLength = scanRspView.advDataLength;
    if (result != nullptr) {
      memcpy(result->scanRspData, scanRspView.advData, scanRspView.advDataLength);
      result->scanRspDataLength = scanRspDataLength;
    }
  }

  uint8_t scanRspPacketCount = 1U;
  if (result != nullptr) {
    result->scanResponseReceived = true;
    result->scanRspRssiDbm = scanRspRssi;
    result->scanRspSecondaryPacketCount = scanRspPacketCount;
  }

  BleExtendedAuxPtrView nextRspAux = scanRspView.auxPtr;
  uint32_t previousRspStartUs = scanRspStartUs;
  bool expectAnotherScanRspPacket = scanRspView.hasAuxPtr;
  while (expectAnotherScanRspPacket) {
    if (scanRspPacketCount >= kBleExtendedSecondaryPacketCountMax) {
      endUnconnectedRadioActivity();
      return false;
    }
    if (nextRspAux.phy != kBleExtendedAuxPhy1M) {
      endUnconnectedRadioActivity();
      return false;
    }
    if (!setDataChannel(nextRspAux.channel)) {
      endUnconnectedRadioActivity();
      return false;
    }

    const uint32_t targetUs = previousRspStartUs + nextRspAux.offsetUs;
    const uint32_t armUs = targetUs - kBleExtendedAuxRxLeadUs;
    nowUs = bleTimingUs();
    while (!timeReachedUs(nowUs, armUs)) {
      nowUs = bleTimingUs();
    }

    uint8_t chainHeader = 0U;
    uint8_t chainPayloadLength = 0U;
    const uint8_t* chainPayload = nullptr;
    int8_t chainRssi = -127;
    uint32_t chainStartUs = 0U;
    uint32_t chainEndUs = 0U;
    if (!receivePacketOnCurrentChannel(secondaryListenSpinLimit, spinLimit,
                                       &chainHeader, &chainPayloadLength,
                                       &chainPayload, &chainRssi,
                                       &chainStartUs, &chainEndUs)) {
      endUnconnectedRadioActivity();
      return false;
    }
    if (static_cast<uint8_t>(chainHeader & 0x0FU) != kBlePduAdvExtInd) {
      endUnconnectedRadioActivity();
      return false;
    }

    BleExtendedHeaderView chainView{};
    if (!parseBleExtendedHeader(chainPayload, chainPayloadLength, &chainView) ||
        !chainView.valid ||
        (chainView.advMode != kBleExtendedAdvModeNonConnNonScan) ||
        !chainView.hasAdi || (chainView.did != primaryView.did) ||
        (chainView.sid != primaryView.sid)) {
      endUnconnectedRadioActivity();
      return false;
    }
    if ((static_cast<uint32_t>(scanRspDataLength) + chainView.advDataLength) >
        kBleExtendedAdvDataMaxLength) {
      endUnconnectedRadioActivity();
      return false;
    }
    if ((chainView.advDataLength > 0U) && (result != nullptr)) {
      memcpy(&result->scanRspData[scanRspDataLength], chainView.advData,
             chainView.advDataLength);
    }
    scanRspDataLength =
        static_cast<uint16_t>(scanRspDataLength + chainView.advDataLength);
    ++scanRspPacketCount;
    if (result != nullptr) {
      result->scanRspDataLength = scanRspDataLength;
      result->scanRspSecondaryPacketCount = scanRspPacketCount;
      result->scanRspRssiDbm = chainRssi;
    }

    if (!chainView.hasAuxPtr) {
      expectAnotherScanRspPacket = false;
      break;
    }
    if (chainView.auxPtr.phy != kBleExtendedAuxPhy1M) {
      endUnconnectedRadioActivity();
      return false;
    }

    nextRspAux = chainView.auxPtr;
    previousRspStartUs = chainStartUs;
  }

  endUnconnectedRadioActivity();
  return true;
}

bool BleRadio::scanExtendedActiveCycle(BleExtendedScanResult* result,
                                       uint32_t perChannelPrimaryListenSpinLimit,
                                       uint32_t secondaryListenSpinLimit,
                                       uint32_t scanRspListenSpinLimit) {
  static constexpr BleAdvertisingChannel kChannels[3] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };

  const uint32_t firstPassSpin = perChannelPrimaryListenSpinLimit;
  const uint32_t secondPassSpin =
      (perChannelPrimaryListenSpinLimit > 1U)
          ? (perChannelPrimaryListenSpinLimit / 2U)
          : perChannelPrimaryListenSpinLimit;

  for (uint8_t pass = 0U; pass < 2U; ++pass) {
    const uint32_t dwell = (pass == 0U) ? firstPassSpin : secondPassSpin;
    for (uint8_t i = 0U; i < 3U; ++i) {
      const uint8_t idx = static_cast<uint8_t>((scanCycleStartIndex_ + i) % 3U);
      if (scanExtendedActiveOnce(kChannels[idx], result, dwell,
                                 secondaryListenSpinLimit,
                                 scanRspListenSpinLimit,
                                 dwell + secondaryListenSpinLimit +
                                     scanRspListenSpinLimit + 300000UL)) {
        scanCycleStartIndex_ = static_cast<uint8_t>((idx + 1U) % 3U);
        return true;
      }
    }
  }

  scanCycleStartIndex_ = static_cast<uint8_t>((scanCycleStartIndex_ + 1U) % 3U);
  return false;
}

bool BleRadio::handleRequestAndMaybeRespond(BleAdvertisingChannel channel,
                                            BleAdvInteraction* interaction,
                                            uint32_t requestListenSpinLimit,
                                            uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return false;
  }
  if (!setAdvertisingChannel(channel)) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  memset(rxPacket_, 0, sizeof(rxPacket_));
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;

  const bool endSeen = waitRadioEndBudgeted(
      radio_, kBleAdvRequestListenMaxUs, requestListenSpinLimit);
  if (!endSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return true;
  }
  const uint32_t rxEndUs = bleTimingUs();

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOk = (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  const uint8_t hdr = rxPacket_[0];
  const uint8_t pduType = static_cast<uint8_t>(hdr & 0x0FU);
  const bool pduChSel2 = ((hdr >> 5U) & 0x1U) != 0U;
  const uint8_t length = static_cast<uint8_t>(rxPacket_[1] & 0x3FU);
  const bool txAddrRandom = ((hdr >> 6U) & 0x1U) != 0U;
  const bool rxAddrRandom = ((hdr >> 7U) & 0x1U) != 0U;
  const uint8_t* payload = &rxPacket_[2];

  bool disabled = waitDisabled(spinLimit / 2U + 1U);
  if (!disabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    disabled = waitDisabled(spinLimit / 2U + 1U);
  }
  radio_->SHORTS = 0U;
  if (!disabled) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  bool hasScanReq = false;
  bool hasConnectInd = false;
  bool connectAccepted = false;
  bool addressMatch = false;
  const bool connectable = (pduType_ == BleAdvPduType::kAdvInd);
  if (crcOk && length >= 12U) {
    const uint8_t* scannerOrInitiator = payload;
    const uint8_t* advertiserAddress = &payload[6];
    addressMatch = bleAddressEqual(advertiserAddress, &address_[0]) &&
                   (rxAddrRandom ==
                    (addressType_ == BleAddressType::kRandomStatic));

    if (addressMatch && pduType == kBlePduScanReq) {
      hasScanReq = true;
      if (interaction != nullptr) {
        memcpy(&interaction->peerAddress[0], scannerOrInitiator, 6U);
      }
    } else if (addressMatch && pduType == kBlePduConnectInd && connectable &&
               (length >= 34U)) {
      hasConnectInd = true;
      if (interaction != nullptr) {
        memcpy(&interaction->peerAddress[0], scannerOrInitiator, 6U);
      }
      connectAccepted = startConnectionFromConnectInd(payload, length, txAddrRandom,
                                                      pduChSel2, rxEndUs);
    }
  }

  if (interaction != nullptr) {
    interaction->channel = channel;
    interaction->rssiDbm = radioRssiDbm(radio_);
    interaction->peerAddressRandom = txAddrRandom;
    interaction->receivedScanRequest = hasScanReq;
    interaction->receivedConnectInd = hasConnectInd;
    interaction->connectIndChSel2 = hasConnectInd && useChSel2_ && pduChSel2;
  }

  bool txScanRspOk = false;
  const bool scannable = (pduType_ == BleAdvPduType::kAdvInd) ||
                         (pduType_ == BleAdvPduType::kAdvScanInd);
  if (hasScanReq && scannable && !connectAccepted) {
    clearRadioCoreEvents(radio_);
    radio_->PACKETPTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&scanRspPacket_[0]));
    radio_->SHORTS =
        ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
         RADIO_SHORTS_TXREADY_START_Msk) |
        ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
          << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
         RADIO_SHORTS_PHYEND_DISABLE_Msk);

    // Wait for T_IFS turnaround (150us nominal).
    const uint32_t txTriggerTargetUs = rxEndUs + kBleConnTxenAfterRxUs;
    uint32_t txTriggerNowUs = bleTimingUs();
    while (!timeReachedUs(txTriggerNowUs, txTriggerTargetUs)) {
      txTriggerNowUs = bleTimingUs();
    }

    radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
    txScanRspOk = waitDisabled(spinLimit);
    radio_->SHORTS = 0U;
  }

  if (interaction != nullptr) {
    interaction->scanResponseTransmitted = txScanRspOk;
    if (hasConnectInd && !connectAccepted) {
      interaction->receivedConnectInd = false;
      interaction->connectIndChSel2 = false;
    }
  }

  clearRadioCoreEvents(radio_);
  return true;
}

bool BleRadio::startConnectionFromConnectInd(const uint8_t* payload, uint8_t length,
                                             bool peerAddressRandom, bool useChSel2,
                                             uint32_t connectIndEndUs) {
  if (!initialized_ || radio_ == nullptr || payload == nullptr || length < 34U) {
    return false;
  }

  memcpy(connectionPeerAddress_, payload, sizeof(connectionPeerAddress_));
  connectionPeerAddressRandom_ = peerAddressRandom;

  const uint8_t* llData = &payload[12];
  connectionAccessAddress_ = readLe32(&llData[0]);
  connectionCrcInit_ = readLe24(&llData[4]);

  const uint8_t winSize = llData[7];
  const uint16_t winOffset = readLe16(&llData[8]);
  connectionIntervalUnits_ = readLe16(&llData[10]);
  connectionLatency_ = readLe16(&llData[12]);
  connectionTimeoutUnits_ = readLe16(&llData[14]);
  memcpy(connectionChannelMap_, &llData[16], sizeof(connectionChannelMap_));
  connectionChannelMap_[4] &= 0x1FU;
  connectionSca_ = (llData[21] >> 5U) & 0x07U;
  connectionHop_ = llData[21] & 0x1FU;
  // CSA#2 is used only when both sides indicate support via ChSel bit.
  connectionUseChSel2_ = useChSel2_ && useChSel2;
  connectionChannelId_ = static_cast<uint16_t>(
      (connectionAccessAddress_ & 0xFFFFU) ^ ((connectionAccessAddress_ >> 16U) & 0xFFFFU));

  connectionChannelCount_ = bitCount37(connectionChannelMap_);
  if (connectionIntervalUnits_ < 6U || connectionIntervalUnits_ > 3200U) {
    return false;
  }
  if (connectionTimeoutUnits_ < 10U) {
    return false;
  }
  if (connectionChannelCount_ < 2U) {
    return false;
  }
  if (connectionHop_ < 5U || connectionHop_ > 16U) {
    return false;
  }
  if (connectionAccessAddress_ == kBleAccessAddress) {
    return false;
  }

  connectionChanUse_ = 0U;
  connectionExpectedRxSn_ = 0U;
  connectionTxSn_ = 0U;
  connectionLastTxSn_ = 0U;
  connectionTxHistoryValid_ = false;
  connectionEventCounter_ = 0U;
  connectionMissedEventCount_ = 0U;
  connectionFirstEventListenUs_ =
      static_cast<uint32_t>((winSize > 0U) ? winSize : 1U) * 1250UL;
  connectionSyncAttemptsRemaining_ = 8U;
  connectionAttMtu_ = kBleDefaultAttMtu;
  connectionLastTxLlid_ = 0x01U;
  connectionLastTxLength_ = 0U;
  connectionLastTxSn_ = 0U;
  connectionLastTxPlainLlid_ = 0x01U;
  connectionLastTxPlainLength_ = 0U;
  memset(connectionLastTxPlainPayload_, 0, sizeof(connectionLastTxPlainPayload_));
  connectionPendingTxLlid_ = 0x01U;
  connectionPendingTxLength_ = 0U;
  connectionPendingTxValid_ = false;
  memset(connectionPendingTxPayload_, 0, sizeof(connectionPendingTxPayload_));
  connectionUpdatePending_ = false;
  connectionUpdateInstant_ = 0U;
  connectionPendingWinSize_ = 0U;
  connectionPendingWinOffset_ = 0U;
  connectionPendingIntervalUnits_ = 0U;
  connectionPendingLatency_ = 0U;
  connectionPendingTimeoutUnits_ = 0U;
  connectionChannelMapPending_ = false;
  connectionChannelMapInstant_ = 0U;
  memset(connectionPendingChannelMap_, 0, sizeof(connectionPendingChannelMap_));
  connectionPendingChannelCount_ = 0U;
  connectionServiceChangedIndicationsEnabled_ = false;
  // Arduino sketches can change the exposed GATT database completely between
  // uploads while keeping the same static BLE address. Queue a full-database
  // Service Changed indication on every new connection so centrals like
  // Android invalidate stale caches before using custom services.
  connectionServiceChangedIndicationPending_ = true;
  connectionServiceChangedIndicationAwaitingConfirm_ = false;
  connectionBatteryNotificationsEnabled_ = false;
  connectionBatteryNotificationPending_ = false;
  connectionPreparedWriteActive_ = false;
  connectionPreparedWriteHandle_ = 0U;
  connectionPreparedWriteValue_[0] = 0U;
  connectionPreparedWriteValue_[1] = 0U;
  connectionPreparedWriteMask_ = 0U;
  clearCustomGattConnectionState();
  clearConnectionSecurityState();
  if (primeBondForCurrentPeer()) {
    emitBleTrace("BOND_PRIMED");
  }
  memset(connectionTxPayload_, 0, sizeof(connectionTxPayload_));

  radio_->BASE0 = bleAccessAddressBase(connectionAccessAddress_);
  radio_->PREFIX0 = (radio_->PREFIX0 & ~RADIO_PREFIX0_AP0_Msk) |
                    ((bleAccessAddressPrefix(connectionAccessAddress_)
                      << RADIO_PREFIX0_AP0_Pos) &
                     RADIO_PREFIX0_AP0_Msk);
  radio_->CRCINIT = (connectionCrcInit_ & RADIO_CRCINIT_CRCINIT_Msk);
  radio_->TXADDRESS =
      (0UL << RADIO_TXADDRESS_TXADDRESS_Pos) & RADIO_TXADDRESS_TXADDRESS_Msk;
  radio_->RXADDRESSES =
      (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos) &
      RADIO_RXADDRESSES_ADDR0_Msk;

  const uint32_t nowUs =
      (connectIndEndUs != 0U) ? connectIndEndUs : bleTimingUs();
  // First data event uses legacy transmitWindowDelay (+1.25 ms) in addition to
  // transmitWindowOffset from CONNECT_IND.
  const uint32_t offsetUs =
      static_cast<uint32_t>(static_cast<uint32_t>(winOffset) + 1U) * 1250UL;
  connectionNextEventUs_ = nowUs + ((offsetUs > 0U) ? offsetUs : kBleConnFirstEventFallbackUs);

  if (!ensureRfPathActiveForBle()) {
    return false;
  }
  connected_ = true;
  emitBleTrace("CONNECTED");
  return true;
}

bool BleRadio::buildAttErrorResponse(uint8_t requestOpcode, uint16_t handle,
                                     uint8_t errorCode, uint8_t* outAttResponse,
                                     uint16_t* outAttResponseLength) const {
  if (outAttResponse == nullptr || outAttResponseLength == nullptr) {
    return false;
  }

  outAttResponse[0] = kAttOpErrorRsp;
  outAttResponse[1] = requestOpcode;
  writeLe16(&outAttResponse[2], handle);
  outAttResponse[4] = errorCode;
  *outAttResponseLength = 5U;
  return true;
}

uint8_t BleRadio::readAttributeValue(uint16_t handle, uint16_t offset,
                                     uint8_t* outValue, uint8_t maxLen) const {
  if (outValue == nullptr) {
    return kAttrReadInvalidHandleLen;
  }

  uint8_t fullValue[31] = {0};
  uint8_t fullLen = 0U;

  switch (handle) {
    case kHandleGapService:
      writeLe16(&fullValue[0], kUuidGapService);
      fullLen = 2U;
      break;
    case kHandleGapDeviceNameDecl:
      fullValue[0] = kGattCharacteristicPropRead;
      writeLe16(&fullValue[1], kHandleGapDeviceNameValue);
      writeLe16(&fullValue[3], kUuidDeviceName);
      fullLen = 5U;
      break;
    case kHandleGapDeviceNameValue:
      if (gapDeviceNameLen_ > 0U) {
        memcpy(fullValue, gapDeviceName_, gapDeviceNameLen_);
      }
      fullLen = gapDeviceNameLen_;
      break;
    case kHandleGapAppearanceDecl:
      fullValue[0] = kGattCharacteristicPropRead;
      writeLe16(&fullValue[1], kHandleGapAppearanceValue);
      writeLe16(&fullValue[3], kUuidAppearance);
      fullLen = 5U;
      break;
    case kHandleGapAppearanceValue:
      writeLe16(&fullValue[0], gapAppearance_);
      fullLen = 2U;
      break;
    case kHandleGapPpcpDecl:
      fullValue[0] = kGattCharacteristicPropRead;
      writeLe16(&fullValue[1], kHandleGapPpcpValue);
      writeLe16(&fullValue[3], kUuidPpcp);
      fullLen = 5U;
      break;
    case kHandleGapPpcpValue:
      writeLe16(&fullValue[0], gapPpcpIntervalMin_);
      writeLe16(&fullValue[2], gapPpcpIntervalMax_);
      writeLe16(&fullValue[4], gapPpcpLatency_);
      writeLe16(&fullValue[6], gapPpcpTimeout_);
      fullLen = 8U;
      break;
    case kHandleGattService:
      writeLe16(&fullValue[0], kUuidGattService);
      fullLen = 2U;
      break;
    case kHandleGattServiceChangedDecl:
      fullValue[0] = kGattCharacteristicPropIndicate;
      writeLe16(&fullValue[1], kHandleGattServiceChangedValue);
      writeLe16(&fullValue[3], kUuidServiceChanged);
      fullLen = 5U;
      break;
    case kHandleGattServiceChangedValue:
      writeLe16(&fullValue[0], 0x0001U);
      writeLe16(&fullValue[2], 0xFFFFU);
      fullLen = 4U;
      break;
    case kHandleGattServiceChangedCccd:
      writeLe16(&fullValue[0],
                connectionServiceChangedIndicationsEnabled_ ? 0x0002U : 0x0000U);
      fullLen = 2U;
      break;
    case kHandleBatteryService:
      writeLe16(&fullValue[0], kUuidBatteryService);
      fullLen = 2U;
      break;
    case kHandleBatteryLevelDecl:
      fullValue[0] = static_cast<uint8_t>(kGattCharacteristicPropRead |
                                          kGattCharacteristicPropNotify);
      writeLe16(&fullValue[1], kHandleBatteryLevelValue);
      writeLe16(&fullValue[3], kUuidBatteryLevel);
      fullLen = 5U;
      break;
    case kHandleBatteryLevelValue:
      fullValue[0] = gapBatteryLevel_;
      fullLen = 1U;
      break;
    case kHandleBatteryLevelCccd:
      writeLe16(&fullValue[0], connectionBatteryNotificationsEnabled_ ? 0x0001U : 0x0000U);
      fullLen = 2U;
      break;
    default: {
      const BleCustomServiceState* service = findCustomServiceByHandle(handle);
      if (service != nullptr) {
        const uint8_t uuidLength = customUuidLength(service->uuid.length);
        if (uuidLength == 0U) {
          return kAttrReadInvalidHandleLen;
        }
        memcpy(&fullValue[0], service->uuid.bytes, uuidLength);
        fullLen = uuidLength;
        break;
      }

      const BleCustomCharacteristicState* characteristic =
          findCustomCharacteristicByValueHandle(handle);
      if (characteristic != nullptr) {
        if ((characteristic->properties & kBleGattPropRead) == 0U) {
          return kAttrReadNotPermittedLen;
        }
        if (characteristic->valueLength > 0U) {
          memcpy(&fullValue[0], characteristic->value, characteristic->valueLength);
        }
        fullLen = characteristic->valueLength;
        break;
      }

      characteristic = findCustomCharacteristicByCccdHandle(handle);
      if (characteristic != nullptr) {
        writeLe16(&fullValue[0], characteristic->cccdValue);
        fullLen = 2U;
        break;
      }

      for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
        const BleCustomCharacteristicState& candidate = customGattCharacteristics_[i];
        if (candidate.declarationHandle != handle) {
          continue;
        }
        fullValue[0] = candidate.properties;
        writeLe16(&fullValue[1], candidate.valueHandle);
        const uint8_t uuidLength = customUuidLength(candidate.uuid.length);
        if (uuidLength == 0U) {
          return kAttrReadInvalidHandleLen;
        }
        memcpy(&fullValue[3], candidate.uuid.bytes, uuidLength);
        fullLen = static_cast<uint8_t>(3U + uuidLength);
        break;
      }
      if (fullLen == 0U) {
        return kAttrReadInvalidHandleLen;
      }
      break;
    }
  }

  if (offset > fullLen) {
    return kAttrReadInvalidOffsetLen;
  }
  const uint8_t remaining = static_cast<uint8_t>(fullLen - offset);
  const uint8_t toCopy = minU8(remaining, maxLen);
  if (toCopy > 0U) {
    memcpy(outValue, &fullValue[offset], toCopy);
  }
  return toCopy;
}

bool BleRadio::buildAttResponse(const uint8_t* attRequest, uint16_t requestLength,
                                uint8_t* outAttResponse,
                                uint16_t* outAttResponseLength) {
  if (attRequest == nullptr || outAttResponse == nullptr ||
      outAttResponseLength == nullptr || requestLength == 0U) {
    return false;
  }

  const uint16_t maxAttResponseLen = minU16(
      connectionAttMtu_, static_cast<uint16_t>(kBleDataPduMaxPayload - kBleL2capHeaderLen));
  if (maxAttResponseLen < 1U) {
    return false;
  }
  const uint8_t maxAttValueLen =
      static_cast<uint8_t>((maxAttResponseLen > 1U) ? (maxAttResponseLen - 1U) : 0U);

  const uint8_t opcode = attRequest[0];
  *outAttResponseLength = 0U;
  auto clearPreparedWrite = [&]() {
    connectionPreparedWriteActive_ = false;
    connectionPreparedWriteHandle_ = 0U;
    connectionPreparedWriteValue_[0] = 0U;
    connectionPreparedWriteValue_[1] = 0U;
    connectionPreparedWriteMask_ = 0U;
  };
  auto applyCccdState = [&](uint16_t handle, uint16_t cccd) -> bool {
    if (handle == kHandleGattServiceChangedCccd) {
      if ((cccd & ~0x0002U) != 0U) {
        return false;
      }
      const bool enableIndication = ((cccd & 0x0002U) != 0U);
      if (!enableIndication) {
        connectionServiceChangedIndicationPending_ = false;
        connectionServiceChangedIndicationAwaitingConfirm_ = false;
      }
      connectionServiceChangedIndicationsEnabled_ = enableIndication;
      return true;
    }
    if (handle == kHandleBatteryLevelCccd) {
      if ((cccd & ~0x0001U) != 0U) {
        return false;
      }
      const bool enableNotify = ((cccd & 0x0001U) != 0U);
      connectionBatteryNotificationsEnabled_ = enableNotify;
      connectionBatteryNotificationPending_ = enableNotify;
      return true;
    }

    BleCustomCharacteristicState* custom = findCustomCharacteristicByCccdHandle(handle);
    if (custom == nullptr) {
      return false;
    }

    uint16_t allowedMask = 0U;
    if ((custom->properties & kBleGattPropNotify) != 0U) {
      allowedMask |= 0x0001U;
    }
    if ((custom->properties & kBleGattPropIndicate) != 0U) {
      allowedMask |= 0x0002U;
    }
    if (allowedMask == 0U || ((cccd & ~allowedMask) != 0U)) {
      return false;
    }

    custom->cccdValue = cccd;
    if (((custom->cccdValue & 0x0002U) == 0U) &&
        (connectionCustomIndicationAwaitingHandle_ == custom->valueHandle)) {
      connectionCustomIndicationAwaitingHandle_ = 0U;
    }
    if (connectionCustomNotificationPending_) {
      const uint8_t pendingIndex = connectionCustomPendingCharIndex_;
      if (pendingIndex < customGattCharacteristicCount_ &&
          (&customGattCharacteristics_[pendingIndex] == custom)) {
        const bool pendingEnabled =
            connectionCustomPendingIndication_
                ? ((custom->cccdValue & 0x0002U) != 0U)
                : ((custom->cccdValue & 0x0001U) != 0U);
        if (!pendingEnabled) {
          connectionCustomNotificationPending_ = false;
          connectionCustomPendingCharIndex_ = 0xFFU;
          connectionCustomPendingIndication_ = false;
        }
      }
    }
    return true;
  };

  switch (opcode) {
    case kAttOpExchangeMtuReq: {
      if (requestLength < 3U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t clientRxMtu = readLe16(&attRequest[1]);
      const uint16_t serverRxMtu =
          minU16(kBleDefaultAttMtu,
                 static_cast<uint16_t>(kBleDataPduMaxPayload - kBleL2capHeaderLen));
      const uint16_t negotiatedMtu = minU16(clientRxMtu, serverRxMtu);
      connectionAttMtu_ = (negotiatedMtu >= 23U) ? negotiatedMtu : 23U;

      outAttResponse[0] = kAttOpExchangeMtuRsp;
      writeLe16(&outAttResponse[1], serverRxMtu);
      *outAttResponseLength = 3U;
      return true;
    }

    case kAttOpFindInfoReq: {
      if (requestLength < 5U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t rawStart = readLe16(&attRequest[1]);
      const uint16_t end = readLe16(&attRequest[3]);
      if (rawStart == 0U || rawStart > end) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidHandle, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t start = rawStart;

      outAttResponse[0] = kAttOpFindInfoRsp;
      outAttResponse[1] = 0U;
      uint16_t used = 2U;
      uint8_t responseFormat = 0U;
      uint16_t recordCount = 0U;
      bool stopFindInfo = false;
      for (size_t i = 0; i < (sizeof(kBleAttributeUuids) / sizeof(kBleAttributeUuids[0]));
           ++i) {
        const BleAttributeUuidRecord& record = kBleAttributeUuids[i];
        if (!inHandleRange(record.handle, start, end)) {
          continue;
        }
        if (responseFormat == kAttFindInfoFormat128Bit) {
          break;
        }
        if ((used + 4U) > maxAttResponseLen) {
          break;
        }
        responseFormat = kAttFindInfoFormat16Bit;
        outAttResponse[1] = responseFormat;
        writeLe16(&outAttResponse[used], record.handle);
        writeLe16(&outAttResponse[used + 2U], record.uuid16);
        used += 4U;
        ++recordCount;
      }
      for (uint8_t i = 0U;
           (i < customGattServiceCount_) && !stopFindInfo; ++i) {
        const BleCustomServiceState& service = customGattServices_[i];
        if (inHandleRange(service.serviceHandle, start, end)) {
          if (responseFormat == kAttFindInfoFormat128Bit || (used + 4U) > maxAttResponseLen) {
            break;
          }
          responseFormat = kAttFindInfoFormat16Bit;
          outAttResponse[1] = responseFormat;
          writeLe16(&outAttResponse[used], service.serviceHandle);
          writeLe16(&outAttResponse[used + 2U], kUuidPrimaryService);
          used += 4U;
          ++recordCount;
        }
        for (uint8_t j = 0U;
             j < customGattCharacteristicCount_; ++j) {
          const BleCustomCharacteristicState& ch = customGattCharacteristics_[j];
          if (ch.serviceHandle != service.serviceHandle) {
            continue;
          }
          if (inHandleRange(ch.declarationHandle, start, end)) {
            if (responseFormat == kAttFindInfoFormat128Bit ||
                (used + 4U) > maxAttResponseLen) {
              break;
            }
            responseFormat = kAttFindInfoFormat16Bit;
            outAttResponse[1] = responseFormat;
            writeLe16(&outAttResponse[used], ch.declarationHandle);
            writeLe16(&outAttResponse[used + 2U], kUuidCharacteristic);
            used += 4U;
            ++recordCount;
          }
          if (inHandleRange(ch.valueHandle, start, end)) {
            const uint8_t uuidLength = customUuidLength(ch.uuid.length);
            const uint8_t nextFormat =
                (uuidLength == kBleUuidLength128) ? kAttFindInfoFormat128Bit
                                                  : kAttFindInfoFormat16Bit;
            const uint16_t recordLen = static_cast<uint16_t>(2U + uuidLength);
            if (uuidLength == 0U) {
              stopFindInfo = true;
              break;
            }
            if ((responseFormat != 0U && responseFormat != nextFormat) ||
                (used + recordLen) > maxAttResponseLen) {
              stopFindInfo = true;
              break;
            }
            responseFormat = nextFormat;
            outAttResponse[1] = responseFormat;
            writeLe16(&outAttResponse[used], ch.valueHandle);
            memcpy(&outAttResponse[used + 2U], ch.uuid.bytes, uuidLength);
            used += recordLen;
            ++recordCount;
          }
          if (ch.cccdHandle != 0U &&
              inHandleRange(ch.cccdHandle, start, end)) {
            if (responseFormat == kAttFindInfoFormat128Bit ||
                (used + 4U) > maxAttResponseLen) {
              stopFindInfo = true;
              break;
            }
            responseFormat = kAttFindInfoFormat16Bit;
            outAttResponse[1] = responseFormat;
            writeLe16(&outAttResponse[used], ch.cccdHandle);
            writeLe16(&outAttResponse[used + 2U], kUuidClientCharacteristicConfig);
            used += 4U;
            ++recordCount;
          }
        }
      }

      if (recordCount == 0U) {
        return buildAttErrorResponse(opcode, start, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpFindByTypeValueReq: {
      if (requestLength < 9U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t rawStart = readLe16(&attRequest[1]);
      const uint16_t end = readLe16(&attRequest[3]);
      const uint16_t attrType = readLe16(&attRequest[5]);
      if (rawStart == 0U || rawStart > end) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }

      const uint8_t valueLen = static_cast<uint8_t>(requestLength - 7U);
      if (attrType != kUuidPrimaryService ||
          (valueLen != kBleUuidLength16 && valueLen != kBleUuidLength128)) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      outAttResponse[0] = kAttOpFindByTypeValueRsp;
      uint16_t used = 1U;
      const uint16_t maxRecords = static_cast<uint16_t>((maxAttResponseLen - 1U) / 4U);
      uint16_t recordCount = 0U;
      for (size_t i = 0; i < (sizeof(kBlePrimaryServices) / sizeof(kBlePrimaryServices[0]));
           ++i) {
        const BlePrimaryServiceRecord& service = kBlePrimaryServices[i];
        if (valueLen != kBleUuidLength16 || service.uuid16 != readLe16(&attRequest[7])) {
          continue;
        }
        if (!inHandleRange(service.startHandle, rawStart, end)) {
          continue;
        }
        if (recordCount >= maxRecords) {
          break;
        }
        writeLe16(&outAttResponse[used], service.startHandle);
        writeLe16(&outAttResponse[used + 2U], service.endHandle);
        used += 4U;
        ++recordCount;
      }
      for (uint8_t i = 0U;
           (i < customGattServiceCount_) && (recordCount < maxRecords); ++i) {
        const BleCustomServiceState& service = customGattServices_[i];
        if (!customUuidMatchesRaw(service.uuid.bytes, service.uuid.length, &attRequest[7],
                                  valueLen)) {
          continue;
        }
        if (!inHandleRange(service.serviceHandle, rawStart, end)) {
          continue;
        }
        writeLe16(&outAttResponse[used], service.serviceHandle);
        writeLe16(&outAttResponse[used + 2U], service.endHandle);
        used += 4U;
        ++recordCount;
      }

      if (recordCount == 0U) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpReadByGroupTypeReq: {
      if (requestLength < 7U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t rawStart = readLe16(&attRequest[1]);
      const uint16_t end = readLe16(&attRequest[3]);
      if (rawStart == 0U || rawStart > end) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }

      const uint16_t groupTypeLen = static_cast<uint16_t>(requestLength - 5U);
      if (groupTypeLen != 2U) {
        if (groupTypeLen == 16U) {
          return buildAttErrorResponse(opcode, rawStart, kAttErrUnsupportedGroupType,
                                       outAttResponse, outAttResponseLength);
        }
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidPdu,
                                     outAttResponse, outAttResponseLength);
      }

      const uint16_t start = rawStart;
      const uint16_t groupType = readLe16(&attRequest[5]);
      if (groupType != kUuidPrimaryService) {
        return buildAttErrorResponse(opcode, start, kAttErrUnsupportedGroupType,
                                     outAttResponse, outAttResponseLength);
      }

      outAttResponse[0] = kAttOpReadByGroupTypeRsp;
      outAttResponse[1] = 0U;
      uint16_t used = 2U;
      uint8_t entryLen = 0U;
      uint16_t recordCount = 0U;
      for (size_t i = 0; i < (sizeof(kBlePrimaryServices) / sizeof(kBlePrimaryServices[0]));
           ++i) {
        const BlePrimaryServiceRecord& service = kBlePrimaryServices[i];
        if (!inHandleRange(service.startHandle, start, end)) {
          continue;
        }
        if (entryLen == 0U) {
          entryLen = 6U;
          outAttResponse[1] = entryLen;
        } else if (entryLen != 6U) {
          break;
        }
        if ((used + entryLen) > maxAttResponseLen) {
          break;
        }
        writeLe16(&outAttResponse[used], service.startHandle);
        writeLe16(&outAttResponse[used + 2U], service.endHandle);
        writeLe16(&outAttResponse[used + 4U], service.uuid16);
        used += 6U;
        ++recordCount;
      }
      for (uint8_t i = 0U;
           i < customGattServiceCount_; ++i) {
        const BleCustomServiceState& service = customGattServices_[i];
        if (!inHandleRange(service.serviceHandle, start, end)) {
          continue;
        }
        const uint8_t uuidLength = customUuidLength(service.uuid.length);
        const uint8_t candidateEntryLen =
            uuidLength == 0U ? 0U : static_cast<uint8_t>(4U + uuidLength);
        if (candidateEntryLen == 0U) {
          break;
        }
        if (entryLen == 0U) {
          entryLen = candidateEntryLen;
          outAttResponse[1] = entryLen;
        } else if (candidateEntryLen != entryLen) {
          break;
        }
        if ((used + entryLen) > maxAttResponseLen) {
          break;
        }
        writeLe16(&outAttResponse[used], service.serviceHandle);
        writeLe16(&outAttResponse[used + 2U], service.endHandle);
        memcpy(&outAttResponse[used + 4U], service.uuid.bytes, uuidLength);
        used += entryLen;
        ++recordCount;
      }

      if (recordCount == 0U) {
        return buildAttErrorResponse(opcode, start, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpReadByTypeReq: {
      if (requestLength < 7U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t rawStart = readLe16(&attRequest[1]);
      const uint16_t end = readLe16(&attRequest[3]);
      if (rawStart == 0U || rawStart > end) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidHandle, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t start = rawStart;
      const uint8_t typeLength = static_cast<uint8_t>(requestLength - 5U);
      if (typeLength != kBleUuidLength16 && typeLength != kBleUuidLength128) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t type16 =
          (typeLength == kBleUuidLength16) ? readLe16(&attRequest[5]) : 0U;

      outAttResponse[0] = kAttOpReadByTypeRsp;
      uint16_t used = 2U;
      uint8_t entryLen = 0U;
      uint16_t recordCount = 0U;

      if (typeLength == kBleUuidLength16 && type16 == kUuidCharacteristic) {
        for (size_t i = 0; i < (sizeof(kBleCharacteristics) / sizeof(kBleCharacteristics[0]));
             ++i) {
          const BleCharacteristicRecord& ch = kBleCharacteristics[i];
          if (!inHandleRange(ch.declarationHandle, start, end)) {
            continue;
          }
          const uint8_t candidateEntryLen = 7U;
          if (entryLen == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          } else if (candidateEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], ch.declarationHandle);
          outAttResponse[used + 2U] = ch.properties;
          writeLe16(&outAttResponse[used + 3U], ch.valueHandle);
          writeLe16(&outAttResponse[used + 5U], ch.uuid16);
          used += entryLen;
          ++recordCount;
        }
        for (uint8_t i = 0U;
             i < customGattCharacteristicCount_; ++i) {
          const BleCustomCharacteristicState& ch = customGattCharacteristics_[i];
          const uint8_t uuidLength = customUuidLength(ch.uuid.length);
          const uint8_t candidateEntryLen = customCharacteristicDeclarationValueLength(
              ch.uuid.length);
          if (uuidLength == 0U || candidateEntryLen == 0U) {
            break;
          }
          const uint8_t responseEntryLen = static_cast<uint8_t>(2U + candidateEntryLen);
          if (!inHandleRange(ch.declarationHandle, start, end)) {
            continue;
          }
          if (entryLen == 0U) {
            entryLen = responseEntryLen;
            outAttResponse[1] = entryLen;
          } else if (responseEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], ch.declarationHandle);
          outAttResponse[used + 2U] = ch.properties;
          writeLe16(&outAttResponse[used + 3U], ch.valueHandle);
          memcpy(&outAttResponse[used + 5U], ch.uuid.bytes, uuidLength);
          used += entryLen;
          ++recordCount;
        }
      } else if (typeLength == kBleUuidLength16 && type16 == kUuidPrimaryService) {
        for (size_t i = 0; i < (sizeof(kBlePrimaryServices) / sizeof(kBlePrimaryServices[0]));
             ++i) {
          const BlePrimaryServiceRecord& service = kBlePrimaryServices[i];
          if (!inHandleRange(service.startHandle, start, end)) {
            continue;
          }
          const uint8_t candidateEntryLen = 4U;
          if (entryLen == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          } else if (candidateEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], service.startHandle);
          writeLe16(&outAttResponse[used + 2U], service.uuid16);
          used += entryLen;
          ++recordCount;
        }
        for (uint8_t i = 0U;
             i < customGattServiceCount_; ++i) {
          const BleCustomServiceState& service = customGattServices_[i];
          if (!inHandleRange(service.serviceHandle, start, end)) {
            continue;
          }
          const uint8_t uuidLength = customUuidLength(service.uuid.length);
          const uint8_t responseEntryLen =
              uuidLength == 0U ? 0U : static_cast<uint8_t>(2U + uuidLength);
          if (responseEntryLen == 0U) {
            break;
          }
          if (entryLen == 0U) {
            entryLen = responseEntryLen;
            outAttResponse[1] = entryLen;
          } else if (responseEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], service.serviceHandle);
          memcpy(&outAttResponse[used + 2U], service.uuid.bytes, uuidLength);
          used += entryLen;
          ++recordCount;
        }
      } else {
        uint16_t handles[6] = {0U, 0U, 0U, 0U, 0U, 0U};
        uint8_t handleCount = 0U;
        if (typeLength == kBleUuidLength16 && type16 == kUuidDeviceName) {
          handles[handleCount++] = kHandleGapDeviceNameValue;
        } else if (typeLength == kBleUuidLength16 && type16 == kUuidAppearance) {
          handles[handleCount++] = kHandleGapAppearanceValue;
        } else if (typeLength == kBleUuidLength16 && type16 == kUuidPpcp) {
          handles[handleCount++] = kHandleGapPpcpValue;
        } else if (typeLength == kBleUuidLength16 && type16 == kUuidServiceChanged) {
          handles[handleCount++] = kHandleGattServiceChangedValue;
        } else if (typeLength == kBleUuidLength16 &&
                   type16 == kUuidClientCharacteristicConfig) {
          handles[handleCount++] = kHandleGattServiceChangedCccd;
          handles[handleCount++] = kHandleBatteryLevelCccd;
        } else if (typeLength == kBleUuidLength16 && type16 == kUuidBatteryLevel) {
          handles[handleCount++] = kHandleBatteryLevelValue;
        }

        for (uint8_t i = 0U; i < handleCount; ++i) {
          const uint16_t handle = handles[i];
          if (!inHandleRange(handle, start, end)) {
            continue;
          }
          uint8_t temp[31] = {0};
          const uint8_t vlen = readAttributeValue(handle, 0U, temp, maxAttValueLen);
          if (vlen == kAttrReadInvalidHandleLen || vlen == kAttrReadInvalidOffsetLen) {
            continue;
          }

          const uint8_t candidateEntryLen = static_cast<uint8_t>(2U + vlen);
          if (candidateEntryLen == 2U) {
            continue;
          }
          if (entryLen == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          } else if (candidateEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }

          writeLe16(&outAttResponse[used], handle);
          if (vlen > 0U) {
            memcpy(&outAttResponse[used + 2U], temp, vlen);
          }
          used += entryLen;
          ++recordCount;
        }

        for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
          const BleCustomCharacteristicState& ch = customGattCharacteristics_[i];
          uint16_t handle = 0U;
          if (customUuidMatchesRaw(ch.uuid.bytes, ch.uuid.length, &attRequest[5], typeLength)) {
            handle = ch.valueHandle;
          } else if (typeLength == kBleUuidLength16 &&
                     type16 == kUuidClientCharacteristicConfig) {
            handle = ch.cccdHandle;
          }
          if (handle == 0U || !inHandleRange(handle, start, end)) {
            continue;
          }

          uint8_t temp[31] = {0};
          const uint8_t vlen = readAttributeValue(handle, 0U, temp, maxAttValueLen);
          if (vlen == kAttrReadInvalidHandleLen ||
              vlen == kAttrReadInvalidOffsetLen ||
              vlen == kAttrReadNotPermittedLen) {
            continue;
          }

          const uint8_t candidateEntryLen = static_cast<uint8_t>(2U + vlen);
          if (candidateEntryLen == 2U) {
            continue;
          }
          if (entryLen == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          } else if (candidateEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }

          writeLe16(&outAttResponse[used], handle);
          if (vlen > 0U) {
            memcpy(&outAttResponse[used + 2U], temp, vlen);
          }
          used += entryLen;
          ++recordCount;
        }
      }

      if (recordCount == 0U) {
        return buildAttErrorResponse(opcode, start, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpReadReq: {
      if (requestLength < 3U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t handle = readLe16(&attRequest[1]);
      if (handle == 0U) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }
      outAttResponse[0] = kAttOpReadRsp;
      const uint8_t vlen =
          readAttributeValue(handle, 0U, &outAttResponse[1], maxAttValueLen);
      if (vlen == kAttrReadInvalidHandleLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }
      if (vlen == kAttrReadNotPermittedLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrReadNotPermitted,
                                     outAttResponse, outAttResponseLength);
      }
      if (vlen == kAttrReadInvalidOffsetLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidOffset,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = static_cast<uint16_t>(1U + vlen);
      return true;
    }

    case kAttOpReadBlobReq: {
      if (requestLength < 5U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t handle = readLe16(&attRequest[1]);
      const uint16_t offset = readLe16(&attRequest[3]);
      if (handle == 0U) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }

      outAttResponse[0] = kAttOpReadBlobRsp;
      const uint8_t vlen =
          readAttributeValue(handle, offset, &outAttResponse[1], maxAttValueLen);
      if (vlen == kAttrReadInvalidHandleLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }
      if (vlen == kAttrReadNotPermittedLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrReadNotPermitted,
                                     outAttResponse, outAttResponseLength);
      }
      if (vlen == kAttrReadInvalidOffsetLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidOffset,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = static_cast<uint16_t>(1U + vlen);
      return true;
    }

    case kAttOpReadMultipleReq: {
      if (requestLength < 5U || ((requestLength & 0x01U) == 0U)) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      outAttResponse[0] = kAttOpReadMultipleRsp;
      uint16_t used = 1U;
      const uint16_t handleCount = static_cast<uint16_t>((requestLength - 1U) / 2U);
      for (uint16_t i = 0U; i < handleCount; ++i) {
        const uint16_t handle = readLe16(&attRequest[1U + (i * 2U)]);
        if (handle == 0U) {
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                       outAttResponse, outAttResponseLength);
        }

        if (used >= maxAttResponseLen) {
          break;
        }

        const uint8_t chunkMax = static_cast<uint8_t>(maxAttResponseLen - used);
        const uint8_t vlen = readAttributeValue(handle, 0U, &outAttResponse[used], chunkMax);
        if (vlen == kAttrReadInvalidHandleLen) {
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                       outAttResponse, outAttResponseLength);
        }
        if (vlen == kAttrReadNotPermittedLen) {
          return buildAttErrorResponse(opcode, handle, kAttErrReadNotPermitted,
                                       outAttResponse, outAttResponseLength);
        }
        if (vlen == kAttrReadInvalidOffsetLen) {
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidOffset,
                                       outAttResponse, outAttResponseLength);
        }
        used = static_cast<uint16_t>(used + vlen);
      }

      if (used <= 1U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpPrepareWriteReq: {
      if (requestLength < 5U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t handle = readLe16(&attRequest[1]);
      if ((handle != kHandleGattServiceChangedCccd) &&
          (handle != kHandleBatteryLevelCccd) &&
          (findCustomCharacteristicByCccdHandle(handle) == nullptr)) {
        return buildAttErrorResponse(opcode, handle, kAttErrWriteNotPermitted,
                                     outAttResponse, outAttResponseLength);
      }

      const uint16_t offset = readLe16(&attRequest[3]);
      const uint16_t valueLen = static_cast<uint16_t>(requestLength - 5U);
      if (offset > 1U) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidOffset,
                                     outAttResponse, outAttResponseLength);
      }
      if ((offset + valueLen) > 2U) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidAttrValueLen,
                                     outAttResponse, outAttResponseLength);
      }

      if (!connectionPreparedWriteActive_) {
        clearPreparedWrite();
        connectionPreparedWriteActive_ = true;
        connectionPreparedWriteHandle_ = handle;
      } else if (connectionPreparedWriteHandle_ != handle) {
        return buildAttErrorResponse(opcode, handle, kAttErrPrepareQueueFull,
                                     outAttResponse, outAttResponseLength);
      }

      for (uint16_t i = 0U; i < valueLen; ++i) {
        const uint16_t pos = static_cast<uint16_t>(offset + i);
        connectionPreparedWriteValue_[pos] = attRequest[5U + i];
        connectionPreparedWriteMask_ |= static_cast<uint8_t>(1U << pos);
      }

      if (requestLength > maxAttResponseLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      memcpy(outAttResponse, attRequest, requestLength);
      outAttResponse[0] = kAttOpPrepareWriteRsp;
      *outAttResponseLength = requestLength;
      return true;
    }

    case kAttOpExecuteWriteReq: {
      if (requestLength != 2U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint8_t flags = attRequest[1];
      if (flags == 0x00U) {
        clearPreparedWrite();
        outAttResponse[0] = kAttOpExecuteWriteRsp;
        *outAttResponseLength = 1U;
        return true;
      }
      if (flags != 0x01U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      if (connectionPreparedWriteActive_) {
        const uint16_t handle = connectionPreparedWriteHandle_;
        if ((handle != kHandleGattServiceChangedCccd) &&
            (handle != kHandleBatteryLevelCccd) &&
            (findCustomCharacteristicByCccdHandle(handle) == nullptr)) {
          clearPreparedWrite();
          return buildAttErrorResponse(opcode, handle, kAttErrWriteNotPermitted,
                                       outAttResponse, outAttResponseLength);
        }
        if ((connectionPreparedWriteMask_ & 0x03U) != 0x03U) {
          clearPreparedWrite();
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidAttrValueLen,
                                       outAttResponse, outAttResponseLength);
        }
        const uint16_t cccd = readLe16(&connectionPreparedWriteValue_[0]);
        if (!applyCccdState(handle, cccd)) {
          clearPreparedWrite();
          return buildAttErrorResponse(opcode, handle, kAttErrWriteNotPermitted,
                                       outAttResponse, outAttResponseLength);
        }
        clearPreparedWrite();
      }

      outAttResponse[0] = kAttOpExecuteWriteRsp;
      *outAttResponseLength = 1U;
      return true;
    }

    case kAttOpHandleValueCfm: {
      // Confirmation for ATT Handle Value Indication. This opcode has no response.
      if (requestLength == 1U) {
        connectionServiceChangedIndicationAwaitingConfirm_ = false;
        connectionCustomIndicationAwaitingHandle_ = 0U;
      }
      *outAttResponseLength = 0U;
      return false;
    }

    case kAttOpWriteReq: {
      if (requestLength < 3U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t handle = readLe16(&attRequest[1]);
      const uint16_t valueLen = static_cast<uint16_t>(requestLength - 3U);
      const uint8_t* value = (valueLen > 0U) ? &attRequest[3] : nullptr;

      const bool isCccdHandle =
          (handle == kHandleGattServiceChangedCccd) ||
          (handle == kHandleBatteryLevelCccd) ||
          (findCustomCharacteristicByCccdHandle(handle) != nullptr);
      if (isCccdHandle) {
        if (valueLen != 2U) {
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidAttrValueLen,
                                       outAttResponse, outAttResponseLength);
        }
        const uint16_t cccd = readLe16(&attRequest[3]);
        if (!applyCccdState(handle, cccd)) {
          return buildAttErrorResponse(opcode, handle, kAttErrWriteNotPermitted,
                                       outAttResponse, outAttResponseLength);
        }
        if (connectionPreparedWriteActive_ &&
            connectionPreparedWriteHandle_ == handle) {
          clearPreparedWrite();
        }
        outAttResponse[0] = kAttOpWriteRsp;
        *outAttResponseLength = 1U;
        return true;
      }
      uint8_t errorCode = kAttErrWriteNotPermitted;
      if (writeCustomGattCharacteristic(handle, value, valueLen, true, &errorCode)) {
        outAttResponse[0] = kAttOpWriteRsp;
        *outAttResponseLength = 1U;
        return true;
      }
      return buildAttErrorResponse(opcode, handle, errorCode, outAttResponse,
                                   outAttResponseLength);
    }

    case kAttOpWriteCmd: {
      if (requestLength >= 3U) {
        const uint16_t handle = readLe16(&attRequest[1]);
        const uint16_t valueLen = static_cast<uint16_t>(requestLength - 3U);
        const uint8_t* value = (valueLen > 0U) ? &attRequest[3] : nullptr;
        const bool isCccdHandle =
            (handle == kHandleGattServiceChangedCccd) ||
            (handle == kHandleBatteryLevelCccd) ||
            (findCustomCharacteristicByCccdHandle(handle) != nullptr);
        if (isCccdHandle && valueLen == 2U) {
          const uint16_t cccd = readLe16(&attRequest[3]);
          if (applyCccdState(handle, cccd)) {
            if (connectionPreparedWriteActive_ &&
                connectionPreparedWriteHandle_ == handle) {
              clearPreparedWrite();
            }
          }
        } else {
          uint8_t ignoredError = kAttErrWriteNotPermitted;
          writeCustomGattCharacteristic(handle, value, valueLen, false, &ignoredError);
        }
      }
      // Write command has no ATT response.
      *outAttResponseLength = 0U;
      return false;
    }

    default:
      return buildAttErrorResponse(opcode, 0U, kAttErrRequestNotSupported,
                                   outAttResponse, outAttResponseLength);
  }
}

bool BleRadio::buildL2capResponse(const uint8_t* l2capPayload,
                                  uint8_t l2capPayloadLength,
                                  uint8_t* outPayload,
                                  uint8_t* outPayloadLength) {
  if (l2capPayload == nullptr || l2capPayloadLength < kBleL2capHeaderLen) {
    return false;
  }

  const uint16_t cid = readLe16(&l2capPayload[2]);
  const bool smpPairingHandshakeActive =
      (smpPairingState_ == kSmpPairingStateRspSent) ||
      (smpPairingState_ == kSmpPairingStateConfirmSent);
  if (cid == kBleL2capCidAtt) {
    // During SMP confirm exchange, prioritize security-channel traffic and keep
    // ATT off the data path to reduce retransmission churn.
    if (smpPairingHandshakeActive) {
      return false;
    }
    return buildL2capAttResponse(l2capPayload, l2capPayloadLength, outPayload,
                                 outPayloadLength);
  }
  if (cid == kBleL2capCidLeSignaling) {
    return buildL2capSignalingResponse(l2capPayload, l2capPayloadLength,
                                       outPayload, outPayloadLength);
  }
  if (cid == kBleL2capCidSmp) {
    return buildL2capSmpResponse(l2capPayload, l2capPayloadLength,
                                 outPayload, outPayloadLength);
  }
  return false;
}

bool BleRadio::buildL2capSignalingResponse(const uint8_t* l2capPayload,
                                           uint8_t l2capPayloadLength,
                                           uint8_t* outPayload,
                                           uint8_t* outPayloadLength) {
  if (l2capPayload == nullptr || outPayload == nullptr || outPayloadLength == nullptr ||
      l2capPayloadLength < kBleL2capHeaderLen) {
    return false;
  }

  const uint16_t declaredL2capLength = readLe16(&l2capPayload[0]);
  const uint16_t cid = readLe16(&l2capPayload[2]);
  if (cid != kBleL2capCidLeSignaling) {
    return false;
  }

  const uint16_t available = static_cast<uint16_t>(l2capPayloadLength - kBleL2capHeaderLen);
  const uint16_t sigLength = minU16(declaredL2capLength, available);
  if (sigLength < 4U) {
    return false;
  }

  const uint8_t code = l2capPayload[kBleL2capHeaderLen + 0U];
  const uint8_t identifier = l2capPayload[kBleL2capHeaderLen + 1U];
  const uint16_t commandLength = readLe16(&l2capPayload[kBleL2capHeaderLen + 2U]);

  uint8_t outSigLen = 0U;
  auto writeCommandReject = [&](uint16_t reason) {
    outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeCommandRejectRsp;
    outPayload[kBleL2capHeaderLen + 1U] = identifier;
    writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 2U);
    writeLe16(&outPayload[kBleL2capHeaderLen + 4U], reason);
    outSigLen = 6U;
  };
  auto writeCommandRejectMtu = [&]() {
    outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeCommandRejectRsp;
    outPayload[kBleL2capHeaderLen + 1U] = identifier;
    writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 4U);
    writeLe16(&outPayload[kBleL2capHeaderLen + 4U],
              kL2capCmdRejectReasonSignalingMtuExceeded);
    writeLe16(&outPayload[kBleL2capHeaderLen + 6U], kBleL2capLeSignalingMtu);
    outSigLen = 8U;
  };
  auto writeCommandRejectInvalidCid = [&](uint16_t localCid, uint16_t remoteCid) {
    outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeCommandRejectRsp;
    outPayload[kBleL2capHeaderLen + 1U] = identifier;
    writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 6U);
    writeLe16(&outPayload[kBleL2capHeaderLen + 4U], kL2capCmdRejectReasonInvalidCid);
    writeLe16(&outPayload[kBleL2capHeaderLen + 6U], localCid);
    writeLe16(&outPayload[kBleL2capHeaderLen + 8U], remoteCid);
    outSigLen = 10U;
  };

  if ((4U + commandLength) > sigLength) {
    // Truncated signaling command payload; reject with signaling MTU hint.
    writeCommandRejectMtu();
  } else if (code == kL2capSigCodeCommandRejectRsp ||
             code == kL2capSigCodeConnParamUpdateRsp ||
             code == kL2capSigCodeLeCreditConnRsp) {
    // Response opcodes do not require another response.
    return false;
  } else if (code == kL2capSigCodeConnParamUpdateReq) {
    if (commandLength != 8U) {
      writeCommandReject(kL2capCmdRejectReasonCmdNotUnderstood);
    } else {
      const uint8_t* req = &l2capPayload[kBleL2capHeaderLen + 4U];
      const uint16_t intervalMin = readLe16(&req[0]);
      const uint16_t intervalMax = readLe16(&req[2]);
      const uint16_t latency = readLe16(&req[4]);
      const uint16_t timeout = readLe16(&req[6]);
      (void)intervalMin;
      (void)intervalMax;
      (void)latency;
      (void)timeout;

      // This device is currently operating in peripheral/slave role only, so
      // it cannot drive the central-side LL update procedure required by this
      // signaling request. Reply with "rejected" deterministically.
      outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeConnParamUpdateRsp;
      outPayload[kBleL2capHeaderLen + 1U] = identifier;
      writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 2U);
      writeLe16(&outPayload[kBleL2capHeaderLen + 4U], kL2capConnParamResultRejected);
      outSigLen = 6U;
    }
  } else if (code == kL2capSigCodeLeCreditConnReq) {
    if (commandLength != 10U) {
      writeCommandReject(kL2capCmdRejectReasonCmdNotUnderstood);
    } else {
      // LE Credit Based Connection Request:
      //   [PSM(2), SCID(2), MTU(2), MPS(2), InitialCredits(2)]
      // Peripheral path currently does not expose dynamic LE CoC channels.
      // Reply deterministically with "PSM not supported".
      outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeLeCreditConnRsp;
      outPayload[kBleL2capHeaderLen + 1U] = identifier;
      writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 10U);
      writeLe16(&outPayload[kBleL2capHeaderLen + 4U], 0x0000U);  // DCID
      writeLe16(&outPayload[kBleL2capHeaderLen + 6U], kBleL2capLeSignalingMtu);
      writeLe16(&outPayload[kBleL2capHeaderLen + 8U], kBleL2capLeSignalingMtu);
      writeLe16(&outPayload[kBleL2capHeaderLen + 10U], 0x0000U);  // Credits
      writeLe16(&outPayload[kBleL2capHeaderLen + 12U],
                kL2capLeCreditConnResultPsmNotSupported);
      outSigLen = 14U;
    }
  } else if (code == kL2capSigCodeLeFlowControlCredit) {
    if (commandLength != 4U) {
      writeCommandReject(kL2capCmdRejectReasonCmdNotUnderstood);
    } else {
      // No LE credit channels are established in this clean peripheral path.
      // Reject with Invalid CID and include received CID for diagnostics.
      const uint16_t cidFromPeer = readLe16(&l2capPayload[kBleL2capHeaderLen + 4U]);
      writeCommandRejectInvalidCid(0x0000U, cidFromPeer);
    }
  } else {
    writeCommandReject(kL2capCmdRejectReasonCmdNotUnderstood);
  }

  if ((kBleL2capHeaderLen + outSigLen) > kBleDataPduMaxPayload) {
    return false;
  }

  writeLe16(&outPayload[0], outSigLen);
  writeLe16(&outPayload[2], kBleL2capCidLeSignaling);
  *outPayloadLength = static_cast<uint8_t>(kBleL2capHeaderLen + outSigLen);
  return true;
}

bool BleRadio::buildL2capSmpResponse(const uint8_t* l2capPayload,
                                     uint8_t l2capPayloadLength,
                                     uint8_t* outPayload,
                                     uint8_t* outPayloadLength) {
  if (l2capPayload == nullptr || outPayload == nullptr || outPayloadLength == nullptr ||
      l2capPayloadLength < kBleL2capHeaderLen) {
    return false;
  }

  const uint16_t declaredL2capLength = readLe16(&l2capPayload[0]);
  const uint16_t cid = readLe16(&l2capPayload[2]);
  if (cid != kBleL2capCidSmp) {
    return false;
  }

  const uint16_t available = static_cast<uint16_t>(l2capPayloadLength - kBleL2capHeaderLen);
  const uint16_t smpLength = minU16(declaredL2capLength, available);
  if (smpLength < 1U) {
    return false;
  }

  const uint8_t* smp = &l2capPayload[kBleL2capHeaderLen];
  const uint8_t smpCode = smp[0];

  auto buildSmpResponse = [&](const uint8_t* smpData, uint8_t smpDataLen) -> bool {
    if (smpData == nullptr || smpDataLen == 0U) {
      return false;
    }
    if ((kBleL2capHeaderLen + smpDataLen) > kBleDataPduMaxPayload) {
      return false;
    }
    writeLe16(&outPayload[0], smpDataLen);
    writeLe16(&outPayload[2], kBleL2capCidSmp);
    memcpy(&outPayload[kBleL2capHeaderLen], smpData, smpDataLen);
    *outPayloadLength = static_cast<uint8_t>(kBleL2capHeaderLen + smpDataLen);
    return true;
  };
  auto buildSmpPairingFailed = [&](uint8_t reason) -> bool {
    clearConnectionSecurityState();
    uint8_t failed[2] = {kSmpCodePairingFailed, reason};
    emitBleTrace("SMP_PAIRING_FAILED");
    return buildSmpResponse(failed, sizeof(failed));
  };

  if (smpCode == kSmpCodePairingFailed) {
    clearConnectionSecurityState();
    emitBleTrace("SMP_PAIRING_FAILED_RX");
    return false;
  }

  switch (smpCode) {
    case kSmpCodePairingRequest: {
      if (smpLength != kSmpPairingRequestLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }

      const uint8_t oobFlag = smp[2];
      const uint8_t maxKeySize = smp[4];
      if (oobFlag != 0x00U) {
        return buildSmpPairingFailed(kSmpReasonOobNotAvailable);
      }
      if (maxKeySize < 7U || maxKeySize > 16U) {
        return buildSmpPairingFailed(kSmpReasonEncryptionKeySize);
      }

      const bool replacingPrimedBond = bondKeyPrimedForConnection_;
      clearConnectionSecurityState();
      if (replacingPrimedBond && bondRecordValid_) {
        // Peer explicitly started a new pairing procedure; drop the prior
        // bonded key so this connection re-establishes security state cleanly.
        clearBondRecord(true);
        emitBleTrace("BOND_REPLACED");
      }
      memcpy(smpPairingReq_, smp, kSmpPairingRequestLen);
      smpPairingRsp_[0] = kSmpCodePairingResponse;
      smpPairingRsp_[1] = kSmpIoCapNoInputNoOutput;
      smpPairingRsp_[2] = 0x00U;
      const uint8_t peerBonding = smp[3] & kSmpAuthReqBondingMask;
      smpPairingRsp_[3] = (peerBonding != 0U) ? 0x01U : 0x00U;
      smpBondingRequested_ = (peerBonding != 0U);
      smpPairingRsp_[4] = maxKeySize;
      smpPairingRsp_[5] = smpBondingRequested_
                              ? static_cast<uint8_t>(smp[5] & kSmpKeyDistEncKeyMask)
                              : 0x00U;
      smpPairingRsp_[6] = 0x00U;
      smpExpectInitiatorEncKey_ = ((smpPairingRsp_[5] & kSmpKeyDistEncKeyMask) != 0U);
      smpKeySize_ = maxKeySize;

      // Precompute local confirm material here so confirm/random stages avoid
      // repeated AES work while servicing tight connection events.
      const uint32_t seed = micros() ^ connectionAccessAddress_ ^
                            static_cast<uint32_t>(connectionEventCounter_ << 16U);
      fillPseudoRandomBytes(smpLocalRandom_, sizeof(smpLocalRandom_), seed);
      const uint8_t tk[16] = {0};
      const uint8_t iat = connectionPeerAddressRandom_ ? 0x01U : 0x00U;
      const uint8_t rat =
          (addressType_ == BleAddressType::kRandomStatic) ? 0x01U : 0x00U;
      if (!smpC1(tk, smpLocalRandom_, smpPairingReq_, smpPairingRsp_, iat,
                 connectionPeerAddress_, rat, address_, smpLocalConfirm_)) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }

      smpPairingState_ = kSmpPairingStateRspSent;
      emitBleTrace("SMP_PAIRING_REQUEST_RX");
      return buildSmpResponse(smpPairingRsp_, kSmpPairingResponseLen);
    }

    case kSmpCodePairingConfirm: {
      if (smpLength != kSmpPairingConfirmLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smpPairingState_ != kSmpPairingStateRspSent) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }

      memcpy(smpPeerConfirm_, &smp[1], sizeof(smpPeerConfirm_));
      uint8_t localConfirm[17] = {0};
      localConfirm[0] = kSmpCodePairingConfirm;
      memcpy(&localConfirm[1], smpLocalConfirm_, sizeof(smpLocalConfirm_));
      smpPairingState_ = kSmpPairingStateConfirmSent;
      emitBleTrace("SMP_CONFIRM_RX");
      return buildSmpResponse(localConfirm, sizeof(localConfirm));
    }

    case kSmpCodePairingRandom: {
      if (smpLength != kSmpPairingRandomLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smpPairingState_ != kSmpPairingStateConfirmSent) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }

      memcpy(smpPeerRandom_, &smp[1], sizeof(smpPeerRandom_));
      const uint8_t tk[16] = {0};
      const uint8_t iat = connectionPeerAddressRandom_ ? 0x01U : 0x00U;
      const uint8_t rat =
          (addressType_ == BleAddressType::kRandomStatic) ? 0x01U : 0x00U;
      uint8_t expectedRemoteConfirm[16] = {0};
      if (!smpC1(tk, smpPeerRandom_, smpPairingReq_, smpPairingRsp_, iat,
                 connectionPeerAddress_, rat, address_, expectedRemoteConfirm)) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }

      if (memcmp(expectedRemoteConfirm, smpPeerConfirm_, sizeof(smpPeerConfirm_)) != 0) {
        return buildSmpPairingFailed(kSmpReasonConfirmValueFailed);
      }

      // STK is derived from the initiator (peer) and responder (local) random values.
      if (!smpS1(tk, smpPeerRandom_, smpLocalRandom_, smpStk_)) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }
      smpStkValid_ = true;

      uint8_t localRandom[17] = {0};
      localRandom[0] = kSmpCodePairingRandom;
      memcpy(&localRandom[1], smpLocalRandom_, sizeof(smpLocalRandom_));
      smpPairingState_ = kSmpPairingStateRandomSent;
      emitBleTrace("SMP_RANDOM_RX");
      return buildSmpResponse(localRandom, sizeof(localRandom));
    }

    case kSmpCodeEncryptionInformation:
      if (smpLength != kSmpEncryptionInformationLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpBondingRequested_ || !smpExpectInitiatorEncKey_ ||
          !connectionEncSessionValid_ || !connectionEncRxEnabled_) {
        return false;
      }
      memcpy(smpPeerLtk_, &smp[1], sizeof(smpPeerLtk_));
      smpPeerLtkValid_ = true;
      smpPeerLtkAwaitMasterId_ = true;
      emitBleTrace("SMP_ENC_INFO_RX");
      return false;

    case kSmpCodeMasterIdentification:
      if (smpLength != kSmpMasterIdentificationLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpBondingRequested_ || !smpPeerLtkValid_ || !smpPeerLtkAwaitMasterId_) {
        return false;
      }
      smpEncReqEdiv_ = readLe16(&smp[1]);
      memcpy(smpEncReqRand_, &smp[3], sizeof(smpEncReqRand_));
      smpPeerLtkAwaitMasterId_ = false;
      {
        BleBondRecord record{};
        memcpy(record.peerAddress, connectionPeerAddress_, sizeof(record.peerAddress));
        record.peerAddressRandom = connectionPeerAddressRandom_ ? 1U : 0U;
        memcpy(record.localAddress, address_, sizeof(record.localAddress));
        record.localAddressRandom =
            (addressType_ == BleAddressType::kRandomStatic) ? 1U : 0U;
        memcpy(record.ltk, smpPeerLtk_, sizeof(record.ltk));
        memcpy(record.rand, smpEncReqRand_, sizeof(record.rand));
        record.ediv = smpEncReqEdiv_;
        record.keySize = (smpKeySize_ >= 7U && smpKeySize_ <= 16U) ? smpKeySize_ : 16U;
        persistBondRecord(record);
      }
      emitBleTrace("SMP_MASTER_ID_RX");
      return false;

    case kSmpCodePairingResponse:
      if (smpLength != kSmpPairingResponseLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      return buildSmpPairingFailed(kSmpReasonCommandNotSupported);

    case kSmpCodeSecurityRequest:
      if (smpLength != 2U) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      return buildSmpPairingFailed(kSmpReasonCommandNotSupported);

    default:
      return buildSmpPairingFailed(kSmpReasonPairingNotSupported);
  }
}

bool BleRadio::buildL2capAttResponse(const uint8_t* l2capPayload,
                                     uint8_t l2capPayloadLength,
                                     uint8_t* outPayload,
                                     uint8_t* outPayloadLength) {
  if (l2capPayload == nullptr || outPayload == nullptr || outPayloadLength == nullptr ||
      l2capPayloadLength < kBleL2capHeaderLen) {
    return false;
  }

  const uint16_t declaredL2capLength = readLe16(&l2capPayload[0]);
  const uint16_t cid = readLe16(&l2capPayload[2]);
  if (cid != kBleL2capCidAtt) {
    return false;
  }

  const uint16_t available = static_cast<uint16_t>(l2capPayloadLength - kBleL2capHeaderLen);
  const uint16_t attRequestLength = minU16(declaredL2capLength, available);
  if (attRequestLength == 0U) {
    return false;
  }

  uint16_t attResponseLength = 0U;
  if (!buildAttResponse(&l2capPayload[kBleL2capHeaderLen], attRequestLength,
                        &outPayload[kBleL2capHeaderLen], &attResponseLength) ||
      attResponseLength == 0U) {
    return false;
  }

  if (attResponseLength > (kBleDataPduMaxPayload - kBleL2capHeaderLen)) {
    return false;
  }

  writeLe16(&outPayload[0], attResponseLength);
  writeLe16(&outPayload[2], kBleL2capCidAtt);
  *outPayloadLength =
      static_cast<uint8_t>(kBleL2capHeaderLen + static_cast<uint8_t>(attResponseLength));
  return true;
}

bool BleRadio::buildLlControlResponse(const uint8_t* payload, uint8_t length,
                                      uint8_t* outPayload, uint8_t* outLength,
                                      bool* terminateInd) {
  if (terminateInd != nullptr) {
    *terminateInd = false;
  }
  if (payload == nullptr || outPayload == nullptr || outLength == nullptr || length == 0U) {
    return false;
  }

  const uint8_t opcode = payload[0];
  *outLength = 0U;
  auto rejectMalformedRequest = [&]() -> bool {
    outPayload[0] = kBleLlCtrlRejectExtInd;
    outPayload[1] = opcode;
    outPayload[2] = kBleLlErrorUnsupportedLlParamValue;
    *outLength = 3U;
    emitBleTrace("LL_REJECT_MALFORMED");
    return true;
  };
  auto rejectUnsupportedFeatureRequest = [&]() -> bool {
    outPayload[0] = kBleLlCtrlRejectExtInd;
    outPayload[1] = opcode;
    outPayload[2] = kBleLlErrorUnsupportedRemoteFeature;
    *outLength = 3U;
    emitBleTrace("LL_REJECT_UNSUPPORTED_FEATURE");
    return true;
  };
  auto rejectPinOrKeyMissing = [&]() -> bool {
    outPayload[0] = kBleLlCtrlRejectExtInd;
    outPayload[1] = opcode;
    outPayload[2] = kBleLlErrorPinOrKeyMissing;
    *outLength = 3U;
    emitBleTrace("LL_REJECT_PIN_OR_KEY_MISSING");
    return true;
  };
  auto rejectCommandDisallowed = [&]() -> bool {
    outPayload[0] = kBleLlCtrlRejectExtInd;
    outPayload[1] = opcode;
    outPayload[2] = kBleLlErrorCommandDisallowed;
    *outLength = 3U;
    emitBleTrace("LL_REJECT_COMMAND_DISALLOWED");
    return true;
  };
  auto rejectProcedureCollision = [&]() -> bool {
    outPayload[0] = kBleLlCtrlRejectExtInd;
    outPayload[1] = opcode;
    outPayload[2] = kBleLlErrorLlProcedureCollision;
    *outLength = 3U;
    emitBleTrace("LL_REJECT_PROCEDURE_COLLISION");
    return true;
  };

  switch (opcode) {
    case kBleLlCtrlTerminateInd:
      if (length != 2U) {
        return rejectMalformedRequest();
      }
      if (terminateInd != nullptr) {
        *terminateInd = true;
      }
      return false;

    case kBleLlCtrlConnectionUpdateInd:
      if (length != 12U) {
        return rejectMalformedRequest();
      }
      {
        const uint8_t winSize = payload[1];
        const uint16_t winOffset = readLe16(&payload[2]);
        const uint16_t interval = readLe16(&payload[4]);
        const uint16_t latency = readLe16(&payload[6]);
        const uint16_t timeout = readLe16(&payload[8]);
        const uint16_t instant = readLe16(&payload[10]);
        bool valid = true;
        if (winSize > 8U) {
          valid = false;
        }
        if (interval < 6U || interval > 3200U) {
          valid = false;
        }
        if (latency > 499U) {
          valid = false;
        }
        if (timeout < 10U || timeout > 3200U) {
          valid = false;
        }
        const uint32_t timeoutMs = static_cast<uint32_t>(timeout) * 10UL;
        const uint32_t requiredMs =
            ((1UL + static_cast<uint32_t>(latency)) *
             static_cast<uint32_t>(interval) * 5UL) /
            2UL;
        if (timeoutMs <= requiredMs) {
          valid = false;
        }
        const uint16_t instantDelta =
            static_cast<uint16_t>(instant - connectionEventCounter_);
        const bool instantInFuture = (instantDelta != 0U) && (instantDelta < 0x8000U);
        if (!instantInFuture) {
          valid = false;
        }
        if (valid) {
          connectionPendingWinSize_ = (winSize > 0U) ? winSize : 1U;
          connectionPendingWinOffset_ = winOffset;
          connectionPendingIntervalUnits_ = interval;
          connectionPendingLatency_ = latency;
          connectionPendingTimeoutUnits_ = timeout;
          connectionUpdateInstant_ = instant;
          connectionUpdatePending_ = true;
        } else {
          return rejectMalformedRequest();
        }
      }
      return false;

    case kBleLlCtrlChannelMapInd:
      if (length != 8U) {
        return rejectMalformedRequest();
      }
      {
        uint8_t map[5] = {payload[1], payload[2], payload[3], payload[4], payload[5]};
        const uint16_t instant = readLe16(&payload[6]);
        map[4] &= 0x1FU;
        const uint8_t count = bitCount37(map);
        const uint16_t instantDelta =
            static_cast<uint16_t>(instant - connectionEventCounter_);
        const bool instantInFuture = (instantDelta != 0U) && (instantDelta < 0x8000U);
        if (count >= 2U && instantInFuture) {
          memcpy(connectionPendingChannelMap_, map, sizeof(connectionPendingChannelMap_));
          connectionPendingChannelCount_ = count;
          connectionChannelMapInstant_ = instant;
          connectionChannelMapPending_ = true;
        } else {
          return rejectMalformedRequest();
        }
      }
      return false;

    case kBleLlCtrlEncReq:
      if (length != 23U) {
        return rejectMalformedRequest();
      }
      if (connectionEncSessionValid_ || connectionEncStartReqPending_ ||
          connectionEncStartReqTxPending_ ||
          connectionEncAwaitingStartRsp_ || connectionEncRxEnabled_ ||
          connectionEncTxEnabled_) {
        return rejectProcedureCollision();
      }
      if (!smpStkValid_) {
        return rejectPinOrKeyMissing();
      }
      if (bondKeyPrimedForConnection_) {
        const uint16_t ediv = readLe16(&payload[9]);
        if ((ediv != bondRecord_.ediv) ||
            (memcmp(&payload[1], bondRecord_.rand, sizeof(bondRecord_.rand)) != 0)) {
          return rejectPinOrKeyMissing();
        }
      }
      {
        // Controller-compatible layout:
        //   SKD[0..7]  = SKDm (from LL_ENC_REQ)
        //   SKD[8..15] = SKDs (from LL_ENC_RSP)
        //   IV[0..3]   = IVm  (from LL_ENC_REQ)
        //   IV[4..7]   = IVs  (from LL_ENC_RSP)
        memset(connectionEncSkd_, 0, sizeof(connectionEncSkd_));
        memcpy(&connectionEncSkd_[0], &payload[11], 8U);  // SKDm
        memcpy(&connectionEncIv_[0], &payload[19], 4U);   // IVm
        memcpy(encDebug_.encLastSkdm, &payload[11], sizeof(encDebug_.encLastSkdm));
        memcpy(encDebug_.encLastIvm, &payload[19], sizeof(encDebug_.encLastIvm));
        smpEncReqEdiv_ = readLe16(&payload[9]);
        memcpy(smpEncReqRand_, &payload[1], sizeof(smpEncReqRand_));

        const uint32_t seed =
            micros() ^ connectionAccessAddress_ ^
            static_cast<uint32_t>(connectionEventCounter_ << 16U) ^
            readLe32(&payload[11]) ^ readLe32(&payload[19]);
        fillPseudoRandomBytes(&connectionEncSkd_[8], 8U, seed);  // SKDs
        fillPseudoRandomBytes(&connectionEncIv_[4], 4U, seed ^ 0x6BA38B4FUL);  // IVs
        memcpy(encDebug_.encLastSkds, &connectionEncSkd_[8], sizeof(encDebug_.encLastSkds));
        memcpy(encDebug_.encLastIvs, &connectionEncIv_[4], sizeof(encDebug_.encLastIvs));
        // Derive the session key after the TX phase so we meet T_IFS timing.
        connectionEncKeyDerivationPending_ = true;
        memset(connectionEncSessionKey_, 0, sizeof(connectionEncSessionKey_));
        memset(connectionEncSessionKeyAlt_, 0, sizeof(connectionEncSessionKeyAlt_));
        connectionEncAltKeyValid_ = false;
        // Peripheral role direction bits (A/B interop probe):
        // RX (central->peripheral) uses 0, TX (peripheral->central) uses 1.
        connectionEncRxDirection_ = 0U;
        connectionEncTxDirection_ = 1U;
        encDebug_.encLastRxDir = connectionEncRxDirection_;
        encDebug_.encLastTxDir = connectionEncTxDirection_;

        connectionEncSessionValid_ = true;
        connectionEncRxEnabled_ = false;
        connectionEncTxEnabled_ = false;
        // Peripheral role sequence per Core LLCP:
        // after LL_ENC_RSP, wait for initiator LL_START_ENC_REQ and answer with
        // LL_START_ENC_RSP.
        connectionEncStartReqPending_ = true;
        connectionEncStartReqTxPending_ = false;
        connectionEncAwaitingStartRsp_ = false;
        connectionEncEnableTxOnNextEvent_ = false;
        connectionEncRxCounter_ = 0ULL;
        connectionEncTxCounter_ = 0ULL;
        connectionPendingTxValid_ = false;
        connectionPendingTxLlid_ = 0x01U;
        connectionPendingTxLength_ = 0U;
        connectionLastTxWasEncrypted_ = false;
        connectionLastTxEncryptedLength_ = 0U;
        memset(connectionLastTxEncryptedPayload_, 0, sizeof(connectionLastTxEncryptedPayload_));
        connectionEncPrecomputedEmptyValid_ = false;
        connectionEncPrecomputedCounter_ = 0ULL;
        memset(connectionEncPrecomputedPayload_, 0, sizeof(connectionEncPrecomputedPayload_));
        connectionEncPrecomputedStartRspValid_ = false;
        memset(connectionEncPrecomputedStartRsp_, 0, sizeof(connectionEncPrecomputedStartRsp_));
        connectionEncPrecomputedStartRspTxValid_ = false;
        connectionEncPrecomputedStartRspTxCounter_ = 0ULL;
        memset(connectionEncPrecomputedStartRspTx_, 0,
               sizeof(connectionEncPrecomputedStartRspTx_));

        outPayload[0] = kBleLlCtrlEncRsp;
        memcpy(&outPayload[1], &connectionEncSkd_[8], 8U);
        memcpy(&outPayload[9], &connectionEncIv_[4], 4U);
        *outLength = 13U;
        emitBleTrace("LL_ENC_REQ_ACCEPTED");
        return true;
      }

    case kBleLlCtrlStartEncReq:
      if (length != 1U) {
        return rejectMalformedRequest();
      }
      if (!connectionEncSessionValid_) {
        return rejectPinOrKeyMissing();
      }
      if (connectionEncTxEnabled_) {
        return rejectProcedureCollision();
      }
      if (!connectionEncStartReqPending_ &&
          !connectionEncStartReqTxPending_ &&
          !connectionEncAwaitingStartRsp_) {
        return rejectCommandDisallowed();
      }
      connectionEncStartReqPending_ = false;
      connectionEncStartReqTxPending_ = false;
      connectionEncAwaitingStartRsp_ = false;
      connectionEncRxEnabled_ = true;
      connectionEncEnableTxOnNextEvent_ = true;
      outPayload[0] = kBleLlCtrlStartEncRsp;
      *outLength = 1U;
      emitBleTrace("LL_START_ENC_REQ_ACCEPTED");
      return true;

    case kBleLlCtrlPauseEncReq:
      if (length != 1U) {
        return rejectMalformedRequest();
      }
      if (!connectionEncSessionValid_) {
        return rejectPinOrKeyMissing();
      }
      if (connectionEncStartReqPending_ || connectionEncStartReqTxPending_ ||
          connectionEncAwaitingStartRsp_) {
        return rejectProcedureCollision();
      }
      if (!connectionEncRxEnabled_ && !connectionEncTxEnabled_) {
        return rejectCommandDisallowed();
      }
      ++encDebug_.encPauseReqAcceptedCount;
      ++encDebug_.encClearCount;
      encDebug_.encLastClearReason = 0x01U;  // PauseEncReq accepted.
      clearEncryptionState();
      outPayload[0] = kBleLlCtrlPauseEncRsp;
      *outLength = 1U;
      emitBleTrace("LL_PAUSE_ENC_REQ_ACCEPTED");
      return true;

    case kBleLlCtrlCteReq:
    case kBleLlCtrlClockAccuracyReq:
      if (length != 2U) {
        return rejectMalformedRequest();
      }
      return rejectUnsupportedFeatureRequest();

    case kBleLlCtrlCisReq:
      if (length != 36U) {
        return rejectMalformedRequest();
      }
      return rejectUnsupportedFeatureRequest();

    case kBleLlCtrlConnectionParamReq:
      if (length != 24U) {
        return rejectMalformedRequest();
      }
      {
        const uint16_t intervalMin = readLe16(&payload[1]);
        const uint16_t intervalMax = readLe16(&payload[3]);
        const uint16_t latency = readLe16(&payload[5]);
        const uint16_t timeout = readLe16(&payload[7]);
        bool valid = true;
        if (intervalMin < 6U || intervalMin > 3200U ||
            intervalMax < 6U || intervalMax > 3200U ||
            intervalMin > intervalMax) {
          valid = false;
        }
        if (latency > 499U) {
          valid = false;
        }
        if (timeout < 10U || timeout > 3200U) {
          valid = false;
        }
        const uint32_t timeoutMs = static_cast<uint32_t>(timeout) * 10UL;
        const uint32_t requiredMs =
            ((1UL + static_cast<uint32_t>(latency)) *
             static_cast<uint32_t>(intervalMax) * 5UL) /
            2UL;
        if (timeoutMs <= requiredMs) {
          valid = false;
        }
        if (!valid) {
          return rejectMalformedRequest();
        }

        outPayload[0] = kBleLlCtrlConnectionParamRsp;
        memcpy(&outPayload[1], &payload[1], 23U);
        *outLength = 24U;
      }
      return true;

    case kBleLlCtrlFeatureReq:
    case kBleLlCtrlSlaveFeatureReq:
      if (length != 9U) {
        return rejectMalformedRequest();
      }
      outPayload[0] = kBleLlCtrlFeatureRsp;
      outPayload[1] = kBleLlFeatureMaskOctet0;
      memset(&outPayload[2], 0, 7U);
      *outLength = 9U;
      return true;

    case kBleLlCtrlVersionInd:
      if (length != 6U) {
        return rejectMalformedRequest();
      }
      outPayload[0] = kBleLlCtrlVersionInd;
      outPayload[1] = 0x0DU;  // Core spec version 5.4.
      writeLe16(&outPayload[2], 0x0059U);  // Nordic Semiconductor company ID.
      writeLe16(&outPayload[4], 0x0001U);  // Implementation subversion.
      *outLength = 6U;
      return true;

    case kBleLlCtrlLengthReq:
      if (length != 9U) {
        return rejectMalformedRequest();
      }
      outPayload[0] = kBleLlCtrlLengthRsp;
      writeLe16(&outPayload[1], kBleDataPduMaxPayload);
      writeLe16(&outPayload[3], 328U);
      writeLe16(&outPayload[5], kBleDataPduMaxPayload);
      writeLe16(&outPayload[7], 328U);
      *outLength = 9U;
      return true;

    case kBleLlCtrlPhyReq:
      if (length != 3U) {
        return rejectMalformedRequest();
      }
      outPayload[0] = kBleLlCtrlPhyRsp;
      outPayload[1] = 0x01U;  // LE 1M TX.
      outPayload[2] = 0x01U;  // LE 1M RX.
      *outLength = 3U;
      return true;

    case kBleLlCtrlPingReq:
      if (length != 1U) {
        return rejectMalformedRequest();
      }
      outPayload[0] = kBleLlCtrlPingRsp;
      *outLength = 1U;
      return true;

    case kBleLlCtrlConnectionParamRsp:
      if (length != 24U) {
        return false;
      }
      return false;

    case kBleLlCtrlEncRsp:
      if (length != 13U) {
        return false;
      }
      return false;

    case kBleLlCtrlStartEncRsp:
      if (length != 1U) {
        return false;
      }
      if (connectionEncAwaitingStartRsp_ && connectionEncSessionValid_) {
        ++encDebug_.encStartRspRxCount;
        connectionEncAwaitingStartRsp_ = false;
        // Zephyr-compatible timing: transmit final LL_START_ENC_RSP with TX
        // encryption active.
        connectionEncTxEnabled_ = true;
        connectionEncEnableTxOnNextEvent_ = false;
        connectionEncPrecomputedStartRspValid_ = false;
        outPayload[0] = kBleLlCtrlStartEncRsp;
        *outLength = 1U;
        emitBleTrace("LL_START_ENC_RSP_RX");
        return true;
      }
      return false;

    case kBleLlCtrlPauseEncRsp:
      if (length != 1U) {
        return false;
      }
      ++encDebug_.encPauseRspRxCount;
      ++encDebug_.encClearCount;
      encDebug_.encLastClearReason = 0x02U;  // PauseEncRsp received.
      clearEncryptionState();
      emitBleTrace("LL_PAUSE_ENC_RSP_RX");
      return false;

    case kBleLlCtrlPingRsp:
      if (length != 1U) {
        return false;
      }
      return false;

    case kBleLlCtrlRejectInd:
      if (length != 2U) {
        return false;
      }
      if (connectionEncAwaitingStartRsp_ || connectionEncStartReqPending_ ||
          connectionEncStartReqTxPending_) {
        ++encDebug_.encClearCount;
        encDebug_.encLastClearReason = 0x03U;  // RejectInd during enc procedure.
        clearEncryptionState();
      }
      return false;

    case kBleLlCtrlRejectExtInd:
      if (length != 3U) {
        return false;
      }
      if ((payload[1] == kBleLlCtrlEncReq) ||
          (payload[1] == kBleLlCtrlStartEncReq) ||
          (payload[1] == kBleLlCtrlPauseEncReq)) {
        ++encDebug_.encClearCount;
        encDebug_.encLastClearReason = 0x04U;  // RejectExtInd during enc procedure.
        clearEncryptionState();
      }
      return false;

    case kBleLlCtrlPhyUpdateInd:
      if (length != 5U) {
        return false;
      }
      return false;

    case kBleLlCtrlMinUsedChannelsInd:
      if (length != 3U) {
        return false;
      }
      return false;

    case kBleLlCtrlCteRsp:
      if (length != 1U) {
        return false;
      }
      return false;

    case kBleLlCtrlPeriodicSyncInd:
      if (length != 35U) {
        return false;
      }
      return false;

    case kBleLlCtrlClockAccuracyRsp:
      if (length != 2U) {
        return false;
      }
      return false;

    case kBleLlCtrlCisRsp:
      if (length != 9U) {
        return false;
      }
      return false;

    case kBleLlCtrlCisInd:
      if (length != 16U) {
        return false;
      }
      return false;

    case kBleLlCtrlCisTerminateInd:
      if (length != 4U) {
        return false;
      }
      return false;

    case kBleLlCtrlUnknownRsp:
      if (length != 2U) {
        return false;
      }
      return false;

    case kBleLlCtrlFeatureRsp:
      if (length != 9U) {
        return false;
      }
      return false;

    case kBleLlCtrlLengthRsp:
      if (length != 9U) {
        return false;
      }
      return false;

    case kBleLlCtrlPhyRsp:
      if (length != 3U) {
        return false;
      }
      return false;

    default:
      outPayload[0] = kBleLlCtrlUnknownRsp;
      outPayload[1] = opcode;
      *outLength = 2U;
      return true;
  }
}

void BleRadio::clearSmpPairingState() {
  smpPairingState_ = kSmpPairingStateIdle;
  memset(smpPairingReq_, 0, sizeof(smpPairingReq_));
  memset(smpPairingRsp_, 0, sizeof(smpPairingRsp_));
  memset(smpPeerConfirm_, 0, sizeof(smpPeerConfirm_));
  memset(smpPeerRandom_, 0, sizeof(smpPeerRandom_));
  memset(smpLocalRandom_, 0, sizeof(smpLocalRandom_));
  memset(smpLocalConfirm_, 0, sizeof(smpLocalConfirm_));
  memset(smpStk_, 0, sizeof(smpStk_));
  smpStkValid_ = false;
  smpBondingRequested_ = false;
  smpExpectInitiatorEncKey_ = false;
  smpPeerLtkValid_ = false;
  smpPeerLtkAwaitMasterId_ = false;
  memset(smpPeerLtk_, 0, sizeof(smpPeerLtk_));
  memset(smpEncReqRand_, 0, sizeof(smpEncReqRand_));
  smpEncReqEdiv_ = 0U;
  smpKeySize_ = 16U;
  bondKeyPrimedForConnection_ = false;
}

void BleRadio::clearEncryptionState() {
  connectionEncSessionValid_ = false;
  connectionEncRxEnabled_ = false;
  connectionEncTxEnabled_ = false;
  connectionEncStartReqPending_ = false;
  connectionEncStartReqTxPending_ = false;
  connectionEncAwaitingStartRsp_ = false;
  connectionEncEnableTxOnNextEvent_ = false;
  connectionEncRxCounter_ = 0ULL;
  connectionEncTxCounter_ = 0ULL;
  connectionEncKeyDerivationPending_ = false;
  memset(connectionEncSkd_, 0, sizeof(connectionEncSkd_));
  memset(connectionEncSessionKey_, 0, sizeof(connectionEncSessionKey_));
  memset(connectionEncSessionKeyAlt_, 0, sizeof(connectionEncSessionKeyAlt_));
  connectionEncAltKeyValid_ = false;
  connectionEncRxDirection_ = 1U;
  connectionEncTxDirection_ = 0U;
  memset(connectionEncIv_, 0, sizeof(connectionEncIv_));
  connectionLastTxWasEncrypted_ = false;
  connectionLastTxEncryptedLength_ = 0U;
  memset(connectionLastTxEncryptedPayload_, 0, sizeof(connectionLastTxEncryptedPayload_));
  connectionEncPrecomputedEmptyValid_ = false;
  connectionEncPrecomputedCounter_ = 0ULL;
  memset(connectionEncPrecomputedPayload_, 0, sizeof(connectionEncPrecomputedPayload_));
  connectionEncPrecomputedStartRspValid_ = false;
  memset(connectionEncPrecomputedStartRsp_, 0, sizeof(connectionEncPrecomputedStartRsp_));
  connectionEncPrecomputedStartRspTxValid_ = false;
  connectionEncPrecomputedStartRspTxCounter_ = 0ULL;
  memset(connectionEncPrecomputedStartRspTx_, 0, sizeof(connectionEncPrecomputedStartRspTx_));
}

void BleRadio::clearConnectionSecurityState() {
  clearSmpPairingState();
  clearEncryptionState();
}

bool BleRadio::isBondRecordUsable(const BleBondRecord& record) const {
  return bleBondRecordLooksSane(record);
}

bool BleRadio::loadBondRecordFromPersistence() {
  if (bondStorageLoaded_) {
    return bondRecordValid_;
  }

  BleBondRecord loaded{};
  bool loadedFromStore = false;
  if (bondLoadCallback_ != nullptr) {
    loadedFromStore = bondLoadCallback_(&loaded, bondCallbackContext_);
    if (loadedFromStore && !isBondRecordUsable(loaded)) {
      loadedFromStore = false;
    }
  }
  if (!loadedFromStore) {
    loadedFromStore = readFlashBondRecord(&loaded);
  }
  if (!loadedFromStore) {
    loadedFromStore = readRetainedBondRecord(&loaded);
  }

  bondStorageLoaded_ = true;
  if (loadedFromStore && isBondRecordUsable(loaded)) {
    memcpy(&bondRecord_, &loaded, sizeof(bondRecord_));
    bondRecordValid_ = true;
    emitBleTrace("BOND_LOADED");
  } else {
    memset(&bondRecord_, 0, sizeof(bondRecord_));
    bondRecordValid_ = false;
  }

  return bondRecordValid_;
}

bool BleRadio::persistBondRecord(const BleBondRecord& record) {
  if (!isBondRecordUsable(record)) {
    return false;
  }

  const bool retentionOk = writeRetainedBondRecord(record);
  const bool flashOk = writeFlashBondRecord(record);
  bool callbackOk = true;
  if (bondSaveCallback_ != nullptr) {
    callbackOk = bondSaveCallback_(&record, bondCallbackContext_);
  }
  if (retentionOk || flashOk || callbackOk) {
    memcpy(&bondRecord_, &record, sizeof(bondRecord_));
    bondRecordValid_ = true;
    bondStorageLoaded_ = true;
    emitBleTrace("BOND_SAVED");
  }
  return (retentionOk || flashOk) && callbackOk;
}

bool BleRadio::clearPersistentBondRecord() {
  clearRetainedBondBlob();
  const bool flashOk = clearFlashBondRecord();
  bool callbackOk = true;
  if (bondClearCallback_ != nullptr) {
    callbackOk = bondClearCallback_(bondCallbackContext_);
  }
  emitBleTrace("BOND_CLEARED");
  return flashOk && callbackOk;
}

bool BleRadio::primeBondForCurrentPeer() {
  bondKeyPrimedForConnection_ = false;
  if (!bondStorageLoaded_) {
    loadBondRecordFromPersistence();
  }
  if (!bondRecordValid_) {
    return false;
  }

  // For random peer addresses (e.g. Android resolvable private addresses that
  // rotate between connections) skip the exact address match and rely on the
  // EDIV/Rand check in the LL_ENC_REQ handler to verify the bond is valid.
  // For public peer addresses require an exact address and type match.
  const bool bondPeerIsRandom = (bondRecord_.peerAddressRandom & 0x01U) != 0U;
  if (!bondPeerIsRandom) {
    if (connectionPeerAddressRandom_) {
      return false;
    }
    if (memcmp(bondRecord_.peerAddress, connectionPeerAddress_,
               sizeof(connectionPeerAddress_)) != 0) {
      return false;
    }
  } else {
    // Bond was formed with a random peer. Accept any random-typed peer and
    // let the EDIV/Rand comparison in buildLlControlResponse gate the key.
    if (!connectionPeerAddressRandom_) {
      return false;
    }
  }
  if (memcmp(bondRecord_.localAddress, address_, sizeof(address_)) != 0) {
    return false;
  }
  const uint8_t localAddressRandom =
      (addressType_ == BleAddressType::kRandomStatic) ? 0x01U : 0x00U;
  if ((bondRecord_.localAddressRandom & 0x01U) != localAddressRandom) {
    return false;
  }

  memcpy(smpStk_, bondRecord_.ltk, sizeof(smpStk_));
  smpStkValid_ = true;
  memcpy(smpPeerLtk_, bondRecord_.ltk, sizeof(smpPeerLtk_));
  smpPeerLtkValid_ = true;
  smpPeerLtkAwaitMasterId_ = false;
  memcpy(smpEncReqRand_, bondRecord_.rand, sizeof(smpEncReqRand_));
  smpEncReqEdiv_ = bondRecord_.ediv;
  smpKeySize_ = (bondRecord_.keySize >= 7U && bondRecord_.keySize <= 16U)
                    ? bondRecord_.keySize
                    : 16U;
  smpBondingRequested_ = true;
  smpExpectInitiatorEncKey_ = false;
  bondKeyPrimedForConnection_ = true;
  return true;
}

void BleRadio::emitBleTrace(const char* message) const {
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
  if (message == nullptr) {
    return;
  }
  if (traceCallback_ != nullptr) {
    traceCallback_(message, traceCallbackContext_);
    return;
  }
  Serial.print(F("[BLE] "));
  Serial.println(message);
#else
  (void)message;
#endif
}

void BleRadio::updateNextConnectionEventTime() {
  const uint32_t intervalUs = static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL;
  if (intervalUs == 0U) {
    connectionNextEventUs_ = bleTimingUs() + 1250UL;
    ++connectionEventCounter_;
    return;
  }

  connectionNextEventUs_ += intervalUs;
  ++connectionEventCounter_;

  // Catch up if caller serviced connection late.
  const uint32_t nowUs = bleTimingUs();
  uint8_t guard = 8U;
  while (guard-- > 0U && timeReachedUs(nowUs, connectionNextEventUs_)) {
    connectionNextEventUs_ += intervalUs;
    ++connectionEventCounter_;
    // For CSA#1, advance the channel-use pointer so it stays synchronised
    // with the central's channel sequence for the missed events.
    if (!connectionUseChSel2_) {
      connectionChanUse_ =
          static_cast<uint8_t>((connectionChanUse_ + connectionHop_) % 37U);
    }
  }
}

uint8_t BleRadio::selectNextDataChannel(bool useCurrentEventCounter) {
  if (connectionUseChSel2_) {
    // pollConnectionEvent() advances connectionEventCounter_ before selecting
    // the channel. Normally we map to counter-1 (the current event). When the
    // caller was late enough to miss the full event window, we map to the
    // already-advanced counter to re-synchronize with the next event.
    const uint16_t eventCounter = useCurrentEventCounter
                                      ? connectionEventCounter_
                                      : ((connectionEventCounter_ > 0U)
                                             ? static_cast<uint16_t>(
                                                   connectionEventCounter_ - 1U)
                                             : 0U);
    const uint16_t prnE = bleCsa2PrnE(eventCounter, connectionChannelId_);
    uint8_t chanNext = static_cast<uint8_t>(prnE % 37U);
    if ((connectionChannelMap_[chanNext >> 3U] & (1U << (chanNext & 0x07U))) != 0U) {
      return chanNext;
    }
    if (connectionChannelCount_ == 0U) {
      return 0U;
    }
    const uint8_t remapIndex =
        static_cast<uint8_t>((static_cast<uint32_t>(connectionChannelCount_) *
                              static_cast<uint32_t>(prnE)) >>
                             16U);
    chanNext = remapChannelByIndex(connectionChannelMap_, remapIndex);
    return chanNext;
  }

  uint8_t chanNext = static_cast<uint8_t>((connectionChanUse_ + connectionHop_) % 37U);
  connectionChanUse_ = chanNext;
  if ((connectionChannelMap_[chanNext >> 3U] & (1U << (chanNext & 0x07U))) == 0U) {
    const uint8_t remapIndex = static_cast<uint8_t>(chanNext % connectionChannelCount_);
    chanNext = remapChannelByIndex(connectionChannelMap_, remapIndex);
  }
  return chanNext;
}

bool BleRadio::setDataChannel(uint8_t dataChannel) {
  if (radio_ == nullptr || dataChannel > 36U) {
    return false;
  }

  const uint8_t freq = bleDataChannelToFrequency(dataChannel);
  radio_->FREQUENCY =
      ((static_cast<uint32_t>(freq) << RADIO_FREQUENCY_FREQUENCY_Pos) &
       RADIO_FREQUENCY_FREQUENCY_Msk) |
      (0UL << RADIO_FREQUENCY_MAP_Pos);
  radio_->DATAWHITE = bleDataWhiteValue(dataChannel);
  return true;
}

void BleRadio::restoreAdvertisingLinkDefaults() {
  if (radio_ == nullptr) {
    return;
  }

  radio_->BASE0 = bleAccessAddressBase(kBleAccessAddress);
  radio_->PREFIX0 = (radio_->PREFIX0 & ~RADIO_PREFIX0_AP0_Msk) |
                    ((bleAccessAddressPrefix(kBleAccessAddress)
                      << RADIO_PREFIX0_AP0_Pos) &
                     RADIO_PREFIX0_AP0_Msk);
  radio_->CRCINIT = (kBleAdvertisingCrcInit & RADIO_CRCINIT_CRCINIT_Msk);
  radio_->TXADDRESS =
      (0UL << RADIO_TXADDRESS_TXADDRESS_Pos) & RADIO_TXADDRESS_TXADDRESS_Msk;
  radio_->RXADDRESSES =
      (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos) &
      RADIO_RXADDRESSES_ADDR0_Msk;
  connectionAttMtu_ = kBleDefaultAttMtu;
  connectionLastTxLlid_ = 0x01U;
  connectionLastTxLength_ = 0U;
  connectionLastTxPlainLlid_ = 0x01U;
  connectionLastTxPlainLength_ = 0U;
  memset(connectionLastTxPlainPayload_, 0, sizeof(connectionLastTxPlainPayload_));
  connectionPendingTxLlid_ = 0x01U;
  connectionPendingTxLength_ = 0U;
  connectionPendingTxValid_ = false;
  memset(connectionPendingTxPayload_, 0, sizeof(connectionPendingTxPayload_));
  connectionUpdatePending_ = false;
  connectionUpdateInstant_ = 0U;
  connectionPendingIntervalUnits_ = 0U;
  connectionPendingLatency_ = 0U;
  connectionPendingTimeoutUnits_ = 0U;
  connectionChannelMapPending_ = false;
  connectionChannelMapInstant_ = 0U;
  memset(connectionPendingChannelMap_, 0, sizeof(connectionPendingChannelMap_));
  connectionPendingChannelCount_ = 0U;
  connectionServiceChangedIndicationsEnabled_ = false;
  connectionServiceChangedIndicationPending_ = false;
  connectionServiceChangedIndicationAwaitingConfirm_ = false;
  connectionBatteryNotificationsEnabled_ = false;
  connectionBatteryNotificationPending_ = false;
  connectionPreparedWriteActive_ = false;
  connectionPreparedWriteHandle_ = 0U;
  connectionPreparedWriteValue_[0] = 0U;
  connectionPreparedWriteValue_[1] = 0U;
  connectionPreparedWriteMask_ = 0U;
  clearConnectionSecurityState();
  scanCycleStartIndex_ = 0U;
  buildAdvertisingPacket();
  buildScanResponsePacket();
  setAdvertisingChannel(BleAdvertisingChannel::k37);
}

bool BleRadio::configureBle1M() {
  if (radio_ == nullptr) {
    return false;
  }

  connected_ = false;

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitDisabled(120000UL);
  radio_->TASKS_SOFTRESET = RADIO_TASKS_SOFTRESET_TASKS_SOFTRESET_Trigger;

  radio_->MODE = ((RADIO_MODE_MODE_Ble_1Mbit << RADIO_MODE_MODE_Pos) &
                  RADIO_MODE_MODE_Msk);
  radio_->TIMING = ((RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) &
                    RADIO_TIMING_RU_Msk);

  uint32_t pcnf0 = 0;
  pcnf0 |= (8UL << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk;
  pcnf0 |= (1UL << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk;
  pcnf0 |= (RADIO_PCNF0_S1INCL_Automatic << RADIO_PCNF0_S1INCL_Pos) &
           RADIO_PCNF0_S1INCL_Msk;
  pcnf0 |= (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos) &
           RADIO_PCNF0_PLEN_Msk;
  pcnf0 |= (RADIO_PCNF0_CRCINC_Exclude << RADIO_PCNF0_CRCINC_Pos) &
           RADIO_PCNF0_CRCINC_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_TERMLEN_Pos) & RADIO_PCNF0_TERMLEN_Msk;
  radio_->PCNF0 = pcnf0;

  uint32_t pcnf1 = 0;
  pcnf1 |= (255UL << RADIO_PCNF1_MAXLEN_Pos) & RADIO_PCNF1_MAXLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk;
  pcnf1 |= (3UL << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk;
  pcnf1 |= (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) &
           RADIO_PCNF1_ENDIAN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) &
           RADIO_PCNF1_WHITEEN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEOFFSET_Include << RADIO_PCNF1_WHITEOFFSET_Pos) &
           RADIO_PCNF1_WHITEOFFSET_Msk;
  radio_->PCNF1 = pcnf1;

  radio_->BASE0 = bleAccessAddressBase(kBleAccessAddress);
  radio_->PREFIX0 = (radio_->PREFIX0 & ~RADIO_PREFIX0_AP0_Msk) |
                    ((bleAccessAddressPrefix(kBleAccessAddress)
                      << RADIO_PREFIX0_AP0_Pos) &
                     RADIO_PREFIX0_AP0_Msk);
  radio_->TXADDRESS =
      (0UL << RADIO_TXADDRESS_TXADDRESS_Pos) & RADIO_TXADDRESS_TXADDRESS_Msk;
  radio_->RXADDRESSES =
      (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos) &
      RADIO_RXADDRESSES_ADDR0_Msk;

  uint32_t crccnf = 0;
  crccnf |= (RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos) &
            RADIO_CRCCNF_LEN_Msk;
  crccnf |= (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos) &
            RADIO_CRCCNF_SKIPADDR_Msk;
  radio_->CRCCNF = crccnf;
  radio_->CRCPOLY = (kBleCrcPolynomial & RADIO_CRCPOLY_CRCPOLY_Msk);
  radio_->CRCINIT = (kBleAdvertisingCrcInit & RADIO_CRCINIT_CRCINIT_Msk);

  radio_->TIFS = 150U;
  radio_->DATAWHITE = bleDataWhiteValue(37U);
  clearRadioCoreEvents(radio_);
  return true;
}

bool BleRadio::waitDisabled(uint32_t spinLimit) {
  if (radio_ == nullptr) {
    return false;
  }

  radio_->EVENTS_DISABLED = 0U;
  while (spinLimit-- > 0U) {
    if (radio_->EVENTS_DISABLED != 0U) {
      return true;
    }
    const uint32_t state =
        (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (state == RADIO_STATE_STATE_Disabled) {
      return true;
    }
  }
  return false;
}

bool BleRadio::waitForEnd(uint32_t spinLimit) {
  if (radio_ == nullptr) {
    return false;
  }

  while (spinLimit-- > 0U) {
    if (radio_->EVENTS_PHYEND != 0U || radio_->EVENTS_END != 0U) {
      return true;
    }
  }
  return false;
}

bool BleRadio::setAdvertisingChannel(BleAdvertisingChannel channel) {
  if (radio_ == nullptr) {
    return false;
  }

  const uint8_t freq = bleChannelToFrequency(channel);
  const uint8_t channelIndex = bleChannelToIndex(channel);

  radio_->FREQUENCY =
      ((static_cast<uint32_t>(freq) << RADIO_FREQUENCY_FREQUENCY_Pos) &
       RADIO_FREQUENCY_FREQUENCY_Msk) |
      (0UL << RADIO_FREQUENCY_MAP_Pos);

  radio_->DATAWHITE = bleDataWhiteValue(channelIndex);
  return true;
}

uint32_t BleRadio::txPowerRegFromDbm(int8_t dbm) {
  if (dbm >= 8) {
    return RADIO_TXPOWER_TXPOWER_Pos8dBm;
  }
  if (dbm >= 7) {
    return RADIO_TXPOWER_TXPOWER_Pos7dBm;
  }
  if (dbm >= 6) {
    return RADIO_TXPOWER_TXPOWER_Pos6dBm;
  }
  if (dbm >= 5) {
    return RADIO_TXPOWER_TXPOWER_Pos5dBm;
  }
  if (dbm >= 4) {
    return RADIO_TXPOWER_TXPOWER_Pos4dBm;
  }
  if (dbm >= 3) {
    return RADIO_TXPOWER_TXPOWER_Pos3dBm;
  }
  if (dbm >= 2) {
    return RADIO_TXPOWER_TXPOWER_Pos2dBm;
  }
  if (dbm >= 1) {
    return RADIO_TXPOWER_TXPOWER_Pos1dBm;
  }
  if (dbm >= 0) {
    return RADIO_TXPOWER_TXPOWER_0dBm;
  }
  if (dbm >= -1) {
    return RADIO_TXPOWER_TXPOWER_Neg1dBm;
  }
  if (dbm >= -2) {
    return RADIO_TXPOWER_TXPOWER_Neg2dBm;
  }
  if (dbm >= -3) {
    return RADIO_TXPOWER_TXPOWER_Neg3dBm;
  }
  if (dbm >= -4) {
    return RADIO_TXPOWER_TXPOWER_Neg4dBm;
  }
  if (dbm >= -5) {
    return RADIO_TXPOWER_TXPOWER_Neg5dBm;
  }
  if (dbm >= -6) {
    return RADIO_TXPOWER_TXPOWER_Neg6dBm;
  }
  if (dbm >= -7) {
    return RADIO_TXPOWER_TXPOWER_Neg7dBm;
  }
  if (dbm >= -8) {
    return RADIO_TXPOWER_TXPOWER_Neg8dBm;
  }
  if (dbm >= -9) {
    return RADIO_TXPOWER_TXPOWER_Neg9dBm;
  }
  if (dbm >= -10) {
    return RADIO_TXPOWER_TXPOWER_Neg10dBm;
  }
  if (dbm >= -12) {
    return RADIO_TXPOWER_TXPOWER_Neg12dBm;
  }
  if (dbm >= -14) {
    return RADIO_TXPOWER_TXPOWER_Neg14dBm;
  }
  if (dbm >= -16) {
    return RADIO_TXPOWER_TXPOWER_Neg16dBm;
  }
  if (dbm >= -18) {
    return RADIO_TXPOWER_TXPOWER_Neg18dBm;
  }
  if (dbm >= -20) {
    return RADIO_TXPOWER_TXPOWER_Neg20dBm;
  }
  if (dbm >= -28) {
    return RADIO_TXPOWER_TXPOWER_Neg28dBm;
  }
  if (dbm >= -40) {
    return RADIO_TXPOWER_TXPOWER_Neg40dBm;
  }
  return RADIO_TXPOWER_TXPOWER_Neg46dBm;
}

}  // namespace xiao_nrf54l15
