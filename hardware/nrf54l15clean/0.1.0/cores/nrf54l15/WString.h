/*
 * Arduino WString - String class for Arduino
 *
 * Licensed under the Apache License 2.0
 */

#ifndef WString_h
#define WString_h

#include <stddef.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

class __FlashStringHelper;
class StringSumHelper;

class String {
public:
    String() { assign(""); }

    String(const char *value)
    {
        assign(value == nullptr ? "" : value);
    }

    String(const __FlashStringHelper *value)
    {
        assign(value == nullptr ? "" : reinterpret_cast<const char *>(value));
    }

    String(char value)
    {
        char tmp[2] = {value, '\0'};
        assign(tmp);
    }

    String(unsigned char value)
        : String(value, 10)
    {
    }

    String(unsigned char value, unsigned char base)
    {
        assignUnsigned(value, base);
    }

    String(int value)
        : String(value, 10)
    {
    }

    String(int value, unsigned char base)
    {
        assignSigned(value, base);
    }

    String(unsigned int value)
        : String(value, 10)
    {
    }

    String(unsigned int value, unsigned char base)
    {
        assignUnsigned(value, base);
    }

    String(long value)
        : String(value, 10)
    {
    }

    String(long value, unsigned char base)
    {
        assignSigned(value, base);
    }

    String(unsigned long value)
        : String(value, 10)
    {
    }

    String(unsigned long value, unsigned char base)
    {
        assignUnsigned(value, base);
    }

    String(float value)
    {
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "%f", (double)value);
        assign(tmp);
    }

    String(double value)
    {
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "%f", value);
        assign(tmp);
    }

    String(const String &other)
    {
        assign(other.c_str());
    }

    String(String &&other) noexcept
        : _data(other._data), _length(other._length)
    {
        other._data = nullptr;
        other._length = 0;
    }

    ~String()
    {
        free(_data);
    }

    String &operator=(const String &other)
    {
        if (this != &other) {
            assign(other.c_str());
        }
        return *this;
    }

    String &operator=(const char* value)
    {
        assign(value == nullptr ? "" : value);
        return *this;
    }

    String &operator=(const __FlashStringHelper* value)
    {
        assign(value == nullptr ? "" : reinterpret_cast<const char*>(value));
        return *this;
    }

    String &operator=(String &&other) noexcept
    {
        if (this != &other) {
            free(_data);
            _data = other._data;
            _length = other._length;
            other._data = nullptr;
            other._length = 0;
        }
        return *this;
    }

    size_t length() const { return _length; }
    bool isEmpty() const { return _length == 0; }
    const char *c_str() const { return _data == nullptr ? "" : _data; }

    int toInt() const { return (int)strtol(c_str(), nullptr, 10); }
    float toFloat() const { return strtof(c_str(), nullptr); }
    void toUpperCase()
    {
        if (_data == nullptr) {
            return;
        }
        for (size_t i = 0; i < _length; ++i) {
            _data[i] = (char)toupper((unsigned char)_data[i]);
        }
    }

    int indexOf(char ch, unsigned int fromIndex = 0) const
    {
        if (fromIndex >= _length) {
            return -1;
        }
        for (size_t i = fromIndex; i < _length; ++i) {
            if (_data[i] == ch) {
                return (int)i;
            }
        }
        return -1;
    }

    int indexOf(const String& value, unsigned int fromIndex = 0) const
    {
        return indexOf(value.c_str(), fromIndex);
    }

    int indexOf(const char* value, unsigned int fromIndex = 0) const
    {
        if (value == nullptr) {
            return -1;
        }

        const size_t needleLen = strlen(value);
        if (needleLen == 0U) {
            return (fromIndex <= _length) ? (int)fromIndex : -1;
        }
        if (fromIndex >= _length || needleLen > _length) {
            return -1;
        }

        const char* haystack = c_str();
        for (size_t i = fromIndex; (i + needleLen) <= _length; ++i) {
            if (memcmp(haystack + i, value, needleLen) == 0) {
                return (int)i;
            }
        }
        return -1;
    }

    String substring(unsigned int beginIndex) const
    {
        return substring(beginIndex, (unsigned int)_length);
    }

    String substring(unsigned int beginIndex, unsigned int endIndex) const
    {
        if (beginIndex > endIndex) {
            const unsigned int tmp = beginIndex;
            beginIndex = endIndex;
            endIndex = tmp;
        }

        if (beginIndex >= _length) {
            return String("");
        }
        if (endIndex > _length) {
            endIndex = (unsigned int)_length;
        }

        String out;
        out.assignRange(c_str() + beginIndex, (size_t)(endIndex - beginIndex));
        return out;
    }

    void toCharArray(char* buffer, unsigned int bufferSize, unsigned int index = 0) const
    {
        if (buffer == nullptr || bufferSize == 0U) {
            return;
        }
        if (index >= _length) {
            buffer[0] = '\0';
            return;
        }

        size_t copyLen = _length - index;
        if (copyLen > (size_t)(bufferSize - 1U)) {
            copyLen = (size_t)(bufferSize - 1U);
        }
        memcpy(buffer, c_str() + index, copyLen);
        buffer[copyLen] = '\0';
    }

    bool equals(const String &other) const { return strcmp(c_str(), other.c_str()) == 0; }

    bool concat(const String& other)
    {
        const size_t rhs_len = other.length();
        if (rhs_len == 0U) {
            return true;
        }

        char* new_buf = (char*)realloc(_data, _length + rhs_len + 1U);
        if (new_buf == nullptr) {
            return false;
        }

        memcpy(new_buf + _length, other.c_str(), rhs_len + 1U);
        _data = new_buf;
        _length += rhs_len;
        return true;
    }

    bool concat(const char* value)
    {
        if (value == nullptr) {
            return false;
        }

        const size_t rhs_len = strlen(value);
        if (rhs_len == 0U) {
            return true;
        }

        char* new_buf = (char*)realloc(_data, _length + rhs_len + 1U);
        if (new_buf == nullptr) {
            return false;
        }

        memcpy(new_buf + _length, value, rhs_len + 1U);
        _data = new_buf;
        _length += rhs_len;
        return true;
    }

    bool concat(char value)
    {
        char tmp[2] = {value, '\0'};
        return concat(tmp);
    }

    bool concat(unsigned char value)
    {
        return concat(String(value));
    }

    bool concat(int value)
    {
        return concat(String(value));
    }

    bool concat(unsigned int value)
    {
        return concat(String(value));
    }

    bool concat(long value)
    {
        return concat(String(value));
    }

    bool concat(unsigned long value)
    {
        return concat(String(value));
    }

    bool concat(float value)
    {
        return concat(String(value));
    }

    bool concat(double value)
    {
        return concat(String(value));
    }

    bool concat(const __FlashStringHelper* value)
    {
        return concat(value == nullptr ? nullptr : reinterpret_cast<const char*>(value));
    }

    String &operator+=(const String &other)
    {
        (void)concat(other);
        return *this;
    }

    String operator+(const String &other) const
    {
        String out(*this);
        out += other;
        return out;
    }

    bool operator==(const String &other) const { return equals(other); }
    bool operator!=(const String &other) const { return !equals(other); }

    char operator[](size_t index) const { return (index < _length) ? _data[index] : '\0'; }
    char &operator[](size_t index)
    {
        static char dummy = '\0';
        if (index >= _length) {
            return dummy;
        }
        return _data[index];
    }

    operator const char *() const { return c_str(); }

private:
    static unsigned char normalizeBase(unsigned char base)
    {
        return (base >= 2U && base <= 36U) ? base : 10U;
    }

    static size_t formatUnsigned(unsigned long value, unsigned char base, char* out, size_t outSize)
    {
        if (out == nullptr || outSize == 0U) {
            return 0U;
        }

        base = normalizeBase(base);
        char tmp[34];
        size_t idx = 0U;

        do {
            const unsigned long digit = value % base;
            tmp[idx++] = (digit < 10UL) ? (char)('0' + digit)
                                        : (char)('A' + (digit - 10UL));
            value /= base;
        } while (value != 0UL && idx < sizeof(tmp));

        size_t outLen = (idx < (outSize - 1U)) ? idx : (outSize - 1U);
        for (size_t i = 0U; i < outLen; ++i) {
            out[i] = tmp[idx - i - 1U];
        }
        out[outLen] = '\0';
        return outLen;
    }

    void assignUnsigned(unsigned long value, unsigned char base)
    {
        char tmp[34];
        (void)formatUnsigned(value, base, tmp, sizeof(tmp));
        assign(tmp);
    }

    void assignSigned(long value, unsigned char base)
    {
        base = normalizeBase(base);
        if (base == 10U && value < 0L) {
            const unsigned long mag =
                (unsigned long)(-(value + 1L)) + 1UL;  // Avoid overflow for LONG_MIN.
            char tmp[35];
            tmp[0] = '-';
            (void)formatUnsigned(mag, base, tmp + 1, sizeof(tmp) - 1U);
            assign(tmp);
            return;
        }
        assignUnsigned((unsigned long)value, base);
    }

    void assign(const char *value)
    {
        assignRange(value, strlen(value));
    }

    void assignRange(const char* value, size_t len)
    {
        char *new_buf = (char *)malloc(len + 1U);
        if (new_buf == nullptr) {
            return;
        }

        if (len > 0U && value != nullptr) {
            memcpy(new_buf, value, len);
        }
        new_buf[len] = '\0';
        free(_data);
        _data = new_buf;
        _length = len;
    }

    char *_data = nullptr;
    size_t _length = 0;
};

// AVR-style compatibility type used by legacy libraries (for example ArduinoJson).
class StringSumHelper : public String {
public:
    StringSumHelper(const String& value)
        : String(value)
    {
    }
    StringSumHelper(const char* value)
        : String(value)
    {
    }
    StringSumHelper(char value)
        : String(value)
    {
    }
    StringSumHelper(unsigned char value)
        : String(value)
    {
    }
    StringSumHelper(int value)
        : String(value)
    {
    }
    StringSumHelper(unsigned int value)
        : String(value)
    {
    }
    StringSumHelper(long value)
        : String(value)
    {
    }
    StringSumHelper(unsigned long value)
        : String(value)
    {
    }
    StringSumHelper(float value)
        : String(value)
    {
    }
    StringSumHelper(double value)
        : String(value)
    {
    }
    StringSumHelper(const __FlashStringHelper* value)
        : String(value)
    {
    }
};

#endif
