#include "Arduino.h"

#include "cmsis.h"
#include <nrf54l15.h>

typedef struct {
    uint8_t port;
    uint8_t pin;
    uint8_t valid;
} pin_desc_t;

static volatile uint32_t g_irq_nest = 0;
void analogWriteDisable(uint8_t pin) __attribute__((weak));

#define CORE_GPIOTE_CHANNEL_COUNT        8U
#define CORE_GPIOTE_EVENTS_IN            0x100UL
#define CORE_GPIOTE_INTENSET0            0x304UL
#define CORE_GPIOTE_INTENCLR0            0x308UL
#define CORE_GPIOTE_CONFIG               0x510UL

#define CORE_GPIOTE_CONFIG_MODE_Pos      0U
#define CORE_GPIOTE_CONFIG_PSEL_Pos      4U
#define CORE_GPIOTE_CONFIG_PORT_Pos      9U
#define CORE_GPIOTE_CONFIG_POLARITY_Pos  16U

#define CORE_GPIOTE_CONFIG_MODE_DISABLED 0U
#define CORE_GPIOTE_CONFIG_MODE_EVENT    1U
#define CORE_GPIOTE_POLARITY_LOTOHI      1U
#define CORE_GPIOTE_POLARITY_HITOLO      2U
#define CORE_GPIOTE_POLARITY_TOGGLE      3U

#ifdef NRF_TRUSTZONE_NONSECURE
#define CORE_GPIOTE20_BASE               0x400DA000UL
#else
#define CORE_GPIOTE20_BASE               0x500DA000UL
#endif

#define PIN_TO_CHANNEL_UNUSED       0xFFU
#define IRQ_PIN_MAP_SIZE            32U

typedef struct {
    uint8_t in_use;
    uint8_t pin;
    void (*callback)(void);
} irq_channel_t;

static irq_channel_t g_irq_channels[CORE_GPIOTE_CHANNEL_COUNT];
static uint8_t g_irq_pin_to_channel[IRQ_PIN_MAP_SIZE];
static uint8_t g_task_channels_in_use[CORE_GPIOTE_CHANNEL_COUNT];
static uint8_t g_irq_state_initialized = 0U;

static pin_desc_t resolve_pin(uint8_t pin)
{
    switch (pin) {
        case PIN_D0:  return (pin_desc_t){1U, 4U, 1U};
        case PIN_D1:  return (pin_desc_t){1U, 5U, 1U};
        case PIN_D2:  return (pin_desc_t){1U, 6U, 1U};
        case PIN_D3:  return (pin_desc_t){1U, 7U, 1U};
        case PIN_D4:  return (pin_desc_t){1U, 10U, 1U};
        case PIN_D5:  return (pin_desc_t){1U, 11U, 1U};
        case PIN_D6:  return (pin_desc_t){2U, 8U, 1U};
        case PIN_D7:  return (pin_desc_t){2U, 7U, 1U};
        case PIN_D8:  return (pin_desc_t){2U, 1U, 1U};
        case PIN_D9:  return (pin_desc_t){2U, 4U, 1U};
        case PIN_D10: return (pin_desc_t){2U, 2U, 1U};
        case PIN_D11: return (pin_desc_t){0U, 3U, 1U};
        case PIN_D12: return (pin_desc_t){0U, 4U, 1U};
        case PIN_D13: return (pin_desc_t){2U, 10U, 1U};
        case PIN_D14: return (pin_desc_t){2U, 9U, 1U};
        case PIN_D15: return (pin_desc_t){2U, 6U, 1U};
        case PIN_LED_BUILTIN: return (pin_desc_t){2U, 0U, 1U};
        case PIN_BUTTON: return (pin_desc_t){0U, 0U, 1U};
        case PIN_SAMD11_TX: return (pin_desc_t){1U, 8U, 1U};
        case PIN_SAMD11_RX: return (pin_desc_t){1U, 9U, 1U};
        case PIN_IMU_MIC_PWR: return (pin_desc_t){0U, 1U, 1U};
        case PIN_RF_SW: return (pin_desc_t){2U, 3U, 1U};
        case PIN_RF_SW_CTL: return (pin_desc_t){2U, 5U, 1U};
        case PIN_VBAT_EN: return (pin_desc_t){1U, 15U, 1U};
        default: return (pin_desc_t){0U, 0U, 0U};
    }
}

static NRF_GPIO_Type* gpio_for_port(uint8_t port)
{
    switch (port) {
        case 0: return NRF_P0;
        case 1: return NRF_P1;
        case 2: return NRF_P2;
        default: return (NRF_GPIO_Type*)0;
    }
}

static void irq_state_init_once(void)
{
    if (g_irq_state_initialized != 0U) {
        return;
    }

    for (uint8_t i = 0; i < IRQ_PIN_MAP_SIZE; ++i) {
        g_irq_pin_to_channel[i] = PIN_TO_CHANNEL_UNUSED;
    }
    for (uint8_t ch = 0; ch < CORE_GPIOTE_CHANNEL_COUNT; ++ch) {
        g_irq_channels[ch].in_use = 0U;
        g_irq_channels[ch].pin = 0xFFU;
        g_irq_channels[ch].callback = 0;
        g_task_channels_in_use[ch] = 0U;
    }
    g_irq_state_initialized = 1U;
}

static inline volatile uint32_t* gpiote_regptr(uintptr_t base, uintptr_t off)
{
    return (volatile uint32_t*)(base + off);
}

static uint32_t polarity_from_mode(int mode)
{
    switch (mode) {
        case RISING:
            return CORE_GPIOTE_POLARITY_LOTOHI;
        case LOW:
        case FALLING:
            return CORE_GPIOTE_POLARITY_HITOLO;
        case CHANGE:
        default:
            return CORE_GPIOTE_POLARITY_TOGGLE;
    }
}

static int8_t find_channel_for_pin(uint8_t pin)
{
    if (pin < IRQ_PIN_MAP_SIZE) {
        uint8_t mapped = g_irq_pin_to_channel[pin];
        if (mapped < CORE_GPIOTE_CHANNEL_COUNT && g_irq_channels[mapped].in_use != 0U) {
            return (int8_t)mapped;
        }
    }

    for (uint8_t ch = 0; ch < CORE_GPIOTE_CHANNEL_COUNT; ++ch) {
        if (g_irq_channels[ch].in_use != 0U && g_irq_channels[ch].pin == pin) {
            return (int8_t)ch;
        }
    }

    return -1;
}

static int8_t alloc_channel(void)
{
    for (uint8_t ch = 0; ch < CORE_GPIOTE_CHANNEL_COUNT; ++ch) {
        if (g_irq_channels[ch].in_use == 0U &&
            g_task_channels_in_use[ch] == 0U) {
            return (int8_t)ch;
        }
    }
    return -1;
}

uint8_t nrf54l15_gpiote20_acquire_task_channel(uint8_t* channel)
{
    if (channel == 0) {
        return 0U;
    }

    irq_state_init_once();
    for (uint8_t ch = 0; ch < CORE_GPIOTE_CHANNEL_COUNT; ++ch) {
        if (g_irq_channels[ch].in_use == 0U &&
            g_task_channels_in_use[ch] == 0U) {
            g_task_channels_in_use[ch] = 1U;
            *channel = ch;
            return 1U;
        }
    }

    return 0U;
}

void nrf54l15_gpiote20_release_task_channel(uint8_t channel)
{
    irq_state_init_once();
    if (channel >= CORE_GPIOTE_CHANNEL_COUNT) {
        return;
    }
    g_task_channels_in_use[channel] = 0U;
}

static uint8_t any_irq_channel_active(void)
{
    for (uint8_t ch = 0; ch < CORE_GPIOTE_CHANNEL_COUNT; ++ch) {
        if (g_irq_channels[ch].in_use != 0U) {
            return 1U;
        }
    }
    return 0U;
}

static void configure_pin_for_interrupt(const pin_desc_t* d)
{
    if (d == 0 || d->valid == 0U) {
        return;
    }

    NRF_GPIO_Type* gpio = gpio_for_port(d->port);
    if (gpio == 0) {
        return;
    }

    const uint32_t bit = (1UL << d->pin);
    uint32_t cnf = gpio->PIN_CNF[d->pin];
    cnf &= ~(GPIO_PIN_CNF_DIR_Msk |
             GPIO_PIN_CNF_INPUT_Msk |
             GPIO_PIN_CNF_SENSE_Msk);
    cnf |= (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos);
    cnf |= GPIO_PIN_CNF_INPUT_Connect;
    cnf |= GPIO_PIN_CNF_SENSE_Disabled;
    gpio->DIRCLR = bit;
    gpio->PIN_CNF[d->pin] = cnf;
}

static void gpiote_irq_service(void)
{
    if (g_irq_state_initialized == 0U) {
        return;
    }

    const uintptr_t base = (uintptr_t)CORE_GPIOTE20_BASE;

    for (uint8_t ch = 0; ch < CORE_GPIOTE_CHANNEL_COUNT; ++ch) {
        if (g_irq_channels[ch].in_use == 0U) {
            continue;
        }

        volatile uint32_t* event_reg = gpiote_regptr(base, CORE_GPIOTE_EVENTS_IN + ((uintptr_t)ch * sizeof(uint32_t)));
        if (*event_reg == 0U) {
            continue;
        }

        *event_reg = 0U;
        __DSB();

        void (*callback)(void) = g_irq_channels[ch].callback;
        if (callback != 0) {
            callback();
        }
    }
}

void GPIOTE20_0_IRQHandler(void)
{
    gpiote_irq_service();
}

void GPIOTE20_1_IRQHandler(void)
{
    gpiote_irq_service();
}

void pinMode(uint8_t pin, uint8_t mode)
{
    pin_desc_t d = resolve_pin(pin);
    if (!d.valid) {
        return;
    }

    NRF_GPIO_Type* gpio = gpio_for_port(d.port);
    if (gpio == 0) {
        return;
    }

    const uint32_t bit = (1UL << d.pin);
    uint32_t cnf = gpio->PIN_CNF[d.pin];

    cnf &= ~(GPIO_PIN_CNF_DIR_Msk |
             GPIO_PIN_CNF_INPUT_Msk |
             GPIO_PIN_CNF_PULL_Msk);

    if (mode == OUTPUT) {
        cnf |= (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos);
        cnf |= GPIO_PIN_CNF_INPUT_Disconnect;
        cnf |= GPIO_PIN_CNF_PULL_Disabled;
        gpio->DIRSET = bit;
    } else {
        if (analogWriteDisable != 0) {
            analogWriteDisable(pin);
        }

        cnf |= (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos);
        cnf |= GPIO_PIN_CNF_INPUT_Connect;

        if (mode == INPUT_PULLUP) {
            cnf |= GPIO_PIN_CNF_PULL_Pullup;
        } else if (mode == INPUT_PULLDOWN) {
            cnf |= GPIO_PIN_CNF_PULL_Pulldown;
        } else {
            cnf |= GPIO_PIN_CNF_PULL_Disabled;
        }

        gpio->DIRCLR = bit;
    }

    gpio->PIN_CNF[d.pin] = cnf;
}

void digitalWrite(uint8_t pin, uint8_t value)
{
    pin_desc_t d = resolve_pin(pin);
    if (!d.valid) {
        return;
    }

    if (analogWriteDisable != 0) {
        analogWriteDisable(pin);
    }

    NRF_GPIO_Type* gpio = gpio_for_port(d.port);
    if (gpio == 0) {
        return;
    }

    const uint32_t bit = (1UL << d.pin);
    if (value != LOW) {
        gpio->OUTSET = bit;
    } else {
        gpio->OUTCLR = bit;
    }
}

int digitalRead(uint8_t pin)
{
    pin_desc_t d = resolve_pin(pin);
    if (!d.valid) {
        return LOW;
    }

    NRF_GPIO_Type* gpio = gpio_for_port(d.port);
    if (gpio == 0) {
        return LOW;
    }

    const uint32_t bit = (1UL << d.pin);
    return ((gpio->IN & bit) != 0U) ? HIGH : LOW;
}

void attachInterrupt(uint8_t pin, void (*userFunc)(void), int mode)
{
    pin_desc_t d = resolve_pin(pin);
    if (!d.valid || userFunc == 0) {
        return;
    }

    if (mode != CHANGE && mode != RISING && mode != FALLING &&
        mode != LOW) {
        return;
    }

    irq_state_init_once();
    configure_pin_for_interrupt(&d);

    const uintptr_t base = (uintptr_t)CORE_GPIOTE20_BASE;
    const uint32_t polarity = polarity_from_mode(mode);

    noInterrupts();

    int8_t channel = find_channel_for_pin(pin);
    if (channel < 0) {
        channel = alloc_channel();
    }
    if (channel < 0) {
        interrupts();
        return;
    }

    const uint8_t ch = (uint8_t)channel;

    g_irq_channels[ch].in_use = 1U;
    g_irq_channels[ch].pin = pin;
    g_irq_channels[ch].callback = userFunc;
    if (pin < IRQ_PIN_MAP_SIZE) {
        g_irq_pin_to_channel[pin] = ch;
    }

    *gpiote_regptr(base, CORE_GPIOTE_INTENCLR0) = (1UL << ch);
    *gpiote_regptr(base, CORE_GPIOTE_CONFIG + ((uintptr_t)ch * sizeof(uint32_t))) = 0U;
    *gpiote_regptr(base, CORE_GPIOTE_EVENTS_IN + ((uintptr_t)ch * sizeof(uint32_t))) = 0U;

    uint32_t config = 0U;
    config |= (CORE_GPIOTE_CONFIG_MODE_EVENT << CORE_GPIOTE_CONFIG_MODE_Pos);
    config |= ((uint32_t)(d.pin & 0x1FU) << CORE_GPIOTE_CONFIG_PSEL_Pos);
    config |= ((uint32_t)(d.port & 0x7U) << CORE_GPIOTE_CONFIG_PORT_Pos);
    config |= (polarity << CORE_GPIOTE_CONFIG_POLARITY_Pos);

    *gpiote_regptr(base, CORE_GPIOTE_CONFIG + ((uintptr_t)ch * sizeof(uint32_t))) = config;
    *gpiote_regptr(base, CORE_GPIOTE_INTENSET0) = (1UL << ch);

    NVIC_SetPriority(GPIOTE20_0_IRQn, 3U);
    NVIC_SetPriority(GPIOTE20_1_IRQn, 3U);
    NVIC_EnableIRQ(GPIOTE20_0_IRQn);
    NVIC_EnableIRQ(GPIOTE20_1_IRQn);

    interrupts();
}

void detachInterrupt(uint8_t pin)
{
    pin_desc_t d = resolve_pin(pin);
    if (!d.valid || g_irq_state_initialized == 0U) {
        return;
    }

    noInterrupts();

    int8_t channel = find_channel_for_pin(pin);
    if (channel < 0) {
        interrupts();
        return;
    }

    const uintptr_t base = (uintptr_t)CORE_GPIOTE20_BASE;
    const uint8_t ch = (uint8_t)channel;

    *gpiote_regptr(base, CORE_GPIOTE_INTENCLR0) = (1UL << ch);
    *gpiote_regptr(base, CORE_GPIOTE_CONFIG + ((uintptr_t)ch * sizeof(uint32_t))) =
        (CORE_GPIOTE_CONFIG_MODE_DISABLED << CORE_GPIOTE_CONFIG_MODE_Pos);
    *gpiote_regptr(base, CORE_GPIOTE_EVENTS_IN + ((uintptr_t)ch * sizeof(uint32_t))) = 0U;

    g_irq_channels[ch].in_use = 0U;
    g_irq_channels[ch].pin = 0xFFU;
    g_irq_channels[ch].callback = 0;
    if (pin < IRQ_PIN_MAP_SIZE) {
        g_irq_pin_to_channel[pin] = PIN_TO_CHANNEL_UNUSED;
    }

    if (any_irq_channel_active() == 0U) {
        NVIC_DisableIRQ(GPIOTE20_0_IRQn);
        NVIC_DisableIRQ(GPIOTE20_1_IRQn);
    }

    interrupts();
}

void noInterrupts(void)
{
    __disable_irq();
    ++g_irq_nest;
}

void interrupts(void)
{
    if (g_irq_nest > 0U) {
        --g_irq_nest;
    }
    if (g_irq_nest == 0U) {
        __enable_irq();
    }
}

void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value)
{
    for (uint8_t i = 0; i < 8; ++i) {
        uint8_t bit_index = (bitOrder == LSBFIRST) ? i : (7 - i);
        digitalWrite(dataPin, (value >> bit_index) & 0x01U);
        digitalWrite(clockPin, HIGH);
        digitalWrite(clockPin, LOW);
    }
}

uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder)
{
    uint8_t value = 0;

    for (uint8_t i = 0; i < 8; ++i) {
        digitalWrite(clockPin, HIGH);
        uint8_t bit = (digitalRead(dataPin) == HIGH) ? 1U : 0U;
        uint8_t bit_index = (bitOrder == LSBFIRST) ? i : (7 - i);
        value |= (uint8_t)(bit << bit_index);
        digitalWrite(clockPin, LOW);
    }

    return value;
}

unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout)
{
    unsigned long start = micros();

    while (digitalRead(pin) == state) {
        if ((micros() - start) >= timeout) {
            return 0;
        }
    }

    while (digitalRead(pin) != state) {
        if ((micros() - start) >= timeout) {
            return 0;
        }
    }

    unsigned long pulse_start = micros();

    while (digitalRead(pin) == state) {
        if ((micros() - start) >= timeout) {
            return 0;
        }
    }

    return micros() - pulse_start;
}

unsigned long pulseInLong(uint8_t pin, uint8_t state, unsigned long timeout)
{
    return pulseIn(pin, state, timeout);
}
