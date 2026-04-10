# nRF54L15 Hardware Implementation Gap Map

This document tracks the remaining nRF54L15 hardware surface that is either:

- already implemented and usable
- present in the silicon but not wrapped cleanly yet
- present in the silicon but low-value on the XIAO board
- often confused with "hardware missing" even though the real gap is still higher-level software

The goal is to keep the next hardware work focused on the parts of the SoC that
still unlock meaningful capability for this core, not to treat every datasheet
block as equally urgent.

Status baseline:

- repo release line: `0.4.1`
- board target: Seeed XIAO nRF54L15
- implementation style: direct register-level core, no Zephyr runtime, no NCS runtime

## Already Covered Well

The current hardware surface is already strong for normal Arduino and wireless
bring-up work.

Implemented or exposed today:

- GPIO / interrupts / GPIOTE
- SPI controller and SPI target
- I2C controller plus I2C target through `Wire`
- UART / serial routing on the XIAO board
- broader serial-fabric instance exposure, including instance `22` and `30`
- SAADC / VBAT / TEMP
- TIMER / PWM / GRTC / watchdog / low-power paths
- COMP / LPCOMP / QDEC
- PDM / I2S
- DPPI helpers and public `Egu` wrapper
- CRACEN RNG, AAR, ECB, CCM
- `KMU` base wrapper and safe metadata / task surface
- `TAMPC` wrapper with status, protected controls, active-shield config, and
  debug-control helpers
- raw RADIO plus BLE / Zigbee / proprietary helpers
- VPR shared-memory transport foundation for controller-style experiments

Evidence:

- [Nrf54L15-Clean-Implementation README](../hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/README.md)
- [Development Notes](development.md)
- [Wire.h](../hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/Wire.h)
- [Hardware Implementation Phases](HARDWARE_IMPLEMENTATION_PHASES.md)

## Highest Priority

These are the remaining hardware-facing gaps that are worth doing first because
they unlock real nRF54-specific value, not just cosmetic parity.

### P0: KMU -> CRACEN Proof

Current state:

- the base `Kmu` wrapper now exists
- safe slot-task and metadata probing are in place
- a dedicated-slot `KMU -> CRACEN IKG` seed proof now exists and was validated
  on real hardware
- the remaining work is to broaden that into more reusable CRACEN consumer
  helpers and cleaner secure/non-secure documentation

Why it matters:

- this is the right way to move sensitive key material toward CRACEN
- it matters for secure product work, not just demos
- it is one of the clearest "nRF54-specific hardware advantage" gaps still open

What should exist:

- controlled CRACEN key handoff path
- one proof example that uses KMU material in hardware crypto without falling
  back to normal RAM-owned key handling
- clearer secure / non-secure behavior notes

Suggested validation:

- slot status / metadata checks
- CRACEN/ECB or CRACEN/CCM path using KMU-loaded key material
- persistence and reset behavior validation

### P0: VPR / MAILBOX Controller Service Layer

Current state:

- the datasheet exposes the RISC-V coprocessor and mailbox path
- the repo now has shared-memory transport, boot/control groundwork, and a
  working CS-oriented VPR demo path
- there is now a generic VPR shared-transport probe beyond the CS workflow
- there is now a reusable host-side controller-service wrapper for the current
  vendor-style transport commands
- there are now validated non-CS VPR-side offload proofs through the built-in
  `FNV1a`, `CRC32`, `CRC32C`, autonomous ticker services, and VPR hibernate
  saved-context flow plus matching probe sketches
- there is still no richer VPR-side general controller service or reusable
  softperipheral runtime on top of that transport
- dedicated local probes now exist for VPR hibernate resume and loaded-image
  restart
- the honest current lifecycle result is:
  - hibernate saved-context works
  - repeated loaded-image restart is now hardware-validated on both attached
    boards through `VprRestartLifecycleProbe`
  - `VprHibernateResumeProbe` now passes on both attached boards through a
    deterministic reset-after-hibernate service restart that preserves retained
    host-side service state
  - the service restart path now intentionally disables raw VPR hardware
    context restore before reboot, because that hardware path was board-unstable
    while the service-level retained restart is cross-board stable
  - true raw VPR CPU-context resume remains an investigation topic, but
    lifecycle correctness for the reusable service path is no longer blocked on
    board-to-board variance

Why it matters:

- this is one of the biggest differentiators of the nRF54 family
- a lot of future capability depends on it, including soft peripherals like sQSPI
- without this, the core is still leaving one of the major SoC features unused

What should exist first:

- a richer VPR-side general controller/offload service on top of the transport
- one non-CS proof that the path is useful beyond the current demo responder
- clearer memory ownership and lifecycle rules for shared buffers

Suggested validation:

- message roundtrip through the mailbox
- deterministic trigger / completion flow
- retained-context behavior across low-power modes
  current proof now exists through `VprHibernateContextProbe`
- reset-after-hibernate behavior through `VprHibernateResumeProbe`
  current proof now shows a stable reset-after-hibernate retained service
  restart across both attached boards, not raw VPR CPU-context resume

### P1: TAMPC Reset / External-Tamper Characterization

Current state:

- the public `Tampc` wrapper now exists
- status reporting, protected control writes, active-shield channel control,
  glitch monitor control, and domain/AP debug control are implemented
- `TampcAdvancedConfigProbe` now exercises that wider surface on hardware
- what still needs more work is reset-cause behavior, external-tamper
  semantics, and board-specific characterization beyond register-level control

Why it matters:

- this is a real security hardware block, not a marketing extra
- it belongs with KMU/CRACEN in the "serious secure product" path
- it is one of the obvious datasheet features still missing from the public surface

What should exist next:

- clearer external tamper semantics and safe examples for the XIAO target
- reset-cause validation around internal/external tamper reset paths
- clearer secure/non-secure caveats in the public docs

Suggested validation:

- controlled status flag tests
- reset-enable behavior validation
- interaction with secure/non-secure startup policy

## Medium Priority

These are real hardware gaps, but they are more about breadth and completeness
than missing flagship capability.

### P1: Full Serial-Fabric Breadth And Cleanup

Current state:

- the SoC exposes five serial interfaces with HS and regular variants
- the current core now exposes more of the instance map cleanly, including the
  extra `22` and `30` serial-fabric paths
- `SerialFabricRuntimeProbe` now exercises extra `UARTE`, `TWIM`, and `SPIM`
  instance bring-up successfully on hardware
- what is still missing is broader multi-instance cleanup, HS-instance notes,
  and deeper examples beyond bring-up

What is still missing or partial:

- broader runtime checks for the newly exposed `SPIM`, `SPIS`, `TWIM`, `TWIS`,
  and `UARTE` instances beyond simple bring-up
- clearer HS-instance usage notes in examples/docs
- more multi-instance examples instead of only compile-oriented probes

Why it matters:

- this improves advanced board ports and custom pin-routing work
- it reduces the gap between "works on XIAO defaults" and "usable as a general nRF54L15 platform"

### P1: Multi-Instance Peripheral Breadth

Current state:

- the wrappers are good, but some peripheral families are effectively represented by one validated instance even when the SoC has more

Candidates here:

- more `PWM` instances
- more `TIMER` instances
- `PDM21`
- cleaner exposure of secondary `GPIOTE` / fabric helper instances
- broader `WDT` instance handling where relevant

This is not blocked by missing hardware knowledge. It is mostly wrapper cleanup,
example coverage, and validation effort.

### P2: Broader Peripheral Breadth

Current state:

- the public `Egu` wrapper now exists
- the next breadth work is broader validation and the remaining secondary
  instance cleanup across selected families

## Lower Priority

These blocks are real, but they are not the next best use of time on the XIAO board.

### P2: NFCT

Current state:

- the SoC has `NFCT`
- the core explicitly does not wrap it today

Why it is low priority:

- the XIAO board does not expose a practical NFC antenna path for normal sketches
- implementing it now would increase maintenance without helping the main board target much

This becomes worth revisiting only if:

- a different nRF54L15 board target is added
- or there is a specific need for NFC-A listener/tag work

### P2: Native USB Device Surface

Current state:

- the XIAO board routes serial through the external SAMD11 bridge
- the current core model is built around that board reality

Why it is low priority:

- native nRF54 USB device support is not the practical board path here
- it would be more relevant for another board variant than for the shipped XIAO setup

### P2: Trace / Debug Specialty Blocks

Examples:

- ETM
- ITM
- TPIU
- advanced authenticated debug plumbing beyond what is needed for normal development

These are real features, but they are not the next product-value unlocks for the core.

## Not Primarily Hardware Gaps

These are important, but the main work is above the raw peripheral layer.

### BLE

The major remaining BLE work is now mostly controller/host-stack behavior, not
missing silicon wrappers:

- broader generic central/client behavior
- more complete pairing/bond persistence coverage
- fuller Bluefruit parity on less-common flows
- finished user-facing channel sounding

The current channel-sounding code itself already documents that the RTT side is
still incomplete enough to be unreliable as a clean-core ranging feature.

### Zigbee

The current gap is not "the radio hardware is missing." The remaining work is:

- fuller Zigbee 3.0 device/cluster coverage
- richer HA personalities
- better sleepy remote/device typing
- more coordinator/interoperability validation breadth

### Thread and Matter

These are future stack projects, not missing peripheral wrappers.

## Recommended Implementation Order

If the goal is "maximum value per unit of engineering time", the order should be:

1. `VPR` / `MAILBOX` controller service layer
2. reusable CRACEN consumer integration beyond the current `KMU -> CRACEN IKG` proof
3. TAMPC reset / external-tamper characterization
4. serial-fabric multi-instance cleanup and broader runtime coverage
5. broader peripheral-breadth validation
6. optional board-limited blocks like `NFCT`

For the concrete execution breakdown, see:

- [`HARDWARE_IMPLEMENTATION_PHASES.md`](HARDWARE_IMPLEMENTATION_PHASES.md)

## Suggested Phase Breakdown

### Phase 1: Security Hardware

- `Kmu` wrapper
- `Tampc` wrapper
- examples and diagnostics
- secure key path integration with CRACEN helpers

Status:

- base wrappers and diagnostic examples are in
- `KMU -> CRACEN IKG` secure key proof is in
- advanced `Tampc` config/runtime probing is in

### Phase 2: Coprocessor Foundation

- VPR boot/trigger/mailbox layer
- one proof-of-life example
- retained-context and low-power interaction checks

Status:

- shared-memory/VPR groundwork is in
- a CS-oriented demo responder exists
- a generic shared-transport probe exists
- a reusable host-side controller-service layer now exists
- a first non-CS VPR offload proof now exists
- the richer VPR-side controller/offload service layer is still open

### Phase 3: Serial-Fabric Completion

- complete instance map exposure
- HS-instance documentation
- validation examples for non-default routes

Status:

- extra instance exposure is in
- a real runtime probe for the extra `22` / `30` instance paths is in
- broader multi-instance cleanup is still open

### Phase 4: Remaining Peripheral Breadth

- broader `Egu` / secondary-instance validation
- second-instance cleanup for selected peripherals
- lower-priority board-specific extras

## Practical Conclusion

The core is already past the point where "basic hardware support" is the main
problem. The high-value remaining hardware work is concentrated in:

- `VPR` / mailbox / softperipheral groundwork
- reusable secure-consumer integration on top of `KMU`
- deeper `TAMPC` reset/external-tamper characterization

Everything else is either:

- already implemented enough to use
- mainly a completeness pass
- or a higher-level BLE/Zigbee/Thread/Matter software effort
