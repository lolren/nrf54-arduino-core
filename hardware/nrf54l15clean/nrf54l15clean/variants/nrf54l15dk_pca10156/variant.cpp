#include "Arduino.h"
#include "variant.h"

#include <string.h>

namespace {

// The DK has a fixed RF path from the SoC to the board antenna. Keep a small
// software-side ownership bit so existing radio helpers stay link-compatible.
static uint8_t g_fixedRfPathEnabled = 1U;

}  // namespace

extern "C" uint8_t arduinoXiaoNrf54l15SetAntenna(uint8_t) { return 0U; }
extern "C" uint8_t arduinoXiaoNrf54l15GetAntenna(void) {
    return static_cast<uint8_t>(XIAO_NRF54L15_ANTENNA_CERAMIC);
}
extern "C" uint8_t arduinoXiaoNrf54l15SetRfSwitchPower(uint8_t enabled) {
    g_fixedRfPathEnabled = enabled ? 1U : 0U;
    return 1U;
}
extern "C" uint8_t arduinoXiaoNrf54l15GetRfSwitchPower(void) {
    return g_fixedRfPathEnabled;
}
extern "C" uint8_t arduinoXiaoNrf54l15SetBatteryEnable(uint8_t) { return 1U; }
extern "C" uint8_t arduinoXiaoNrf54l15GetBatteryEnable(void) { return 0U; }
extern "C" uint8_t arduinoXiaoNrf54l15SetImuMicEnable(uint8_t) { return 1U; }
extern "C" uint8_t arduinoXiaoNrf54l15GetImuMicEnable(void) { return 0U; }

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
    // The DK does not need variant-specific rail bring-up.
}
