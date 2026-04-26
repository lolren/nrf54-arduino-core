# Thread And Matter Implementation Plan

Status baseline:

- Audit date: `2026-04-26`
- First supported board target: `XIAO nRF54L15 / Sense`
- First runtime direction: `CPUAPP-first`
- Thread upstream: `OpenThread`
- Matter upstream: `connectedhomeip`
- Radio backend: existing `ZigbeeRadio` / IEEE 802.15.4 path

This is the current public plan for Thread and Matter in this repo. Older
phase-by-phase scratch notes were removed because they were written before the
staged OpenThread and Matter foundation work landed.

## Current Claim Level

| Area | Claim today | Not claimed yet |
|---|---|---|
| Thread | Experimental staged OpenThread path with fixed dataset, leader/child/router paths, PSKc/passphrase helpers, and UDP examples. | Production Thread stack, joiner, commissioner, reference-network attach, reboot recovery, sleepy-device depth. |
| Matter | Foundation-only on-network/on-off-light shape with onboarding helpers and Thread dataset export seam. | Real commissioning, discovery, control from commissioner/Home Assistant, secure sessions, reboot/reconnect recovery. |
| VPR | Available as a future offload seam, not the first Thread/Matter owner. | VPR-owned Thread radio/controller or Matter runtime. |

## Architecture Decisions

- `CPUAPP` owns the first OpenThread core and Matter foundation path.
- `ZigbeeRadio` remains the first IEEE 802.15.4 backend for Thread.
- `VPR` is intentionally out of the first Thread radio path.
- `Preferences` is the first settings/persistence backend.
- `CracenRng` provides entropy.
- Existing CRACEN-backed and software fallback crypto glue is used only where
  the staged upstream paths need it.
- Thread Border Router is out of scope for this repo; use an external border
  router for product networks.
- Matter BLE rendezvous is out of first-pass scope; first Matter direction is
  on-network Thread commissioning.

## Phase Checklist

| Box | Phase | Status | Remaining work |
|---|---|---|---|
| [x] | 0. Ownership freeze | Done | Keep ownership docs synchronized with code constants. |
| [x] | 1. OpenThread platform skeleton | Done | Maintain compile coverage when upstream snapshots change. |
| [x] | 2. Real 802.15.4 radio backend | Done | Keep Zigbee regression coverage because Thread shares the same radio path. |
| [x] | 3. Experimental Thread runtime | Partial / experimental | Fixed dataset, role, and UDP examples exist; production validation remains open. |
| [x] | 4. Arduino Thread wrapper | Partial / experimental | Keep API explicitly experimental until reference-network and reboot tests pass. |
| [x] | 5. Matter foundation | Foundation done | Staged CHIP subset, onboarding helpers, on/off-light model, and Thread dataset export seam exist. |
| [ ] | 6. Matter commissioning | Not done | Implement real on-network commissioning, discovery, secure sessions, command handling, and Home Assistant validation. |
| [ ] | 7. Hardening | Not done | Soak tests, failure recovery, storage migration, interop matrix, and docs for production limits. |

## Thread Next Ticks

- [ ] Validate attach to a reference Thread network through an external border router.
- [ ] Add reboot recovery test for saved dataset/settings.
- [ ] Add joiner support or clearly document fixed-dataset-only limitations.
- [ ] Add commissioner support only if it becomes necessary for in-repo workflows.
- [ ] Expand sleepy-device behavior beyond the current staged runtime.
- [ ] Keep Zigbee examples green while Thread shares the 802.15.4 backend.

## Matter Next Ticks

- [ ] Wire the staged on/off-light model into a real CHIP exchange path.
- [ ] Implement on-network commissioning flow over the staged Thread path.
- [ ] Prove discovery from a commissioner.
- [ ] Prove command/control from a commissioner.
- [ ] Validate with Home Assistant.
- [ ] Prove reboot/reconnect recovery after commissioning.
- [ ] Keep BLE rendezvous explicitly out of scope until the on-network path works.

## Evidence Pointers

- Thread examples live under
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread`.
- Matter examples live under
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter`.
- Current status rollup lives in
  `docs/NRF54L15_FEATURE_MATRIX.md`.
- Runtime ownership is documented in
  `docs/THREAD_RUNTIME_OWNERSHIP.md` and
  `docs/MATTER_RUNTIME_OWNERSHIP.md`.

## Do Not Claim Yet

- Production Thread support.
- Thread Border Router support.
- Matter commissioning.
- Matter Home Assistant support.
- Matter BLE rendezvous.
- VPR-owned Thread or Matter runtime.
