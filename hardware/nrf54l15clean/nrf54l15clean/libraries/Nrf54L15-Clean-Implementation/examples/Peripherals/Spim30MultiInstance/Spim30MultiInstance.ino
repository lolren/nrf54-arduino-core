/*
 * Spim30 Multi-Instance — LP-Domain SPI Master
 *
 * SPIM30 runs in the LP (Low Power) domain, sharing the bus with
 * UARTE30, TWIM30, and TWIS30. It can operate independently of
 * the MCU clock domain.
 *
 * This example demonstrates SPIM30 configuration and basic transfer.
 *
 * Hardware: XIAO nRF54L15
 * Pins:     SCK=P0.24, MOSI=P0.23, MISO=P0.25, CS=P0.26 (adjust for your board)
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  SPIM30 — LP-Domain SPI Master"));
  Serial.println(F("======================================"));
  Serial.println();

  Spim spim30(nrf54l15::SPIM30_BASE);

  Serial.print(F("  SPIM30 base: 0x"));
  Serial.println(nrf54l15::SPIM30_BASE, HEX);
  Serial.println();

  // SPIM30 is LP domain — note it shares with UARTE30/TWIM30/TWIS30
  Serial.println(F("--- LP Domain Peripheral Sharing ---"));
  Serial.print(F("  SPIM30:  0x"));
  Serial.println(nrf54l15::SPIM30_BASE, HEX);
  Serial.print(F("  UARTE30: 0x"));
  Serial.println(nrf54l15::UARTE30_BASE, HEX);
  Serial.print(F("  TWIM30:  0x"));
  Serial.println(nrf54l15::TWIM30_BASE, HEX);
  Serial.print(F("  TWIS30:  0x"));
  Serial.println(nrf54l15::TWIS30_BASE, HEX);
  Serial.println(F("  Note: These share the LP domain bus"));
  Serial.println();

  Serial.println(F("--- SPIM30 Configuration ---"));
  bool ok = spim30.begin({0, 24}, {0, 23}, {0, 25}, {0, 26}, 2000000);
  Serial.print(F("  begin() result: "));
  Serial.println(ok ? F("OK") : F("FAIL"));

  if (ok) {
    Serial.println(F("  Pins: SCK=P0.24, MOSI=P0.23, MISO=P0.25, CS=P0.26"));
    Serial.println(F("  Frequency: 2 MHz"));

    // Simple transfer test
    Serial.println();
    Serial.println(F("--- Transfer Test ---"));
    uint8_t tx[] = {0x01, 0x02, 0x03};
    uint8_t rx[sizeof(tx)];
    bool result = spim30.transfer(tx, rx, sizeof(tx));
    Serial.print(F("  Transfer: "));
    Serial.println(result ? F("OK") : F("FAIL"));

    if (result) {
      Serial.print(F("  RX: "));
      for (size_t i = 0; i < sizeof(rx); i++) {
        Serial.print(F("0x"));
        Serial.print(rx[i], HEX);
        Serial.print(F(" "));
      }
      Serial.println();
    }

    spim30.end();
  }
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  SPIM30 demo complete."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
