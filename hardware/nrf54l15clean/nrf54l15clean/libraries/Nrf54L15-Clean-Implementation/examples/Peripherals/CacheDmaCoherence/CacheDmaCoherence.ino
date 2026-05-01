/*
 * Cache DMA Coherence — Cache Maintenance for DMA Buffers
 *
 * Demonstrates the Cache class DMA coherence helpers:
 * - cleanForDma()    : write-back CPU changes before DMA reads
 * - invalidateForDma(): discard stale cache lines after DMA writes
 * - cleanInvalidateForDma(): full coherence reset
 *
 * This is essential when using DMA peripherals (UARTE, SPIM, TWIM,
 * SAADC, PDM, I2S) with cacheable RAM on the nRF54L15.
 *
 * Hardware: XIAO nRF54L15
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

// Allocate a 64-byte buffer aligned to 16 bytes (cache line size).
// In real code, use __attribute__((aligned(16))) or new with alignment.
alignas(16) static uint8_t dmaBuffer[64];

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  Cache — DMA Coherence Helpers"));
  Serial.println(F("======================================"));
  Serial.println();

  // Check cache state
  Serial.print(F("Cache enabled: "));
  Serial.println(Cache::isEnabled() ? F("Yes") : F("No"));
  Serial.println();

  // Ensure cache is enabled for the demo
  Cache::enable();
  Serial.println(F("Cache enabled for demo."));
  Serial.println();

  // Fill buffer with known data
  for (int i = 0; i < 64; i++) {
    dmaBuffer[i] = (uint8_t)i;
  }
  Serial.println(F("Buffer filled with 0x00..0x3F"));
  Serial.println();

  // --- Clean for DMA (write-back CPU writes to RAM) ---
  Serial.println(F("--- Clean for DMA ---"));
  Serial.print(F("Buffer address: 0x"));
  Serial.println((uint32_t)dmaBuffer, HEX);
  Cache::cleanForDma(dmaBuffer, sizeof(dmaBuffer));
  Serial.println(F("  Cache cleaned (DMA can now read correct data)"));
  Serial.println();

  // --- Invalidate for DMA (discard stale cache lines) ---
  // Simulate: DMA wrote new data, now CPU needs to read it fresh
  Serial.println(F("--- Invalidate for DMA ---"));
  Cache::invalidateForDma(dmaBuffer, sizeof(dmaBuffer));
  Serial.println(F("  Cache invalidated (CPU will read fresh data from RAM)"));
  Serial.println();

  // --- Clean + Invalidate for DMA (full coherence reset) ---
  Serial.println(F("--- Clean + Invalidate for DMA ---"));
  Cache::cleanInvalidateForDma(dmaBuffer, sizeof(dmaBuffer));
  Serial.println(F("  Full coherence reset (safe for both read & write)"));
  Serial.println();

  // --- Line-granularity operations ---
  Serial.println(F("--- Line-granularity cache ops ---"));
  // First cache line of the buffer
  uint32_t lineAddr = (uint32_t)dmaBuffer & 0xFFFFFFF0;
  Cache::cleanDataCacheLine(lineAddr);
  Serial.println(F("  Single line cleaned"));
  Cache::invalidateDataCacheLine(lineAddr);
  Serial.println(F("  Single line invalidated"));
  Cache::cleanInvalidateDataCacheLine(lineAddr);
  Serial.println(F("  Single line clean+invalidated"));
  Serial.println();

  // --- Full cache maintenance ---
  Serial.println(F("--- Full cache maintenance ---"));
  Cache::cleanDataCache();
  Serial.println(F("  Full data cache cleaned"));
  Cache::invalidateDataCache();
  Serial.println(F("  Full data cache invalidated"));
  Cache::invalidateInstructionCache();
  Serial.println(F("  Instruction cache invalidated"));
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  Cache DMA coherence demo complete."));
  Serial.println(F("======================================"));
  Serial.println();
  Serial.println(F("Note: In real DMA code, always call these helpers"));
  Serial.println(F("before/after DMA transfers on cacheable RAM."));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
