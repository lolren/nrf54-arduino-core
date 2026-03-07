/*
 * TwoWire (I2C/TWI) Library for nRF54L15
 *
 * Register-level TWIM controller implementation.
 *
 * Licensed under the Apache License 2.0
 */

#ifndef TwoWire_h
#define TwoWire_h

#include <Arduino.h>
#include <nrf54l15.h>

struct i2c_target_config;

#ifdef __cplusplus
extern "C" {
#endif
void TWIM20_IRQHandler(void);
void TWIM21_IRQHandler(void);
#ifdef __cplusplus
}
#endif

#define BUFFER_LENGTH 32

class TwoWire : public Stream {
public:
    TwoWire(NRF_TWIM_Type* twim, uint8_t sda, uint8_t scl);

    void begin();
    void begin(uint8_t address);
    void begin(int address);
    void end();
    bool setPins(uint8_t sda, uint8_t scl);
    void setClock(uint32_t freq);

    void beginTransmission(uint8_t address);
    void beginTransmission(int address);
    uint8_t endTransmission(bool sendStop = true);

    uint8_t requestFrom(uint8_t address, uint8_t quantity, uint8_t sendStop = true);
    uint8_t requestFrom(uint8_t address, size_t quantity, bool sendStop = true);
    uint8_t requestFrom(int address, int quantity);
    uint8_t requestFrom(int address, int quantity, uint8_t sendStop);

    int available(void);
    int read(void);
    int peek(void);

    size_t write(uint8_t data);
    size_t write(const uint8_t* data, size_t quantity);
    void flush(void);

    // Target/slave callbacks (Arduino-compatible API)
    void onReceive(void (*callback)(int));
    void onRequest(void (*callback)(void));
    void serviceAutoGate();

    uint8_t getTransmissionAddress() const { return _txAddress; }

    using Print::write;

private:
    enum TargetDirection : uint8_t {
        TARGET_DIR_NONE = 0,
        TARGET_DIR_WRITE = 1,
        TARGET_DIR_READ = 2,
    };

    const void* _i2c;
    NRF_TWIM_Type* _twim;
    uint8_t _sda;
    uint8_t _scl;
    uint32_t _frequency;
    bool _initialized;

    alignas(4) uint8_t _txBuffer[BUFFER_LENGTH];
    uint8_t _txBufferLength;
    uint8_t _txAddress;

    alignas(4) uint8_t _rxBuffer[BUFFER_LENGTH];
    uint8_t _rxBufferIndex;
    uint8_t _rxBufferLength;
    int _peek;

    alignas(4) uint8_t _targetTxBuffer[BUFFER_LENGTH];
    uint8_t _targetTxLength;
    uint8_t _targetTxIndex;
    uint8_t _targetAddress;
    bool _targetRegistered;
    bool _inOnRequestCallback;
    TargetDirection _targetDirection;

    void (*_onReceive)(int);
    void (*_onRequest)(void);

    bool _pendingRepeatedStart;
    uint32_t _lastActivityUs;

    friend void ::TWIM20_IRQHandler(void);
    friend void ::TWIM21_IRQHandler(void);

    static int targetWriteRequestedAdapter(struct i2c_target_config* config);
    static int targetWriteReceivedAdapter(struct i2c_target_config* config, uint8_t value);
    static int targetReadRequestedAdapter(struct i2c_target_config* config, uint8_t* value);
    static int targetReadProcessedAdapter(struct i2c_target_config* config, uint8_t* value);
    static int targetStopAdapter(struct i2c_target_config* config);

    bool isTargetWriteContext() const;
    void clearControllerTxState();
    void clearReceiveState();
    void clearTargetTxState();
    int provideTargetByte(uint8_t* value);

    int handleTargetWriteRequested();
    int handleTargetWriteReceived(uint8_t value);
    int handleTargetReadRequested(uint8_t* value);
    int handleTargetReadProcessed(uint8_t* value);
    int handleTargetStop();

    void armTargetRx();
    void armTargetTx();
    void handleTargetIrq();
};

extern TwoWire Wire;
extern TwoWire Wire1;

#endif // TwoWire_h
