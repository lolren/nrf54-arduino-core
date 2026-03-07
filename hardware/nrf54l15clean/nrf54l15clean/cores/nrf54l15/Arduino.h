/*
 * Arduino Header for nRF54L15 - Clean Bare-Metal Core
 *
 * This header exposes Arduino-compatible APIs for the nRF54L15 using
 * register-level peripheral control.
 */

#ifndef Arduino_h
#define Arduino_h

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <avr/pgmspace.h>

// Legacy Arduino binary constants (B00000000 style).
#include "binary.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration for SysTick initialization
void initSysTick(void);

// Arduino type definitions
typedef bool boolean;
typedef uint8_t byte;
typedef unsigned int word;
typedef uint8_t pin_size_t;
typedef uint8_t BitOrder;

// Arduino pin constants
#define HIGH 0x1
#define LOW  0x0

// Pin modes
#define INPUT           0x0
#define OUTPUT          0x1
#define INPUT_PULLUP    0x2
#define INPUT_PULLDOWN  0x3

// Bit order
#define LSBFIRST 0
#define MSBFIRST 1

// Interrupt modes
#define CHANGE 0x1
#define FALLING 0x2
#define RISING  0x3

#define DEFAULT 1

// Serial config constants (Arduino-compatible)
#define SERIAL_PARITY_NONE (0x0ul)
#define SERIAL_PARITY_EVEN (0x1ul)
#define SERIAL_PARITY_ODD  (0x2ul)
#define SERIAL_STOP_BIT_1  (0x10ul)
#define SERIAL_STOP_BIT_2  (0x20ul)
#define SERIAL_DATA_5      (0x1ul)
#define SERIAL_DATA_6      (0x2ul)
#define SERIAL_DATA_7      (0x3ul)
#define SERIAL_DATA_8      (0x4ul)

#define SERIAL_5N1 0x00ul
#define SERIAL_6N1 0x02ul
#define SERIAL_7N1 0x04ul
#define SERIAL_8N1 0x06ul
#define SERIAL_5N2 0x08ul
#define SERIAL_6N2 0x0Aul
#define SERIAL_7N2 0x0Cul
#define SERIAL_8N2 0x0Eul
#define SERIAL_5E1 0x20ul
#define SERIAL_6E1 0x22ul
#define SERIAL_7E1 0x24ul
#define SERIAL_8E1 0x26ul
#define SERIAL_5E2 0x28ul
#define SERIAL_6E2 0x2Aul
#define SERIAL_7E2 0x2Cul
#define SERIAL_8E2 0x2Eul
#define SERIAL_5O1 0x30ul
#define SERIAL_6O1 0x32ul
#define SERIAL_7O1 0x34ul
#define SERIAL_8O1 0x36ul
#define SERIAL_5O2 0x38ul
#define SERIAL_6O2 0x3Aul
#define SERIAL_7O2 0x3Cul
#define SERIAL_8O2 0x3Eul

// Number bases
#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

// Math constants
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

// Math helpers
#ifndef __cplusplus
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#endif
#ifndef abs
#define abs(x) ((x) > 0 ? (x) : -(x))
#endif
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#ifndef round
#define round(x) ((x) >= 0 ? (long)((x) + 0.5) : (long)((x) - 0.5))
#endif
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))

// Byte macros
#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))

// Bit macros
#ifndef bit
#define bit(b) (1UL << (b))
#endif
#ifndef _BV
#define _BV(b) (1UL << (b))
#endif
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitToggle(value, bit) ((value) ^= (1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))

// Legacy interrupt-control aliases used by AVR-centric libraries.
#ifndef cli
#define cli() noInterrupts()
#endif
#ifndef sei
#define sei() interrupts()
#endif

// Clock macros (nRF54L15 runs at 64 MHz by default)
#ifndef F_CPU
#define F_CPU 64000000UL
#endif

#define clockCyclesPerMicrosecond() (F_CPU / 1000000UL)
#define clockCyclesToMicroseconds(a) ((a) / clockCyclesPerMicrosecond())
#define microsecondsToClockCycles(a) ((a) * clockCyclesPerMicrosecond())

// Flash string macro
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PSTR
#define PSTR(str_literal) (str_literal)
#endif
#ifdef __cplusplus
class __FlashStringHelper;
#define F(str_literal) (reinterpret_cast<const __FlashStringHelper *>(PSTR(str_literal)))
#else
#define F(str_literal) (str_literal)
#endif

// ============================================================================
// Core Arduino API Function Declarations
// ============================================================================

// Initialization and main loop
void init(void);
void initVariant(void);
void yield(void);

// Pin I/O
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);

// Analog I/O
int analogRead(uint8_t pin);
void analogReference(uint8_t mode);
void analogReadResolution(uint8_t bits);
void analogWriteResolution(uint8_t bits);
void analogWrite(uint8_t pin, int value);

// Timing
unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);

// Tone generation (noTone is defined in C++ section with overloaded version)
void tone(uint8_t pin, unsigned int frequency, unsigned long duration);
void noTone(uint8_t pin);

// Shift functions
void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t value);
uint8_t shiftIn(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder);

// Pulse measurement
unsigned long pulseIn(uint8_t pin, uint8_t state, unsigned long timeout);
unsigned long pulseInLong(uint8_t pin, uint8_t state, unsigned long timeout);

// Random number generation
long arduinoRandom(long max);
long arduinoRandomRange(long min, long max);
void randomSeed(unsigned long seed);

// Interrupts
void attachInterrupt(uint8_t pin, void (*userFunc)(void), int mode);
void detachInterrupt(uint8_t pin);
void interrupts(void);
void noInterrupts(void);

// Advanced I/O
long map(long, long, long, long, long);

// System helpers
void softReset(void);
void SoftReset(void);
uint32_t getFreeHeap(void);

// Sketch functions (defined by user)
void setup(void);
void loop(void);

#ifdef __cplusplus
}
#endif

// Include pin definitions for the active variant
#include "pins_arduino.h"

#ifdef __cplusplus

template <typename T, typename U>
constexpr auto min(const T& a, const U& b) -> decltype((b < a) ? b : a)
{
    return (b < a) ? b : a;
}

template <typename T, typename U>
constexpr auto max(const T& a, const U& b) -> decltype((a < b) ? b : a)
{
    return (a < b) ? b : a;
}

// Legacy single-bit binary constants without macro collisions in nRF headers.
#ifndef B0
static constexpr uint8_t B0 = 0U;
#endif
#ifndef B1
static constexpr uint8_t B1 = 1U;
#endif

// C++ wrapper functions
inline long random(long max)
{
    return arduinoRandom(max);
}

inline long random(long min, long max)
{
    return arduinoRandomRange(min, max);
}

inline word makeWord(uint16_t value)
{
    return static_cast<word>(value);
}

inline word makeWord(uint8_t high, uint8_t low)
{
    return static_cast<word>((static_cast<word>(high) << 8) | low);
}

// Tone function with default parameter (C++ only)
inline void tone(uint8_t pin, unsigned int frequency)
{
    tone(pin, frequency, 0);
}

inline unsigned long pulseIn(uint8_t pin, uint8_t state)
{
    return pulseIn(pin, state, 1000000UL);
}

inline unsigned long pulseInLong(uint8_t pin, uint8_t state)
{
    return pulseInLong(pin, state, 1000000UL);
}

// Include C++ classes
#include "WCharacter.h"
#include "WString.h"
#include "Print.h"
#include "Stream.h"
#include "HardwareSerial.h"
#include "SPI.h"
#include "Wire.h"
#include "IPAddress.h"
#include "Client.h"
#include "Server.h"
#include "Udp.h"

#endif

#endif // Arduino_h
