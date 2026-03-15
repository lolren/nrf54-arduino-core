#include <Arduino.h>

#include <string.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

/*
 * AAR resolves Bluetooth Resolvable Private Addresses in hardware.
 *
 * This sketch checks two useful paths:
 * 1. A known spec-style IRK/address pair should resolve.
 * 2. A software-generated RPA should resolve back to the matching IRK index.
 *
 * The address passed to Aar uses Bluetooth byte order:
 *   HASH[0..2] + PRAND[0..2]
 */

static Aar g_aar;
static Ecb g_ecb;

static const uint8_t kSpecIrk[16] = {
    0x9B, 0x7D, 0x39, 0x0A, 0xA6, 0x10, 0x10, 0x34,
    0x05, 0xAD, 0xC8, 0x57, 0xA3, 0x34, 0x02, 0xEC,
};

static const uint8_t kSpecMatchingResolvable[6] = {
    0xAA, 0xFB, 0x0D, 0x94, 0x81, 0x70,
};

static const uint8_t kIrks[3][16] = {
    {0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
     0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F},
    {0x6C, 0xD4, 0x18, 0x5A, 0xB1, 0x22, 0x39, 0x7E,
     0x08, 0x6F, 0xC3, 0x91, 0xAA, 0x54, 0x2D, 0x17},
    {0xE0, 0xD1, 0xC2, 0xB3, 0xA4, 0x95, 0x86, 0x77,
     0x68, 0x59, 0x4A, 0x3B, 0x2C, 0x1D, 0x0E, 0xFF},
};

static bool computeBleAh(Ecb& ecb, const uint8_t irk[16], const uint8_t prand[3],
                         uint8_t hash[3]) {
  uint8_t keyBe[16] = {};
  uint8_t plaintextBe[16] = {};
  uint8_t ciphertextBe[16] = {};
  uint8_t ciphertextLe[16] = {};

  // The ECB wrapper accepts standard byte order, then handles the nRF54L
  // key register packing. We still build the ah() block in the format the
  // Bluetooth spec describes: prand in the low 24 bits of the plaintext.
  for (uint8_t i = 0U; i < 16U; ++i) {
    keyBe[i] = irk[15U - i];
  }
  plaintextBe[13] = prand[2];
  plaintextBe[14] = prand[1];
  plaintextBe[15] = prand[0];

  if (!ecb.encryptBlock(keyBe, plaintextBe, ciphertextBe, 400000UL)) {
    return false;
  }

  for (uint8_t i = 0U; i < 16U; ++i) {
    ciphertextLe[i] = ciphertextBe[15U - i];
  }
  memcpy(hash, &ciphertextLe[0], 3U);
  return true;
}

static bool buildResolvablePrivateAddress(Ecb& ecb, const uint8_t irk[16],
                                         const uint8_t prandSeed[3],
                                         uint8_t address[6]) {
  uint8_t prand[3] = {prandSeed[0], prandSeed[1],
                      static_cast<uint8_t>((prandSeed[2] & 0x3FU) | 0x40U)};
  uint8_t hash[3] = {};
  if (!computeBleAh(ecb, irk, prand, hash)) {
    return false;
  }

  memcpy(&address[0], hash, 3U);
  memcpy(&address[3], prand, 3U);
  return true;
}

static void printRawAddress(const uint8_t address[6]) {
  for (uint8_t i = 0U; i < 6U; ++i) {
    if (address[i] < 0x10U) {
      Serial.print('0');
    }
    Serial.print(address[i], HEX);
  }
}

static void reportSingleResult(const char* label, const uint8_t address[6],
                               bool ok, bool resolved, uint16_t index,
                               bool printIndex) {
  Serial.print(label);
  Serial.print(".address=");
  printRawAddress(address);
  Serial.println();
  Serial.print(label);
  Serial.print(".ok=");
  Serial.println(ok ? "yes" : "no");
  Serial.print(label);
  Serial.print(".resolved=");
  Serial.println(resolved ? "yes" : "no");
  if (printIndex) {
    Serial.print(label);
    Serial.print(".index=");
    Serial.println(static_cast<unsigned>(index));
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("AarResolvePrivateAddress");
  Serial.println("Checks a known RPA and a generated RPA against the hardware AAR block.");

  bool specResolved = false;
  const bool specOk = g_aar.resolveSingle(kSpecMatchingResolvable, kSpecIrk,
                                          &specResolved, 800000UL);

  static const uint8_t kPrandSeed[3] = {0xA5, 0x5A, 0x13};
  uint8_t generated[6] = {};
  bool generatedOk =
      buildResolvablePrivateAddress(g_ecb, kIrks[1], kPrandSeed, generated);

  bool generatedResolved = false;
  uint16_t generatedIndex = 0xFFFFU;
  if (generatedOk) {
    generatedOk = g_aar.resolveFirst(generated, &kIrks[0][0], 3U,
                                     &generatedResolved, &generatedIndex,
                                     800000UL);
  }

  reportSingleResult("spec", kSpecMatchingResolvable, specOk, specResolved,
                     0xFFFFU, false);
  reportSingleResult("generated", generated, generatedOk, generatedResolved,
                     generatedIndex, true);

  // A successful generated case should resolve the middle IRK in kIrks.
  const bool pass =
      specOk && specResolved && generatedOk && generatedResolved &&
      (generatedIndex == 1U);

  Serial.print("amount=");
  Serial.println(static_cast<unsigned long>(g_aar.resolvedAmountBytes()));
  Serial.print("errorStatus=");
  Serial.println(static_cast<unsigned long>(g_aar.errorStatus()));
  Serial.print("result=");
  Serial.println(pass ? "PASS" : "FAIL");

  // The XIAO LED is active low. Leave it on for PASS, off for FAIL.
  digitalWrite(LED_BUILTIN, pass ? LOW : HIGH);
}

void loop() {}
