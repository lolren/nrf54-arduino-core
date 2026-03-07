#pragma once

#include <stdint.h>
#include <nrf54l15.h>

namespace nrf54l15 {

inline volatile uint32_t& reg32(uint32_t addr) {
  return *reinterpret_cast<volatile uint32_t*>(addr);
}

// Use the Arduino core address selection (secure/non-secure) from nrf54l15.h.
constexpr uint32_t GPIO_P0_BASE = static_cast<uint32_t>(NRF_P0_BASE);
constexpr uint32_t GPIO_P1_BASE = static_cast<uint32_t>(NRF_P1_BASE);
constexpr uint32_t GPIO_P2_BASE = static_cast<uint32_t>(NRF_P2_BASE);

constexpr uint32_t SPIM20_BASE = static_cast<uint32_t>(NRF_SPIM20_BASE);
constexpr uint32_t TWIM20_BASE = static_cast<uint32_t>(NRF_TWIM20_BASE);
constexpr uint32_t UARTE20_BASE = static_cast<uint32_t>(NRF_UARTE20_BASE);
constexpr uint32_t TWIM21_BASE = static_cast<uint32_t>(NRF_TWIM21_BASE);
constexpr uint32_t UARTE21_BASE = static_cast<uint32_t>(NRF_UARTE21_BASE);
// SPIM and TWIM/UARTE share serial fabric instances on this SoC family.
constexpr uint32_t SPIM21_BASE = static_cast<uint32_t>(NRF_TWIM21_BASE);
constexpr uint32_t SAADC_BASE = static_cast<uint32_t>(NRF_SAADC_BASE);
constexpr uint32_t PWM20_BASE = static_cast<uint32_t>(NRF_PWM20_BASE);
constexpr uint32_t OSCILLATORS_BASE = static_cast<uint32_t>(NRF_OSCILLATORS_BASE);
constexpr uint32_t FICR_BASE = 0x00FFC000UL;

#ifdef NRF_TRUSTZONE_NONSECURE
constexpr uint32_t SPIM00_BASE = 0x4004A000UL;
constexpr uint32_t TWIM22_BASE = 0x400C8000UL;
constexpr uint32_t TWIM30_BASE = 0x40104000UL;
constexpr uint32_t TIMER20_BASE = 0x400CA000UL;
constexpr uint32_t GPIOTE20_BASE = 0x400DA000UL;
constexpr uint32_t GPIOTE30_BASE = 0x4010C000UL;
constexpr uint32_t PDM20_BASE = 0x400D0000UL;
constexpr uint32_t TEMP_BASE = 0x400D7000UL;
constexpr uint32_t RADIO_BASE = 0x4008A000UL;
constexpr uint32_t GRTC_BASE = 0x400E2000UL;
constexpr uint32_t WDT31_BASE = 0x40109000UL;
constexpr uint32_t POWER_BASE = 0x4010E000UL;
constexpr uint32_t RESET_BASE = 0x4010E000UL;
constexpr uint32_t REGULATORS_BASE = 0x40120000UL;
#else
constexpr uint32_t SPIM00_BASE = 0x5004A000UL;
constexpr uint32_t TWIM22_BASE = 0x500C8000UL;
constexpr uint32_t TWIM30_BASE = 0x50104000UL;
constexpr uint32_t TIMER20_BASE = 0x500CA000UL;
constexpr uint32_t GPIOTE20_BASE = 0x500DA000UL;
constexpr uint32_t GPIOTE30_BASE = 0x5010C000UL;
constexpr uint32_t PDM20_BASE = 0x500D0000UL;
constexpr uint32_t TEMP_BASE = 0x500D7000UL;
constexpr uint32_t RADIO_BASE = 0x5008A000UL;
constexpr uint32_t GRTC_BASE = 0x500E2000UL;
constexpr uint32_t WDT31_BASE = 0x50109000UL;
constexpr uint32_t POWER_BASE = 0x5010E000UL;
constexpr uint32_t RESET_BASE = 0x5010E000UL;
constexpr uint32_t REGULATORS_BASE = 0x50120000UL;
#endif

constexpr uint32_t PSEL_DISCONNECTED = 0xFFFFFFFFUL;

inline constexpr uint32_t make_psel(uint8_t port, uint8_t pin) {
  return (static_cast<uint32_t>(pin) & 0x1FUL) |
         ((static_cast<uint32_t>(port) & 0x7UL) << 5U);
}

namespace gpio {
constexpr uint32_t OUT = 0x000;
constexpr uint32_t OUTSET = 0x004;
constexpr uint32_t OUTCLR = 0x008;
constexpr uint32_t IN = 0x00C;
constexpr uint32_t DIR = 0x010;
constexpr uint32_t DIRSET = 0x014;
constexpr uint32_t DIRCLR = 0x018;
constexpr uint32_t PIN_CNF = 0x080;

// PIN_CNF bit fields (from GPIO.PIN_CNF[n] table).
constexpr uint32_t PIN_CNF_DIR_Pos = 0;
constexpr uint32_t PIN_CNF_INPUT_Pos = 1;
constexpr uint32_t PIN_CNF_PULL_Pos = 2;
constexpr uint32_t PIN_CNF_DRIVE0_Pos = 8;
constexpr uint32_t PIN_CNF_DRIVE1_Pos = 10;
constexpr uint32_t PIN_CNF_SENSE_Pos = 16;
constexpr uint32_t PIN_CNF_CTRLSEL_Pos = 28;

constexpr uint32_t PIN_CNF_DIR_Msk = 0x1UL << PIN_CNF_DIR_Pos;
constexpr uint32_t PIN_CNF_INPUT_Msk = 0x1UL << PIN_CNF_INPUT_Pos;
constexpr uint32_t PIN_CNF_PULL_Msk = 0x3UL << PIN_CNF_PULL_Pos;
constexpr uint32_t PIN_CNF_DRIVE0_Msk = 0x3UL << PIN_CNF_DRIVE0_Pos;
constexpr uint32_t PIN_CNF_DRIVE1_Msk = 0x3UL << PIN_CNF_DRIVE1_Pos;

constexpr uint32_t PULL_DISABLED = 0;
constexpr uint32_t PULL_DOWN = 1;
constexpr uint32_t PULL_UP = 3;

constexpr uint32_t DRIVE0_S0 = 0;
constexpr uint32_t DRIVE1_D1 = 2;
}  // namespace gpio

namespace spim {
constexpr uint32_t TASKS_START = 0x000;
constexpr uint32_t TASKS_STOP = 0x004;
constexpr uint32_t EVENTS_STARTED = 0x100;
constexpr uint32_t EVENTS_STOPPED = 0x104;
constexpr uint32_t EVENTS_END = 0x108;
constexpr uint32_t EVENTS_DMA_RX_END = 0x14C;
constexpr uint32_t EVENTS_DMA_TX_END = 0x168;

constexpr uint32_t ENABLE = 0x500;
constexpr uint32_t PRESCALER = 0x52C;
constexpr uint32_t CONFIG = 0x554;
constexpr uint32_t ORC = 0x5C0;

constexpr uint32_t PSEL_SCK = 0x600;
constexpr uint32_t PSEL_MOSI = 0x604;
constexpr uint32_t PSEL_MISO = 0x608;
constexpr uint32_t PSEL_DCX = 0x60C;
constexpr uint32_t PSEL_CSN = 0x610;

constexpr uint32_t DMA_RX_PTR = 0x704;
constexpr uint32_t DMA_RX_MAXCNT = 0x708;
constexpr uint32_t DMA_TX_PTR = 0x73C;
constexpr uint32_t DMA_TX_MAXCNT = 0x740;

constexpr uint32_t ENABLE_DISABLED = 0;
constexpr uint32_t ENABLE_ENABLED = 7;

constexpr uint32_t CONFIG_ORDER_LSB_FIRST = 1U << 0;
constexpr uint32_t CONFIG_CPHA_TRAILING = 1U << 1;
constexpr uint32_t CONFIG_CPOL_ACTIVE_LOW = 1U << 2;
}  // namespace spim

namespace twim {
constexpr uint32_t TASKS_STOP = 0x004;
constexpr uint32_t TASKS_DMA_RX_START = 0x028;
constexpr uint32_t TASKS_DMA_TX_START = 0x050;

constexpr uint32_t EVENTS_STOPPED = 0x104;
constexpr uint32_t EVENTS_ERROR = 0x114;
constexpr uint32_t EVENTS_LASTRX = 0x134;
constexpr uint32_t EVENTS_LASTTX = 0x138;
constexpr uint32_t EVENTS_DMA_RX_END = 0x14C;
constexpr uint32_t EVENTS_DMA_TX_END = 0x168;

constexpr uint32_t ERRORSRC = 0x4C4;
constexpr uint32_t ENABLE = 0x500;
constexpr uint32_t FREQUENCY = 0x524;
constexpr uint32_t ADDRESS = 0x588;

constexpr uint32_t PSEL_SCL = 0x600;
constexpr uint32_t PSEL_SDA = 0x604;

constexpr uint32_t DMA_RX_PTR = 0x704;
constexpr uint32_t DMA_RX_MAXCNT = 0x708;
constexpr uint32_t DMA_TX_PTR = 0x73C;
constexpr uint32_t DMA_TX_MAXCNT = 0x740;

constexpr uint32_t ENABLE_DISABLED = 0;
constexpr uint32_t ENABLE_ENABLED = 6;

constexpr uint32_t FREQUENCY_100K = 0x01980000UL;
constexpr uint32_t FREQUENCY_250K = 0x04000000UL;
constexpr uint32_t FREQUENCY_400K = 0x06400000UL;
constexpr uint32_t FREQUENCY_1000K = 0x0FF00000UL;

constexpr uint32_t ERRORSRC_ALL = 0x7UL;
}  // namespace twim

namespace uarte {
constexpr uint32_t TASKS_DMA_RX_START = 0x028;
constexpr uint32_t TASKS_DMA_RX_STOP = 0x02C;
constexpr uint32_t TASKS_DMA_TX_START = 0x050;
constexpr uint32_t TASKS_DMA_TX_STOP = 0x054;

constexpr uint32_t EVENTS_ERROR = 0x114;
constexpr uint32_t EVENTS_RXTO = 0x124;
constexpr uint32_t EVENTS_TXSTOPPED = 0x130;
constexpr uint32_t EVENTS_DMA_RX_END = 0x14C;
constexpr uint32_t EVENTS_DMA_TX_END = 0x168;

constexpr uint32_t ERRORSRC = 0x480;
constexpr uint32_t ENABLE = 0x500;
constexpr uint32_t BAUDRATE = 0x524;
constexpr uint32_t CONFIG = 0x56C;

constexpr uint32_t PSEL_TXD = 0x604;
constexpr uint32_t PSEL_CTS = 0x608;
constexpr uint32_t PSEL_RXD = 0x60C;
constexpr uint32_t PSEL_RTS = 0x610;

constexpr uint32_t DMA_RX_PTR = 0x704;
constexpr uint32_t DMA_RX_MAXCNT = 0x708;
constexpr uint32_t DMA_RX_AMOUNT = 0x70C;
constexpr uint32_t DMA_TX_PTR = 0x73C;
constexpr uint32_t DMA_TX_MAXCNT = 0x740;

constexpr uint32_t ENABLE_DISABLED = 0;
constexpr uint32_t ENABLE_ENABLED = 8;

constexpr uint32_t BAUD_9600 = 0x00275000UL;
constexpr uint32_t BAUD_115200 = 0x01D60000UL;
constexpr uint32_t BAUD_1M = 0x10000000UL;

constexpr uint32_t CONFIG_HWFC_Pos = 0;
constexpr uint32_t CONFIG_HWFC_Msk = 0x1UL << CONFIG_HWFC_Pos;

constexpr uint32_t ERRORSRC_ALL = 0xFUL;
}  // namespace uarte

namespace saadc {
constexpr uint32_t TASKS_START = 0x000;
constexpr uint32_t TASKS_SAMPLE = 0x004;
constexpr uint32_t TASKS_STOP = 0x008;
constexpr uint32_t TASKS_CALIBRATEOFFSET = 0x00C;

constexpr uint32_t EVENTS_STARTED = 0x100;
constexpr uint32_t EVENTS_END = 0x104;
constexpr uint32_t EVENTS_CALIBRATEDONE = 0x110;
constexpr uint32_t EVENTS_STOPPED = 0x114;

constexpr uint32_t ENABLE = 0x500;
constexpr uint32_t CH_PSELP = 0x510;
constexpr uint32_t CH_PSELN = 0x514;
constexpr uint32_t CH_CONFIG = 0x518;
constexpr uint32_t CH_STRIDE = 0x10;

constexpr uint32_t RESOLUTION = 0x5F0;
constexpr uint32_t OVERSAMPLE = 0x5F4;
constexpr uint32_t SAMPLERATE = 0x5F8;

constexpr uint32_t RESULT_PTR = 0x62C;
constexpr uint32_t RESULT_MAXCNT = 0x630;
constexpr uint32_t RESULT_AMOUNT = 0x634;

constexpr uint32_t NOISESHAPE = 0x654;

constexpr uint32_t ENABLE_DISABLED = 0;
constexpr uint32_t ENABLE_ENABLED = 1;

// CH[n].PSELx fields.
constexpr uint32_t CH_PSEL_PIN_Pos = 0;
constexpr uint32_t CH_PSEL_PORT_Pos = 8;
constexpr uint32_t CH_PSEL_INTERNAL_Pos = 12;
constexpr uint32_t CH_PSEL_CONNECT_Pos = 30;

constexpr uint32_t CH_PSEL_CONNECT_NC = 0;
constexpr uint32_t CH_PSEL_CONNECT_ANALOG = 1;
constexpr uint32_t CH_PSEL_CONNECT_INTERNAL = 2;

// CH[n].CONFIG fields.
constexpr uint32_t CH_CONFIG_GAIN_Pos = 8;
constexpr uint32_t CH_CONFIG_BURST_Pos = 11;
constexpr uint32_t CH_CONFIG_REFSEL_Pos = 12;
constexpr uint32_t CH_CONFIG_MODE_Pos = 15;
constexpr uint32_t CH_CONFIG_TACQ_Pos = 16;
constexpr uint32_t CH_CONFIG_TCONV_Pos = 28;

constexpr uint32_t REFSEL_INTERNAL = 0;
constexpr uint32_t MODE_SINGLE_ENDED = 0;

// SAMPLERATE.MODE field.
constexpr uint32_t SAMPLERATE_MODE_Pos = 31;
constexpr uint32_t SAMPLERATE_MODE_TASK = 0;
constexpr uint32_t SAMPLERATE_MODE_TIMER = 1;

constexpr uint32_t OVERSAMPLE_BYPASS = 0;
constexpr uint32_t NOISESHAPE_DISABLED = 0;
}  // namespace saadc

namespace timer {
constexpr uint32_t TASKS_START = 0x000;
constexpr uint32_t TASKS_STOP = 0x004;
constexpr uint32_t TASKS_COUNT = 0x008;
constexpr uint32_t TASKS_CLEAR = 0x00C;
constexpr uint32_t TASKS_CAPTURE = 0x040;

constexpr uint32_t EVENTS_COMPARE = 0x140;

constexpr uint32_t SHORTS = 0x200;
constexpr uint32_t INTENSET = 0x304;
constexpr uint32_t INTENCLR = 0x308;

constexpr uint32_t MODE = 0x504;
constexpr uint32_t BITMODE = 0x508;
constexpr uint32_t PRESCALER = 0x510;
constexpr uint32_t CC = 0x540;
constexpr uint32_t ONESHOTEN = 0x580;

constexpr uint32_t MODE_TIMER = 0;
constexpr uint32_t MODE_COUNTER = 1;

constexpr uint32_t BITMODE_16 = 0;
constexpr uint32_t BITMODE_8 = 1;
constexpr uint32_t BITMODE_24 = 2;
constexpr uint32_t BITMODE_32 = 3;
}  // namespace timer

namespace pwm {
constexpr uint32_t TASKS_STOP = 0x004;
constexpr uint32_t TASKS_NEXTSTEP = 0x008;
constexpr uint32_t TASKS_DMA_SEQ_START = 0x010;
constexpr uint32_t TASKS_DMA_SEQ_STOP = 0x014;

constexpr uint32_t EVENTS_STOPPED = 0x104;
constexpr uint32_t EVENTS_SEQSTARTED = 0x108;
constexpr uint32_t EVENTS_SEQEND = 0x110;
constexpr uint32_t EVENTS_PWMPERIODEND = 0x118;
constexpr uint32_t EVENTS_LOOPSDONE = 0x11C;
constexpr uint32_t EVENTS_RAMUNDERFLOW = 0x120;
constexpr uint32_t EVENTS_DMA_SEQ_END = 0x124;

constexpr uint32_t SHORTS = 0x200;
constexpr uint32_t ENABLE = 0x500;
constexpr uint32_t MODE = 0x504;
constexpr uint32_t COUNTERTOP = 0x508;
constexpr uint32_t PRESCALER = 0x50C;
constexpr uint32_t DECODER = 0x510;
constexpr uint32_t LOOP = 0x514;
constexpr uint32_t IDLEOUT = 0x518;
constexpr uint32_t SEQ_REFRESH = 0x528;
constexpr uint32_t SEQ_ENDDELAY = 0x52C;
constexpr uint32_t PSEL_OUT = 0x560;
constexpr uint32_t DMA_SEQ_PTR = 0x704;
constexpr uint32_t DMA_SEQ_MAXCNT = 0x708;

constexpr uint32_t ENABLE_DISABLED = 0;
constexpr uint32_t ENABLE_ENABLED = 1;

constexpr uint32_t MODE_UP = 0;
constexpr uint32_t MODE_UPDOWN = 1;

constexpr uint32_t DECODER_LOAD_COMMON = 0;
constexpr uint32_t DECODER_LOAD_GROUPED = 1;
constexpr uint32_t DECODER_LOAD_INDIVIDUAL = 2;
constexpr uint32_t DECODER_LOAD_WAVEFORM = 3;

constexpr uint32_t DECODER_MODE_REFRESHCOUNT = 0;
constexpr uint32_t DECODER_MODE_NEXTSTEP = 1;
}  // namespace pwm

namespace gpiote {
constexpr uint32_t TASKS_OUT = 0x000;
constexpr uint32_t TASKS_SET = 0x030;
constexpr uint32_t TASKS_CLR = 0x060;

constexpr uint32_t EVENTS_IN = 0x100;
constexpr uint32_t EVENTS_PORT_NONSECURE = 0x140;

constexpr uint32_t INTENSET0 = 0x304;
constexpr uint32_t INTENCLR0 = 0x308;

constexpr uint32_t CONFIG = 0x510;

constexpr uint32_t CONFIG_MODE_Pos = 0;
constexpr uint32_t CONFIG_PSEL_Pos = 4;
constexpr uint32_t CONFIG_PORT_Pos = 9;
constexpr uint32_t CONFIG_POLARITY_Pos = 16;
constexpr uint32_t CONFIG_OUTINIT_Pos = 20;

constexpr uint32_t MODE_DISABLED = 0;
constexpr uint32_t MODE_EVENT = 1;
constexpr uint32_t MODE_TASK = 3;

constexpr uint32_t POLARITY_NONE = 0;
constexpr uint32_t POLARITY_LOTOHI = 1;
constexpr uint32_t POLARITY_HITOLO = 2;
constexpr uint32_t POLARITY_TOGGLE = 3;
}  // namespace gpiote

}  // namespace nrf54l15
