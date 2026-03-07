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

// PWM register offsets for analogWrite() on D6..D9.
#define PWM_TASKS_STOP                    0x004UL
#define PWM_TASKS_DMA_SEQ_START           0x010UL
#define PWM_EVENTS_STOPPED                0x104UL
#define PWM_EVENTS_SEQSTARTED0            0x108UL
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

#define PWM_SHORT_LOOPSDONE_SEQ0_START    (1UL << 2U)
#define PWM_SHORT_DMA_SEQ0_BUSERROR_STOP  (1UL << 6U)

#define ANALOG_PWM_CHANNELS               4U
#define ANALOG_PWM_DEFAULT_HZ             1000UL

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

static uint8_t g_pwm_initialized = 0U;
static uint8_t g_pwm_running = 0U;
static uint8_t g_pwm_channel_used[ANALOG_PWM_CHANNELS] = {0U, 0U, 0U, 0U};
static uint16_t g_pwm_channel_pulse[ANALOG_PWM_CHANNELS] = {0U, 0U, 0U, 0U};
static uint16_t g_pwm_sequence[ANALOG_PWM_CHANNELS] = {0U, 0U, 0U, 0U};
static uint16_t g_pwm_countertop = 16000U;
static uint8_t g_pwm_prescaler = 0U;

static const uint8_t k_pwm_arduino_pin[ANALOG_PWM_CHANNELS] = {
    PIN_D6, PIN_D7, PIN_D8, PIN_D9
};
static const uint8_t k_pwm_port[ANALOG_PWM_CHANNELS] = {
    2U, 2U, 2U, 2U
};
static const uint8_t k_pwm_pin[ANALOG_PWM_CHANNELS] = {
    8U, 7U, 1U, 4U
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

static inline uint32_t make_gpio_psel(uint8_t port, uint8_t pin)
{
    return ((uint32_t)(pin & 0x1FU) << 0U) |
           ((uint32_t)(port & 0x7U) << 5U);
}

static uint8_t pwm_channel_for_pin(uint8_t pin, uint8_t* channel)
{
    if (channel == 0) {
        return 0U;
    }
    for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS; ++ch) {
        if (k_pwm_arduino_pin[ch] == pin) {
            *channel = ch;
            return 1U;
        }
    }
    return 0U;
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

static uint8_t pwm_any_dynamic_channel(void)
{
    for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS; ++ch) {
        if (g_pwm_channel_used[ch] == 0U) {
            continue;
        }
        if (g_pwm_channel_pulse[ch] != 0U && g_pwm_channel_pulse[ch] != g_pwm_countertop) {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t pwm_any_used_channel(void)
{
    for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS; ++ch) {
        if (g_pwm_channel_used[ch] != 0U) {
            return 1U;
        }
    }
    return 0U;
}

static void pwm_disconnect_all(uintptr_t base)
{
    for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS; ++ch) {
        *regptr(base, PWM_PSEL_OUT0 + ((uintptr_t)ch * PWM_PSEL_OUT_STRIDE)) = PWM_PSEL_DISCONNECTED;
    }
}

static void pwm_apply_outputs(uintptr_t base)
{
    for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS; ++ch) {
        if (g_pwm_channel_used[ch] != 0U) {
            *regptr(base, PWM_PSEL_OUT0 + ((uintptr_t)ch * PWM_PSEL_OUT_STRIDE)) =
                make_gpio_psel(k_pwm_port[ch], k_pwm_pin[ch]);
        } else {
            *regptr(base, PWM_PSEL_OUT0 + ((uintptr_t)ch * PWM_PSEL_OUT_STRIDE)) =
                PWM_PSEL_DISCONNECTED;
        }
    }
}

static void pwm_drive_static_levels(void)
{
    for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS; ++ch) {
        if (g_pwm_channel_used[ch] == 0U) {
            continue;
        }
        gpio_write_raw(
            k_pwm_port[ch],
            k_pwm_pin[ch],
            (g_pwm_channel_pulse[ch] >= g_pwm_countertop) ? 1U : 0U
        );
    }
}

static uint8_t pwm_init_once(void)
{
    if (g_pwm_initialized != 0U) {
        return 1U;
    }

    if (!pwm_compute_timing(ANALOG_PWM_DEFAULT_HZ, &g_pwm_prescaler, &g_pwm_countertop)) {
        return 0U;
    }

    const uintptr_t base = (uintptr_t)NRF_PWM21;

    *regptr(base, PWM_ENABLE) = PWM_ENABLE_DISABLED;
    *regptr(base, PWM_SHORTS) = 0U;
    *regptr(base, PWM_MODE) = PWM_MODE_UP;
    *regptr(base, PWM_COUNTERTOP) = (uint32_t)g_pwm_countertop;
    *regptr(base, PWM_PRESCALER) = (uint32_t)g_pwm_prescaler;
    *regptr(base, PWM_DECODER) =
        (PWM_DECODER_LOAD_INDIVIDUAL << 0U) |
        (PWM_DECODER_MODE_REFRESHCOUNT << 8U);
    *regptr(base, PWM_LOOP) = 0U;
    *regptr(base, PWM_IDLEOUT) = 0U;
    *regptr(base, PWM_SEQ0_REFRESH) = 0U;
    *regptr(base, PWM_SEQ0_ENDDELAY) = 0U;
    *regptr(base, PWM_SEQ1_REFRESH) = 0U;
    *regptr(base, PWM_SEQ1_ENDDELAY) = 0U;
    pwm_disconnect_all(base);

    *regptr(base, PWM_DMA_SEQ0_PTR) = (uint32_t)(uintptr_t)&g_pwm_sequence[0];
    *regptr(base, PWM_DMA_SEQ0_MAXCNT) = 8U;
    *regptr(base, PWM_DMA_SEQ1_PTR) = (uint32_t)(uintptr_t)&g_pwm_sequence[0];
    *regptr(base, PWM_DMA_SEQ1_MAXCNT) = 8U;

    *regptr(base, PWM_ENABLE) = PWM_ENABLE_ENABLED;
    g_pwm_initialized = 1U;
    return 1U;
}

static void pwm_start_if_needed(void)
{
    if (g_pwm_running != 0U) {
        return;
    }
    if (!pwm_init_once()) {
        return;
    }

    const uintptr_t base = (uintptr_t)NRF_PWM21;

    pwm_apply_outputs(base);

    *regptr(base, PWM_EVENTS_SEQSTARTED0) = 0U;
    *regptr(base, PWM_SHORTS) =
        PWM_SHORT_LOOPSDONE_SEQ0_START |
        PWM_SHORT_DMA_SEQ0_BUSERROR_STOP;
    *regptr(base, PWM_TASKS_DMA_SEQ_START) = 1U;

    for (uint32_t guard = 0UL; guard < 200000UL; ++guard) {
        if (*regptr(base, PWM_EVENTS_SEQSTARTED0) != 0U) {
            break;
        }
    }
    *regptr(base, PWM_EVENTS_SEQSTARTED0) = 0U;
    g_pwm_running = 1U;
}

static void pwm_stop_all(void)
{
    if (g_pwm_initialized == 0U) {
        return;
    }

    const uintptr_t base = (uintptr_t)NRF_PWM21;

    if (g_pwm_running != 0U) {
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
    pwm_disconnect_all(base);
    *regptr(base, PWM_ENABLE) = PWM_ENABLE_DISABLED;

    g_pwm_running = 0U;
    g_pwm_initialized = 0U;
}

void analogWriteDisable(uint8_t pin)
{
    uint8_t channel = 0U;
    if (!pwm_channel_for_pin(pin, &channel)) {
        return;
    }
    if (g_pwm_channel_used[channel] == 0U) {
        return;
    }

    g_pwm_channel_used[channel] = 0U;
    g_pwm_channel_pulse[channel] = 0U;
    g_pwm_sequence[channel] = 0U;

    if (g_pwm_initialized != 0U) {
        const uintptr_t base = (uintptr_t)NRF_PWM21;
        *regptr(base, PWM_PSEL_OUT0 + ((uintptr_t)channel * PWM_PSEL_OUT_STRIDE)) = PWM_PSEL_DISCONNECTED;
    }

    if (pwm_any_dynamic_channel() != 0U) {
        if (g_pwm_running == 0U) {
            pwm_start_if_needed();
        }
        return;
    }

    pwm_stop_all();

    if (pwm_any_used_channel() != 0U) {
        pwm_drive_static_levels();
    }
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

    uint8_t channel = 0U;
    if (!pwm_channel_for_pin(pin, &channel)) {
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

    g_pwm_channel_used[channel] = 1U;
    g_pwm_channel_pulse[channel] = (uint16_t)pulse;
    g_pwm_sequence[channel] = (uint16_t)(pulse & 0x7FFFU);

    if (pwm_any_dynamic_channel() != 0U) {
        if (pwm_init_once()) {
            pwm_apply_outputs((uintptr_t)NRF_PWM21);
        }
        pwm_start_if_needed();
        return;
    }

    pwm_stop_all();
    pwm_drive_static_levels();
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
