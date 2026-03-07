#include "Arduino.h"

#include <stdint.h>

#include "cmsis.h"
#include "nrf54l15.h"
#include "variant.h"

extern uint32_t SystemCoreClock;
extern void SystemCoreClockUpdate(void);
extern void nrf54l15_clean_idle_service(void);

static volatile uint32_t g_millis_ticks = 0;
static volatile uint32_t* const kScbScr = (volatile uint32_t*)0xE000ED10UL;
static const uint32_t kScbScrSleepDeep_Msk = (1UL << 2);
static const uint32_t kScbScrSleepOnExit_Msk = (1UL << 1);

#if NRF54L15_GRTC_IRQ_GROUP == 2U
static const IRQn_Type kLowPowerTickIrq = GRTC_2_IRQn;
#elif NRF54L15_GRTC_IRQ_GROUP == 1U
static const IRQn_Type kLowPowerTickIrq = GRTC_1_IRQn;
#else
static const IRQn_Type kLowPowerTickIrq = GRTC_0_IRQn;
#endif

static void disableSystemOffRetention(void)
{
    for (uint32_t i = 0; i < MEMCONF_POWER_MaxCount; ++i) {
        NRF_MEMCONF->POWER[i].RET = 0U;
        NRF_MEMCONF->POWER[i].RET2 = 0U;
    }
}

#if defined(NRF54L15_CLEAN_POWER_LOW)
#ifdef NRF_TRUSTZONE_NONSECURE
#define NRF54L15_CLEAN_GRTC_BASE 0x400E2000UL
#else
#define NRF54L15_CLEAN_GRTC_BASE 0x500E2000UL
#endif
static NRF_GRTC_Type* const g_low_power_grtc =
    (NRF_GRTC_Type*)NRF54L15_CLEAN_GRTC_BASE;
// On XIAO nRF54L15 secure CPUAPP, the Zephyr-derived allowed mask is 0x67
// (channels 0,1,2,5,6). Channel 5 is reserved here as the core's low-power
// delay wake source so delay() can sleep until its deadline instead of waking
// every millisecond.
static const uint8_t kLowPowerDelayChannel = 5U;
static volatile uint8_t g_low_power_delay_fired = 0U;

static uint64_t readLowPowerCounterUs(void)
{
    for (uint8_t attempt = 0U; attempt < 32U; ++attempt) {
        const uint32_t hi0 = NRF54L15_GRTC_SYSCOUNTER(g_low_power_grtc).SYSCOUNTERH;
        const uint32_t lo = NRF54L15_GRTC_SYSCOUNTER(g_low_power_grtc).SYSCOUNTERL;
        const uint32_t hi1 = NRF54L15_GRTC_SYSCOUNTER(g_low_power_grtc).SYSCOUNTERH;

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

        return ((uint64_t)high0 << 32U) | (uint64_t)lo;
    }

    const uint32_t hi =
        NRF54L15_GRTC_SYSCOUNTER(g_low_power_grtc).SYSCOUNTERH &
        GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk;
    const uint32_t lo = NRF54L15_GRTC_SYSCOUNTER(g_low_power_grtc).SYSCOUNTERL;
    return ((uint64_t)hi << 32U) | (uint64_t)lo;
}

static void lowPowerDisarmDelayWake(void)
{
    NRF54L15_GRTC_INTENCLR_REG(g_low_power_grtc) = (1UL << kLowPowerDelayChannel);
    g_low_power_grtc->CC[kLowPowerDelayChannel].CCEN =
        (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
    g_low_power_grtc->EVENTS_COMPARE[kLowPowerDelayChannel] = 0U;
}

static void lowPowerArmDelayWake(uint64_t targetUs)
{
    const uint32_t lo = (uint32_t)(targetUs & 0xFFFFFFFFULL);
    const uint32_t hi = (uint32_t)((targetUs >> 32U) & 0xFFFFFUL);

    g_low_power_delay_fired = 0U;
    g_low_power_grtc->EVENTS_COMPARE[kLowPowerDelayChannel] = 0U;
    g_low_power_grtc->CC[kLowPowerDelayChannel].CCEN =
        (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
    g_low_power_grtc->CC[kLowPowerDelayChannel].CCL = lo;
    g_low_power_grtc->CC[kLowPowerDelayChannel].CCH =
        (hi << GRTC_CC_CCH_CCH_Pos) & GRTC_CC_CCH_CCH_Msk;
    NVIC->ICPR[((uint32_t)kLowPowerTickIrq) >> 5U] =
        (1UL << (((uint32_t)kLowPowerTickIrq) & 0x1FUL));
    NRF54L15_GRTC_INTENSET_REG(g_low_power_grtc) = (1UL << kLowPowerDelayChannel);
    g_low_power_grtc->CC[kLowPowerDelayChannel].CCEN =
        (GRTC_CC_CCEN_ACTIVE_Enable << GRTC_CC_CCEN_ACTIVE_Pos);
}

static void initLowPowerTimebase(void)
{
    uint32_t clkcfg = g_low_power_grtc->CLKCFG;
    clkcfg &= ~GRTC_CLKCFG_CLKSEL_Msk;
    clkcfg |= (GRTC_CLKCFG_CLKSEL_SystemLFCLK << GRTC_CLKCFG_CLKSEL_Pos);
    g_low_power_grtc->CLKCFG = clkcfg;

    g_low_power_grtc->MODE =
        (GRTC_MODE_AUTOEN_Default << GRTC_MODE_AUTOEN_Pos) |
        (GRTC_MODE_SYSCOUNTEREN_Enabled << GRTC_MODE_SYSCOUNTEREN_Pos);
    g_low_power_grtc->TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;

    // GRTC state can survive in ways that matter across debug/program cycles.
    // Clear the whole interrupt/event group before using the low-power delay
    // channel, or a stale compare event on another channel can trap the CPU in
    // the IRQ.
    NRF54L15_GRTC_INTENCLR_REG(g_low_power_grtc) = 0xFFFFFFFFUL;
    for (uint8_t channel = 0; channel < GRTC_CC_MaxCount; ++channel) {
        g_low_power_grtc->CC[channel].CCEN =
            (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
        g_low_power_grtc->EVENTS_COMPARE[channel] = 0U;
    }

    NVIC->ICPR[((uint32_t)kLowPowerTickIrq) >> 5U] =
        (1UL << (((uint32_t)kLowPowerTickIrq) & 0x1FUL));
    NVIC_SetPriority(kLowPowerTickIrq, 3U);
    NVIC_EnableIRQ(kLowPowerTickIrq);
}

#if NRF54L15_GRTC_IRQ_GROUP == 2U
void GRTC_2_IRQHandler(void)
#elif NRF54L15_GRTC_IRQ_GROUP == 1U
void GRTC_1_IRQHandler(void)
#else
void GRTC_0_IRQHandler(void)
#endif
{
    if (g_low_power_grtc->EVENTS_COMPARE[kLowPowerDelayChannel] != 0U) {
        g_low_power_grtc->EVENTS_COMPARE[kLowPowerDelayChannel] = 0U;
        g_low_power_grtc->CC[kLowPowerDelayChannel].CCEN =
            (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
        NRF54L15_GRTC_INTENCLR_REG(g_low_power_grtc) =
            (1UL << kLowPowerDelayChannel);
        g_low_power_delay_fired = 1U;
    }
}
#endif

void SysTick_Handler(void)
{
#if !defined(NRF54L15_CLEAN_POWER_LOW)
    ++g_millis_ticks;
#endif
}

void initSysTick(void)
{
    SystemCoreClockUpdate();

    uint32_t ticks = SystemCoreClock / 1000UL;
    if (ticks == 0UL) {
        ticks = 64000UL;
    }

    SysTick->CTRL = 0;
    SysTick->LOAD = ticks - 1UL;
    SysTick->VAL = 0UL;
#if defined(NRF54L15_CLEAN_POWER_LOW)
    initLowPowerTimebase();
#else
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
#endif
}

unsigned long millis(void)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    return (unsigned long)(readLowPowerCounterUs() / 1000ULL);
#else
    return (unsigned long)g_millis_ticks;
#endif
}

unsigned long micros(void)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    return (unsigned long)(uint32_t)readLowPowerCounterUs();
#else
    uint32_t ms_a;
    uint32_t ms_b;
    uint32_t val;
    uint32_t load;

    do {
        ms_a = g_millis_ticks;
        val = SysTick->VAL;
        ms_b = g_millis_ticks;
    } while (ms_a != ms_b);

    load = SysTick->LOAD + 1UL;
    uint32_t elapsed = load - val;
    uint32_t cycles_per_us = (SystemCoreClock == 0UL) ? 64UL : (SystemCoreClock / 1000000UL);
    if (cycles_per_us == 0UL) {
        cycles_per_us = 64UL;
    }

    return (unsigned long)(ms_a * 1000UL + (elapsed / cycles_per_us));
#endif
}

void delay(unsigned long ms)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    if (ms == 0UL) {
        nrf54l15_clean_idle_service();
        return;
    }

    const uint64_t targetUs = readLowPowerCounterUs() + ((uint64_t)ms * 1000ULL);

    if ((__get_PRIMASK() & 1U) != 0U) {
        while ((int64_t)(targetUs - readLowPowerCounterUs()) > 0) {
            nrf54l15_clean_idle_service();
            __NOP();
        }
        return;
    }

    while ((int64_t)(targetUs - readLowPowerCounterUs()) > 0) {
        nrf54l15_clean_idle_service();
        lowPowerArmDelayWake(targetUs);
        *kScbScr &= ~(kScbScrSleepDeep_Msk | kScbScrSleepOnExit_Msk);
        while ((g_low_power_delay_fired == 0U) &&
               ((int64_t)(targetUs - readLowPowerCounterUs()) > 0)) {
            __asm volatile("wfi");
        }
        lowPowerDisarmDelayWake();
    }
#else
    const unsigned long start = millis();
    while ((millis() - start) < ms) {
        nrf54l15_clean_idle_service();
        *kScbScr &= ~(kScbScrSleepDeep_Msk | kScbScrSleepOnExit_Msk);
        __asm volatile("wfi");
    }
#endif
}

void delayMicroseconds(unsigned int us)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    uint32_t cycles_per_us = (SystemCoreClock == 0UL) ? 64UL : (SystemCoreClock / 1000000UL);
    if (cycles_per_us == 0UL) {
        cycles_per_us = 64UL;
    }
    volatile uint32_t cycles = cycles_per_us * (uint32_t)us;
    while (cycles-- > 0U) {
        __NOP();
    }
#else
    const unsigned long start = micros();
    while ((micros() - start) < (unsigned long)us) {
        __NOP();
    }
#endif
}

void nrf54l15_core_prepare_system_off(void)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    NRF54L15_GRTC_INTENCLR_REG(g_low_power_grtc) =
        (1UL << kLowPowerDelayChannel);
    g_low_power_grtc->CC[kLowPowerDelayChannel].CCEN =
        (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
    g_low_power_grtc->EVENTS_COMPARE[kLowPowerDelayChannel] = 0U;
    NVIC_DisableIRQ(kLowPowerTickIrq);
    NVIC->ICPR[((uint32_t)kLowPowerTickIrq) >> 5U] =
        (1UL << (((uint32_t)kLowPowerTickIrq) & 0x1FUL));
#endif

    xiaoNrf54l15EnterLowestPowerBoardState();
    SysTick->CTRL = 0U;
}

void nrf54l15_core_disable_system_off_retention(void)
{
    disableSystemOffRetention();
}
