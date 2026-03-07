/*
 * nRF54L15 clean-core compatibility header.
 *
 * Pulls in Nordic register/type definitions and provides compatibility
 * aliases used by this Arduino core.
 */

#ifndef NRF54L15_H
#define NRF54L15_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Newlib math headers may define these as numeric errno helpers.
// Nordic register structs use the same identifiers as field names.
#ifdef OVERFLOW
#undef OVERFLOW
#endif
#ifdef DOMAIN
#undef DOMAIN
#endif

#include "nrf54l15_types.h"

// ============================================================================
// Non-secure peripheral base addresses used by this core
// ============================================================================

#define NRF_P2_NS_BASE         0x40050400UL
#define NRF_P2_S_BASE          0x50050400UL
#define NRF_SPIM00_NS_BASE     0x4004A000UL
#define NRF_SPIM00_S_BASE      0x5004A000UL
#define NRF_SPIM20_NS_BASE     0x400C6000UL
#define NRF_TWIM20_NS_BASE     0x400C6000UL
#define NRF_UARTE20_NS_BASE    0x400C6000UL
#define NRF_SPIM20_S_BASE      0x500C6000UL
#define NRF_TWIM20_S_BASE      0x500C6000UL
#define NRF_UARTE20_S_BASE     0x500C6000UL
#define NRF_SPIM21_NS_BASE     0x400C7000UL
#define NRF_TWIM21_NS_BASE     0x400C7000UL
#define NRF_UARTE21_NS_BASE    0x400C7000UL
#define NRF_SPIM21_S_BASE      0x500C7000UL
#define NRF_TWIM21_S_BASE      0x500C7000UL
#define NRF_UARTE21_S_BASE     0x500C7000UL
#define NRF_TWIM22_NS_BASE     0x400C8000UL
#define NRF_TWIM22_S_BASE      0x500C8000UL
#define NRF_TWIM30_NS_BASE     0x40104000UL
#define NRF_TWIM30_S_BASE      0x50104000UL
#define NRF_PWM20_NS_BASE      0x400D2000UL
#define NRF_PWM21_NS_BASE      0x400D3000UL
#define NRF_SAADC_NS_BASE      0x400D5000UL
#define NRF_OSCILLATORS_NS_BASE 0x40120000UL
#define NRF_PWM20_S_BASE       0x500D2000UL
#define NRF_PWM21_S_BASE       0x500D3000UL
#define NRF_SAADC_S_BASE       0x500D5000UL
#define NRF_OSCILLATORS_S_BASE  0x50120000UL
#define NRF_P1_NS_BASE         0x400D8200UL
#define NRF_P1_S_BASE          0x500D8200UL
#define NRF_P0_NS_BASE         0x4010A000UL
#define NRF_P0_S_BASE          0x5010A000UL

#ifdef NRF_TRUSTZONE_NONSECURE
#define NRF_P0_BASE            NRF_P0_NS_BASE
#define NRF_P1_BASE            NRF_P1_NS_BASE
#define NRF_P2_BASE            NRF_P2_NS_BASE
#define NRF_SPIM00_BASE        NRF_SPIM00_NS_BASE
#define NRF_SPIM20_BASE        NRF_SPIM20_NS_BASE
#define NRF_SPIM21_BASE        NRF_SPIM21_NS_BASE
#define NRF_TWIM20_BASE        NRF_TWIM20_NS_BASE
#define NRF_TWIM21_BASE        NRF_TWIM21_NS_BASE
#define NRF_TWIM22_BASE        NRF_TWIM22_NS_BASE
#define NRF_TWIM30_BASE        NRF_TWIM30_NS_BASE
#define NRF_UARTE20_BASE       NRF_UARTE20_NS_BASE
#define NRF_UARTE21_BASE       NRF_UARTE21_NS_BASE
#define NRF_PWM20_BASE         NRF_PWM20_NS_BASE
#define NRF_PWM21_BASE         NRF_PWM21_NS_BASE
#define NRF_SAADC_BASE         NRF_SAADC_NS_BASE
#define NRF_OSCILLATORS_BASE   NRF_OSCILLATORS_NS_BASE
#else
#define NRF_P0_BASE            NRF_P0_S_BASE
#define NRF_P1_BASE            NRF_P1_S_BASE
#define NRF_P2_BASE            NRF_P2_S_BASE
#define NRF_SPIM00_BASE        NRF_SPIM00_S_BASE
#define NRF_SPIM20_BASE        NRF_SPIM20_S_BASE
#define NRF_SPIM21_BASE        NRF_SPIM21_S_BASE
#define NRF_TWIM20_BASE        NRF_TWIM20_S_BASE
#define NRF_TWIM21_BASE        NRF_TWIM21_S_BASE
#define NRF_TWIM22_BASE        NRF_TWIM22_S_BASE
#define NRF_TWIM30_BASE        NRF_TWIM30_S_BASE
#define NRF_UARTE20_BASE       NRF_UARTE20_S_BASE
#define NRF_UARTE21_BASE       NRF_UARTE21_S_BASE
#define NRF_PWM20_BASE         NRF_PWM20_S_BASE
#define NRF_PWM21_BASE         NRF_PWM21_S_BASE
#define NRF_SAADC_BASE         NRF_SAADC_S_BASE
#define NRF_OSCILLATORS_BASE   NRF_OSCILLATORS_S_BASE
#endif

// ============================================================================
// Peripheral pointers (Arduino core naming)
// ============================================================================

#define NRF_P0        ((NRF_GPIO_Type *)NRF_P0_BASE)
#define NRF_P1        ((NRF_GPIO_Type *)NRF_P1_BASE)
#define NRF_P2        ((NRF_GPIO_Type *)NRF_P2_BASE)

#define NRF_SPIM00    ((NRF_SPIM_Type *)NRF_SPIM00_BASE)
#define NRF_SPIM20    ((NRF_SPIM_Type *)NRF_SPIM20_BASE)
#define NRF_SPIM21    ((NRF_SPIM_Type *)NRF_SPIM21_BASE)
#define NRF_TWIM20    ((NRF_TWIM_Type *)NRF_TWIM20_BASE)
#define NRF_TWIM21    ((NRF_TWIM_Type *)NRF_TWIM21_BASE)
#define NRF_TWIM22    ((NRF_TWIM_Type *)NRF_TWIM22_BASE)
#define NRF_TWIM30    ((NRF_TWIM_Type *)NRF_TWIM30_BASE)
#define NRF_UARTE20   ((NRF_UARTE_Type *)NRF_UARTE20_BASE)
#define NRF_UARTE21   ((NRF_UARTE_Type *)NRF_UARTE21_BASE)
#define NRF_PWM20     ((NRF_PWM_Type *)NRF_PWM20_BASE)
#define NRF_PWM21     ((NRF_PWM_Type *)NRF_PWM21_BASE)
#define NRF_SAADC     ((NRF_SAADC_Type *)NRF_SAADC_BASE)
#define NRF_OSCILLATORS ((NRF_OSCILLATORS_Type *)NRF_OSCILLATORS_BASE)

// ============================================================================
// Compatibility aliases for pre-shifted field values expected by core code
// ============================================================================

#ifdef GPIO_PIN_CNF_INPUT_Connect
#undef GPIO_PIN_CNF_INPUT_Connect
#endif
#define GPIO_PIN_CNF_INPUT_Connect      (0UL << GPIO_PIN_CNF_INPUT_Pos)

#ifdef GPIO_PIN_CNF_INPUT_Disconnect
#undef GPIO_PIN_CNF_INPUT_Disconnect
#endif
#define GPIO_PIN_CNF_INPUT_Disconnect   (1UL << GPIO_PIN_CNF_INPUT_Pos)

#ifdef GPIO_PIN_CNF_PULL_Disabled
#undef GPIO_PIN_CNF_PULL_Disabled
#endif
#define GPIO_PIN_CNF_PULL_Disabled      (0UL << GPIO_PIN_CNF_PULL_Pos)

#ifdef GPIO_PIN_CNF_PULL_Pulldown
#undef GPIO_PIN_CNF_PULL_Pulldown
#endif
#define GPIO_PIN_CNF_PULL_Pulldown      (1UL << GPIO_PIN_CNF_PULL_Pos)

#ifdef GPIO_PIN_CNF_PULL_Pullup
#undef GPIO_PIN_CNF_PULL_Pullup
#endif
#define GPIO_PIN_CNF_PULL_Pullup        (3UL << GPIO_PIN_CNF_PULL_Pos)

#define GPIO_PIN_CNF_DRIVE_S0S1         ((0UL << GPIO_PIN_CNF_DRIVE0_Pos) | (0UL << GPIO_PIN_CNF_DRIVE1_Pos))

#ifdef GPIO_PIN_CNF_SENSE_Disabled
#undef GPIO_PIN_CNF_SENSE_Disabled
#endif
#define GPIO_PIN_CNF_SENSE_Disabled     (0UL << GPIO_PIN_CNF_SENSE_Pos)

#ifdef SPIM_CONFIG_CPHA_Leading
#undef SPIM_CONFIG_CPHA_Leading
#endif
#define SPIM_CONFIG_CPHA_Leading        (0UL << SPIM_CONFIG_CPHA_Pos)

#ifdef SPIM_CONFIG_CPHA_Trailing
#undef SPIM_CONFIG_CPHA_Trailing
#endif
#define SPIM_CONFIG_CPHA_Trailing       (1UL << SPIM_CONFIG_CPHA_Pos)

#ifdef SPIM_CONFIG_CPOL_ActiveHigh
#undef SPIM_CONFIG_CPOL_ActiveHigh
#endif
#define SPIM_CONFIG_CPOL_ActiveHigh     (0UL << SPIM_CONFIG_CPOL_Pos)

#ifdef SPIM_CONFIG_CPOL_ActiveLow
#undef SPIM_CONFIG_CPOL_ActiveLow
#endif
#define SPIM_CONFIG_CPOL_ActiveLow      (1UL << SPIM_CONFIG_CPOL_Pos)

#ifdef SPIM_CONFIG_ORDER_MsbFirst
#undef SPIM_CONFIG_ORDER_MsbFirst
#endif
#define SPIM_CONFIG_ORDER_MsbFirst      (0UL << SPIM_CONFIG_ORDER_Pos)

#ifdef SPIM_CONFIG_ORDER_LsbFirst
#undef SPIM_CONFIG_ORDER_LsbFirst
#endif
#define SPIM_CONFIG_ORDER_LsbFirst      (1UL << SPIM_CONFIG_ORDER_Pos)

// SPIM on nRF54L15 uses a divisor-based PRESCALER register.
// These aliases preserve the existing core API names.
#define SPIM_FREQUENCY_M8               (2UL)
#define SPIM_FREQUENCY_M4               (4UL)
#define SPIM_FREQUENCY_M2               (8UL)
#define SPIM_FREQUENCY_M1               (16UL)
#define SPIM_FREQUENCY_K500             (32UL)
#define SPIM_FREQUENCY_K250             (64UL)
#define SPIM_FREQUENCY_K125             (126UL)

#define TWIM_FREQUENCY_K100             TWIM_FREQUENCY_FREQUENCY_K100
#define TWIM_FREQUENCY_K250             TWIM_FREQUENCY_FREQUENCY_K250
#define TWIM_FREQUENCY_K400             TWIM_FREQUENCY_FREQUENCY_K400

#define UARTE_BAUDRATE_1200             UARTE_BAUDRATE_BAUDRATE_Baud1200
#define UARTE_BAUDRATE_2400             UARTE_BAUDRATE_BAUDRATE_Baud2400
#define UARTE_BAUDRATE_4800             UARTE_BAUDRATE_BAUDRATE_Baud4800
#define UARTE_BAUDRATE_9600             UARTE_BAUDRATE_BAUDRATE_Baud9600
#define UARTE_BAUDRATE_14400            UARTE_BAUDRATE_BAUDRATE_Baud14400
#define UARTE_BAUDRATE_19200            UARTE_BAUDRATE_BAUDRATE_Baud19200
#define UARTE_BAUDRATE_28800            UARTE_BAUDRATE_BAUDRATE_Baud28800
#define UARTE_BAUDRATE_31250            UARTE_BAUDRATE_BAUDRATE_Baud31250
#define UARTE_BAUDRATE_38400            UARTE_BAUDRATE_BAUDRATE_Baud38400
#define UARTE_BAUDRATE_56000            UARTE_BAUDRATE_BAUDRATE_Baud56000
#define UARTE_BAUDRATE_57600            UARTE_BAUDRATE_BAUDRATE_Baud57600
#define UARTE_BAUDRATE_76800            UARTE_BAUDRATE_BAUDRATE_Baud76800
#define UARTE_BAUDRATE_115200           UARTE_BAUDRATE_BAUDRATE_Baud115200
#define UARTE_BAUDRATE_230400           UARTE_BAUDRATE_BAUDRATE_Baud230400
#define UARTE_BAUDRATE_250000           UARTE_BAUDRATE_BAUDRATE_Baud250000
#define UARTE_BAUDRATE_460800           UARTE_BAUDRATE_BAUDRATE_Baud460800
#define UARTE_BAUDRATE_921600           UARTE_BAUDRATE_BAUDRATE_Baud921600
#define UARTE_BAUDRATE_1000000          UARTE_BAUDRATE_BAUDRATE_Baud1M

#define SAADC_RESOLUTION_8BIT           (SAADC_RESOLUTION_VAL_8bit  << SAADC_RESOLUTION_VAL_Pos)
#define SAADC_RESOLUTION_10BIT          (SAADC_RESOLUTION_VAL_10bit << SAADC_RESOLUTION_VAL_Pos)
#define SAADC_RESOLUTION_12BIT          (SAADC_RESOLUTION_VAL_12bit << SAADC_RESOLUTION_VAL_Pos)
#define SAADC_RESOLUTION_14BIT          (SAADC_RESOLUTION_VAL_14bit << SAADC_RESOLUTION_VAL_Pos)

#define SAADC_PSELP_NC                  (SAADC_CH_PSELP_CONNECT_NC << SAADC_CH_PSELP_CONNECT_Pos)
#define SAADC_PSELN_NC                  (SAADC_CH_PSELN_CONNECT_NC << SAADC_CH_PSELN_CONNECT_Pos)

#define SAADC_CH_CONFIG_RESP_Bypass     (0UL)
#define SAADC_CH_CONFIG_GAIN_Gain1_6    (SAADC_CH_CONFIG_GAIN_Gain2_8 << SAADC_CH_CONFIG_GAIN_Pos)
#ifdef SAADC_CH_CONFIG_REFSEL_Internal
#undef SAADC_CH_CONFIG_REFSEL_Internal
#endif
#define SAADC_CH_CONFIG_REFSEL_Internal (0UL << SAADC_CH_CONFIG_REFSEL_Pos)
#define SAADC_CH_CONFIG_TACQ_10us       (79UL << SAADC_CH_CONFIG_TACQ_Pos)
#define SAADC_CH_CONFIG_MODE_SingleEnded (SAADC_CH_CONFIG_MODE_SE << SAADC_CH_CONFIG_MODE_Pos)
#ifdef SAADC_CH_CONFIG_BURST_Disabled
#undef SAADC_CH_CONFIG_BURST_Disabled
#endif
#define SAADC_CH_CONFIG_BURST_Disabled  (0UL << SAADC_CH_CONFIG_BURST_Pos)

#define PWM_MODE_Up                     (PWM_MODE_UPDOWN_Up << PWM_MODE_UPDOWN_Pos)
#define PWM_PRESCALER_DIV64             (PWM_PRESCALER_PRESCALER_DIV_64 << PWM_PRESCALER_PRESCALER_Pos)

#ifdef PWM_DECODER_LOAD_Individual
#undef PWM_DECODER_LOAD_Individual
#endif
#define PWM_DECODER_LOAD_Individual     (0x2UL << PWM_DECODER_LOAD_Pos)

#define PWM_PSEL_NOT_CONNECTED          (PWM_PSEL_OUT_CONNECT_Disconnected << PWM_PSEL_OUT_CONNECT_Pos)

#ifdef __cplusplus
}
#endif

#endif // NRF54L15_H
