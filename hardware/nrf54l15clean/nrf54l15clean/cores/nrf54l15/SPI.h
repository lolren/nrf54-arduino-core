/*
 * SPI Library for nRF54L15 - Bare Metal Implementation
 *
 * Arduino-compatible SPI library.
 *
 * Licensed under the Apache License 2.0
 */

#ifndef SPI_h
#define SPI_h

#include <Arduino.h>
#include <nrf54l15.h>

#define SPI_HAS_TRANSACTION 1

// SPI configuration constants
#define SPI_MODE0 0x00  // CPOL=0, CPHA=0
#define SPI_MODE1 0x01  // CPOL=0, CPHA=1
#define SPI_MODE2 0x02  // CPOL=1, CPHA=0
#define SPI_MODE3 0x03  // CPOL=1, CPHA=1

// Clock divider settings (nRF54L15 specific)
#define SPI_CLOCK_DIV4   4000000   // 4 MHz
#define SPI_CLOCK_DIV8   2000000   // 2 MHz
#define SPI_CLOCK_DIV16  1000000   // 1 MHz
#define SPI_CLOCK_DIV32  500000    // 500 kHz
#define SPI_CLOCK_DIV64  250000    // 250 kHz
#define SPI_CLOCK_DIV128 125000    // 125 kHz

// Bit order
#define SPI_BIT_ORDER_MSBFIRST 0
#define SPI_BIT_ORDER_LSBFIRST 1

class SPISettings {
public:
    SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode) {
        _clock = clock;
        _bitOrder = bitOrder;
        _dataMode = dataMode;
    }

    SPISettings() {
        _clock = 4000000;    // 4 MHz default
        _bitOrder = MSBFIRST;
        _dataMode = SPI_MODE0;
    }

    uint32_t clock() const { return _clock; }
    uint8_t bitOrder() const { return _bitOrder; }
    uint8_t dataMode() const { return _dataMode; }

private:
    uint32_t _clock;
    uint8_t _bitOrder;
    uint8_t _dataMode;
};

class SPIClass {
public:
    SPIClass(NRF_SPIM_Type* spim, uint8_t mosi, uint8_t miso, uint8_t sck, uint8_t cs);

    // Initialize the SPI bus
    void begin();
    void begin(uint8_t csPin);
    // Runtime remap using the common Arduino ordering: SCK, MISO, MOSI, SS.
    bool setPins(int8_t sck, int8_t miso, int8_t mosi, int8_t ss = -1);

    // Disable the SPI bus
    void end();

    // Initialize SPI with specific settings
    void beginTransaction(SPISettings settings);

    // End SPI transaction
    void endTransaction(void);

    // Transfer one byte
    uint8_t transfer(uint8_t data);
    uint16_t transfer16(uint16_t data);

    // Transfer buffer
    void transfer(void* buf, size_t count);

    // Transfer buffer with separate RX and TX
    void transfer(const void* tx_buf, void* rx_buf, size_t count);

    // Set bit order (deprecated - use SPISettings)
    void setBitOrder(uint8_t order);

    // Set data mode (deprecated - use SPISettings)
    void setDataMode(uint8_t mode);

    // Set clock divider (deprecated - use SPISettings)
    void setClockDivider(uint32_t div);

    // API-compatible no-op hooks for cores that gate SPI with IRQ ownership.
    void usingInterrupt(int interruptNumber);
    void notUsingInterrupt(int interruptNumber);
    void attachInterrupt();
    void detachInterrupt();

    // Called from core idle path to optionally auto-disable SPI on idle windows.
    void serviceAutoGate();

private:
    NRF_SPIM_Type* _spim;
    uint8_t _mosi;
    uint8_t _miso;
    uint8_t _sck;
    uint8_t _cs;
    SPISettings _settings;
    bool _initialized;
    bool _inTransaction;
    uint32_t _lastActivityUs;

    // Configure SPI pins
    void configurePins();

    // Apply SPI settings to hardware
    void applySettings();

    // Get frequency value for SPIM register
    uint32_t getFrequencyValue(uint32_t clockHz);
};

// Global SPI instance (using SPIM00 on XIAO nRF54L15 headers)
extern SPIClass SPI;

#endif // SPI_h
