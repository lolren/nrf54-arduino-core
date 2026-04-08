# HAL Refactor 0.4.0

This worktree is the local refactor track for the nRF54L15 HAL. The goal is
to reduce correctness risk first, then split the oversized HAL into smaller
units without losing the currently working BLE and Zigbee behavior from main.

## Current baseline

- Source branch: `main` at `b73658a`
- Local refactor branch: `v0.4.0-local`
- Working tree: `NRF54L15-Clean-Arduino-core.v040`

## First-wave hardening

These changes are intended to be behavior-preserving:

1. Remove undefined behavior from highest-set-bit allocation.
2. Add generic IRQ critical helpers for non-BLE ownership transitions.
3. Make comparator ownership changes IRQ-safe.
4. Make BLE GRTC one-time init safer against concurrent first use.

Completed:

- generic support helpers have been split into
  `src/nrf54l15_hal_support.cpp` with declarations in
  `src/nrf54l15_hal_support_internal.h`
- board-specific RF path, antenna, and VBAT policy now live in
  `src/nrf54l15_hal_board_policy.cpp` with internal accessors in
  `src/nrf54l15_hal_board_policy_internal.h`
- clock, GRTC, and system-off helpers now live in
  `src/nrf54l15_hal_timebase.cpp` with declarations in
  `src/nrf54l15_hal_timebase_internal.h`
- the old duplicated support block has been removed from `nrf54l15_hal.cpp`
- `spimPrescaler()` now rejects invalid zero-input requests instead of silently
  defaulting them to `1 MHz`
- native NUS host regression still passes after the split and helper cleanup:
  `.build/v040_hal_refactor_ble_nus_runtime/summary.json`
- native NUS host regression also passes after the board-policy split:
  `.build/v040_final_host_nus_runtime/summary.json`
- board-to-board BLE still works on the two connected nRF54 boards with the
  stock Bluefruit `central_notify` / `notify_peripheral` pair; the central
  continued printing `[Notify] count=...` after connect and discovery
- Zigbee sleepy button still joins and interviews against Zigbee2MQTT on the
  Home Assistant box at `192.168.1.100`:
  `.build/v040_zigbee_sleepy_button_ha_validation/summary.txt`
- Zigbee sleepy button still joins and interviews against Zigbee2MQTT after
  the board-policy split:
  `.build/v040_final_zigbee_ha_clean/summary.txt`
- native NUS host regression also passes after the timebase split:
  `.build/v040_timebase_host_nus_runtime/summary.json`
- board-to-board BLE still works after the timebase split; the local
  `central_notify` log continued printing notification counters:
  `.build/v040_timebase_ble_board2board/central_notify.log`
- local coordinator + sleepy button runtime passes cleanly after the timebase
  split on rerun:
  `.build/v040_timebase_zigbee_local_rerun/summary.txt`
- Zigbee sleepy button still joins and interviews against Zigbee2MQTT after
  the timebase split:
  `.build/v040_timebase_zigbee_ha/summary.txt`
- local coordinator + sleepy button runtime still shows join/action traffic on
  the two connected nRF54 boards:
  `.build/v040_zigbee_sleepy_button_local_validation/button.log`
  `.build/v040_zigbee_sleepy_button_local_validation/coord.log`
- local coordinator + sleepy button runtime still shows join/action traffic
  after the board-policy split:
  `.build/v040_final_zigbee_local_clean/button.log`
  `.build/v040_final_zigbee_local_clean/coord.log`

Validation notes:

- the sleepy-button validators still have a few false negatives
  (`device_announce_ok`, `action_report_seen`, `alive_joined`, `sleep_cycle_logged`)
  because they key off narrow log text or expect the full report set before the
  script exits
- the local sleepy-button validator was flaky immediately after flashing in the
  timebase pass; a clean rerun on the same firmware passed fully, so the current
  suspicion is harness timing rather than a HAL regression
- the runtime logs themselves still show the important behavior:
  - local button: `button_action cmd=toggle`, `boot_report onoff=OK`,
    `state joined=yes mode=joined`
  - HA/Zigbee2MQTT: `join_ok=true`, `transport_key_ok=true`,
    `z2m_interview_success=true`, `ha_discovery_seen=true`
- the phone bridge endpoints on `192.168.1.117:8787` and `192.168.1.71:8787`
  were unreachable during this pass, so phone-side BLE validation is still a
  gap in the `0.4.0` branch

## Planned follow-up

1. Tighten invalid-argument handling in remaining peripheral helpers and sweep
   the remaining silent fallback paths.
2. Audit one-time init and ownership transitions outside the BLE-specific
   critical helpers.
3. Start carving the HAL into focused translation units:
   - `hal_analog.cpp`
   - `hal_peripherals.cpp`
   - `hal_ble_radio.cpp`
   - `hal_ble_gatt.cpp`
   - `hal_ble_bond.cpp`
   - `hal_board_policy.cpp`  (started with the internal board-policy unit)
4. Re-run BLE and Zigbee regressions after each structural step.
