# Thread Runtime Ownership

Status baseline:

- Audit date: `2026-04-26`
- Claim level: experimental staged OpenThread runtime
- First runtime owner: `CPUAPP`
- First radio backend: `ZigbeeRadio`

This document records the Thread ownership decisions encoded in
`OpenThreadRuntimeOwnership` in
`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.h`.

## Current Ownership Split

- `CPUAPP` hosts the staged OpenThread core and PAL glue.
- `ZigbeeRadio` owns the first IEEE 802.15.4 backend.
- `VPR` is not part of the first Thread radio path.
- alarms/timebase stay on the existing HAL-backed `otPlatAlarmMilli*` /
  `otPlatAlarmMicro*` path.
- settings stay on `Preferences`.
- entropy stays on `CracenRng`.
- symmetric crypto and key-ref PAL support stay on the current repo-backed
  helper paths.

## Current Delivered Runtime

- staged upstream OpenThread source is in-tree.
- platform glue initializes the staged core.
- fixed-dataset role bring-up exists for experimental examples.
- leader, child, and router paths have been brought up in staged examples.
- UDP send/receive examples exist for staged two-board validation.
- PSKc/passphrase dataset helpers exist.

## First-Pass Scope

- board set: `XIAO nRF54L15 / Sense`
- allowed staged roles:
  - `detached`
  - `child`
  - `router`
  - `leader` for isolated bring-up
- network style: fixed test dataset first
- border router: external, not implemented in this repo

## In-Tree Paths

- platform glue:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.cpp`
- public/staged OpenThread headers:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread`
- staged full-core path:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core`
- core user config:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread-core-user-config.h`
- import script:
  `scripts/import_openthread_core_scaffold.sh`

## Current Non-Claims

- no production Thread certification claim.
- no joiner claim.
- no commissioner claim.
- no in-repo border-router claim.
- no reference-network attach claim until that validation is completed.
- no reboot-recovery claim until dataset/settings recovery is validated.
- no VPR-offloaded Thread controller claim.

## Next Ticks

- [ ] validate attach against a reference Thread network.
- [ ] validate saved dataset/settings after reboot.
- [ ] add or explicitly defer joiner support.
- [ ] validate sleepy-device behavior.
- [ ] keep Zigbee regressions green because Zigbee and Thread share the same
  IEEE 802.15.4 backend.
