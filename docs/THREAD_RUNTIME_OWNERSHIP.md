# Thread Runtime Ownership

This document freezes the current Thread runtime decisions that are now also
encoded in `OpenThreadRuntimeOwnership` in
`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.h`.

## Current Ownership Split

- `CPUAPP` hosts the future `OpenThread` core and the current PAL glue.
- `ZigbeeRadio` remains the single `IEEE 802.15.4` backend for the first pass.
- `VPR` is not part of the Thread radio path.
- alarms/timebase stay on the existing HAL-backed `otPlatAlarmMilli*` /
  `otPlatAlarmMicro*` path.
- settings stay on `Preferences`.
- entropy stays on `CracenRng`.
- AES/key-ref PAL support stays on the current CRACEN-backed helpers.
- current repo scope is still header-only `OpenThread` intake plus PAL/radio
  bring-up; real attach/role work needs the full upstream core staged in-tree.

## First-Pass Scope

- board set: `XIAO nRF54L15 / Sense`
- allowed Thread roles for the first working milestone:
  - `detached`
  - `child`
  - `router`
- current delivered runtime is still detached/PAL validation only

## In-Tree Paths

- platform glue:
  - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.cpp`
- PAL/public-header intake:
  - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread`
- staged full-core path:
  - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core`
- core user config header:
  - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread-core-user-config.h`
- core staging script:
  - `scripts/import_openthread_core_scaffold.sh`

## Why Phase 3 Is Still Blocked

`otInstance` creation, dataset handling, MLE attach, role transitions, IPv6,
and UDP need real core integration work on top of the staged upstream sources.
The repo is no longer blocked on missing source intake; it is blocked on the
Arduino build seam and the remaining platform hook conflicts.

## Current Blocker

The source blocker is gone: the upstream core is now staged in-tree. The
remaining blocker is integration work:

- expand the new hidden Arduino build seam from the current upstream
  version/API + compiled-`Instance` smoke slice into the first
  `otInstance`-capable FTD slice
- compile the upstream crypto fallback slice, then flip the PAL stand-down path
  for SHA/HMAC/HKDF/ECDSA/PBKDF2

## Next Mechanical Step

1. Keep the hidden `build.thread_flags` opt-in seam off by default.
2. Expand the seam to `otInstanceInitSingle()` plus fixed test dataset plumbing.
3. Add role-state probing from `otThreadGetDeviceRole()`.
4. Only then enable the upstream crypto fallback slice and claim Phase 3 attach work.
