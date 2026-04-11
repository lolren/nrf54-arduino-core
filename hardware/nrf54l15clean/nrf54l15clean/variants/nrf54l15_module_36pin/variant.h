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

typedef struct {
    uint32_t pinCnf;
    uint8_t isOutput;
    uint8_t outputHigh;
} xiao_nrf54l15_pin_state_t;

typedef struct {
    xiao_nrf54l15_pin_state_t rfSwitchPower;
    xiao_nrf54l15_pin_state_t rfSwitchControl;
    xiao_nrf54l15_pin_state_t batteryEnable;
    xiao_nrf54l15_pin_state_t imuMicEnable;
    xiao_nrf54l15_pin_state_t imuInt;
    xiao_nrf54l15_pin_state_t imuScl;
    xiao_nrf54l15_pin_state_t imuSda;
    xiao_nrf54l15_pin_state_t micClk;
    xiao_nrf54l15_pin_state_t micData;
    xiao_nrf54l15_pin_state_t samd11Tx;
    xiao_nrf54l15_pin_state_t samd11Rx;
} xiao_nrf54l15_board_state_t;

#define XIAO_NRF54L15_BOARD_STATE_DECLARED 1

// XIAO compatibility hooks. On the bare module variants these are safe no-op
// shims so sketches that were written against the XIAO board helpers still
// compile even though the module does not expose the same board rails.
void xiaoNrf54l15SetAntenna(xiao_nrf54l15_antenna_t antenna);
xiao_nrf54l15_antenna_t xiaoNrf54l15GetAntenna(void);
uint8_t arduinoXiaoNrf54l15SetAntenna(uint8_t selection);
uint8_t arduinoXiaoNrf54l15GetAntenna(void);
uint8_t arduinoXiaoNrf54l15SetRfSwitchPower(uint8_t enabled);
uint8_t arduinoXiaoNrf54l15GetRfSwitchPower(void);
uint8_t arduinoXiaoNrf54l15SetBatteryEnable(uint8_t enabled);
uint8_t arduinoXiaoNrf54l15GetBatteryEnable(void);
uint8_t arduinoXiaoNrf54l15SetImuMicEnable(uint8_t enabled);
uint8_t arduinoXiaoNrf54l15GetImuMicEnable(void);
uint8_t xiaoNrf54l15SaveBoardState(xiao_nrf54l15_board_state_t* state);
uint8_t xiaoNrf54l15RestoreBoardState(const xiao_nrf54l15_board_state_t* state);
void xiaoNrf54l15EnterLowestPowerBoardState(void);

void initVariant(void);

#ifdef __cplusplus
}
#endif

#endif
