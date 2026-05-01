#pragma once

#include <stdint.h>

#include "nrf54l15_regs.h"

namespace xiao_nrf54l15 {

// Memory Configuration (MEMCONF)
// Controls RAM section power/retention and RRAM region security attributes.
// Datasheet chapter 4.3.6 (MEMCONF — Memory configuration).
class Memconf {
 public:
  // ---- RAM section control ----

  // Power on a specific RAM section (0..7 on nRF54L15).
  // The section is powered from the main supply rail.
  inline static bool powerOnRamSection(uint8_t section) {
    if (section >= 8) return false;
    // RAM section power-on is via the RAM section power register.
    // Each section has a separate TASKS_RAMON[n] and EVENTS_RAMON[n].
    writeReg(MEMCONF_TASKS_RAMON0 + section * 4, 1);
    return true;
  }

  // Power off a specific RAM section (0..7 on nRF54L15).
  // Contents are NOT retained unless retention was enabled before power-off.
  inline static bool powerOffRamSection(uint8_t section) {
    if (section >= 8) return false;
    writeReg(MEMCONF_TASKS_RAMOFF0 + section * 4, 1);
    return true;
  }

  // Check if a RAM section power-on event has fired.
  inline static bool ramOnEvent(uint8_t section, bool clear = true) {
    if (section >= 8) return false;
    uint32_t ev = readReg(MEMCONF_EVENTS_RAMON0 + section * 4);
    if (clear) writeReg(MEMCONF_EVENTS_RAMON0 + section * 4, 0);
    return ev != 0;
  }

  // Check if a RAM section power-off event has fired.
  inline static bool ramOffEvent(uint8_t section, bool clear = true) {
    if (section >= 8) return false;
    uint32_t ev = readReg(MEMCONF_EVENTS_RAMOFF0 + section * 4);
    if (clear) writeReg(MEMCONF_EVENTS_RAMOFF0 + section * 4, 0);
    return ev != 0;
  }

  // Check if a RAM section is powered on (from STATUS register).
  // Returns bitmask of powered-on sections (bit 0 = section 0, etc.).
  inline static uint32_t ramSectionStatus() {
    return readReg(MEMCONF_RAMSTATUS);
  }

  // Returns true if the specified section is powered on.
  inline static bool isRamSectionPowered(uint8_t section) {
    if (section >= 8) return false;
    return (ramSectionStatus() & (1U << section)) != 0;
  }

  // ---- RRAM (NVM) security control ----

  // Get the current RRAM region security configuration.
  // Returns the value of the NVMC read-protection and write-protection registers.
  inline static uint32_t nvmcReadProtection() {
    return readReg(NVMC_READPROTECTED);
  }

  inline static uint32_t nvmcWriteProtection() {
    return readReg(NVMC_WRITEPROTECTED);
  }

  // ---- Interrupts ----

  inline static void enableRamOnInterrupt(uint8_t section, bool enable = true) {
    if (section >= 8) return;
    volatile uint32_t* base = reinterpret_cast<volatile uint32_t*>(BASE);
    if (enable) {
      base[INTENSET_OFFSET / 4] |= (1U << (16 + section));
    } else {
      base[INTENCLR_OFFSET / 4] |= (1U << (16 + section));
    }
  }

  inline static void enableRamOffInterrupt(uint8_t section, bool enable = true) {
    if (section >= 8) return;
    volatile uint32_t* base = reinterpret_cast<volatile uint32_t*>(BASE);
    if (enable) {
      base[INTENSET_OFFSET / 4] |= (1U << section);
    } else {
      base[INTENCLR_OFFSET / 4] |= (1U << section);
    }
  }

  // ---- Low-level raw access ----

  inline static uint32_t readReg(uint32_t offset) {
    return *reinterpret_cast<const volatile uint32_t*>(BASE + offset);
  }

  inline static void writeReg(uint32_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(BASE + offset) = value;
  }

 private:
  static constexpr uint32_t BASE = 0x400CF000UL;  // MEMCONF base (PERI domain)

  // Register offsets.
  // Tasks: RAMON[0..7], RAMOFF[0..7]
  static constexpr uint32_t MEMCONF_TASKS_RAMON0  = 0x000;
  static constexpr uint32_t MEMCONF_TASKS_RAMOFF0 = 0x020;
  // Events: RAMON[0..7], RAMOFF[0..7]
  static constexpr uint32_t MEMCONF_EVENTS_RAMON0  = 0x100;
  static constexpr uint32_t MEMCONF_EVENTS_RAMOFF0 = 0x120;
  // Status
  static constexpr uint32_t MEMCONF_RAMSTATUS = 0x400;
  // NVMC
  static constexpr uint32_t NVMC_READPROTECTED  = 0x530;
  static constexpr uint32_t NVMC_WRITEPROTECTED = 0x534;
  // Interrupt enable
  static constexpr uint32_t INTENSET_OFFSET = 0x304;
  static constexpr uint32_t INTENCLR_OFFSET = 0x308;
};

}  // namespace xiao_nrf54l15
