#ifndef HardwareSerial_h
#define HardwareSerial_h

#include <stdint.h>

#include <nrf54l15.h>

#include "Stream.h"
#include "cmsis.h"
#include "pins_arduino.h"

extern "C" void SPIM00_IRQHandler(void);
extern "C" void SPIM20_IRQHandler(void);
extern "C" void SPIM21_IRQHandler(void);
extern "C" void SPIM22_IRQHandler(void);
extern "C" void SPIM30_IRQHandler(void);

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
    size_t write(const uint8_t* buffer, size_t size) override;
    using Print::write;

    operator bool() const;
    bool isConfigured() const;
    bool usesPins(uint8_t txPin, uint8_t rxPin) const;

private:
    friend void SPIM00_IRQHandler(void);
    friend void SPIM20_IRQHandler(void);
    friend void SPIM21_IRQHandler(void);
    friend void SPIM22_IRQHandler(void);
    friend void SPIM30_IRQHandler(void);

    void startRxDma();
    void stopRxDma();
    void serviceRxDma();
    void handleIrq();
    void processRxDmaEvents();
    void commitRxBytes(const uint8_t* data, uint32_t amount);
    void flushPartialRxDma(uintptr_t base);
    size_t writeBlocking(const uint8_t* buffer, size_t size);
    bool usesP2Pins() const;
    void requestConstlatIfNeeded();
    void releaseConstlatIfNeeded();

    static constexpr uint16_t kRxRingSize = 1024U;
    // Keep enough hardware-backed RX slack to survive short BLE timing-critical
    // sections without dropping bridge UART bytes.
    static constexpr uint8_t kRxDmaChunkSize = 128U;
    static constexpr uint8_t kTxDmaChunkSize = 64U;

    NRF_UARTE_Type* _uart;
    uint8_t _txPin;
    uint8_t _rxPin;
    bool _configured;
    bool _constlatOwned;
    unsigned long _baud;
    uint16_t _config;

    volatile uint16_t _rxHead;
    volatile uint16_t _rxTail;
    volatile uint16_t _rxCount;
    volatile uint32_t _rxDropped;

    // UARTE EasyDMA requires word-aligned RAM addresses for PTR.
    alignas(4) uint8_t _rxDmaBuffer[2][kRxDmaChunkSize];
    volatile uint8_t _rxDmaActive;
    volatile uint8_t _rxDmaPrepared;
    volatile bool _rxDmaRunning;
    volatile uint8_t _rxDmaObservedAmount;
    volatile uint32_t _rxDmaLastActivityUs;

    alignas(4) uint8_t _txByte;
    alignas(4) uint8_t _txBuffer[kTxDmaChunkSize];
    uint8_t _dataMask;

    volatile uint8_t _rxRing[kRxRingSize];
};

extern HardwareSerial Serial;
extern HardwareSerial Serial1;
extern HardwareSerial& Serial2;

extern "C" uint8_t nrf54l15_bridge_serial_active(void);

#endif
