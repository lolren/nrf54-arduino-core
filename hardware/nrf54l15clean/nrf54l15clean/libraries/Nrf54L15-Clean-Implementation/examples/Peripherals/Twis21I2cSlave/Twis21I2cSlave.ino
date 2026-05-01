/*
 * Twis21 I2C Slave — TWIS21 as I2C Target
 *
 * TWIS21 (PERI domain) provides I2C slave functionality with
 * EasyDMA buffers for receive and transmit.
 *
 * This example sets up TWIS21 as a slave device at address 0x42
 * and demonstrates receiving data from a master.
 *
 * Hardware: XIAO nRF54L15
 * Pins:     SCL=P0.16, SDA=P0.17 (adjust for your board)
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static const uint8_t SLAVE_ADDR = 0x42;
static const uint8_t TX_DATA[] = "HELLO TWIS21";
static uint8_t rxBuffer[64];

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  TWIS21 — I2C Slave Device"));
  Serial.println(F("======================================"));
  Serial.println();

  Twis twis(nrf54l15::TWIS21_BASE);

  Serial.print(F("  TWIS21 base: 0x"));
  Serial.println(nrf54l15::TWIS21_BASE, HEX);
  Serial.println();

  // List all TWIS instances
  Serial.println(F("--- All TWIS Instances ---"));
  Serial.print(F("  TWIS20: 0x"));
  Serial.println(nrf54l15::TWIS20_BASE, HEX);
  Serial.print(F("  TWIS21: 0x"));
  Serial.println(nrf54l15::TWIS21_BASE, HEX);
  Serial.print(F("  TWIS22: 0x"));
  Serial.println(nrf54l15::TWIS22_BASE, HEX);
  Serial.print(F("  TWIS30: 0x"));
  Serial.println(nrf54l15::TWIS30_BASE, HEX);
  Serial.println();

  // Start TWIS21 as slave
  Serial.println(F("--- TWIS21 Configuration ---"));
  bool ok = twis.begin({0, 16}, {0, 17}, SLAVE_ADDR,
                       rxBuffer, sizeof(rxBuffer),
                       TX_DATA, sizeof(TX_DATA));
  Serial.print(F("  begin() result: "));
  Serial.println(ok ? F("OK") : F("FAIL"));

  if (ok) {
    Serial.print(F("  Address: 0x"));
    Serial.print(SLAVE_ADDR, HEX);
    Serial.print(F("  Pins: SCL=P0.16, SDA=P0.17"));
    Serial.println();
    Serial.print(F("  TX buffer: "));
    Serial.print(sizeof(TX_DATA));
    Serial.print(F(" bytes (\""));
    Serial.print((const char*)TX_DATA);
    Serial.println(F("\")"));
    Serial.print(F("  RX buffer: "));
    Serial.println(sizeof(rxBuffer));
    Serial.println();

    twis.setOverReadChar(0x00);

    Serial.println(F("  TWIS21 ready — waiting for I2C master..."));
    Serial.println(F("  (Connect an I2C master to read/write this device)"));
    Serial.println(F("  Polling for events..."));
    Serial.println();

    // Poll for I2C events (demonstration — real usage would use interrupts)
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

    // Show results
    Serial.println();
    Serial.println(F("--- TWIS21 Session Results ---"));
    Serial.print(F("  RX amount: "));
    Serial.println(twis.rxAmount());
    Serial.print(F("  TX amount: "));
    Serial.println(twis.txAmount());

    twis.end();
    Serial.println(F("  TWIS21 disabled"));
  }
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  TWIS21 demo complete."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
