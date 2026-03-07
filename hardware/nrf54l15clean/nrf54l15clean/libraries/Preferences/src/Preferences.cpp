/*
 * Preferences - flash-backed key/value storage for nrf54l15clean.
 */

#include "Preferences.h"

#include <nrf54l15.h>

#include <string.h>

namespace {

constexpr uint32_t kPrefsMagic = 0x50524653UL;  // "PFRS"
constexpr uint16_t kPrefsVersion = 1U;
constexpr uint16_t kPrefsEntryCount = 28U;
constexpr size_t kPrefsNamespaceMaxLen = 15U;
constexpr size_t kPrefsKeyMaxLen = 15U;
constexpr size_t kPrefsValueMaxLen = 48U;
constexpr uint32_t kPrefsRramcBase = 0x5004B000UL;
constexpr uint32_t kPrefsRramcSpinLimit = 600000UL;

constexpr uint8_t kTypeAny = 0U;
constexpr uint8_t kTypeI8 = 1U;
constexpr uint8_t kTypeU8 = 2U;
constexpr uint8_t kTypeI16 = 3U;
constexpr uint8_t kTypeU16 = 4U;
constexpr uint8_t kTypeI32 = 5U;
constexpr uint8_t kTypeU32 = 6U;
constexpr uint8_t kTypeI64 = 7U;
constexpr uint8_t kTypeU64 = 8U;
constexpr uint8_t kTypeF32 = 9U;
constexpr uint8_t kTypeF64 = 10U;
constexpr uint8_t kTypeBool = 11U;
constexpr uint8_t kTypeString = 12U;
constexpr uint8_t kTypeBytes = 13U;

struct PreferencesEntry {
    uint8_t used;
    uint8_t type;
    uint16_t valueLen;
    char namespaceName[kPrefsNamespaceMaxLen + 1U];
    char key[kPrefsKeyMaxLen + 1U];
    uint8_t value[kPrefsValueMaxLen];
};

struct PreferencesBlob {
    uint32_t magic;
    uint16_t version;
    uint16_t entryCount;
    uint32_t crc32;
    PreferencesEntry entries[kPrefsEntryCount];
};

__attribute__((section(".prefs_storage"), aligned(4)))
volatile PreferencesBlob g_preferencesFlashBlob;

uint32_t crc32(const uint8_t* data, size_t len) {
    if (data == nullptr) {
        return 0U;
    }
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint32_t>(data[i]);
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = (crc & 1U) ? 0xFFFFFFFFUL : 0UL;
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

NRF_RRAMC_Type* prefsRramc() {
    return reinterpret_cast<NRF_RRAMC_Type*>(kPrefsRramcBase);
}

bool waitRramcReady(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
    if (rramc == nullptr) {
        return false;
    }
    while (spinLimit-- > 0U) {
        if (((rramc->READY & RRAMC_READY_READY_Msk) >> RRAMC_READY_READY_Pos) ==
            RRAMC_READY_READY_Ready) {
            return true;
        }
    }
    return false;
}

bool waitRramcReadyNext(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
    if (rramc == nullptr) {
        return false;
    }
    while (spinLimit-- > 0U) {
        if (((rramc->READYNEXT & RRAMC_READYNEXT_READYNEXT_Msk) >>
             RRAMC_READYNEXT_READYNEXT_Pos) == RRAMC_READYNEXT_READYNEXT_Ready) {
            return true;
        }
    }
    return false;
}

void copyFromVolatile(const volatile uint8_t* src, uint8_t* dst, size_t len) {
    if (src == nullptr || dst == nullptr) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        dst[i] = src[i];
    }
}

void initBlob(PreferencesBlob* blob) {
    if (blob == nullptr) {
        return;
    }
    memset(blob, 0, sizeof(*blob));
    blob->magic = kPrefsMagic;
    blob->version = kPrefsVersion;
    blob->entryCount = kPrefsEntryCount;
    blob->crc32 = 0U;
    blob->crc32 = crc32(reinterpret_cast<const uint8_t*>(blob), sizeof(*blob));
}

bool normalizeName(const char* src, char* dst, size_t maxLen) {
    if (src == nullptr || dst == nullptr) {
        return false;
    }
    const size_t len = strnlen(src, maxLen + 1U);
    if (len == 0U || len > maxLen) {
        return false;
    }
    memset(dst, 0, maxLen + 1U);
    memcpy(dst, src, len);
    return true;
}

bool validateBlob(const PreferencesBlob& blob) {
    if (blob.magic != kPrefsMagic || blob.version != kPrefsVersion ||
        blob.entryCount != kPrefsEntryCount) {
        return false;
    }

    PreferencesBlob tmp = blob;
    const uint32_t storedCrc = tmp.crc32;
    tmp.crc32 = 0U;
    const uint32_t calcCrc = crc32(reinterpret_cast<const uint8_t*>(&tmp), sizeof(tmp));
    if (storedCrc != calcCrc) {
        return false;
    }

    for (size_t i = 0; i < kPrefsEntryCount; ++i) {
        const PreferencesEntry& e = tmp.entries[i];
        if (e.used == 0U) {
            continue;
        }
        if (e.type == 0U || e.valueLen > kPrefsValueMaxLen) {
            return false;
        }
        if (e.namespaceName[0] == '\0' || e.key[0] == '\0') {
            return false;
        }
        if (e.namespaceName[kPrefsNamespaceMaxLen] != '\0' ||
            e.key[kPrefsKeyMaxLen] != '\0') {
            return false;
        }
    }
    return true;
}

bool readBlob(PreferencesBlob* outBlob) {
    if (outBlob == nullptr) {
        return false;
    }
    copyFromVolatile(reinterpret_cast<const volatile uint8_t*>(&g_preferencesFlashBlob),
                     reinterpret_cast<uint8_t*>(outBlob), sizeof(*outBlob));
    return true;
}

bool writeBlobRaw(const PreferencesBlob& blob) {
    NRF_RRAMC_Type* const rramc = prefsRramc();
    if (rramc == nullptr) {
        return false;
    }

    const uint32_t targetAddress =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_preferencesFlashBlob));
    const uint8_t* const src = reinterpret_cast<const uint8_t*>(&blob);
    const uint32_t prevConfig = rramc->CONFIG;
    const uint32_t writeConfig = prevConfig | RRAMC_CONFIG_WEN_Msk;
    bool ok = true;

    rramc->CONFIG = writeConfig;
    if (!waitRramcReady(rramc, kPrefsRramcSpinLimit)) {
        ok = false;
    }

    if (ok) {
        rramc->EVENTS_ACCESSERROR = 0U;
        for (size_t i = 0; i < sizeof(blob); ++i) {
            if (!waitRramcReadyNext(rramc, kPrefsRramcSpinLimit)) {
                ok = false;
                break;
            }
            *reinterpret_cast<volatile uint8_t*>(targetAddress + static_cast<uint32_t>(i)) =
                src[i];
        }
        if (rramc->EVENTS_ACCESSERROR != 0U) {
            ok = false;
        }
    }

    if (ok) {
        rramc->EVENTS_READY = 0U;
        rramc->TASKS_COMMITWRITEBUF = 1U;
        ok = waitRramcReady(rramc, kPrefsRramcSpinLimit);
    }

    rramc->CONFIG = prevConfig;
    if (!waitRramcReady(rramc, kPrefsRramcSpinLimit)) {
        ok = false;
    }
    return ok;
}

bool commitBlob(PreferencesBlob* blob) {
    if (blob == nullptr) {
        return false;
    }
    blob->magic = kPrefsMagic;
    blob->version = kPrefsVersion;
    blob->entryCount = kPrefsEntryCount;
    blob->crc32 = 0U;
    blob->crc32 = crc32(reinterpret_cast<const uint8_t*>(blob), sizeof(*blob));

    if (!writeBlobRaw(*blob)) {
        return false;
    }

    PreferencesBlob verify{};
    if (!readBlob(&verify)) {
        return false;
    }
    return (memcmp(&verify, blob, sizeof(verify)) == 0);
}

bool loadOrInitBlob(PreferencesBlob* blob) {
    if (blob == nullptr) {
        return false;
    }
    if (!readBlob(blob)) {
        return false;
    }
    if (validateBlob(*blob)) {
        return true;
    }
    initBlob(blob);
    return commitBlob(blob);
}

bool entryNameMatch(const char* lhs, const char* rhs) {
    return (strncmp(lhs, rhs, kPrefsNamespaceMaxLen + 1U) == 0);
}

int findEntry(const PreferencesBlob& blob, const char* nsName, const char* keyName) {
    for (size_t i = 0; i < kPrefsEntryCount; ++i) {
        const PreferencesEntry& e = blob.entries[i];
        if (e.used == 0U) {
            continue;
        }
        if (entryNameMatch(e.namespaceName, nsName) && entryNameMatch(e.key, keyName)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int findFreeEntry(const PreferencesBlob& blob) {
    for (size_t i = 0; i < kPrefsEntryCount; ++i) {
        if (blob.entries[i].used == 0U) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

size_t setValue(const char* nsName, bool readOnly, const char* key, uint8_t type,
                const void* value, size_t len) {
    if (readOnly || nsName == nullptr || key == nullptr || value == nullptr ||
        len == 0U || len > kPrefsValueMaxLen) {
        return 0U;
    }

    char normalizedKey[kPrefsKeyMaxLen + 1U];
    if (!normalizeName(key, normalizedKey, kPrefsKeyMaxLen)) {
        return 0U;
    }

    PreferencesBlob blob{};
    if (!loadOrInitBlob(&blob)) {
        return 0U;
    }

    int index = findEntry(blob, nsName, normalizedKey);
    if (index < 0) {
        index = findFreeEntry(blob);
        if (index < 0) {
            return 0U;
        }
    }

    PreferencesEntry& e = blob.entries[static_cast<size_t>(index)];
    memset(&e, 0, sizeof(e));
    e.used = 1U;
    e.type = type;
    e.valueLen = static_cast<uint16_t>(len);
    memcpy(e.namespaceName, nsName, strnlen(nsName, kPrefsNamespaceMaxLen));
    memcpy(e.key, normalizedKey, strnlen(normalizedKey, kPrefsKeyMaxLen));
    memcpy(e.value, value, len);

    if (!commitBlob(&blob)) {
        return 0U;
    }
    return len;
}

size_t getValue(const char* nsName, const char* key, uint8_t expectedType, void* outValue,
                size_t outSize) {
    if (nsName == nullptr || key == nullptr) {
        return 0U;
    }

    char normalizedKey[kPrefsKeyMaxLen + 1U];
    if (!normalizeName(key, normalizedKey, kPrefsKeyMaxLen)) {
        return 0U;
    }

    PreferencesBlob blob{};
    if (!readBlob(&blob) || !validateBlob(blob)) {
        return 0U;
    }

    const int index = findEntry(blob, nsName, normalizedKey);
    if (index < 0) {
        return 0U;
    }
    const PreferencesEntry& e = blob.entries[static_cast<size_t>(index)];
    if (expectedType != kTypeAny && e.type != expectedType) {
        return 0U;
    }

    const size_t storedLen = e.valueLen;
    if (outValue != nullptr && outSize > 0U) {
        const size_t toCopy = (storedLen < outSize) ? storedLen : outSize;
        memcpy(outValue, e.value, toCopy);
    }
    return storedLen;
}

bool isKeyPresent(const char* nsName, const char* key) {
    if (nsName == nullptr || key == nullptr) {
        return false;
    }

    char normalizedKey[kPrefsKeyMaxLen + 1U];
    if (!normalizeName(key, normalizedKey, kPrefsKeyMaxLen)) {
        return false;
    }

    PreferencesBlob blob{};
    if (!readBlob(&blob) || !validateBlob(blob)) {
        return false;
    }
    return findEntry(blob, nsName, normalizedKey) >= 0;
}

bool clearNamespace(const char* nsName, bool readOnly) {
    if (readOnly || nsName == nullptr) {
        return false;
    }

    PreferencesBlob blob{};
    if (!loadOrInitBlob(&blob)) {
        return false;
    }

    bool changed = false;
    for (size_t i = 0; i < kPrefsEntryCount; ++i) {
        PreferencesEntry& e = blob.entries[i];
        if (e.used == 0U) {
            continue;
        }
        if (entryNameMatch(e.namespaceName, nsName)) {
            memset(&e, 0, sizeof(e));
            changed = true;
        }
    }

    if (!changed) {
        return true;
    }
    return commitBlob(&blob);
}

bool removeKey(const char* nsName, bool readOnly, const char* key) {
    if (readOnly || nsName == nullptr || key == nullptr) {
        return false;
    }

    char normalizedKey[kPrefsKeyMaxLen + 1U];
    if (!normalizeName(key, normalizedKey, kPrefsKeyMaxLen)) {
        return false;
    }

    PreferencesBlob blob{};
    if (!loadOrInitBlob(&blob)) {
        return false;
    }

    const int index = findEntry(blob, nsName, normalizedKey);
    if (index < 0) {
        return false;
    }

    memset(&blob.entries[static_cast<size_t>(index)], 0, sizeof(PreferencesEntry));
    return commitBlob(&blob);
}

size_t countFreeEntries() {
    PreferencesBlob blob{};
    if (!readBlob(&blob) || !validateBlob(blob)) {
        return kPrefsEntryCount;
    }
    size_t used = 0U;
    for (size_t i = 0; i < kPrefsEntryCount; ++i) {
        if (blob.entries[i].used != 0U) {
            ++used;
        }
    }
    return kPrefsEntryCount - used;
}

}  // namespace

Preferences::Preferences() : _started(false), _readOnly(false), _namespace{0} {}

Preferences::~Preferences() {
    end();
}

bool Preferences::begin(const char* name, bool readOnly, const char* partition_label) {
    (void)partition_label;

    if (!normalizeName(name, _namespace, kPrefsNamespaceMaxLen)) {
        return false;
    }

    PreferencesBlob blob{};
    if (!loadOrInitBlob(&blob)) {
        return false;
    }

    _readOnly = readOnly;
    _started = true;
    return true;
}

void Preferences::end() {
    _started = false;
    _readOnly = false;
    memset(_namespace, 0, sizeof(_namespace));
}

bool Preferences::clear() {
    if (!_started) {
        return false;
    }
    return clearNamespace(_namespace, _readOnly);
}

bool Preferences::remove(const char* key) {
    if (!_started) {
        return false;
    }
    return removeKey(_namespace, _readOnly, key);
}

bool Preferences::isKey(const char* key) const {
    if (!_started) {
        return false;
    }
    return isKeyPresent(_namespace, key);
}

size_t Preferences::freeEntries() const {
    return countFreeEntries();
}

size_t Preferences::putChar(const char* key, int8_t value) {
    return setValue(_namespace, _readOnly, key, kTypeI8, &value, sizeof(value));
}

size_t Preferences::putUChar(const char* key, uint8_t value) {
    return setValue(_namespace, _readOnly, key, kTypeU8, &value, sizeof(value));
}

size_t Preferences::putShort(const char* key, int16_t value) {
    return setValue(_namespace, _readOnly, key, kTypeI16, &value, sizeof(value));
}

size_t Preferences::putUShort(const char* key, uint16_t value) {
    return setValue(_namespace, _readOnly, key, kTypeU16, &value, sizeof(value));
}

size_t Preferences::putInt(const char* key, int32_t value) {
    return setValue(_namespace, _readOnly, key, kTypeI32, &value, sizeof(value));
}

size_t Preferences::putUInt(const char* key, uint32_t value) {
    return setValue(_namespace, _readOnly, key, kTypeU32, &value, sizeof(value));
}

size_t Preferences::putLong(const char* key, int32_t value) {
    return putInt(key, value);
}

size_t Preferences::putULong(const char* key, uint32_t value) {
    return putUInt(key, value);
}

size_t Preferences::putLong64(const char* key, int64_t value) {
    return setValue(_namespace, _readOnly, key, kTypeI64, &value, sizeof(value));
}

size_t Preferences::putULong64(const char* key, uint64_t value) {
    return setValue(_namespace, _readOnly, key, kTypeU64, &value, sizeof(value));
}

size_t Preferences::putFloat(const char* key, float value) {
    return setValue(_namespace, _readOnly, key, kTypeF32, &value, sizeof(value));
}

size_t Preferences::putDouble(const char* key, double value) {
    return setValue(_namespace, _readOnly, key, kTypeF64, &value, sizeof(value));
}

size_t Preferences::putBool(const char* key, bool value) {
    const uint8_t raw = value ? 1U : 0U;
    return setValue(_namespace, _readOnly, key, kTypeBool, &raw, sizeof(raw));
}

size_t Preferences::putString(const char* key, const char* value) {
    if (value == nullptr) {
        return 0U;
    }
    const size_t len = strlen(value) + 1U;
    if (len > kPrefsValueMaxLen) {
        return 0U;
    }
    return setValue(_namespace, _readOnly, key, kTypeString, value, len);
}

size_t Preferences::putString(const char* key, const String& value) {
    return putString(key, value.c_str());
}

size_t Preferences::putBytes(const char* key, const void* value, size_t len) {
    if (value == nullptr || len == 0U || len > kPrefsValueMaxLen) {
        return 0U;
    }
    return setValue(_namespace, _readOnly, key, kTypeBytes, value, len);
}

int8_t Preferences::getChar(const char* key, int8_t defaultValue) const {
    int8_t value = defaultValue;
    if (getValue(_namespace, key, kTypeI8, &value, sizeof(value)) != sizeof(value)) {
        return defaultValue;
    }
    return value;
}

uint8_t Preferences::getUChar(const char* key, uint8_t defaultValue) const {
    uint8_t value = defaultValue;
    if (getValue(_namespace, key, kTypeU8, &value, sizeof(value)) != sizeof(value)) {
        return defaultValue;
    }
    return value;
}

int16_t Preferences::getShort(const char* key, int16_t defaultValue) const {
    int16_t value = defaultValue;
    if (getValue(_namespace, key, kTypeI16, &value, sizeof(value)) != sizeof(value)) {
        return defaultValue;
    }
    return value;
}

uint16_t Preferences::getUShort(const char* key, uint16_t defaultValue) const {
    uint16_t value = defaultValue;
    if (getValue(_namespace, key, kTypeU16, &value, sizeof(value)) != sizeof(value)) {
        return defaultValue;
    }
    return value;
}

int32_t Preferences::getInt(const char* key, int32_t defaultValue) const {
    int32_t value = defaultValue;
    if (getValue(_namespace, key, kTypeI32, &value, sizeof(value)) != sizeof(value)) {
        return defaultValue;
    }
    return value;
}

uint32_t Preferences::getUInt(const char* key, uint32_t defaultValue) const {
    uint32_t value = defaultValue;
    if (getValue(_namespace, key, kTypeU32, &value, sizeof(value)) != sizeof(value)) {
        return defaultValue;
    }
    return value;
}

int32_t Preferences::getLong(const char* key, int32_t defaultValue) const {
    return getInt(key, defaultValue);
}

uint32_t Preferences::getULong(const char* key, uint32_t defaultValue) const {
    return getUInt(key, defaultValue);
}

int64_t Preferences::getLong64(const char* key, int64_t defaultValue) const {
    int64_t value = defaultValue;
    if (getValue(_namespace, key, kTypeI64, &value, sizeof(value)) != sizeof(value)) {
        return defaultValue;
    }
    return value;
}

uint64_t Preferences::getULong64(const char* key, uint64_t defaultValue) const {
    uint64_t value = defaultValue;
    if (getValue(_namespace, key, kTypeU64, &value, sizeof(value)) != sizeof(value)) {
        return defaultValue;
    }
    return value;
}

float Preferences::getFloat(const char* key, float defaultValue) const {
    float value = defaultValue;
    if (getValue(_namespace, key, kTypeF32, &value, sizeof(value)) != sizeof(value)) {
        return defaultValue;
    }
    return value;
}

double Preferences::getDouble(const char* key, double defaultValue) const {
    double value = defaultValue;
    if (getValue(_namespace, key, kTypeF64, &value, sizeof(value)) != sizeof(value)) {
        return defaultValue;
    }
    return value;
}

bool Preferences::getBool(const char* key, bool defaultValue) const {
    uint8_t value = defaultValue ? 1U : 0U;
    if (getValue(_namespace, key, kTypeBool, &value, sizeof(value)) != sizeof(value)) {
        return defaultValue;
    }
    return value != 0U;
}

String Preferences::getString(const char* key, const String& defaultValue) const {
    char tmp[kPrefsValueMaxLen + 1U];
    memset(tmp, 0, sizeof(tmp));
    const size_t storedLen = getValue(_namespace, key, kTypeString, tmp, kPrefsValueMaxLen);
    if (storedLen == 0U) {
        return defaultValue;
    }
    tmp[kPrefsValueMaxLen] = '\0';
    return String(tmp);
}

size_t Preferences::getString(const char* key, char* value, size_t maxLen) const {
    if (value == nullptr || maxLen == 0U) {
        return 0U;
    }
    memset(value, 0, maxLen);
    if (maxLen == 1U) {
        return 0U;
    }

    char tmp[kPrefsValueMaxLen + 1U];
    memset(tmp, 0, sizeof(tmp));
    const size_t storedLen = getValue(_namespace, key, kTypeString, tmp, kPrefsValueMaxLen);
    if (storedLen == 0U) {
        value[0] = '\0';
        return 0U;
    }

    const size_t srcLen = strnlen(tmp, kPrefsValueMaxLen);
    const size_t copyLen = (srcLen < (maxLen - 1U)) ? srcLen : (maxLen - 1U);
    memcpy(value, tmp, copyLen);
    value[copyLen] = '\0';
    return copyLen;
}

size_t Preferences::getBytes(const char* key, void* value, size_t maxLen) const {
    if (value == nullptr || maxLen == 0U) {
        return 0U;
    }
    const size_t storedLen = getValue(_namespace, key, kTypeBytes, value, maxLen);
    return (storedLen < maxLen) ? storedLen : maxLen;
}

size_t Preferences::getBytesLength(const char* key) const {
    return getValue(_namespace, key, kTypeBytes, nullptr, 0U);
}
