/*
 * Arduino Print implementation
 *
 * Licensed under the Apache License 2.0
 */

#include "Print.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

size_t Print::write(const uint8_t *buffer, size_t size)
{
    size_t written = 0;
    if (buffer == nullptr) {
        return 0;
    }

    for (size_t i = 0; i < size; ++i) {
        written += write(buffer[i]);
    }

    return written;
}

size_t Print::write(const char *str)
{
    if (str == nullptr) {
        return 0;
    }

    return write(reinterpret_cast<const uint8_t *>(str), strlen(str));
}

size_t Print::print(const String &value)
{
    return write(value.c_str());
}

size_t Print::print(const __FlashStringHelper *value)
{
    return write(reinterpret_cast<const char *>(value));
}

size_t Print::print(const char *value)
{
    return write(value);
}

size_t Print::print(char value)
{
    return write(static_cast<uint8_t>(value));
}

size_t Print::print(unsigned char value, int base)
{
    return print(static_cast<unsigned long>(value), base);
}

size_t Print::print(int value, int base)
{
    return print(static_cast<long>(value), base);
}

size_t Print::print(unsigned int value, int base)
{
    return print(static_cast<unsigned long>(value), base);
}

size_t Print::print(long value, int base)
{
    return printSigned(value, static_cast<uint8_t>(base));
}

size_t Print::print(unsigned long value, int base)
{
    return printNumber(value, static_cast<uint8_t>(base));
}

size_t Print::print(double value, int digits)
{
    return printFloat(value, static_cast<uint8_t>(digits));
}

size_t Print::print(const Printable &value)
{
    return value.printTo(*this);
}

size_t Print::println(void)
{
    return write('\n');
}

size_t Print::println(const String &value)
{
    size_t count = print(value);
    count += println();
    return count;
}

size_t Print::println(const __FlashStringHelper *value)
{
    size_t count = print(value);
    count += println();
    return count;
}

size_t Print::println(const char *value)
{
    size_t count = print(value);
    count += println();
    return count;
}

size_t Print::println(char value)
{
    size_t count = print(value);
    count += println();
    return count;
}

size_t Print::println(unsigned char value, int base)
{
    size_t count = print(value, base);
    count += println();
    return count;
}

size_t Print::println(int value, int base)
{
    size_t count = print(value, base);
    count += println();
    return count;
}

size_t Print::println(unsigned int value, int base)
{
    size_t count = print(value, base);
    count += println();
    return count;
}

size_t Print::println(long value, int base)
{
    size_t count = print(value, base);
    count += println();
    return count;
}

size_t Print::println(unsigned long value, int base)
{
    size_t count = print(value, base);
    count += println();
    return count;
}

size_t Print::println(double value, int digits)
{
    size_t count = print(value, digits);
    count += println();
    return count;
}

size_t Print::println(const Printable &value)
{
    size_t count = print(value);
    count += println();
    return count;
}

size_t Print::printNumber(unsigned long value, uint8_t base)
{
    if (base < 2) {
        base = 10;
    }

    char buf[33];
    char *ptr = &buf[sizeof(buf) - 1];
    *ptr = '\0';

    do {
        unsigned long digit = value % base;
        *--ptr = (digit < 10) ? static_cast<char>('0' + digit) : static_cast<char>('A' + (digit - 10));
        value /= base;
    } while (value > 0);

    return write(ptr);
}

size_t Print::printSigned(long value, uint8_t base)
{
    if (base == 10 && value < 0) {
        size_t count = write('-');
        count += printNumber(static_cast<unsigned long>(-value), base);
        return count;
    }

    return printNumber(static_cast<unsigned long>(value), base);
}

size_t Print::printFloat(double value, uint8_t digits)
{
    if (isnan(value)) {
        return print("nan");
    }
    if (isinf(value)) {
        return print("inf");
    }

    // Match longstanding Arduino behavior for out-of-range values.
    if (value > 4294967040.0 || value < -4294967040.0) {
        return print("ovf");
    }

    if (digits > 9U) {
        digits = 9U;
    }

    size_t count = 0;
    if (value < 0.0) {
        count += write('-');
        value = -value;
    }

    double rounding = 0.5;
    for (uint8_t i = 0; i < digits; ++i) {
        rounding /= 10.0;
    }
    value += rounding;

    const unsigned long intPart = static_cast<unsigned long>(value);
    double remainder = value - static_cast<double>(intPart);

    count += printNumber(intPart, 10);

    if (digits > 0U) {
        count += write('.');
    }

    while (digits-- > 0U) {
        remainder *= 10.0;
        const uint8_t toPrint = static_cast<uint8_t>(remainder);
        count += printNumber(toPrint, 10);
        remainder -= static_cast<double>(toPrint);
    }

    return count;
}
