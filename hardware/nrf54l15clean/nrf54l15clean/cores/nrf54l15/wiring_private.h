/*
 * Minimal wiring_private compatibility header.
 *
 * Some third-party libraries include this header unconditionally to access
 * pinPeripheral() on SAMD-like cores. For this core we provide a harmless
 * no-op implementation so those libraries still compile.
 */

#ifndef WIRING_PRIVATE_H
#define WIRING_PRIVATE_H

#include <stdint.h>

typedef enum {
    PIO_NOT_A_PIN = 0,
    PIO_DIGITAL,
    PIO_ANALOG,
    PIO_SERCOM,
    PIO_SERCOM_ALT,
    PIO_TIMER,
    PIO_TIMER_ALT,
    PIO_COM,
    PIO_AC_CLK,
} EPioType;

static inline int pinPeripheral(uint32_t pin, EPioType peripheral)
{
    (void)pin;
    (void)peripheral;
    return 1;
}

#endif
