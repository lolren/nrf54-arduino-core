/*
 * Memconf Power — RAM Section Power Control Demo
 *
 * Demonstrates reading RAM section power status and cycling
 * individual RAM sections. Each section is 32 KB on nRF54L15.
 *
 * WARNING: Powering off sections that contain active data (heap,
 * stack, global variables, or DMA buffers) will cause corruption.
 * This example only exercises sections 4-7 which are typically free.
 *
 * Hardware: XIAO nRF54L15 (256 KB RAM = 8 x 32 KB sections)
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  MEMCONF — RAM Section Power Control"));
  Serial.println(F("======================================"));
  Serial.println();

  // Read current power status
  uint32_t status = Memconf::ramSectionStatus();
  Serial.print(F("RAM sections powered: 0x"));
  Serial.println(status, HEX);

  for (int i = 0; i < 8; i++) {
    Serial.print(F("  Section "));
    Serial.print(i);
    Serial.print(F(": "));
    Serial.println(Memconf::isRamSectionPowered(i) ? F("ON") : F("OFF"));
  }
  Serial.println();

  // Safety: only cycle sections 6 and 7 (last 64 KB of RAM, typically free)
  Serial.println(F("--- Cycling sections 6-7 (safe zone) ---"));

  // Section 6
  if (Memconf::isRamSectionPowered(6)) {
    Serial.println(F("Powering off section 6..."));
    Memconf::powerOffRamSection(6);
    delay(100);
    if (!Memconf::isRamSectionPowered(6)) {
      Serial.println(F("  Section 6 confirmed OFF"));
    }

    Serial.println(F("Powering on section 6..."));
    Memconf::powerOnRamSection(6);
    delay(100);
    if (Memconf::isRamSectionPowered(6)) {
      Serial.println(F("  Section 6 confirmed ON (cycle OK)"));
    } else {
      Serial.println(F("  Section 6 still OFF (unexpected)"));
    }
  } else {
    Serial.println(F("Section 6 already OFF (skipping cycle)"));
  }

  // Section 7
  if (Memconf::isRamSectionPowered(7)) {
    Serial.println(F("Powering off section 7..."));
    Memconf::powerOffRamSection(7);
    delay(100);
    if (!Memconf::isRamSectionPowered(7)) {
      Serial.println(F("  Section 7 confirmed OFF"));
    }

    Serial.println(F("Powering on section 7..."));
    Memconf::powerOnRamSection(7);
    delay(100);
    if (Memconf::isRamSectionPowered(7)) {
      Serial.println(F("  Section 7 confirmed ON (cycle OK)"));
    } else {
      Serial.println(F("  Section 7 still OFF (unexpected)"));
    }
  } else {
    Serial.println(F("Section 7 already OFF (skipping cycle)"));
  }

  Serial.println();

  // Final status
  status = Memconf::ramSectionStatus();
  Serial.print(F("Final RAM sections powered: 0x"));
  Serial.println(status, HEX);

  Serial.println();
  Serial.println(F("======================================"));
  Serial.println(F("  MEMCONF demo complete."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
