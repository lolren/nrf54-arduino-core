# BLE And Channel Sounding Completion Checklist

This checklist tracks practical completion for the repo's BLE and Channel
Sounding work on `nRF54L15`.

Scope note:

- this is not a claim that the repo implements every Bluetooth Core 6.0 feature
- the target is a strong BLE base plus a controller-backed CS path with clear
  hardware/runtime ownership
- the current CS `~0.75 m` output is nominal synthetic regression output only,
  not a physical tape-measure claim
- recent live spacing during checks has been roughly `0.7 m` to `1.0 m`

## Hardware And Board Layer

- [x] BLE-capable board package for `XIAO nRF54L15 / Sense`
- [x] BLE-capable board package for `HOLYIOT-25008 nRF54L15 Module`
- [x] BLE-capable board package for `HOLYIOT-25007 nRF54L15 Module`
- [x] BLE-capable board package for `Generic nRF54L15 Module (36-pad)`
- [x] Working CMSIS-DAP / Pico Debugprobe programming path for the module boards
- [x] Board pin maps and BLE compile compatibility across the supported boards
- [x] Real XIAO RF-path helpers and safe no-op antenna helpers on fixed-antenna module boards
- [x] Two-board physical CS regression setup
- [x] Real RF calibration and board-pair antenna-delay characterization for the active `XIAO nRF54L15 / Sense` CS setup
- [ ] Measured BLE / CS power characterization on real hardware
- [x] Measured BLE / CS latency characterization on real hardware
- [x] Real physical-distance calibration model for CS results

## CPUAPP BLE Base

- [x] Legacy advertising
- [x] Extended advertising
- [x] Passive scan
- [x] Active scan
- [x] Stable connected-link scheduling
- [x] ATT / GATT peripheral flows
- [x] ATT / GATT client flows
- [x] Nordic UART Service transport and wrapper support
- [ ] Full Bluetooth Core 6.x controller feature coverage beyond current repo scope

## Generic VPR BLE Service

- [x] Shared CPUAPP <-> VPR transport/runtime
- [x] Async event path on VPR
- [x] VPR-owned legacy non-connectable advertising scheduler state
- [x] VPR-owned retained advertising payload storage and readback
- [x] VPR-owned single-link connected-session state
- [x] CPUAPP-readable shared-state connected-link snapshot
- [x] VPR-owned connect and disconnect async event publication
- [x] Generic-service CS link bind and readiness state for the current VPR-owned BLE connection
- [x] Generic-service CS workflow/config shadow state for the current VPR-owned BLE connection
- [x] Generic-service nominal CS workflow runtime and completion summary for the current VPR-owned BLE connection
- [x] Persistent generic BLE controller service on VPR that owns the live link path instead of just proving it through probes
- [x] Reusable BLE controller-service path that higher BLE features can bind to without booting a separate dedicated runtime

## Channel Sounding Core

- [x] Controller-style CS command builders, parsers, and host layers
- [x] Dedicated VPR-backed CS image
- [x] Handle-scoped CS session on VPR
- [x] Create / remove / security / set-procedure / enable lifecycle on VPR
- [x] Multi-procedure, subevent, chunking, pacing, and direct-control regressions
- [x] Stored-config selection / promotion / eviction / authority reporting on VPR
- [x] Two-board nominal CS regression
- [x] Stable nominal synthetic regression output around `~0.75 m`
- [x] Real controller-owned CS result production instead of the current nominal synthetic result shaping
- [ ] Real physically defensible ranging output
- [x] Clear calibration and error-model documentation

## BLE Link To CS Handoff

- [x] Generic VPR connected handle can be imported into the dedicated CS path
- [x] Reusable host-side handoff helpers
- [x] Standalone proof sketch for BLE-connection-to-CS handoff
- [x] Normal initiator-surface handoff regression through `hcivprhandoffdemo`
- [x] Persistent in-place generic-service-to-CS runtime instead of `generic service -> boot dedicated CS image -> import handle`

## User-Facing And Release Quality

- [x] Large CS / VPR regression pack and proof logs
- [x] VPR proof examples for advertising, connection state, and CS handoff
- [x] Hardened release/install pipeline for Boards Manager assets
- [x] Normal sketch-facing API/example for `start BLE link, then run CS` without probe-style validation plumbing
- [x] Release checkpoint after the persistent runtime path lands

## Outside Current Practical Scope

- [ ] Full Bluetooth Core 6.x controller feature coverage beyond the repo's current architecture
- [ ] Multi-link BLE controller ownership on VPR
- [ ] Thread foundation
- [ ] Matter commissioning/device support

## Current Next Work

1. Real physically defensible ranging output on top of the landed controller-produced CS payload path.
2. Measured BLE / CS power characterization on real hardware.
