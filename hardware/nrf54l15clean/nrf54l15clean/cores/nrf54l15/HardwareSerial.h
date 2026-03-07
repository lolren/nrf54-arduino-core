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

    int available() override;
    int read() override;
    int peek() override;
    void flush() override;

    size_t write(uint8_t value) override;
    using Print::write;

    operator bool() const;

private:
    bool beginRxByte();

    NRF_UARTE_Type* _uart;
    uint8_t _txPin;
    uint8_t _rxPin;
    int _peek;
    bool _configured;
    unsigned long _baud;
    uint8_t _rxByte;
    uint8_t _txByte;
    uint8_t _dataMask;
};

extern HardwareSerial Serial;
extern HardwareSerial Serial1;
extern HardwareSerial& Serial2;

#endif
