#pragma once

#include <stdint.h>

#include "nrf54l15_regs.h"

namespace xiao_nrf54l15 {

// CRACEN Public Key Engine (PKE) — Register-Level Interface
//
// The nRF54L15 CRACEN includes a PKE coprocessor supporting ECDSA,
// EC-KCDSA, ECDH, and SHA-256/512.
// Requires Nordic's proprietary PKE microcode library for actual
// cryptographic operations.
//
// Datasheet chapter 7.8 (CRACEN — Cryptographic engine).
class CracenPke {
 public:
  // ---- CRACEN control (base: 0x40048000, non-secure alias) ----

  // Enable CRACEN. Starts zeroization of PKE data RAM.
  inline static void enable() {
    wr(0x500, 1);  // CONFIG.ENABLE = Enabled
  }

  // Disable CRACEN.
  inline static void disable() {
    wr(0x500, 0);  // CONFIG.ENABLE = Disabled
  }

  inline static bool isEnabled() {
    return (rd(0x500) & 0x01) != 0;
  }

  // ---- CRACEN status ----

  inline static bool ready() {
    // STATUS register at 0x504: bit 0 = Ready/NotReady
    return (rd(0x504) & 0x01) != 0;
  }

  inline static bool waitForReady(uint32_t spinLimit = 1000000UL) {
    while (spinLimit-- > 0) { if (ready()) return true; }
    return false;
  }

  // ---- PKE/IKG control (via CRACENCORE, base: 0x51800000) ----

  inline static void enablePkeIkg() {
    wrCore(0x301C, 1);  // IKG.PKECONTROL.ENABLE
  }

  inline static void disablePkeIkg() {
    wrCore(0x301C, 0);
  }

  inline static uint32_t pkeStatus() {
    return rdCore(0x3024);  // IKG.PKESTATUS
  }

  inline static bool pkeBusy() {
    return (pkeStatus() & 0x01) != 0;  // PKBUSY bit
  }

  inline static bool pkeReady() { return !pkeBusy(); }

  inline static bool waitPkeReady(uint32_t spinLimit = 1000000UL) {
    while (spinLimit-- > 0) { if (pkeReady()) return true; }
    return false;
  }

  // ---- PKE data RAM (base: 0x51808000, aligned 32-bit) ----

  inline static void writeData(uint32_t offset, uint32_t value) {
    if (offset & 0x3) return;
    *reinterpret_cast<volatile uint32_t*>(0x51808000UL + offset) = value;
  }

  inline static void writeDataBlock(uint32_t offset, const uint32_t* words, size_t count) {
    for (size_t i = 0; i < count; i++) writeData(offset + i * 4, words[i]);
  }

  inline static void writeDataBytes(uint32_t offset, const uint8_t* bytes, size_t count) {
    for (size_t i = 0; i + 3 < count; i += 4) {
      uint32_t w = ((uint32_t)bytes[i]<<0)|((uint32_t)bytes[i+1]<<8)|
                   ((uint32_t)bytes[i+2]<<16)|((uint32_t)bytes[i+3]<<24);
      writeData(offset + i, w);
    }
    size_t rem = count % 4;
    if (rem > 0) {
      uint32_t w = 0;
      for (size_t i = 0; i < rem; i++)
        w |= ((uint32_t)bytes[count - rem + i] << (i * 8));
      writeData(offset + (count - rem), w);
    }
  }

  inline static uint32_t readData(uint32_t offset) {
    if (offset & 0x3) return 0;
    return *reinterpret_cast<const volatile uint32_t*>(0x51808000UL + offset);
  }

  inline static void readDataBlock(uint32_t offset, uint32_t* words, size_t count) {
    for (size_t i = 0; i < count; i++) words[i] = readData(offset + i * 4);
  }

  inline static void readDataBytes(uint32_t offset, uint8_t* bytes, size_t count) {
    for (size_t i = 0; i + 3 < count; i += 4) {
      uint32_t w = readData(offset + i);
      bytes[i+0]=(uint8_t)(w>>0); bytes[i+1]=(uint8_t)(w>>8);
      bytes[i+2]=(uint8_t)(w>>16); bytes[i+3]=(uint8_t)(w>>24);
    }
    size_t rem = count % 4;
    if (rem > 0) {
      uint32_t w = readData(offset + (count - rem));
      for (size_t i = 0; i < rem; i++)
        bytes[count - rem + i] = (uint8_t)(w >> (i * 8));
    }
  }

  // ---- PKE code RAM (base: 0x5180C000, aligned 32-bit) ----

  inline static void writeCode(uint32_t offset, uint32_t value) {
    if (offset & 0x3) return;
    *reinterpret_cast<volatile uint32_t*>(0x5180C000UL + offset) = value;
  }

  inline static void writeCodeBlock(uint32_t offset, const uint32_t* words, size_t count) {
    for (size_t i = 0; i < count; i++) writeCode(offset + i * 4, words[i]);
  }

  // ---- PKE command ----

  inline static void issueCommand(uint32_t command) {
    wrCore(0x3020, command);  // IKG.PKECOMMAND
  }

  // ---- Interrupts ----

  inline static bool pkeIkgEvent(bool clear = true) {
    uint32_t ev = rd(0x108);  // EVENTS_PKEIKG
    if (clear) wr(0x108, 0);
    return ev != 0;
  }

  inline static void enablePkeIkgInterrupt(bool en = true) {
    if (en) wr(0x300, 0x04);  // INTENSET.PKEIKG = 1
    else     wr(0x304, 0x04); // INTENCLR.PKEIKG = 1
  }

  // ---- Capacity info ----

  inline static constexpr size_t pkeDataSize() { return 8192; }
  inline static constexpr size_t pkeCodeSize() { return 16384; }

 private:
  inline static uint32_t rd(uint32_t off) {
    return *reinterpret_cast<const volatile uint32_t*>(0x40048000UL + off);
  }
  inline static void wr(uint32_t off, uint32_t val) {
    *reinterpret_cast<volatile uint32_t*>(0x40048000UL + off) = val;
  }
  inline static uint32_t rdCore(uint32_t off) {
    return *reinterpret_cast<const volatile uint32_t*>(0x51800000UL + off);
  }
  inline static void wrCore(uint32_t off, uint32_t val) {
    *reinterpret_cast<volatile uint32_t*>(0x51800000UL + off) = val;
  }
};

}  // namespace xiao_nrf54l15
