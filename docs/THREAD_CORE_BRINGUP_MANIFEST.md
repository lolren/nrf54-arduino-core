# Thread Core Bring-Up Manifest

This document pins the first real `OpenThread` core integration target for the
Arduino repo after the PAL-only Phase 2 work.

## Staged Upstream Snapshot

- staged path:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core`
- source repo:
  `https://github.com/openthread/openthread`
- staged commit:
  `254043deece3b8b372659dc2b79b84fa923483b8`
- import metadata:
  `third_party/openthread-core/IMPORT_INFO.txt`

## First Integration Target

The correct first target for this repo is the upstream `openthread-ftd`
library shape from:

- `third_party/openthread-core/src/core/CMakeLists.txt`
- `third_party/openthread-core/src/core/ftd.cmake`

For this repo, that means:

- target mode: `FTD`
- compile defs:
  - `OPENTHREAD_FTD=1`
  - `OPENTHREAD_MTD=0`
  - `OPENTHREAD_RADIO=0`
- source selection: upstream `COMMON_SOURCES`
- explicitly out of scope for the first link slice:
  - `CLI`
  - `NCP`
  - `POSIX`
  - `RCP`

## What The Repo Already Has

These pieces are already present and proven through PAL probes:

- radio path on top of `ZigbeeRadio`
- alarm milli/micro platform hooks
- `otPlatCAlloc()` / `otPlatFree()`
- repo-owned core user config header at
  `src/openthread-core-user-config.h`
- settings persistence on `Preferences`
- entropy via `CracenRng`
- AES ECB + key-ref basics
- source match / ACK / ED / RX metadata / diag bring-up

## Current Integration State

The staged seam has now crossed from link-only work into real hardware init:

- the hidden opt-in Arduino seam links the curated upstream `FTD` source slice
- `otInstanceInitSingle()` is now hardware-proven on the board, not just on the
  linker:
  - minimal proof:
    `examples/Thread/OpenThreadCoreStageProbe/OpenThreadCoreStageProbe.ino`
  - broader PAL + staged-core proof:
    `examples/Thread/OpenThreadPlatformSkeletonProbe/OpenThreadPlatformSkeletonProbe.ino`
- the previous runtime blocker was concrete and is now fixed:
  - `otInstanceInitSingle()` asserted in `Crypto::HmacSha256::HmacSha256()`
  - root cause:
    PAL `otPlatCryptoHmacSha256*` / `otPlatCryptoHkdf*` /
    `otPlatCryptoSha256*` returned `OT_ERROR_NOT_CAPABLE`
  - fix:
    repo PAL now provides real software-backed SHA-256 / HMAC-SHA256 / HKDF
    implementations for the staged core path
- the default public Arduino build still compiles cleanly with the staged seam
  disabled
- the staged seam now also proves the first real Thread role on hardware:
  - `OpenThreadRoleStageProbe` applies a fixed active dataset, enables IP6,
    enables Thread, and reaches `OT_DEVICE_ROLE_LEADER` on a single XIAO
    nRF54L15 board
- the staged settings backend now chunks oversized OpenThread values across
  multiple `Preferences` entries, which removed the earlier operational-dataset
  persistence assert during `otDatasetSetActive()`
- the staged settings backend now also has a safe persistent-store headroom fix:
  - `Preferences` grows from `28` to `35` entries, which still fits alongside
    EEPROM emulation and BLE bond retention in the shared `FLASH_BOND` page
  - legacy `28`-entry blobs are migrated in place on boot instead of being
    wiped
- two-board staged attach is now proven on attached hardware:
  - starting from the captured full `28`-entry legacy `Preferences` blob on
    both boards, one board settles as `leader` and the second settles as
    `child`
  - repo-owned logs:
    `measurements/thread_phase3_latest/role_probe_board_a_legacy28_migrated.log`
    `measurements/thread_phase3_latest/role_probe_board_b_legacy28_migrated.log`
- the PAL still intentionally leaves these advanced crypto paths as explicit
  non-goals for now:
  - `otPlatCryptoEcdsa*`
  - `otPlatCryptoPbkdf2GenerateKey`

## First Executable Slice After This

The next real implementation step should be:

1. keep `OpenThreadRoleStageProbe` as the repo-owned checkpoint and extend it
   from `leader + child` attach into actual Thread payload validation:
   - add a real ping / UDP smoke path over the attached link
   - keep the proof on two attached boards
2. decide whether the repo should keep the PAL-owned software SHA/HMAC/HKDF path
   for staged core bring-up, or later replace it with a compiled upstream
   crypto fallback slice once the mbedTLS/PSA intake is ready
3. once attached-node traffic is alive:
   - revisit `otPlatCryptoEcdsa*`
   - revisit `otPlatCryptoPbkdf2GenerateKey`
   - start the first real reference-network / border-router validation

## Integration Rule

Do not move the full upstream source tree under the Arduino library `src/`
folder. Keep it staged under `third_party/openthread-core` and only expose the
curated bridge files needed for the enabled build slice.
