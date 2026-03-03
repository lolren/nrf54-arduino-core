/*
 * Arduino Print - Output class for Arduino
 *
 * Licensed under the Apache License 2.0
 */

#ifndef Print_h
#define Print_h

#include <stddef.h>
#include <stdint.h>

#include "WString.h"

class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t value) = 0;

    size_t write(const uint8_t *buffer, size_t size);
    size_t write(const char *str);
    size_t write(int value) { return write(static_cast<uint8_t>(value)); }
    size_t write(unsigned int value) { return write(static_cast<uint8_t>(value)); }
    size_t write(long value) { return write(static_cast<uint8_t>(value)); }
    size_t write(unsigned long value) { return write(static_cast<uint8_t>(value)); }

    size_t print(const String &value);
    size_t print(const char *value);
    size_t print(char value);
    size_t print(unsigned char value, int base = 10);
    size_t print(int value, int base = 10);
    size_t print(unsigned int value, int base = 10);
    size_t print(long value, int base = 10);
    size_t print(unsigned long value, int base = 10);
    size_t print(double value, int digits = 2);

    size_t println(void);
    size_t println(const String &value);
    size_t println(const char *value);
    size_t println(char value);
    size_t println(unsigned char value, int base = 10);
    size_t println(int value, int base = 10);
    size_t println(unsigned int value, int base = 10);
    size_t println(long value, int base = 10);
    size_t println(unsigned long value, int base = 10);
    size_t println(double value, int digits = 2);

protected:
    size_t printNumber(unsigned long value, uint8_t base);
    size_t printSigned(long value, uint8_t base);
    size_t printFloat(double value, uint8_t digits);
};

#endif
