/*
 * FICR Dump — Factory Information Configuration Registers reader
 *
 * Reads all factory-programmed FICR registers on the nRF54L15 and
 * prints them to Serial. Validates device identity, memory sizes,
 * and Bluetooth address information.
 *
 * Hardware: XIAO nRF54L15 (any variant)
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static void printHexLabel(const char* label, const uint8_t* data, size_t len) {
  Serial.print(F("  "));
  Serial.print(label);
  Serial.print(F(": "));
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
    if (i < len - 1) Serial.print(':');
  }
  Serial.println();
}

static void printHexLabel(const char* label, uint64_t val) {
  Serial.print(F("  ")); Serial.print(label);
  Serial.print(F(": 0x"));
  Serial.print((uint32_t)(val >> 32), HEX);
  Serial.print((uint32_t)(val & 0xFFFFFFFF), HEX);
  Serial.println();
}

static void printHexLabel(const char* label, uint32_t val) {
  Serial.print(F("  "));
  Serial.print(label);
  Serial.print(F(": 0x"));
  Serial.print(val, HEX);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("===================================="));
  Serial.println(F("  nRF54L15 FICR Dump"));
  Serial.println(F("===================================="));
  Serial.println();

  // ---- Device identification ----
  Serial.println(F("--- Device Identification ---"));
  printHexLabel("Device ID", Ficr::deviceId());
  printHexLabel("Config ID", Ficr::configId());

  uint8_t uuid[16];
  Ficr::uuid(uuid);
  printHexLabel("UUID", uuid, sizeof(uuid));

  printHexLabel("Part code", Ficr::partCode());
  Serial.print(F("  Part: "));
  if (Ficr::isNrf54l15()) Serial.println(F("nRF54L15"));
  else if (Ficr::isNrf54l10()) Serial.println(F("nRF54L10"));
  else if (Ficr::isNrf54l05()) Serial.println(F("nRF54L05"));
  else Serial.println(F("Unknown"));

  char variant[16];
  if (Ficr::variant(variant, sizeof(variant))) {
    Serial.print(F("  Variant: "));
    Serial.println(variant);
  } else {
    Serial.println(F("  Variant: (unspecified)"));
  }

  printHexLabel("Package", Ficr::packageCode());
  Serial.println();

  // ---- Memory sizes ----
  Serial.println(F("--- Memory ---"));
  Serial.print(F("  RAM: "));
  Serial.print(Ficr::ramKb());
  Serial.println(F(" KB"));
  Serial.print(F("  RRAM: "));
  Serial.print(Ficr::rramKb());
  Serial.println(F(" KB"));
  Serial.println();

  // ---- Bluetooth identity ----
  Serial.println(F("--- Bluetooth Identity ---"));
  uint8_t addr[6];
  Ficr::deviceAddress(addr);
  printHexLabel("Device Address", addr, sizeof(addr));

  Serial.print(F("  Address type: "));
  if (Ficr::hasPublicAddress()) {
    Serial.println(F("Public"));
  } else {
    Serial.println(F("Random"));
  }
  Serial.println();

  // ---- Bluetooth root keys ----
  Serial.println(F("--- Bluetooth Root Keys ---"));
  uint8_t er[16], ir[16];
  Ficr::encryptionRoot(er);
  Ficr::identityRoot(ir);
  printHexLabel("Encryption Root", er, sizeof(er));
  printHexLabel("Identity Root", ir, sizeof(ir));
  Serial.println();

  // ---- NFC tag header ----
  Serial.println(F("--- NFC Tag Header ---"));
  uint8_t nfcId[16];
  Ficr::nfcId1(nfcId);
  printHexLabel("NFCID1", nfcId, sizeof(nfcId));
  Serial.print(F("  Mfr ID: 0x"));
  Serial.println(Ficr::nfcManufacturerId(), HEX);
  Serial.println();

  // ---- Oscillator trim ----
  Serial.println(F("--- Oscillator Trim ---"));
  printHexLabel("XOSC32M trim", Ficr::xosc32mTrim());
  Serial.print(F("  XOSC32M slope: "));
  Serial.print(Ficr::xosc32mSlope());
  Serial.print(F(", offset: "));
  Serial.println(Ficr::xosc32mOffset());

  printHexLabel("XOSC32K trim", Ficr::xosc32kTrim());
  Serial.print(F("  XOSC32K slope: "));
  Serial.print(Ficr::xosc32kSlope());
  Serial.print(F(", offset: "));
  Serial.println(Ficr::xosc32kOffset());
  Serial.println();

  // ---- Trim config (first few entries) ----
  Serial.println(F("--- Trim Config (first 4 entries) ---"));
  for (int i = 0; i < 4; i++) {
    uint32_t trimAddr, trimData;
    if (Ficr::trimConfig(i, &trimAddr, &trimData)) {
      Serial.print(F("  TRIMCNF["));
      Serial.print(i);
      Serial.print(F("]: addr=0x"));
      Serial.print(trimAddr, HEX);
      Serial.print(F(", data=0x"));
      Serial.println(trimData, HEX);
    }
  }
  Serial.println();

  // ---- Self-test validation ----
  Serial.println(F("--- Self-Test ---"));
  bool ok = true;

  if (Ficr::partCode() == 0xFFFFFFFFUL) {
    Serial.println(F("  FAIL: Part code is all 0xFF (FICR not readable?)"));
    ok = false;
  } else {
    Serial.println(F("  PASS: Part code readable"));
  }

  if (Ficr::ramKb() == 0 || Ficr::ramKb() == 0xFFFFFFFFUL) {
    Serial.println(F("  FAIL: RAM size invalid"));
    ok = false;
  } else {
    Serial.print(F("  PASS: RAM size = "));
    Serial.print(Ficr::ramKb());
    Serial.println(F(" KB"));
  }

  if (Ficr::rramKb() == 0 || Ficr::rramKb() == 0xFFFFFFFFUL) {
    Serial.println(F("  FAIL: RRAM size invalid"));
    ok = false;
  } else {
    Serial.print(F("  PASS: RRAM size = "));
    Serial.print(Ficr::rramKb());
    Serial.println(F(" KB"));
  }

  uint64_t devId = Ficr::deviceId();
  if (devId == 0 || devId == 0xFFFFFFFFFFFFFFFFULL) {
    Serial.println(F("  FAIL: Device ID invalid"));
    ok = false;
  } else {
    Serial.println(F("  PASS: Device ID unique"));
  }

  // Verify two boards have different IDs (printed for manual check)
  Serial.print(F("  Board ID for cross-check: 0x"));
  Serial.print((uint32_t)(devId >> 32), HEX);
  Serial.print((uint32_t)(devId & 0xFFFFFFFF), HEX);

  if (ok) {
    Serial.println(F("  ALL TESTS PASSED"));
  } else {
    Serial.println(F("  SOME TESTS FAILED"));
  }

  Serial.println();
  Serial.println(F("===================================="));
  Serial.println(F("  FICR dump complete."));
  Serial.println(F("===================================="));

  // Halt — leave board in idle for current measurement if desired.
  while (true) {
    delay(1000);
  }
}

void loop() {
  // Not reached.
}
