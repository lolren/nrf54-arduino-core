/*
 * nPM1300 PMIC driver for XIAO nRF54LM20A.
 *
 * The nPM1300 register protocol uses a two-byte address: {base, offset}.
 * This API follows the same access pattern used by Zephyr's nPM13xx MFD,
 * regulator, charger, GPIO, and LED drivers.
 */

#ifndef NPM1300_H
#define NPM1300_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NPM1300_ADDR 0x6B

/* Register bases. */
#define NPM1300_BASE_MAIN    0x00
#define NPM1300_BASE_VBUS    0x02
#define NPM1300_BASE_CHARGER 0x03
#define NPM1300_BASE_BUCK    0x04
#define NPM1300_BASE_ADC     0x05
#define NPM1300_BASE_GPIO    0x06
#define NPM1300_BASE_TIMER   0x07
#define NPM1300_BASE_LDSW    0x08
#define NPM1300_BASE_LED     0x0A
#define NPM1300_BASE_SHIP    0x0B

/* GPIO modes, matching Zephyr's nordic,npm13xx GPIO binding. */
#define NPM1300_GPIO_GPIINPUT         0
#define NPM1300_GPIO_GPILOGIC1        1
#define NPM1300_GPIO_GPILOGIC0        2
#define NPM1300_GPIO_GPIEVENTRISE     3
#define NPM1300_GPIO_GPIEVENTFALL     4
#define NPM1300_GPIO_GPOIRQ           5
#define NPM1300_GPIO_GPORESET         6
#define NPM1300_GPIO_GPOPWRLOSSWARN   7
#define NPM1300_GPIO_GPOLOGIC1        8
#define NPM1300_GPIO_GPOLOGIC0        9

/* VBUS status bits returned by npm1300_vbus_status(). */
#define NPM1300_VBUS_STATUS_PRESENT      0x01
#define NPM1300_VBUS_STATUS_CUR_LIMIT    0x02
#define NPM1300_VBUS_STATUS_OVERVOLT     0x04
#define NPM1300_VBUS_STATUS_UNDERVOLT    0x08
#define NPM1300_VBUS_STATUS_SUSPENDED    0x10
#define NPM1300_VBUS_STATUS_BUSOUT       0x20

/* Common voltage/current helper values. */
#define NPM1300_LDO_VOLTAGE_1V0      1000
#define NPM1300_LDO_VOLTAGE_1V1      1100
#define NPM1300_LDO_VOLTAGE_1V2      1200
#define NPM1300_LDO_VOLTAGE_1V3      1300
#define NPM1300_LDO_VOLTAGE_1V4      1400
#define NPM1300_LDO_VOLTAGE_1V5      1500
#define NPM1300_LDO_VOLTAGE_1V6      1600
#define NPM1300_LDO_VOLTAGE_1V7      1700
#define NPM1300_LDO_VOLTAGE_1V8      1800
#define NPM1300_LDO_VOLTAGE_2V0      2000
#define NPM1300_LDO_VOLTAGE_2V5      2500
#define NPM1300_LDO_VOLTAGE_3V0      3000
#define NPM1300_LDO_VOLTAGE_3V3      3300

#define NPM1300_CHARGE_CURRENT_32MA   32
#define NPM1300_CHARGE_CURRENT_50MA   50
#define NPM1300_CHARGE_CURRENT_100MA  100
#define NPM1300_CHARGE_CURRENT_150MA  150
#define NPM1300_CHARGE_CURRENT_200MA  200
#define NPM1300_CHARGE_CURRENT_300MA  300
#define NPM1300_CHARGE_CURRENT_400MA  400
#define NPM1300_CHARGE_CURRENT_500MA  500
#define NPM1300_CHARGE_CURRENT_800MA  800

/* Raw MFD-style register access. */
bool npm1300_read_reg(uint8_t base, uint8_t offset, uint8_t *value);
bool npm1300_write_reg(uint8_t base, uint8_t offset, uint8_t value);
bool npm1300_read_burst(uint8_t base, uint8_t offset, uint8_t *data, size_t len);
bool npm1300_write_burst(uint8_t base, uint8_t offset, const uint8_t *data, size_t len);
bool npm1300_update_reg(uint8_t base, uint8_t offset, uint8_t mask, uint8_t value);

/* Initialize/probe the PMIC bus. These calls are optional; all API calls lazy-init. */
void npm1300_begin(void);
void npm1300_init(void);  /* compatibility alias */
bool npm1300_is_present(void);

/* Load switch/LDO rails. */
bool npm1300_ldo1_enable(bool enable);
bool npm1300_ldo1_set_voltage(uint16_t mv);
bool npm1300_ldo1_is_enabled(void);
bool npm1300_ldo2_enable(bool enable);
bool npm1300_ldo2_set_voltage(uint16_t mv);
bool npm1300_ldo2_is_enabled(void);

/* Load-switch/LDO function select.
 * LDSW mode is a plain load switch using LSIN as input.
 * LDO mode regulates LSOUT to the selected voltage.
 */
#define NPM1300_LDSW_MODE_LOAD_SWITCH 0
#define NPM1300_LDSW_MODE_LDO         1

bool npm1300_ldo1_set_mode(uint8_t mode);
bool npm1300_ldo2_set_mode(uint8_t mode);

/* XIAO nRF54LM20A helper: LDO1 feeds the shared IMU&MIC_3V3 rail. */
bool npm1300_imu_mic_power_enable(bool enable);
bool npm1300_sensor_power_enable(bool enable);  /* compatibility alias */

/* Stop optional PMIC measurement loads before MCU sleep. */
bool npm1300_prepare_for_sleep(void);

/* XIAO nRF54LM20A buck wiring.
 *
 * BUCK1 is not connected on this board. BUCK2 is the fixed 3.3 V VSYS_3V3
 * system rail; it powers both the nRF54LM20A and the SAMD11 USB/debug bridge.
 * Converter mode may be tuned through the board-aware helpers below, but the
 * system rail must not be disabled or moved away from its strapped voltage.
 */
#define NPM1300_SYSTEM_BUCK_VOLTAGE_MV 3300U

bool npm1300_buck1_enable(bool enable);
bool npm1300_buck1_set_voltage(uint16_t mv);
bool npm1300_buck1_is_enabled(void);

/* Legacy raw BUCK2 mutators are retained for source compatibility.
 * By default, disable requests and all voltage mutations fail closed without
 * writing the PMIC. Defining NPM1300_ENABLE_UNSAFE_SYSTEM_RAIL_CONTROL for the
 * complete core build restores the old raw behavior and can remove both MCU
 * and upload-probe power; normal applications must not enable it.
 */
#if defined(__GNUC__) || defined(__clang__)
#define NPM1300_DEPRECATED(message) __attribute__((deprecated(message)))
#else
#define NPM1300_DEPRECATED(message)
#endif

bool npm1300_buck2_enable(bool enable)
    NPM1300_DEPRECATED("BUCK2 is the XIAO system rail; disable requests are blocked");
bool npm1300_buck2_set_voltage(uint16_t mv)
    NPM1300_DEPRECATED("BUCK2 is the fixed XIAO system rail; voltage changes are blocked");
bool npm1300_buck2_is_enabled(void);

/* Buck mode control (applies to both BUCK1 and BUCK2).
 * AUTO (default): Hysteretic at light load, PWM at heavy load.
 * FORCE_PWM:       Low-ripple forced PWM (higher quiescent current).
 * FORCE_HYST:      Forced Hysteretic (lowest IQ, higher ripple for light loads).
 */
#define NPM1300_BUCK_MODE_AUTO       0
#define NPM1300_BUCK_MODE_FORCE_PWM  1
#define NPM1300_BUCK_MODE_FORCE_HYST 2

bool npm1300_buck1_set_mode(uint8_t mode);
bool npm1300_buck2_set_mode(uint8_t mode);

/* Preferred board-aware access to the fixed BUCK2 system rail. */
bool npm1300_system_buck_is_enabled(void);
bool npm1300_system_buck_set_mode(uint8_t mode);

#undef NPM1300_DEPRECATED

/* Battery charger and status. */
bool npm1300_charger_enable(bool enable);
bool npm1300_charger_set_current(uint16_t ma);
bool npm1300_charger_set_discharge_current_ma(uint16_t ma);
bool npm1300_charger_set_term_voltage(uint16_t mv);
bool npm1300_charger_is_charging(void);
bool npm1300_charger_status(uint8_t *status);
bool npm1300_charger_error(uint8_t *error);
bool npm1300_vbus_status(uint8_t *status);

/* VBUS input-current limit.
 * This is the PMIC input limiter, not the battery charge-current setpoint.
 * Range: 100 mA to 1500 mA in 100 mA steps. The setter also applies the
 * active ILIM switch task so the new limit takes effect immediately.
 */
bool npm1300_vbus_set_input_current_limit_ma(uint16_t ma);
uint16_t npm1300_vbus_get_input_current_limit_ma(void);

/* Measurements. Returns value in requested units, or -1 when unavailable. */
int32_t npm1300_read_vbat_mv(void);
int32_t npm1300_read_temp_mc(void);  /* PMIC die temperature in millicelsius */
int32_t npm1300_read_ibat_ma(void);
int32_t npm1300_read_vsys_mv(void);
int32_t npm1300_read_vbus_mv(void);

/* Ultra-low power modes.
 * Plain nPM1300 Ship/Hibernate require USB/VBUS to be disconnected.
 * Timed hibernate selects the lowest valid cold-reset path automatically:
 * nPM1300 Hibernate while VBUS is absent, or nRF54 GRTC System OFF while
 * VBUS is present. A successful timed entry normally does not return; wake
 * restarts the sketch from setup(). Returns false when the request cannot be
 * armed. Both paths accept the range below.
 */
#define NPM1300_HIBERNATE_TIMER_PRESCALE_MS 16UL
#define NPM1300_HIBERNATE_TIMER_MAX_MS      268435440UL

bool npm1300_enter_ship_mode(void);
bool npm1300_enter_hibernate(void);
bool npm1300_set_ship_debounce(uint8_t code);
bool npm1300_save_ship_config(void);
bool npm1300_configure_hibernate_timer_ms(uint32_t delay_ms);
bool npm1300_enter_timed_hibernate_ms(uint32_t delay_ms);
bool npm1300_enter_hibernate_after_ms(uint32_t delay_ms);  /* compatibility alias */

/* PMIC LED and GPIO helpers. LED brightness is treated as off/on. */
bool npm1300_led_set(uint8_t led, uint8_t brightness);
bool npm1300_gpio_set_mode(uint8_t pin, uint8_t mode);
bool npm1300_gpio_write(uint8_t pin, bool high);
bool npm1300_gpio_status(uint8_t *status);

#ifdef __cplusplus
}
#endif

#endif
