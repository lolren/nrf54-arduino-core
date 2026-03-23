/*
 * System startup for the nRF54L15 clean Arduino core.
 *
 * For secure builds, this mirrors the Zephyr/nrfx startup writes that were
 * required to reach the same low-power SYSTEM OFF behavior on XIAO nRF54L15.
 */

#include <stdbool.h>
#include <stdint.h>

#include "cmsis.h"
#include "nrf54l15.h"

uint32_t SystemCoreClock = 64000000UL;
static uint32_t g_idleCpuTargetRaw = OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK64M;
static bool g_idleCpuScalingEnabled = false;

#if !defined(NRF_TRUSTZONE_NONSECURE)
static const NRF_FICR_Type *const kFicr =
    (const NRF_FICR_Type *)0x00FFC000UL;
static const uintptr_t kErrata37Reg = 0x5005340CUL;
static const uintptr_t kDeviceConfigReg = 0x50120440UL;
static const uintptr_t kErrata32CheckReg = 0x00FFC334UL;
static const uintptr_t kErrata32Reg = 0x50120640UL;
static const uintptr_t kErrata31Reg0 = 0x50120624UL;
static const uintptr_t kErrata31Reg1 = 0x5012063CUL;
static const uintptr_t kErrata40Reg = 0x5008A7ACUL;
static const uintptr_t kRramcLowPowerConfigReg = 0x5004B518UL;
static const uintptr_t kGlitchDetConfigReg = 0x5004B5A0UL;
static const uintptr_t kCacheEnableReg = 0xE0082404UL;
/* TAMPC (Tamper Controller) base: 0x500DC000 (secure).
 * PROTECT is at +0x500, DOMAIN[0] at +0x000, DBGEN.CTRL at +0x000, NIDEN.CTRL at +0x008.
 * ResetValue of each CTRL = 0x00000010 (VALUE=0=debug disabled, WRITEPROTECTION=enabled).
 * Write KEY=0x50FA in [31:16], WRITEPROTECTION_Clear=0xF in [7:4], VALUE_High=1 in [0]
 * to unlock and enable in one atomic write. */
static const uintptr_t kTampcDbgenCtrlReg = 0x500DC500UL;
static const uintptr_t kTampcNidenCtrlReg = 0x500DC508UL;
static const uint32_t kTampcDebugEnableVal =
    (0x50FAUL << 16U) | (0xFUL << 4U) | 0x1UL; /* KEY | WRITEPROTECTION_Clear | VALUE=1 */
#endif

static inline volatile uint32_t *reg32(uintptr_t address)
{
    return (volatile uint32_t *)address;
}

static uint32_t currentPllFrequencyRaw(void)
{
    return (NRF_OSCILLATORS->PLL.CURRENTFREQ &
            OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Msk) >>
           OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Pos;
}

static uint32_t cpuFrequencyHzFromRaw(uint32_t raw)
{
    if (raw == OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK128M) {
        return 128000000UL;
    }
    if (raw == OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK64M) {
        return 64000000UL;
    }
    return 0UL;
}

static uint32_t cpuFrequencyRawFromHz(uint32_t hz)
{
    if (hz >= 128000000UL) {
        return OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK128M;
    }
    if (hz >= 64000000UL) {
        return OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK64M;
    }
    return 0UL;
}

static void setPllFrequency(uint32_t targetFrequency)
{
    NRF_OSCILLATORS->PLL.FREQ = targetFrequency;

    uint32_t guard = 0U;
    while ((((NRF_OSCILLATORS->PLL.CURRENTFREQ &
              OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Msk) >>
             OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Pos) != targetFrequency) &&
           (guard++ < 1000000UL)) {
        __NOP();
    }
}

void SystemCoreClockUpdate(void)
{
    const uint32_t hz = cpuFrequencyHzFromRaw(currentPllFrequencyRaw());
    SystemCoreClock = (hz == 0UL) ? 64000000UL : hz;
}

#if !defined(NRF_TRUSTZONE_NONSECURE)
static bool zephyrErrata31(void)
{
    return (kFicr->INFO.PART == 0x1CU) && (kFicr->INFO.VARIANT == 0x01U);
}

static bool zephyrErrata32(void)
{
    return (kFicr->INFO.PART == 0x1CU) && (kFicr->INFO.VARIANT == 0x01U);
}

static bool zephyrErrata37(void)
{
    return kFicr->INFO.PART == 0x1CU;
}

static bool zephyrErrata40(void)
{
    return (kFicr->INFO.PART == 0x1CU) && (kFicr->INFO.VARIANT == 0x01U);
}

static void zephyrCopyTrimConfig(void)
{
    for (uint32_t index = 0U; index < FICR_TRIMCNF_MaxCount; ++index) {
        const uint32_t address = kFicr->TRIMCNF[index].ADDR;
        if ((address == 0xFFFFFFFFUL) || (address == 0x00000000UL)) {
            break;
        }

        *reg32(address) = kFicr->TRIMCNF[index].DATA;
    }
}

static void zephyrApplySystemInitParity(void)
{
    if (zephyrErrata37()) {
        *reg32(kErrata37Reg) = 1U;
    }

    zephyrCopyTrimConfig();

    if (*reg32(kDeviceConfigReg) == 0U) {
        *reg32(kDeviceConfigReg) = 0xC8U;
    }

    if (zephyrErrata32() && (*reg32(kErrata32CheckReg) <= 0x180A1D00UL)) {
        *reg32(kErrata32Reg) = 0x1EA9E040UL;
    }

    if (zephyrErrata40()) {
        *reg32(kErrata40Reg) = 0x040A0078UL;
    }

    if (zephyrErrata31()) {
        *reg32(kErrata31Reg0) = 20U | (1U << 5);
        *reg32(kErrata31Reg1) &= ~(1UL << 19);
    }

    if ((NRF_RESET->RESETREAS & RESET_RESETREAS_RESETPIN_Msk) != 0U) {
        NRF_RESET->RESETREAS = ~RESET_RESETREAS_RESETPIN_Msk;
    }

    *reg32(kRramcLowPowerConfigReg) = 3U;
    *reg32(kGlitchDetConfigReg) = 0U;

    /* Enable invasive (DBGEN) and non-invasive (NIDEN) debug for the Application domain.
     * UICR.APPROTECT = 0xFFFFFFFF (erased) leaves the TAMPC signal unchanged, so DBGEN
     * stays at its reset-default of 0 (disabled) unless we explicitly set it here.
     * Without this, after a power cycle CSW.DEVICEEN = 0 and pyOCD reports APPROTECT. */
    *reg32(kTampcDbgenCtrlReg) = kTampcDebugEnableVal;
    *reg32(kTampcNidenCtrlReg) = kTampcDebugEnableVal;
}

static void zephyrApplyClockTrimParity(void)
{
    const uint32_t xosc32ktrim = kFicr->XOSC32KTRIM;
    const uint32_t slopeFieldK =
        (xosc32ktrim & FICR_XOSC32KTRIM_SLOPE_Msk) >> FICR_XOSC32KTRIM_SLOPE_Pos;
    const uint32_t slopeMaskK =
        FICR_XOSC32KTRIM_SLOPE_Msk >> FICR_XOSC32KTRIM_SLOPE_Pos;
    const uint32_t slopeSignK = slopeMaskK - (slopeMaskK >> 1U);
    const int32_t slopeK =
        (int32_t)(slopeFieldK ^ slopeSignK) - (int32_t)slopeSignK;
    const uint32_t offsetK =
        (xosc32ktrim & FICR_XOSC32KTRIM_OFFSET_Msk) >> FICR_XOSC32KTRIM_OFFSET_Pos;
    const uint32_t lfxoIntcapFemtoF = 16000UL;
    const uint32_t lfxoMidValue =
        (2UL * lfxoIntcapFemtoF - 12000UL) *
            (uint32_t)(slopeK + 392) +
        ((offsetK << 3U) * 1000UL);
    uint32_t lfxoIntcap = lfxoMidValue / 512000UL;
    if ((lfxoMidValue % 512000UL) >= 256000UL) {
        ++lfxoIntcap;
    }
    NRF_OSCILLATORS->XOSC32KI.INTCAP =
        (lfxoIntcap << OSCILLATORS_XOSC32KI_INTCAP_VAL_Pos) &
        OSCILLATORS_XOSC32KI_INTCAP_VAL_Msk;

    const uint32_t xosc32mtrim = kFicr->XOSC32MTRIM;
    const uint32_t slopeFieldM =
        (xosc32mtrim & FICR_XOSC32MTRIM_SLOPE_Msk) >> FICR_XOSC32MTRIM_SLOPE_Pos;
    const uint32_t slopeMaskM =
        FICR_XOSC32MTRIM_SLOPE_Msk >> FICR_XOSC32MTRIM_SLOPE_Pos;
    const uint32_t slopeSignM = slopeMaskM - (slopeMaskM >> 1U);
    const int32_t slopeM =
        (int32_t)(slopeFieldM ^ slopeSignM) - (int32_t)slopeSignM;
    const uint32_t offsetM =
        (xosc32mtrim & FICR_XOSC32MTRIM_OFFSET_Msk) >> FICR_XOSC32MTRIM_OFFSET_Pos;
    const uint32_t hfxoIntcapFemtoF = 16000UL;
    const uint32_t hfxoMidValue =
        (((hfxoIntcapFemtoF - 5500UL) * (uint32_t)(slopeM + 791)) +
         ((offsetM << 2U) * 1000UL)) >>
        8U;
    uint32_t hfxoIntcap = hfxoMidValue / 1000UL;
    if ((hfxoMidValue % 1000UL) >= 500UL) {
        ++hfxoIntcap;
    }
    NRF_OSCILLATORS->XOSC32M.CONFIG.INTCAP =
        (hfxoIntcap << OSCILLATORS_XOSC32M_CONFIG_INTCAP_VAL_Pos) &
        OSCILLATORS_XOSC32M_CONFIG_INTCAP_VAL_Msk;

    NRF_REGULATORS->VREGMAIN.DCDCEN = REGULATORS_VREGMAIN_DCDCEN_VAL_Enabled;
    *reg32(kCacheEnableReg) = 1U;
}
#endif

void SystemInit(void)
{
#if defined(ARDUINO_NRF54_CPU_128M)
    setPllFrequency(OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK128M);
#else
    setPllFrequency(OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK64M);
#endif

#if !defined(NRF_TRUSTZONE_NONSECURE)
    zephyrApplySystemInitParity();
    zephyrApplyClockTrimParity();
#endif

    SystemCoreClockUpdate();
}

bool nrf54l15_core_set_cpu_frequency_hz(uint32_t hz)
{
    const uint32_t raw = cpuFrequencyRawFromHz(hz);
    if (raw == 0UL) {
        return false;
    }

    setPllFrequency(raw);
    SystemCoreClockUpdate();
    return currentPllFrequencyRaw() == raw;
}

uint32_t nrf54l15_core_get_cpu_frequency_hz(void)
{
    SystemCoreClockUpdate();
    return SystemCoreClock;
}

bool nrf54l15_core_set_idle_cpu_scaling_hz(uint32_t hz, bool enable)
{
    if (!enable) {
        g_idleCpuScalingEnabled = false;
        return true;
    }

    const uint32_t raw = cpuFrequencyRawFromHz(hz);
    if (raw == 0UL) {
        return false;
    }

    g_idleCpuTargetRaw = raw;
    g_idleCpuScalingEnabled = true;
    return true;
}

bool nrf54l15_core_get_idle_cpu_scaling_enabled(void)
{
    return g_idleCpuScalingEnabled;
}

uint32_t nrf54l15_core_get_idle_cpu_frequency_hz(void)
{
    return cpuFrequencyHzFromRaw(g_idleCpuTargetRaw);
}

uint32_t nrf54l15_core_enter_idle_cpu_scaling(void)
{
    if (!g_idleCpuScalingEnabled) {
        return 0UL;
    }

    const uint32_t currentRaw = currentPllFrequencyRaw();
    if ((currentRaw == 0UL) || (currentRaw == g_idleCpuTargetRaw)) {
        return 0UL;
    }

    setPllFrequency(g_idleCpuTargetRaw);
    SystemCoreClockUpdate();
    return currentRaw;
}

void nrf54l15_core_exit_idle_cpu_scaling(uint32_t previousRaw)
{
    if (previousRaw == 0UL) {
        return;
    }

    setPllFrequency(previousRaw);
    SystemCoreClockUpdate();
}
