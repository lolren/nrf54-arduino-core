# Thread And Matter Implementation Plan

This document turns the repo's future `Thread` and `Matter` work into a
concrete execution plan with explicit checkpoints.

Scope:

- target silicon: `nRF54L15`
- first supported board target: `XIAO nRF54L15 / Sense`
- first runtime direction: `CPUAPP-first`
- first upstreams:
  - `OpenThread` for Thread
  - `connectedhomeip` for Matter
- first reference implementations:
  - `nRF54L15` product/datasheet material
  - `Zephyr` / `nRF Connect SDK` board and 802.15.4 / Thread / Matter ports

Why this direction:

- the repo already has a usable `IEEE 802.15.4` hardware path through
  `ZigbeeRadio`, so a first `OpenThread` radio/alarm/entropy platform port is a
  more direct next step than inventing a new controller-service split up front
- `Matter` should not start until a real `Thread` path exists, otherwise it
  becomes glue on top of unstable wireless ownership
- the existing `VPR` controller-service work is useful later, but it is not the
  shortest path to the first honest `Thread` / `Matter` milestone

## Planning Status

- [x] Existing `IEEE 802.15.4` repo assets identified
- [x] Primary upstreams selected: `OpenThread`, `connectedhomeip`, `Zephyr` / `NCS`
- [x] Initial runtime direction selected: `CPUAPP-first Thread`, `Matter after Thread`
- [x] Current `VPR` / `BLE` / `CS` dependency chain reviewed
- [ ] Thread runtime ownership frozen in code
- [ ] Matter runtime ownership frozen in code

## Non-Goals For First Pass

- [ ] Multi-board Thread support on day one
- [ ] Full `Matter` device-type coverage
- [ ] Full production security hardening before bring-up
- [ ] `VPR`-offloaded Thread radio/controller in the first working milestone
- [ ] Thread Border Router in this repo

## Existing Repo Assets To Reuse

- [x] `ZigbeeRadio` `IEEE 802.15.4` PHY / MAC-lite path
- [x] low-level radio / clock / timer / interrupt ownership in the clean HAL
- [x] `CRACEN` / security helper groundwork
- [x] `VprSharedTransportStream` / `VprControllerServiceHost` as future offload seams
- [x] board support and upload/recovery flow for `XIAO nRF54L15 / Sense`

Primary in-tree touch points:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/zigbee_stack.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_vpr.h`

## Phase 0: Foundation Freeze

Goal:

- freeze the architectural rules before importing large upstreams

Checklist:

- [ ] choose exact ownership split for `RADIO`, clocks, timers, entropy, crypto
- [ ] decide how third-party code is vendored:
  - subtree
  - snapshot
  - generated import script
- [ ] define allowed first-pass board set
- [ ] define allowed first-pass Thread role set:
  - detached
  - child
  - router
- [ ] define allowed first-pass Matter feature set
- [ ] define where `OpenThread` platform glue lives in-tree
- [ ] define where `Matter` glue lives in-tree

Exit criteria:

- [ ] no unresolved ownership argument remains for `RADIO`, alarms, entropy, or crypto

## Phase 1: OpenThread Platform Port Skeleton

Goal:

- get a minimal in-tree `OpenThread` platform layer compiling against this core

Checklist:

- [x] add third-party `OpenThread` public-header intake path
- [x] add build-system integration for a disabled-by-default Thread skeleton target
- [x] create platform stubs for:
  - radio
  - alarm millisecond
  - alarm microsecond if needed
  - entropy
  - settings
  - diagnostics
  - logging
- [x] map repo timebase helpers to the `OpenThread` alarm model
- [x] add temporary entropy bring-up fallback for the PAL skeleton
- [ ] map repo crypto entry points cleanly
- [x] add one compile-only validation sketch / probe

Validation:

- [x] clean compile for a minimal `OpenThread` platform target
- [x] no regressions in existing BLE / Zigbee / CS builds

Exit criteria:

- [x] `OpenThread` platform skeleton compiles in-tree without manual patching

Current status note:

- this is a compile-valid PAL skeleton plus probe
- it is not yet a real `OpenThread` core build
- it is not yet a working Thread node

## Phase 2: Real 802.15.4 Thread Radio Path

Goal:

- replace stub radio ownership with a real `OpenThread` radio backend on top of
  the existing `IEEE 802.15.4` path

Checklist:

- [ ] implement TX path for Thread MAC frames
- [ ] implement RX path with usable frame metadata
- [ ] implement channel and power control hooks
- [ ] implement receive sensitivity / energy scan path if required
- [ ] implement source-match / ack / pending handling needed by Thread roles
- [ ] implement timing-critical radio state transitions cleanly
- [ ] decide whether `ZigbeeRadio` is:
  - refactored into a reusable 802.15.4 lower layer, or
  - wrapped directly for Thread as a first pass

Validation:

- [ ] packet-level bring-up against `OpenThread` radio diagnostics
- [ ] two-board `OpenThread` radio smoke validation
- [ ] no regressions in current Zigbee examples sharing the same hardware path

Exit criteria:

- [ ] `OpenThread` can send and receive real `802.15.4` frames through this repo

## Phase 3: Thread Role Bring-Up

Goal:

- get a first honest working Thread node on hardware

Checklist:

- [ ] MLE attach path works
- [ ] child role works on the first target board
- [ ] router role works if feasible in the first pass
- [ ] dataset handling exists for fixed test credentials
- [ ] CLI or probe path exposes attach state and role transitions
- [ ] repo docs explain exactly what Thread roles are actually supported

Validation:

- [ ] attach to a reference Thread network
- [ ] two-board or external-border-router validation
- [ ] real ping / UDP smoke path over Thread

Exit criteria:

- [ ] the repo has a documented working Thread node, not just a compile-only port

## Phase 4: Arduino Thread API Surface

Goal:

- expose a small Arduino-facing Thread surface without overpromising

Checklist:

- [ ] define minimal API for:
  - begin
  - attach / join
  - role / state query
  - dataset set / get
  - UDP send / receive
- [ ] add one or two normal examples that are not probe-only
- [ ] keep API names honest about supported scope
- [ ] document unsupported Thread features explicitly

Validation:

- [ ] example compile coverage
- [ ] real hardware join/send/receive validation

Exit criteria:

- [ ] Thread support is usable from an Arduino sketch without reaching into raw port glue

## Phase 5: Matter Foundation

Goal:

- make `Matter` technically possible on top of a real Thread path

Checklist:

- [ ] add third-party `connectedhomeip` intake path
- [ ] define minimal platform/adaptation layer boundaries
- [ ] map entropy / crypto / storage / time / event-loop ownership
- [ ] decide whether first commissioning target is:
  - on-network only
  - BLE rendezvous plus Thread
- [ ] define exact first device type

Validation:

- [ ] compile-only `Matter` platform target
- [ ] no unresolved dependency on missing Thread behavior

Exit criteria:

- [ ] a real first-device `Matter` path is mechanically possible in-tree

## Phase 6: Matter Commissioning And First Device

Goal:

- ship one real end-to-end `Matter` device type

Checklist:

- [ ] implement first commissioning flow
- [ ] implement first device type state model
- [ ] expose a small Arduino-facing API for that device type
- [ ] validate with a real commissioner
- [ ] validate with `Home Assistant`

Validation:

- [ ] commission on real hardware
- [ ] discover/control from `Home Assistant`
- [ ] reconnect / reboot behavior checked

Exit criteria:

- [ ] the repo has one honest working `Matter` device example on real hardware

## Phase 7: Hardening And Expansion

Goal:

- turn bring-up into supportable product surface

Checklist:

- [ ] add more boards if the radio/power path is stable
- [ ] add more device types only after the first one is solid
- [ ] review whether `VPR` offload is worth it for:
  - Thread timing-sensitive work
  - Matter runtime separation
- [ ] broaden interoperability matrix
- [ ] characterize power and latency for Thread and Matter paths

Exit criteria:

- [ ] Thread / Matter work is no longer marked experimental in the repo docs

## Validation Matrix

Thread should not be claimed until all of these are true for the first target:

- [ ] compile
- [ ] flash
- [ ] attach
- [ ] role transition
- [ ] packet exchange
- [ ] reboot recovery

Matter should not be claimed until all of these are true for the first target:

- [ ] compile
- [ ] flash
- [ ] commission
- [ ] discover
- [ ] control
- [ ] reboot recovery
- [ ] `Home Assistant` retest

## Recommended Immediate Order

1. Phase 0: foundation freeze
2. Phase 1: `OpenThread` platform skeleton
3. Phase 2: real `802.15.4` radio backend
4. Phase 3: first real Thread node
5. Phase 4: Arduino Thread API
6. Phase 5: Matter foundation
7. Phase 6: first Matter device

## First Tickable Deliverable

- [x] `OpenThread` platform port skeleton compiles in-tree

That is the next honest milestone. Not “Matter started”, not “Thread support”,
just the first concrete box that should be completed next.
