# Post-0.5.0 Implementation Plan

This document is the concrete follow-on plan after the `0.5.0` release.

`0.5.0` is the point where the repo has:

- the reusable VPR shared-transport/controller-service layer
- a dedicated VPR-backed CS image and two-board CS bring-up path
- broader VPR proof examples beyond CS
- a corrected Bluefruit active-scan callback path for real `SCAN_RSP` reports

What is still missing now is less about "basic hardware bring-up" and more
about turning the current groundwork into complete reusable stack features.

## Phase 1: VPR Runtime Hardening

Goal:

- move from a probe/offload service to a reusable VPR runtime with clear
  lifecycle and message ownership rules

Deliverables:

- documented host/VPR shared-memory ownership rules
- versioned transport/service ABI notes
- stronger recovery paths for service restart and hibernate failure
- one stable async event path beyond ticker-only events

Validation:

- repeated boot/restart/hibernate regression
- queue-depth and event-loss checks
- no regressions in existing VPR examples

Exit criteria:

- VPR is a reusable subsystem in the repo, not a collection of point probes

## Phase 2: BLE Controller Offload On VPR

Goal:

- move the first real BLE controller responsibilities onto VPR

Deliverables:

- VPR-side BLE controller service skeleton
- host/controller boundary inside the clean core
- connection-state ownership rules between CPUAPP and VPR
- first offloaded controller task beyond the current CS demo responder

Validation:

- board-to-board BLE regression with the offloaded path enabled
- host BLE NUS regression unchanged
- power/latency sanity compared to the current CPUAPP-owned path

Exit criteria:

- at least one real BLE control path is owned by VPR instead of the sketch-side
  main core

## Phase 3: Full Channel Sounding Completion

Goal:

- move channel sounding from bring-up status to a real controller-backed feature

Deliverables:

- real connected CS procedure flow on top of the BLE/VPR controller path
- controller-owned capability/config/security/procedure handling
- real CS result delivery instead of the current demo-trigger split
- clearer calibration and error-model docs

Validation:

- two-board regression with real controller-owned CS procedure handling
- repeatable phase/RTT output on the repo examples
- no dependence on sketch-side fake peer-result injection

Exit criteria:

- the CS path is no longer described as a demo responder plus host injection

## Phase 4: Thread Foundation

Goal:

- build the minimum Thread-capable foundation needed before Matter

Deliverables:

- clear 802.15.4 MAC/NWK ownership model
- Thread stack direction decision:
  - direct clean-core implementation, or
  - VPR-backed controller/service split with CPUAPP host API
- minimal Thread bring-up plan and artifact list

Validation:

- architecture review plus one concrete bring-up milestone

Exit criteria:

- Matter no longer depends on unresolved stack-architecture decisions

## Phase 5: Matter API And Home Assistant Compatibility

Goal:

- provide an Arduino-facing Matter path that feels close to existing Arduino
  Matter board APIs while staying honest about what is actually implemented

Deliverables:

- draft Arduino Matter API compatibility surface
- first commissioning/device type target
- Home Assistant validation plan
- required VPR/Thread/BLE ownership dependencies made explicit

Validation:

- one end-to-end HA-friendly commissioning/discovery flow on real hardware

Exit criteria:

- Matter work starts from a deliberate API/runtime plan instead of ad hoc glue

## Phase 6: HAL Monolith Reduction

Goal:

- keep shrinking `nrf54l15_hal.cpp` so controller/runtime work is easier to
  reason about

Deliverables:

- split the remaining BLE/Zigbee/runtime-heavy slices into focused units
- document the intended ownership of the remaining monolith

Validation:

- no regressions in BLE/Zigbee compile/runtime smoke paths

Exit criteria:

- the remaining monolith is no longer the default landing zone for new work

## Priority Order

The practical order should be:

1. VPR runtime hardening
2. BLE controller offload on VPR
3. full channel sounding completion
4. Thread foundation
5. Matter API and Home Assistant validation
6. continued HAL decomposition

That order keeps the work aligned with the actual dependency chain:

- VPR first
- BLE controller ownership next
- controller-backed CS on top of that
- Thread/Matter only after the lower wireless/runtime split is real
