/*
 * CracenPkeStateProbe — CRACEN PKE Register State Probe
 *
 * Probes the CRACEN PKE registers to verify they are accessible
 * and report the current state. Demonstrates enable/disable,
 * status queries, and data/code memory access.
 *
 * NOTE: Full ECDSA requires Nordic's PKE microcode library.
 * This example only exercises the register interface.
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
  Serial.println(F("  CRACEN PKE — Register State Probe"));
  Serial.println(F("======================================"));
  Serial.println();

  // ---- CRACEN status ----
  Serial.println(F("--- CRACEN Status ---"));
  bool wasEnabled = CracenPke::isEnabled();
  Serial.print(F("  Enabled: "));
  Serial.println(wasEnabled ? F("Yes") : F("No"));

  bool wasReady = CracenPke::ready();
  Serial.print(F("  Ready: "));
  Serial.println(wasReady ? F("Yes") : F("No"));
  Serial.println();

  // ---- PKE status ----
  Serial.println(F("--- PKE Status ---"));
  Serial.print(F("  PKE busy: "));
  Serial.println(CracenPke::pkeBusy() ? F("Yes") : F("No"));
  Serial.print(F("  PKE ready: "));
  Serial.println(CracenPke::pkeReady() ? F("Yes") : F("No"));
  Serial.print(F("  PKE status: 0x"));
  Serial.println(CracenPke::pkeStatus(), HEX);
  Serial.println();

  // ---- PKE capacity ----
  Serial.println(F("--- PKE Capacity ---"));
  Serial.print(F("  Data RAM: "));
  Serial.print(CracenPke::pkeDataSize() / 1024);
  Serial.println(F(" KB"));
  Serial.print(F("  Code RAM: "));
  Serial.print(CracenPke::pkeCodeSize() / 1024);
  Serial.println(F(" KB"));
  Serial.println();

  // ---- Enable/disable cycle ----
  Serial.println(F("--- Enable/Disable Cycle ---"));
  CracenPke::enable();
  Serial.println(F("  CRACEN enabled"));
  if (CracenPke::waitForReady(500000)) {
    Serial.println(F("  CRACEN ready after enable"));
  } else {
    Serial.println(F("  CRACEN not ready within timeout"));
  }

  CracenPke::enablePkeIkg();
  Serial.println(F("  PKE+IKG enabled"));
  Serial.print(F("  PKE busy after enable: "));
  Serial.println(CracenPke::pkeBusy() ? F("Yes") : F("No"));

  CracenPke::disablePkeIkg();
  Serial.println(F("  PKE+IKG disabled"));

  CracenPke::disable();
  Serial.println(F("  CRACEN disabled"));
  Serial.println();

  // ---- Data RAM read/write test ----
  Serial.println(F("--- Data RAM Access Test ---"));
  uint32_t testData[4] = {0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0xFEDCBA98};
  CracenPke::writeDataBlock(0, testData, 4);
  Serial.println(F("  Wrote 4 words to PKE data RAM"));

  uint32_t readBack[4];
  CracenPke::readDataBlock(0, readBack, 4);
  bool match = (readBack[0] == testData[0] && readBack[1] == testData[1] &&
                readBack[2] == testData[2] && readBack[3] == testData[3]);
  Serial.print(F("  Read-back match: "));
  Serial.println(match ? F("Yes") : F("No"));
  if (!match) {
    for (int i = 0; i < 4; i++) {
      Serial.print(F("    ["));
      Serial.print(i);
      Serial.print(F("]: 0x"));
      Serial.print(readBack[i], HEX);
      Serial.print(F(" (expected 0x"));
      Serial.print(testData[i], HEX);
      Serial.println(F(")"));
    }
  }
  Serial.println();

  // ---- Byte-level access test ----
  Serial.println(F("--- Byte Access Test ---"));
  uint8_t byteData[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  CracenPke::writeDataBytes(64, byteData, sizeof(byteData));
  uint8_t byteRead[sizeof(byteData)];
  CracenPke::readDataBytes(64, byteRead, sizeof(byteRead));
  bool byteMatch = (memcmp(byteRead, byteData, sizeof(byteData)) == 0);
  Serial.print(F("  Byte read-back match: "));
  Serial.println(byteMatch ? F("Yes") : F("No"));
  Serial.println();

  // ---- Interrupt test ----
  Serial.println(F("--- Interrupt Test ---"));
  CracenPke::enablePkeIkgInterrupt(true);
  Serial.println(F("  PKE+IKG interrupt enabled"));
  bool irqEvent = CracenPke::pkeIkgEvent();
  Serial.print(F("  PKE+IKG event pending: "));
  Serial.println(irqEvent ? F("Yes") : F("No"));
  CracenPke::enablePkeIkgInterrupt(false);
  Serial.println(F("  PKE+IKG interrupt disabled"));
  Serial.println();

  // ---- Summary ----
  Serial.println(F("======================================"));
  Serial.println(F("  CRACEN PKE probe complete."));
  Serial.println(F("  For ECDSA, load Nordic PKE microcode"));
  Serial.println(F("  into code RAM and invoke via issueCommand()"));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
