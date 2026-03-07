# Bug Tracker and Zephyr-Parity Execution Plan

Last updated: 2026-02-25
Target: close the remaining parity gap vs the Zephyr-based core while keeping this clean, standalone Arduino implementation (no Zephyr/nRF Connect runtime dependency).

## Definition of done for parity

- Non-BLE Arduino API and board features behave equivalently for common workflows.
- BLE peripheral pairing is reliable enough to reach `Paired: yes` and `Bonded: yes` repeatedly with commodity hosts.
- Bonded reconnect path works (existing keys used, no forced re-pair loop).
- Regressions are caught by repeatable CLI runs (serial + btmon + summarized pass/fail).

## Priority 0 blockers

- [ ] BUG-BLE-ENC-01: Legacy pairing still flaky around LL encryption transition.
  - Observed outcomes across runs:
    - `Encryption Change: Success` followed by host controller reset (`Intel 0x0c`).
    - `Connection Terminated due to MIC Failure (0x3d)` shortly after `LE Start Encryption`.
    - `BleBondPersistenceProbe` timing out immediately after SMP random / `LE Start Encryption`.
    - intermittent successful host-side pair/bond despite unstable link continuity.
  - Latest evidence:
    - `measurements/ble_pair_bond_regression_20260225_000106` (`BlePairingEncryptionStatus`, `hci0`, target sends immediate `LL_FEATURE_RSP`, host still disconnects with `0x3e`).
    - `measurements/ble_pair_bond_regression_20260224_233855` (`BlePairingEncryptionStatus`, `hci0`, btmon confirms `LE Read Remote Used Features` fails with `Connection Failed to be Established (0x3e)`).
    - `measurements/ble_pair_bond_regression_20260224_212703` (`BlePairingEncryptionStatus`, Broadcom `hci1`, `4/5` pass, `1/5` host-inconclusive).
    - `measurements/ble_pair_bond_regression_20260224_212347` (`BleBondPersistenceProbe`, clean bond storage, repeated timeout during encryption start).
    - `measurements/ble_pair_bond_regression_20260224_224348` (`BlePairingEncryptionStatus`, Broadcom `hci1`, `2/2` host-inconclusive in this session).
    - `measurements/ble_pair_bond_regression_20260224_224649` (`BlePairingEncryptionStatus`, alternate adapter `hci0`, `1/1 fail_target`).
    - `measurements/ble_pair_bond_regression_20260224_211654` (pair reaches SMP random exchange then disconnects at start-encryption transition).
    - `measurements/ble_pair_bond_regression_20260224_064411` (2/2 paired, 1/2 bonded, one run still host-crash contaminated).
    - `measurements/ble_pair_bond_regression_20260224_064748` (single-attempt bonded pass with Intel host crash signature).
    - `measurements/ab_clean_instantfix_baseline_bondprobe_20260223_234808` (`Paired: yes`, `Bonded: yes`, then Intel host crash).
    - `measurements/ab_clean_instantfix_dir10_pairstatus_20260223_234512` (immediate MIC failure path).
    - `measurements/ab_clean_instantfix_dir10_bondprobe_20260223_234312` (MIC failure in bond probe).
  - Acceptance criteria:
    - Minimum 10 consecutive pair attempts with `Paired: yes` and no MIC failure from a stable host adapter.
    - At least 3 bonded reconnect attempts without re-pair.

- [ ] BUG-BLE-HOST-INTEL-01: Intel host controller crash (`Hardware Error 0x0c`) contaminates BLE-security signal.
  - Impact: makes it hard to distinguish target-side faults from host adapter instability.
  - Acceptance criteria:
    - Reproduce the same pair/bond scenarios on a non-Intel adapter or phone and compare outcomes.

## Priority 1 parity gaps

- [x] GAP-BLE-SEC-02: Durable default bond persistence backend implemented.
  - Current path now uses flash-backed RRAM + `.noinit` retention blob + callback hooks.
  - Remaining work: power-cycle and bonded-reconnect validation matrix to close parity.

- [x] GAP-BLE-GATT-RT-01: Optional runtime custom GATT registration baseline implemented.
  - Added dynamic 16-bit service/characteristic registration, runtime value updates, CCCD handling, and notify/indicate scheduling.
  - Demo sketch: `examples/BleCustomGattRuntime/BleCustomGattRuntime.ino`.
  - Remaining work: descriptor registration and mutable service-table operations (remove/reorder).

- [ ] GAP-BLE-CTRL-01: LL control hardening under edge-case sequencing.
  - Focus: start-encryption sequencing, retransmit/ACK windows, control/data transition edge cases.

- [ ] GAP-BLE-CENTRAL-01: Minimal central role (initiate + basic client interactions).
- [ ] GAP-BLE-EXTADV-01: Extended/periodic advertising feasibility and implementation plan.
- [ ] GAP-BLE-CS-01: Full Bluetooth Channel Sounding LL capability (document exact hardware/runtime constraints and host control model).
  - Partial progress: RSSI-based two-board channel sounding baseline now available via
    `examples/BleChannelSoundingReflector/BleChannelSoundingReflector.ino` and
    `examples/BleChannelSoundingInitiator/BleChannelSoundingInitiator.ino`.
- [ ] RF-ZB-01: Full Zigbee stack layers (commissioning + NWK/APS/ZCL/security profiles) over 802.15.4.
  - Partial progress: `ZigbeeRadio` IEEE 802.15.4 PHY/MAC-lite path with two-board
    `ZigbeePingInitiator` / `ZigbeePongResponder` examples is implemented.

## Priority 2 tooling and regression

- [x] Add deterministic BLE-security regression runner:
  - fixed host prep
  - repeat attempts
  - summarized verdict (`pair_ok`, `bond_ok`, `enc_change_ok`, `mic_fail`, `host_crash`)
- [x] Add a concise runbook for unattended pair/bond tests (no interactive trust/agent setup surprises).
- [x] Tag runs as `host-unstable` when Intel crash signature is present.
- [x] Split attempt verdicts into target and host dimensions:
  - `target_verdict` (`pass`, `fail`, `unknown_host`)
  - `overall_verdict` (`pass`, `fail_target`, `inconclusive_host`)
- [x] Add bonded reconnect regression mode:
  - `--mode bonded-reconnect` (pair + disconnect + reconnect without re-pair)
  - reconnect metrics in CSV (`reconnect_connected`, `reconnect_bonded`, `reconnect_enc_seen`)

## What was implemented in this cycle

- [x] LL encryption transition hardening:
  - derive session key during TX window after `TASKS_TXEN` (instead of waiting until post follow-up path);
  - tolerate bounded plain zero-length data PDU while final `LL_START_ENC_RSP` is still in transition.
  - File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp`

- [x] Added deterministic pair/bond regression script and fixed parser edge cases:
  - `scripts/ble_pair_bond_regression.sh`
  - handles multiline encryption-change detection;
  - removes false-positive MIC-failure detection from unrelated `0x3d` bitmasks.
  - adds `--mode pair-bond|bonded-reconnect`, `--controller`, `--btmon-iface`;
  - emits `host_unstable`, `target_trace_error`, `target_verdict`, `overall_verdict`.

- [x] LL instant application now uses the current-event counter basis (aligned with channel selection), reducing off-by-one risk during pending connection/channel-map instant application.
  - File: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp`

- [x] LL control response scheduling widened:
  - when TX freshness allows, new LL control requests are answered in the same event (not only encryption-critical opcodes);
  - validated on target logs: repeated `LL_FEATURE_REQ (0x08)` now gets immediate `LL_FEATURE_RSP (0x09)` with feature mask bytes visible in serial output.
  - Files:
    - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp`
    - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BlePairingEncryptionStatus/BlePairingEncryptionStatus.ino`

- [x] Runtime custom GATT feature-breadth step:
  - new `BleRadio` API for dynamic 16-bit services/chars:
    - `clearCustomGatt`, `addCustomGattService`, `addCustomGattCharacteristic`
    - runtime value IO, CCCD state, write callbacks
    - `notifyCustomGattCharacteristic` with notify/indicate scheduling in connection events
  - new example: `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BleCustomGattRuntime/BleCustomGattRuntime.ino`
  - validation captures:
    - `measurements/custom_gatt_runtime_20260224_224921`
    - `measurements/custom_gatt_runtime_20260224_225041_connect`

- [x] Executed new hardware regression passes with this change:
  - `measurements/ble_pair_bond_regression_20260224_224048` (`BleBondPersistenceProbe`, `2/2` host-inconclusive)
  - `measurements/ble_pair_bond_regression_20260224_224348` (`BlePairingEncryptionStatus`, `2/2` host-inconclusive)
  - `measurements/ble_pair_bond_regression_20260224_224649` (`BlePairingEncryptionStatus`, `1/1 fail_target` on alternate adapter)
  - `measurements/ble_pair_bond_regression_20260224_071104` (pair-bond smoke, new verdict fields)
  - `measurements/ble_pair_bond_regression_20260224_071149` (bonded-reconnect smoke, reconnect metrics)
  - `measurements/ble_pair_bond_regression_20260224_064007`
  - `measurements/ble_pair_bond_regression_20260224_064411`
  - `measurements/ble_pair_bond_regression_20260224_064748`
  - `measurements/ab_clean_instantfix_bondprobe_20260223_233817`
  - `measurements/ab_clean_instantfix_pairstatus_20260223_234024`
  - `measurements/ab_clean_instantfix_dir10_bondprobe_20260223_234312` (A/B direction experiment)
  - `measurements/ab_clean_instantfix_dir10_pairstatus_20260223_234512` (A/B direction experiment)
  - `measurements/ab_clean_instantfix_baseline_bondprobe_20260223_234808`

- [x] Regression harness flow hardening:
  - single-session `bluetoothctl` timelines per attempt (setup/scan/trust/pair/info/disconnect) to avoid per-command teardown side effects;
  - delayed `default-agent` setup and stronger host-instability signature tagging;
  - automatic bond-sector erase (`pyocd`) for fresh bond-probe attempts.
  - File: `scripts/ble_pair_bond_regression.sh`

## Decisions taken this cycle

- Keep the LL instant-counter alignment patch.
- Do not keep the temporary default direction remap (`rx=1`, `tx=0`) as a static default:
  - it increased immediate MIC-failure behavior in current tests;
  - runtime direction fallback logic remains the safer path for now.

## Ordered next actions

1. Run 10-attempt bond probe matrix on current baseline + transition-hardening patch and record aggregate outcomes.
2. Validate same matrix on non-Intel host (or phone) to separate core defects from host crashes.
3. Add explicit trace points for:
   - pending instant apply (`connUpdateInstant`, `channelMapInstant`)
   - first three encrypted RX/TX counters and headers after `LL_START_ENC_RSP`.
4. If MIC failures remain without host crash signature, tighten start-encryption acceptance rules:
   - bounded plaintext-empty tolerance window while awaiting final encrypted transition.
5. Validate durable bond persistence across reset/power-cycle (Broadcom + Intel host matrix).
6. Investigate bond-probe-only timeout at start encryption:
   - compare LL/SMP trace delta between `BlePairingEncryptionStatus` (mostly pass) and `BleBondPersistenceProbe` (timeout);
   - confirm whether timeout occurs before `LL_ENC_REQ` response or during `LL_START_ENC_*` transition.

## Operating rules

- Every BLE security/codepath change must have:
  - one new measurement folder
  - one row update in `docs/BLE_FIX_ATTEMPT_LOG.md`
  - parity doc sync (`FEATURE_PARITY.md`, `TODO.md`) if status changed.
- Do not repeat an old fix attempt without a new hypothesis and explicit A/B objective.
