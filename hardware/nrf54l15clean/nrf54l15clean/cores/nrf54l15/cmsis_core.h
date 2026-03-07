#ifndef CMSIS_CORE_H
#define CMSIS_CORE_H

/*
 * Zephyr expects <cmsis_core.h> from its CMSIS modules. During Arduino
 * library-discovery this generated include set may not exist yet, so fall
 * back to the core's local CMSIS compatibility header.
 */

#if defined(__has_include)
#  if __has_include(<modules/cmsis/cmsis_core.h>)
#    include <modules/cmsis/cmsis_core.h>
#  elif __has_include(<modules/cmsis_6/cmsis_core.h>)
#    include <modules/cmsis_6/cmsis_core.h>
#  else
#    include "cmsis.h"
#  endif
#else
#  include "cmsis.h"
#endif

#endif /* CMSIS_CORE_H */
