/*
 * Tampc Tamper Probe — TAMPC Peripheral State and Configuration
 *
 * The nRF54L15 TAMPC peripheral monitors internal and external tamper
 * sources and can trigger system reset or flash erase on detection.
 *
 * This example probes the existing Tampc class from the core HAL
 * and exercises its configuration methods.
 *
 * WARNING: Changing tamper response settings can cause a system
 * reset or erase user flash. This example queries and does not
 * modify the tamper response action.
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
  Serial.println(F("  TAMPC — Tamper Detection State Probe"));
  Serial.println(F("======================================"));
  Serial.println();

  // Create Tampc instance (uses TAMPC_BASE from regs.h)
  Tampc tampc;

  // ---- Status ----
  Serial.println(F("--- TAMPC Status ---"));
  uint32_t status = tampc.status();
  Serial.print(F("  Status: 0x"));
  Serial.println(status, HEX);

  bool tamper = tampc.tamperDetected();
  Serial.print(F("  Tamper detected: "));
  Serial.println(tamper ? F("Yes") : F("No"));

  bool writeError = tampc.writeErrorDetected();
  Serial.print(F("  Write error: "));
  Serial.println(writeError ? F("Yes") : F("No"));
  Serial.println();

  // ---- Tamper response settings ----
  Serial.println(F("--- Tamper Response Configuration ---"));
  Serial.print(F("  Internal reset on tamper: "));
  Serial.println(tampc.internalResetOnTamperEnabled() ? F("Enabled") : F("Disabled"));

  Serial.print(F("  External reset on tamper: "));
  Serial.println(tampc.externalResetOnTamperEnabled() ? F("Enabled") : F("Disabled"));

  Serial.print(F("  Erase protect: "));
  Serial.println(tampc.eraseProtectEnabled() ? F("Enabled") : F("Disabled"));
  Serial.println();

  // ---- Monitor sources ----
  Serial.println(F("--- Monitor Sources ---"));
  Serial.print(F("  CRACEN tamper monitor: "));
  Serial.println(tampc.cracenTamperMonitorEnabled() ? F("Enabled") : F("Disabled"));

  Serial.print(F("  Active shield monitor: "));
  Serial.println(tampc.activeShieldMonitorEnabled() ? F("Enabled") : F("Disabled"));

  Serial.print(F("  Glitch (slow) monitor: "));
  Serial.println(tampc.glitchSlowMonitorEnabled() ? F("Enabled") : F("Disabled"));

  Serial.print(F("  Glitch (fast) monitor: "));
  Serial.println(tampc.glitchFastMonitorEnabled() ? F("Enabled") : F("Disabled"));
  Serial.println();

  // ---- Domain debug control ----
  Serial.println(F("--- Domain Debug Control (Domain 0) ---"));
  Serial.print(F("  DBGEN: "));
  Serial.println(tampc.domainDbgenEnabled(0) ? F("Enabled") : F("Disabled"));
  Serial.print(F("  NIDEN: "));
  Serial.println(tampc.domainNidenEnabled(0) ? F("Enabled") : F("Disabled"));
  Serial.print(F("  SPIDEN: "));
  Serial.println(tampc.domainSpidenEnabled(0) ? F("Enabled") : F("Disabled"));
  Serial.print(F("  SPNIDEN: "));
  Serial.println(tampc.domainSpnidenEnabled(0) ? F("Enabled") : F("Disabled"));
  Serial.println();

  // ---- Poll tamper event ----
  Serial.println(F("--- Event Polling ---"));
  bool tamperEvent = tampc.pollTamper();
  Serial.print(F("  Tamper event pending: "));
  Serial.println(tamperEvent ? F("Yes") : F("No"));

  bool writeErrorEvent = tampc.pollWriteError();
  Serial.print(F("  Write error event pending: "));
  Serial.println(writeErrorEvent ? F("Yes") : F("No"));
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  TAMPC state probe complete."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
