/*
 * Spu Protect Domain — SPU (System Protection Unit) Overview
 *
 * The nRF54L15 has four SPU instances:
 *   SPU00 (HF domain): 0x50040000 — Secures HF peripherals
 *   SPU10 (HF domain): 0x50080000 — Secures HF peripherals  
 *   SPU20 (HF domain): 0x500C0000 — Secures HF peripherals
 *   SPU30 (HF domain): 0x50100000 — Secures LP peripherals
 *
 * ALL SPU instances are SECURE-ONLY (S, no NSA).
 *
 * The SPU controls access permissions for peripherals and DPPI channels.
 * It works alongside MPC (Memory Privilege Controller) to enforce
 * the TrustZone security model.
 *
 * From non-secure Arduino code:
 *   - SPU registers are NOT accessible (any access will fault)
 *   - The SPU is configured by the secure bootloader/MCU firmware
 *   - You can read what's configured via peripheral-specific status
 *
 * This example documents the SPU architecture and shows how to
 * query peripheral access information from non-secure code.
 *
 * Hardware: XIAO nRF54L15
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  SPU — System Protection Unit"));
  Serial.println(F("======================================"));
  Serial.println();

  Serial.println(F("--- SPU Instances (All Secure-Only) ---"));
  Serial.println(F("  SPU00: 0x50040000 (HF domain, peripherals AAR/ECB/CRA/SPIS00)"));
  Serial.println(F("  SPU10: 0x50080000 (HF domain, peripherals GPIOTE10/QDEC/TIMER10)"));
  Serial.println(F("  SPU20: 0x500C0000 (HF domain, peripherals TWIM20-22/PDM/GRTC)"));
  Serial.println(F("  SPU30: 0x50100000 (HF domain, peripherals TWIM30/SPIM30/POWER)"));
  Serial.println();

  Serial.println(F("--- SPU Architecture ---"));
  Serial.println(F("  Each SPU has:"));
  Serial.println(F("    PERIPH[n].PERM  — Peripheral access permission (S/NS/Split)"));
  Serial.println(F("    DPPI[n].PERM   — DPPI channel access permission"));
  Serial.println(F("    PERIPHACCERR   — Error status register"));
  Serial.println(F("    EVENTS_PERIPHACCERR — Error event"));
  Serial.println();

  Serial.println(F("--- Access from Non-Secure Code ---"));
  Serial.println(F("  Direct SPU register access: NOT POSSIBLE (will fault)"));
  Serial.println(F("  The secure bootloader configures SPU permissions at boot."));
  Serial.println(F("  Peripherals with Split security (SA) have both S and NS aliases."));
  Serial.println(F("  Non-secure peripherals (NS) are accessible to this code."));
  Serial.println();

  // Show which peripherals are accessible from non-secure
  Serial.println(F("--- Non-Secure Accessible Peripherals ---"));
  Serial.print(F("  SPIM20:  0x"));
  Serial.println(nrf54l15::SPIM20_BASE, HEX);
  Serial.print(F("  SPIM21:  0x"));
  Serial.println(nrf54l15::SPIM21_BASE, HEX);
  Serial.print(F("  SPIM22:  0x"));
  Serial.println(nrf54l15::SPIM22_BASE, HEX);
  Serial.print(F("  SPIM30:  0x"));
  Serial.println(nrf54l15::SPIM30_BASE, HEX);
  Serial.print(F("  TWIS20:  0x"));
  Serial.println(nrf54l15::TWIS20_BASE, HEX);
  Serial.print(F("  TWIS21:  0x"));
  Serial.println(nrf54l15::TWIS21_BASE, HEX);
  Serial.print(F("  TWIS22:  0x"));
  Serial.println(nrf54l15::TWIS22_BASE, HEX);
  Serial.print(F("  TWIS30:  0x"));
  Serial.println(nrf54l15::TWIS30_BASE, HEX);
  Serial.print(F("  WDT31:   0x"));
  Serial.println(nrf54l15::WDT31_BASE, HEX);
  Serial.print(F("  TIMER00: 0x"));
  Serial.println(nrf54l15::TIMER00_BASE, HEX);
  Serial.print(F("  GRTC:    0x"));
  Serial.println(nrf54l15::GRTC_BASE, HEX);
  Serial.println();

  Serial.println(F("=== Secure-Only Peripherals (No NS Alias) ==="));
  Serial.println(F("  WDT30, SPU00-30, MPC, COMP, DPPIC00-30 (partially)"));
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  SPU architecture overview complete."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
