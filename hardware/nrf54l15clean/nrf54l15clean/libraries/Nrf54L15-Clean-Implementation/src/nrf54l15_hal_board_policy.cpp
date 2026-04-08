#include "nrf54l15_hal_board_policy_internal.h"

#include <Arduino.h>
#include "variant.h"

namespace xiao_nrf54l15::hal_internal {
namespace {

#if defined(NRF54L15_CLEAN_ANTENNA_EXTERNAL)
constexpr BoardAntennaPath kDefaultBoardAntennaPath =
    BoardAntennaPath::kExternal;
#else
constexpr BoardAntennaPath kDefaultBoardAntennaPath =
    BoardAntennaPath::kCeramic;
#endif

BoardAntennaPath g_boardDesiredAntennaPath = kDefaultBoardAntennaPath;

}  // namespace

xiao_nrf54l15_antenna_t boardAntennaSelectionFromPath(BoardAntennaPath path) {
  switch (path) {
    case BoardAntennaPath::kExternal:
      return XIAO_NRF54L15_ANTENNA_EXTERNAL;
    case BoardAntennaPath::kControlHighImpedance:
      return XIAO_NRF54L15_ANTENNA_CONTROL_HIZ;
    case BoardAntennaPath::kCeramic:
    default:
      return XIAO_NRF54L15_ANTENNA_CERAMIC;
  }
}

BoardAntennaPath boardDesiredAntennaPath() { return g_boardDesiredAntennaPath; }

void setBoardDesiredAntennaPath(BoardAntennaPath path) {
  g_boardDesiredAntennaPath = path;
}

}  // namespace xiao_nrf54l15::hal_internal

namespace xiao_nrf54l15 {
using namespace hal_internal;

bool BoardControl::setAntennaPath(BoardAntennaPath path) {
  xiaoNrf54l15SetAntenna(boardAntennaSelectionFromPath(path));
  setBoardDesiredAntennaPath(path);
  return true;
}

BoardAntennaPath BoardControl::antennaPath() {
  switch (xiaoNrf54l15GetAntenna()) {
    case XIAO_NRF54L15_ANTENNA_EXTERNAL:
      return BoardAntennaPath::kExternal;
    case XIAO_NRF54L15_ANTENNA_CONTROL_HIZ:
      return BoardAntennaPath::kControlHighImpedance;
    case XIAO_NRF54L15_ANTENNA_CERAMIC:
    default:
      return BoardAntennaPath::kCeramic;
  }
}

bool BoardControl::setImuMicEnabled(bool enable) {
  return arduinoXiaoNrf54l15SetImuMicEnable(enable ? 1U : 0U) != 0U;
}

bool BoardControl::imuMicEnabled() {
  return arduinoXiaoNrf54l15GetImuMicEnable() != 0U;
}

bool BoardControl::setRfSwitchPowerEnabled(bool enable) {
  return arduinoXiaoNrf54l15SetRfSwitchPower(enable ? 1U : 0U) != 0U;
}

bool BoardControl::rfSwitchPowerEnabled() {
  return arduinoXiaoNrf54l15GetRfSwitchPower() != 0U;
}

bool BoardControl::enableRfPath(BoardAntennaPath path) {
  return setRfSwitchPowerEnabled(true) && setAntennaPath(path);
}

bool BoardControl::collapseRfPathIdle(BoardAntennaPath idlePath,
                                      bool disablePower) {
  xiaoNrf54l15SetAntenna(boardAntennaSelectionFromPath(idlePath));
  if (!disablePower) {
    return true;
  }
  return setRfSwitchPowerEnabled(false);
}

void BoardControl::enterLowestPowerState() {
  xiaoNrf54l15EnterLowestPowerBoardState();
}

bool BoardControl::setBatterySenseEnabled(bool enable) {
  if (!Gpio::configure(kPinVbatEnable, GpioDirection::kOutput,
                       GpioPull::kDisabled)) {
    return false;
  }
  return Gpio::write(kPinVbatEnable, enable);
}

bool BoardControl::sampleBatteryMilliVolts(int32_t* outMilliVolts,
                                           uint32_t settleDelayUs,
                                           uint32_t spinLimit) {
  if (outMilliVolts == nullptr) {
    return false;
  }

  Saadc adc(nrf54l15::SAADC_BASE);
  if (!adc.begin(AdcResolution::k12bit, spinLimit)) {
    return false;
  }

  bool ok = setBatterySenseEnabled(true);
  if (ok && settleDelayUs > 0U) {
    delayMicroseconds(settleDelayUs);
  }

  int32_t halfMilliVolts = 0;
  if (ok) {
    ok = adc.configureSingleEnded(0U, kPinVbatSense, AdcGain::k2over8) &&
         adc.sampleMilliVolts(&halfMilliVolts, spinLimit);
  }

  const bool disableOk = setBatterySenseEnabled(false);
  adc.end();

  if (!ok || !disableOk) {
    return false;
  }

  *outMilliVolts = halfMilliVolts * 2;
  return true;
}

bool BoardControl::sampleBatteryPercent(uint8_t* outPercent,
                                        int32_t emptyMilliVolts,
                                        int32_t fullMilliVolts,
                                        uint32_t settleDelayUs,
                                        uint32_t spinLimit) {
  if (outPercent == nullptr || fullMilliVolts <= emptyMilliVolts) {
    return false;
  }

  int32_t batteryMv = 0;
  if (!sampleBatteryMilliVolts(&batteryMv, settleDelayUs, spinLimit)) {
    return false;
  }

  if (batteryMv <= emptyMilliVolts) {
    *outPercent = 0U;
    return true;
  }
  if (batteryMv >= fullMilliVolts) {
    *outPercent = 100U;
    return true;
  }

  const int32_t range = fullMilliVolts - emptyMilliVolts;
  const int32_t level = batteryMv - emptyMilliVolts;
  *outPercent = static_cast<uint8_t>((level * 100 + (range / 2)) / range);
  return true;
}

}  // namespace xiao_nrf54l15
