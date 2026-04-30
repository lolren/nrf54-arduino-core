#include "Arduino.h"

#include <stdint.h>
#include <string.h>

#include "cmsis.h"
#include <nrf54l15.h>

extern void nrf54l15_pwm20_irq_service(void) __attribute__((weak));
extern void nrf54l15_pwm21_irq_service(void) __attribute__((weak));
extern void nrf54l15_pwm22_irq_service(void) __attribute__((weak));

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
#define PWM_DMA_SEQ_STRIDE                0x024UL
#define PWM_DMA_SEQ1_PTR                  (PWM_DMA_SEQ0_PTR + PWM_DMA_SEQ_STRIDE)
#define PWM_DMA_SEQ1_MAXCNT               (PWM_DMA_SEQ0_MAXCNT + PWM_DMA_SEQ_STRIDE)

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
#define TIMER_TASKS_CAPTURE0              0x040UL
#define TIMER_TASKS_CAPTURE_STRIDE        0x004UL
#define TIMER_SUBSCRIBE_START             0x080UL
#define TIMER_SUBSCRIBE_CLEAR             0x08CUL
#define TIMER_EVENTS_COMPARE0             0x140UL
#define TIMER_EVENTS_COMPARE_STRIDE       0x004UL
#define TIMER_INTENSET                    0x304UL
#define TIMER_INTENCLR                    0x308UL
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

#define GPIOTE_TASKS_OUT0                 0x000UL
#define GPIOTE_TASKS_SET0                 0x030UL
#define GPIOTE_TASKS_CLR0                 0x060UL
#define GPIOTE_SUBSCRIBE_OUT0             0x080UL
#define GPIOTE_SUBSCRIBE_SET0             0x0B0UL
#define GPIOTE_SUBSCRIBE_CLR0             0x0E0UL
#define GPIOTE_CONFIG0                    0x510UL
#define GPIOTE_CONFIG_STRIDE              0x004UL

#define GPIOTE_CONFIG_MODE_TASK           3UL
#define GPIOTE_CONFIG_POLARITY_NONE       0UL
#define GPIOTE_CONFIG_POLARITY_TOGGLE     3UL

#define PPIB_SUBSCRIBE_SEND0              0x080UL
#define PPIB_EVENTS_RECEIVE0              0x100UL
#define PPIB_PUBLISH_RECEIVE0             0x180UL
#define PPIB_CHANNEL_STRIDE               0x004UL

#define DPPIC_CHENSET                     0x504UL
#define DPPIC_CHENCLR                     0x508UL

#define ANALOG_PWM_CHANNELS_PER_INSTANCE  4U
#define ANALOG_PWM_INSTANCES              3U
#define ANALOG_PWM_PIN_COUNT              16U
#define ANALOG_TIMER_PWM_SLOT_COUNT       6U
#define ANALOG_TIMER_PWM_PIN_COUNT        ANALOG_PWM_PIN_COUNT
#define ANALOG_TIMER_PWM_PERIOD_CHANNEL   0U
#define ANALOG_TIMER_PWM_CAPTURE_CHANNEL  7U
#define ANALOG_TIMER_PWM_PERI_DPPIC_CHANNELS  16U
#define ANALOG_TIMER_PWM_RADIO_DPPIC_CHANNELS 8U
#define ANALOG_TIMER_PWM_RADIO_DPPIC_FIRST_CHANNEL 16U
#define ANALOG_TIMER_PWM_PPIB1121_CHANNELS 8U
#define ANALOG_TIMER_PWM_PPIB1121_FIRST_INDEX 8U
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
static void timer_pwm_release_pin(uint8_t index);
static void soft_pwm_drive_channel(uint8_t index, uint8_t high);
static uint8_t pwm_pin_can_use_hardware(uint8_t index);
static uint8_t pwm_pin_prefers_timer_output(uint8_t index);

static inline volatile uint32_t* regptr(uintptr_t base, uintptr_t off)
{
    return (volatile uint32_t*)(base + off);
}

#if defined(NRF54L15_CLEAN_POWER_LOW)
static const uint16_t kSaadcLowPowerSettleUs = 250U;
static const uint16_t kSaadcLowPowerRetrySettleUs = 750U;
static const uint8_t kSaadcLowPowerAttempts = 3U;
#endif
static const uint32_t kSaadcCalibrateSpinLimit = 200000UL;
static const uint32_t kSaadcStartSpinLimit = 200000UL;
static const uint32_t kSaadcSampleSpinLimit = 400000UL;
static const uint32_t kSaadcStopSpinLimit = 200000UL;

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
    uint8_t pwm_instance;
    uint8_t pwm_channel;
} pwm_pin_desc_t;

static adc_pin_desc_t resolve_analog(uint8_t apin)
{
    uint8_t port = 0U;
    uint8_t pin = 0U;
    if (!pinToPortPin(apin, &port, &pin)) {
        return (adc_pin_desc_t){0U, 0U, -1};
    }
    return (adc_pin_desc_t){port, pin, pinToSaadcChannel(apin)};
}

// Keep Arduino compatibility default (0..1023) unless sketch overrides.
static uint8_t g_analog_read_resolution = 10U;
static uint8_t g_analog_write_resolution = 8U;
// Written by SAADC EasyDMA, so this must be volatile.
static volatile int16_t g_saadc_sample = 0;

static uint8_t g_pwm_initialized[ANALOG_PWM_INSTANCES] = {0U, 0U, 0U};
static uint8_t g_pwm_running[ANALOG_PWM_INSTANCES] = {0U, 0U, 0U};
static uint8_t g_pwm_pin_used[ANALOG_PWM_PIN_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static uint8_t g_pwm_pin_software[ANALOG_PWM_PIN_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static uint8_t g_pwm_pin_channel[ANALOG_PWM_PIN_COUNT] = {
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL
};
static uint8_t g_pwm_channel_owner[ANALOG_PWM_INSTANCES][ANALOG_PWM_CHANNELS_PER_INSTANCE] = {
    {ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN},
    {ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN},
    {ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN, ANALOG_PWM_NO_PIN}
};
static uint16_t g_pwm_pin_pulse[ANALOG_PWM_PIN_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static uint16_t g_pwm_sequence[ANALOG_PWM_INSTANCES][ANALOG_PWM_CHANNELS_PER_INSTANCE]
    __attribute__((aligned(4))) = {
    {0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U}
};
static uint32_t g_soft_pwm_on_time_us[ANALOG_PWM_PIN_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static uint8_t g_soft_pwm_output_high[ANALOG_PWM_PIN_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static uint8_t g_pwm_pin_timer_slot[ANALOG_PWM_PIN_COUNT] = {
    ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT,
    ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT,
    ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT,
    ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT,
    ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT, ANALOG_PWM_NO_SLOT,
    ANALOG_PWM_NO_SLOT
};
static uint16_t g_timer_pwm_slot_member_mask[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U
};
static uint8_t g_timer_pwm_slot_set_dppi_channel[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL
};
static uint8_t g_timer_pwm_slot_set_source_channel[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL
};
static uint8_t g_timer_pwm_slot_set_ppib_channel[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL
};
static uint8_t g_timer_pwm_pin_gpiote_channel[ANALOG_PWM_PIN_COUNT] = {
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL
};
static uint8_t g_timer_pwm_pin_clr_dppi_channel[ANALOG_PWM_PIN_COUNT] = {
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL
};
static uint8_t g_timer_pwm_pin_clr_source_channel[ANALOG_PWM_PIN_COUNT] = {
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL
};
static uint8_t g_timer_pwm_pin_clr_ppib_channel[ANALOG_PWM_PIN_COUNT] = {
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL
};
static uint8_t g_timer_pwm_pin_compare_channel[ANALOG_PWM_PIN_COUNT] = {
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL, ANALOG_PWM_NO_CHANNEL,
    ANALOG_PWM_NO_CHANNEL
};
static uint8_t g_timer_pwm_slot_prescaler[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U
};
static uint32_t g_timer_pwm_slot_frequency_hz[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    0UL, 0UL, 0UL, 0UL, 0UL, 0UL
};
static uint32_t g_timer_pwm_slot_period_ticks[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    0UL, 0UL, 0UL, 0UL, 0UL, 0UL
};
static uint8_t g_timer_pwm_slot_active[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U
};
static uint32_t g_timer_pwm_pin_high_ticks[ANALOG_PWM_PIN_COUNT] = {
    0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL,
    0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL
};
static uint8_t g_timer_pwm_peri_dppi_channel_in_use[ANALOG_TIMER_PWM_PERI_DPPIC_CHANNELS] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static uint8_t g_timer_pwm_radio_dppi_channel_in_use[ANALOG_TIMER_PWM_RADIO_DPPIC_CHANNELS] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static uint8_t g_timer_pwm_ppib1121_channel_in_use[ANALOG_TIMER_PWM_PPIB1121_CHANNELS] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};
static uint32_t g_pwm_pin_frequency_hz[ANALOG_PWM_PIN_COUNT] = {
    0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL,
    0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL, 0UL
};
static uint16_t g_pwm_countertop = 16000U;
static uint8_t g_pwm_prescaler = 0U;
static uint32_t g_analog_write_frequency_hz = ANALOG_PWM_DEFAULT_HZ;
static uint32_t g_soft_pwm_period_us = ANALOG_SOFT_PWM_DEFAULT_PERIOD_US;

static const uintptr_t k_pwm_base[ANALOG_PWM_INSTANCES] = {
    (uintptr_t)NRF_PWM20,
    (uintptr_t)NRF_PWM21,
    (uintptr_t)NRF_PWM22
};
#ifdef NRF_TRUSTZONE_NONSECURE
static const uintptr_t k_timer_pwm_base[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    0x400CA000UL, 0x400CB000UL, 0x400CC000UL, 0x400CD000UL, 0x400CE000UL,
    0x40085000UL
};
static const uintptr_t k_timer_pwm_gpiote_base = 0x400DA000UL;
static const uintptr_t k_timer_pwm_peri_dppic_base = 0x400C2000UL;
static const uintptr_t k_timer_pwm_radio_dppic_base = 0x40082000UL;
static const uintptr_t k_timer_pwm_ppib11_base = 0x40084000UL;
static const uintptr_t k_timer_pwm_ppib21_base = 0x400C4000UL;
#else
static const uintptr_t k_timer_pwm_base[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    0x500CA000UL, 0x500CB000UL, 0x500CC000UL, 0x500CD000UL, 0x500CE000UL,
    0x50085000UL
};
static const uintptr_t k_timer_pwm_gpiote_base = 0x500DA000UL;
static const uintptr_t k_timer_pwm_peri_dppic_base = 0x500C2000UL;
static const uintptr_t k_timer_pwm_radio_dppic_base = 0x50082000UL;
static const uintptr_t k_timer_pwm_ppib11_base = 0x50084000UL;
static const uintptr_t k_timer_pwm_ppib21_base = 0x500C4000UL;
#endif
static const IRQn_Type k_timer_pwm_irqn[ANALOG_TIMER_PWM_SLOT_COUNT] = {
    TIMER20_IRQn, TIMER21_IRQn, TIMER22_IRQn, TIMER23_IRQn, TIMER24_IRQn, Reset_IRQn
};
static const pwm_pin_desc_t k_pwm_pin_desc[ANALOG_PWM_PIN_COUNT] = {
    {PIN_D0, 0U, ANALOG_PWM_NO_CHANNEL},
    {PIN_D1, 0U, ANALOG_PWM_NO_CHANNEL},
    {PIN_D2, 0U, ANALOG_PWM_NO_CHANNEL},
    {PIN_D3, 0U, ANALOG_PWM_NO_CHANNEL},
    {PIN_D4, 1U, ANALOG_PWM_NO_CHANNEL},
    {PIN_D5, 1U, ANALOG_PWM_NO_CHANNEL},
    {PIN_D6, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL},
    {PIN_D7, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL},
    {PIN_D8, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL},
    {PIN_D9, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL},
    {PIN_D10, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL},
    {PIN_D11, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL},
    {PIN_D12, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL},
    {PIN_D13, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL},
    {PIN_D14, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL},
    {PIN_D15, ANALOG_PWM_INSTANCE_NONE, ANALOG_PWM_NO_CHANNEL}
};

static uint8_t resolve_pwm_gpio(uint8_t index, uint8_t* port, uint8_t* pin)
{
    if (index >= ANALOG_PWM_PIN_COUNT || port == 0 || pin == 0) {
        return 0U;
    }
    return pinToPortPin(k_pwm_pin_desc[index].arduino_pin, port, pin) ? 1U : 0U;
}

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

static uint8_t saadc_wait_event(uintptr_t base, uintptr_t event_off, uint32_t spin_limit)
{
    while (spin_limit-- > 0U) {
        if (*regptr(base, event_off) != 0U) {
            return 1U;
        }
        __NOP();
    }

    return (*regptr(base, event_off) != 0U) ? 1U : 0U;
}

static void saadc_stop_and_disable(uintptr_t base)
{
    *regptr(base, SAADC_TASKS_STOP) = 1UL;
    (void)saadc_wait_event(base, SAADC_EVENTS_STOPPED, kSaadcStopSpinLimit);
    *regptr(base, SAADC_ENABLE) = SAADC_ENABLE_DISABLED;
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
    return (index < ANALOG_TIMER_PWM_PIN_COUNT) ? 1U : 0U;
}

static uint8_t pwm_pin_supports_gpiote_tasks(uint8_t index)
{
    uint8_t port = 0U;
    uint8_t pin = 0U;
    if (resolve_pwm_gpio(index, &port, &pin) == 0U) {
        return 0U;
    }

    (void)pin;
    return (port == 2U) ? 0U : 1U;
}

static uint8_t timer_pwm_pin_uses_irq_drive(uint8_t index)
{
    if (pwm_pin_supports_timer_output(index) == 0U) {
        return 0U;
    }

    return (pwm_pin_supports_gpiote_tasks(index) == 0U) ? 1U : 0U;
}

static uint8_t timer_pwm_slot_uses_bridge(uint8_t slot)
{
    return (slot == (ANALOG_TIMER_PWM_SLOT_COUNT - 1U)) ? 1U : 0U;
}

static uint8_t timer_pwm_slot_supports_irq_service(uint8_t slot)
{
    return (slot < (ANALOG_TIMER_PWM_SLOT_COUNT - 1U)) ? 1U : 0U;
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

static uint32_t timer_pwm_slot_base_clock_hz(uint8_t slot)
{
    return (timer_pwm_slot_uses_bridge(slot) != 0U) ? 32000000UL : 16000000UL;
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
                                        uint32_t base_clock_hz,
                                        uint8_t* prescaler,
                                        uint32_t* period_ticks)
{
    if (prescaler == 0 || period_ticks == 0 || target_hz == 0UL ||
        base_clock_hz == 0UL) {
        return 0U;
    }

    uint32_t best_error = 0xFFFFFFFFUL;
    uint8_t best_prescaler = 0U;
    uint32_t best_period_ticks = 0U;
    uint8_t found = 0U;

    for (uint8_t p = 0U; p <= 9U; ++p) {
        const uint32_t timer_clk = base_clock_hz >> p;
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

static uint32_t timer_pwm_capture_phase_ticks(uintptr_t timer_base,
                                              uint8_t capture_channel,
                                              uint32_t period_ticks)
{
    if (capture_channel == ANALOG_PWM_NO_CHANNEL) {
        return period_ticks;
    }

    *regptr(timer_base,
            TIMER_TASKS_CAPTURE0 + ((uintptr_t)capture_channel * TIMER_TASKS_CAPTURE_STRIDE)) = 1U;

    uint32_t phase_ticks =
        *regptr(timer_base,
                TIMER_CC0 + ((uintptr_t)capture_channel * TIMER_CC_STRIDE));
    if (period_ticks != 0UL && phase_ticks >= period_ticks) {
        phase_ticks = 0UL;
    }
    return phase_ticks;
}

static uint16_t timer_pwm_pin_mask(uint8_t index)
{
    if (index >= ANALOG_TIMER_PWM_PIN_COUNT) {
        return 0U;
    }
    return (uint16_t)(1U << index);
}

static uint8_t timer_pwm_slot_max_compare_channel(uint8_t slot)
{
    if (slot >= ANALOG_TIMER_PWM_SLOT_COUNT) {
        return 0U;
    }

    return (timer_pwm_slot_uses_bridge(slot) != 0U) ? 6U : 5U;
}

static uint8_t timer_pwm_compare_channel_in_use(uint8_t slot, uint8_t compare_channel)
{
    if (slot >= ANALOG_TIMER_PWM_SLOT_COUNT ||
        compare_channel == 0U ||
        compare_channel > timer_pwm_slot_max_compare_channel(slot)) {
        return 0U;
    }

    for (uint8_t index = 0U; index < ANALOG_TIMER_PWM_PIN_COUNT; ++index) {
        if ((g_timer_pwm_slot_member_mask[slot] & timer_pwm_pin_mask(index)) == 0U) {
            continue;
        }
        if (g_timer_pwm_pin_compare_channel[index] == compare_channel) {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t timer_pwm_compare_channel_member_count(uint8_t slot, uint8_t compare_channel)
{
    if (slot >= ANALOG_TIMER_PWM_SLOT_COUNT ||
        compare_channel == ANALOG_PWM_NO_CHANNEL) {
        return 0U;
    }

    uint8_t count = 0U;
    for (uint8_t index = 0U; index < ANALOG_TIMER_PWM_PIN_COUNT; ++index) {
        if ((g_timer_pwm_slot_member_mask[slot] & timer_pwm_pin_mask(index)) == 0U ||
            g_timer_pwm_pin_compare_channel[index] != compare_channel) {
            continue;
        }
        ++count;
    }

    return count;
}

static uint8_t timer_pwm_find_reusable_compare_channel(uint8_t slot,
                                                       uint8_t index,
                                                       uint32_t high_ticks)
{
    if (slot >= ANALOG_TIMER_PWM_SLOT_COUNT ||
        timer_pwm_pin_uses_irq_drive(index) == 0U) {
        return ANALOG_PWM_NO_CHANNEL;
    }

    for (uint8_t other = 0U; other < ANALOG_TIMER_PWM_PIN_COUNT; ++other) {
        if (other == index ||
            (g_timer_pwm_slot_member_mask[slot] & timer_pwm_pin_mask(other)) == 0U ||
            timer_pwm_pin_uses_irq_drive(other) == 0U ||
            g_timer_pwm_pin_high_ticks[other] != high_ticks ||
            g_timer_pwm_pin_compare_channel[other] == ANALOG_PWM_NO_CHANNEL) {
            continue;
        }

        return g_timer_pwm_pin_compare_channel[other];
    }

    return ANALOG_PWM_NO_CHANNEL;
}

static uint8_t timer_pwm_find_free_compare_channel(uint8_t slot)
{
    const uint8_t max_compare = timer_pwm_slot_max_compare_channel(slot);
    for (uint8_t compare_channel = 1U; compare_channel <= max_compare; ++compare_channel) {
        if (timer_pwm_compare_channel_in_use(slot, compare_channel) == 0U) {
            return compare_channel;
        }
    }

    return ANALOG_PWM_NO_CHANNEL;
}

static uint8_t timer_pwm_slot_capture_channel(uint8_t slot)
{
    (void)slot;
    return ANALOG_TIMER_PWM_CAPTURE_CHANNEL;
}

static uint32_t timer_pwm_dppi_channel_mask32(uint8_t channel)
{
    if (channel >= 32U) {
        return 0UL;
    }
    return (1UL << channel);
}

static uint32_t timer_pwm_compare_irq_mask(uint8_t compare_channel)
{
    if (compare_channel > 7U) {
        return 0UL;
    }

    return (TIMER_INTENSET_COMPARE0_Msk << compare_channel);
}

static uint8_t timer_pwm_slot_can_host_pin(uint8_t slot, uint8_t index)
{
    if (slot >= ANALOG_TIMER_PWM_SLOT_COUNT ||
        pwm_pin_supports_timer_output(index) == 0U) {
        return 0U;
    }

    if (timer_pwm_pin_uses_irq_drive(index) != 0U &&
        timer_pwm_slot_supports_irq_service(slot) == 0U) {
        return 0U;
    }

    return 1U;
}

static void timer_pwm_handle_irq_slot(uint8_t slot)
{
    if (slot >= ANALOG_TIMER_PWM_SLOT_COUNT ||
        timer_pwm_slot_supports_irq_service(slot) == 0U ||
        g_timer_pwm_slot_active[slot] == 0U ||
        g_timer_pwm_slot_member_mask[slot] == 0U) {
        return;
    }

    const uintptr_t timer_base = k_timer_pwm_base[slot];
    const uint16_t member_mask = g_timer_pwm_slot_member_mask[slot];

    if (*regptr(timer_base, TIMER_EVENTS_COMPARE0) != 0U) {
        *regptr(timer_base, TIMER_EVENTS_COMPARE0) = 0U;
        for (uint8_t index = 0U; index < ANALOG_TIMER_PWM_PIN_COUNT; ++index) {
            if ((member_mask & timer_pwm_pin_mask(index)) == 0U ||
                timer_pwm_pin_uses_irq_drive(index) == 0U) {
                continue;
            }
            soft_pwm_drive_channel(index, 1U);
        }
    }

    for (uint8_t compare_channel = 1U;
         compare_channel <= timer_pwm_slot_max_compare_channel(slot);
         ++compare_channel) {
        const uintptr_t event_reg =
            TIMER_EVENTS_COMPARE0 + ((uintptr_t)compare_channel * TIMER_EVENTS_COMPARE_STRIDE);
        if (*regptr(timer_base, event_reg) == 0U) {
            continue;
        }

        *regptr(timer_base, event_reg) = 0U;
        for (uint8_t index = 0U; index < ANALOG_TIMER_PWM_PIN_COUNT; ++index) {
            if ((member_mask & timer_pwm_pin_mask(index)) == 0U ||
                timer_pwm_pin_uses_irq_drive(index) == 0U ||
                g_timer_pwm_pin_compare_channel[index] != compare_channel) {
                continue;
            }
            soft_pwm_drive_channel(index, 0U);
        }
    }
}

void TIMER20_IRQHandler(void)
{
    timer_pwm_handle_irq_slot(0U);
}

void TIMER21_IRQHandler(void)
{
    timer_pwm_handle_irq_slot(1U);
}

void TIMER22_IRQHandler(void)
{
    timer_pwm_handle_irq_slot(2U);
}

void TIMER23_IRQHandler(void)
{
    timer_pwm_handle_irq_slot(3U);
}

void TIMER24_IRQHandler(void)
{
    timer_pwm_handle_irq_slot(4U);
}

void PWM20_IRQHandler(void)
{
    if (nrf54l15_pwm20_irq_service != 0) {
        nrf54l15_pwm20_irq_service();
    }
}

void PWM21_IRQHandler(void)
{
    if (nrf54l15_pwm21_irq_service != 0) {
        nrf54l15_pwm21_irq_service();
    }
}

void PWM22_IRQHandler(void)
{
    if (nrf54l15_pwm22_irq_service != 0) {
        nrf54l15_pwm22_irq_service();
    }
}

static void timer_pwm_ppib1121_disconnect(uint8_t channel)
{
    if (channel < ANALOG_TIMER_PWM_PPIB1121_FIRST_INDEX ||
        channel >= (ANALOG_TIMER_PWM_PPIB1121_FIRST_INDEX +
                    ANALOG_TIMER_PWM_PPIB1121_CHANNELS)) {
        return;
    }

    *regptr(k_timer_pwm_ppib11_base,
            PPIB_SUBSCRIBE_SEND0 + ((uintptr_t)channel * PPIB_CHANNEL_STRIDE)) = 0U;
    *regptr(k_timer_pwm_ppib21_base,
            PPIB_PUBLISH_RECEIVE0 + ((uintptr_t)channel * PPIB_CHANNEL_STRIDE)) = 0U;
    *regptr(k_timer_pwm_ppib21_base,
            PPIB_EVENTS_RECEIVE0 + ((uintptr_t)channel * PPIB_CHANNEL_STRIDE)) = 0U;
}

static void timer_pwm_ppib1121_connect(uint8_t channel,
                                       uint8_t source_dppi_channel,
                                       uint8_t sink_dppi_channel)
{
    if (channel < ANALOG_TIMER_PWM_PPIB1121_FIRST_INDEX ||
        channel >= (ANALOG_TIMER_PWM_PPIB1121_FIRST_INDEX +
                    ANALOG_TIMER_PWM_PPIB1121_CHANNELS)) {
        return;
    }

    *regptr(k_timer_pwm_ppib21_base,
            PPIB_EVENTS_RECEIVE0 + ((uintptr_t)channel * PPIB_CHANNEL_STRIDE)) = 0U;
    *regptr(k_timer_pwm_ppib11_base,
            PPIB_SUBSCRIBE_SEND0 + ((uintptr_t)channel * PPIB_CHANNEL_STRIDE)) =
        dppi_config_value(source_dppi_channel);
    *regptr(k_timer_pwm_ppib21_base,
            PPIB_PUBLISH_RECEIVE0 + ((uintptr_t)channel * PPIB_CHANNEL_STRIDE)) =
        dppi_config_value(sink_dppi_channel);
}

static uint8_t timer_pwm_peri_dppi_acquire_channel(uint8_t* channel_out)
{
    if (channel_out == 0) {
        return 0U;
    }

    for (int8_t channel = (int8_t)ANALOG_TIMER_PWM_PERI_DPPIC_CHANNELS - 1;
         channel >= 0; --channel) {
        if (g_timer_pwm_peri_dppi_channel_in_use[channel] != 0U) {
            continue;
        }

        g_timer_pwm_peri_dppi_channel_in_use[channel] = 1U;
        *channel_out = (uint8_t)channel;
        return 1U;
    }

    return 0U;
}

static void timer_pwm_peri_dppi_release_channel(uint8_t channel)
{
    if (channel >= ANALOG_TIMER_PWM_PERI_DPPIC_CHANNELS) {
        return;
    }
    g_timer_pwm_peri_dppi_channel_in_use[channel] = 0U;
}

static uint8_t timer_pwm_radio_dppi_acquire_channel(uint8_t* channel_out)
{
    if (channel_out == 0) {
        return 0U;
    }

    for (int8_t offset = (int8_t)ANALOG_TIMER_PWM_RADIO_DPPIC_CHANNELS - 1;
         offset >= 0; --offset) {
        if (g_timer_pwm_radio_dppi_channel_in_use[offset] != 0U) {
            continue;
        }

        g_timer_pwm_radio_dppi_channel_in_use[offset] = 1U;
        *channel_out = (uint8_t)(ANALOG_TIMER_PWM_RADIO_DPPIC_FIRST_CHANNEL +
                                 (uint8_t)offset);
        return 1U;
    }

    return 0U;
}

static void timer_pwm_radio_dppi_release_channel(uint8_t channel)
{
    if (channel < ANALOG_TIMER_PWM_RADIO_DPPIC_FIRST_CHANNEL ||
        channel >= (ANALOG_TIMER_PWM_RADIO_DPPIC_FIRST_CHANNEL +
                    ANALOG_TIMER_PWM_RADIO_DPPIC_CHANNELS)) {
        return;
    }

    g_timer_pwm_radio_dppi_channel_in_use[
        channel - ANALOG_TIMER_PWM_RADIO_DPPIC_FIRST_CHANNEL] = 0U;
}

static uint8_t timer_pwm_ppib1121_acquire_channel(uint8_t* channel_out)
{
    if (channel_out == 0) {
        return 0U;
    }

    for (int8_t offset = (int8_t)ANALOG_TIMER_PWM_PPIB1121_CHANNELS - 1;
         offset >= 0; --offset) {
        if (g_timer_pwm_ppib1121_channel_in_use[offset] != 0U) {
            continue;
        }

        g_timer_pwm_ppib1121_channel_in_use[offset] = 1U;
        *channel_out = (uint8_t)(ANALOG_TIMER_PWM_PPIB1121_FIRST_INDEX +
                                 (uint8_t)offset);
        return 1U;
    }

    return 0U;
}

static void timer_pwm_ppib1121_release_channel(uint8_t channel)
{
    if (channel < ANALOG_TIMER_PWM_PPIB1121_FIRST_INDEX ||
        channel >= (ANALOG_TIMER_PWM_PPIB1121_FIRST_INDEX +
                    ANALOG_TIMER_PWM_PPIB1121_CHANNELS)) {
        return;
    }

    g_timer_pwm_ppib1121_channel_in_use[
        channel - ANALOG_TIMER_PWM_PPIB1121_FIRST_INDEX] = 0U;
}

static uint8_t timer_pwm_slot_member_count(uint8_t slot)
{
    if (slot >= ANALOG_TIMER_PWM_SLOT_COUNT) {
        return 0U;
    }

    uint8_t count = 0U;
    const uint16_t mask = g_timer_pwm_slot_member_mask[slot];
    for (uint8_t index = 0U; index < ANALOG_TIMER_PWM_PIN_COUNT; ++index) {
        if ((mask & timer_pwm_pin_mask(index)) != 0U) {
            ++count;
        }
    }
    return count;
}

static uint8_t timer_pwm_find_matching_slot(uint8_t index, uint32_t target_hz)
{
    for (uint8_t slot = 0U; slot < ANALOG_TIMER_PWM_SLOT_COUNT; ++slot) {
        if (g_timer_pwm_slot_member_mask[slot] == 0U ||
            timer_pwm_slot_can_host_pin(slot, index) == 0U) {
            continue;
        }
        if (g_timer_pwm_slot_frequency_hz[slot] == target_hz &&
            timer_pwm_find_free_compare_channel(slot) != ANALOG_PWM_NO_CHANNEL) {
            return slot;
        }
    }

    return ANALOG_PWM_NO_SLOT;
}

static uint8_t timer_pwm_find_free_slot(uint8_t index)
{
    for (uint8_t slot = 0U; slot < ANALOG_TIMER_PWM_SLOT_COUNT; ++slot) {
        if (g_timer_pwm_slot_member_mask[slot] == 0U &&
            timer_pwm_slot_can_host_pin(slot, index) != 0U) {
            return slot;
        }
    }

    return ANALOG_PWM_NO_SLOT;
}

static uint8_t timer_pwm_ensure_pin_resources(uint8_t slot, uint8_t index)
{
    uint8_t acquired_gpiote = 0U;
    uint8_t acquired_sink_dppi = 0U;
    uint8_t acquired_source_dppi = 0U;
    uint8_t acquired_ppib = 0U;

    if (slot >= ANALOG_TIMER_PWM_SLOT_COUNT || index >= ANALOG_PWM_PIN_COUNT) {
        return 0U;
    }

    if (timer_pwm_pin_uses_irq_drive(index) != 0U) {
        g_timer_pwm_pin_gpiote_channel[index] = ANALOG_PWM_NO_CHANNEL;
        g_timer_pwm_pin_clr_dppi_channel[index] = ANALOG_PWM_NO_CHANNEL;
        g_timer_pwm_pin_clr_source_channel[index] = ANALOG_PWM_NO_CHANNEL;
        g_timer_pwm_pin_clr_ppib_channel[index] = ANALOG_PWM_NO_CHANNEL;
        return 1U;
    }

    if (g_timer_pwm_pin_gpiote_channel[index] == ANALOG_PWM_NO_CHANNEL) {
        if (nrf54l15_gpiote20_acquire_task_channel(&g_timer_pwm_pin_gpiote_channel[index]) == 0U) {
            return 0U;
        }
        acquired_gpiote = 1U;
    }

    if (g_timer_pwm_pin_clr_dppi_channel[index] == ANALOG_PWM_NO_CHANNEL) {
        if (timer_pwm_peri_dppi_acquire_channel(&g_timer_pwm_pin_clr_dppi_channel[index]) == 0U) {
            if (acquired_gpiote != 0U) {
                nrf54l15_gpiote20_release_task_channel(g_timer_pwm_pin_gpiote_channel[index]);
                g_timer_pwm_pin_gpiote_channel[index] = ANALOG_PWM_NO_CHANNEL;
            }
            return 0U;
        }
        acquired_sink_dppi = 1U;
    }

    if (timer_pwm_slot_uses_bridge(slot) != 0U) {
        if (g_timer_pwm_pin_clr_source_channel[index] == ANALOG_PWM_NO_CHANNEL) {
            if (timer_pwm_radio_dppi_acquire_channel(&g_timer_pwm_pin_clr_source_channel[index]) == 0U) {
                if (acquired_sink_dppi != 0U) {
                    timer_pwm_peri_dppi_release_channel(g_timer_pwm_pin_clr_dppi_channel[index]);
                    g_timer_pwm_pin_clr_dppi_channel[index] = ANALOG_PWM_NO_CHANNEL;
                }
                if (acquired_gpiote != 0U) {
                    nrf54l15_gpiote20_release_task_channel(g_timer_pwm_pin_gpiote_channel[index]);
                    g_timer_pwm_pin_gpiote_channel[index] = ANALOG_PWM_NO_CHANNEL;
                }
                return 0U;
            }
            acquired_source_dppi = 1U;
        }

        if (g_timer_pwm_pin_clr_ppib_channel[index] == ANALOG_PWM_NO_CHANNEL) {
            if (timer_pwm_ppib1121_acquire_channel(&g_timer_pwm_pin_clr_ppib_channel[index]) == 0U) {
                if (acquired_source_dppi != 0U) {
                    timer_pwm_radio_dppi_release_channel(g_timer_pwm_pin_clr_source_channel[index]);
                    g_timer_pwm_pin_clr_source_channel[index] = ANALOG_PWM_NO_CHANNEL;
                }
                if (acquired_sink_dppi != 0U) {
                    timer_pwm_peri_dppi_release_channel(g_timer_pwm_pin_clr_dppi_channel[index]);
                    g_timer_pwm_pin_clr_dppi_channel[index] = ANALOG_PWM_NO_CHANNEL;
                }
                if (acquired_gpiote != 0U) {
                    nrf54l15_gpiote20_release_task_channel(g_timer_pwm_pin_gpiote_channel[index]);
                    g_timer_pwm_pin_gpiote_channel[index] = ANALOG_PWM_NO_CHANNEL;
                }
                return 0U;
            }
            acquired_ppib = 1U;
        }
    } else {
        g_timer_pwm_pin_clr_source_channel[index] = g_timer_pwm_pin_clr_dppi_channel[index];
        g_timer_pwm_pin_clr_ppib_channel[index] = ANALOG_PWM_NO_CHANNEL;
    }

    (void)acquired_ppib;
    return 1U;
}

static uint8_t timer_pwm_ensure_slot_set_channel(uint8_t slot)
{
    if (slot >= ANALOG_TIMER_PWM_SLOT_COUNT) {
        return 0U;
    }

    if (g_timer_pwm_slot_set_dppi_channel[slot] != ANALOG_PWM_NO_CHANNEL) {
        return 1U;
    }

    if (timer_pwm_peri_dppi_acquire_channel(&g_timer_pwm_slot_set_dppi_channel[slot]) == 0U) {
        return 0U;
    }

    if (timer_pwm_slot_uses_bridge(slot) != 0U) {
        if (timer_pwm_radio_dppi_acquire_channel(&g_timer_pwm_slot_set_source_channel[slot]) == 0U) {
            timer_pwm_peri_dppi_release_channel(g_timer_pwm_slot_set_dppi_channel[slot]);
            g_timer_pwm_slot_set_dppi_channel[slot] = ANALOG_PWM_NO_CHANNEL;
            return 0U;
        }
        if (timer_pwm_ppib1121_acquire_channel(&g_timer_pwm_slot_set_ppib_channel[slot]) == 0U) {
            timer_pwm_radio_dppi_release_channel(g_timer_pwm_slot_set_source_channel[slot]);
            timer_pwm_peri_dppi_release_channel(g_timer_pwm_slot_set_dppi_channel[slot]);
            g_timer_pwm_slot_set_source_channel[slot] = ANALOG_PWM_NO_CHANNEL;
            g_timer_pwm_slot_set_dppi_channel[slot] = ANALOG_PWM_NO_CHANNEL;
            return 0U;
        }
    } else {
        g_timer_pwm_slot_set_source_channel[slot] = g_timer_pwm_slot_set_dppi_channel[slot];
        g_timer_pwm_slot_set_ppib_channel[slot] = ANALOG_PWM_NO_CHANNEL;
    }

    return 1U;
}

static void timer_pwm_stop_slot(uint8_t slot)
{
    if (slot >= ANALOG_TIMER_PWM_SLOT_COUNT) {
        return;
    }

    const uintptr_t timer_base = k_timer_pwm_base[slot];
    const uint8_t use_bridge = timer_pwm_slot_uses_bridge(slot);
    uint32_t sink_disable_mask = 0UL;
    uint32_t source_disable_mask = 0UL;
    if (g_timer_pwm_slot_set_dppi_channel[slot] != ANALOG_PWM_NO_CHANNEL) {
        sink_disable_mask |=
            timer_pwm_dppi_channel_mask32(g_timer_pwm_slot_set_dppi_channel[slot]);
    }
    if (use_bridge != 0U) {
        if (g_timer_pwm_slot_set_source_channel[slot] != ANALOG_PWM_NO_CHANNEL) {
            source_disable_mask |=
                timer_pwm_dppi_channel_mask32(g_timer_pwm_slot_set_source_channel[slot]);
        }
        timer_pwm_ppib1121_disconnect(g_timer_pwm_slot_set_ppib_channel[slot]);
    }

    *regptr(timer_base, TIMER_TASKS_STOP) = 1U;
    *regptr(timer_base, TIMER_SHORTS) = 0U;
    *regptr(timer_base, TIMER_INTENCLR) =
        timer_pwm_compare_irq_mask(0U) |
        timer_pwm_compare_irq_mask(1U) |
        timer_pwm_compare_irq_mask(2U) |
        timer_pwm_compare_irq_mask(3U) |
        timer_pwm_compare_irq_mask(4U) |
        timer_pwm_compare_irq_mask(5U) |
        timer_pwm_compare_irq_mask(6U) |
        timer_pwm_compare_irq_mask(7U);
    *regptr(timer_base, TIMER_SUBSCRIBE_START) = 0U;
    *regptr(timer_base, TIMER_SUBSCRIBE_CLEAR) = 0U;

    for (uint8_t compare = 0U; compare < 8U; ++compare) {
        *regptr(timer_base,
                TIMER_PUBLISH_COMPARE0 + ((uintptr_t)compare * TIMER_PUBLISH_COMPARE_STRIDE)) = 0U;
        *regptr(timer_base,
                TIMER_EVENTS_COMPARE0 + ((uintptr_t)compare * TIMER_EVENTS_COMPARE_STRIDE)) = 0U;
    }

    for (uint8_t index = 0U; index < ANALOG_TIMER_PWM_PIN_COUNT; ++index) {
        if ((g_timer_pwm_slot_member_mask[slot] & timer_pwm_pin_mask(index)) == 0U) {
            continue;
        }

        if (timer_pwm_pin_uses_irq_drive(index) != 0U) {
            soft_pwm_drive_channel(index, 0U);
        }

        const uint8_t gpiote_channel = g_timer_pwm_pin_gpiote_channel[index];
        if (gpiote_channel != ANALOG_PWM_NO_CHANNEL) {
            *regptr(k_timer_pwm_gpiote_base,
                    GPIOTE_SUBSCRIBE_SET0 +
                        ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE)) = 0U;
            *regptr(k_timer_pwm_gpiote_base,
                    GPIOTE_SUBSCRIBE_CLR0 +
                        ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE)) = 0U;
        }

        if (g_timer_pwm_pin_clr_dppi_channel[index] != ANALOG_PWM_NO_CHANNEL) {
            sink_disable_mask |=
                timer_pwm_dppi_channel_mask32(g_timer_pwm_pin_clr_dppi_channel[index]);
        }
        if (use_bridge != 0U) {
            if (g_timer_pwm_pin_clr_source_channel[index] != ANALOG_PWM_NO_CHANNEL) {
                source_disable_mask |=
                    timer_pwm_dppi_channel_mask32(g_timer_pwm_pin_clr_source_channel[index]);
            }
            timer_pwm_ppib1121_disconnect(g_timer_pwm_pin_clr_ppib_channel[index]);
        }
    }

    if (sink_disable_mask != 0UL) {
        *regptr(k_timer_pwm_peri_dppic_base, DPPIC_CHENCLR) = sink_disable_mask;
    }
    if (source_disable_mask != 0UL) {
        *regptr(k_timer_pwm_radio_dppic_base, DPPIC_CHENCLR) = source_disable_mask;
    }
    if (timer_pwm_slot_supports_irq_service(slot) != 0U) {
        NVIC_DisableIRQ(k_timer_pwm_irqn[slot]);
        NVIC_ClearPendingIRQ(k_timer_pwm_irqn[slot]);
    }

    g_timer_pwm_slot_active[slot] = 0U;
}

static void timer_pwm_restart_slot(uint8_t slot)
{
    if (slot >= ANALOG_TIMER_PWM_SLOT_COUNT ||
        g_timer_pwm_slot_member_mask[slot] == 0U ||
        g_timer_pwm_slot_set_dppi_channel[slot] == ANALOG_PWM_NO_CHANNEL) {
        return;
    }

    const uintptr_t timer_base = k_timer_pwm_base[slot];
    const uint8_t use_bridge = timer_pwm_slot_uses_bridge(slot);
    const uint8_t set_dppi_channel = g_timer_pwm_slot_set_dppi_channel[slot];
    const uint8_t set_source_channel = g_timer_pwm_slot_set_source_channel[slot];
    uint32_t sink_enable_mask = timer_pwm_dppi_channel_mask32(set_dppi_channel);
    uint32_t source_enable_mask =
        (use_bridge != 0U) ? timer_pwm_dppi_channel_mask32(set_source_channel) : 0UL;
    uint32_t irq_enable_mask = 0UL;

    timer_pwm_stop_slot(slot);

    *regptr(timer_base, TIMER_TASKS_CLEAR) = 1U;
    *regptr(timer_base, TIMER_MODE) = TIMER_MODE_TIMER;
    *regptr(timer_base, TIMER_BITMODE) = TIMER_BITMODE_32;
    *regptr(timer_base, TIMER_PRESCALER) = (uint32_t)g_timer_pwm_slot_prescaler[slot];
    *regptr(timer_base, TIMER_SHORTS) = TIMER_SHORT_COMPARE0_CLEAR;
    *regptr(timer_base, TIMER_CC0 + ((uintptr_t)ANALOG_TIMER_PWM_PERIOD_CHANNEL * TIMER_CC_STRIDE)) =
        g_timer_pwm_slot_period_ticks[slot];
    *regptr(timer_base, TIMER_PUBLISH_COMPARE0) = dppi_config_value(set_source_channel);
    if (use_bridge != 0U) {
        timer_pwm_ppib1121_connect(g_timer_pwm_slot_set_ppib_channel[slot],
                                   set_source_channel,
                                   set_dppi_channel);
    }

    for (uint8_t index = 0U; index < ANALOG_TIMER_PWM_PIN_COUNT; ++index) {
        if ((g_timer_pwm_slot_member_mask[slot] & timer_pwm_pin_mask(index)) == 0U) {
            continue;
        }

        const uint8_t gpiote_channel = g_timer_pwm_pin_gpiote_channel[index];
        const uint8_t clr_dppi_channel = g_timer_pwm_pin_clr_dppi_channel[index];
        const uint8_t clr_source_channel = g_timer_pwm_pin_clr_source_channel[index];
        const uint8_t compare_channel = g_timer_pwm_pin_compare_channel[index];
        const uint8_t irq_drive = timer_pwm_pin_uses_irq_drive(index);
        uint8_t port = 0U;
        uint8_t pin = 0U;

        if (compare_channel == ANALOG_PWM_NO_CHANNEL ||
            resolve_pwm_gpio(index, &port, &pin) == 0U) {
            continue;
        }

        *regptr(timer_base,
                TIMER_CC0 + ((uintptr_t)compare_channel * TIMER_CC_STRIDE)) =
            g_timer_pwm_pin_high_ticks[index];
        *regptr(timer_base,
                TIMER_EVENTS_COMPARE0 +
                    ((uintptr_t)compare_channel * TIMER_EVENTS_COMPARE_STRIDE)) = 0U;

        if (irq_drive != 0U) {
            irq_enable_mask |= timer_pwm_compare_irq_mask(compare_channel);
            g_pwm_pin_software[index] = 0U;
            g_soft_pwm_on_time_us[index] = 0UL;
            g_soft_pwm_output_high[index] = 0U;
            continue;
        }

        if (gpiote_channel == ANALOG_PWM_NO_CHANNEL ||
            clr_dppi_channel == ANALOG_PWM_NO_CHANNEL ||
            clr_source_channel == ANALOG_PWM_NO_CHANNEL) {
            continue;
        }

        uint32_t gpiote_config = 0U;
        gpiote_config |= (GPIOTE_CONFIG_MODE_TASK << GPIOTE_CONFIG_MODE_Pos);
        gpiote_config |= ((uint32_t)(pin & 0x1FU) << GPIOTE_CONFIG_PSEL_Pos);
        gpiote_config |= ((uint32_t)(port & 0x7U) << GPIOTE_CONFIG_PORT_Pos);
        gpiote_config |= (GPIOTE_CONFIG_POLARITY_NONE << GPIOTE_CONFIG_POLARITY_Pos);
        gpiote_config |= (0UL << GPIOTE_CONFIG_OUTINIT_Pos);
        *regptr(k_timer_pwm_gpiote_base,
                GPIOTE_CONFIG0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE)) =
            gpiote_config;
        *regptr(k_timer_pwm_gpiote_base,
                GPIOTE_SUBSCRIBE_SET0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE)) =
            dppi_config_value(set_dppi_channel);
        *regptr(k_timer_pwm_gpiote_base,
                GPIOTE_SUBSCRIBE_CLR0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE)) =
            dppi_config_value(clr_dppi_channel);
        *regptr(timer_base,
                TIMER_PUBLISH_COMPARE0 +
                    ((uintptr_t)compare_channel * TIMER_PUBLISH_COMPARE_STRIDE)) =
            dppi_config_value(clr_source_channel);
        if (use_bridge != 0U) {
            timer_pwm_ppib1121_connect(g_timer_pwm_pin_clr_ppib_channel[index],
                                       clr_source_channel,
                                       clr_dppi_channel);
            source_enable_mask |= timer_pwm_dppi_channel_mask32(clr_source_channel);
        }
        sink_enable_mask |= timer_pwm_dppi_channel_mask32(clr_dppi_channel);
        g_pwm_pin_software[index] = 0U;
        g_soft_pwm_on_time_us[index] = 0UL;
        g_soft_pwm_output_high[index] = 1U;
    }

    *regptr(k_timer_pwm_peri_dppic_base, DPPIC_CHENSET) = sink_enable_mask;
    if (source_enable_mask != 0UL) {
        *regptr(k_timer_pwm_radio_dppic_base, DPPIC_CHENSET) = source_enable_mask;
    }
    if (irq_enable_mask != 0UL) {
        *regptr(timer_base, TIMER_INTENSET) =
            irq_enable_mask | timer_pwm_compare_irq_mask(ANALOG_TIMER_PWM_PERIOD_CHANNEL);
        NVIC_ClearPendingIRQ(k_timer_pwm_irqn[slot]);
        NVIC_SetPriority(k_timer_pwm_irqn[slot], 3U);
        NVIC_EnableIRQ(k_timer_pwm_irqn[slot]);
    }

    for (uint8_t index = 0U; index < ANALOG_TIMER_PWM_PIN_COUNT; ++index) {
        if ((g_timer_pwm_slot_member_mask[slot] & timer_pwm_pin_mask(index)) == 0U) {
            continue;
        }

        if (timer_pwm_pin_uses_irq_drive(index) != 0U) {
            g_soft_pwm_output_high[index] = 0U;
            soft_pwm_drive_channel(index, 1U);
            continue;
        }

        const uint8_t gpiote_channel = g_timer_pwm_pin_gpiote_channel[index];
        if (gpiote_channel != ANALOG_PWM_NO_CHANNEL) {
            *regptr(k_timer_pwm_gpiote_base,
                    GPIOTE_TASKS_SET0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE)) = 1U;
        }
    }

    *regptr(timer_base, TIMER_TASKS_START) = 1U;
    g_timer_pwm_slot_active[slot] = 1U;
}

static uint8_t timer_pwm_hold_pin_static(uint8_t index, uint8_t high)
{
    uint8_t port = 0U;
    uint8_t pin = 0U;
    if (index >= ANALOG_PWM_PIN_COUNT ||
        g_pwm_pin_timer_slot[index] == ANALOG_PWM_NO_SLOT ||
        resolve_pwm_gpio(index, &port, &pin) == 0U) {
        return 0U;
    }

    timer_pwm_release_pin(index);
    gpio_write_raw(port, pin, high);
    g_pwm_pin_software[index] = 0U;
    g_soft_pwm_on_time_us[index] = 0UL;
    g_soft_pwm_output_high[index] = high;
    return 1U;
}

static uint8_t timer_pwm_update_pin_live(uint8_t slot,
                                         uint8_t index,
                                         uint8_t prescaler,
                                         uint32_t period_ticks,
                                         uint32_t high_ticks)
{
    if (slot >= ANALOG_TIMER_PWM_SLOT_COUNT || index >= ANALOG_PWM_PIN_COUNT) {
        return 0U;
    }

    if (g_pwm_pin_timer_slot[index] != slot ||
        (g_timer_pwm_slot_member_mask[slot] & timer_pwm_pin_mask(index)) == 0U ||
        g_timer_pwm_pin_gpiote_channel[index] == ANALOG_PWM_NO_CHANNEL ||
        g_timer_pwm_slot_active[slot] == 0U ||
        g_timer_pwm_slot_prescaler[slot] != prescaler ||
        g_timer_pwm_slot_period_ticks[slot] != period_ticks) {
        return 0U;
    }

    const uintptr_t timer_base = k_timer_pwm_base[slot];
    const uint8_t gpiote_channel = g_timer_pwm_pin_gpiote_channel[index];
    const uint8_t compare_channel = g_timer_pwm_pin_compare_channel[index];
    const uint8_t capture_channel = timer_pwm_slot_capture_channel(slot);
    const uint8_t irq_drive = timer_pwm_pin_uses_irq_drive(index);
    const uint32_t current_high_ticks = g_timer_pwm_pin_high_ticks[index];
    const uint32_t phase_ticks =
        timer_pwm_capture_phase_ticks(timer_base, capture_channel, period_ticks);

    if (compare_channel == ANALOG_PWM_NO_CHANNEL ||
        capture_channel == ANALOG_PWM_NO_CHANNEL ||
        (irq_drive == 0U && gpiote_channel == ANALOG_PWM_NO_CHANNEL)) {
        return 0U;
    }

    if (high_ticks != current_high_ticks &&
        timer_pwm_compare_channel_member_count(slot, compare_channel) > 1U) {
        return 0U;
    }

    // Apply the compare immediately so the next cycle already uses the new duty.
    // When the pin is currently high and the new compare has already passed,
    // force only the falling edge. Do not synthesize a new mid-cycle high phase
    // for upward duty updates, as that is what produces the 1 ms / multi-pin glitch.
    *regptr(timer_base, TIMER_CC0 + ((uintptr_t)compare_channel * TIMER_CC_STRIDE)) = high_ticks;
    *regptr(timer_base,
            TIMER_EVENTS_COMPARE0 + ((uintptr_t)compare_channel * TIMER_EVENTS_COMPARE_STRIDE)) = 0U;
    g_timer_pwm_pin_high_ticks[index] = high_ticks;

    if (current_high_ticks != 0UL &&
        phase_ticks < current_high_ticks &&
        (phase_ticks + 1UL) >= high_ticks) {
        if (irq_drive != 0U) {
            soft_pwm_drive_channel(index, 0U);
        } else {
            *regptr(k_timer_pwm_gpiote_base,
                    GPIOTE_TASKS_CLR0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE)) = 1U;
        }
    }

    g_pwm_pin_software[index] = 0U;
    g_soft_pwm_on_time_us[index] = 0UL;
    g_soft_pwm_output_high[index] = 1U;
    return 1U;
}

static void timer_pwm_service_pending_updates(void)
{
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

    const uint16_t pin_mask = timer_pwm_pin_mask(index);
    const uint8_t use_bridge = timer_pwm_slot_uses_bridge(slot);
    const uint8_t gpiote_channel = g_timer_pwm_pin_gpiote_channel[index];
    const uint8_t clr_dppi_channel = g_timer_pwm_pin_clr_dppi_channel[index];
    const uint8_t clr_source_channel = g_timer_pwm_pin_clr_source_channel[index];
    const uint8_t clr_ppib_channel = g_timer_pwm_pin_clr_ppib_channel[index];
    const uint8_t compare_channel = g_timer_pwm_pin_compare_channel[index];

    if ((g_timer_pwm_slot_member_mask[slot] & pin_mask) != 0U) {
        *regptr(k_timer_pwm_peri_dppic_base, DPPIC_CHENCLR) =
            timer_pwm_dppi_channel_mask32(clr_dppi_channel);
        if (use_bridge != 0U) {
            *regptr(k_timer_pwm_radio_dppic_base, DPPIC_CHENCLR) =
                timer_pwm_dppi_channel_mask32(clr_source_channel);
            timer_pwm_ppib1121_disconnect(clr_ppib_channel);
        }
        if (compare_channel != ANALOG_PWM_NO_CHANNEL) {
            *regptr(k_timer_pwm_base[slot],
                    TIMER_PUBLISH_COMPARE0 +
                        ((uintptr_t)compare_channel * TIMER_PUBLISH_COMPARE_STRIDE)) = 0U;
            *regptr(k_timer_pwm_base[slot],
                    TIMER_EVENTS_COMPARE0 +
                        ((uintptr_t)compare_channel * TIMER_EVENTS_COMPARE_STRIDE)) = 0U;
        }
        g_timer_pwm_slot_member_mask[slot] &= (uint16_t)(~pin_mask);
    }

    if (gpiote_channel != ANALOG_PWM_NO_CHANNEL) {
        *regptr(k_timer_pwm_gpiote_base,
                GPIOTE_SUBSCRIBE_SET0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE)) = 0U;
        *regptr(k_timer_pwm_gpiote_base,
                GPIOTE_SUBSCRIBE_CLR0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE)) = 0U;
        *regptr(k_timer_pwm_gpiote_base,
                GPIOTE_CONFIG0 + ((uintptr_t)gpiote_channel * GPIOTE_CONFIG_STRIDE)) = 0U;
        nrf54l15_gpiote20_release_task_channel(gpiote_channel);
        g_timer_pwm_pin_gpiote_channel[index] = ANALOG_PWM_NO_CHANNEL;
    }

    if (clr_dppi_channel != ANALOG_PWM_NO_CHANNEL) {
        timer_pwm_peri_dppi_release_channel(clr_dppi_channel);
        g_timer_pwm_pin_clr_dppi_channel[index] = ANALOG_PWM_NO_CHANNEL;
    }
    if (clr_source_channel != ANALOG_PWM_NO_CHANNEL &&
        use_bridge != 0U) {
        timer_pwm_radio_dppi_release_channel(clr_source_channel);
    }
    if (clr_ppib_channel != ANALOG_PWM_NO_CHANNEL &&
        use_bridge != 0U) {
        timer_pwm_ppib1121_release_channel(clr_ppib_channel);
    }
    g_timer_pwm_pin_clr_source_channel[index] = ANALOG_PWM_NO_CHANNEL;
    g_timer_pwm_pin_clr_ppib_channel[index] = ANALOG_PWM_NO_CHANNEL;
    g_timer_pwm_pin_compare_channel[index] = ANALOG_PWM_NO_CHANNEL;

    g_timer_pwm_pin_high_ticks[index] = 0UL;
    g_pwm_pin_timer_slot[index] = ANALOG_PWM_NO_SLOT;

    {
        uint8_t port = 0U;
        uint8_t pin = 0U;
        if (resolve_pwm_gpio(index, &port, &pin) != 0U) {
            gpio_write_raw(port, pin, 0U);
        }
    }
    g_soft_pwm_output_high[index] = 0U;

    if (g_timer_pwm_slot_member_mask[slot] == 0U) {
        timer_pwm_stop_slot(slot);
        if (g_timer_pwm_slot_set_dppi_channel[slot] != ANALOG_PWM_NO_CHANNEL) {
            timer_pwm_peri_dppi_release_channel(g_timer_pwm_slot_set_dppi_channel[slot]);
            g_timer_pwm_slot_set_dppi_channel[slot] = ANALOG_PWM_NO_CHANNEL;
        }
        if (g_timer_pwm_slot_set_source_channel[slot] != ANALOG_PWM_NO_CHANNEL &&
            use_bridge != 0U) {
            timer_pwm_radio_dppi_release_channel(g_timer_pwm_slot_set_source_channel[slot]);
        }
        if (g_timer_pwm_slot_set_ppib_channel[slot] != ANALOG_PWM_NO_CHANNEL &&
            use_bridge != 0U) {
            timer_pwm_ppib1121_release_channel(g_timer_pwm_slot_set_ppib_channel[slot]);
        }
        g_timer_pwm_slot_set_source_channel[slot] = ANALOG_PWM_NO_CHANNEL;
        g_timer_pwm_slot_set_ppib_channel[slot] = ANALOG_PWM_NO_CHANNEL;
        g_timer_pwm_slot_frequency_hz[slot] = 0UL;
        g_timer_pwm_slot_prescaler[slot] = 0U;
        g_timer_pwm_slot_period_ticks[slot] = 0UL;
        g_timer_pwm_slot_active[slot] = 0U;
    }
}

static uint8_t timer_pwm_apply_pin(uint8_t index)
{
    if (index >= ANALOG_PWM_PIN_COUNT ||
        pwm_pin_supports_timer_output(index) == 0U ||
        pwm_pin_prefers_timer_output(index) == 0U) {
        return 0U;
    }

    const uint32_t pulse = g_pwm_pin_pulse[index];
    if (pulse == 0UL || pulse >= (uint32_t)g_pwm_countertop) {
        return 0U;
    }

    const uint32_t target_hz = pwm_pin_effective_frequency_hz(index);
    uint8_t slot = ANALOG_PWM_NO_SLOT;
    const uint8_t current_slot = g_pwm_pin_timer_slot[index];
    if (current_slot != ANALOG_PWM_NO_SLOT &&
        g_timer_pwm_slot_frequency_hz[current_slot] == target_hz &&
        timer_pwm_slot_can_host_pin(current_slot, index) != 0U) {
        slot = current_slot;
    } else {
        const uint8_t matching_slot = timer_pwm_find_matching_slot(index, target_hz);
        if (matching_slot != ANALOG_PWM_NO_SLOT) {
            slot = matching_slot;
        }
    }
    if (slot == ANALOG_PWM_NO_SLOT &&
        current_slot != ANALOG_PWM_NO_SLOT &&
        timer_pwm_slot_member_count(current_slot) <= 1U &&
        timer_pwm_slot_can_host_pin(current_slot, index) != 0U) {
        slot = current_slot;
    } else if (slot == ANALOG_PWM_NO_SLOT) {
        slot = timer_pwm_find_free_slot(index);
    }

    if (slot == ANALOG_PWM_NO_SLOT) {
        timer_pwm_release_pin(index);
        return 0U;
    }

    uint8_t prescaler = 0U;
    uint32_t period_ticks = 0U;
    if (timer_pwm_compute_timing(target_hz,
                                 timer_pwm_slot_base_clock_hz(slot),
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

    if (current_slot != ANALOG_PWM_NO_SLOT &&
        timer_pwm_update_pin_live(current_slot, index, prescaler, period_ticks, high_ticks) != 0U) {
        return 1U;
    }

    if (current_slot != ANALOG_PWM_NO_SLOT && current_slot != slot) {
        timer_pwm_release_pin(index);
    }

    if (g_timer_pwm_pin_compare_channel[index] != ANALOG_PWM_NO_CHANNEL &&
        g_timer_pwm_pin_high_ticks[index] != high_ticks &&
        timer_pwm_compare_channel_member_count(slot,
                                               g_timer_pwm_pin_compare_channel[index]) > 1U) {
        g_timer_pwm_pin_compare_channel[index] = ANALOG_PWM_NO_CHANNEL;
    }

    if (g_timer_pwm_pin_compare_channel[index] == ANALOG_PWM_NO_CHANNEL) {
        g_timer_pwm_pin_compare_channel[index] =
            timer_pwm_find_reusable_compare_channel(slot, index, high_ticks);
        if (g_timer_pwm_pin_compare_channel[index] == ANALOG_PWM_NO_CHANNEL) {
            g_timer_pwm_pin_compare_channel[index] = timer_pwm_find_free_compare_channel(slot);
        }
    }
    if (g_timer_pwm_pin_compare_channel[index] == ANALOG_PWM_NO_CHANNEL ||
        timer_pwm_ensure_slot_set_channel(slot) == 0U ||
        timer_pwm_ensure_pin_resources(slot, index) == 0U) {
        timer_pwm_release_pin(index);
        return 0U;
    }

    g_timer_pwm_slot_prescaler[slot] = prescaler;
    g_timer_pwm_slot_frequency_hz[slot] = target_hz;
    g_timer_pwm_slot_period_ticks[slot] = period_ticks;
    g_timer_pwm_pin_high_ticks[index] = high_ticks;
    g_timer_pwm_slot_member_mask[slot] |= timer_pwm_pin_mask(index);
    g_pwm_pin_timer_slot[index] = slot;

    timer_pwm_restart_slot(slot);
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
    uint8_t port = 0U;
    uint8_t pin = 0U;
    if (index >= ANALOG_PWM_PIN_COUNT) {
        return 0U;
    }
    if (k_pwm_pin_desc[index].pwm_instance >= ANALOG_PWM_INSTANCES) {
        return 0U;
    }
    if (resolve_pwm_gpio(index, &port, &pin) == 0U) {
        return 0U;
    }
    (void)pin;
    // nRF54L15 PWM20/21/22 route to GPIO port P1 on this package/board path.
    return (port == 1U) ? 1U : 0U;
}

static uint8_t pwm_pin_prefers_timer_output(uint8_t index)
{
    if (pwm_pin_supports_timer_output(index) == 0U) {
        return 0U;
    }

    if (pwm_pin_requests_custom_frequency(index) != 0U) {
        return 1U;
    }

    return (pwm_pin_can_use_hardware(index) == 0U) ? 1U : 0U;
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

    const uint8_t instance = k_pwm_pin_desc[index].pwm_instance;
    for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS_PER_INSTANCE; ++ch) {
        if (g_pwm_channel_owner[instance][ch] == ANALOG_PWM_NO_PIN) {
            g_pwm_channel_owner[instance][ch] = index;
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
    const uint8_t instance = k_pwm_pin_desc[index].pwm_instance;
    if (instance >= ANALOG_PWM_INSTANCES) {
        return;
    }

    g_pwm_pin_channel[index] = ANALOG_PWM_NO_CHANNEL;
    if (channel < ANALOG_PWM_CHANNELS_PER_INSTANCE &&
        g_pwm_channel_owner[instance][channel] == index) {
        g_pwm_channel_owner[instance][channel] = ANALOG_PWM_NO_PIN;
        g_pwm_sequence[instance][channel] = 0U;
        if (g_pwm_initialized[instance] != 0U) {
            *regptr(k_pwm_base[instance], PWM_PSEL_OUT0 + ((uintptr_t)channel * PWM_PSEL_OUT_STRIDE)) =
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
    uint8_t port = 0U;
    uint8_t pin = 0U;
    if (index >= ANALOG_PWM_PIN_COUNT) {
        return;
    }
    if (g_soft_pwm_output_high[index] == high) {
        return;
    }
    if (resolve_pwm_gpio(index, &port, &pin) == 0U) {
        return;
    }
    g_soft_pwm_output_high[index] = high;
    gpio_write_raw(port, pin, high);
}

void nrf54l15_analog_write_idle_service(void)
{
    timer_pwm_service_pending_updates();

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
        const uint8_t owner = g_pwm_channel_owner[instance][ch];
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
        const uint8_t owner = g_pwm_channel_owner[instance][ch];
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
        const uint8_t owner = g_pwm_channel_owner[instance][ch];
        uint8_t port = 0U;
        uint8_t pin = 0U;
        if (owner == ANALOG_PWM_NO_PIN || owner >= ANALOG_PWM_PIN_COUNT) {
            continue;
        }
        if (resolve_pwm_gpio(owner, &port, &pin) == 0U) {
            continue;
        }
        *regptr(base, PWM_PSEL_OUT0 + ((uintptr_t)ch * PWM_PSEL_OUT_STRIDE)) =
            make_gpio_psel(port, pin);
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

    for (uint8_t instance = 0U; instance < ANALOG_PWM_INSTANCES; ++instance) {
        for (uint8_t ch = 0U; ch < ANALOG_PWM_CHANNELS_PER_INSTANCE; ++ch) {
            const uint8_t owner = g_pwm_channel_owner[instance][ch];
            if (owner == ANALOG_PWM_NO_PIN || owner >= ANALOG_PWM_PIN_COUNT) {
                continue;
            }
            g_pwm_sequence[instance][ch] =
                (uint16_t)(PWM_COMPARE_POLARITY_FALLING_EDGE | (g_pwm_pin_pulse[owner] & 0x7FFFU));
        }
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

    {
        uint8_t port = 0U;
        uint8_t pin_in_port = 0U;
        if (resolve_pwm_gpio(pwm_pin, &port, &pin_in_port) != 0U) {
            gpio_write_raw(port, pin_in_port, 0U);
        }
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

static int saadc_sample_pin_once(uint8_t port, uint8_t pin, uint8_t resolution_bits)
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

#if defined(NRF54L15_CLEAN_POWER_LOW)
    delayMicroseconds(kSaadcLowPowerSettleUs);
#endif
    *regptr(base, SAADC_ENABLE) = SAADC_ENABLE_ENABLED;
    *regptr(base, SAADC_TASKS_CALIBRATE) = 1UL;
    if (!saadc_wait_event(base, SAADC_EVENTS_CALDONE, kSaadcCalibrateSpinLimit)) {
        saadc_stop_and_disable(base);
        return -1;
    }
    *regptr(base, SAADC_EVENTS_CALDONE) = 0;

    *regptr(base, SAADC_TASKS_START) = 1UL;
    if (!saadc_wait_event(base, SAADC_EVENTS_STARTED, kSaadcStartSpinLimit)) {
        saadc_stop_and_disable(base);
        return -1;
    }

    *regptr(base, SAADC_TASKS_SAMPLE) = 1UL;
    if (!saadc_wait_event(base, SAADC_EVENTS_END, kSaadcSampleSpinLimit)) {
        saadc_stop_and_disable(base);
        return -1;
    }

    *regptr(base, SAADC_TASKS_STOP) = 1UL;
    if (!saadc_wait_event(base, SAADC_EVENTS_STOPPED, kSaadcStopSpinLimit)) {
        *regptr(base, SAADC_ENABLE) = SAADC_ENABLE_DISABLED;
        return -1;
    }

    *regptr(base, SAADC_ENABLE) = SAADC_ENABLE_DISABLED;

    if (*regptr(base, SAADC_RESULT_AMOUNT) < 2UL) {
        return -1;
    }

    int32_t value = (int32_t)g_saadc_sample;
    if (value < 0) {
        value = 0;
    }
    return (int)value;
}

static int saadc_sample_pin(uint8_t port, uint8_t pin, uint8_t resolution_bits)
{
#if defined(NRF54L15_CLEAN_POWER_LOW)
    for (uint8_t attempt = 0U; attempt < kSaadcLowPowerAttempts; ++attempt) {
        if (attempt != 0U) {
            delayMicroseconds(kSaadcLowPowerRetrySettleUs);
        }

        const int value = saadc_sample_pin_once(port, pin, resolution_bits);
        if (value >= 0) {
            return value;
        }
    }
    return -1;
#else
    return saadc_sample_pin_once(port, pin, resolution_bits);
#endif
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

    for (uint8_t instance = 0U; instance < ANALOG_PWM_INSTANCES; ++instance) {
        if (g_pwm_initialized[instance] != 0U ||
            pwm_instance_any_used_channel(instance) != 0U) {
            pwm_stop_instance(instance);
            if (pwm_instance_any_used_channel(instance) != 0U) {
                pwm_start_if_needed(instance);
            }
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
        if (pwm_pin_prefers_timer_output(pwm_pin) != 0U &&
            timer_pwm_hold_pin_static(pwm_pin, 0U) != 0U) {
            return;
        }
        g_pwm_pin_used[pwm_pin] = 0U;
        g_pwm_pin_software[pwm_pin] = 0U;
        timer_pwm_release_pin(pwm_pin);
        if (pwm_pin_can_use_hardware(pwm_pin) != 0U) {
            pwm_release_shared_output(pwm_pin);
        }
        g_soft_pwm_on_time_us[pwm_pin] = 0UL;
        {
            uint8_t port = 0U;
            uint8_t pin = 0U;
            if (resolve_pwm_gpio(pwm_pin, &port, &pin) != 0U) {
                gpio_write_raw(port, pin, 0U);
            }
        }
        return;
    }

    if (pulse >= (uint32_t)g_pwm_countertop) {
        if (pwm_pin_prefers_timer_output(pwm_pin) != 0U &&
            timer_pwm_hold_pin_static(pwm_pin, 1U) != 0U) {
            return;
        }
        g_pwm_pin_used[pwm_pin] = 0U;
        g_pwm_pin_software[pwm_pin] = 0U;
        timer_pwm_release_pin(pwm_pin);
        if (pwm_pin_can_use_hardware(pwm_pin) != 0U) {
            pwm_release_shared_output(pwm_pin);
        }
        g_soft_pwm_on_time_us[pwm_pin] = 0UL;
        {
            uint8_t port = 0U;
            uint8_t pin = 0U;
            if (resolve_pwm_gpio(pwm_pin, &port, &pin) != 0U) {
                gpio_write_raw(port, pin, 1U);
            }
        }
        return;
    }

    if (pwm_pin_prefers_timer_output(pwm_pin) != 0U) {
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
    const int raw = (second_raw >= 0) ? second_raw : first_raw;
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
