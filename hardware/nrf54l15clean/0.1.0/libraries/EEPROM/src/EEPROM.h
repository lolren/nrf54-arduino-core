/*
 * EEPROM - flash-backed EEPROM emulation for nrf54l15clean.
 */

#ifndef EEPROM_h
#define EEPROM_h

#include <Arduino.h>

class EEPROMClass;

class EEPROMRef {
public:
    EEPROMRef(EEPROMClass* eeprom, int index);

    EEPROMRef& operator=(uint8_t value);
    EEPROMRef& operator=(const EEPROMRef& other);
    operator uint8_t() const;
    EEPROMRef& update(uint8_t value);

private:
    EEPROMClass* _eeprom;
    int _index;
};

class EEPROMClass {
public:
    static constexpr size_t kMaxLength = 1024U;

    EEPROMClass();

    bool begin(size_t size = kMaxLength);
    void end();
    bool commit();

    uint8_t read(int address);
    void write(int address, uint8_t value);
    void update(int address, uint8_t value);

    template <typename T>
    T& get(int address, T& value) {
        if (!ensureStarted()) {
            return value;
        }
        if (address < 0) {
            return value;
        }

        const size_t addr = static_cast<size_t>(address);
        if ((addr + sizeof(T)) > _size) {
            return value;
        }

        memcpy(&value, &_buffer[addr], sizeof(T));
        return value;
    }

    template <typename T>
    const T& put(int address, const T& value) {
        if (!ensureStarted()) {
            return value;
        }
        if (address < 0) {
            return value;
        }

        const size_t addr = static_cast<size_t>(address);
        if ((addr + sizeof(T)) > _size) {
            return value;
        }

        const uint8_t* src = reinterpret_cast<const uint8_t*>(&value);
        bool changed = false;
        for (size_t i = 0; i < sizeof(T); ++i) {
            const size_t index = addr + i;
            if (_buffer[index] != src[i]) {
                _buffer[index] = src[i];
                changed = true;
            }
        }

        if (changed) {
            _dirty = true;
            if (_autoCommit) {
                (void)commit();
            }
        }

        return value;
    }

    size_t length() const;
    uint8_t* getDataPtr();

    EEPROMRef operator[](int address);

private:
    bool beginInternal(size_t size, bool autoCommit);
    bool ensureStarted();
    bool addressInRange(int address) const;

    bool _started;
    bool _dirty;
    bool _autoCommit;
    size_t _size;
    uint8_t _buffer[kMaxLength];
};

extern EEPROMClass EEPROM;

#endif
