/*
 * EEPROM - flash-backed EEPROM emulation for nrf54l15clean.
 */

#include "EEPROM.h"

#include <nrf54l15.h>

#include <string.h>

namespace {

constexpr uint32_t kEepromMagic = 0x45455052UL;  // "EEPR"
constexpr uint16_t kEepromVersion = 1U;
constexpr uint32_t kEepromRramcBase = 0x5004B000UL;
constexpr uint32_t kEepromRramcSpinLimit = 600000UL;

struct EepromBlob {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t crc32;
    uint8_t data[EEPROMClass::kMaxLength];
};

__attribute__((section(".eeprom_storage"), aligned(4)))
volatile EepromBlob g_eepromFlashBlob;

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

NRF_RRAMC_Type* eepromRramc() {
    return reinterpret_cast<NRF_RRAMC_Type*>(kEepromRramcBase);
}

bool waitReady(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
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

bool waitReadyNext(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
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

bool readBlob(EepromBlob* blob) {
    if (blob == nullptr) {
        return false;
    }

    copyFromVolatile(reinterpret_cast<const volatile uint8_t*>(&g_eepromFlashBlob),
                     reinterpret_cast<uint8_t*>(blob), sizeof(*blob));
    return true;
}

bool validateBlob(const EepromBlob& blob) {
    if (blob.magic != kEepromMagic || blob.version != kEepromVersion ||
        blob.length != EEPROMClass::kMaxLength) {
        return false;
    }

    const uint32_t expected = crc32(blob.data, sizeof(blob.data));
    return expected == blob.crc32;
}

void buildBlob(const uint8_t* data, EepromBlob* blob) {
    if (data == nullptr || blob == nullptr) {
        return;
    }

    memset(blob, 0, sizeof(*blob));
    blob->magic = kEepromMagic;
    blob->version = kEepromVersion;
    blob->length = static_cast<uint16_t>(EEPROMClass::kMaxLength);
    memcpy(blob->data, data, EEPROMClass::kMaxLength);
    blob->crc32 = crc32(blob->data, sizeof(blob->data));
}

bool writeBlob(const EepromBlob& blob) {
    NRF_RRAMC_Type* const rramc = eepromRramc();
    if (rramc == nullptr) {
        return false;
    }

    const uint32_t targetAddress =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_eepromFlashBlob));
    const uint8_t* const src = reinterpret_cast<const uint8_t*>(&blob);
    const uint32_t prevConfig = rramc->CONFIG;
    const uint32_t writeConfig = prevConfig | RRAMC_CONFIG_WEN_Msk;
    bool ok = true;

    rramc->CONFIG = writeConfig;
    if (!waitReady(rramc, kEepromRramcSpinLimit)) {
        ok = false;
    }

    if (ok) {
        rramc->EVENTS_ACCESSERROR = 0U;
        for (size_t i = 0; i < sizeof(blob); ++i) {
            if (!waitReadyNext(rramc, kEepromRramcSpinLimit)) {
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
        ok = waitReady(rramc, kEepromRramcSpinLimit);
    }

    rramc->CONFIG = prevConfig;
    if (!waitReady(rramc, kEepromRramcSpinLimit)) {
        ok = false;
    }

    if (!ok) {
        return false;
    }

    EepromBlob verify{};
    if (!readBlob(&verify)) {
        return false;
    }

    return memcmp(&verify, &blob, sizeof(blob)) == 0;
}

}  // namespace

EEPROMRef::EEPROMRef(EEPROMClass* eeprom, int index) : _eeprom(eeprom), _index(index) {}

EEPROMRef& EEPROMRef::operator=(uint8_t value) {
    if (_eeprom != nullptr) {
        _eeprom->write(_index, value);
    }
    return *this;
}

EEPROMRef& EEPROMRef::operator=(const EEPROMRef& other) {
    return (*this = static_cast<uint8_t>(other));
}

EEPROMRef::operator uint8_t() const {
    if (_eeprom == nullptr) {
        return 0U;
    }
    return _eeprom->read(_index);
}

EEPROMRef& EEPROMRef::update(uint8_t value) {
    if (_eeprom != nullptr) {
        _eeprom->update(_index, value);
    }
    return *this;
}

EEPROMClass::EEPROMClass()
    : _started(false), _dirty(false), _autoCommit(false), _size(0U), _buffer{0} {}

bool EEPROMClass::begin(size_t size) {
    return beginInternal(size, false);
}

bool EEPROMClass::beginInternal(size_t size, bool autoCommit) {
    if (size == 0U || size > kMaxLength) {
        return false;
    }

    _size = size;
    _autoCommit = autoCommit;

    EepromBlob blob{};
    if (readBlob(&blob) && validateBlob(blob)) {
        memcpy(_buffer, blob.data, kMaxLength);
        _dirty = false;
        _started = true;
        return true;
    }

    memset(_buffer, 0xFF, kMaxLength);
    _dirty = true;
    _started = true;

    if (!commit()) {
        _started = false;
        return false;
    }

    return true;
}

bool EEPROMClass::ensureStarted() {
    if (_started) {
        return true;
    }
    return beginInternal(kMaxLength, true);
}

void EEPROMClass::end() {
    if (_started && _dirty) {
        (void)commit();
    }

    _started = false;
    _dirty = false;
    _autoCommit = false;
    _size = 0U;
}

bool EEPROMClass::commit() {
    if (!ensureStarted()) {
        return false;
    }

    if (!_dirty) {
        return true;
    }

    EepromBlob blob{};
    buildBlob(_buffer, &blob);
    if (!writeBlob(blob)) {
        return false;
    }

    _dirty = false;
    return true;
}

bool EEPROMClass::addressInRange(int address) const {
    if (address < 0) {
        return false;
    }

    return static_cast<size_t>(address) < _size;
}

uint8_t EEPROMClass::read(int address) {
    if (!ensureStarted()) {
        return 0U;
    }

    if (!addressInRange(address)) {
        return 0U;
    }

    return _buffer[static_cast<size_t>(address)];
}

void EEPROMClass::write(int address, uint8_t value) {
    if (!ensureStarted()) {
        return;
    }

    if (!addressInRange(address)) {
        return;
    }

    const size_t index = static_cast<size_t>(address);
    if (_buffer[index] != value) {
        _buffer[index] = value;
        _dirty = true;
        if (_autoCommit) {
            (void)commit();
        }
    }
}

void EEPROMClass::update(int address, uint8_t value) {
    write(address, value);
}

size_t EEPROMClass::length() const {
    return _started ? _size : kMaxLength;
}

uint8_t* EEPROMClass::getDataPtr() {
    if (!ensureStarted()) {
        return nullptr;
    }

    return _buffer;
}

EEPROMRef EEPROMClass::operator[](int address) {
    return EEPROMRef(this, address);
}

EEPROMClass EEPROM;
