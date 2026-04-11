#include "Arduino.h"
#include "variant.h"

#include <string.h>

extern "C" uint8_t arduinoXiaoNrf54l15SetAntenna(uint8_t) { return 0U; }
extern "C" uint8_t arduinoXiaoNrf54l15GetAntenna(void) {
    return static_cast<uint8_t>(XIAO_NRF54L15_ANTENNA_CERAMIC);
}
extern "C" uint8_t arduinoXiaoNrf54l15SetRfSwitchPower(uint8_t) { return 0U; }
extern "C" uint8_t arduinoXiaoNrf54l15GetRfSwitchPower(void) { return 0U; }
extern "C" uint8_t arduinoXiaoNrf54l15SetBatteryEnable(uint8_t) { return 1U; }
extern "C" uint8_t arduinoXiaoNrf54l15GetBatteryEnable(void) { return 0U; }
extern "C" uint8_t arduinoXiaoNrf54l15SetImuMicEnable(uint8_t) { return 1U; }
extern "C" uint8_t arduinoXiaoNrf54l15GetImuMicEnable(void) { return 0U; }

// Bare module variants do not have the XIAO RF switch hardware. Keep the raw
// XIAO hooks link-compatible, but leave them as no-ops so old sketches build
// without accidentally driving a fake board policy.
extern "C" void xiaoNrf54l15SetAntenna(xiao_nrf54l15_antenna_t) {}
extern "C" xiao_nrf54l15_antenna_t xiaoNrf54l15GetAntenna(void) {
    return XIAO_NRF54L15_ANTENNA_CERAMIC;
}
extern "C" uint8_t xiaoNrf54l15SaveBoardState(xiao_nrf54l15_board_state_t* state) {
    if (state == nullptr) {
        return 0U;
    }
    memset(state, 0, sizeof(*state));
    return 1U;
}
extern "C" uint8_t xiaoNrf54l15RestoreBoardState(const xiao_nrf54l15_board_state_t* state) {
    return (state != nullptr) ? 1U : 0U;
}
extern "C" void xiaoNrf54l15EnterLowestPowerBoardState(void) {}

extern "C" void initVariant(void) {
    // Bare module variants do not require board-specific rail bring-up.
}
