/*
 * Generic 36-pad nRF54L15 module pin definitions.
 *
 * Arduino numbering intentionally mirrors the XIAO nRF54L15 mapping for the
 * shared pins so sketches written for XIAO stay source-compatible on the bare
 * module boards. Only the genuinely extra module GPIOs are added as D16/D17.
 */

#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include <nrf54l15.h>

#define NUM_DIGITAL_PINS 30
#define NUM_ANALOG_INPUTS 8

// XIAO-compatible Arduino D numbering on the shared GPIO set.
#define PIN_D0  (0)   /* P1.04 pad 12 */
#define PIN_D1  (1)   /* P1.05 pad 13 */
#define PIN_D2  (2)   /* P1.06 pad 14 */
#define PIN_D3  (3)   /* P1.07 pad 15 */
#define PIN_D4  (4)   /* P1.10 pad 3  */
#define PIN_D5  (5)   /* P1.11 pad 5  */
#define PIN_D6  (6)   /* P2.08 pad 25 */
#define PIN_D7  (7)   /* P2.07 pad 24 */
#define PIN_D8  (8)   /* P2.01 pad 18 */
#define PIN_D9  (9)   /* P2.04 pad 21 */
#define PIN_D10 (10)  /* P2.02 pad 19 */
#define PIN_D11 (11)  /* P0.03 pad 33 */
#define PIN_D12 (12)  /* P0.04 pad 34 */
#define PIN_D13 (13)  /* P2.10 pad 27 */
#define PIN_D14 (14)  /* P2.09 pad 26 */
#define PIN_D15 (15)  /* P2.06 pad 23 */

// Module-only extra exposed GPIOs not present on the XIAO header map.
#define PIN_D16 (28)  /* P1.02 pad 10 */
#define PIN_D17 (29)  /* P1.03 pad 11 */

#define PIN_LED_BUILTIN (16)  /* P2.00 pad 17 */
#define PIN_BUTTON      (17)  /* P0.00 pad 28 */

#define PIN_LED         PIN_LED_BUILTIN
#define LED_BUILTIN     PIN_LED_BUILTIN
#define LED_RED         PIN_LED_BUILTIN
#define LED_GREEN       PIN_LED_BUILTIN
#define LED_BLUE        PIN_LED_BUILTIN
#define LED_STATE_ON    HIGH

// Kept XIAO-compatible even on module boards so XIAO sketches compile.
#define PIN_SAMD11_RX   (18)  /* P1.09 pad 2  */
#define PIN_SAMD11_TX   (19)  /* P1.08 pad 16 */
#define PIN_IMU_MIC_PWR (20)  /* P0.01 pad 29 */
#define PIN_RF_SW       (21)  /* P2.03 pad 20 */
#define PIN_RF_SW_CTL   (22)  /* P2.05 pad 22 */
#define PIN_VBAT_EN     (23)  /* not exposed on the module */
#define PIN_IMU_INT     (24)  /* P0.02 pad 32 */
#define PIN_PDM_CLK     (25)  /* P1.12 pad 6  */
#define PIN_A6          (26)  /* P1.13 pad 7  */
#define PIN_A7          (27)  /* P1.14 pad 8  */
#define PIN_PDM_DATA    (PIN_A6)
#define PIN_VBAT_READ   (PIN_A7)
#define PIN_VBAT        PIN_VBAT_READ

// Extra GPIO aliases by real MCU pin name.
#define PIN_P1_04 PIN_D0
#define PIN_P1_05 PIN_D1
#define PIN_P1_06 PIN_D2
#define PIN_P1_07 PIN_D3
#define PIN_P1_10 PIN_D4
#define PIN_P1_11 PIN_D5
#define PIN_P2_08 PIN_D6
#define PIN_P2_07 PIN_D7
#define PIN_P2_01 PIN_D8
#define PIN_P2_04 PIN_D9
#define PIN_P2_02 PIN_D10
#define PIN_P0_03 PIN_D11
#define PIN_P0_04 PIN_D12
#define PIN_P2_10 PIN_D13
#define PIN_P2_09 PIN_D14
#define PIN_P2_06 PIN_D15
#define PIN_P1_02 PIN_D16
#define PIN_P1_03 PIN_D17
#define PIN_P2_00 PIN_LED_BUILTIN
#define PIN_P0_00 PIN_BUTTON
#define PIN_P1_09 PIN_SAMD11_RX
#define PIN_P1_08 PIN_SAMD11_TX
#define PIN_P0_01 PIN_IMU_MIC_PWR
#define PIN_P2_03 PIN_RF_SW
#define PIN_P2_05 PIN_RF_SW_CTL
#define PIN_P0_02 PIN_IMU_INT
#define PIN_P1_12 PIN_PDM_CLK
#define PIN_P1_13 PIN_A6
#define PIN_P1_14 PIN_A7

// Compatibility aliases used by some sketches/docs.
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

#define PIN_A0 (PIN_D0)
#define PIN_A1 (PIN_D1)
#define PIN_A2 (PIN_D2)
#define PIN_A3 (PIN_D3)
#define PIN_A4 (PIN_D4)
#define PIN_A5 (PIN_D5)

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

// Match the XIAO default peripheral routes so sketches stay source-compatible.
#define PIN_WIRE_SDA  (PIN_D4)
#define PIN_WIRE_SCL  (PIN_D5)
#define PIN_WIRE1_SDA (PIN_D12)
#define PIN_WIRE1_SCL (PIN_D11)

#define PIN_SERIAL_TX  (PIN_D6)
#define PIN_SERIAL_RX  (PIN_D7)
#define PIN_SERIAL1_TX PIN_SERIAL_TX
#define PIN_SERIAL1_RX PIN_SERIAL_RX

#define PIN_SPI_MOSI (PIN_D10)
#define PIN_SPI_MISO (PIN_D9)
#define PIN_SPI_SCK  (PIN_D8)
#define PIN_SPI_SS   (PIN_D2)

typedef volatile uint32_t PortReg;
typedef uint32_t PortMask;

static inline bool pinToPortPin(uint8_t pin, uint8_t* port, uint8_t* pinInPort)
{
    if (port == 0 || pinInPort == 0) {
        return false;
    }

    switch (pin) {
        case PIN_D0: *port = 1; *pinInPort = 4; return true;
        case PIN_D1: *port = 1; *pinInPort = 5; return true;
        case PIN_D2: *port = 1; *pinInPort = 6; return true;
        case PIN_D3: *port = 1; *pinInPort = 7; return true;
        case PIN_D4: *port = 1; *pinInPort = 10; return true;
        case PIN_D5: *port = 1; *pinInPort = 11; return true;
        case PIN_D6: *port = 2; *pinInPort = 8; return true;
        case PIN_D7: *port = 2; *pinInPort = 7; return true;
        case PIN_D8: *port = 2; *pinInPort = 1; return true;
        case PIN_D9: *port = 2; *pinInPort = 4; return true;
        case PIN_D10: *port = 2; *pinInPort = 2; return true;
        case PIN_D11: *port = 0; *pinInPort = 3; return true;
        case PIN_D12: *port = 0; *pinInPort = 4; return true;
        case PIN_D13: *port = 2; *pinInPort = 10; return true;
        case PIN_D14: *port = 2; *pinInPort = 9; return true;
        case PIN_D15: *port = 2; *pinInPort = 6; return true;
        case PIN_D16: *port = 1; *pinInPort = 2; return true;
        case PIN_D17: *port = 1; *pinInPort = 3; return true;
        case PIN_LED_BUILTIN: *port = 2; *pinInPort = 0; return true;
        case PIN_BUTTON: *port = 0; *pinInPort = 0; return true;
        case PIN_SAMD11_RX: *port = 1; *pinInPort = 9; return true;
        case PIN_SAMD11_TX: *port = 1; *pinInPort = 8; return true;
        case PIN_IMU_MIC_PWR: *port = 0; *pinInPort = 1; return true;
        case PIN_IMU_INT: *port = 0; *pinInPort = 2; return true;
        case PIN_PDM_CLK: *port = 1; *pinInPort = 12; return true;
        case PIN_A6: *port = 1; *pinInPort = 13; return true;
        case PIN_A7: *port = 1; *pinInPort = 14; return true;
        case PIN_RF_SW: *port = 2; *pinInPort = 3; return true;
        case PIN_RF_SW_CTL: *port = 2; *pinInPort = 5; return true;
        default: return false;
    }
}

static inline int8_t pinToSaadcChannel(uint8_t pin)
{
    switch (pin) {
        case PIN_A0: return 0;
        case PIN_A1: return 1;
        case PIN_A2: return 2;
        case PIN_A3: return 3;
        case PIN_A5: return 4;
        case PIN_PDM_CLK: return 5;
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

#define digitalPinHasPWM(p) ((p) <= PIN_D9)

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

static const uint8_t P1_04 = PIN_P1_04;
static const uint8_t P1_05 = PIN_P1_05;
static const uint8_t P1_06 = PIN_P1_06;
static const uint8_t P1_07 = PIN_P1_07;
static const uint8_t P1_10 = PIN_P1_10;
static const uint8_t P1_11 = PIN_P1_11;
static const uint8_t P2_08 = PIN_P2_08;
static const uint8_t P2_07 = PIN_P2_07;
static const uint8_t P2_01 = PIN_P2_01;
static const uint8_t P2_04 = PIN_P2_04;
static const uint8_t P2_02 = PIN_P2_02;
static const uint8_t P0_03 = PIN_P0_03;
static const uint8_t P0_04 = PIN_P0_04;
static const uint8_t P2_10 = PIN_P2_10;
static const uint8_t P2_09 = PIN_P2_09;
static const uint8_t P2_06 = PIN_P2_06;
static const uint8_t P1_02 = PIN_P1_02;
static const uint8_t P1_03 = PIN_P1_03;
static const uint8_t P2_00 = PIN_P2_00;
static const uint8_t P0_00 = PIN_P0_00;
static const uint8_t P1_09 = PIN_P1_09;
static const uint8_t P1_08 = PIN_P1_08;
static const uint8_t P0_01 = PIN_P0_01;
static const uint8_t P2_03 = PIN_P2_03;
static const uint8_t P2_05 = PIN_P2_05;
static const uint8_t P0_02 = PIN_P0_02;
static const uint8_t P1_12 = PIN_P1_12;
static const uint8_t P1_13 = PIN_P1_13;
static const uint8_t P1_14 = PIN_P1_14;

static const uint8_t SDA  = PIN_WIRE_SDA;
static const uint8_t SCL  = PIN_WIRE_SCL;
static const uint8_t SDA1 = PIN_WIRE1_SDA;
static const uint8_t SCL1 = PIN_WIRE1_SCL;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK  = PIN_SPI_SCK;
static const uint8_t SS   = PIN_SPI_SS;
static const uint8_t IMU_MIC_PWR = PIN_IMU_MIC_PWR;
static const uint8_t IMU_INT1 = PIN_IMU_INT;
static const uint8_t MIC_CLK_PAD = PIN_PDM_CLK;
static const uint8_t MIC_DATA_PAD = PIN_PDM_DATA;
static const uint8_t RF_SW_POWER = PIN_RF_SW;
static const uint8_t RF_SW_SELECT = PIN_RF_SW_CTL;

#define SERIAL_PORT_MONITOR Serial
#define SERIAL_PORT_USBVIRTUAL Serial
#define SERIAL_PORT_HARDWARE Serial1
#define SERIAL_PORT_HARDWARE1 Serial1
#define SERIAL_PORT_HARDWARE2 Serial2

#define HAVE_HWSERIAL1
#define HAVE_HWSERIAL2

#endif
