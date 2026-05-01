/*
 * Wdt30 Secure Watchdog — Access Limitation Documentation
 *
 * The nRF54L15 has two watchdog timers:
 *
 *   WDT30 (Secure,   HF domain, 0x50108000) — NO non-secure alias
 *   WDT31 (Secure+NS, HF/LP domains) — Has non-secure alias
 *         Secure:  0x50109000
 *         Non-sec: 0x40109000
 *
 * WDT30 generates NMI (Non-Maskable Interrupt).
 * It is accessible ONLY from secure firmware.
 * Non-secure Arduino code cannot access WDT30.
 *
 * The accessible watchdog is WDT31 via its NS alias.
 * The existing Watchdog class in the core wraps WDT31.
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
  Serial.println(F("  WDT30/WDT31 — Watchdog Timer Overview"));
  Serial.println(F("======================================"));
  Serial.println();

  Serial.println(F("--- Watchdog Timer Instances ---"));
  Serial.println(F("  WDT30 (S only):   0x50108000 <-- NO NS alias"));
  Serial.println(F("  WDT31 (S):        0x50109000"));
  Serial.print(F("  WDT31 (NS alias): 0x"));
  Serial.println(nrf54l15::WDT31_BASE, HEX);
  Serial.println(F("  <-- Used by Arduino Watchdog class"));
  Serial.println();

  Serial.println(F("--- WDT30 Access Note ---"));
  Serial.println(F("  WDT30 generates NMI (non-maskable interrupt)."));
  Serial.println(F("  It can only be configured from secure firmware."));
  Serial.println(F("  Non-secure code (Arduino) cannot access WDT30."));
  Serial.println(F("  Attempting to read WDT30 registers will fault."));
  Serial.println();

  Serial.println(F("--- WDT31 (Accessible from Non-Secure) ---"));

  Watchdog wdtns;  // Uses WDT31_NS_BASE

  Serial.print(F("  WDT31 base: 0x"));
  Serial.println(nrf54l15::WDT31_BASE, HEX);

  Serial.println(F("  Configuring WDT31 with 100ms timeout..."));

  bool ok = wdtns.configure(100);
  Serial.print(F("  configure() result: "));
  Serial.println(ok ? F("OK") : F("FAIL"));

  if (ok) {
    wdtns.start();
    Serial.println(F("  WDT31 started."));

    Serial.println(F("  Feeding watchdog every 50ms..."));

    for (int i = 0; i < 10; i++) {
      delay(50);
      wdtns.feed();
      Serial.print(F("  Feed "));
      Serial.print(i + 1);
      Serial.println(F("/10"));
    }

    wdtns.stop();
    Serial.println(F("  WDT31 stopped."));
  }
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  WDT30/WDT31 demo complete."));
  Serial.println(F("  Use WDT31 (Watchdog class) for non-secure code."));
  Serial.println(F("  WDT30 requires secure firmware configuration."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
