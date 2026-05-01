/*
 * KmuKeyManagement — KMU (Key Management Unit) Overview
 *
 * The nRF54L15 KMU provides secure key storage, provisioning, and
 * management for cryptographic operations. It manages keys that are
 * pushed to RRAM and used by CRACEN for PKE/AES operations.
 *
 * KMU address: 0x50045000 (SECURE-ONLY, no non-secure alias)
 *
 * From non-secure Arduino code:
 *   - Direct KMU register access is NOT possible (will fault)
 *   - The secure bootloader manages KMU provisioning at boot
 *   - This example documents the KMU architecture and shows
 *     what's accessible from non-secure code
 *
 * KMU workflow:
 *   1. Provision slots with random numbers or public keys
 *   2. Push slots to RRAM (protected memory)
 *   3. CRACEN/IKG uses pushed keys for encryption/signing
 *   4. Revoke slots when no longer needed
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
  Serial.println(F("  KMU — Key Management Unit"));
  Serial.println(F("======================================"));
  Serial.println();

  Serial.println(F("--- KMU Architecture ---"));
  Serial.println(F("  Address: 0x50045000 (Secure-Only)"));
  Serial.println(F("  Key slots: 256 (each 128-bit)"));
  Serial.println(F("  Groups: 4 (A, B, C, D) for key isolation"));
  Serial.println(F("  RRAM:  Protected RAM for key storage"));
  Serial.println();

  Serial.println(F("--- KMU Operations ---"));
  Serial.println(F("  PROVISION: Load random data or public key into a slot"));
  Serial.println(F("  PUSH:     Move slot contents to protected RRAM"));
  Serial.println(F("  REVOKE:   Invalidate a slot (one-way)"));
  Serial.println(F("  METADATA: Read slot metadata (slot size, flags)"));
  Serial.println(F("  READ:     Read pushed key from RRAM (CRACEN only)"));
  Serial.println();

  Serial.println(F("--- Access from Non-Secure Code ---"));
  Serial.println(F("  KMU is SECURE-ONLY (S, no NSA)."));
  Serial.println(F("  All KMU registers are at 0x50045000+"));
  Serial.println(F("  Non-secure access will fault the core."));
  Serial.println(F("  KMU is configured by the secure bootloader."));
  Serial.println();

  Serial.println(F("--- KMU and CRACEN Integration ---"));
  Serial.println(F("  1. KMU provisions and pushes keys to RRAM"));
  Serial.println(F("  2. CRACEN/IKG reads keys from RRAM for crypto"));
  Serial.println(F("  3. Keys never leave the secure domain"));
  Serial.println(F("  4. Pushed keys are used for:"));
  Serial.println(F("     - ECDSA key pairs (private keys in RRAM)"));
  Serial.println(F("     - AES symmetric keys (for encryption)"));
  Serial.println(F("     - IKG random seeds"));
  Serial.println();

  // Check what's accessible via existing CracenIkg class
  Serial.println(F("--- CRACEN IKG (Accessible from Non-Secure) ---"));

  // CracenIkg uses CRACEN_BASE which has non-secure alias
  CracenIkg ikg;

  Serial.println(F("  Probing CRACEN IKG status..."));

  uint32_t status = ikg.status();
  Serial.print(F("  Status: 0x"));
  Serial.println(status, HEX);

  uint32_t hwCfg = ikg.hwConfig();
  Serial.print(F("  HW config: 0x"));
  Serial.println(hwCfg, HEX);

  uint8_t symKeys = ikg.symmetricKeyCapacity();
  Serial.print(F("  Symmetric key capacity: "));
  Serial.println(symKeys);

  uint8_t privKeys = ikg.privateKeyCapacity();
  Serial.print(F("  Private key capacity: "));
  Serial.println(privKeys);

  bool ikgOkay = ikg.okay();
  Serial.print(F("  IKG okay: "));
  Serial.println(ikgOkay ? F("Yes") : F("No"));

  bool seedErr = ikg.seedError();
  Serial.print(F("  Seed error: "));
  Serial.println(seedErr ? F("Yes") : F("No"));

  bool entErr = ikg.entropyError();
  Serial.print(F("  Entropy error: "));
  Serial.println(entErr ? F("Yes") : F("No"));

  bool catErr = ikg.catastrophicError();
  Serial.print(F("  Catastrophic error: "));
  Serial.println(catErr ? F("Yes") : F("No"));

  bool symStored = ikg.symmetricKeysStored();
  Serial.print(F("  Symmetric keys stored: "));
  Serial.println(symStored ? F("Yes") : F("No"));

  bool privStored = ikg.privateKeysStored();
  Serial.print(F("  Private keys stored: "));
  Serial.println(privStored ? F("Yes") : F("No"));

  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  KMU/CRACEN IKG overview complete."));
  Serial.println(F("  KMU is secure-only; use CracenIkg for"));
  Serial.println(F("  IKG status from non-secure code."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
