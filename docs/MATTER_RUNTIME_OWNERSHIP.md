# Matter Runtime Ownership

This document freezes the first Matter foundation decisions for the Arduino
repo at the staged foundation stage, before any commissioned Matter runtime is
claimed.

These same decisions are mirrored in
`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_platform_nrf54l15.h`.

## First Foundation Decisions

- `CPUAPP` will host the future Matter runtime.
- Matter transport is `Thread` first, using the same staged `OpenThread`
  instance on `CPUAPP`.
- the first commissioning target is `on-network-only`; BLE rendezvous is not
  part of the first Matter slice.
- the first device type is an `on-off-light`.
- `VPR` is not part of the first Matter foundation path.
- a minimal upstream `connectedhomeip` header seed is now staged at commit:
  `337f8f54b4f0813681664e5b179dc3e16fdd14a0`
  and it now includes staged support implementation units
  `src/lib/support/Base64.cpp` / `src/lib/support/Base85.cpp` /
  `src/lib/support/TimeUtils.cpp` / `src/lib/support/BytesToHex.cpp` /
  `src/lib/support/ThreadOperationalDataset.cpp` plus staged core/data-model
  headers including `src/lib/core/DataModelTypes.h`, plus one staged core
  error unit `src/lib/core/CHIPError.cpp` / `src/lib/core/ErrorStr.cpp` plus
  one staged core key-id unit `src/lib/core/CHIPKeyIds.cpp` through the
  Arduino Matter foundation seam
- Matter onboarding-code generation is repo-owned for now in
  `src/matter_manual_pairing.h` / `src/matter_manual_pairing.cpp`, using the
  same decimal chunk layout, Verhoeff check digit, QR bit packing, and Base38
  encoding expected by the upstream Matter setup-payload tests.
- the first foundation device target is now also repo-owned in
  `src/matter_foundation_target.h` / `src/matter_foundation_target.cpp`
  and is intentionally bounded to:
  - root-node + on/off-light endpoint metadata
  - onboarding code generation
  - explicit Thread dependency resolution
  - Thread dataset export into staged CHIP TLV form
- the staged on-network on/off-light examples are API/bootstrap proofs only;
  they are not a commissioned Matter runtime.

## Ownership Map

- entropy:
  `CracenRng`
- symmetric crypto / key storage:
  existing CRACEN-backed paths first, with future CHIP-specific glue sitting on
  top rather than replacing the current platform primitives
- Thread transport:
  staged `OpenThread` core on the current CPUAPP + `ZigbeeRadio` backend path
- persistent storage:
  `Preferences`, with Matter using its own keys/namespaces instead of
  overwriting Thread state
- timebase:
  existing HAL/GRTC-backed millisecond + microsecond timing paths
- event loop:
  cooperative Arduino loop pumping on `CPUAPP`, following the existing staged
  Thread processing model

## Adaptation Boundary

- `connectedhomeip` should own:
  - device model
  - secure sessions
  - interaction model
  - exchange manager
  - Matter data model state
- repo platform glue should own:
  - board bring-up
  - clocks / time
  - entropy / hardware-backed crypto entry points
  - settings backend
  - Thread instance ownership and radio backend selection
  - the current minimal CHIP config shim used only for staged error-layer
    bring-up
  - the current minimal CHIP core umbrella shim used only for staged
    TimeUtils bring-up
  - the current minimal CHIP encoding shim used only for staged BytesToHex
    bring-up
  - the current minimal CHIP logging shim used only for staged BytesToHex
    bring-up
  - the current minimal `CodeUtils` shim used only for staged key-id bring-up
  - the current onboarding-code helper used only to prove future on-network
    commissioning manual-code and QR-code generation
  - the current compile-only foundation target used only to prove that a first
    on-network on/off-light device shape is mechanically possible in-tree
- the existing `Nrf54ThreadExperimental` wrapper is not the long-term Matter
  transport API; Matter should sit closer to the staged `OpenThread` instance
  and platform glue once that compile target exists
- the current staged `ThreadOperationalDataset` helper is only a bounded
  foundation seam for dataset TLV handling; it is not yet a claim that Matter
  commissioning or a CHIP device runtime exists

## Current Non-Claims

- no commissioned Matter runtime is claimed yet
- no real commissioner flow is claimed yet
- no BLE rendezvous path is claimed yet
- no Home Assistant integration is claimed yet
- the staged import is still a minimal
  header/support/error/key/time/hex/thread-dataset/data-model seed, not a
  full upstream tree
