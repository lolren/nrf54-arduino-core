#pragma once

#include "nrf54l15_hal.h"

namespace xiao_nrf54l15::hal_internal {

bool bleHfxoRunning();
bool grtcSyscounterReady(NRF_GRTC_Type* grtc);
uint64_t readGrtcCounterPreserveActive(NRF_GRTC_Type* grtc);
void ensureGrtcReady(NRF_GRTC_Type* grtc);
uint64_t readGrtcCounter(NRF_GRTC_Type* grtc);

void ensureLfxoRunning();
void busyWaitApproxUs(uint32_t us);
void programSystemOffWake(uint32_t delayUs);

}  // namespace xiao_nrf54l15::hal_internal
