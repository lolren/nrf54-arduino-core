#include "nrf54l15_hal.h"
#include "nrf54l15_hal_board_policy_internal.h"
#include "nrf54l15_hal_support_internal.h"
#include "nrf54l15_hal_timebase_internal.h"

#include <Arduino.h>
#include <string.h>
#include <cmsis.h>
#include "variant.h"

extern "C" uint8_t nrf54l15_constlat_users_active(void) __attribute__((weak));
namespace xiao_nrf54l15 {
class I2sTx;
class I2sRx;
class I2sDuplex;
}


// This file is intentionally an ordered amalgamation of smaller HAL fragments.
// Keep fragments in this order unless the cross-fragment helper dependencies are also refactored.
#include "nrf54l15_hal_parts/nrf54l15_hal_internal.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_hooks.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_peripherals.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_i2s_radio802154.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_setup_gatt.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_advertising.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_connection_api.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_connection_events.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_scanning_connections.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_protocol_security.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_radio_tail.inc"
