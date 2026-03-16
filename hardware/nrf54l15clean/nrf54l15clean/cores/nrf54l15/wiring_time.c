#include "Arduino.h"

#include <stdint.h>

#include "cmsis.h"
#include "nrf54l15.h"
#include "variant.h"

extern uint32_t SystemCoreClock;
extern void SystemCoreClockUpdate(void);
extern void nrf54l15_clean_idle_service(void);
extern uint8_t nrf54l15_bridge_serial_active(void);
void nrf54l15_core_prepare_system_off_wake_timebase(void);
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
static const uint32_t kSystemOffMinimumLatencyGuardUs = 1000UL;
static const uint32_t kGrtcStartSettleUs = 93UL;
static const uint16_t kLowPowerDelayTimeoutLfclk = 5U;
static const uint8_t kLowPowerDelayWakeLfclk = 4U;
static const unsigned long kLowPowerDelayBoardCollapseThresholdMs = 30UL;
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

static void clearSystemOffVprRetention(void)
{
    if (MEMCONF_POWER_MaxCount > 1U) {
        NRF_MEMCONF->POWER[1U].RET &= ~MEMCONF_POWER_RET_MEM0_Msk;
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

static void busyWaitApproxUs(uint32_t us)
{
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
// (channels 0,1,2,5,6). Channel 5 is reserved here as the core's tickless
// wake source so delay() and delayLowPowerIdle() can sleep until deadline
// instead of waking every millisecond.
static const uint8_t kLowPowerDelayChannel = 5U;
static volatile uint8_t g_low_power_delay_fired = 0U;
static volatile uint8_t g_low_power_timebase_initialized = 0U;

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
    if (g_low_power_timebase_initialized != 0U) {
        return;
    }

    ensureSystemOffLfxoRunning();

    uint32_t clkcfg = g_low_power_grtc->CLKCFG;
    clkcfg &= ~GRTC_CLKCFG_CLKSEL_Msk;
    clkcfg |= (GRTC_CLKCFG_CLKSEL_LFXO << GRTC_CLKCFG_CLKSEL_Pos);
    g_low_power_grtc->CLKCFG = clkcfg;

    g_low_power_grtc->TIMEOUT =
        (((uint32_t)kLowPowerDelayTimeoutLfclk << GRTC_TIMEOUT_VALUE_Pos) &
         GRTC_TIMEOUT_VALUE_Msk);
    g_low_power_grtc->WAKETIME =
        (((uint32_t)kLowPowerDelayWakeLfclk << GRTC_WAKETIME_VALUE_Pos) &
         GRTC_WAKETIME_VALUE_Msk);
    g_low_power_grtc->MODE =
        (GRTC_MODE_AUTOEN_CpuActive << GRTC_MODE_AUTOEN_Pos) |
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
    g_low_power_timebase_initialized = 1U;
}

void nrf54l15_core_bootstrap_low_power_timebase(void)
{
    initLowPowerTimebase();
}

static void delayUntilLowPowerCounterUs(uint64_t targetUs)
{
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

static uint8_t delayBoardStateEnter(xiao_nrf54l15_board_state_t* state)
{
#if defined(ARDUINO_XIAO_NRF54L15)
    if (state == 0 || xiaoNrf54l15SaveBoardState(state) == 0U) {
        return 0U;
    }

    xiaoNrf54l15EnterLowestPowerBoardState();
    return 1U;
#else
    (void)state;
    return 0U;
#endif
}

static void delayBoardStateExit(const xiao_nrf54l15_board_state_t* state, uint8_t active)
{
#if defined(ARDUINO_XIAO_NRF54L15)
    if (active != 0U) {
        (void)xiaoNrf54l15RestoreBoardState(state);
    }
#else
    (void)state;
    (void)active;
#endif
}

static uint8_t delayShouldCollapseBoardState(unsigned long ms)
{
#if defined(ARDUINO_XIAO_NRF54L15)
    return (ms >= kLowPowerDelayBoardCollapseThresholdMs) &&
           (nrf54l15_bridge_serial_active() == 0U);
#else
    (void)ms;
    return 0U;
#endif
}

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

void nrf54l15_core_prepare_system_off_wake_timebase(void)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    if (g_low_power_timebase_initialized != 0U) {
        NRF54L15_GRTC_INTENCLR_REG(g_low_power_grtc) = 0xFFFFFFFFUL;
        for (uint8_t channel = 0U; channel < GRTC_CC_MaxCount; ++channel) {
            g_low_power_grtc->CC[channel].CCEN =
                (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
            g_low_power_grtc->EVENTS_COMPARE[channel] = 0U;
        }

        NRF54L15_GRTC_SYSCOUNTER(g_low_power_grtc).ACTIVE =
            (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_NotActive <<
             GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
        __asm volatile("dsb 0xF" ::: "memory");
        g_low_power_grtc->TASKS_STOP = GRTC_TASKS_STOP_TASKS_STOP_Trigger;
        __asm volatile("dsb 0xF" ::: "memory");
        busyWaitApproxUs(kGrtcStartSettleUs);

        NVIC_DisableIRQ(kLowPowerTickIrq);
        NVIC->ICPR[((uint32_t)kLowPowerTickIrq) >> 5U] =
            (1UL << (((uint32_t)kLowPowerTickIrq) & 0x1FUL));
        g_low_power_delay_fired = 0U;
        g_low_power_timebase_initialized = 0U;
    }
#endif
}

static uint32_t systemOffMinimumLatencyUs(void)
{
    return ((((uint32_t)kSystemOffTimeoutLfclk +
              (uint32_t)kSystemOffWakeLeadLfclk) *
             1000000UL) /
            kSystemOffLfclkFrequencyHz) +
           kSystemOffMinimumLatencyGuardUs;
}

static uint32_t clampSystemOffDelayUs(uint32_t delayUs)
{
    const uint32_t minimumLatencyUs = systemOffMinimumLatencyUs();
    if (delayUs < minimumLatencyUs) {
        return minimumLatencyUs;
    }
    return delayUs;
}

static void configureSystemOffWakeSleep(NRF_GRTC_Type* grtc)
{
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

    grtc->TIMEOUT = (((uint32_t)kSystemOffTimeoutLfclk << GRTC_TIMEOUT_VALUE_Pos) &
                     GRTC_TIMEOUT_VALUE_Msk);
    grtc->WAKETIME =
        (((uint32_t)kSystemOffWakeLeadLfclk << GRTC_WAKETIME_VALUE_Pos) &
         GRTC_WAKETIME_VALUE_Msk);

    mode &= ~GRTC_MODE_SYSCOUNTEREN_Msk;
    mode |= (GRTC_MODE_SYSCOUNTEREN_Enabled << GRTC_MODE_SYSCOUNTEREN_Pos);
    grtc->MODE = mode;
    __asm volatile("dsb 0xF" ::: "memory");
}

static void armSystemOffWakeCompare(NRF_GRTC_Type* grtc,
                                    uint8_t wakeChannel,
                                    uint64_t wakeTimestamp)
{
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
    delayUs = clampSystemOffDelayUs(delayUs);

    nrf54l15_core_prepare_system_off_wake_timebase();
    ensureSystemOffLfxoRunning();
    configureSystemOffWakeSleep(grtc);

    const uint8_t wakeChannel = systemOffWakeChannel();
    for (uint8_t channel = 0U; channel < GRTC_CC_MaxCount; ++channel) {
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
        const uint64_t wakeTimestamp = readGrtcCounterUs(grtc) + wakeDelayUs;
        armSystemOffWakeCompare(grtc, wakeChannel, wakeTimestamp);
        waitForSystemOffWakeLatch();

        if (grtc->EVENTS_COMPARE[wakeChannel] == 0U) {
            return;
        }

        const uint64_t now = readGrtcCounterUs(grtc);
        if (wakeTimestamp > now) {
            grtc->EVENTS_COMPARE[wakeChannel] = 0U;
            return;
        }

        wakeDelayUs += minimumLatencyUs;
    }
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
#if !defined(NRF54L15_CLEAN_POWER_LOW)
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
#endif
}

unsigned long millis(void)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    initLowPowerTimebase();
    return (unsigned long)(readLowPowerCounterUs() / 1000ULL);
#else
    return (unsigned long)g_millis_ticks;
#endif
}

unsigned long micros(void)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    initLowPowerTimebase();
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

    initLowPowerTimebase();
    xiao_nrf54l15_board_state_t boardState;
    const uint8_t boardStateActive = delayShouldCollapseBoardState(ms) != 0U
                                         ? delayBoardStateEnter(&boardState)
                                         : 0U;
    const uint64_t targetUs = readLowPowerCounterUs() + ((uint64_t)ms * 1000ULL);
    delayUntilLowPowerCounterUs(targetUs);
    delayBoardStateExit(&boardState, boardStateActive);
#else
    const unsigned long start = millis();
    while ((millis() - start) < ms) {
        nrf54l15_clean_idle_service();
        __NOP();
    }
#endif
}

void delayLowPowerIdle(unsigned long ms)
{
    if (ms == 0UL) {
        nrf54l15_clean_idle_service();
        return;
    }

    xiao_nrf54l15_board_state_t boardState;
    const uint8_t boardStateActive = delayBoardStateEnter(&boardState);

#if defined(NRF54L15_CLEAN_POWER_LOW)
    initLowPowerTimebase();
    const uint64_t targetUs = readLowPowerCounterUs() + ((uint64_t)ms * 1000ULL);
    delayUntilLowPowerCounterUs(targetUs);
#else
    const unsigned long start = millis();
    while ((millis() - start) < ms) {
        nrf54l15_clean_idle_service();
        if ((__get_PRIMASK() & 1U) != 0U) {
            __NOP();
            continue;
        }

        const uint32_t restoreRaw = beginIdleSleep();
        __asm volatile("wfi");
        endIdleSleep(restoreRaw);
    }
#endif

    delayBoardStateExit(&boardState, boardStateActive);
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
    busyWaitApproxUs((uint32_t)us);
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
    if (g_low_power_timebase_initialized != 0U) {
        NRF54L15_GRTC_INTENCLR_REG(g_low_power_grtc) =
            (1UL << kLowPowerDelayChannel);
        g_low_power_grtc->CC[kLowPowerDelayChannel].CCEN =
            (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
        g_low_power_grtc->EVENTS_COMPARE[kLowPowerDelayChannel] = 0U;
        NVIC_DisableIRQ(kLowPowerTickIrq);
        NVIC->ICPR[((uint32_t)kLowPowerTickIrq) >> 5U] =
            (1UL << (((uint32_t)kLowPowerTickIrq) & 0x1FUL));
    }
#endif

    clearSystemOffVprRetention();
    xiaoNrf54l15EnterLowestPowerBoardState();
    SysTick->CTRL = 0U;
}

void nrf54l15_core_disable_system_off_retention(void)
{
    disableSystemOffRetention();
}
