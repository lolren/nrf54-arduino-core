#include <string.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

constexpr uint8_t kSeedSlotBase = 32U;
constexpr uint8_t kSeedSlotCount = 3U;
constexpr uint8_t kSeedWordIndex[kSeedSlotCount] = {0U, 4U, 8U};
constexpr uint32_t kMetadataBase = 0x494B4700UL;

CracenRng g_rng;
Kmu g_kmu;
CracenIkg g_ikg;
uint32_t g_seedWords[12];
bool g_lastOk = false;
uint32_t g_lastStage = 0U;
uint32_t g_lastDetail = 0U;

uint32_t seedDestinationAddress(uint8_t seedWordStartIndex) {
  return static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(&NRF_CRACEN->SEED[seedWordStartIndex]));
}

void printIkgStatus() {
  Serial.print("status=0x");
  Serial.print(g_ikg.status(), HEX);
  Serial.print(" hw=0x");
  Serial.print(g_ikg.hwConfig(), HEX);
  Serial.print(" active=");
  Serial.print(g_ikg.active() ? 1 : 0);
  Serial.print(" okay=");
  Serial.print(g_ikg.okay() ? 1 : 0);
  Serial.print(" seed_err=");
  Serial.print(g_ikg.seedError() ? 1 : 0);
  Serial.print(" entropy_err=");
  Serial.print(g_ikg.entropyError() ? 1 : 0);
  Serial.print(" catastrophic=");
  Serial.print(g_ikg.catastrophicError() ? 1 : 0);
  Serial.print(" sym_stored=");
  Serial.print(g_ikg.symmetricKeysStored() ? 1 : 0);
  Serial.print(" priv_stored=");
  Serial.print(g_ikg.privateKeysStored() ? 1 : 0);
  Serial.print(" sym_cap=");
  Serial.print(g_ikg.symmetricKeyCapacity());
  Serial.print(" priv_cap=");
  Serial.print(g_ikg.privateKeyCapacity());
  Serial.print(" stage=");
  Serial.print(g_lastStage);
  Serial.print(" detail=0x");
  Serial.print(g_lastDetail, HEX);
  Serial.print(" last_ok=");
  Serial.println(g_lastOk ? 1 : 0);
}

bool inspectSeedSlots(bool* outNeedProvision) {
  if (outNeedProvision == nullptr) {
    return false;
  }
  bool anyProvisioned = false;
  bool anyMissing = false;
  for (uint8_t i = 0; i < kSeedSlotCount; ++i) {
    const uint8_t slot = static_cast<uint8_t>(kSeedSlotBase + i);
    const uint32_t expectedMetadata = kMetadataBase | static_cast<uint32_t>(i);
    uint32_t metadata = 0U;
    const bool provisioned = g_kmu.readMetadata(slot, &metadata, 200000UL);
    if (!provisioned) {
      anyMissing = true;
      continue;
    }
    anyProvisioned = true;
    if (metadata != expectedMetadata) {
      g_lastStage = 31U;
      g_lastDetail = 0x400U | slot;
      Serial.print("unexpected metadata slot=");
      Serial.println(slot);
      return false;
    }
  }

  if (anyProvisioned && anyMissing) {
    g_lastStage = 32U;
    g_lastDetail = 0x401U;
    Serial.println("mixed KMU seed-slot state");
    return false;
  }

  *outNeedProvision = !anyProvisioned;
  return true;
}

bool provisionSeedSlots() {
  for (uint8_t i = 0; i < kSeedSlotCount; ++i) {
    const uint8_t slot = static_cast<uint8_t>(kSeedSlotBase + i);

    KmuProvisionSource source{};
    memcpy(source.value, &g_seedWords[i * 4U], sizeof(source.value));
    source.revocationPolicy =
        static_cast<uint32_t>(KmuRevocationPolicy::kRotating);
    source.destination = seedDestinationAddress(kSeedWordIndex[i]);
    source.metadata = kMetadataBase | static_cast<uint32_t>(i);

    if (!g_kmu.provision(slot, source, 600000UL)) {
      g_lastStage = 42U;
      g_lastDetail = 0x200U | slot;
      Serial.print("provision failed slot=");
      Serial.println(slot);
      return false;
    }
  }
  return true;
}

bool pushSeedSlots() {
  for (uint8_t i = 0; i < kSeedSlotCount; ++i) {
    const uint8_t slot = static_cast<uint8_t>(kSeedSlotBase + i);
    if (!g_kmu.push(slot, 600000UL)) {
      g_lastStage = 51U;
      g_lastDetail = 0x300U | slot;
      Serial.print("push failed slot=");
      Serial.println(slot);
      return false;
    }
  }
  return true;
}

bool runProof(bool lockSeed) {
  memset(g_seedWords, 0, sizeof(g_seedWords));
  g_lastOk = false;
  g_lastStage = 0U;
  g_lastDetail = 0U;

  if (g_ikg.active()) {
    g_lastStage = 1U;
    g_ikg.end();
    delay(2);
  }

  bool needProvision = false;
  g_lastStage = 2U;
  if (!inspectSeedSlots(&needProvision)) {
    return false;
  }
  if (needProvision) {
    g_lastStage = 21U;
    if (!g_rng.fill(g_seedWords, sizeof(g_seedWords), 800000UL)) {
      g_lastDetail = 0xB000U;
      Serial.println("rng fill failed");
      return false;
    }
  }
  g_lastStage = 3U;
  if (!g_ikg.begin(800000UL)) {
    g_lastDetail = 0xB001U;
    Serial.println("ikg begin failed");
    return false;
  }
  g_lastStage = 4U;
  if (!g_ikg.softResetKeys(800000UL)) {
    g_lastDetail = 0xB002U;
    Serial.println("ikg soft reset failed");
    return false;
  }
  g_lastStage = 5U;
  if (needProvision) {
    if (!provisionSeedSlots()) {
      return false;
    }
  }
  g_lastStage = 6U;
  if (!pushSeedSlots()) {
    return false;
  }
  g_lastStage = 7U;
  if (!g_ikg.markSeedValid(true)) {
    g_lastDetail = 0xB003U;
    Serial.println("seed valid failed");
    return false;
  }
  g_lastStage = 8U;
  if (lockSeed && !g_ikg.lockSeed()) {
    g_lastDetail = 0xB004U;
    Serial.println("seed lock failed");
    return false;
  }
  g_lastStage = 9U;
  if (!g_ikg.start(1200000UL)) {
    g_lastDetail = 0xB005U;
    Serial.println("ikg start failed");
    return false;
  }

  g_lastStage = 10U;
  g_lastOk = true;
  return true;
}

void printHelp() {
  Serial.println("Commands: s status, r rerun proof, l rerun+lock seed, x soft-reset IKG keys");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("KMU -> CRACEN IKG seed proof");
  Serial.println("Uses KMU slots 0..2 and pushes them into CRACEN.SEED[0], [4], [8].");
  printHelp();

  const bool ok = runProof(false);
  Serial.print("proof ok=");
  Serial.println(ok ? 1 : 0);
  printIkgStatus();
}

void loop() {
  if (!Serial.available()) {
    delay(20);
    return;
  }

  const int incoming = Serial.read();
  if (incoming < 0) {
    return;
  }

  switch (static_cast<char>(incoming)) {
    case 's':
      printIkgStatus();
      break;
    case 'r': {
      const bool ok = runProof(false);
      Serial.print("proof ok=");
      Serial.println(ok ? 1 : 0);
      printIkgStatus();
      break;
    }
    case 'l': {
      const bool ok = runProof(true);
      Serial.print("proof+lock ok=");
      Serial.println(ok ? 1 : 0);
      Serial.println("Seed lock is write-once until reset.");
      printIkgStatus();
      break;
    }
    case 'x':
      Serial.print("soft reset=");
      Serial.println(g_ikg.softResetKeys(800000UL) ? 1 : 0);
      printIkgStatus();
      break;
    default:
      printHelp();
      break;
  }
}
