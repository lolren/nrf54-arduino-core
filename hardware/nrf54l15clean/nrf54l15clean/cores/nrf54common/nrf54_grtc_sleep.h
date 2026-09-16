#pragma once

// nRF54LM20A/B PS v1.0, GRTC SYSCOUNTER: TIMEOUT > WAKETIME + guard,
// with a minimum guard of one LFCLK cycle. Share this policy with BLE so
// initializing the radio cannot replace the core's valid sleep settings.
enum {
    kNrf54GrtcSystemOnTimeoutLfclk = 6U,
    kNrf54GrtcSystemOnWakeLfclk = 4U,
    kNrf54GrtcSleepGuardLfclk = 1U
};

#if defined(__cplusplus)
static_assert(kNrf54GrtcSystemOnTimeoutLfclk >
                  kNrf54GrtcSystemOnWakeLfclk + kNrf54GrtcSleepGuardLfclk,
              "GRTC TIMEOUT must exceed WAKETIME plus guard");
#else
_Static_assert(kNrf54GrtcSystemOnTimeoutLfclk >
                   kNrf54GrtcSystemOnWakeLfclk + kNrf54GrtcSleepGuardLfclk,
               "GRTC TIMEOUT must exceed WAKETIME plus guard");
#endif
