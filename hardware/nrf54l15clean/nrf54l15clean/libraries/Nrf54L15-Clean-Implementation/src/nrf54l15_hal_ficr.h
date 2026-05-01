#pragma once

#include <stdint.h>
#include <string.h>

#include "nrf54l15_regs.h"

namespace xiao_nrf54l15 {

// Factory Information Configuration Registers (FICR)
// All registers are read-only, pre-programmed at factory.
// Datasheet chapter 4.2.4.
class Ficr {
 public:
  // ---- Device identification ----

  // 64-bit unique device identifier (DEVICEID[0:1]).
  // DEVICEID[0] = LSB, DEVICEID[1] = MSB.
  inline static uint64_t deviceId() {
    uint64_t lo = readRegister(OFFS_INFO_DEVICEID0);
    uint64_t hi = readRegister(OFFS_INFO_DEVICEID1);
    return lo | (hi << 32);
  }

  // 128-bit UUID (UUID[0:3]). UUID[0] = LSB.
  inline static void uuid(uint8_t out[16]) {
    uint32_t words[4];
    for (int i = 0; i < 4; i++) {
      words[i] = readRegister(OFFS_INFO_UUID0 + i * 4);
    }
    // Store in little-endian byte order (each 32-bit word in LE).
    for (int i = 0; i < 4; i++) {
      out[i * 4 + 0] = (uint8_t)(words[i] >> 0);
      out[i * 4 + 1] = (uint8_t)(words[i] >> 8);
      out[i * 4 + 2] = (uint8_t)(words[i] >> 16);
      out[i * 4 + 3] = (uint8_t)(words[i] >> 24);
    }
  }

  // Configuration identifier (HWID).
  inline static uint32_t configId() {
    return readRegister(OFFS_INFO_CONFIGID);
  }

  // ---- Part information ----

  // Part code: 0x00054B15 = nRF54L15, 0x00054B10 = nRF54L10, 0x00054B05 = nRF54L05.
  inline static uint32_t partCode() {
    return readRegister(OFFS_INFO_PART);
  }

  // Returns true if this is an nRF54L15 (any variant).
  inline static bool isNrf54l15() {
    return partCode() == PART_NRF54L15;
  }

  // Returns true if this is an nRF54L10 (any variant).
  inline static bool isNrf54l10() {
    return partCode() == PART_NRF54L10;
  }

  // Returns true if this is an nRF54L05 (any variant).
  inline static bool isNrf54l05() {
    return partCode() == PART_NRF54L05;
  }

  // Variant string (up to 8 ASCII chars, null-terminated).
  // Returns true if variant data is valid (not all 0xFF).
  inline static bool variant(char* out, size_t outLen) {
    if (!out || outLen == 0) return false;
    uint32_t raw = readRegister(OFFS_INFO_VARIANT);
    if (raw == 0xFFFFFFFFUL) {
      out[0] = '\0';
      return false;
    }
    // Variant is ASCII-encoded: bytes 0-7 of the 32-bit value.
    size_t copyLen = outLen < 8 ? outLen : 8;
    for (size_t i = 0; i < copyLen; i++) {
      out[i] = static_cast<char>((raw >> (i * 8)) & 0xFF);
    }
    out[copyLen] = '\0';
    return true;
  }

  // Package option code.
  inline static uint32_t packageCode() {
    return readRegister(OFFS_INFO_PACKAGE);
  }

  // ---- Memory sizes ----

  // RAM size in kilobytes (96, 192, or 256).
  inline static uint32_t ramKb() {
    return readRegister(OFFS_INFO_RAM);
  }

  // RRAM (non-volatile) size in kilobytes (500, 1012, or 1524).
  inline static uint32_t rramKb() {
    return readRegister(OFFS_INFO_RRAM);
  }

  // ---- Bluetooth identity ----

  // Factory-assigned 48-bit device address (LE byte order in out[0:5]).
  inline static void deviceAddress(uint8_t out[6]) {
    uint32_t addr0 = readRegister(OFFS_DEVICEADDR0);
    uint32_t addr1 = readRegister(OFFS_DEVICEADDR1);
    // DEVICEADDR[0] = LSB, DEVICEADDR[1] = MSB (only lower 16 bits used).
    out[0] = (uint8_t)(addr0 >> 0);
    out[1] = (uint8_t)(addr0 >> 8);
    out[2] = (uint8_t)(addr0 >> 16);
    out[3] = (uint8_t)(addr1 >> 0);
    out[4] = (uint8_t)(addr1 >> 8);
    out[5] = (uint8_t)(addr1 >> 16);
  }

  // Device address type: 0 = Public, 1 = Random.
  inline static uint32_t deviceAddressType() {
    return readRegister(OFFS_DEVICEADDRTYPE);
  }

  // Returns true if the device has a public Bluetooth address.
  inline static bool hasPublicAddress() {
    return (deviceAddressType() & 0x1UL) == ADDR_TYPE_PUBLIC;
  }

  // ---- Bluetooth root keys ----

  // Common Encryption Root (ER[0:3]) — 128 bits, out is 16 bytes (LE).
  inline static void encryptionRoot(uint8_t out[16]) {
    read4WordsLe(out, OFFS_ER0);
  }

  // Common Identity Root (IR[0:3]) — 128 bits, out is 16 bytes (LE).
  inline static void identityRoot(uint8_t out[16]) {
    read4WordsLe(out, OFFS_IR0);
  }

  // ---- NFC tag header ----

  // 16-byte NFCID1 unique identifier from NFC.TAGHEADER[0:3] (LE).
  inline static void nfcId1(uint8_t out[16]) {
    read4WordsLe(out, OFFS_NFC_TAGHEADER0);
  }

  // NFC manufacturer ID (lower 8 bits of TAGHEADER0).
  // Nordic Semiconductor ASA = 0x5F.
  inline static uint8_t nfcManufacturerId() {
    return static_cast<uint8_t>(readRegister(OFFS_NFC_TAGHEADER0) & 0xFF);
  }

  // ---- Oscillator trim values ----

  // XOSC32M trim word (slope + offset packed per datasheet).
  inline static uint32_t xosc32mTrim() {
    return readRegister(OFFS_XOSC32MTRIM);
  }

  // XOSC32K trim word (slope + offset packed per datasheet).
  inline static uint32_t xosc32kTrim() {
    return readRegister(OFFS_XOSC32KTRIM);
  }

  // XOSC32M slope component (bits [15:0] of trim word).
  inline static uint16_t xosc32mSlope() {
    return static_cast<uint16_t>(xosc32mTrim() & 0xFFFF);
  }

  // XOSC32M offset component (bits [31:16] of trim word).
  inline static int16_t xosc32mOffset() {
    return static_cast<int16_t>(static_cast<int32_t>(xosc32mTrim() << 16) >> 16);
  }

  // XOSC32K slope component (bits [15:0] of trim word).
  inline static uint16_t xosc32kSlope() {
    return static_cast<uint16_t>(xosc32kTrim() & 0xFFFF);
  }

  // XOSC32K offset component (bits [31:16] of trim word).
  inline static int16_t xosc32kOffset() {
    return static_cast<int16_t>(static_cast<int32_t>(xosc32kTrim() << 16) >> 16);
  }

  // ---- Trim configuration (factory-programmed) ----

  // Read a trim configuration entry (n=0..63).
  // Returns {address, data} pair or {0, 0} if out of range.
  inline static bool trimConfig(uint8_t index, uint32_t* outAddr, uint32_t* outData) {
    if (index >= 64 || !outAddr || !outData) return false;
    *outAddr = readRegister(OFFS_TRIMCNF0_ADDR + index * 8);
    *outData = readRegister(OFFS_TRIMCNF0_ADDR + index * 8 + 4);
    return true;
  }

  // ---- Low-level raw access ----

  // Read any FICR register by offset from base (0x00FFC000).
  inline static uint32_t readRegister(uint32_t offset) {
    return *reinterpret_cast<const volatile uint32_t*>(BASE + offset);
  }

 private:
  static constexpr uint32_t BASE = 0x00FFC000UL;

  // Register offsets (from datasheet chapter 4.2.4).
  static constexpr uint32_t OFFS_INFO_CONFIGID    = 0x300;
  static constexpr uint32_t OFFS_INFO_DEVICEID0   = 0x304;
  static constexpr uint32_t OFFS_INFO_DEVICEID1   = 0x308;
  static constexpr uint32_t OFFS_INFO_UUID0       = 0x30C;
  static constexpr uint32_t OFFS_INFO_PART        = 0x31C;
  static constexpr uint32_t OFFS_INFO_VARIANT     = 0x320;
  static constexpr uint32_t OFFS_INFO_PACKAGE     = 0x324;
  static constexpr uint32_t OFFS_INFO_RAM         = 0x328;
  static constexpr uint32_t OFFS_INFO_RRAM        = 0x32C;
  static constexpr uint32_t OFFS_ER0              = 0x380;
  static constexpr uint32_t OFFS_IR0              = 0x390;
  static constexpr uint32_t OFFS_DEVICEADDRTYPE   = 0x3A0;
  static constexpr uint32_t OFFS_DEVICEADDR0      = 0x3A4;
  static constexpr uint32_t OFFS_DEVICEADDR1      = 0x3A8;
  static constexpr uint32_t OFFS_TRIMCNF0_ADDR    = 0x400;
  static constexpr uint32_t OFFS_NFC_TAGHEADER0   = 0x600;
  static constexpr uint32_t OFFS_NFC_TAGHEADER1   = 0x604;
  static constexpr uint32_t OFFS_NFC_TAGHEADER2   = 0x608;
  static constexpr uint32_t OFFS_NFC_TAGHEADER3   = 0x60C;
  static constexpr uint32_t OFFS_XOSC32MTRIM      = 0x620;
  static constexpr uint32_t OFFS_XOSC32KTRIM      = 0x624;

  // Part code constants.
  static constexpr uint32_t PART_NRF54L15 = 0x00054B15UL;
  static constexpr uint32_t PART_NRF54L10 = 0x00054B10UL;
  static constexpr uint32_t PART_NRF54L05 = 0x00054B05UL;

  // Address type constants.
  static constexpr uint32_t ADDR_TYPE_PUBLIC  = 0UL;
  static constexpr uint32_t ADDR_TYPE_RANDOM  = 1UL;

  // Helper: read 4 words and store as 16 bytes in LE.
  inline static void read4WordsLe(uint8_t* out, uint32_t baseOffset) {
    for (int i = 0; i < 4; i++) {
      uint32_t w = readRegister(baseOffset + i * 4);
      out[i * 4 + 0] = (uint8_t)(w >> 0);
      out[i * 4 + 1] = (uint8_t)(w >> 8);
      out[i * 4 + 2] = (uint8_t)(w >> 16);
      out[i * 4 + 3] = (uint8_t)(w >> 24);
    }
  }
};

}  // namespace xiao_nrf54l15
