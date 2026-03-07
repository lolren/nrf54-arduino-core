/*
 * Minimal system clock init for nRF54L15 Arduino core.
 *
 * Keeps CPU domain at 64 MHz so Arduino timing APIs behave as expected.
 */

#include <stdint.h>

#include "cmsis.h"
#include "nrf54l15.h"

uint32_t SystemCoreClock = 64000000UL;

void SystemCoreClockUpdate(void)
{
    uint32_t current = (NRF_OSCILLATORS->PLL.CURRENTFREQ &
                        OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Msk) >>
                       OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Pos;

    if (current == OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK128M) {
        SystemCoreClock = 128000000UL;
    } else {
        SystemCoreClock = 64000000UL;
    }
}

void SystemInit(void)
{
#if defined(ARDUINO_NRF54_CPU_128M)
    NRF_OSCILLATORS->PLL.FREQ =
        (OSCILLATORS_PLL_FREQ_FREQ_CK128M << OSCILLATORS_PLL_FREQ_FREQ_Pos);

    uint32_t guard = 0;
    while ((((NRF_OSCILLATORS->PLL.CURRENTFREQ &
              OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Msk) >>
             OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Pos) !=
            OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK128M) &&
           (guard++ < 1000000UL)) {
        __NOP();
    }
#else
    NRF_OSCILLATORS->PLL.FREQ =
        (OSCILLATORS_PLL_FREQ_FREQ_CK64M << OSCILLATORS_PLL_FREQ_FREQ_Pos);

    uint32_t guard = 0;
    while ((((NRF_OSCILLATORS->PLL.CURRENTFREQ &
              OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Msk) >>
             OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Pos) !=
            OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK64M) &&
           (guard++ < 1000000UL)) {
        __NOP();
    }
#endif

    SystemCoreClockUpdate();
}
