/*
 * Nordic PCA10156 / nRF54L15 DK pin definitions.
 *
 * This variant keeps the board-specific aliases for the DK while also exposing
 * a linear GPIO model through _PINNUM(port, pin), so sketches can address the
 * real nRF54L15 GPIO number directly when that is more useful than board-local
 * D aliases.
 */

#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include <nrf54l15.h>

#ifndef _PINNUM
#define _PINNUM(port, pin) ((((port) & 0x7U) * 32U) + ((pin) & 0x1FU))
#endif

#define NUM_DIGITAL_PINS 75
#define NUM_ANALOG_INPUTS 8

/*
 * Raw GPIO aliases by real MCU port/pin. The nRF54L15 package used by this
 * core exposes P0.00..04, P1.02..15, and P2.00..10.
 */
#define PIN_P0_00 _PINNUM(0, 0)
#define PIN_P0_01 _PINNUM(0, 1)
#define PIN_P0_02 _PINNUM(0, 2)
#define PIN_P0_03 _PINNUM(0, 3)
#define PIN_P0_04 _PINNUM(0, 4)

#define PIN_P1_02 _PINNUM(1, 2)
#define PIN_P1_03 _PINNUM(1, 3)
#define PIN_P1_04 _PINNUM(1, 4)
#define PIN_P1_05 _PINNUM(1, 5)
#define PIN_P1_06 _PINNUM(1, 6)
#define PIN_P1_07 _PINNUM(1, 7)
#define PIN_P1_08 _PINNUM(1, 8)
#define PIN_P1_09 _PINNUM(1, 9)
#define PIN_P1_10 _PINNUM(1, 10)
#define PIN_P1_11 _PINNUM(1, 11)
#define PIN_P1_12 _PINNUM(1, 12)
#define PIN_P1_13 _PINNUM(1, 13)
#define PIN_P1_14 _PINNUM(1, 14)
#define PIN_P1_15 _PINNUM(1, 15)

#define PIN_P2_00 _PINNUM(2, 0)
#define PIN_P2_01 _PINNUM(2, 1)
#define PIN_P2_02 _PINNUM(2, 2)
#define PIN_P2_03 _PINNUM(2, 3)
#define PIN_P2_04 _PINNUM(2, 4)
#define PIN_P2_05 _PINNUM(2, 5)
#define PIN_P2_06 _PINNUM(2, 6)
#define PIN_P2_07 _PINNUM(2, 7)
#define PIN_P2_08 _PINNUM(2, 8)
#define PIN_P2_09 _PINNUM(2, 9)
#define PIN_P2_10 _PINNUM(2, 10)

/*
 * DK-oriented D numbering:
 * - D0..D10 follow the P1 expansion header positions 4..14.
 * - D11..D15 expose the on-board P0.00..04 set used by the alternate UART and
 *   SW3/clock-out nets.
 * - D16/D17 keep P1.02/P1.03 available like the generic module variant.
 */
#define PIN_D0  PIN_P1_04
#define PIN_D1  PIN_P1_05
#define PIN_D2  PIN_P1_06
#define PIN_D3  PIN_P1_07
#define PIN_D4  PIN_P1_08
#define PIN_D5  PIN_P1_09
#define PIN_D6  PIN_P1_10
#define PIN_D7  PIN_P1_11
#define PIN_D8  PIN_P1_12
#define PIN_D9  PIN_P1_13
#define PIN_D10 PIN_P1_14
#define PIN_D11 PIN_P0_00
#define PIN_D12 PIN_P0_01
#define PIN_D13 PIN_P0_02
#define PIN_D14 PIN_P0_03
#define PIN_D15 PIN_P0_04
#define PIN_D16 PIN_P1_02
#define PIN_D17 PIN_P1_03

// Header silkscreen helpers so sketches can match the DK labels directly.
#define PIN_HEADER_4  PIN_D0
#define PIN_HEADER_5  PIN_D1
#define PIN_HEADER_6  PIN_D2
#define PIN_HEADER_7  PIN_D3
#define PIN_HEADER_8  PIN_D4
#define PIN_HEADER_9  PIN_D5
#define PIN_HEADER_10 PIN_D6
#define PIN_HEADER_11 PIN_D7
#define PIN_HEADER_12 PIN_D8
#define PIN_HEADER_13 PIN_D9
#define PIN_HEADER_14 PIN_D10

// Board LEDs and buttons from Zephyr's nrf54l15dk DTS.
#define PIN_LED0 PIN_P2_09
#define PIN_LED1 PIN_P1_10
#define PIN_LED2 PIN_P2_07
#define PIN_LED3 PIN_P1_14

#define PIN_BUTTON0 PIN_P1_13
#define PIN_BUTTON1 PIN_P1_09
#define PIN_BUTTON2 PIN_P1_08
#define PIN_BUTTON3 PIN_P0_04

#define PIN_LED_BUILTIN PIN_LED0
#define PIN_BUTTON      PIN_BUTTON0

#define PIN_LED         PIN_LED_BUILTIN
#define LED_BUILTIN     PIN_LED_BUILTIN
// The DK only has green LEDs; keep the common aliases as independent pads.
#define LED_RED         PIN_LED0
#define LED_GREEN       PIN_LED1
#define LED_BLUE        PIN_LED2
#define LED_STATE_ON    HIGH

#ifndef BUTTON_STATE_ON
#define BUTTON_STATE_ON LOW
#endif

/*
 * Alternate serial pair kept under the legacy SAMD11 naming only because the
 * core already uses these macros as the "second" serial route. On the DK this
 * is simply the P0.00/P0.01 UART pair.
 * - PIN_SAMD11_RX is the nRF TX pin
 * - PIN_SAMD11_TX is the nRF RX pin
 */
#define PIN_SAMD11_RX PIN_P0_00
#define PIN_SAMD11_TX PIN_P0_01

// Compatibility aliases reused by existing board-control examples/libraries.
#define PIN_IMU_MIC_PWR PIN_P0_01
#define PIN_RF_SW       PIN_P2_03
#define PIN_RF_SW_CTL   PIN_P2_05
#define PIN_VBAT_EN     PIN_P1_15
#define PIN_IMU_INT     PIN_P0_02
#define PIN_PDM_CLK     PIN_P1_12
#define PIN_A6          PIN_P1_13
#define PIN_A7          PIN_P1_14
#define PIN_PDM_DATA    PIN_A6
#define PIN_VBAT_READ   PIN_A7
#define PIN_VBAT        PIN_VBAT_READ

#define SAMD11_TX PIN_SAMD11_TX
#define SAMD11_RX PIN_SAMD11_RX
#define IMU_MIC PIN_IMU_MIC_PWR
#define IMU_MIC_EN PIN_IMU_MIC_PWR
#define RF_SW PIN_RF_SW
#define RF_SW_CTL PIN_RF_SW_CTL
#define VBAT_EN PIN_VBAT_EN
#define VBAT_READ PIN_VBAT_READ
#define IMU_INT PIN_IMU_INT
#define PDM_CLK PIN_PDM_CLK
#define PDM_DATA PIN_PDM_DATA
#define MIC_CLK PIN_PDM_CLK
#define MIC_DATA PIN_PDM_DATA

// Real analog-capable channel order for the current core SAADC mapping.
#define PIN_A0 PIN_P1_04
#define PIN_A1 PIN_P1_05
#define PIN_A2 PIN_P1_06
#define PIN_A3 PIN_P1_07
#define PIN_A4 PIN_P1_11
#define PIN_A5 PIN_P1_12
// PIN_A6 / PIN_A7 are already declared above as P1.13 / P1.14.

enum {
    A0 = PIN_A0,
    A1 = PIN_A1,
    A2 = PIN_A2,
    A3 = PIN_A3,
    A4 = PIN_A4,
    A5 = PIN_A5,
    A6 = PIN_A6,
    A7 = PIN_A7,
};

/*
 * Default peripheral routes:
 * - Serial  : P1.04/P1.05 matches the DK expansion header/VCOM1 pair.
 * - Serial1 : P0.00/P0.01 exposes the alternate UART pair.
 * - Wire    : P1.10/P1.11 stays on the P1 header domain used by the core.
 * - Wire1   : P0.04/P0.03 mirrors the existing secondary route.
 * - SPI     : keep the known-good core route (P2.02/P2.04/P2.01, SS=P1.06).
 */
#define PIN_WIRE_SDA  PIN_P1_10
#define PIN_WIRE_SCL  PIN_P1_11
#define PIN_WIRE1_SDA PIN_P0_04
#define PIN_WIRE1_SCL PIN_P0_03

#define PIN_SERIAL_TX  PIN_P1_04
#define PIN_SERIAL_RX  PIN_P1_05
#define PIN_SERIAL1_TX PIN_SERIAL_TX
#define PIN_SERIAL1_RX PIN_SERIAL_RX

#define PIN_SPI_MOSI PIN_P2_02
#define PIN_SPI_MISO PIN_P2_04
#define PIN_SPI_SCK  PIN_P2_01
#define PIN_SPI_SS   PIN_P1_06

typedef volatile uint32_t PortReg;
typedef uint32_t PortMask;

static inline bool pinToPortPin(uint8_t pin, uint8_t* port, uint8_t* pinInPort)
{
    if (port == 0 || pinInPort == 0) {
        return false;
    }

    const uint8_t linearPort = pin >> 5;
    const uint8_t linearPin = pin & 0x1FU;

    switch (linearPort) {
        case 0:
            if (linearPin <= 4U) {
                *port = 0U;
                *pinInPort = linearPin;
                return true;
            }
            return false;
        case 1:
            if (linearPin >= 2U && linearPin <= 15U) {
                *port = 1U;
                *pinInPort = linearPin;
                return true;
            }
            return false;
        case 2:
            if (linearPin <= 10U) {
                *port = 2U;
                *pinInPort = linearPin;
                return true;
            }
            return false;
        default:
            return false;
    }
}

static inline int8_t pinToSaadcChannel(uint8_t pin)
{
    switch (pin) {
        case PIN_A0: return 0;
        case PIN_A1: return 1;
        case PIN_A2: return 2;
        case PIN_A3: return 3;
        case PIN_A4: return 4;
        case PIN_A5: return 5;
        case PIN_A6: return 6;
        case PIN_A7: return 7;
        default: return -1;
    }
}

static inline uint8_t digitalPinToPort(uint8_t pin)
{
    uint8_t port = 0;
    uint8_t pinInPort = 0;
    return pinToPortPin(pin, &port, &pinInPort) ? port : 0xFF;
}

static inline uint32_t digitalPinToBitMask(uint8_t pin)
{
    uint8_t port = 0;
    uint8_t pinInPort = 0;
    (void)port;
    return pinToPortPin(pin, &port, &pinInPort) ? (1UL << pinInPort) : 0UL;
}

static inline volatile uint32_t* portOutputRegister(uint8_t port)
{
    switch (port) {
        case 0: return &NRF_P0->OUT;
        case 1: return &NRF_P1->OUT;
        case 2: return &NRF_P2->OUT;
        default: return (volatile uint32_t*)0;
    }
}

static inline volatile uint32_t* portInputRegister(uint8_t port)
{
    switch (port) {
        case 0: return (volatile uint32_t*)&NRF_P0->IN;
        case 1: return (volatile uint32_t*)&NRF_P1->IN;
        case 2: return (volatile uint32_t*)&NRF_P2->IN;
        default: return (volatile uint32_t*)0;
    }
}

static inline volatile uint32_t* portModeRegister(uint8_t port)
{
    switch (port) {
        case 0: return &NRF_P0->DIR;
        case 1: return &NRF_P1->DIR;
        case 2: return &NRF_P2->DIR;
        default: return (volatile uint32_t*)0;
    }
}

#define digitalPinHasPWM(p) (digitalPinToPort((p)) == 1U)

#ifndef NOT_AN_INTERRUPT
#define NOT_AN_INTERRUPT 0xFF
#endif

static inline int digitalPinToInterrupt(uint8_t pin)
{
    uint8_t port = 0;
    uint8_t pinInPort = 0;
    if (!pinToPortPin(pin, &port, &pinInPort) || port == 2U) {
        return NOT_AN_INTERRUPT;
    }
    return pin;
}

static inline uint8_t analogInputToDigitalPin(uint8_t p)
{
    switch (p) {
        case 0: return PIN_A0;
        case 1: return PIN_A1;
        case 2: return PIN_A2;
        case 3: return PIN_A3;
        case 4: return PIN_A4;
        case 5: return PIN_A5;
        case 6: return PIN_A6;
        case 7: return PIN_A7;
        default: return 0xFF;
    }
}

static const uint8_t P0_00 = PIN_P0_00;
static const uint8_t P0_01 = PIN_P0_01;
static const uint8_t P0_02 = PIN_P0_02;
static const uint8_t P0_03 = PIN_P0_03;
static const uint8_t P0_04 = PIN_P0_04;

static const uint8_t P1_02 = PIN_P1_02;
static const uint8_t P1_03 = PIN_P1_03;
static const uint8_t P1_04 = PIN_P1_04;
static const uint8_t P1_05 = PIN_P1_05;
static const uint8_t P1_06 = PIN_P1_06;
static const uint8_t P1_07 = PIN_P1_07;
static const uint8_t P1_08 = PIN_P1_08;
static const uint8_t P1_09 = PIN_P1_09;
static const uint8_t P1_10 = PIN_P1_10;
static const uint8_t P1_11 = PIN_P1_11;
static const uint8_t P1_12 = PIN_P1_12;
static const uint8_t P1_13 = PIN_P1_13;
static const uint8_t P1_14 = PIN_P1_14;
static const uint8_t P1_15 = PIN_P1_15;

static const uint8_t P2_00 = PIN_P2_00;
static const uint8_t P2_01 = PIN_P2_01;
static const uint8_t P2_02 = PIN_P2_02;
static const uint8_t P2_03 = PIN_P2_03;
static const uint8_t P2_04 = PIN_P2_04;
static const uint8_t P2_05 = PIN_P2_05;
static const uint8_t P2_06 = PIN_P2_06;
static const uint8_t P2_07 = PIN_P2_07;
static const uint8_t P2_08 = PIN_P2_08;
static const uint8_t P2_09 = PIN_P2_09;
static const uint8_t P2_10 = PIN_P2_10;

static const uint8_t SDA  = PIN_WIRE_SDA;
static const uint8_t SCL  = PIN_WIRE_SCL;
static const uint8_t SDA1 = PIN_WIRE1_SDA;
static const uint8_t SCL1 = PIN_WIRE1_SCL;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK  = PIN_SPI_SCK;
static const uint8_t SS   = PIN_SPI_SS;

static const uint8_t LED0_PAD = PIN_LED0;
static const uint8_t LED1_PAD = PIN_LED1;
static const uint8_t LED2_PAD = PIN_LED2;
static const uint8_t LED3_PAD = PIN_LED3;
static const uint8_t BUTTON0_PAD = PIN_BUTTON0;
static const uint8_t BUTTON1_PAD = PIN_BUTTON1;
static const uint8_t BUTTON2_PAD = PIN_BUTTON2;
static const uint8_t BUTTON3_PAD = PIN_BUTTON3;

#define SERIAL_PORT_MONITOR Serial
#define SERIAL_PORT_USBVIRTUAL Serial
#define SERIAL_PORT_HARDWARE Serial1
#define SERIAL_PORT_HARDWARE1 Serial1
#define SERIAL_PORT_HARDWARE2 Serial2

#define HAVE_HWSERIAL1
#define HAVE_HWSERIAL2

#endif
