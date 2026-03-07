#ifndef SOC_H_
#define SOC_H_

/*
 * Minimal SoC compatibility header for the clean Arduino core.
 */

#include "cmsis.h"

#ifndef NUM_IRQ_PRIO_BITS
#define NUM_IRQ_PRIO_BITS __NVIC_PRIO_BITS
#endif

#endif /* SOC_H_ */
