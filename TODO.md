# TODO: Nrf54L15-Clean-Implementation

Last updated: 2026-02-25

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
- [ ] Full Zigbee stack layers (commissioning + NWK/APS/ZCL/security profiles) on top of IEEE 802.15.4 PHY/MAC-lite baseline.
- [x] Optional dynamic GATT registration API (16-bit services/chars + CCCD + runtime notify/indicate path).
- [ ] Expand dynamic GATT support with descriptor registration and multi-service editing (remove/reorder) parity.

## Priority 4: Advanced RF features

- [x] Channel sounding / AoA/AoD feasibility design note (RSSI-based legacy adv-channel sounding path added with initiator/reflector examples).
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
- [x] LL control request handling widened to same-event response when TX freshness permits (including `LL_FEATURE_REQ -> LL_FEATURE_RSP`), and pairing example now logs LL rx/tx opcodes for triage.
- [x] LL instant application aligned to current-event counter basis (connection update/channel map apply path).
- [x] LL encryption transition hardened:
  - derive session key during TX window to enable same-event encrypted follow-up decode;
  - allow bounded plain zero-length data PDU during final `LL_START_ENC_RSP` transition.
- [x] Added `scripts/ble_pair_bond_regression.sh` with CSV/summary output and host-crash tagging.
- [x] BLE security regression runner upgraded:
  - mode switch: `pair-bond` and `bonded-reconnect`;
  - host-controller controls: `--controller`, `--btmon-iface`;
  - verdict split: `target_verdict`, `overall_verdict`, `host_unstable`;
  - reconnect metrics: `reconnect_connected`, `reconnect_bonded`, `reconnect_enc_seen`.
- [x] BLE security harness execution model hardened:
  - single-session `bluetoothctl` timeline per attempt (no per-command teardown);
  - delayed `default-agent` priming and stronger host-instability tagging;
  - optional automatic bond-sector erase via `pyocd` for clean bond-probe attempts.
- [x] Added default flash-backed RRAM bond persistence backend with retention fallback and reserved linker storage region.
- [x] Bond probe now exposes serial command hooks (`clear-bond`, `show-bond`) and uses constant-latency mode for tighter pairing timing.
- [x] Added runtime custom 16-bit GATT registration API (service/characteristic/CCCD/value update/write callback) with `BleCustomGattRuntime` example.
- [x] Added two-board BLE channel sounding examples:
  - `BleChannelSoundingReflector` (scannable reflector + `SCAN_REQ/SCAN_RSP` counters and channel RSSI).
  - `BleChannelSoundingInitiator` (active scanner + per-channel RSSI aggregation and best-channel hint).
- [x] README upgraded with pinout image, mapping tables, default routes, and example index.
