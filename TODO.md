# TODO: Nrf54L15-Clean-Implementation

Last updated: 2026-02-24

## Priority 0: Release quality / packaging

- [ ] Finalize `0.1.0` release commit with current BLE matrix artifacts and docs.
- [ ] Tag and publish release assets (`dist/*.tar.bz2`, package index sync).
- [ ] Keep CI matrix aligned with current BLE + low-power example set.

## Priority 1: BLE security parity (current top blocker)

- [ ] Make legacy pairing outcome deterministic so `bluetoothctl pair` reaches `Paired: yes` / `Bonded: yes` reliably across repeated runs.
- [ ] Resolve intermittent post-`LE Start Encryption` failure modes (MIC/auth failures and host timeout paths).
- [ ] Add targeted traces/tests for `LL_ENC_REQ/LL_ENC_RSP/LL_START_ENC_REQ/LL_START_ENC_RSP` sequencing.
- [ ] Validate encrypted data path counters/nonce behavior against known-good vectors.
- [ ] Verify bonded reconnect path with persisted keys after successful pairing.
- [ ] Verify BLE security results on a non-Intel host adapter to isolate target bugs from Intel `Hardware Error 0x0c` crashes.

## Priority 2: BLE procedure hardening

- [ ] Expand LL control edge-case handling to reduce controller-specific interop issues.
- [ ] Add negative tests for malformed LL/SMP/L2CAP/ATT packets.
- [ ] Add packet-level regression tests for connection-update/channel-map instant edge cases.
- [ ] Add over-the-air sniff correlation for encryption-procedure debugging to isolate host-adapter artifacts.

## Priority 3: Feature breadth

- [ ] Central role baseline (scan/initiate/minimal GATT client).
- [ ] Extended/periodic advertising support where hardware path is practical.
- [ ] Optional dynamic GATT registration API.

## Priority 4: Advanced RF features

- [ ] Channel sounding / AoA/AoD feasibility design note.
- [ ] Prototype antenna pattern + sampling control API if practical without proprietary runtime.
- [ ] Add explicit capability flags in API/docs when advanced RF features remain unavailable.

## Priority 5: Developer experience

- [ ] Add Zephyr-core-to-clean-core migration guide.
- [ ] Add upload troubleshooting guide (`pyocd` vs `openocd`).
- [ ] Add host-side unit-style tests for BLE packet builders/parsers.
- [x] Add scripted pairing/bonding regression runner with pass/fail summary.

## Done recently

- [x] BLE matrix automation script improved: scan retries, pair/bond modes, robust CLI reporting.
- [x] Active BLE scan path added (`SCAN_REQ`/`SCAN_RSP`) with `BleActiveScanner` example.
- [x] Antenna Tools-menu route now respected during BLE init.
- [x] Pair/bond example default TX level improved for better discoverability.
- [x] Pair/bond examples now fail fast with explicit step diagnostics when BLE init/config fails.
- [x] LL security path updated to emit immediate `LL_ENC_RSP` handling attempt for improved interop testing.
- [x] LL instant application aligned to current-event counter basis (connection update/channel map apply path).
- [x] LL encryption transition hardened:
  - derive session key during TX window to enable same-event encrypted follow-up decode;
  - allow bounded plain zero-length data PDU during final `LL_START_ENC_RSP` transition.
- [x] Added `scripts/ble_pair_bond_regression.sh` with CSV/summary output and host-crash tagging.
- [x] README upgraded with pinout image, mapping tables, default routes, and example index.
