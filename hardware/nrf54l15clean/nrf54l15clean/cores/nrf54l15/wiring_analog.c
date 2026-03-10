#include "Arduino.h"

#include <stdint.h>
#include <string.h>

#include <nrf54l15.h>

// SAADC register offsets and fields (single-channel helper for Arduino API).
#define SAADC_TASKS_START          0x000UL
#define SAADC_TASKS_SAMPLE         0x004UL
#define SAADC_TASKS_STOP           0x008UL
#define SAADC_TASKS_CALIBRATE      0x00CUL
#define SAADC_EVENTS_STARTED       0x100UL
#define SAADC_EVENTS_END           0x104UL
#define SAADC_EVENTS_CALDONE       0x110UL
#define SAADC_EVENTS_STOPPED       0x114UL
#define SAADC_ENABLE               0x500UL
#define SAADC_CH_PSELP             0x510UL
#define SAADC_CH_PSELN             0x514UL
#define SAADC_CH_CONFIG            0x518UL
#define SAADC_CH_STRIDE            0x10UL
#define SAADC_RESOLUTION           0x5F0UL
#define SAADC_OVERSAMPLE           0x5F4UL
#define SAADC_SAMPLERATE           0x5F8UL
#define SAADC_RESULT_PTR           0x62CUL
#define SAADC_RESULT_MAXCNT        0x630UL
#define SAADC_RESULT_AMOUNT        0x634UL
#define SAADC_NOISESHAPE           0x654UL

#define SAADC_ENABLE_DISABLED      0UL
#define SAADC_ENABLE_ENABLED       1UL

#define SAADC_OVERSAMPLE_BYPASS     0UL
#define SAADC_NOISESHAPE_DISABLED   0UL

// PWM register offsets for analogWrite().
#define PWM_TASKS_STOP                    0x004UL
#define PWM_TASKS_DMA_SEQ_START           0x010UL
#define PWM_EVENTS_STOPPED                0x104UL
#define PWM_EVENTS_SEQSTARTED0            0x108UL
#define PWM_EVENTS_RAMUNDERFLOW           0x120UL
#define PWM_EVENTS_DMA_SEQ0_END           0x124UL
#define PWM_SHORTS                        0x200UL
#define PWM_ENABLE                        0x500UL
#define PWM_MODE                          0x504UL
#define PWM_COUNTERTOP                    0x508UL
#define PWM_PRESCALER                     0x50CUL
#define PWM_DECODER                       0x510UL
#define PWM_LOOP                          0x514UL
#define PWM_IDLEOUT                       0x518UL
#define PWM_SEQ0_REFRESH                  0x528UL
#define PWM_SEQ0_ENDDELAY                 0x52CUL
#define PWM_SEQ1_REFRESH                  0x530UL
#define PWM_SEQ1_ENDDELAY                 0x534UL
#define PWM_PSEL_OUT0                     0x560UL
#define PWM_PSEL_OUT_STRIDE               0x004UL
#define PWM_DMA_SEQ0_PTR                  0x704UL
#define PWM_DMA_SEQ0_MAXCNT               0x708UL
#define PWM_DMA_SEQ1_PTR                  0x70CUL
#define PWM_DMA_SEQ1_MAXCNT               0x710UL

#define PWM_ENABLE_DISABLED               0UL
#define PWM_ENABLE_ENABLED                1UL
#define PWM_MODE_UP                       0UL
#define PWM_DECODER_LOAD_INDIVIDUAL       2UL
#define PWM_DECODER_MODE_REFRESHCOUNT     0UL
#define PWM_PSEL_DISCONNECTED             0xFFFFFFFFUL

#define PWM_SHORT_DMA_SEQ0_BUSERROR_STOP  (1UL << 6U)
#define PWM_COMPARE_POLARITY_FALLING_EDGE 0x8000U

#define TIMER_TASKS_START                 0x000UL
#define TIMER_TASKS_STOP                  0x004UL
#define TIMER_TASKS_CLEAR                 0x00CUL
#define TIMER_EVENTS_COMPARE0             0x140UL
#define TIMER_EVENTS_COMPARE_STRIDE       0x004UL
#define TIMER_SHORTS                      0x200UL
#define TIMER_MODE                        0x504UL
#define TIMER_BITMODE                     0x508UL
#define TIMER_PRESCALER                   0x510UL
#define TIMER_CC0                         0x540UL
#define TIMER_CC_STRIDE                   0x004UL
#define TIMER_PUBLISH_COMPARE0            0x1C0UL
#define TIMER_PUBLISH_COMPARE_STRIDE      0x004UL

#define TIMER_MODE_TIMER                  0UL
#define TIMER_BITMODE_32                  3UL
#define TIMER_SHORT_COMPARE0_CLEAR        (1UL << 0U)

#define GPIOTE_TASKS_SET0                 0x030UL
#define GPIOTE_TASKS_CLR0                 0x060UL
#define GPIOTE_SUBSCRIBE_SET0             0x0B0UL
#define GPIOTE_SUBSCRIBE_CLR0             0x0E0UL
#define GPIOTE_CONFIG0                    0x510UL
#define GPIOTE_CONFIG_STRIDE              0x004UL

#define GPIOTE_CONFIG_MODE_TASK           3UL
#define GPIOTE_CONFIG_POLARITY_NONE       0UL

#define DPPIC_CHENSET                     0x504UL
#define DPPIC_CHENCLR                     0x508UL

#define ANALOG_PWM_CHANNELS_PER_INSTANCE  4U
#define ANALOG_PWM_INSTANCES              2U
#define ANALOG_PWM_PIN_COUNT              10U
#define ANALOG_TIMER_PWM_SLOT_COUNT       5U
#define ANALOG_PWM_INSTANCE_NONE          0xFFU
#define ANALOG_PWM_NO_CHANNEL             0xFFU
#define ANALOG_PWM_NO_PIN                 0xFFU
#define ANALOG_PWM_NO_SLOT                0xFFU
#define ANALOG_PWM_DEFAULT_HZ             1000UL
#define ANALOG_SOFT_PWM_DEFAULT_PERIOD_US 1000UL

extern uint8_t nrf54l15_gpiote20_acquire_task_channel(uint8_t* channel);
extern void nrf54l15_gpiote20_release_task_channel(uint8_t channel);

static uint8_t pwm_instance_any_dynamic_channel(uint8_t instance);
static void pwm_apply_outputs(uint8_t instance);
static void pwm_stop_instance(uint8_t instance);

static inline volatile uint32_t* regptr(uintptr_t base, uintptr_t off)
{
    return (volatile uint32_t*)(base + off);
}

static inline uint32_t make_psel(uint8_t port, uint8_t pin)
{
    return ((uint32_t)(pin & 0x1FU) << SAADC_CH_PSELP_PIN_Pos) |
           ((uint32_t)(port & 0x7U) << SAADC_CH_PSELP_PORT_Pos) |
           ((uint32_t)SAADC_CH_PSELP_CONNECT_AnalogInput << SAADC_CH_PSELP_CONNECT_Pos);
}

typedef struct {
    uint8_t port;
    uint8_t pin;
    int8_t ain;
} adc_pin_desc_t;

typedef struct {
    uint8_t arduino_pin;
    uint8_t port;
    uint8_t pin;
    uint8_t pwm_instance;
    uint8_t pwm_channel;
} pwm_pin_desc_t;

static adc_pin_desc_t resolve_analog(uint8_t apin)
{
    switch (apin) {
        case PIN_A0: return (adc_pin_desc_t){1U, 4U, 0};
        case PIN_A1: return (adc_pin_desc_t){1U, 5U, 1};
        case PIN_A2: return (adc_pin_desc_t){1U, 6U, 2};
        case PIN_A3: return (adc_pin_desc_t){1U, 7U, 3};
        case PIN_A4: return (adc_pin_desc_t){1U, 10U, -1};
        case PIN_A5: return (adc_pin_desc_t){1U, 11U, 4};
        case PIN_A6: return (adc_pin_desc_t){1U, 13U, 6};
        case PIN_A7: return (adc_pin_desc_t){1U, 14U, 7};
        default: return (adc_pin_desc_t){0U, 0U, -1};
    }
}

// Keep Arduino compatibility default (0..1023) unless sketch overrides.
static uint8_t g_analog_read_resolution = 10U;
static uint8_t g_analog_write_resolution = 8U;
// Written by SAADC EasyDMA, so this must be volatile.
static volatile int16_t g_saadc_sample = 0;

static uint8_t g_pwm_initialized[ANALOG_PWM_INSTANCES] = {0U, 0U};
static uint8_t g_pwm_running[ANALOG_PWM_INSTANCES] = {0U, 0U};
static uint8_t g_pwm_pin_used[ANALOG_PWM_PIN_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static uint8_t g_pwm_pin_software[ANALOG_PWM_PIN_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static uint8_t g_pwm_pin_channel[ANALOG_PWM_PIN_COUNT] = {
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL
};
static uint8_t g_pwm_channel_owner[ANALOG_PWM_CHANNELS_PER_INSTANCE] = {
    ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN
};
static uint16_t g_pwm_pin_pulse[ANALOG_PWM_PIN_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static uint16_t g_pwm_sequence[ANALOG_PWM_INSTANCES][ANALOG_PWM_CHANNELS_PER_INSTANCE]
    __attribute__((aligned(4))) = {
    {0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U}
};
static uint32_t g_soft_pwm_on_time_us[ANALOG_PWM_PIN_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static uint8_t g_soft_pwm_output_high[ANALOG_PWM_PIN_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static uint8_t g_pwm_pin_timer_slot[ANALOG_PWM_PIN_COUNT] = {
    ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT,
    ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT,
    ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT,
    ANALOG_PWM_NO_SLOT
};
static uint8_t g_timer_pwm_slot_owner[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN,
    ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN
};
static uint8_t g_timer_pwm_slot_gpiote_channel[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL
};
static uint32_t g_pwm_pin_frequency_hz[ANALOG_PWM_PIN_COUNT] = {
    0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL
};
static uint16_t g_pwm_countertop = 16000U;
static uint8_t g_pwm_prescaler = 0U;
static uint32_t g_analog_write_frequency_hz = ANALOG_PWM_DEFAULT_HZ;
static uint32_t g_soft_pwm_period_us = ANALOG_SOFT_PWM_DEFAULT_PERIOD_US;

static const uintptr_t k_pwm_base[ANALOG_PWM_INSTANCES] = {
    (uintptr_t)NRF_PWM20,
    (uintptr_t)NRF_PWM21
};
#ifdef NRF_TRUSTZONE_NONSECURE
static const uintptr_t k_timer_pwm_base[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    0x400CA000UL, 0x400CB000UL, 0x400CC000UL, 0x400CD000UL, 0x400CE000UL
};
static const uintptr_t k_timer_pwm_gpiote_base = 0x400DA000UL;
static const uintptr_t k_timer_pwm_dppic_base = 0x400C2000UL;
#else
static const uintptr_t k_timer_pwm_base[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    0x500CA000UL, 0x500CB000UL, 0x500CC000UL, 0x500CD000UL, 0x500CE000UL
};
static const uintptr_t k_timer_pwm_gpiote_base = 0x500DA000UL;
static const uintptr_t k_timer_pwm_dppic_base = 0x500C2000UL;
#endif
static const pwm_pin_desc_t k_pwm_pin_desc[ANALOG_PWM_PIN_COUNT] = {
    {PIN_D0, 1U, 4U, 0U, ANALOG_PWM_NO_CHANNEL},
    {PIN_D1, 1U, 5U, 0U, ANALOG_PWM_NO_CHANNEL},
    {PIN_D2, 1U, 6U, 0U, ANALOG_PWM_NO_CHANNEL},
    {PIN_D3, 1U, 7U, 0U, ANALOG_PWM_NO_CHANNEL},
    {PIN_D4, 1U, 10U, 0U, ANALOG_PWM_NO_CHANNEL},
    {PIN_D5, 1U, 11U, 0U, ANALOG_PWM_NO_CHANNEL},
    {PIN_D6, 2U, 8U, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL},
    {PIN_D7, 2U, 7U, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL},
    {PIN_D8, 2U, 1U, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL},
    {PIN_D9, 2U, 4U, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL}
};

static NRF_GPIO_Type* gpio_for_port(uint8_t port)
{
    switch (port) {
        case 0U: return NRF_P0;
        case 1U: return NRF_P1;
        case 2U: return NRF_P2;
        default: return (NRF_GPIO_Type*)0;
    }
}

static void gpio_prepare_analog_input(uint8_t port, uint8_t pin)
{
    NRF_GPIO_Type* gpio = gpio_for_port(port);
    if (gpio == 0 || pin > 31U) {
        return;
    }

    const uint32_t bit = (1UL << pin);
    uint32_t cnf = gpio->PIN_CNF[pin];
    cnf &= ~(GPIO_PIN_CNF_DIR_Msk |
             GPIO_PIN_CNF_INPUT_Msk |
             GPIO_PIN_CNF_PULL_Msk |
             GPIO_PIN_CNF_SENSE_Msk);
    cnf |= (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos);
    // SAADC owns the input path while sampling; disconnect digital path to avoid leakage/pulls.
    cnf |= GPIO_PIN_CNF_INPUT_Disconnect;
    cnf |= GPIO_PIN_CNF_PULL_Disabled;
    cnf |= GPIO_PIN_CNF_SENSE_Disabled;
    gpio->DIRCLR = bit;
    gpio->PIN_CNF[pin] = cnf;
}

static void gpio_write_raw(uint8_t port, uint8_t pin, uint8_t high)
{
    NRF_GPIO_Type* gpio = gpio_for_port(port);
    if (gpio == 0 || pin > 31U) {
        return;
    }

    const uint32_t bit = (1UL << pin);
    gpio->DIRSET = bit;
    if (high != 0U) {
        gpio->OUTSET = bit;
    } else {
        gpio->OUTCLR = bit;
    }
}

static uint32_t soft_pwm_period_us_for_frequency(uint32_t hz);

static uint32_t dppi_config_value(uint8_t channel)
{
    return ((uint32_t)channel & 0xFFUL) | (1UL << 31U);
}

static uint8_t pwm_pin_supports_timer_output(uint8_t index)
{
    return (index < 6U) ? 1U : 0U;
}

static uint8_t pwm_pin_requests_custom_frequency(uint8_t index)
{
    if (index >= ANALOG_PWM_PIN_COUNT) {
        return 0U;
    }
    return (g_pwm_pin_frequency_hz[index] != 0UL) ? 1U : 0U;
}

static uint32_t pwm_pin_effective_frequency_hz(uint8_t index)
{
    if (index >= ANALOG_PWM_PIN_COUNT) {
        return g_analog_write_frequency_hz;
    }
    if (g_pwm_pin_frequency_hz[index] != 0UL) {
        return g_pwm_pin_frequency_hz[index];
    }
    return g_analog_write_frequency_hz;
}

static uint32_t soft_pwm_period_us_for_pin(uint8_t index)
{
    if (index >= ANALOG_PWM_PIN_COUNT || g_pwm_pin_frequency_hz[index] == 0UL) {
        return g_soft_pwm_period_us;
    }
    return soft_pwm_period_us_for_frequency(g_pwm_pin_frequency_hz[index]);
}

static void soft_pwm_update_channel(uint8_t index)
{
    if (index >= ANALOG_PWM_PIN_COUNT || g_pwm_countertop == 0U) {
        return;
    }

    const uint32_t soft_period_us = soft_pwm_period_us_for_pin(index);
    uint32_t on_time_us =
        ((uint32_t)g_pwm_pin_pulse[index] * soft_period_us + (g_pwm_countertop / 2U)) /
        (uint32_t)g_pwm_countertop;

    if (on_time_us == 0UL) {
        on_time_us = 1UL;
    } else if (on_time_us >= soft_period_us) {
        on_time_us = soft_period_us - 1UL;
    }

    g_soft_pwm_on_time_us[index] = on_time_us;
}

static uint8_t timer_pwm_compute_timing(uint32_t target_hz,
                                        uint8_t* prescaler,
                                        uint32_t* period_ticks)
{
    if (prescaler == 0 || period_ticks == 0 || target_hz == 0UL) {
        return 0U;
    }

    uint32_t best_error = 0xFFFFFFFFUL;
    uint8_t best_prescaler = 0U;
    uint32_t best_period_ticks = 0U;
    uint8_t found = 0U;

    for (uint8_t p = 0U; p <= 9U; ++p) {
        const uint32_t timer_clk = 16000000UL >> p;
        if (timer_clk == 0UL) {
            continue;
        }

        uint32_t ticks = (timer_clk + (target_hz / 2UL)) / target_hz;
        if (ticks < 2UL) {
            ticks = 2UL;
        }

        const uint32_t actual_hz = timer_clk / ticks;
        const uint32_t error =
            (actual_hz >= target_hz) ? (actual_hz - target_hz) : (target_hz - actual_hz);

        if (!found || error < best_error ||
            (error == best_error && p < best_prescaler)) {
            found = 1U;
            best_error = error;
            best_prescaler = p;
            best_period_ticks = ticks;
            if (error == 0UL) {
                break;
            }
        }
    }

    if (!found || best_period_ticks < 2UL) {
        return 0U;
    }

    *prescaler = best_prescaler;
    *period_ticks = best_period_ticks;
    return 1U;
}

static uint8_t timer_pwm_ensure_slot(uint8_t index, uint8_t* slot_out)
{
    if (slot_out == 0 || pwm_pin_supports_timer_output(index) == 0U) {
        return 0U;
    }

    if (g_pwm_pin_timer_slot[index] != ANALOG_PWM_NO_SLOT) {
        *slot_out = g_pwm_pin_timer_slot[index];
        return 1U;
    }

    for (uint8_t slot = 0U; slot < ANALOG_TIMER_PWM_SLOT_COUNT; ++slot) {
        if (g_timer_pwm_slot_owner[slot] != ANALOG_PWM_NO_PIN) {
            continue;
        }

        uint8_t gpiote_channel = ANALOG_PWM_NO_CHANNEL;
        if (nrf54l15_gpiote20_acquire_task_channel(&gpiote_channel) == 0U) {
            continue;
        }

        g_timer_pwm_slot_owner[slot] = index;
        g_timer_pwm_slot_gpiote_channel[slot] = gpiote_channel;
        g_pwm_pin_timer_slot[index] = slot;
        *slot_out = slot;
        return 1U;
    }

    return 0U;
}

static void timer_pwm_release_pin(uint8_t index)
{
    if (index >= ANALOG_PWM_PIN_COUNT) {
        return;
    }

    const uint8_t slot = g_pwm_pin_timer_slot[index];
    if (slot == ANALOG_PWM_NO_SLOT || slot >= ANALOG_TIMER_PWM_SLOT_COUNT) {
        return;
    }

    const uint8_t set_dppi_channel = (uint8_t)(slot * 2U);
    const uint8_t clr_dppi_channel = (uint8_t)(set_dppi_channel + 1U);
    const uintptr_t timer_base = k_timer_pwm_base[slot];
    const uint8_t gpiote_channel = g_timer_pwm_slot_gpiote_channel[slot];

    *regptr(timer_base, TIMER_TASKS_STOP) = 1U;
    *regptr(timer_base, TIMER_SHORTS) = 0U;
    *regptr(timer_base, TIMER_PUBLISH_COMPARE0) = 0U;
    *regptr(timer_base, TIMER_PUBLISH_COMPARE0 + TIMER_PUBLISH_COMPARE_STRIDE) = 0U;
    *regptr(k_timer_pwm_dppic_base, DPPIC_CHENCLR) =
        (1UL << set_dppi_channel) | (1UL << clr_dppi_channel);

    if (gpiote_channel != ANALOG_PWM_NO_CHANNEL) {
        *regptr(k_timer_pwm_gpiote_base,
                GPIOTE_SUBSCRIBE_SET0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE)) = 0U;
        *regptr(k_timer_pwm_gpiote_base,
                GPIOTE_SUBSCRIBE_CLR0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE)) = 0U;
        *regptr(k_timer_pwm_gpiote_base,
                GPIOTE_CONFIG0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE)) = 0U;
        nrf54l15_gpiote20_release_task_channel(gpiote_channel);
    }

    g_timer_pwm_slot_gpiote_channel[slot] = ANALOG_PWM_NO_CHANNEL;
    g_timer_pwm_slot_owner[slot] = ANALOG_PWM_NO_PIN;
    g_pwm_pin_timer_slot[index] = ANALOG_PWM_NO_SLOT;
}

static uint8_t timer_pwm_apply_pin(uint8_t index)
{
    if (index >= ANALOG_PWM_PIN_COUNT ||
        pwm_pin_supports_timer_output(index) == 0U ||
        pwm_pin_requests_custom_frequency(index) == 0U) {
        return 0U;
    }

    const uint32_t pulse = g_pwm_pin_pulse[index];
    if (pulse == 0UL || pulse >= (uint32_t)g_pwm_countertop) {
        return 0U;
    }

    uint8_t slot = ANALOG_PWM_NO_SLOT;
    if (timer_pwm_ensure_slot(index, &slot) == 0U) {
        return 0U;
    }

    const uint8_t gpiote_channel = g_timer_pwm_slot_gpiote_channel[slot];
    if (gpiote_channel == ANALOG_PWM_NO_CHANNEL) {
        timer_pwm_release_pin(index);
        return 0U;
    }

    uint8_t prescaler = 0U;
    uint32_t period_ticks = 0U;
    if (timer_pwm_compute_timing(pwm_pin_effective_frequency_hz(index),
                                 &prescaler,
                                 &period_ticks) == 0U) {
        timer_pwm_release_pin(index);
        return 0U;
    }

    uint32_t high_ticks =
        (pulse * period_ticks + (uint32_t)(g_pwm_countertop / 2U)) / (uint32_t)g_pwm_countertop;
    if (high_ticks == 0UL) {
        high_ticks = 1UL;
    } else if (high_ticks >= period_ticks) {
        high_ticks = period_ticks - 1UL;
    }

    const uint8_t set_dppi_channel = (uint8_t)(slot * 2U);
    const uint8_t clr_dppi_channel = (uint8_t)(set_dppi_channel + 1U);
    const uintptr_t timer_base = k_timer_pwm_base[slot];
    const uintptr_t gpiote_cfg =
        GPIOTE_CONFIG0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE);
    const uintptr_t gpiote_set =
        GPIOTE_TASKS_SET0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE);
    const uintptr_t gpiote_sub_set =
        GPIOTE_SUBSCRIBE_SET0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE);
    const uintptr_t gpiote_sub_clr =
        GPIOTE_SUBSCRIBE_CLR0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE);

    *regptr(timer_base, TIMER_TASKS_STOP) = 1U;
    *regptr(k_timer_pwm_dppic_base, DPPIC_CHENCLR) =
        (1UL << set_dppi_channel) | (1UL << clr_dppi_channel);
    *regptr(timer_base, TIMER_PUBLISH_COMPARE0) = 0U;
    *regptr(timer_base, TIMER_PUBLISH_COMPARE0 + TIMER_PUBLISH_COMPARE_STRIDE) = 0U;
    *regptr(k_timer_pwm_gpiote_base, gpiote_sub_set) = 0U;
    *regptr(k_timer_pwm_gpiote_base, gpiote_sub_clr) = 0U;

    gpio_write_raw(k_pwm_pin_desc[index].port, k_pwm_pin_desc[index].pin, 0U);

    uint32_t gpiote_config = 0U;
    gpiote_config |= (GPIOTE_CONFIG_MODE_TASK << GPIOTE_CONFIG_MODE_Pos);
    gpiote_config |= ((uint32_t)(k_pwm_pin_desc[index].pin & 0x1FU) << GPIOTE_CONFIG_PSEL_Pos);
    gpiote_config |= ((uint32_t)(k_pwm_pin_desc[index].port & 0x7U) << GPIOTE_CONFIG_PORT_Pos);
    gpiote_config |= (GPIOTE_CONFIG_POLARITY_NONE << GPIOTE_CONFIG_POLARITY_Pos);
    gpiote_config |= (0UL << GPIOTE_CONFIG_OUTINIT_Pos);
    *regptr(k_timer_pwm_gpiote_base, gpiote_cfg) = gpiote_config;

    *regptr(timer_base, TIMER_TASKS_CLEAR) = 1U;
    *regptr(timer_base, TIMER_MODE) = TIMER_MODE_TIMER;
    *regptr(timer_base, TIMER_BITMODE) = TIMER_BITMODE_32;
    *regptr(timer_base, TIMER_PRESCALER) = (uint32_t)prescaler;
    *regptr(timer_base, TIMER_SHORTS) = TIMER_SHORT_COMPARE0_CLEAR;
    *regptr(timer_base, TIMER_CC0) = period_ticks;
    *regptr(timer_base, TIMER_CC0 + TIMER_CC_STRIDE) = high_ticks;
    *regptr(timer_base, TIMER_EVENTS_COMPARE0) = 0U;
    *regptr(timer_base, TIMER_EVENTS_COMPARE0 + TIMER_EVENTS_COMPARE_STRIDE) = 0U;

    *regptr(timer_base, TIMER_PUBLISH_COMPARE0) = dppi_config_value(set_dppi_channel);
    *regptr(timer_base, TIMER_PUBLISH_COMPARE0 + TIMER_PUBLISH_COMPARE_STRIDE) =
        dppi_config_value(clr_dppi_channel);
    *regptr(k_timer_pwm_gpiote_base, gpiote_sub_set) = dppi_config_value(set_dppi_channel);
    *regptr(k_timer_pwm_gpiote_base, gpiote_sub_clr) = dppi_config_value(clr_dppi_channel);
    *regptr(k_timer_pwm_dppic_base, DPPIC_CHENSET) =
        (1UL << set_dppi_channel) | (1UL << clr_dppi_channel);

    *regptr(k_timer_pwm_gpiote_base, gpiote_set) = 1U;
    *regptr(timer_base, TIMER_TASKS_START) = 1U;

    g_pwm_pin_software[index] = 0U;
    g_soft_pwm_on_time_us[index] = 0UL;
    g_soft_pwm_output_high[index] = 1U;
    return 1U;
}

static inline uint32_t make_gpio_psel(uint8_t port, uint8_t pin)
{
    return ((uint32_t)(pin & 0x1FU) << 0U) |
           ((uint32_t)(port & 0x7U) << 5U);
}

static uint8_t pwm_pin_index_for_pin(uint8_t pin, uint8_t* index)
{
    if (index == 0) {
        return 0U;
    }
    for (uint8_t i = 0U; i < ANALOG_PWM_PIN_COUNT; ++i) {
        if (k_pwm_pin_desc[i].arduino_pin == pin) {
            *index = i;
            return 1U;
        }
    }
    return 0U;
}

static uint8_t pwm_pin_can_use_hardware(uint8_t index)
{
    if (index >= ANALOG_PWM_PIN_COUNT) {
        return 0U;
    }
    return (k_pwm_pin_desc[index].pwm_instance == 0U) ? 1U : 0U;
}

static uint8_t pwm_pin_uses_software(uint8_t index)
{
    if (index >= ANALOG_PWM_PIN_COUNT) {
        return 0U;
    }
    return g_pwm_pin_software[index];
}

static uint8_t pwm_acquire_channel(uint8_t index, uint8_t* channel)
{
    if (channel == 0 || pwm_pin_can_use_hardware(index) == 0U) {
        return 0U;
    }

    if (g_pwm_pin_channel[index] != ANALOG_PWM_NO_CHANNEL) {
        *channel = g_pwm_pin_channel[index];
        return 1U;
    }

    for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS_PER_INSTANCE; ++ch) {
        if (g_pwm_channel_owner[ch] == ANALOG_PWM_NO_PIN) {
            g_pwm_channel_owner[ch] = index;
            g_pwm_pin_channel[index] = ch;
            *channel = ch;
            return 1U;
        }
    }
    return 0U;
}

static void pwm_release_channel(uint8_t index)
{
    if (index >= ANALOG_PWM_PIN_COUNT) {
        return;
    }

    const uint8_t channel = g_pwm_pin_channel[index];
    if (channel == ANALOG_PWM_NO_CHANNEL) {
        return;
    }

    g_pwm_pin_channel[index] = ANALOG_PWM_NO_CHANNEL;
    if (channel < ANALOG_PWM_CHANNELS_PER_INSTANCE &&
        g_pwm_channel_owner[channel] == index) {
        g_pwm_channel_owner[channel] = ANALOG_PWM_NO_PIN;
        g_pwm_sequence[0][channel] = 0U;
        if (g_pwm_initialized[0U] != 0U) {
            *regptr(k_pwm_base[0U], PWM_PSEL_OUT0 + ((uintptr_t)channel * PWM_PSEL_OUT_STRIDE)) =
                PWM_PSEL_DISCONNECTED;
        }
    }
}

static void pwm_release_shared_output(uint8_t index)
{
    if (index >= ANALOG_PWM_PIN_COUNT) {
        return;
    }

    if (pwm_pin_can_use_hardware(index) == 0U) {
        return;
    }

    const uint8_t instance = k_pwm_pin_desc[index].pwm_instance;
    pwm_release_channel(index);
    if (pwm_instance_any_dynamic_channel(instance) == 0U) {
        pwm_stop_instance(instance);
    } else {
        pwm_apply_outputs(instance);
    }
}

static void soft_pwm_drive_channel(uint8_t index, uint8_t high)
{
    if (index >= ANALOG_PWM_PIN_COUNT) {
        return;
    }
    if (g_soft_pwm_output_high[index] == high) {
        return;
    }
    g_soft_pwm_output_high[index] = high;
    gpio_write_raw(k_pwm_pin_desc[index].port, k_pwm_pin_desc[index].pin, high);
}

void nrf54l15_analog_write_idle_service(void)
{
    uint8_t any_soft_channel = 0U;
    for (uint8_t i = 0U; i < ANALOG_PWM_PIN_COUNT; ++i) {
        if (g_pwm_pin_used[i] != 0U && pwm_pin_uses_software(i) != 0U) {
            any_soft_channel = 1U;
            break;
        }
    }
    if (any_soft_channel == 0U) {
        return;
    }

    const uint32_t now_us = micros();
    for (uint8_t i = 0U; i < ANALOG_PWM_PIN_COUNT; ++i) {
        if (g_pwm_pin_used[i] == 0U || pwm_pin_uses_software(i) == 0U) {
            continue;
        }

        const uint32_t soft_period_us = soft_pwm_period_us_for_pin(i);
        const uint32_t phase_us = now_us % soft_period_us;
        uint8_t high = 0U;
        if (g_pwm_pin_pulse[i] >= g_pwm_countertop) {
            high = 1U;
        } else if (g_pwm_pin_pulse[i] != 0U &&
                   phase_us < g_soft_pwm_on_time_us[i]) {
            high = 1U;
        }
        soft_pwm_drive_channel(i, high);
    }
}

static uint8_t pwm_compute_timing(uint32_t target_hz, uint8_t* prescaler, uint16_t* countertop)
{
    if (prescaler == 0 || countertop == 0 || target_hz == 0UL) {
        return 0U;
    }

    uint32_t best_error = 0xFFFFFFFFUL;
    uint8_t best_prescaler = 0U;
    uint16_t best_countertop = 0U;
    uint8_t found = 0U;

    for (uint8_t p = 0U; p <= 7U; ++p) {
        const uint32_t pwm_clk = 16000000UL >> p;
        if (pwm_clk == 0UL) {
            continue;
        }

        uint32_t top = (pwm_clk + (target_hz / 2UL)) / target_hz;
        if (top < 3UL) {
            top = 3UL;
        }
        if (top > 32767UL) {
            continue;
        }

        const uint32_t actual_hz = pwm_clk / top;
        const uint32_t error = (actual_hz >= target_hz) ? (actual_hz - target_hz) : (target_hz - actual_hz);
        if (!found || error < best_error) {
            found = 1U;
            best_error = error;
            best_prescaler = p;
            best_countertop = (uint16_t)top;
            if (error == 0UL) {
                break;
            }
        }
    }

    if (!found) {
        return 0U;
    }

    *prescaler = best_prescaler;
    *countertop = best_countertop;
    return 1U;
}

static uint8_t pwm_instance_any_dynamic_channel(uint8_t instance)
{
    if (instance >= ANALOG_PWM_INSTANCES) {
        return 0U;
    }
    for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS_PER_INSTANCE; ++ch) {
        const uint8_t owner = g_pwm_channel_owner[ch];
        if (owner == ANALOG_PWM_NO_PIN || owner >= ANALOG_PWM_PIN_COUNT) {
            continue;
        }
        if (g_pwm_pin_pulse[owner] != 0U && g_pwm_pin_pulse[owner] != g_pwm_countertop) {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t pwm_instance_any_used_channel(uint8_t instance)
{
    if (instance >= ANALOG_PWM_INSTANCES) {
        return 0U;
    }
    for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS_PER_INSTANCE; ++ch) {
        const uint8_t owner = g_pwm_channel_owner[ch];
        if (owner != ANALOG_PWM_NO_PIN) {
            return 1U;
        }
    }
    return 0U;
}

static void pwm_disconnect_all(uint8_t instance)
{
    if (instance >= ANALOG_PWM_INSTANCES) {
        return;
    }
    const uintptr_t base = k_pwm_base[instance];
    for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS_PER_INSTANCE; ++ch) {
        *regptr(base, PWM_PSEL_OUT0 + ((uintptr_t)ch * PWM_PSEL_OUT_STRIDE)) = PWM_PSEL_DISCONNECTED;
    }
}

static void pwm_apply_outputs(uint8_t instance)
{
    if (instance >= ANALOG_PWM_INSTANCES) {
        return;
    }

    const uintptr_t base = k_pwm_base[instance];
    pwm_disconnect_all(instance);

    for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS_PER_INSTANCE; ++ch) {
        const uint8_t owner = g_pwm_channel_owner[ch];
        if (owner == ANALOG_PWM_NO_PIN || owner >= ANALOG_PWM_PIN_COUNT) {
            continue;
        }
        *regptr(base, PWM_PSEL_OUT0 + ((uintptr_t)ch * PWM_PSEL_OUT_STRIDE)) =
            make_gpio_psel(k_pwm_pin_desc[owner].port, k_pwm_pin_desc[owner].pin);
    }
}

static uint32_t soft_pwm_period_us_for_frequency(uint32_t hz)
{
    if (hz == 0UL) {
        return ANALOG_SOFT_PWM_DEFAULT_PERIOD_US;
    }

    const uint32_t rounded = (1000000UL + (hz / 2UL)) / hz;
    return (rounded == 0UL) ? 1UL : rounded;
}

static void analog_write_recompute_active_outputs(uint16_t old_countertop)
{
    if (old_countertop == 0U) {
        old_countertop = 1U;
    }

    for (uint8_t i = 0U; i < ANALOG_PWM_PIN_COUNT; ++i) {
        if (g_pwm_pin_used[i] == 0U) {
            continue;
        }

        uint32_t pulse = g_pwm_pin_pulse[i];
        pulse = (pulse * (uint32_t)g_pwm_countertop + (old_countertop / 2U)) /
                (uint32_t)old_countertop;
        if (pulse > (uint32_t)g_pwm_countertop) {
            pulse = (uint32_t)g_pwm_countertop;
        }
        g_pwm_pin_pulse[i] = (uint16_t)pulse;

        if (pwm_pin_uses_software(i) != 0U) {
            soft_pwm_update_channel(i);
        }
    }

    for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS_PER_INSTANCE; ++ch) {
        const uint8_t owner = g_pwm_channel_owner[ch];
        if (owner == ANALOG_PWM_NO_PIN || owner >= ANALOG_PWM_PIN_COUNT) {
            continue;
        }
        g_pwm_sequence[0U][ch] =
            (uint16_t)(PWM_COMPARE_POLARITY_FALLING_EDGE | (g_pwm_pin_pulse[owner] & 0x7FFFU));
    }
}

static uint8_t pwm_init_once(uint8_t instance)
{
    if (instance >= ANALOG_PWM_INSTANCES) {
        return 0U;
    }
    if (g_pwm_initialized[instance] != 0U) {
        return 1U;
    }

    if (!pwm_compute_timing(g_analog_write_frequency_hz, &g_pwm_prescaler, &g_pwm_countertop)) {
        return 0U;
    }

    const uintptr_t base = k_pwm_base[instance];

    *regptr(base, PWM_ENABLE) = PWM_ENABLE_DISABLED;
    *regptr(base, PWM_SHORTS) = 0U;
    *regptr(base, PWM_MODE) = PWM_MODE_UP;
    *regptr(base, PWM_COUNTERTOP) = (uint32_t)g_pwm_countertop;
    *regptr(base, PWM_PRESCALER) = (uint32_t)g_pwm_prescaler;
    *regptr(base, PWM_DECODER) =
        (PWM_DECODER_LOAD_INDIVIDUAL << 0U) |
        (PWM_DECODER_MODE_REFRESHCOUNT << 8U);
    // A single SEQ[0] START loads a new duty set, and the last loaded duty
    // remains active until the next START or STOP.
    *regptr(base, PWM_LOOP) = 0U;
    *regptr(base, PWM_IDLEOUT) = 0U;
    *regptr(base, PWM_SEQ0_REFRESH) = 0U;
    *regptr(base, PWM_SEQ0_ENDDELAY) = 0U;
    *regptr(base, PWM_SEQ1_REFRESH) = 0U;
    *regptr(base, PWM_SEQ1_ENDDELAY) = 0U;
    pwm_disconnect_all(instance);

    *regptr(base, PWM_DMA_SEQ0_PTR) = (uint32_t)(uintptr_t)&g_pwm_sequence[instance][0];
    *regptr(base, PWM_DMA_SEQ0_MAXCNT) = (uint32_t)sizeof(g_pwm_sequence[instance]);
    *regptr(base, PWM_DMA_SEQ1_PTR) = 0U;
    *regptr(base, PWM_DMA_SEQ1_MAXCNT) = 0U;

    *regptr(base, PWM_ENABLE) = PWM_ENABLE_ENABLED;
    g_pwm_initialized[instance] = 1U;
    return 1U;
}

static void pwm_start_if_needed(uint8_t instance)
{
    if (instance >= ANALOG_PWM_INSTANCES) {
        return;
    }
    if (!pwm_init_once(instance)) {
        return;
    }

    const uintptr_t base = k_pwm_base[instance];

    pwm_apply_outputs(instance);

    *regptr(base, PWM_EVENTS_SEQSTARTED0) = 0U;
    *regptr(base, PWM_EVENTS_RAMUNDERFLOW) = 0U;
    *regptr(base, PWM_EVENTS_DMA_SEQ0_END) = 0U;
    *regptr(base, PWM_SHORTS) = PWM_SHORT_DMA_SEQ0_BUSERROR_STOP;
    *regptr(base, PWM_TASKS_DMA_SEQ_START) = 1U;

    for (uint32_t guard = 0UL; guard < 200000UL; ++guard) {
        if (*regptr(base, PWM_EVENTS_SEQSTARTED0) != 0U) {
            break;
        }
    }
    *regptr(base, PWM_EVENTS_SEQSTARTED0) = 0U;
    g_pwm_running[instance] = 1U;
}

static void pwm_stop_instance(uint8_t instance)
{
    if (instance >= ANALOG_PWM_INSTANCES || g_pwm_initialized[instance] == 0U) {
        return;
    }

    const uintptr_t base = k_pwm_base[instance];

    if (g_pwm_running[instance] != 0U) {
        *regptr(base, PWM_EVENTS_STOPPED) = 0U;
        *regptr(base, PWM_TASKS_STOP) = 1U;
        for (uint32_t guard = 0UL; guard < 200000UL; ++guard) {
            if (*regptr(base, PWM_EVENTS_STOPPED) != 0U) {
                break;
            }
        }
        *regptr(base, PWM_EVENTS_STOPPED) = 0U;
    }

    *regptr(base, PWM_SHORTS) = 0U;
    pwm_disconnect_all(instance);
    *regptr(base, PWM_ENABLE) = PWM_ENABLE_DISABLED;

    g_pwm_running[instance] = 0U;
    g_pwm_initialized[instance] = 0U;
}

void analogWriteDisable(uint8_t pin)
{
    uint8_t pwm_pin = 0U;
    if (!pwm_pin_index_for_pin(pin, &pwm_pin)) {
        return;
    }
    if (g_pwm_pin_used[pwm_pin] == 0U) {
        return;
    }

    g_pwm_pin_used[pwm_pin] = 0U;
    g_pwm_pin_software[pwm_pin] = 0U;
    g_pwm_pin_pulse[pwm_pin] = 0U;
    g_soft_pwm_on_time_us[pwm_pin] = 0UL;
    g_soft_pwm_output_high[pwm_pin] = 0U;
    timer_pwm_release_pin(pwm_pin);

    if (pwm_pin_can_use_hardware(pwm_pin) != 0U) {
        pwm_release_shared_output(pwm_pin);
    }

    gpio_write_raw(k_pwm_pin_desc[pwm_pin].port, k_pwm_pin_desc[pwm_pin].pin, 0U);
}

static uint8_t select_saadc_resolution_bits(uint8_t requested_bits)
{
    if (requested_bits <= 8U) {
        return 8U;
    }
    if (requested_bits <= 10U) {
        return 10U;
    }
    if (requested_bits <= 12U) {
        return 12U;
    }
    return 14U;
}

static uint32_t scale_resolution(uint32_t value, uint8_t from_bits, uint8_t to_bits)
{
    if (to_bits == from_bits) {
        return value;
    }
    if (to_bits < from_bits) {
        return value >> (from_bits - to_bits);
    }

    const uint8_t shift = (uint8_t)(to_bits - from_bits);
    if (shift >= 31U) {
        return 0x7FFFFFFFUL;
    }
    const uint32_t shifted = value << shift;
    if ((shifted >> shift) != value) {
        return 0x7FFFFFFFUL;
    }
    return shifted;
}

static int saadc_sample_pin(uint8_t port, uint8_t pin, uint8_t resolution_bits)
{
    const uintptr_t base = (uintptr_t)NRF_SAADC;

    gpio_prepare_analog_input(port, pin);

    *regptr(base, SAADC_ENABLE) = SAADC_ENABLE_DISABLED;
    *regptr(base, SAADC_OVERSAMPLE) = SAADC_OVERSAMPLE_BYPASS;
    *regptr(base, SAADC_NOISESHAPE) = SAADC_NOISESHAPE_DISABLED;
    *regptr(base, SAADC_SAMPLERATE) = (SAADC_SAMPLERATE_MODE_Task << SAADC_SAMPLERATE_MODE_Pos);

    for (uint8_t ch = 0; ch < 8U; ++ch) {
        const uintptr_t off = (uintptr_t)ch * SAADC_CH_STRIDE;
        *regptr(base, SAADC_CH_PSELP + off) =
            ((uint32_t)SAADC_CH_PSELP_CONNECT_NC << SAADC_CH_PSELP_CONNECT_Pos);
        *regptr(base, SAADC_CH_PSELN + off) =
            ((uint32_t)SAADC_CH_PSELN_CONNECT_NC << SAADC_CH_PSELN_CONNECT_Pos);
        *regptr(base, SAADC_CH_CONFIG + off) = 0;
    }

    uint32_t cfg = 0;
    cfg |= (7UL << SAADC_CH_CONFIG_GAIN_Pos);      // Gain = 2/8
    cfg |= (SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos);
    cfg |= (SAADC_CH_CONFIG_MODE_SE << SAADC_CH_CONFIG_MODE_Pos);
    cfg |= (159UL << SAADC_CH_CONFIG_TACQ_Pos);
    cfg |= (4UL << SAADC_CH_CONFIG_TCONV_Pos);

    *regptr(base, SAADC_CH_PSELP) = make_psel(port, pin);
    *regptr(base, SAADC_CH_PSELN) =
        ((uint32_t)SAADC_CH_PSELN_CONNECT_NC << SAADC_CH_PSELN_CONNECT_Pos);
    *regptr(base, SAADC_CH_CONFIG) = cfg;

    uint8_t resolution_sel = 2U;
    if (resolution_bits <= 8U) {
        resolution_sel = 0U;
    } else if (resolution_bits <= 10U) {
        resolution_sel = 1U;
    } else if (resolution_bits <= 12U) {
        resolution_sel = 2U;
    } else {
        resolution_sel = 3U;
    }

    *regptr(base, SAADC_RESOLUTION) = resolution_sel;

    g_saadc_sample = 0;
    *regptr(base, SAADC_RESULT_PTR) = (uint32_t)(uintptr_t)&g_saadc_sample;
    // MAXCNT is in bytes (one SAADC sample = 2 bytes).
    *regptr(base, SAADC_RESULT_MAXCNT) = 2UL;

    *regptr(base, SAADC_EVENTS_STARTED) = 0;
    *regptr(base, SAADC_EVENTS_END) = 0;
    *regptr(base, SAADC_EVENTS_STOPPED) = 0;
    *regptr(base, SAADC_EVENTS_CALDONE) = 0;

    *regptr(base, SAADC_ENABLE) = SAADC_ENABLE_ENABLED;
    *regptr(base, SAADC_TASKS_CALIBRATE) = 1UL;
    for (uint32_t guard = 0; guard < 200000UL; ++guard) {
        if (*regptr(base, SAADC_EVENTS_CALDONE) != 0U) {
            break;
        }
    }
    *regptr(base, SAADC_EVENTS_CALDONE) = 0;

    *regptr(base, SAADC_TASKS_START) = 1UL;
    for (uint32_t guard = 0; guard < 200000UL; ++guard) {
        if (*regptr(base, SAADC_EVENTS_STARTED) != 0U) {
            break;
        }
    }

    *regptr(base, SAADC_TASKS_SAMPLE) = 1UL;
    for (uint32_t guard = 0; guard < 400000UL; ++guard) {
        if (*regptr(base, SAADC_EVENTS_END) != 0U) {
            break;
        }
    }

    *regptr(base, SAADC_TASKS_STOP) = 1UL;
    for (uint32_t guard = 0; guard < 200000UL; ++guard) {
        if (*regptr(base, SAADC_EVENTS_STOPPED) != 0U) {
            break;
        }
    }

    *regptr(base, SAADC_ENABLE) = SAADC_ENABLE_DISABLED;

    if (*regptr(base, SAADC_RESULT_AMOUNT) < 2UL) {
        return 0;
    }

    int32_t value = (int32_t)g_saadc_sample;
    if (value < 0) {
        value = 0;
    }
    return (int)value;
}

void analogReference(uint8_t mode)
{
    (void)mode;
}

void analogReadResolution(uint8_t bits)
{
    if (bits < 1U) {
        bits = 1U;
    }
    if (bits > 30U) {
        bits = 30U;
    }
    g_analog_read_resolution = bits;
}

void analogWriteResolution(uint8_t bits)
{
    if (bits < 1U) {
        bits = 1U;
    }
    if (bits > 16U) {
        bits = 16U;
    }
    g_analog_write_resolution = bits;
}

void analogWriteFrequency(uint32_t hz)
{
    if (hz == 0UL) {
        hz = ANALOG_PWM_DEFAULT_HZ;
    }

    uint8_t prescaler = 0U;
    uint16_t countertop = 0U;
    if (!pwm_compute_timing(hz, &prescaler, &countertop)) {
        return;
    }

    const uint16_t old_countertop = g_pwm_countertop;
    g_analog_write_frequency_hz = hz;
    g_pwm_prescaler = prescaler;
    g_pwm_countertop = countertop;
    g_soft_pwm_period_us = soft_pwm_period_us_for_frequency(hz);

    analog_write_recompute_active_outputs(old_countertop);
    for (uint8_t i = 0U; i < ANALOG_PWM_PIN_COUNT; ++i) {
        if (g_pwm_pin_used[i] == 0U ||
            g_pwm_pin_timer_slot[i] == ANALOG_PWM_NO_SLOT) {
            continue;
        }
        timer_pwm_apply_pin(i);
    }

    if (g_pwm_initialized[0U] != 0U || pwm_instance_any_used_channel(0U) != 0U) {
        pwm_stop_instance(0U);
        if (pwm_instance_any_used_channel(0U) != 0U) {
            pwm_start_if_needed(0U);
        }
    }

    nrf54l15_analog_write_idle_service();
}

static void analog_write_apply_pwm_pin(uint8_t pwm_pin)
{
    if (pwm_pin >= ANALOG_PWM_PIN_COUNT) {
        return;
    }

    const uint32_t pulse = g_pwm_pin_pulse[pwm_pin];

    if (pulse == 0U) {
        g_pwm_pin_used[pwm_pin] = 0U;
        g_pwm_pin_software[pwm_pin] = 0U;
        timer_pwm_release_pin(pwm_pin);
        if (pwm_pin_can_use_hardware(pwm_pin) != 0U) {
            pwm_release_shared_output(pwm_pin);
        }
        g_soft_pwm_on_time_us[pwm_pin] = 0UL;
        gpio_write_raw(k_pwm_pin_desc[pwm_pin].port, k_pwm_pin_desc[pwm_pin].pin, 0U);
        return;
    }

    if (pulse >= (uint32_t)g_pwm_countertop) {
        g_pwm_pin_used[pwm_pin] = 0U;
        g_pwm_pin_software[pwm_pin] = 0U;
        timer_pwm_release_pin(pwm_pin);
        if (pwm_pin_can_use_hardware(pwm_pin) != 0U) {
            pwm_release_shared_output(pwm_pin);
        }
        g_soft_pwm_on_time_us[pwm_pin] = 0UL;
        gpio_write_raw(k_pwm_pin_desc[pwm_pin].port, k_pwm_pin_desc[pwm_pin].pin, 1U);
        return;
    }

    if (pwm_pin_requests_custom_frequency(pwm_pin) != 0U) {
        pwm_release_shared_output(pwm_pin);
        if (timer_pwm_apply_pin(pwm_pin) != 0U) {
            return;
        }
    } else {
        timer_pwm_release_pin(pwm_pin);
    }

    if (pwm_pin_requests_custom_frequency(pwm_pin) == 0U &&
        pwm_pin_can_use_hardware(pwm_pin) != 0U) {
        const uint8_t instance = k_pwm_pin_desc[pwm_pin].pwm_instance;
        uint8_t channel = ANALOG_PWM_NO_CHANNEL;
        if (pwm_acquire_channel(pwm_pin, &channel) != 0U) {
            g_pwm_pin_software[pwm_pin] = 0U;
            g_soft_pwm_on_time_us[pwm_pin] = 0UL;
            // nRF54L15 uses bit 15 to select polarity. FallingEdge polarity
            // starts high and transitions low at COMPARE, which matches
            // Arduino's active-high analogWrite() semantics.
            g_pwm_sequence[instance][channel] =
                (uint16_t)(PWM_COMPARE_POLARITY_FALLING_EDGE | (pulse & 0x7FFFU));

            if (pwm_init_once(instance)) {
                pwm_apply_outputs(instance);
            }
            pwm_start_if_needed(instance);
            return;
        }
    }

    g_pwm_pin_software[pwm_pin] = 1U;
    soft_pwm_update_channel(pwm_pin);
    nrf54l15_analog_write_idle_service();
}

void analogWritePinFrequency(uint8_t pin, uint32_t hz)
{
    uint8_t pwm_pin = 0U;
    if (!pwm_pin_index_for_pin(pin, &pwm_pin)) {
        return;
    }

    g_pwm_pin_frequency_hz[pwm_pin] = hz;

    if (g_pwm_pin_used[pwm_pin] == 0U) {
        if (hz == 0UL) {
            timer_pwm_release_pin(pwm_pin);
        }
        return;
    }

    if (hz == 0UL) {
        timer_pwm_release_pin(pwm_pin);
    }

    analog_write_apply_pwm_pin(pwm_pin);
}

int analogRead(uint8_t pin)
{
    adc_pin_desc_t d = resolve_analog(pin);
    if (d.ain < 0) {
        return 0;
    }

    const uint8_t hw_bits = select_saadc_resolution_bits(g_analog_read_resolution);
    // Take a second sample after channel setup/calibration to avoid stale first-read artifacts.
    const int first_raw = saadc_sample_pin(d.port, d.pin, hw_bits);
    const int second_raw = saadc_sample_pin(d.port, d.pin, hw_bits);
    const int raw = (second_raw > 0) ? second_raw : first_raw;
    if (raw <= 0) {
        return 0;
    }

    uint32_t scaled = scale_resolution((uint32_t)raw, hw_bits, g_analog_read_resolution);
    if (scaled > 0x7FFFFFFFUL) {
        scaled = 0x7FFFFFFFUL;
    }
    return (int)scaled;
}

void analogWrite(uint8_t pin, int value)
{
    int max_value;
    if (g_analog_write_resolution >= 16U) {
        max_value = 65535;
    } else {
        max_value = (1 << g_analog_write_resolution) - 1;
    }
    if (max_value <= 0) {
        max_value = 255;
    }
    if (value < 0) {
        value = 0;
    }
    if (value > max_value) {
        value = max_value;
    }

    uint8_t pwm_pin = 0U;
    if (!pwm_pin_index_for_pin(pin, &pwm_pin)) {
        digitalWrite(pin, (value > (max_value / 2)) ? HIGH : LOW);
        return;
    }

    pinMode(pin, OUTPUT);

    uint32_t pulse = 0U;
    if (value > 0) {
        pulse = ((uint32_t)value * (uint32_t)g_pwm_countertop + (uint32_t)(max_value / 2)) /
                (uint32_t)max_value;
        if (pulse > (uint32_t)g_pwm_countertop) {
            pulse = (uint32_t)g_pwm_countertop;
        }
    }

    g_pwm_pin_used[pwm_pin] = 1U;
    g_pwm_pin_pulse[pwm_pin] = (uint16_t)pulse;
    analog_write_apply_pwm_pin(pwm_pin);
}

void tone(uint8_t pin, unsigned int frequency, unsigned long duration)
{
    if (frequency == 0U) {
        return;
    }

    const unsigned long period_us = 1000000UL / frequency;
    const unsigned long half = (period_us > 1UL) ? (period_us / 2UL) : 1UL;

    if (duration == 0UL) {
        duration = period_us * 8UL;
    }

    const unsigned long start = millis();
    while ((millis() - start) < duration) {
        digitalWrite(pin, HIGH);
        delayMicroseconds((unsigned int)half);
        digitalWrite(pin, LOW);
        delayMicroseconds((unsigned int)half);
    }
}

void noTone(uint8_t pin)
{
    digitalWrite(pin, LOW);
}
