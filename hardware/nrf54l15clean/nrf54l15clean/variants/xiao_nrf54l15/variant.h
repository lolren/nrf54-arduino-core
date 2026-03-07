#ifndef VARIANT_H
#define VARIANT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pins_arduino.h"

typedef enum {
    XIAO_NRF54L15_ANTENNA_CERAMIC = 0,
    XIAO_NRF54L15_ANTENNA_EXTERNAL = 1,
    XIAO_NRF54L15_ANTENNA_CONTROL_HIZ = 2,
} xiao_nrf54l15_antenna_t;

void xiaoNrf54l15SetAntenna(xiao_nrf54l15_antenna_t antenna);
xiao_nrf54l15_antenna_t xiaoNrf54l15GetAntenna(void);

// Arduino-compatible board control helpers (aligned with other XIAO cores).
uint8_t arduinoXiaoNrf54l15SetAntenna(uint8_t selection);
uint8_t arduinoXiaoNrf54l15GetAntenna(void);
uint8_t arduinoXiaoNrf54l15SetRfSwitchPower(uint8_t enabled);
uint8_t arduinoXiaoNrf54l15GetRfSwitchPower(void);
uint8_t arduinoXiaoNrf54l15SetBatteryEnable(uint8_t enabled);
uint8_t arduinoXiaoNrf54l15GetBatteryEnable(void);
uint8_t arduinoXiaoNrf54l15SetImuMicEnable(uint8_t enabled);
uint8_t arduinoXiaoNrf54l15GetImuMicEnable(void);

#ifdef __cplusplus
}
#endif

#endif
