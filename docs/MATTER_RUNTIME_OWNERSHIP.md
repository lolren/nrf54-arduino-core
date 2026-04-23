# Matter Runtime Ownership

This document freezes the first Matter foundation decisions for the Arduino
repo before any real `connectedhomeip` compile target is claimed.

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
  `src/lib/support/TimeUtils.cpp` plus one staged core error unit
  `src/lib/core/CHIPError.cpp` / `src/lib/core/ErrorStr.cpp` plus one staged
  core key-id unit `src/lib/core/CHIPKeyIds.cpp` through the hidden Arduino
  seam

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
  - the current minimal `CodeUtils` shim used only for staged key-id bring-up
- the existing `Nrf54ThreadExperimental` wrapper is not the long-term Matter
  transport API; Matter should sit closer to the staged `OpenThread` instance
  and platform glue once that compile target exists

## Current Non-Claims

- no compileable CHIP library target is claimed yet
- no Matter commissioning flow is claimed yet
- no BLE rendezvous path is claimed yet
- no Matter device example is claimed yet
- the staged import is still a minimal header/support/error/key/time seed, not a
  full
  upstream tree
