/*
 * Spim22 Multi-Instance — Second SPIM Instance
 *
 * The nRF54L15 has multiple SPIM instances:
 *   SPIM20 (PERI domain) — 0x400C3000 NS
 *   SPIM21 (PERI domain) — 0x400C4000 NS (used by Arduino Wire/SPI)
 *   SPIM22 (PERI domain) — 0x400C8000 NS
 *   SPIM30 (LP domain)   — 0x40104000 NS
 *
 * This example demonstrates using SPIM22 as a second SPI master.
 *
 * Hardware: XIAO nRF54L15
 * Pins:     SCK=P0.18, MOSI=P0.17, MISO=P0.19, CS=P0.20 (adjust for your board)
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  SPIM22 — Second SPI Master Instance"));
  Serial.println(F("======================================"));
  Serial.println();

  // Create SPIM22 instance (reuse the Spim class with SPIM22 base)
  Spim spim22(nrf54l15::SPIM22_BASE);

  Serial.print(F("  SPIM22 base: 0x"));
  Serial.println(nrf54l15::SPIM22_BASE, HEX);
  Serial.println();

  // List all SPIM instances
  Serial.println(F("--- All SPIM Instances ---"));
  Serial.print(F("  SPIM20: 0x"));
  Serial.println(nrf54l15::SPIM20_BASE, HEX);
  Serial.print(F("  SPIM21: 0x"));
  Serial.println(nrf54l15::SPIM21_BASE, HEX);
  Serial.print(F("  SPIM22: 0x"));
  Serial.println(nrf54l15::SPIM22_BASE, HEX);
  Serial.print(F("  SPIM30: 0x"));
  Serial.println(nrf54l15::SPIM30_BASE, HEX);
  Serial.println();

  // Configure SPIM22
  Serial.println(F("--- SPIM22 Configuration ---"));
  bool ok = spim22.begin({0, 18}, {0, 17}, {0, 19}, {0, 20}, 4000000);
  Serial.print(F("  begin() result: "));
  Serial.println(ok ? F("OK") : F("FAIL"));

  if (ok) {
    Serial.print(F("  Pins: SCK=P0.18, MOSI=P0.17, MISO=P0.19, CS=P0.20"));
    Serial.println();
    Serial.print(F("  Frequency: 4 MHz"));
    Serial.println();

    // Test loopback (if MISO connected to MOSI)
    Serial.println();
    Serial.println(F("--- Transfer Test (loopback) ---"));
    uint8_t txData[] = {0xAA, 0x55, 0xF0, 0x0F, 0xFF};
    uint8_t rxData[sizeof(txData)];
    bool result = spim22.transfer(txData, rxData, sizeof(txData));
    Serial.print(F("  Transfer result: "));
    Serial.println(result ? F("OK") : F("FAIL"));

    if (result) {
      Serial.print(F("  TX: "));
      for (size_t i = 0; i < sizeof(txData); i++) {
        Serial.print(F("0x"));
        Serial.print(txData[i], HEX);
        Serial.print(F(" "));
      }
      Serial.println();
      Serial.print(F("  RX: "));
      for (size_t i = 0; i < sizeof(rxData); i++) {
        Serial.print(F("0x"));
        Serial.print(rxData[i], HEX);
        Serial.print(F(" "));
      }
      Serial.println();
    }

    spim22.end();
    Serial.println(F("  SPIM22 disabled"));
  }
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  SPIM22 demo complete."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
