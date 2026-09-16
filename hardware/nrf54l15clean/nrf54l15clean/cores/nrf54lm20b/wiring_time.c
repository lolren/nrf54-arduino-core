#include "Arduino.h"

#include <stdint.h>

#include "cmsis.h"
#include "nrf54lm20b.h"
#include "variant.h"
#include "../nrf54common/nrf54_systick_timebase.h"
#include "../nrf54common/nrf54_grtc_sleep.h"

#if !defined(ARDUINO_XIAO_NRF54L15) && !defined(ARDUINO_XIAO_NRF54L15_CLEAN) && !defined(XIAO_NRF54L15_BOARD_STATE_DECLARED)
typedef struct {
    uint8_t unused;
} xiao_nrf54l15_board_state_t;
#endif

extern uint32_t SystemCoreClock;
extern void SystemCoreClockUpdate(void);
extern void nrf54l15_clean_idle_service(void);
extern uint8_t nrf54l15_bridge_serial_active(void) __attribute__((weak));
extern uint8_t nrf54l15_ble_idle_wake_consume(void) __attribute__((weak));
extern void nrf54l15_ble_grtc_irq_service(void) __attribute__((weak));
extern void nrf54l15_grtc_pwm_irq_service(void) __attribute__((weak));
extern uint32_t nrf54l15_ble_grtc_reserved_cc_mask(void) __attribute__((weak));
extern uint32_t nrf54l15_clean_ble_idle_sleep_cap_us(void) __attribute__((weak));
extern uint8_t nrf54l15_clean_low_power_micro_delay_sleep_allowed(void)
    __attribute__((weak));
void nrf54lm20b_core_prepare_system_off_wake_timebase(void);
bool nrf54lm20b_core_prepare_system_off(void);
void nrf54lm20b_core_disable_system_off_retention(void);
uint32_t nrf54_core_reset_reason(void);
void nrf54_core_clear_reset_reason(uint32_t mask);
extern bool nrf54_hal_quiesce_for_system_off(uint32_t spinLimit)
    __attribute__((weak));

static nrf54_systick_epoch_t g_systick_epoch =
    NRF54_SYSTICK_EPOCH_INITIALIZER;
static volatile uint32_t g_system_off_abort_magic
    __attribute__((section(".noinit")));
static volatile uint32_t g_system_off_abort_magic_inverse
    __attribute__((section(".noinit")));
static volatile uint32_t g_system_off_abort_stage
    __attribute__((section(".noinit")));
static volatile uint32_t g_system_off_abort_stage_inverse
    __attribute__((section(".noinit")));
static const uint32_t kSystemOffAbortMagic = 0x534F4646UL; /* "SOFF" */

enum {
    kSystemOffAbortPrepare = 1U,
    kSystemOffAbortDmaQuiesce = 2U,
    kSystemOffAbortWakeLfxo = 3U,
    kSystemOffAbortWakeSync = 4U,
    kSystemOffAbortWakeCompare = 5U,
    kSystemOffAbortHfxoStop = 6U,
    kSystemOffAbortPreEntryCompare = 7U,
    kSystemOffAbortResetReasonClear = 8U,
    kSystemOffAbortWakeUnknown = 9U
};
static volatile uint32_t* const kScbScr = (volatile uint32_t*)0xE000ED10UL;
#if !defined(NRF54L15_CLEAN_POWER_LOW)
static volatile uint32_t* const kScbIcsr = (volatile uint32_t*)0xE000ED04UL;
#endif
static const uint32_t kScbScrSleepDeep_Msk = (1UL << 2);
static const uint32_t kScbScrSleepOnExit_Msk = (1UL << 1);
#if !defined(NRF54L15_CLEAN_POWER_LOW)
static const uint32_t kScbIcsrPendstset_Msk = (1UL << 26);
#endif
enum {
    kSystemOffWakeLeadLfclk = 255U,
    kSystemOffTimeoutLfclk = 288U,
    kSystemOffGuardLfclk = 1U
};
_Static_assert(kSystemOffTimeoutLfclk >
                   kSystemOffWakeLeadLfclk + kSystemOffGuardLfclk,
               "GRTC TIMEOUT must exceed WAKETIME plus guard");
static const uint32_t kSystemOffLfclkFrequencyHz = 32768UL;
static const uint32_t kSystemOffMinimumLatencyGuardUs = 1000UL;
static const uint32_t kGrtcStartSettleUs = 93UL;
static const uint32_t kSystemOffPeripheralSpinLimit = 2000000UL;
static const uint32_t kSystemOffSyncSpinLimit = 4000000UL;
static const uint32_t kSystemOffHfxoStopSpinLimit = 2000000UL;
static const uint32_t kSystemOffResetReasonClearSpinLimit = 200000UL;
static const uint32_t kSystemOffResetReasonStableReads = 64UL;
uint64_t nrf54lm20b_core_monotonic_time_us(void);
uint32_t nrf54lm20b_core_monotonic_time_ms(void);
#if defined(NRF54L15_CLEAN_POWER_LOW)
volatile uint32_t g_nrf54l15_diag_delay_outer_loops = 0U;
volatile uint32_t g_nrf54l15_diag_delay_wfi_entries = 0U;
volatile uint32_t g_nrf54l15_diag_delay_skipwfi_count = 0U;
volatile uint32_t g_nrf54l15_diag_delay_invalid_channel_count = 0U;
volatile uint32_t g_nrf54l15_diag_grtc_irq_count = 0U;
volatile uint32_t g_nrf54l15_diag_grtc_delay_irq_count = 0U;
volatile uint32_t g_nrf54l15_diag_ble_grtc_irq_service_count = 0U;
volatile uint32_t g_nrf54l15_diag_delay_skipwfi_total_us = 0U;
volatile uint32_t g_nrf54l15_diag_delay_skipwfi_max_us = 0U;
// Keep a small non-zero GRTC timeout. TIMEOUT=0 can miss/hold System ON
// compare wakeups on this bare-metal path and hang early setup delay() calls.
static const uint16_t kLowPowerDelayTimeoutLfclk = kNrf54GrtcSystemOnTimeoutLfclk;
static const uint8_t kLowPowerDelayWakeLfclk = kNrf54GrtcSystemOnWakeLfclk;
#if NRF54L15_GRTC_IRQ_GROUP == 2U
static const IRQn_Type kLowPowerTickIrq = GRTC_2_IRQn;
#elif NRF54L15_GRTC_IRQ_GROUP == 1U
static const IRQn_Type kLowPowerTickIrq = GRTC_1_IRQn;
#else
static const IRQn_Type kLowPowerTickIrq = GRTC_0_IRQn;
#endif
#endif
#if defined(ARDUINO_XIAO_NRF54L15) || defined(ARDUINO_XIAO_NRF54L15_CLEAN) || \
    defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B) || \
    defined(ARDUINO_XIAO_NRF54LM20A_CLEAN) || defined(ARDUINO_XIAO_NRF54LM20B_CLEAN)
#define NRF54_CLEAN_XIAO_GRTC_RESTRICTED 1
#endif

#if defined(NRF54_CLEAN_XIAO_GRTC_RESTRICTED)
static const uint32_t kZephyrAllowedCcMaskXiao = 0x67UL;
static const uint8_t kZephyrMainCcChannelXiao = 1U;
static const uint8_t kSystemOffWakePreferredCcChannel = 6U;
#endif

static void disableSystemOffRetention(void)
{
    for (uint32_t i = 0; i < MEMCONF_POWER_MaxCount; ++i) {
        NRF_MEMCONF->POWER[i].RET = 0U;
    }
}

static void clearSystemOffVprRetention(void)
{
    NRF_MEMCONF->POWER[0U].RET &= ~MEMCONF_POWER_RET_MEM15_Msk;
}

static uint32_t beginIdleSleep(void)
{
    *kScbScr &= ~(kScbScrSleepDeep_Msk | kScbScrSleepOnExit_Msk);
    return 0UL;
}

static void endIdleSleep(uint32_t restoreRaw)
{
    (void)restoreRaw;
}

static uint8_t highestSetBit(uint32_t mask)
{
    return (uint8_t)(31U - (uint32_t)__builtin_clz(mask));
}

#if defined(NRF54_CLEAN_XIAO_GRTC_RESTRICTED)
static uint8_t lowestSetBit(uint32_t mask)
{
    return (uint8_t)__builtin_ctz(mask);
}
#endif

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

static bool ensureSystemOffLfxoRunning(void)
{
    // XIAO nRF54LM20A has an LFXO on XL1=P1.20 and XL2=P1.21.
    static const uint32_t kLfxoStartSpinLimit = 240000000UL;
    if (lfclkRunningFrom(CLOCK_LFCLK_STAT_SRC_LFXO)) return true;
    startLfclkSource(CLOCK_LFCLK_SRC_SRC_LFXO);
    return waitForLfclkStarted(CLOCK_LFCLK_STAT_SRC_LFXO,
                               kLfxoStartSpinLimit);
}

static uint32_t selectRunningGrtcLfClockSource(void)
{
#if defined(ARDUINO_NRF54LM20A) || \
    defined(ARDUINO_XIAO_NRF54LM20A_CLEAN)
    // GRTC needs its direct LFXO path for lowest sleep current. CLKSEL cannot
    // change after START, so finish crystal startup before choosing this path.
    // The bounded wait happens only when first initializing a cold timebase.
    if (ensureSystemOffLfxoRunning()) {
        return GRTC_CLKCFG_CLKSEL_LFXO;
    }
    // Keep the timebase usable if the crystal fails to start. This fallback
    // does not promise crystal accuracy or the normal low-power floor.
#endif

    if (lfclkRunningFrom(CLOCK_LFCLK_STAT_SRC_LFXO)) {
        return GRTC_CLKCFG_CLKSEL_LFXO;
    }

    if (!lfclkRunningFrom(CLOCK_LFCLK_STAT_SRC_LFRC)) {
        startLfclkSource(CLOCK_LFCLK_SRC_SRC_LFRC);
        static const uint32_t kQuickLfrcLimit = 10000000UL;
        (void)waitForLfclkStarted(CLOCK_LFCLK_STAT_SRC_LFRC,
                                  kQuickLfrcLimit);
    }

    return lfclkRunningFrom(CLOCK_LFCLK_STAT_SRC_LFRC)
               ? GRTC_CLKCFG_CLKSEL_SystemLFCLK
               : GRTC_CLKCFG_CLKSEL_LFXO;
}

void nrf54lm20b_core_prepare_grtc_clock(void)
{
    // BLE and delay() can initialize GRTC in either order. Do not change the
    // clock of an existing SYSCOUNTER or its internally retained System OFF
    // timer: CLKSEL is write-only and its reset readback cannot identify that
    // retained source (PS v1.0, sections 8.11.1 and 8.11.7.49).
    if ((NRF_GRTC->MODE & GRTC_MODE_SYSCOUNTEREN_Msk) != 0U ||
        (nrf54_core_reset_reason() &
         (RESET_RESETREAS_OFF_Msk | RESET_RESETREAS_GRTC_Msk)) != 0U) {
        return;
    }
    const uint32_t source = selectRunningGrtcLfClockSource();
    uint32_t clkcfg = NRF_GRTC->CLKCFG;
    clkcfg &= ~GRTC_CLKCFG_CLKSEL_Msk;
    clkcfg |= (source << GRTC_CLKCFG_CLKSEL_Pos) & GRTC_CLKCFG_CLKSEL_Msk;
    NRF_GRTC->CLKCFG = clkcfg;
    __asm volatile("dsb 0xF" ::: "memory");
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

static uint64_t readGrtcCounterPreserveActive(NRF_GRTC_Type* grtc)
{
    const uint32_t active =
        NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE & GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Msk;
    const bool restoreActive =
        active == (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_NotActive
                   << GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
    if (restoreActive) {
        grtc->TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;
        __asm volatile("dsb 0xF" ::: "memory");
        NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE =
            (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Active <<
             GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
        __asm volatile("dsb 0xF" ::: "memory");
        while (!grtcSyscounterReady(grtc)) {
            __NOP();
        }
    }

    uint64_t value = 0ULL;
    bool valid = false;
    for (uint8_t attempt = 0U; attempt < 32U; ++attempt) {
        const uint32_t lo = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERL;
        const uint32_t hi = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERH;

        if ((hi & GRTC_SYSCOUNTER_SYSCOUNTERH_BUSY_Msk) != 0U) {
            continue;
        }

        uint32_t high =
            (hi & GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk) >>
            GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Pos;
        if ((hi & GRTC_SYSCOUNTER_SYSCOUNTERH_OVERFLOW_Msk) != 0U) {
            high = (high - 1U) &
                   (GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk >>
                    GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Pos);
        }
        value = ((uint64_t)high << 32U) | (uint64_t)lo;
        valid = true;
        break;
    }

    if (!valid) {
        const uint32_t lo = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERL;
        const uint32_t hi = NRF54L15_GRTC_SYSCOUNTER(grtc).SYSCOUNTERH;
        uint32_t high =
            (hi & GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk) >>
            GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Pos;
        if ((hi & GRTC_SYSCOUNTER_SYSCOUNTERH_OVERFLOW_Msk) != 0U) {
            high = (high - 1U) &
                   (GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk >>
                    GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Pos);
        }
        value = ((uint64_t)high << 32U) | (uint64_t)lo;
    }

    if (restoreActive) {
        NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE =
            (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_NotActive <<
             GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
        __asm volatile("dsb 0xF" ::: "memory");
    }

    return value;
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
    return readGrtcCounterPreserveActive(grtc);
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
// (channels 0,1,2,5,6). Choose a non-conflicting tickless wake source at
// runtime because BLE background scheduling owns several GRTC compare channels.
// Channel 0 enumerates as allowed but does not reliably wake CPUAPP here, so
// prefer the highest non-BLE channel left in the board-allowed mask.
enum { kLowPowerDelayInvalidChannel = 0xFFU };
static volatile uint8_t g_low_power_delay_fired = 0U;
static volatile uint8_t g_low_power_timebase_initialized = 0U;
static volatile uint8_t g_low_power_micro_delay_active = 0U;
static uint8_t g_low_power_delay_channel = kLowPowerDelayInvalidChannel;
static uint8_t g_low_power_monotonic_origin_valid = 0U;
static uint64_t g_low_power_monotonic_origin_us = 0ULL;
static const uint32_t kLowPowerMicroDelaySleepThresholdUs = 1000UL;

static uint32_t lowPowerAllCcMask(void)
{
    if (GRTC_CC_MaxCount >= 32U) {
        return 0xFFFFFFFFUL;
    }
    return (1UL << GRTC_CC_MaxCount) - 1UL;
}

static uint64_t readLowPowerCounterUs(void)
{
    return readGrtcCounterPreserveActive(g_low_power_grtc);
}

static void lowPowerDisarmDelayWake(void)
{
    if (g_low_power_delay_channel == kLowPowerDelayInvalidChannel) {
        return;
    }
    NRF54L15_GRTC_INTENCLR_REG(g_low_power_grtc) = (1UL << g_low_power_delay_channel);
    g_low_power_grtc->CC[g_low_power_delay_channel].CCEN =
        (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
    g_low_power_grtc->EVENTS_COMPARE[g_low_power_delay_channel] = 0U;
}

static void lowPowerArmDelayWake(uint64_t targetUs)
{
    if (g_low_power_delay_channel == kLowPowerDelayInvalidChannel) {
        return;
    }
    const uint32_t lo = (uint32_t)(targetUs & 0xFFFFFFFFULL);
    const uint32_t hi = (uint32_t)((targetUs >> 32U) & 0xFFFFFUL);

    g_low_power_delay_fired = 0U;
    g_low_power_grtc->EVENTS_COMPARE[g_low_power_delay_channel] = 0U;
    g_low_power_grtc->CC[g_low_power_delay_channel].CCEN =
        (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
    g_low_power_grtc->CC[g_low_power_delay_channel].CCL = lo;
    g_low_power_grtc->CC[g_low_power_delay_channel].CCH =
        (hi << GRTC_CC_CCH_CCH_Pos) & GRTC_CC_CCH_CCH_Msk;
    NVIC->ICPR[((uint32_t)kLowPowerTickIrq) >> 5U] =
        (1UL << (((uint32_t)kLowPowerTickIrq) & 0x1FUL));
    NRF54L15_GRTC_INTENSET_REG(g_low_power_grtc) = (1UL << g_low_power_delay_channel);
    g_low_power_grtc->CC[g_low_power_delay_channel].CCEN =
        (GRTC_CC_CCEN_ACTIVE_Enable << GRTC_CC_CCEN_ACTIVE_Pos);
}

static void initLowPowerTimebase(void)
{
    if (g_low_power_timebase_initialized != 0U) {
        return;
    }

    nrf54lm20b_core_prepare_grtc_clock();

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
    const uint32_t bleReservedMask =
        (nrf54l15_ble_grtc_reserved_cc_mask != 0)
            ? nrf54l15_ble_grtc_reserved_cc_mask()
            : 0U;
    uint32_t availableMask = lowPowerAllCcMask() & ~bleReservedMask;
#if defined(NRF54_CLEAN_XIAO_GRTC_RESTRICTED)
    availableMask &= kZephyrAllowedCcMaskXiao;
#endif
    g_low_power_delay_channel =
        (availableMask != 0U) ? highestSetBit(availableMask)
                              : kLowPowerDelayInvalidChannel;
    NRF54L15_GRTC_INTENCLR_REG(g_low_power_grtc) =
        0xFFFFFFFFUL & ~bleReservedMask;
    for (uint8_t channel = 0; channel < GRTC_CC_MaxCount; ++channel) {
        if ((bleReservedMask & (1UL << channel)) != 0U) {
            continue;
        }
        g_low_power_grtc->CC[channel].CCEN =
            (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
        g_low_power_grtc->EVENTS_COMPARE[channel] = 0U;
    }

    if (g_low_power_delay_channel != kLowPowerDelayInvalidChannel) {
        NVIC->ICPR[((uint32_t)kLowPowerTickIrq) >> 5U] =
            (1UL << (((uint32_t)kLowPowerTickIrq) & 0x1FUL));
        NVIC_SetPriority(kLowPowerTickIrq, 3U);
        NVIC_EnableIRQ(kLowPowerTickIrq);
    }
    g_low_power_timebase_initialized = 1U;
}

void nrf54lm20b_core_bootstrap_low_power_timebase(void)
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
        ++g_nrf54l15_diag_delay_outer_loops;
        nrf54l15_clean_idle_service();
        uint64_t sleepTargetUs = targetUs;
        uint8_t skipWfi = 0U;
        if (nrf54l15_clean_ble_idle_sleep_cap_us != 0) {
            // BLE link setup, scanning, and notify/ATT flows are pump-driven in
            // the current CPUAPP path. Sleep in short slices while BLE is
            // active so WFI idle does not starve that state machine.
            const uint32_t sleepCapUs = nrf54l15_clean_ble_idle_sleep_cap_us();
            if (sleepCapUs != 0U) {
                if (sleepCapUs == 1U) {
                    skipWfi = 1U;
                } else {
                const uint64_t cappedTargetUs =
                    readLowPowerCounterUs() + (uint64_t)sleepCapUs;
                if ((int64_t)(targetUs - cappedTargetUs) > 0) {
                    sleepTargetUs = cappedTargetUs;
                }
                }
            }
        }
        if (skipWfi != 0U) {
            const uint32_t skipStartUs = (uint32_t)readLowPowerCounterUs();
            ++g_nrf54l15_diag_delay_skipwfi_count;
            __NOP();
            const uint32_t skipElapsedUs =
                (uint32_t)(readLowPowerCounterUs() - (uint64_t)skipStartUs);
            g_nrf54l15_diag_delay_skipwfi_total_us += skipElapsedUs;
            if (skipElapsedUs > g_nrf54l15_diag_delay_skipwfi_max_us) {
                g_nrf54l15_diag_delay_skipwfi_max_us = skipElapsedUs;
            }
            continue;
        }
        if (g_low_power_delay_channel == kLowPowerDelayInvalidChannel) {
            ++g_nrf54l15_diag_delay_invalid_channel_count;
            __NOP();
            continue;
        }
        lowPowerArmDelayWake(sleepTargetUs);
        const uint32_t restoreRaw = beginIdleSleep();
        while ((g_low_power_delay_fired == 0U) &&
               ((int64_t)(sleepTargetUs - readLowPowerCounterUs()) > 0)) {
            ++g_nrf54l15_diag_delay_wfi_entries;
            __asm volatile("wfi");
            // BLE foreground wake (advertising) fires on a different
            // GRTC channel; break out of the inner WFI loop so
            // idle_service() can run maybeAdvertise() again.
            if (nrf54l15_ble_idle_wake_consume != 0 &&
                nrf54l15_ble_idle_wake_consume() != 0U) {
                break;
            }
        }
        endIdleSleep(restoreRaw);
        lowPowerDisarmDelayWake();
    }
}
#endif

#if NRF54L15_GRTC_IRQ_GROUP == 2U
void GRTC_2_IRQHandler(void)
#elif NRF54L15_GRTC_IRQ_GROUP == 1U
void GRTC_1_IRQHandler(void)
#else
void GRTC_0_IRQHandler(void)
#endif
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    ++g_nrf54l15_diag_grtc_irq_count;
    if (g_low_power_delay_channel != kLowPowerDelayInvalidChannel &&
        g_low_power_grtc->EVENTS_COMPARE[g_low_power_delay_channel] != 0U) {
        ++g_nrf54l15_diag_grtc_delay_irq_count;
        g_low_power_grtc->EVENTS_COMPARE[g_low_power_delay_channel] = 0U;
        g_low_power_grtc->CC[g_low_power_delay_channel].CCEN =
            (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
        NRF54L15_GRTC_INTENCLR_REG(g_low_power_grtc) =
            (1UL << g_low_power_delay_channel);
        g_low_power_delay_fired = 1U;
    }
#endif
    if (nrf54l15_ble_grtc_irq_service != 0) {
#if defined(NRF54L15_CLEAN_POWER_LOW)
        ++g_nrf54l15_diag_ble_grtc_irq_service_count;
#endif
        nrf54l15_ble_grtc_irq_service();
    }
    if (nrf54l15_grtc_pwm_irq_service != 0) {
        nrf54l15_grtc_pwm_irq_service();
    }
}

static uint8_t delayBoardStateEnter(xiao_nrf54l15_board_state_t* state)
{
#if defined(ARDUINO_XIAO_NRF54L15) || defined(ARDUINO_XIAO_NRF54L15_CLEAN)
    if (state == 0 || xiaoNrf54l15SaveBoardState(state) == 0U) {
        return 0U;
    }

    xiaoNrf54l15EnterLowestPowerBoardState();
    // Battery/IMU reads can use a short delay() as a settle window. Keep
    // those user-held rails alive while still collapsing the rest of the XIAO
    // board state.
    if (state->batteryEnable.isOutput != 0U &&
        state->batteryEnable.outputHigh != 0U) {
        (void)arduinoXiaoNrf54l15SetBatteryEnable(1U);
    }
    if (state->imuMicEnable.isOutput != 0U &&
        state->imuMicEnable.outputHigh != 0U) {
        (void)arduinoXiaoNrf54l15SetImuMicEnable(1U);
    }
    return 1U;
#else
    (void)state;
    return 0U;
#endif
}

static void delayBoardStateExit(const xiao_nrf54l15_board_state_t* state, uint8_t active)
{
#if defined(ARDUINO_XIAO_NRF54L15) || defined(ARDUINO_XIAO_NRF54L15_CLEAN)
    if (active != 0U) {
        (void)xiaoNrf54l15RestoreBoardState(state);
    }
#else
    (void)state;
    (void)active;
#endif
}

static uint8_t delayAutoBoardStateEnabled(void)
{
#if defined(ARDUINO_XIAO_NRF54L15) || defined(ARDUINO_XIAO_NRF54L15_CLEAN)
    return (nrf54l15_bridge_serial_active == 0 ||
            nrf54l15_bridge_serial_active() == 0U) ? 1U : 0U;
#else
    return 0U;
#endif
}

static uint8_t systemOffWakeChannel(void)
{
#if defined(NRF54_CLEAN_XIAO_GRTC_RESTRICTED)
    const uint32_t bleReservedMask =
        (nrf54l15_ble_grtc_reserved_cc_mask != 0)
            ? nrf54l15_ble_grtc_reserved_cc_mask()
            : 0U;
    uint32_t available = kZephyrAllowedCcMaskXiao & ~bleReservedMask &
                         ~(1UL << kZephyrMainCcChannelXiao);
    if (available == 0U) {
        available = kZephyrAllowedCcMaskXiao & ~bleReservedMask;
    }
    if ((available & (1UL << kSystemOffWakePreferredCcChannel)) != 0U) {
        return kSystemOffWakePreferredCcChannel;
    }
    return available != 0U ? lowestSetBit(available)
                           : kZephyrMainCcChannelXiao;
#else
    return 1U;
#endif
}

void nrf54lm20b_core_prepare_system_off_wake_timebase(void)
{
    NRF_GRTC_Type* const grtc = NRF_GRTC;
    NRF54L15_GRTC_INTENCLR_REG(grtc) = 0xFFFFFFFFUL;
    grtc->EVTENCLR = 0xFFFFFFFFUL;
    for (uint8_t channel = 0U; channel < GRTC_CC_MaxCount; ++channel) {
        grtc->CC[channel].CCEN =
            (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
        grtc->EVENTS_COMPARE[channel] = 0U;
    }

    for (uint8_t index = 0U; index < GRTC_SYSCOUNTER_MaxCount; ++index) {
        grtc->SYSCOUNTER[index].ACTIVE =
            (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_NotActive <<
             GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
    }
    __asm volatile("dsb 0xF" ::: "memory");
    grtc->TASKS_STOP = GRTC_TASKS_STOP_TASKS_STOP_Trigger;
    __asm volatile("dsb 0xF" ::: "memory");
    busyWaitApproxUs(kGrtcStartSettleUs);

#if defined(NRF54L15_CLEAN_POWER_LOW)
    if (g_low_power_timebase_initialized != 0U) {
        NVIC_DisableIRQ(kLowPowerTickIrq);
        NVIC->ICPR[((uint32_t)kLowPowerTickIrq) >> 5U] =
            (1UL << (((uint32_t)kLowPowerTickIrq) & 0x1FUL));
        g_low_power_delay_fired = 0U;
        g_low_power_timebase_initialized = 0U;
    }
#endif
}

void nrf54_core_prepare_system_off_wake_timebase(void)
{
    nrf54lm20b_core_prepare_system_off_wake_timebase();
}

static uint32_t systemOffMinimumLatencyUs(void)
{
    return ((((uint32_t)kSystemOffTimeoutLfclk +
              (uint32_t)kSystemOffWakeLeadLfclk) *
             1000000UL) /
            kSystemOffLfclkFrequencyHz) +
           kSystemOffMinimumLatencyGuardUs;
}

static uint64_t clampSystemOffDelayUs(uint64_t delayUs)
{
    const uint64_t minimumLatencyUs = systemOffMinimumLatencyUs();
    if (delayUs < minimumLatencyUs) {
        return minimumLatencyUs;
    }
    return delayUs;
}

static void configureSystemOffWakeSleep(NRF_GRTC_Type* grtc,
                                        uint32_t grtcClockSel)
{
    uint32_t mode = grtc->MODE;
    mode &= ~(GRTC_MODE_AUTOEN_Msk | GRTC_MODE_SYSCOUNTEREN_Msk);
    mode |= (GRTC_MODE_AUTOEN_CpuActive << GRTC_MODE_AUTOEN_Pos);
    mode |= (GRTC_MODE_SYSCOUNTEREN_Disabled << GRTC_MODE_SYSCOUNTEREN_Pos);
    grtc->MODE = mode;
    __asm volatile("dsb 0xF" ::: "memory");

    uint32_t clkcfg = grtc->CLKCFG;
    clkcfg &= ~GRTC_CLKCFG_CLKSEL_Msk;
    clkcfg |= (grtcClockSel << GRTC_CLKCFG_CLKSEL_Pos) &
              GRTC_CLKCFG_CLKSEL_Msk;
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
                                    uint64_t wakeTimestampUs)
{
    grtc->EVENTS_RTCOMPARESYNC = 0U;
    grtc->EVENTS_COMPARE[wakeChannel] = 0U;
    grtc->CC[wakeChannel].CCEN =
        (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
    grtc->CC[wakeChannel].CCL =
        (uint32_t)(wakeTimestampUs & 0xFFFFFFFFULL);
    grtc->CC[wakeChannel].CCH =
        ((uint32_t)((wakeTimestampUs >> 32U) & 0xFFFFFULL) <<
         GRTC_CC_CCH_CCH_Pos) &
        GRTC_CC_CCH_CCH_Msk;
    grtc->CC[wakeChannel].CCEN =
        (GRTC_CC_CCEN_ACTIVE_Enable << GRTC_CC_CCEN_ACTIVE_Pos);
    NRF54L15_GRTC_INTENSET_REG(grtc) = (1UL << wakeChannel);
    grtc->EVTENSET = (1UL << wakeChannel);
    __asm volatile("dsb 0xF" ::: "memory");
}

typedef enum {
    kSystemOffCompareSynchronized,
    kSystemOffCompareAlreadyFired,
    kSystemOffCompareSyncTimeout
} system_off_compare_sync_t;

static bool anyGrtcCompareEvent(const NRF_GRTC_Type* grtc)
{
    for (uint8_t channel = 0U; channel < GRTC_CC_MaxCount; ++channel) {
        if (grtc->EVENTS_COMPARE[channel] != 0U) {
            return true;
        }
    }
    return false;
}

static system_off_compare_sync_t waitForSystemOffWakeSynchronization(
    NRF_GRTC_Type* grtc)
{
    uint32_t spinLimit = kSystemOffSyncSpinLimit;
    while (spinLimit-- > 0U) {
        if (anyGrtcCompareEvent(grtc)) {
            return kSystemOffCompareAlreadyFired;
        }
        if (grtc->EVENTS_RTCOMPARESYNC != 0U) {
            grtc->EVENTS_RTCOMPARESYNC = 0U;
            return kSystemOffCompareSynchronized;
        }
    }
    return kSystemOffCompareSyncTimeout;
}

typedef enum {
    kSystemOffWakeProgrammed = 0U,
    kSystemOffWakeLfxoFailed = 1U,
    kSystemOffWakeSyncTimedOut = 2U,
    kSystemOffWakeCompareFired = 3U
} system_off_wake_program_status_t;

static system_off_wake_program_status_t programSystemOffWakeUs(uint64_t delayUs)
{
    NRF_GRTC_Type* const grtc = NRF_GRTC;
    delayUs = clampSystemOffDelayUs(delayUs);

    nrf54lm20b_core_prepare_system_off_wake_timebase();
    if (!ensureSystemOffLfxoRunning()) {
        return kSystemOffWakeLfxoFailed;
    }
    configureSystemOffWakeSleep(grtc, GRTC_CLKCFG_CLKSEL_LFXO);

    const uint8_t wakeChannel = systemOffWakeChannel();
    for (uint8_t channel = 0U; channel < GRTC_CC_MaxCount; ++channel) {
        NRF54L15_GRTC_INTENCLR_REG(grtc) = (1UL << channel);
        grtc->EVTENCLR = (1UL << channel);
        grtc->EVENTS_COMPARE[channel] = 0U;
        if (channel != wakeChannel) {
            grtc->CC[channel].CCEN =
                (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
        }
    }

    const uint32_t minimumLatencyUs = systemOffMinimumLatencyUs();
    uint64_t wakeDelayUs = delayUs;

    for (uint8_t attempt = 0U; attempt < 2U; ++attempt) {
        ensureGrtcReady(grtc);
        NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE =
            (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Active <<
             GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
        __asm volatile("dsb 0xF" ::: "memory");
        while (!grtcSyscounterReady(grtc)) {
            __NOP();
        }

        const uint64_t wakeTimestampUs =
            readGrtcCounterUs(grtc) + (uint64_t)wakeDelayUs;
        armSystemOffWakeCompare(grtc, wakeChannel, wakeTimestampUs);
        NRF54L15_GRTC_SYSCOUNTER(grtc).ACTIVE =
            (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_NotActive <<
             GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
        uint32_t mode = grtc->MODE;
        mode &= ~GRTC_MODE_AUTOEN_Msk;
        mode |= (GRTC_MODE_AUTOEN_Default << GRTC_MODE_AUTOEN_Pos);
        grtc->MODE = mode;
        __asm volatile("dsb 0xF" ::: "memory");

        const system_off_compare_sync_t status =
            waitForSystemOffWakeSynchronization(grtc);
        if (status == kSystemOffCompareSynchronized &&
            grtc->EVENTS_COMPARE[wakeChannel] == 0U) {
            return kSystemOffWakeProgrammed;
        }
        if (status == kSystemOffCompareSyncTimeout) {
            return kSystemOffWakeSyncTimedOut;
        }

        grtc->CC[wakeChannel].CCEN =
            (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
        grtc->EVENTS_COMPARE[wakeChannel] = 0U;
        if (wakeDelayUs > UINT64_MAX - minimumLatencyUs) {
            return kSystemOffWakeCompareFired;
        }
        wakeDelayUs += minimumLatencyUs;
    }
    return kSystemOffWakeCompareFired;
}

static void clearSystemOffAbortDiagnostic(void)
{
    g_system_off_abort_magic = 0U;
    g_system_off_abort_magic_inverse = 0U;
    g_system_off_abort_stage = 0U;
    g_system_off_abort_stage_inverse = 0U;
    __asm volatile("dsb 0xF" ::: "memory");
}

static uint32_t systemOffAbortStageForWakeStatus(
    system_off_wake_program_status_t status)
{
    switch (status) {
        case kSystemOffWakeLfxoFailed:
            return kSystemOffAbortWakeLfxo;
        case kSystemOffWakeSyncTimedOut:
            return kSystemOffAbortWakeSync;
        case kSystemOffWakeCompareFired:
            return kSystemOffAbortWakeCompare;
        default:
            return kSystemOffAbortWakeUnknown;
    }
}

static void resetAfterSystemOffEvent(void) __attribute__((noreturn));

static void resetAfterSystemOffEvent(void)
{
    *(volatile uint32_t*)0xE000ED0CUL = 0x05FA0004UL;
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
    while (1) {
        __asm volatile("wfe");
    }
}

static void abortSystemOffWithReset(uint32_t stage)
    __attribute__((noreturn));

static void abortSystemOffWithReset(uint32_t stage)
{
    g_system_off_abort_stage = stage;
    g_system_off_abort_stage_inverse = ~stage;
    g_system_off_abort_magic_inverse = ~kSystemOffAbortMagic;
    g_system_off_abort_magic = kSystemOffAbortMagic;
    __asm volatile("dsb 0xF" ::: "memory");
    resetAfterSystemOffEvent();
}

static bool runSystemOffQuiesceHook(bool (*hook)(uint32_t))
{
    return hook == 0 || hook(kSystemOffPeripheralSpinLimit);
}

static bool quiesceSystemOffDmaOwners(void)
{
    return runSystemOffQuiesceHook(
               nrf54_core_quiesce_serial_for_system_off) &&
           runSystemOffQuiesceHook(nrf54_core_quiesce_spi_for_system_off) &&
           runSystemOffQuiesceHook(nrf54_core_quiesce_wire_for_system_off) &&
           runSystemOffQuiesceHook(nrf54_core_quiesce_analog_for_system_off) &&
           runSystemOffQuiesceHook(nrf54_hal_quiesce_for_system_off);
}

static bool stopHfxoForSystemOff(void)
{
    const uint32_t state =
        (NRF_CLOCK->XO.STAT & CLOCK_XO_STAT_STATE_Msk) >>
        CLOCK_XO_STAT_STATE_Pos;
    if (state == CLOCK_XO_STAT_STATE_NotRunning) {
        NRF_CLOCK->TASKS_PLLSTOP =
            CLOCK_TASKS_PLLSTOP_TASKS_PLLSTOP_Trigger;
        __asm volatile("dsb 0xF" ::: "memory");
        return true;
    }

    NRF_CLOCK->TASKS_XOSTOP = CLOCK_TASKS_XOSTOP_TASKS_XOSTOP_Trigger;
    __asm volatile("dsb 0xF" ::: "memory");
    NRF_CLOCK->TASKS_PLLSTOP = CLOCK_TASKS_PLLSTOP_TASKS_PLLSTOP_Trigger;
    __asm volatile("dsb 0xF" ::: "memory");
    uint32_t spinLimit = kSystemOffHfxoStopSpinLimit;
    while (spinLimit-- > 0U) {
        if (((NRF_CLOCK->XO.STAT & CLOCK_XO_STAT_STATE_Msk) >>
             CLOCK_XO_STAT_STATE_Pos) == CLOCK_XO_STAT_STATE_NotRunning) {
            return true;
        }
    }
    return false;
}

static bool clearResetReasonsForSystemOff(void)
{
    nrf54_core_clear_reset_reason(0xFFFFFFFFUL);
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");

    uint32_t spinLimit = kSystemOffResetReasonClearSpinLimit;
    uint32_t consecutiveZeroReads = 0U;
    while (spinLimit-- > 0U) {
        const uint32_t remaining = NRF_RESET->RESETREAS;
        if (remaining == 0U) {
            ++consecutiveZeroReads;
            if (consecutiveZeroReads >= kSystemOffResetReasonStableReads) {
                __asm volatile("dsb 0xF" ::: "memory");
                __asm volatile("isb 0xF" ::: "memory");
                return NRF_RESET->RESETREAS == 0U;
            }
            __NOP();
            continue;
        }
        consecutiveZeroReads = 0U;
        /* RESETREAS is W1C and may take several peripheral clock cycles to
         * settle. Re-acknowledge only the causes that remain asserted. */
        NRF_RESET->RESETREAS = remaining;
        __asm volatile("dsb 0xF" ::: "memory");
    }
    __asm volatile("isb 0xF" ::: "memory");
    return false;
}

static void enterSystemOffInternal(bool disableRamRetention,
                                   bool timedWake,
                                   uint64_t delayUs) __attribute__((noreturn));

static void enterSystemOffInternal(bool disableRamRetention,
                                   bool timedWake,
    uint64_t delayUs)
{
    clearSystemOffAbortDiagnostic();
    __asm volatile("cpsid i" ::: "memory");

    if (!nrf54lm20b_core_prepare_system_off()) {
        abortSystemOffWithReset(kSystemOffAbortPrepare);
    }
    if (!quiesceSystemOffDmaOwners()) {
        abortSystemOffWithReset(kSystemOffAbortDmaQuiesce);
    }

    if (timedWake) {
        const system_off_wake_program_status_t wakeStatus =
            programSystemOffWakeUs(delayUs);
        if (wakeStatus != kSystemOffWakeProgrammed) {
            abortSystemOffWithReset(
                systemOffAbortStageForWakeStatus(wakeStatus));
        }
    } else {
        nrf54lm20b_core_prepare_system_off_wake_timebase();
    }

    if (disableRamRetention) {
        nrf54lm20b_core_disable_system_off_retention();
    }
    if (!stopHfxoForSystemOff()) {
        abortSystemOffWithReset(kSystemOffAbortHfxoStop);
    }
    if (timedWake && anyGrtcCompareEvent(NRF_GRTC)) {
        abortSystemOffWithReset(kSystemOffAbortPreEntryCompare);
    }
    NRF_POWER->TASKS_LOWPWR = POWER_TASKS_LOWPWR_TASKS_LOWPWR_Trigger;

    *kScbScr = (*kScbScr | kScbScrSleepDeep_Msk) & ~kScbScrSleepOnExit_Msk;
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
    if (!clearResetReasonsForSystemOff()) {
        abortSystemOffWithReset(kSystemOffAbortResetReasonClear);
    }
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
    NRF_REGULATORS->SYSTEMOFF = REGULATORS_SYSTEMOFF_SYSTEMOFF_Enter;
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");

    while (1) {
        __asm volatile("wfi");
        if (timedWake && anyGrtcCompareEvent(NRF_GRTC)) {
            // A connected debugger can keep the CPU in emulated System OFF.
            // The armed GRTC compare then wakes this fallback path; reset
            // without marking it as a pre-entry abort.
            resetAfterSystemOffEvent();
        }
    }
}

void nrf54_core_system_off(uint8_t disableRamRetention)
    __attribute__((noreturn));
void nrf54_core_system_off(uint8_t disableRamRetention)
{
    enterSystemOffInternal(disableRamRetention != 0U, false, 0U);
}

void nrf54_core_system_off_timed_us(uint32_t delayUs,
                                    uint8_t disableRamRetention)
    __attribute__((noreturn));
void nrf54_core_system_off_timed_us(uint32_t delayUs,
                                    uint8_t disableRamRetention)
{
    enterSystemOffInternal(disableRamRetention != 0U, true, delayUs);
}

static void enterTimedSystemOff(bool disableRamRetention, uint64_t delayUs)
    __attribute__((noreturn));

static void enterTimedSystemOff(bool disableRamRetention, uint64_t delayUs)
{
    enterSystemOffInternal(disableRamRetention, true, delayUs);
}

static uint64_t systemOffDelayMsToUs(unsigned long ms)
{
    return (uint64_t)ms * 1000ULL;
}

static void enterSystemOffWakeReset(uint64_t delayUs) __attribute__((noreturn));

static void enterSystemOffWakeReset(uint64_t delayUs)
{
    enterSystemOffInternal(true, true, delayUs);
}

void __attribute__((weak)) SysTick_Handler(void)
{
    nrf54_systick_publish_tick(&g_systick_epoch);
}

#if !defined(NRF54L15_CLEAN_POWER_LOW)
static uint64_t readSysTickMilliseconds64(void)
{
    return nrf54_systick_read_epoch(&g_systick_epoch);
}

static uint64_t readSysTickMicroseconds64(void)
{
    // Clear an old COUNTFLAG before taking the sample. PENDSTSET is not
    // cleared by this read, so an unserviced tick remains observable.
    const uint32_t initialControl = SysTick->CTRL;
    if ((initialControl & SysTick_CTRL_ENABLE_Msk) == 0U) {
        return readSysTickMilliseconds64() * 1000ULL;
    }

    uint64_t millisecondsBefore;
    uint64_t millisecondsAfter;
    uint32_t currentValue;
    uint32_t pendingState;
    uint32_t controlAfter;
    do {
        millisecondsBefore = readSysTickMilliseconds64();
        currentValue = SysTick->VAL;
        pendingState = *kScbIcsr;
        controlAfter = SysTick->CTRL;
        millisecondsAfter = readSysTickMilliseconds64();
    } while (!nrf54_systick_sample_is_stable(
        millisecondsBefore, millisecondsAfter,
        (controlAfter & SysTick_CTRL_COUNTFLAG_Msk) != 0U));

    uint32_t cyclesPerUs =
        (SystemCoreClock == 0UL) ? 64UL : (SystemCoreClock / 1000000UL);
    if (cyclesPerUs == 0UL) {
        cyclesPerUs = 64UL;
    }
    return nrf54_systick_compose_time_us(
        millisecondsBefore, currentValue, SysTick->LOAD, cyclesPerUs,
        (pendingState & kScbIcsrPendstset_Msk) != 0U);
}
#endif

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
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
}

unsigned long millis(void)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    return (unsigned long)(nrf54lm20b_core_monotonic_time_us() / 1000ULL);
#else
    return (unsigned long)(readSysTickMicroseconds64() / 1000ULL);
#endif
}

unsigned long micros(void)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    return (unsigned long)nrf54lm20b_core_monotonic_time_us();
#else
    return (unsigned long)readSysTickMicroseconds64();
#endif
}

uint64_t nrf54lm20b_core_monotonic_time_us(void)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    initLowPowerTimebase();
    const uint64_t nowUs = readLowPowerCounterUs();
    if (g_low_power_monotonic_origin_valid == 0U) {
        g_low_power_monotonic_origin_us = nowUs;
        g_low_power_monotonic_origin_valid = 1U;
        return 0ULL;
    }
    return nowUs - g_low_power_monotonic_origin_us;
#else
    return readSysTickMicroseconds64();
#endif
}

// Compat wrappers: HAL/Bluefruit libraries reference nrf54l15_core_monotonic_*
uint64_t nrf54l15_core_monotonic_time_us(void) {
    return nrf54lm20b_core_monotonic_time_us();
}
uint32_t nrf54l15_core_monotonic_time_ms(void) {
    return nrf54lm20b_core_monotonic_time_ms();
}

uint32_t nrf54lm20b_core_monotonic_time_ms(void)
{
    return (uint32_t)(nrf54lm20b_core_monotonic_time_us() / 1000ULL);
}

void delay(unsigned long ms)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    if (ms == 0UL) {
        nrf54l15_clean_idle_service();
        return;
    }

    xiao_nrf54l15_board_state_t boardState;
    const uint8_t boardStateActive =
        delayAutoBoardStateEnabled() ? delayBoardStateEnter(&boardState) : 0U;
    initLowPowerTimebase();
    // Keep plain delay() Arduino-compatible for user-held settle rails such as
    // VBAT_EN and IMU_MIC_EN while still collapsing XIAO RF_SW for low idle
    // current. BLE paths that need RF_SW during radio events should use the
    // BoardControl/BleRadio helpers rather than holding it through delay().
    // On XIAO, auto-collapse only when the USB bridge UART is idle. That keeps
    // plain delay() low-current for idle sketches while avoiding garbling
    // active bridge-backed Serial sessions.
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
    enterTimedSystemOff(false, systemOffDelayMsToUs(ms));
}

void delaySystemOffNoRetention(unsigned long ms)
{
    enterTimedSystemOff(true, systemOffDelayMsToUs(ms));
}

void systemOffWakeReset(unsigned long ms)
{
    enterSystemOffWakeReset(systemOffDelayMsToUs(ms));
}

bool wasSystemOffWakeReset(void)
{
    return (nrf54_core_reset_reason() &
            (RESET_RESETREAS_OFF_Msk | RESET_RESETREAS_GRTC_Msk)) != 0U;
}

bool wasSystemOffWakeFromGrtc(void)
{
    return (nrf54_core_reset_reason() & RESET_RESETREAS_GRTC_Msk) != 0U;
}

void clearSystemOffWakeResetReason(void)
{
    nrf54_core_clear_reset_reason(RESET_RESETREAS_OFF_Msk |
                                  RESET_RESETREAS_GRTC_Msk);
}

uint32_t nrf54SystemOffAbortStage(void)
{
    const uint32_t stage = g_system_off_abort_stage;
    if (g_system_off_abort_magic != kSystemOffAbortMagic ||
        g_system_off_abort_magic_inverse != ~kSystemOffAbortMagic ||
        stage == 0U || g_system_off_abort_stage_inverse != ~stage) {
        return 0U;
    }
    return stage;
}

void nrf54ClearSystemOffAbortStage(void)
{
    clearSystemOffAbortDiagnostic();
}

uint32_t nrf54ResetReason(void)
{
    return nrf54_core_reset_reason();
}

void nrf54ClearResetReason(uint32_t mask)
{
    nrf54_core_clear_reset_reason(mask);
}

void delayMicroseconds(unsigned int us)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    // Keep short timing waits busy/precise. Connected BLE paths can opt long
    // waits into tickless idle so 1 ms marker/settle delays do not burn CPU.
    if ((uint32_t)us >= kLowPowerMicroDelaySleepThresholdUs &&
        g_low_power_micro_delay_active == 0U &&
        nrf54l15_clean_low_power_micro_delay_sleep_allowed != 0 &&
        nrf54l15_clean_low_power_micro_delay_sleep_allowed() != 0U &&
        (__get_PRIMASK() & 1U) == 0U &&
        __get_IPSR() == 0U) {
        xiao_nrf54l15_board_state_t boardState;
        g_low_power_micro_delay_active = 1U;
        const uint8_t boardStateActive =
            delayAutoBoardStateEnabled() ? delayBoardStateEnter(&boardState) : 0U;
        initLowPowerTimebase();
        const uint64_t targetUs =
            readLowPowerCounterUs() + (uint64_t)((uint32_t)us);
        delayUntilLowPowerCounterUs(targetUs);
        delayBoardStateExit(&boardState, boardStateActive);
        g_low_power_micro_delay_active = 0U;
        return;
    }
#endif

    const unsigned long start = micros();
    while ((micros() - start) < (unsigned long)us) {
        __NOP();
    }
}

bool nrf54lm20b_core_prepare_system_off(void)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    if (g_low_power_timebase_initialized != 0U) {
        if (g_low_power_delay_channel != kLowPowerDelayInvalidChannel) {
            NRF54L15_GRTC_INTENCLR_REG(g_low_power_grtc) =
                (1UL << g_low_power_delay_channel);
            g_low_power_grtc->CC[g_low_power_delay_channel].CCEN =
                (GRTC_CC_CCEN_ACTIVE_Disable << GRTC_CC_CCEN_ACTIVE_Pos);
            g_low_power_grtc->EVENTS_COMPARE[g_low_power_delay_channel] = 0U;
        }
        NVIC_DisableIRQ(kLowPowerTickIrq);
        NVIC->ICPR[((uint32_t)kLowPowerTickIrq) >> 5U] =
            (1UL << (((uint32_t)kLowPowerTickIrq) & 0x1FUL));
    }
#endif

    clearSystemOffVprRetention();
#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B) || \
    defined(ARDUINO_XIAO_NRF54LM20A_CLEAN) || \
    defined(ARDUINO_XIAO_NRF54LM20B_CLEAN)
    (void)xiaoNrf54lm20PmicPrepareForSleep();
    if (xiaoNrf54lm20QspiFlashPrepareForSleep() == 0) {
        return false;
    }
#elif defined(ARDUINO_XIAO_NRF54L15) || defined(ARDUINO_XIAO_NRF54L15_CLEAN)
    xiaoNrf54l15EnterLowestPowerBoardState();
#endif
    SysTick->CTRL = 0U;
    return true;
}

void nrf54lm20b_core_disable_system_off_retention(void)
{
    disableSystemOffRetention();
}
