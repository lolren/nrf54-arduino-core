/*
 * NFCT Tag Setup — NFC-A Target Header Configuration
 *
 * Reads the factory-programmed NFC tag header from FICR and
 * demonstrates configuring the NFCT tag header registers.
 *
 * On the XIAO nRF54L15, there is no NFC antenna, so this
 * example only demonstrates register-level setup. It can
 * be adapted for boards with NFC antenna routing.
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
  Serial.println(F("  NFCT — NFC-A Tag Setup"));
  Serial.println(F("======================================"));
  Serial.println();

  // ---- Read factory NFC ID from FICR ----
  Serial.println(F("--- Factory NFC ID (from FICR) ---"));
  uint8_t nfcId[16];
  Ficr::nfcId1(nfcId);

  Serial.print(F("  MFGID: 0x"));
  Serial.println(Ficr::nfcManufacturerId(), HEX);

  Serial.print(F("  NFCID1: "));
  for (int i = 0; i < 16; i++) {
    if (nfcId[i] < 0x10) Serial.print('0');
    Serial.print(nfcId[i], HEX);
    if (i < 15) Serial.print(':');
  }
  Serial.println();
  Serial.println();

  // ---- Current NFCT tag header ----
  Serial.println(F("--- Current NFCT Tag Header ---"));
  Serial.print(F("  TAGHEADER0: 0x"));
  Serial.println(Nfct::tagHeader0(), HEX);
  Serial.print(F("  TAGHEADER1: 0x"));
  Serial.println(Nfct::tagHeader1(), HEX);
  Serial.print(F("  TAGHEADER2: 0x"));
  Serial.println(Nfct::tagHeader2(), HEX);
  Serial.print(F("  TAGHEADER3: 0x"));
  Serial.println(Nfct::tagHeader3(), HEX);
  Serial.println();

  // ---- Configure custom tag header ----
  Serial.println(F("--- Setting custom NFCID1 ---"));
  Serial.println(F("  Setting: MFGID=0x5F (Nordic), UID=DE:AD:BE:EF:CA:FE:12:34:56:78:9A:BC:DE:FF:00:11"));

  // NFCID1 layout in tag headers:
  // TAGHEADER0: MFGID[7:0] | UD1[7:0] | UD2[7:0] | UD3[7:0]
  // TAGHEADER1: UD4[7:0] | UD5[7:0] | UD6[7:0] | UD7[7:0]
  // TAGHEADER2: UD8[7:0] | UD9[7:0] | UD10[7:0] | UD11[7:0]
  // TAGHEADER3: UD12[7:0] | UD13[7:0] | UD14[7:0] | UD15[7:0]

  // For this demo, set a custom UID via the setNfcId* helpers
  // Using the convenience setters:
  Nfct::setNfcId3rdLast(0xADDE);  // UD1:UD2 (bytes 1:2 of UID)
  Nfct::setNfcId2ndLast(0xFEBE);  // UD3:UD4 -> actually sets high bytes
  Nfct::setNfcIdLast(0xCA, 0xFE, 0x5F);  // UD5, UD6, MFGID

  Serial.println(F("  Tag headers updated."));
  Serial.println();

  // ---- Verify ----
  Serial.println(F("--- Verified Tag Header ---"));
  Serial.print(F("  TAGHEADER0: 0x"));
  Serial.println(Nfct::tagHeader0(), HEX);
  Serial.print(F("  TAGHEADER1: 0x"));
  Serial.println(Nfct::tagHeader1(), HEX);
  Serial.print(F("  TAGHEADER2: 0x"));
  Serial.println(Nfct::tagHeader2(), HEX);
  Serial.print(F("  TAGHEADER3: 0x"));
  Serial.println(Nfct::tagHeader3(), HEX);
  Serial.println();

  // ---- Configure NFCT features ----
  Serial.println(F("--- NFCT Feature Config ---"));
  Nfct::setIoPolarity(true);   // Active-high I/O
  Serial.println(F("  I/O polarity: active-high"));
  Nfct::enableAutoResponse(true);
  Serial.println(F("  Auto-response: enabled"));
  Nfct::setSensRes(0x04);  // Standard NFC-A SENS_RES
  Serial.println(F("  SENS_RES: 0x04 (106 kbps, single target)"));
  Nfct::setSelRes(0x01);  // SEL_RES for NFC-A
  Serial.println(F("  SEL_RES: 0x01 (UID present)"));
  Serial.println();

  // ---- DMA buffer setup (for real use) ----
  Serial.println(F("--- DMA Buffer Setup (info only) ---"));
  // In real usage, you'd set up TX/RX buffers for NFC frame exchange
  Serial.println(F("  TX/RX buffer setup via setTxBuffer/setRxBuffer"));
  Serial.println(F("  Frame events via frameReceived()/frameTransmitted()"));
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  NFCT tag setup demo complete."));
  Serial.println(F("  (No antenna on XIAO — register config only)"));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
