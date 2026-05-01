/*
 * Twis30 I2C Slave — LP-Domain I2C Target
 *
 * TWIS30 (LP domain) shares its bus with SPIM30, TWIM30, UARTE30.
 * Like all TWIS instances, it provides I2C slave with EasyDMA.
 *
 * This example sets up TWIS30 as a slave device at address 0x40.
 *
 * Hardware: XIAO nRF54L15
 * Pins:     SCL=P0.24, SDA=P0.25 (adjust for your board)
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static const uint8_t SLAVE_ADDR = 0x40;
static const uint8_t TX_DATA[] = "TWIS30 LP DOMAIN";
static uint8_t rxBuffer[64];

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  TWIS30 — LP-Domain I2C Slave"));
  Serial.println(F("======================================"));
  Serial.println();

  Twis twis(nrf54l15::TWIS30_BASE);

  Serial.print(F("  TWIS30 base: 0x"));
  Serial.println(nrf54l15::TWIS30_BASE, HEX);
  Serial.println();

  Serial.println(F("--- LP Domain Sharing ---"));
  Serial.print(F("  SPIM30: 0x"));
  Serial.println(nrf54l15::SPIM30_BASE, HEX);
  Serial.print(F("  TWIM30: 0x"));
  Serial.println(nrf54l15::TWIM30_BASE, HEX);
  Serial.print(F("  TWIS30: 0x"));
  Serial.println(nrf54l15::TWIS30_BASE, HEX);
  Serial.print(F("  UARTE30: 0x"));
  Serial.println(nrf54l15::UARTE30_BASE, HEX);
  Serial.println(F("  Note: All share the LP domain bus"));
  Serial.println();

  Serial.println(F("--- TWIS30 Configuration ---"));
  bool ok = twis.begin({0, 24}, {0, 25}, SLAVE_ADDR,
                       rxBuffer, sizeof(rxBuffer),
                       TX_DATA, sizeof(TX_DATA));
  Serial.print(F("  begin() result: "));
  Serial.println(ok ? F("OK") : F("FAIL"));

  if (ok) {
    Serial.print(F("  Address: 0x"));
    Serial.print(SLAVE_ADDR, HEX);
    Serial.print(F("  Pins: SCL=P0.24, SDA=P0.25"));
    Serial.println();
    Serial.println();
    Serial.println(F("  TWIS30 ready — waiting for I2C master..."));
    Serial.println();

    // Poll for events
    for (int i = 0; i < 20; i++) {
      if (twis.writeReceived()) {
        Serial.print(F("  WRITE received, bytes: "));
        Serial.println(twis.rxAmount());
        break;
      }
      if (twis.readReceived()) {
        Serial.print(F("  READ received, bytes sent: "));
        Serial.println(twis.txAmount());
        break;
      }
      if (twis.stopped()) {
        Serial.println(F("  STOP detected"));
        break;
      }
      delay(100);
    }

    Serial.println();
    Serial.print(F("  RX amount: "));
    Serial.println(twis.rxAmount());
    Serial.print(F("  TX amount: "));
    Serial.println(twis.txAmount());

    twis.end();
  }
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  TWIS30 demo complete."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
