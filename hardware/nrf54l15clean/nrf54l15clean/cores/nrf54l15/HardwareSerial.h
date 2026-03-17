#ifndef HardwareSerial_h
#define HardwareSerial_h

#include <stdint.h>

#include <nrf54l15.h>

#include "Stream.h"
#include "pins_arduino.h"

class HardwareSerial : public Stream {
public:
    explicit HardwareSerial(NRF_UARTE_Type* uart = NRF_UARTE21,
                            uint8_t txPin = PIN_SERIAL_TX,
                            uint8_t rxPin = PIN_SERIAL_RX);

    void begin(unsigned long baud);
    void begin(unsigned long baud, uint16_t config);
    void end();
    // Runtime remap using the common Arduino ordering: RX, TX.
    bool setPins(int8_t rxPin, int8_t txPin);

    int available() override;
    int read() override;
    int peek() override;
    void flush() override;

    size_t write(uint8_t value) override;
    using Print::write;

    operator bool() const;
    bool isConfigured() const;
    bool usesPins(uint8_t txPin, uint8_t rxPin) const;

private:
    void startRxDma();
    void stopRxDma();
    void serviceRxDma();

    static constexpr uint16_t kRxRingSize = 256U;

    NRF_UARTE_Type* _uart;
    uint8_t _txPin;
    uint8_t _rxPin;
    bool _configured;
    unsigned long _baud;
    uint16_t _config;

    uint16_t _rxHead;
    uint16_t _rxTail;
    uint16_t _rxCount;
    uint32_t _rxDropped;

    // UARTE EasyDMA requires word-aligned RAM addresses for PTR.
    alignas(4) uint32_t _rxDmaWord[2];
    uint8_t _rxDmaActive;
    uint8_t _rxDmaPrepared;
    bool _rxDmaRunning;

    alignas(4) uint8_t _txByte;
    uint8_t _dataMask;

    uint8_t _rxRing[kRxRingSize];
};

extern HardwareSerial Serial;
extern HardwareSerial Serial1;
extern HardwareSerial& Serial2;

extern "C" uint8_t nrf54l15_bridge_serial_active(void);

#endif
