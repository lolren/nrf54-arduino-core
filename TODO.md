# TODO: Nrf54L15-Clean-Implementation

Last updated: 2026-02-22

## Priority 0 (release hygiene)

- [x] Replace `REPLACE_ME` URLs in `package_nrf54l15clean_index.json` with real repository/release URLs (done for `v0.1.0`).
- [x] Add CI pipeline:
  - [x] Compile matrix for key examples (`BLE on/off`, `64/128 MHz`, `low/balanced power`).
  - [x] Package build + checksum consistency check.
- [x] Add semantic versioning workflow for board manager releases (`vX.Y.Z`) via tag-driven release workflow.

## Priority 1 (BLE parity next)

- [ ] LL control procedure hardening:
  - [x] Add stricter opcode-specific length validation and explicit malformed-PDU paths.
  - [x] Expand unknown/reject handling consistency for remote corner-cases.
- [ ] ATT server parity:
  - [x] Add `Read By Type` behavior edge-cases for mixed-length records and boundary handles.
  - [x] Add optional `Write Long` path (`Prepare Write`/`Execute Write`) for selected attributes.
- [ ] L2CAP signaling:
  - [x] Implement additional LE signaling commands (still peripheral-safe subset first).
  - [x] Improve command reject reason mapping granularity.

## Priority 2 (BLE security and interoperability)

- [ ] Implement SMP pairing state machine subset (Just Works first).
  - [x] Add Pairing Request/Response capability negotiation and validation.
  - [x] Add Pairing Confirm/Random exchange state tracking and deterministic response path.
  - [x] Add legacy `c1` confirm verification and `s1` STK derivation scaffold.
  - [x] Bind SMP success to LL encryption procedure and encrypted data-path for legacy peripheral flow.
- [ ] Implement LL encryption procedure support.
  - [x] Handle `LL_ENC_REQ`/`LL_ENC_RSP` and queued `LL_START_ENC_REQ`/`LL_START_ENC_RSP` in peripheral role.
  - [x] Handle `LL_PAUSE_ENC_REQ`/`LL_PAUSE_ENC_RSP` in peripheral role with deterministic encryption state clear.
  - [x] Add encrypted RX/TX data-path with software CCM MIC verify/generate.
  - [x] Add collision/disallow reject semantics for overlapping/invalid encryption procedures.
  - [ ] Broaden edge-case interoperability validation across phone stacks.
- [ ] Key storage policy:
  - [x] Define bonding storage format.
  - [x] Implement retention/flash-backed key persistence.
- [ ] Privacy support:
  - [ ] RPA generation/rotation.
  - [ ] Resolving list behavior.

## Priority 3 (feature breadth)

- [ ] Central role baseline (scan + initiate + minimal GATT client flows).
- [ ] Extended advertising support (non-legacy PDUs where hardware path allows).
- [ ] 2M/Coded PHY negotiation strategy and fallback policy.
- [ ] Optional dynamic GATT database registration API.

## Priority 4 (power optimization)

- [ ] Add structured low-power telemetry example (active/sleep duty metrics).
- [ ] Add configurable peripheral auto-gating policy for idle windows.
- [ ] Tune BLE connection event timing to reduce active radio-on windows.
- [ ] Document measured current profiles per example and Tools menu profile.

## Priority 5 (developer experience)

- [ ] Add a dedicated migration guide: Zephyr-based core -> Clean core.
- [ ] Add troubleshooting guide for upload modes (`pyocd` vs `openocd`).
- [ ] Add a protocol-level BLE debug trace option (compile-time flag).
- [ ] Add unit-style host tests for packet builders/parsers (ATT/L2CAP/LL control).
