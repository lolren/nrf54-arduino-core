#pragma once

#include "ble_channel_sounding.h"

namespace xiao_nrf54l15 {

// Measured on 2026-04-16 for the active XIAO nRF54L15 / Sense ceramic-antenna
// pair at approximately 0.20 m spacing. This is a local bench profile, not a
// factory-safe board-agnostic constant.
static constexpr BleCsCalibrationProfile kBleCsCalibrationProfileXiaoPair20cm{
    1.000000f,
    -0.1944f,
    0.2000f,
    0.3944f,
    0.1347f,
    0.1944f,
    0.6484f,
    0.3242f,
    0.2282f,
    0.0234f,
    0.1283f,
    9U,
    6U,
};

}  // namespace xiao_nrf54l15
