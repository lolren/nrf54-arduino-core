# Bug Tracker and Zephyr-Parity Execution Plan

Last updated: 2026-02-24
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
    - intermittent successful host-side pair/bond despite unstable link continuity.
  - Latest evidence:
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

- [ ] GAP-BLE-SEC-02: Bond persistence backend is currently retention-memory oriented.
  - Current path uses `.noinit` retention blob + callback hooks.
  - Need a durable default persistence path for real power-cycle parity.

- [ ] GAP-BLE-CTRL-01: LL control hardening under edge-case sequencing.
  - Focus: start-encryption sequencing, retransmit/ACK windows, control/data transition edge cases.

- [ ] GAP-BLE-CENTRAL-01: Minimal central role (initiate + basic client interactions).
- [ ] GAP-BLE-EXTADV-01: Extended/periodic advertising feasibility and implementation plan.
- [ ] GAP-BLE-CS-01: Channel sounding feasibility/capability (document exact hardware/runtime constraints).

## Priority 2 tooling and regression

- [x] Add deterministic BLE-security regression runner:
  - fixed host prep
  - repeat attempts
  - summarized verdict (`pair_ok`, `bond_ok`, `enc_change_ok`, `mic_fail`, `host_crash`)
- [x] Add a concise runbook for unattended pair/bond tests (no interactive trust/agent setup surprises).
- [ ] Tag runs as `host-unstable` when Intel crash signature is present.

## What was implemented in this cycle

- [x] LL encryption transition hardening:
  - derive session key during TX window after `TASKS_TXEN` (instead of waiting until post follow-up path);
  - tolerate bounded plain zero-length data PDU while final `LL_START_ENC_RSP` is still in transition.
  - File: `hardware/nrf54l15clean/0.1.0/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp`

- [x] Added deterministic pair/bond regression script and fixed parser edge cases:
  - `scripts/ble_pair_bond_regression.sh`
  - handles multiline encryption-change detection;
  - removes false-positive MIC-failure detection from unrelated `0x3d` bitmasks.

- [x] LL instant application now uses the current-event counter basis (aligned with channel selection), reducing off-by-one risk during pending connection/channel-map instant application.
  - File: `hardware/nrf54l15clean/0.1.0/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp`

- [x] Executed new hardware regression passes with this change:
  - `measurements/ble_pair_bond_regression_20260224_064007`
  - `measurements/ble_pair_bond_regression_20260224_064411`
  - `measurements/ble_pair_bond_regression_20260224_064748`
  - `measurements/ab_clean_instantfix_bondprobe_20260223_233817`
  - `measurements/ab_clean_instantfix_pairstatus_20260223_234024`
  - `measurements/ab_clean_instantfix_dir10_bondprobe_20260223_234312` (A/B direction experiment)
  - `measurements/ab_clean_instantfix_dir10_pairstatus_20260223_234512` (A/B direction experiment)
  - `measurements/ab_clean_instantfix_baseline_bondprobe_20260223_234808`

## Decisions taken this cycle

- Keep the LL instant-counter alignment patch.
- Do not keep the temporary default direction remap (`rx=1`, `tx=0`) as a static default:
  - it increased immediate MIC-failure behavior in current tests;
  - runtime direction fallback logic remains the safer path for now.

## Ordered next actions

1. Run 10-attempt bond probe matrix on current baseline + transition-hardening patch and record aggregate outcomes.
2. Validate same matrix on non-Intel host (or phone) to separate core defects from host crashes.
3. Add host-crash contamination tags to regression summary (`host-unstable`) and split target vs host verdicts.
4. Add explicit trace points for:
   - pending instant apply (`connUpdateInstant`, `channelMapInstant`)
   - first three encrypted RX/TX counters and headers after `LL_START_ENC_RSP`.
5. If MIC failures remain without host crash signature, tighten start-encryption acceptance rules:
   - bounded plaintext-empty tolerance window while awaiting final encrypted transition.
6. Implement durable default bond persistence backend (flash-backed) with retention fallback.

## Operating rules

- Every BLE security/codepath change must have:
  - one new measurement folder
  - one row update in `docs/BLE_FIX_ATTEMPT_LOG.md`
  - parity doc sync (`FEATURE_PARITY.md`, `TODO.md`) if status changed.
- Do not repeat an old fix attempt without a new hypothesis and explicit A/B objective.
