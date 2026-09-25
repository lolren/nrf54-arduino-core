#!/usr/bin/env python3
"""Execute production IO/keypair methods with host stubs for issue #114."""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile

from test_ble_security_policy_contracts import HAL, PARTS, function_body, source


def main() -> None:
    security = source(PARTS / "nrf54l15_hal_ble_ll_security.inc")
    setter = function_body(
        security, ("void BleRadio::setSecurityIoCapabilities(",), "IO setter"
    )
    ensure = function_body(
        security,
        ("bool BleRadio::ensureSecureConnectionsLocalKeypair()",),
        "SC local key generation",
    )
    harness = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
constexpr uint8_t kSmpIoCapKeyboardDisplay = 4;
constexpr uint8_t kSmpIoCapNoInputNoOutput = 3;
static unsigned criticalDepth = 0;
static unsigned keyGenerations = 0;
static bool failKeyGeneration = false;
uint32_t bleEnterCritical() { return criticalDepth++; }
void bleExitCritical(uint32_t previous) { criticalDepth = previous; }
uint32_t micros() { static uint32_t now = 0; return ++now; }
struct Secp256r1Scalar { uint8_t bytes[32]; };
struct Secp256r1Point { uint8_t bytes[64]; };
struct Secp256r1 {
  static bool generateKeyPair(Secp256r1Scalar* priv, Secp256r1Point* pub) {
    assert(criticalDepth == 0);
    ++keyGenerations;
    memset(priv->bytes, 0xA5, sizeof(priv->bytes));
    memset(pub->bytes, 0x5A, sizeof(pub->bytes));
    return !failKeyGeneration;
  }
  static void encodeUncompressed(const Secp256r1Point& pub, uint8_t* out) {
    out[0] = 4;
    memcpy(out + 1, pub.bytes, sizeof(pub.bytes));
  }
};
class BleRadio {
 public:
  bool connected_ = false;
  bool smpSecureConnectionsLocalKeyReady_ = false;
  bool smpSecureConnectionsLocalKeyInProgress_ = false;
  uint8_t smpLocalIoCapabilities_ = kSmpIoCapNoInputNoOutput;
  uint8_t smpSecureConnectionsPrivateKey_[32]{};
  uint8_t smpSecureConnectionsPublicKey_[65]{};
  uint32_t smpSecureConnectionsLocalKeypairTimeUs_ = 0;
  void setSecurityIoCapabilities(uint8_t);
  bool ensureSecureConnectionsLocalKeypair();
};
""" + setter + "\n" + ensure + r"""
int main() {
  BleRadio radio;
  for (unsigned connected = 0; connected < 2; ++connected) {
    radio.connected_ = connected;
    for (unsigned io = 0; io < 256; ++io) {
      radio.setSecurityIoCapabilities(static_cast<uint8_t>(io));
      assert(radio.smpLocalIoCapabilities_ == (io <= 4 ? io : 3));
      assert(criticalDepth == 0);
      assert(keyGenerations == 0);
      assert(!radio.smpSecureConnectionsLocalKeyReady_);
      assert(!radio.smpSecureConnectionsLocalKeyInProgress_);
    }
  }
  // Pairing/OOB can still request the key once it is actually needed.
  assert(radio.ensureSecureConnectionsLocalKeypair());
  assert(keyGenerations == 1);
  assert(radio.smpSecureConnectionsLocalKeyReady_);
  assert(radio.smpSecureConnectionsPublicKey_[0] == 4);
  for (unsigned i = 0; i < 32; ++i)
    assert(radio.smpSecureConnectionsPrivateKey_[i] == 0xA5);
  for (unsigned i = 1; i < 65; ++i)
    assert(radio.smpSecureConnectionsPublicKey_[i] == 0x5A);
  radio.setSecurityIoCapabilities(1);
  assert(radio.ensureSecureConnectionsLocalKeypair());
  assert(keyGenerations == 1);
  // Failed entropy/ECC must not install keys; a later explicit request retries.
  radio.smpSecureConnectionsLocalKeyReady_ = false;
  failKeyGeneration = true;
  assert(!radio.ensureSecureConnectionsLocalKeypair());
  assert(!radio.smpSecureConnectionsLocalKeyInProgress_);
  assert(!radio.smpSecureConnectionsLocalKeyReady_);
  for (auto byte : radio.smpSecureConnectionsPrivateKey_) assert(byte == 0);
  for (auto byte : radio.smpSecureConnectionsPublicKey_) assert(byte == 0);
  failKeyGeneration = false;
  radio.smpSecureConnectionsLocalKeyInProgress_ = true;
  radio.setSecurityIoCapabilities(2);
  assert(radio.smpSecureConnectionsLocalKeyInProgress_);
  assert(!radio.ensureSecureConnectionsLocalKeypair());
  assert(keyGenerations == 2);
  radio.smpSecureConnectionsLocalKeyInProgress_ = false;
  assert(radio.ensureSecureConnectionsLocalKeypair());
  assert(keyGenerations == 3);
}
"""
    with tempfile.TemporaryDirectory(prefix="nrf54-ble-lazy-security-") as tmp:
        cpp = Path(tmp) / "lazy_security.cpp"
        binary = Path(tmp) / "lazy_security"
        cpp.write_text(harness)
        subprocess.run(
            shlex.split(os.environ.get("CXX", "c++"))
            + ["-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    print("PASS executable IO clamping, lazy key creation, retry and re-entry guards")

    work = function_body(
        security, ("void BleRadio::serviceSecureConnectionsWork()",), "SC work"
    )
    for token in (
        "if (!connected_)",
        "if (!smpSecureConnectionsActive_)",
        "!smpSecureConnectionsLocalKeyInProgress_",
        "kSmpPairingStateReqSent",
        "kSmpPairingStateRspSent",
        "smpSecureConnectionsDeferredPublicKey_",
        "ensureSecureConnectionsLocalKeypair()",
    ):
        assert token in work, f"missing deferred pairing key-generation gate: {token}"
    for signature in (
        "bool BleRadio::generateSecurityOobData(",
        "bool BleRadio::setSecurityOobLocalData(",
    ):
        assert "ensureSecureConnectionsLocalKeypair()" in function_body(
            security, (signature,), "OOB key preparation"
        )
    application = function_body(
        source(PARTS / "nrf54l15_hal_ble_connection_api.inc"),
        ("void BleRadio::serviceDeferredApplicationWork()",), "foreground work"
    )
    assert application.index("if (bleRunningInIsr())") < application.index(
        "serviceSecureConnectionsWork()"
    )
    multiply = function_body(
        source(HAL / "matter_secp256r1.cpp"),
        ("bool Secp256r1::scalarMultiplyBase(",), "base-point multiplication"
    )
    assert "maybeCooperateWithBle(coopCounter++)" in multiply
    hook = function_body(
        source(HAL / "nrf54l15_hal.cpp"),
        ('extern "C" void nrf54l15_secp256r1_cooperate_hook(void)',), "ECC cooperation"
    )
    assert "g_activeBleRadio->serviceBackgroundConnection(" in hook
    print("PASS SC negotiation/OOB retain key generation and foreground BLE cooperation")


if __name__ == "__main__":
    main()
