#pragma once

#include <stdint.h>

#include "nrf54l15_regs.h"

namespace xiao_nrf54l15 {

// Instruction/Data Cache (CACHE)
// Controls the Cortex-M33 instruction and data cache.
// Datasheet chapter 4.2.5 (CACHE — Instruction/data cache).
class Cache {
 public:
  // ---- Cache control ----

  // Enable the cache (instruction + data).
  inline static void enable() {
    writeReg(CACHE_ENABLE, 1);
  }

  // Disable the cache.
  inline static void disable() {
    writeReg(CACHE_ENABLE, 0);
  }

  // Check if cache is enabled.
  inline static bool isEnabled() {
    return readReg(CACHE_ENABLE) != 0;
  }

  // ---- Cache maintenance (coherency with DMA / VPR shared memory) ----

  // Invalidate the entire data cache.
  // Use before reading data that may have been modified by DMA or VPR.
  inline static void invalidateDataCache() {
    writeReg(CACHE_DINVDALL, 1);
    // Wait for completion.
    while (isDataInvalidatePending()) { /* spin */ }
  }

  // Clean (write-back) the entire data cache.
  // Use before DMA reads from memory that the CPU has written to.
  inline static void cleanDataCache() {
    writeReg(CACHE_DCLEANALL, 1);
    while (isDataCleanPending()) { /* spin */ }
  }

  // Clean and invalidate the entire data cache.
  // Full coherence reset — use when both reading and writing via DMA.
  inline static void cleanInvalidateDataCache() {
    writeReg(CACHE_DCLEANINVDALL, 1);
    while (isDataCleanPending()) { /* spin */ }
  }

  // Invalidate the entire instruction cache.
  // Use after writing to flash (e.g., bootloader handoff).
  inline static void invalidateInstructionCache() {
    writeReg(CACHE_IINVDALL, 1);
    while (isInstrInvalidatePending()) { /* spin */ }
  }

  // ---- Line-granularity cache ops (for VPR shared memory regions) ----

  // Invalidate a data cache line by address.
  // addr must be cache-line aligned (16 bytes).
  inline static void invalidateDataCacheLine(uint32_t addr) {
    writeReg(CACHE_DINVDADDR, addr & 0xFFFFFFF0);
    while (isDataInvalidatePending()) { /* spin */ }
  }

  // Clean a data cache line by address.
  // addr must be cache-line aligned (16 bytes).
  inline static void cleanDataCacheLine(uint32_t addr) {
    writeReg(CACHE_DCLEANADDR, addr & 0xFFFFFFF0);
    while (isDataCleanPending()) { /* spin */ }
  }

  // Clean and invalidate a data cache line by address.
  inline static void cleanInvalidateDataCacheLine(uint32_t addr) {
    writeReg(CACHE_DCLEANINVDADDR, addr & 0xFFFFFFF0);
    while (isDataCleanPending()) { /* spin */ }
  }

  // Invalidate an instruction cache line by address.
  inline static void invalidateInstructionCacheLine(uint32_t addr) {
    writeReg(CACHE_IINVDADDR, addr & 0xFFFFFFF0);
    while (isInstrInvalidatePending()) { /* spin */ }
  }

  // ---- Convenience: flush a buffer for DMA compatibility ----

  // Clean the data cache for a given buffer so DMA can read correct data.
  // len is rounded up to the next cache line.
  inline static void cleanForDma(const void* ptr, size_t len) {
    if (!ptr || len == 0) return;
    uint32_t start = reinterpret_cast<uint32_t>(ptr) & 0xFFFFFFF0;
    uint32_t end   = (reinterpret_cast<uint32_t>(ptr) + len + 15) & 0xFFFFFFF0;
    for (uint32_t addr = start; addr < end; addr += 16) {
      cleanDataCacheLine(addr);
    }
  }

  // Invalidate the data cache for a given buffer so CPU reads fresh DMA data.
  inline static void invalidateForDma(void* ptr, size_t len) {
    if (!ptr || len == 0) return;
    uint32_t start = reinterpret_cast<uint32_t>(ptr) & 0xFFFFFFF0;
    uint32_t end   = (reinterpret_cast<uint32_t>(ptr) + len + 15) & 0xFFFFFFF0;
    for (uint32_t addr = start; addr < end; addr += 16) {
      invalidateDataCacheLine(addr);
    }
  }

  // Clean then invalidate — full coherence reset for a buffer.
  inline static void cleanInvalidateForDma(void* ptr, size_t len) {
    if (!ptr || len == 0) return;
    uint32_t start = reinterpret_cast<uint32_t>(ptr) & 0xFFFFFFF0;
    uint32_t end   = (reinterpret_cast<uint32_t>(ptr) + len + 15) & 0xFFFFFFF0;
    for (uint32_t addr = start; addr < end; addr += 16) {
      cleanInvalidateDataCacheLine(addr);
    }
  }

  // ---- Status ----

  inline static bool isDataInvalidatePending() {
    return (readReg(CACHE_STATUS) & CACHE_STATUS_DINVALL_Msk) != 0;
  }

  inline static bool isDataCleanPending() {
    return (readReg(CACHE_STATUS) & CACHE_STATUS_DCLEANALL_Msk) != 0;
  }

  inline static bool isInstrInvalidatePending() {
    return (readReg(CACHE_STATUS) & CACHE_STATUS_IINVALL_Msk) != 0;
  }

  // ---- Low-level ----

  inline static uint32_t readReg(uint32_t offset) {
    return *reinterpret_cast<const volatile uint32_t*>(BASE + offset);
  }

  inline static void writeReg(uint32_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(BASE + offset) = value;
  }

 private:
  static constexpr uint32_t BASE = 0x4004B000UL;  // CACHE base (MCU domain, non-secure alias)

  // Register offsets from datasheet.
  static constexpr uint32_t CACHE_ENABLE         = 0x500;
  static constexpr uint32_t CACHE_DINVDALL       = 0x004;  // Task: invalidate all data
  static constexpr uint32_t CACHE_DCLEANALL      = 0x008;  // Task: clean all data
  static constexpr uint32_t CACHE_DCLEANINVDALL  = 0x00C;  // Task: clean + invalidate all data
  static constexpr uint32_t CACHE_IINVDALL       = 0x010;  // Task: invalidate all instructions
  static constexpr uint32_t CACHE_DINVDADDR      = 0x020;  // Task: invalidate data by address
  static constexpr uint32_t CACHE_DCLEANADDR     = 0x024;  // Task: clean data by address
  static constexpr uint32_t CACHE_DCLEANINVDADDR = 0x028;  // Task: clean+invalidate data by address
  static constexpr uint32_t CACHE_IINVDADDR      = 0x02C;  // Task: invalidate instr by address
  static constexpr uint32_t CACHE_STATUS         = 0x400;

  // Status bit masks.
  static constexpr uint32_t CACHE_STATUS_DINVALL_Msk    = (1UL << 0);
  static constexpr uint32_t CACHE_STATUS_DCLEANALL_Msk  = (1UL << 1);
  static constexpr uint32_t CACHE_STATUS_IINVALL_Msk    = (1UL << 2);
};

}  // namespace xiao_nrf54l15
