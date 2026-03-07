#include "Arduino.h"

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
// (channels 0,1,2,5,6). Channel 11 does not belong to that domain, so using
// it here can leave delay()/WFI stuck waiting for a tick that never arrives.
static const uint8_t kLowPowerTickChannel = 5U;
static const uint32_t kLowPowerTickUs = 1000UL;

static inline void lowPowerTickScheduleNext(void)
{
    g_low_power_grtc->CC[kLowPowerTickChannel].CCADD =
        ((kLowPowerTickUs << GRTC_CC_CCADD_VALUE_Pos) & GRTC_CC_CCADD_VALUE_Msk) |
        (GRTC_CC_CCADD_REFERENCE_SYSCOUNTER << GRTC_CC_CCADD_REFERENCE_Pos);
}

static void initLowPowerTick(void)
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
    // Clear the whole interrupt/event group before arming the 1 ms tick, or a
    // stale compare event on another channel can trap the CPU in the IRQ.
    NRF54L15_GRTC_INTENCLR_REG(g_low_power_grtc) = 0xFFFFFFFFUL;
    for (uint8_t channel = 0; channel < GRTC_CC_MaxCount; ++channel) {
        g_low_power_grtc->CC[channel].CCEN =
            (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
        g_low_power_grtc->EVENTS_COMPARE[channel] = 0U;
    }

    NRF54L15_GRTC_INTENSET_REG(g_low_power_grtc) =
        (1UL << kLowPowerTickChannel);
    g_low_power_grtc->CC[kLowPowerTickChannel].CCEN =
        (GRTC_CC_CCEN_ACTIVE_Enable << GRTC_CC_CCEN_ACTIVE_Pos);
    lowPowerTickScheduleNext();

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
    if (g_low_power_grtc->EVENTS_COMPARE[kLowPowerTickChannel] != 0U) {
        g_low_power_grtc->EVENTS_COMPARE[kLowPowerTickChannel] = 0U;
        ++g_millis_ticks;
        lowPowerTickScheduleNext();
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
    // Keep SysTick running as a fast timebase for short busy-waits,
    // but use GRTC IRQ tick for millis() while sleeping.
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_ENABLE_Msk;
    initLowPowerTick();
#else
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
#endif
}

unsigned long millis(void)
{
    return (unsigned long)g_millis_ticks;
}

unsigned long micros(void)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    return (unsigned long)(g_millis_ticks * 1000UL);
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
    const unsigned long start = millis();
    while ((millis() - start) < ms) {
        nrf54l15_clean_idle_service();
        *kScbScr &= ~(kScbScrSleepDeep_Msk | kScbScrSleepOnExit_Msk);
        __asm volatile("wfi");
    }
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
        (1UL << kLowPowerTickChannel);
    g_low_power_grtc->CC[kLowPowerTickChannel].CCEN =
        (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
    g_low_power_grtc->EVENTS_COMPARE[kLowPowerTickChannel] = 0U;
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
