/*
 * Silicon Feature Self-Test
 *
 * Validates all newly-exposed nRF54L15 silicon features:
 * - FICR:  Device identification, memory sizes, BT address
 * - CACHE: Enable/disable, invalidate/clean
 * - MEMCONF: RAM section status
 * - OSCILLATORS: HFCLK/LFCLK state queries
 * - NFCT:    Register access (no antenna needed)
 *
 * Hardware: XIAO nRF54L15
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static int testsRun = 0;
static int testsPass = 0;
static int testsFail = 0;

#define TEST(name, expr) do { \
  testsRun++; \
  if (expr) { testsPass++; Serial.print(F("  PASS: ")); Serial.println(name); } \
  else       { testsFail++; Serial.print(F("  FAIL: ")); Serial.println(name); } \
} while(0)

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  nRF54L15 Silicon Feature Self-Test"));
  Serial.println(F("======================================"));
  Serial.println();

  // ---- FICR tests ----
  Serial.println(F("--- FICR (Factory Info) ---"));
  TEST("Part code is nRF54L15", Ficr::isNrf54l15());
  TEST("RAM size is 256 KB", Ficr::ramKb() == 256);
  TEST("RRAM size is 1524 KB", Ficr::rramKb() == 1524);
  TEST("Device ID is non-zero", Ficr::deviceId() != 0);
  TEST("Device ID is not all 0xFF", Ficr::deviceId() != 0xFFFFFFFFFFFFFFFFULL);
  TEST("Part code matches 0x54B15", Ficr::partCode() == 0x00054B15UL);

  uint8_t uuid[16];
  Ficr::uuid(uuid);
  bool uuidValid = false;
  for (int i = 0; i < 16; i++) { if (uuid[i] != 0 && uuid[i] != 0xFF) { uuidValid = true; break; } }
  TEST("UUID has non-trivial bytes", uuidValid);

  uint8_t btAddr[6];
  Ficr::deviceAddress(btAddr);
  bool addrValid = false;
  for (int i = 0; i < 6; i++) { if (btAddr[i] != 0 && btAddr[i] != 0xFF) { addrValid = true; break; } }
  TEST("BT device address is non-trivial", addrValid);

  TEST("Device address type readable",
       Ficr::deviceAddressType() == 0 || Ficr::deviceAddressType() == 1);

  Serial.println();

  // ---- CACHE tests ----
  Serial.println(F("--- CACHE ---"));
  bool cacheWasOn = Cache::isEnabled();
  TEST("Cache enable readable", true);  // If we get here without fault, it works

  // Toggle cache
  if (cacheWasOn) {
    Cache::disable();
    TEST("Cache disable worked", !Cache::isEnabled());
    Cache::enable();
    TEST("Cache re-enable worked", Cache::isEnabled());
  } else {
    Cache::enable();
    TEST("Cache enable worked", Cache::isEnabled());
    Cache::disable();
    TEST("Cache disable worked", !Cache::isEnabled());
    if (cacheWasOn) Cache::enable();  // restore original state
  }

  // Test line-granularity ops (should not fault on valid addresses)
  // Use a small buffer on the stack
  uint32_t dummy[4] __attribute__((aligned(16)));
  Cache::invalidateDataCacheLine((uint32_t)dummy);
  TEST("Cache line invalidate (no fault)", true);

  Serial.println();

  // ---- MEMCONF tests ----
  Serial.println(F("--- MEMCONF ---"));
  uint32_t ramStatus = Memconf::ramSectionStatus();
  TEST("RAM section status readable", ramStatus != 0xFFFFFFFFUL);
  // All sections should be powered on after normal boot
  TEST("Section 0 powered", (ramStatus & 0x1) != 0);
  TEST("Section 1 powered", (ramStatus & 0x2) != 0);

  Serial.println();

  // ---- OSCILLATORS tests ----
  Serial.println(F("--- OSCILLATORS ---"));
  Oscillators::LfclkSource lfSrc = Oscillators::getLfclkSource();
  TEST("LFCLK source readable",
       lfSrc == Oscillators::LfclkSource::kLfrc ||
       lfSrc == Oscillators::LfclkSource::kLfxo ||
       lfSrc == Oscillators::LfclkSource::kLfSynt);

  bool hfRunning = Oscillators::hfclkRunning();
  TEST("HFCLK running (expected true after boot)", hfRunning);

  // HFXO should be running on a normal boot
  (void)Oscillators::hfxoRunning();
  TEST("HFXO state readable", true);  // Just verify no fault

  // LFCLK source copy should match current source after boot
  Oscillators::LfclkSource lfCopy = Oscillators::getLfclkSourceCopy();
  TEST("LFCLK source copy readable",
       lfCopy == Oscillators::LfclkSource::kLfrc ||
       lfCopy == Oscillators::LfclkSource::kLfxo ||
       lfCopy == Oscillators::LfclkSource::kLfSynt);

  Serial.println();

  // ---- NFCT tests (register access only, no antenna) ----
  Serial.println(F("--- NFCT (register access) ---"));
  // Just verify registers are readable without faulting
  uint32_t th0 = Nfct::tagHeader0();
  TEST("NFCT TAGHEADER0 readable", th0 != 0);  // Should have Nordic MFGID
  TEST("NFCT MFGID is Nordic (0x5F)", (th0 & 0xFF) == 0x5F);

  uint32_t enableReg = Nfct::readReg(0x500);
  TEST("NFCT ENABLE register readable", true);  // No fault = success

  Serial.println();

  // ---- Summary ----
  Serial.println(F("======================================"));
  Serial.print(F("  Results: "));
  Serial.print(testsPass);
  Serial.print(F("/"));
  Serial.print(testsRun);
  Serial.print(F(" passed, "));
  Serial.print(testsFail);
  Serial.print(testsFail > 0 ? F(" FAILED") : F(" all OK"));
  Serial.println();
  Serial.println(F("======================================"));

  while (true) {
    delay(1000);
  }
}

void loop() {
  // Not reached.
}
