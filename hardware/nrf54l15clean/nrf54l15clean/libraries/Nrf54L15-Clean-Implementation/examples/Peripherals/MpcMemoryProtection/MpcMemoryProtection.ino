/*
 * Mpc Memory Protection — MPC (Memory Privilege Controller) Overview
 *
 * The nRF54L15 MPC (Memory Privilege Controller) enforces TrustZone
 * memory protection. It has three instances:
 *
 *   MPC0 (Core 0 memory): 0x50000000 — Secure only
 *   MPC1 (Core 1 memory):  0x50001000 — Secure only
 *   MPC2 (Peripheral mem): 0x50002000 — Secure only
 *
 * ALL MPC instances are SECURE-ONLY (S, no NSA).
 *
 * The MPC protects:
 *   - Internal flash (code, data, UICR)
 *   - Internal RAM (MCU, PERI, LP domains)
 *   - External memory (QSPI flash if present)
 *
 * Memory regions are configured as:
 *   - Secure-only (S)
 *   - Non-secure only (NS)
 *   - Split (both S and NS have separate views)
 *
 * From non-secure Arduino code:
 *   - MPC registers are NOT accessible (any access will fault)
 *   - The secure bootloader configures MPC at boot
 *   - The MEMCONF wrapper shows RAM section power status
 *
 * Hardware: XIAO nRF54L15
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  MPC — Memory Privilege Controller"));
  Serial.println(F("======================================"));
  Serial.println();

  Serial.println(F("--- MPC Architecture ---"));
  Serial.println(F("  MPC0:  0x50000000 (Core 0 / ARM CoreSight)"));
  Serial.println(F("  MPC1:  0x50001000 (Core 1 / RISC-V VPR)"));
  Serial.println(F("  MPC2:  0x50002000 (Peripheral memory regions)"));
  Serial.println(F("  All instances: SECURE-ONLY (no non-secure alias)"));
  Serial.println();

  Serial.println(F("--- Memory Layout (nRF54L15) ---"));
  Serial.println(F("  Internal Flash:"));
  Serial.println(F("    App (Secure):     0x00000000 - 0x001F7FFF (2 MB)"));
  Serial.println(F("    Bootloader:       0x001F8000 - 0x001FFFFF"));
  Serial.println(F("    UICR:             0x00FF8000 - 0x00FFFFFF"));
  Serial.println();
  Serial.println(F("  Internal RAM:"));
  Serial.println(F("    MCU domain:       0x20000000 - 0x2000FFFF (64 KB)"));
  Serial.println(F("    PERI domain:      0x20010000 - 0x2002FFFF"));
  Serial.println(F("    LP domain:        0x20040000 - 0x2004FFFF"));
  Serial.println(F("    RRAM (Secure):    0x20180000 - 0x2023BFFF (1524 KB)"));
  Serial.println();

  Serial.println(F("--- Access from Non-Secure Code ---"));
  Serial.println(F("  Direct MPC register access: NOT POSSIBLE (will fault)"));
  Serial.println(F("  The secure bootloader configures MPC regions at boot."));
  Serial.println();

  // Show RAM section power status via MEMCONF
  Serial.println(F("--- RAM Section Power (via MEMCONF) ---"));
  uint32_t status = Memconf::ramSectionStatus();
  Serial.print(F("  RAM sections powered: 0x"));
  Serial.println(status, HEX);
  for (int i = 0; i < 8; i++) {
    Serial.print(F("  Section "));
    Serial.print(i);
    Serial.print(F(": "));
    Serial.println(Memconf::isRamSectionPowered(i) ? F("ON") : F("OFF"));
  }
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  MPC architecture overview complete."));
  Serial.println(F("  MEMCONF shows RAM section power status."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
