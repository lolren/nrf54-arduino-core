/*
 * XIAO nRF54L15 pin definitions for clean bare-metal core.
 */

#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include <nrf54l15.h>

#define NUM_DIGITAL_PINS 24
#define NUM_ANALOG_INPUTS 8

#define PIN_D0  (0)
#define PIN_D1  (1)
#define PIN_D2  (2)
#define PIN_D3  (3)
#define PIN_D4  (4)
#define PIN_D5  (5)
#define PIN_D6  (6)
#define PIN_D7  (7)
#define PIN_D8  (8)
#define PIN_D9  (9)
#define PIN_D10 (10)
#define PIN_D11 (11)
#define PIN_D12 (12)
#define PIN_D13 (13)
#define PIN_D14 (14)
#define PIN_D15 (15)

#define PIN_LED_BUILTIN (16)
#define PIN_BUTTON      (17)

// Routed to SAMD11 USB bridge on XIAO nRF54L15.
// Net naming is from the SAMD11 perspective:
// - SAMD11_TX drives nRF RX (P1.08)
// - SAMD11_RX receives nRF TX (P1.09)
#define PIN_SAMD11_TX   (19)
#define PIN_SAMD11_RX   (18)

// Board control nets (not on standard D0..D15 header) for XIAO compatibility.
// These are intentionally exposed as digital pins so sketches can use pinMode/digitalWrite.
#define PIN_IMU_MIC_PWR (20)  // P0.01: Sense IMU+MIC power enable
#define PIN_RF_SW       (21)  // P2.03: RF switch power enable
#define PIN_RF_SW_CTL   (22)  // P2.05: RF path select (0=ceramic, 1=external)
#define PIN_VBAT_EN     (23)  // P1.15: VBAT divider enable
#define PIN_VBAT_READ   (PIN_A7)  // P1.14: VBAT divider sense input

// Compatibility aliases used by some sketches/docs.
#define SAMD11_TX PIN_SAMD11_TX
#define SAMD11_RX PIN_SAMD11_RX
#define IMU_MIC PIN_IMU_MIC_PWR
#define IMU_MIC_EN PIN_IMU_MIC_PWR
#define RF_SW PIN_RF_SW
#define RF_SW_CTL PIN_RF_SW_CTL
#define VBAT_EN PIN_VBAT_EN
#define VBAT_READ PIN_VBAT_READ

#define LED_BUILTIN PIN_LED_BUILTIN

#define PIN_A0 (0)
#define PIN_A1 (1)
#define PIN_A2 (2)
#define PIN_A3 (3)
#define PIN_A4 (4)
#define PIN_A5 (5)
#define PIN_A6 (6)
#define PIN_A7 (7)

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

// I2C defaults:
// - Wire  : SDA=D4,  SCL=D5  (XIAO header standard pair)
// - Wire1 : SDA=D12, SCL=D11 (back-pad alternate pair)
#define PIN_WIRE_SDA  (PIN_D4)
#define PIN_WIRE_SCL  (PIN_D5)
#define PIN_WIRE1_SDA (PIN_D12)
#define PIN_WIRE1_SCL (PIN_D11)

#define PIN_SERIAL_TX (PIN_D6)
#define PIN_SERIAL_RX (PIN_D7)
#define PIN_SERIAL1_TX PIN_SERIAL_TX
#define PIN_SERIAL1_RX PIN_SERIAL_RX

#define PIN_SPI_MOSI (PIN_D10)
#define PIN_SPI_MISO (PIN_D9)
#define PIN_SPI_SCK  (PIN_D8)
#define PIN_SPI_SS   (PIN_D2)

// Compatibility helpers used by libraries like Adafruit_BusIO on ARM cores.
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
        case PIN_LED_BUILTIN: *port = 2; *pinInPort = 0; return true;
        case PIN_BUTTON: *port = 0; *pinInPort = 0; return true;
        case PIN_SAMD11_RX: *port = 1; *pinInPort = 9; return true;
        case PIN_SAMD11_TX: *port = 1; *pinInPort = 8; return true;
        case PIN_IMU_MIC_PWR: *port = 0; *pinInPort = 1; return true;
        case PIN_RF_SW: *port = 2; *pinInPort = 3; return true;
        case PIN_RF_SW_CTL: *port = 2; *pinInPort = 5; return true;
        case PIN_VBAT_EN: *port = 1; *pinInPort = 15; return true;
        default: return false;
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
#define digitalPinToInterrupt(p) (p)

static inline uint8_t analogInputToDigitalPin(uint8_t p)
{
    return (p < NUM_ANALOG_INPUTS) ? p : 0xFF;
}

static const uint8_t SDA  = PIN_WIRE_SDA;
static const uint8_t SCL  = PIN_WIRE_SCL;
static const uint8_t SDA1 = PIN_WIRE1_SDA;
static const uint8_t SCL1 = PIN_WIRE1_SCL;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK  = PIN_SPI_SCK;
static const uint8_t SS   = PIN_SPI_SS;
static const uint8_t IMU_MIC_PWR = PIN_IMU_MIC_PWR;
static const uint8_t RF_SW_POWER = PIN_RF_SW;
static const uint8_t RF_SW_SELECT = PIN_RF_SW_CTL;
static const uint8_t VBAT_ENABLE = PIN_VBAT_EN;
static const uint8_t VBAT_SENSE = PIN_VBAT_READ;

#define SERIAL_PORT_MONITOR Serial
#define SERIAL_PORT_USBVIRTUAL Serial
#define SERIAL_PORT_HARDWARE Serial1
#define SERIAL_PORT_HARDWARE1 Serial1
#define SERIAL_PORT_HARDWARE2 Serial2

#define HAVE_HWSERIAL1
#define HAVE_HWSERIAL2

#endif
