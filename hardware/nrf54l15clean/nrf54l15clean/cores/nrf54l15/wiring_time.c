#include "Arduino.h"

#include <stdint.h>

#include "cmsis.h"
#include "nrf54l15.h"
#include "variant.h"

extern uint32_t SystemCoreClock;
extern void SystemCoreClockUpdate(void);
extern void nrf54l15_clean_idle_service(void);
void nrf54l15_core_prepare_system_off(void);
void nrf54l15_core_disable_system_off_retention(void);

static volatile uint32_t g_millis_ticks = 0;
static volatile uint32_t* const kScbScr = (volatile uint32_t*)0xE000ED10UL;
static const uint32_t kScbScrSleepDeep_Msk = (1UL << 2);
static const uint32_t kScbScrSleepOnExit_Msk = (1UL << 1);
static const uint16_t kSystemOffTimeoutLfclk = 5U;
static const uint8_t kSystemOffWakeLeadLfclk = 4U;
static const uint32_t kSystemOffLfclkFrequencyHz = 32768UL;
static const uint32_t kSystemOffMaxCcLatchWaitUs = 77UL;
static const uint32_t kGrtcStartSettleUs = 93UL;
#if defined(ARDUINO_XIAO_NRF54L15)
static const uint32_t kZephyrAllowedCcMaskXiao = 0x67UL;
static const uint8_t kZephyrMainCcChannelXiao = 1U;
#endif

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

static uint32_t beginIdleSleep(void)
{
    const uint32_t restoreRaw = nrf54l15_core_enter_idle_cpu_scaling();
    *kScbScr &= ~(kScbScrSleepDeep_Msk | kScbScrSleepOnExit_Msk);
    return restoreRaw;
}

static void endIdleSleep(uint32_t restoreRaw)
{
    nrf54l15_core_exit_idle_cpu_scaling(restoreRaw);
}

static uint8_t highestSetBit(uint32_t mask)
{
    return (uint8_t)(31U - (uint32_t)__builtin_clz(mask));
}

static bool lfclkRunningFrom(uint32_t src)
{
    const uint32_t stat = NRF_CLOCK->LFCLK.STAT;
    const bool running =
        ((stat & CLOCK_LFCLK_STAT_STATE_Msk) >> CLOCK_LFCLK_STAT_STATE_Pos) ==
        CLOCK_LFCLK_STAT_STATE_Running;
    const uint32_t currentSrc =
        (stat & CLOCK_LFCLK_STAT_SRC_Msk) >> CLOCK_LFCLK_STAT_SRC_Pos;
    return running && (currentSrc == src);
}

static bool waitForLfclkStarted(uint32_t expectedSrc, uint32_t spinLimit)
{
    while (spinLimit-- > 0U) {
        if ((NRF_CLOCK->EVENTS_LFCLKSTARTED != 0U) &&
            lfclkRunningFrom(expectedSrc)) {
            return true;
        }
    }
    return false;
}

static void startLfclkSource(uint32_t src)
{
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0U;
    NRF_CLOCK->LFCLK.SRC =
        (src << CLOCK_LFCLK_SRC_SRC_Pos) & CLOCK_LFCLK_SRC_SRC_Msk;
    __asm volatile("dsb 0xF" ::: "memory");
    NRF_CLOCK->TASKS_LFCLKSTART = CLOCK_TASKS_LFCLKSTART_TASKS_LFCLKSTART_Trigger;
}

static void ensureSystemOffLfxoRunning(void)
{
    static const uint32_t kLfclkStartSpinLimit = 2000000UL;

    if (lfclkRunningFrom(CLOCK_LFCLK_STAT_SRC_LFXO)) {
        return;
    }

    if (!lfclkRunningFrom(CLOCK_LFCLK_STAT_SRC_LFRC)) {
        startLfclkSource(CLOCK_LFCLK_SRC_SRC_LFRC);
        if (!waitForLfclkStarted(CLOCK_LFCLK_STAT_SRC_LFRC,
                                 kLfclkStartSpinLimit)) {
            return;
        }
    }

    startLfclkSource(CLOCK_LFCLK_SRC_SRC_LFXO);
    (void)waitForLfclkStarted(CLOCK_LFCLK_STAT_SRC_LFXO, kLfclkStartSpinLimit);
}

static bool grtcSyscounterReady(NRF_GRTC_Type* grtc)
{
    (void)NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERL;
    __asm volatile("dsb 0xF" ::: "memory");
    const uint32_t high = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERH;
    return ((high & GRTC_SYSCOUNTER_SYSCOUNTERH_BUSY_Msk) >>
            GRTC_SYSCOUNTER_SYSCOUNTERH_BUSY_Pos) ==
           GRTC_SYSCOUNTER_SYSCOUNTERH_BUSY_Ready;
}

static void ensureGrtcReady(NRF_GRTC_Type* grtc)
{
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
            (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Active <<
             GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
        __asm volatile("dsb 0xF" ::: "memory");
    }

    while (!grtcSyscounterReady(grtc)) {
        __NOP();
    }

    if (restoreActive) {
        NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE =
            (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_NotActive <<
             GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
        __asm volatile("dsb 0xF" ::: "memory");
    }
}

static uint64_t readGrtcCounterUs(NRF_GRTC_Type* grtc)
{
    for (uint8_t attempt = 0U; attempt < 32U; ++attempt) {
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

        return ((uint64_t)high0 << 32U) | (uint64_t)lo;
    }

    const uint32_t hi =
        NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERH &
        GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk;
    const uint32_t lo = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERL;
    return ((uint64_t)hi << 32U) | (uint64_t)lo;
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

static uint8_t systemOffWakeChannel(void)
{
#if defined(ARDUINO_XIAO_NRF54L15)
    const uint32_t available =
        kZephyrAllowedCcMaskXiao & ~(1UL << kZephyrMainCcChannelXiao);
    return highestSetBit(available);
#else
    return 1U;
#endif
}

static void waitForSystemOffWakeLatch(void)
{
    const uint32_t waitUs =
        ((uint32_t)kSystemOffTimeoutLfclk * 1000000UL) /
            kSystemOffLfclkFrequencyHz +
        kSystemOffMaxCcLatchWaitUs;
    delayMicroseconds(waitUs);
}

static void programSystemOffWakeUs(uint32_t delayUs)
{
    NRF_GRTC_Type* const grtc = NRF_GRTC;
    if (delayUs == 0UL) {
        delayUs = 1UL;
    }

    ensureSystemOffLfxoRunning();

    uint32_t clkcfg = grtc->CLKCFG;
    clkcfg &= ~GRTC_CLKCFG_CLKSEL_Msk;
    clkcfg |= (GRTC_CLKCFG_CLKSEL_LFXO << GRTC_CLKCFG_CLKSEL_Pos);
    grtc->CLKCFG = clkcfg;

    uint32_t mode = grtc->MODE;
    mode &= ~(GRTC_MODE_AUTOEN_Msk | GRTC_MODE_SYSCOUNTEREN_Msk);
    mode |= (GRTC_MODE_AUTOEN_Default << GRTC_MODE_AUTOEN_Pos);
    mode |= (GRTC_MODE_SYSCOUNTEREN_Enabled << GRTC_MODE_SYSCOUNTEREN_Pos);
    grtc->MODE = mode;
    __asm volatile("dsb 0xF" ::: "memory");

    grtc->TIMEOUT = (((uint32_t)kSystemOffTimeoutLfclk << GRTC_TIMEOUT_VALUE_Pos) &
                     GRTC_TIMEOUT_VALUE_Msk);
    grtc->WAKETIME =
        (((uint32_t)kSystemOffWakeLeadLfclk << GRTC_WAKETIME_VALUE_Pos) &
         GRTC_WAKETIME_VALUE_Msk);

    const uint8_t wakeChannel = systemOffWakeChannel();
    for (uint8_t channel = 0U; channel < GRTC_CC_MaxCount; ++channel) {
        NRF54L15_GRTC_INTENCLR_REG(grtc) = (1UL << channel);
        if (channel != wakeChannel) {
            grtc->CC[channel].CCEN =
                (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
        }
    }

    ensureGrtcReady(grtc);

    const uint64_t wakeTimestamp = readGrtcCounterUs(grtc) + delayUs;
    grtc->EVENTS_COMPARE[wakeChannel] = 0U;
    grtc->CC[wakeChannel].CCEN =
        (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
    grtc->CC[wakeChannel].CCL = (uint32_t)(wakeTimestamp & 0xFFFFFFFFULL);
    grtc->CC[wakeChannel].CCH =
        ((uint32_t)((wakeTimestamp >> 32U) & 0xFFFFFUL) <<
         GRTC_CC_CCH_CCH_Pos) &
        GRTC_CC_CCH_CCH_Msk;
    NRF54L15_GRTC_INTENSET_REG(grtc) = (1UL << wakeChannel);
    grtc->CC[wakeChannel].CCEN =
        (GRTC_CC_CCEN_ACTIVE_Enable << GRTC_CC_CCEN_ACTIVE_Pos);

    waitForSystemOffWakeLatch();
}

static void enterTimedSystemOff(bool disableRamRetention, uint32_t delayUs)
{
    programSystemOffWakeUs(delayUs);
    nrf54l15_core_prepare_system_off();
    if (disableRamRetention) {
        nrf54l15_core_disable_system_off_retention();
    }

    __asm volatile("cpsid i" ::: "memory");
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
    NRF_RESET->RESETREAS = 0xFFFFFFFFUL;
    __asm volatile("dsb 0xF" ::: "memory");

    NRF_REGULATORS->SYSTEMOFF = REGULATORS_SYSTEMOFF_SYSTEMOFF_Enter;
    __asm volatile("dsb 0xF" ::: "memory");
    while (true) {
        __asm volatile("wfe");
    }
}

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
        const uint32_t restoreRaw = beginIdleSleep();
        while ((g_low_power_delay_fired == 0U) &&
               ((int64_t)(targetUs - readLowPowerCounterUs()) > 0)) {
            __asm volatile("wfi");
        }
        endIdleSleep(restoreRaw);
        lowPowerDisarmDelayWake();
    }
#else
    const unsigned long start = millis();
    while ((millis() - start) < ms) {
        nrf54l15_clean_idle_service();
        __NOP();
    }
#endif
}

void delaySystemOff(unsigned long ms)
{
    uint32_t delayUs = (uint32_t)ms;
    if (delayUs > (0xFFFFFFFFUL / 1000UL)) {
        delayUs = 0xFFFFFFFFUL / 1000UL;
    }
    enterTimedSystemOff(false, delayUs * 1000UL);
}

void delaySystemOffNoRetention(unsigned long ms)
{
    uint32_t delayUs = (uint32_t)ms;
    if (delayUs > (0xFFFFFFFFUL / 1000UL)) {
        delayUs = 0xFFFFFFFFUL / 1000UL;
    }
    enterTimedSystemOff(true, delayUs * 1000UL);
}

void delayMicroseconds(unsigned int us)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    const uint64_t targetUs = readLowPowerCounterUs() + (uint64_t)us;
    while ((int64_t)(targetUs - readLowPowerCounterUs()) > 0) {
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
