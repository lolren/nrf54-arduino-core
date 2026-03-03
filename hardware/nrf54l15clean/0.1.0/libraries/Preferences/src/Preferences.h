/*
 * Preferences - flash-backed key/value storage for nrf54l15clean.
 *
 * Data survives reboot and power cycle.
 */

#ifndef Preferences_h
#define Preferences_h

#include <Arduino.h>

class Preferences {
public:
    Preferences();
    ~Preferences();

    bool begin(const char* name, bool readOnly = false, const char* partition_label = nullptr);
    void end();

    bool clear();
    bool remove(const char* key);
    bool isKey(const char* key) const;
    size_t freeEntries() const;

    size_t putChar(const char* key, int8_t value);
    size_t putUChar(const char* key, uint8_t value);
    size_t putShort(const char* key, int16_t value);
    size_t putUShort(const char* key, uint16_t value);
    size_t putInt(const char* key, int32_t value);
    size_t putUInt(const char* key, uint32_t value);
    size_t putLong(const char* key, int32_t value);
    size_t putULong(const char* key, uint32_t value);
    size_t putLong64(const char* key, int64_t value);
    size_t putULong64(const char* key, uint64_t value);
    size_t putFloat(const char* key, float value);
    size_t putDouble(const char* key, double value);
    size_t putBool(const char* key, bool value);
    size_t putString(const char* key, const char* value);
    size_t putString(const char* key, const String& value);
    size_t putBytes(const char* key, const void* value, size_t len);

    int8_t getChar(const char* key, int8_t defaultValue = 0) const;
    uint8_t getUChar(const char* key, uint8_t defaultValue = 0) const;
    int16_t getShort(const char* key, int16_t defaultValue = 0) const;
    uint16_t getUShort(const char* key, uint16_t defaultValue = 0) const;
    int32_t getInt(const char* key, int32_t defaultValue = 0) const;
    uint32_t getUInt(const char* key, uint32_t defaultValue = 0) const;
    int32_t getLong(const char* key, int32_t defaultValue = 0) const;
    uint32_t getULong(const char* key, uint32_t defaultValue = 0) const;
    int64_t getLong64(const char* key, int64_t defaultValue = 0) const;
    uint64_t getULong64(const char* key, uint64_t defaultValue = 0) const;
    float getFloat(const char* key, float defaultValue = 0.0f) const;
    double getDouble(const char* key, double defaultValue = 0.0) const;
    bool getBool(const char* key, bool defaultValue = false) const;
    String getString(const char* key, const String& defaultValue = String()) const;
    size_t getString(const char* key, char* value, size_t maxLen) const;
    size_t getBytes(const char* key, void* value, size_t maxLen) const;
    size_t getBytesLength(const char* key) const;

private:
    bool _started;
    bool _readOnly;
    char _namespace[16];
};

#endif
