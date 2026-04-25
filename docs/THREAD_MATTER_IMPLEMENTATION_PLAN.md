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
- [x] Thread runtime ownership frozen in code
- [x] Matter runtime ownership frozen in code

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

- [x] choose exact ownership split for `RADIO`, clocks, timers, entropy, crypto
- [x] decide how third-party code is vendored:
  - subtree
  - snapshot
  - generated import script
- [x] define allowed first-pass board set
- [x] define allowed first-pass Thread role set:
  - detached
  - child
  - router
- [x] define allowed first-pass Matter feature set
- [x] define where `OpenThread` platform glue lives in-tree
- [x] define where `Matter` glue lives in-tree

Exit criteria:

- [x] no unresolved ownership argument remains for `RADIO`, alarms, entropy, or crypto

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
- [x] map repo crypto entry points cleanly
- [x] add one compile-only validation sketch / probe

Validation:

- [x] clean compile for a minimal `OpenThread` platform target
- [x] no regressions in existing BLE / Zigbee / CS builds

Exit criteria:

- [x] `OpenThread` platform skeleton compiles in-tree without manual patching

Current status note:

- this is now a compile-valid PAL skeleton plus a hidden staged-core bring-up
  seam with real board validation
- the PAL now maps repo-backed crypto entry points for RNG, AES-ECB, volatile
  key refs, SHA-256, HMAC-SHA256, HKDF, and the AES-CMAC-based PBKDF2 path
  OpenThread uses for PSKc generation
- ECDSA remains an explicit `OT_ERROR_NOT_CAPABLE` placeholder at this stage
- it is not yet a real `OpenThread` core build
- it is not yet a working Thread node

## Phase 2: Real 802.15.4 Thread Radio Path

Goal:

- replace stub radio ownership with a real `OpenThread` radio backend on top of
  the existing `IEEE 802.15.4` path

Checklist:

- [x] implement TX path for Thread MAC frames
- [x] implement RX path with usable frame metadata
- [x] implement channel and power control hooks
- [x] implement receive sensitivity / energy scan path if required
- [x] implement source-match / ack / pending handling needed by Thread roles
- [x] implement timing-critical radio state transitions cleanly
- [x] decide whether `ZigbeeRadio` is:
  - refactored into a reusable 802.15.4 lower layer, or
  - wrapped directly for Thread as a first pass

Validation:

- [x] packet-level bring-up against `OpenThread` radio diagnostics
- [x] two-board `OpenThread` radio smoke validation
- [x] no regressions in current Zigbee examples sharing the same hardware path

Exit criteria:

- [x] `OpenThread` can send and receive real `802.15.4` frames through this repo

Current status note:

- Thread ownership is now frozen in repo artifacts through
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/docs/THREAD_RUNTIME_OWNERSHIP.md`
  and the compile-time `OpenThreadRuntimeOwnership` constants in
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.h`
- the upstream `OpenThread` core scaffold is now staged in-tree at
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core`,
  and the first actual link blockers are pinned in
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/docs/THREAD_CORE_BRINGUP_MANIFEST.md`
- a hidden Arduino build seam now exists for the staged core through
  `build.thread_flags` / `build.thread_seam_flags`, and the hardware
  `OpenThreadPlatformSkeletonProbe` can report whether that seam is active in
  the current build without exposing a broken Tools-menu option
- that hidden seam now also proves the upstream version API path on hardware:
  the opt-in probe build reports the real upstream `otGetVersionString()`
  output plus the compiled `ot::Instance` footprint instead of the earlier
  placeholder marker
- the hidden seam has now crossed the first true runtime gate:
  `otInstanceInitSingle()` both links and initializes on real hardware through
  the in-tree `OpenThreadCoreStageProbe`, and the broader
  `OpenThreadPlatformSkeletonProbe` now flips from staged-core `0/0/0` to
  `1/1/1` after delayed instance init in the same PAL-heavy binary
- the normal public build still compiles with that staged seam disabled, so the
  current work remains isolated to the hidden Thread-core path
- `otPlatCAlloc()` / `otPlatFree()` are now implemented in the PAL, and the
  hardware `OpenThreadPlatformSkeletonProbe` reports `mem=1` alongside the
  existing policy/state-contract line
- the repo-owned first-pass core config header now exists at
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread-core-user-config.h`
- first-pass ownership is now explicit: `OpenThread` wraps `ZigbeeRadio`
  directly instead of introducing a second lower-layer refactor before bring-up
- channel and TX power settings now flow into the real `IEEE 802.15.4` backend
- `otPlatRadioTransmit()` now uses the real radio path and fires the normal
  `otPlatRadioTxStarted()` / `otPlatRadioTxDone()` boundary
- `otPlatRadioEnergyScan()` now uses the real Nordic ED sampler, reports the
  result through `otPlatRadioEnergyScanDone()`, and exposes raw ED plus mapped
  dBm state in the PAL snapshot
- single-board hardware proof now exists through
  `/home/lolren/Desktop/Nrf54L15/.build/thread_radio_phase2_runtime.log` and
  `/home/lolren/Desktop/Nrf54L15/.build/thread_radio_energy_runtime.log`
- the PAL RX path is now validated on two boards through in-tree
  `OpenThreadRadioPacketInitiator` / `OpenThreadRadioPacketResponder` probes,
  including real `otPlatRadioReceiveDone()` metadata for length, channel, RSSI,
  timestamp, short addresses, and ACK-request parsing
- Thread source-match tables now drive ACK frame-pending decisions through the
  real `ZigbeeRadio` callback seam, and `otPlatRadioTxDone()` now synthesizes
  the received MAC ACK frame back into the PAL when an ACK is present
- the PAL now restores the correct idle state after TX and energy-scan, and the
  single-board `OpenThreadPlatformSkeletonProbe` hardware run confirms the
  expected invalid-state gating for sleep-while-disabled, tx-from-sleep, and
  tx-during-energy-scan via
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/thread_phase2_latest/platform_skeleton_probe.txt`
- the shared `IEEE 802.15.4` path was rechecked against the existing Zigbee
  surface: all 9 in-tree Zigbee examples compile, and steady-state two-board
  `ZigbeePingInitiator` / `ZigbeePongResponder` traffic remains clean through
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/thread_phase2_latest/zigbee_regression_summary.txt`
- two-board hardware proof now exists for parent-side data-poll handling:
  matched child short address returns ACK pending set, unmatched short address
  returns ACK pending clear, using the in-tree Thread source-match probes and
  debugger-read result buffers
- dedicated reverse-direction PAL TX-ACK proof is now green again on hardware:
  the in-tree `OpenThreadRadioTxAckResponder` / `OpenThreadRadioTxAckPeer`
  bench returns a real MAC ACK with frame-pending set for the responder's
  outbound data-request frame
- two-board PAL-to-PAL packet smoke now exists in-tree: initiator-to-responder
  `PING` and responder-to-initiator `PONG`, both with MAC ACKs and debugger-read
  result buffers
- `otPlatDiagProcess()` now implements a minimal but real diag command path for
  `version`, `channel`, `power`, `start`, `send`, and `stats`, and two-board
  hardware proof exists that a diag `send` command from
  `OpenThreadRadioDiagInitiator` reaches `OpenThreadRadioDiagResponder` through
  `otPlatDiagRadioReceived()` with correct frame length / channel / RSSI /
  sequence metadata and debugger-read result buffers
- the current diag probe pair still treats reverse-direction reply reception on
  the original sender as follow-up work; the validated claim here is packet-
  level diag bring-up, not a polished diag ping/pong utility

## Phase 3: Thread Role Bring-Up

Goal:

- get a first honest working Thread node on hardware

Checklist:

- [x] MLE attach path works
- [x] child role works on the first target board
- [x] router role works if feasible in the first pass
- [x] dataset handling exists for fixed test credentials
- [x] CLI or probe path exposes attach state and role transitions
- [x] repo docs explain exactly what Thread roles are actually supported

Validation:

- [ ] attach to a reference Thread network
- [x] two-board or external-border-router validation
- [x] real ping / UDP smoke path over Thread

Exit criteria:

- [x] the repo has a documented working Thread node, not just a compile-only port

Current status note:

- the hidden staged-core seam now has a deterministic fixed-dataset role probe
  at
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/OpenThreadRoleStageProbe/OpenThreadRoleStageProbe.ino`
- hardware proof now exists for the first real single-board Thread role:
  after `otDatasetSetActive()`, `otThreadSetLinkMode()`, `otIp6SetEnabled()`,
  and `otThreadSetEnabled()`, the staged probe reaches `OT_DEVICE_ROLE_LEADER`
  on a real XIAO nRF54L15 board
- the staged settings path now supports oversized OpenThread values by chunking
  them across `Preferences` entries instead of failing on the old `48-byte`
  single-value limit
- the shared `Preferences` store now grows from `28` to `35` entries, which is
  the largest safe size that still links alongside EEPROM emulation and BLE
  bond retention in the shared `FLASH_BOND` page
- legacy `28`-entry `Preferences` blobs are migrated in place on boot instead
  of being discarded, so existing boards keep their stored data when the
  staged Thread role probe needs the extra settings slots
- the staged PAL pump now fires `otPlatAlarmMilliFired()` and processes
  pending tasklets, which is what allowed the role probe to move past
  `otInstanceInitSingle()` and into real Thread role progression
- two-board staged attach is now hardware-proven with the fixed dataset probe:
  starting from the captured full `28`-entry legacy `Preferences` blob on both
  boards, the staged role probe migrates the store to `35` entries in place,
  one board settles as `leader`, and the second settles as `child`
- two-board staged payload validation is now also hardware-proven with
  `OpenThreadUdpStageProbe`: after the same fixed-dataset attach, the `child`
  sends `stage-ping` to the leader RLOC over UDP and the `leader` replies with
  `stage-pong`, with both directions confirmed in the repo-owned logs
- repo-owned evidence for that path now lives at:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/thread_phase3_latest/role_probe_board_a_legacy28_migrated.log`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/thread_phase3_latest/role_probe_board_b_legacy28_migrated.log`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/thread_phase3_latest/udp_stage_probe_board_a.log`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/thread_phase3_latest/udp_stage_probe_board_b.log`

## Phase 4: Arduino Thread API Surface

Goal:

- expose a small Arduino-facing Thread surface without overpromising

Checklist:

- [x] define minimal API for:
  - begin
  - attach / join
  - role / state query
  - dataset set / get
  - UDP send / receive
- [x] add one or two normal examples that are not probe-only
- [x] keep API names honest about supported scope
- [x] document unsupported Thread features explicitly

Validation:

- [x] example compile coverage
- [x] real hardware join/send/receive validation

Exit criteria:

- [x] Thread support is usable from an Arduino sketch without reaching into raw port glue

Current status note:

- the staged Thread seam is no longer hidden-only for CLI experiments: the
  board package now exposes `Tools > Thread Core > Experimental Stage Core
  (Leader/Child/Router + UDP)` for all current boards
- the new Arduino-facing staged wrapper now exists in
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54_thread_experimental.h`
  and
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54_thread_experimental.cpp`
- that wrapper provides the first minimal Arduino-level staged surface for:
  `begin()`, fixed-dataset attach, role/state query, active dataset get/set,
  router promotion request, UDP socket open, UDP send, leader RLOC lookup,
  PSKc generation, and passphrase-derived dataset build
- the first normal staged examples now exist at:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalRoleReporter/ThreadExperimentalRoleReporter.ino`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalUdpHello/ThreadExperimentalUdpHello.ino`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalRouterPromotion/ThreadExperimentalRouterPromotion.ino`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalPskcUdpHello/ThreadExperimentalPskcUdpHello.ino`
- example compile validation now passes against the local `main` checkout with
  `clean_thread=stage`
- real two-board staged API validation now exists through
  `ThreadExperimentalUdpHello`: one board settles as `leader`, the second as
  `child`, the child sends `hello-ping`, and the leader replies with
  `hello-pong`
- repo-owned evidence for that wrapper-level proof now lives at:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/thread_phase4_latest/thread_udp_hello_board_a.log`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/thread_phase4_latest/thread_udp_hello_board_b.log`
- real two-board staged router-promotion validation now also exists through
  `ThreadExperimentalRouterPromotion`: one board settles as `leader`, the
  second attaches as `child`, requests router promotion, becomes `router`, and
  then completes a `router-ping` / `router-pong` UDP exchange with the leader
- repo-owned evidence for that router-level proof now lives at:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/thread_phase4_latest/thread_router_promotion_board_a.log`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/thread_phase4_latest/thread_router_promotion_board_b.log`
- real two-board staged passphrase-derived dataset validation now also exists
  through `ThreadExperimentalPskcUdpHello`: both boards derive the same PSKc
  from the OpenThread PBKDF2-CMAC path, attach on the derived dataset, and
  complete a `pskc-ping` / `pskc-pong` UDP exchange
- repo-owned evidence for that passphrase-derived proof now lives at:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/thread_phase4_latest/thread_pskc_udp_hello_board_a.log`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/thread_phase4_latest/thread_pskc_udp_hello_board_b.log`
- supported scope is intentionally narrow and explicitly staged-only:
  fixed-dataset attach, passphrase-derived dataset attach, router promotion,
  role observation, and UDP payload exchange on the internal Thread interface
- unsupported follow-up work remains explicit:
  joiner/commissioner, border-router/reference-network attach, secure
  commissioning, and any Matter runtime above this staged Thread surface

## Phase 5: Matter Foundation

Goal:

- make `Matter` technically possible on top of a real Thread path

Checklist:

- [x] add third-party `connectedhomeip` intake path
- [x] define minimal platform/adaptation layer boundaries
- [x] map entropy / crypto / storage / time / event-loop ownership
- [x] decide whether first commissioning target is:
  - on-network only
  - BLE rendezvous plus Thread
- [x] define exact first device type
- [x] add bounded manual-code and QR-code onboarding helpers for the first
  on-network commissioning path

Validation:

- [x] compile-only `Matter` platform target
- [x] no unresolved dependency on missing Thread behavior

Exit criteria:

- [x] a real first-device `Matter` path is mechanically possible in-tree

Current status note:

- the first Matter foundation slice now exists without claiming a working CHIP
  runtime
- the repo now has a real upstream intake path for future Matter work:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/scripts/import_connectedhomeip_scaffold.sh`
- the reserved staged upstream path is now:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip`
- a minimal upstream CHIP header/support/error/key/data-model seed is now also staged
  there
  from
  commit:
  `337f8f54b4f0813681664e5b179dc3e16fdd14a0`
- the Arduino board package now exposes
  `Tools > Matter Foundation > Experimental Compile Target (On-Network On/Off Light)`
  while still defaulting to `Disabled`
- Matter runtime ownership and adaptation boundaries are now frozen both in
  docs and in a repo-owned public header:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/docs/MATTER_RUNTIME_OWNERSHIP.md`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/docs/MATTER_FOUNDATION_MANIFEST.md`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_platform_nrf54l15.h`
- the repo now also has a compile-only first-device foundation target in:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_foundation_target.h`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_foundation_target.cpp`
- the first honest commissioning target is now frozen as `on-network-only`
  over `Thread`, not BLE rendezvous
- the first device type is now frozen as an `on-off-light`, with the future
  upstream seed expected to come from `connectedhomeip/examples/lighting-app`
- a repo-owned `MatterFoundationProbe` example now exists to report the
  current build/ownership state without pretending that a real CHIP target is
  already linked
- the current probe path is:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterFoundationProbe/MatterFoundationProbe.ino`
- repo-owned proof logs now live at:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/matter_phase5_latest/matter_foundation_probe_default.log`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/matter_phase5_latest/matter_foundation_probe_staged.log`
- with the hidden seam enabled, that probe now compiles against real staged
  upstream CHIP headers and reports upstream values from
  `CHIPVendorIdentifiers.hpp` and `NodeId.h`
- the hidden seam now also links one real staged upstream support
  implementation unit (`src/lib/support/Base64.cpp`), and the probe prints the
  base64 form of the staged group node value to prove the linked `.cpp` path
- the hidden seam now also links staged upstream core error formatting
  (`src/lib/core/CHIPError.cpp` / `src/lib/core/ErrorStr.cpp`) through a
  repo-owned minimal `CHIPConfig.h` shim, and the probe prints the formatted
  string for `CHIP_ERROR_INVALID_ARGUMENT`
- the hidden seam now also links staged upstream key-id logic
  (`src/lib/core/CHIPKeyIds.cpp`) through a repo-owned minimal
  `CodeUtils.h` shim, and the probe prints live results for
  rotating/static key derivation, key description, validation, and same-group
  checks
- the hidden seam now also links staged upstream `Base85` support, and the
  probe now prints the base85 form of the staged group node value plus a live
  decode round-trip check
- the hidden seam now also links staged upstream `TimeUtils` support
  (`src/lib/support/TimeUtils.cpp`) through a repo-owned minimal
  `CHIPCore.h` shim, and the probe now prints live CHIP-epoch, unix,
  and calendar conversion checks plus a date-adjustment proof
- the hidden seam now also links staged upstream `BytesToHex` support
  (`src/lib/support/BytesToHex.cpp`) through repo-owned minimal
  `CHIPEncoding.h` and `CHIPLogging.h` shims, and the probe now prints live
  uppercase hex encode/decode plus integer round-trip checks
- the hidden seam now also links staged upstream `ThreadOperationalDataset`
  support (`src/lib/support/ThreadOperationalDataset.cpp`) through the
  existing minimal `CHIPCore.h`, `CHIPEncoding.h`, and `CodeUtils.h` shims,
  and the probe now prints live dataset build, validation, commissioned-state,
  field readback, and copy round-trip checks
- the repo now also has a bounded Matter onboarding-code helper in
  `src/matter_manual_pairing.h` / `src/matter_manual_pairing.cpp`; it
  generates short and long decimal manual codes with Verhoeff check digits,
  generates basic QR setup payload strings with Matter Base38 packing, and
  `MatterFoundationProbe` exercises deterministic upstream-style vectors
  without importing the large setup-payload parser / optional QR TLV stack yet
- the compile-only foundation target now defines a root-node endpoint plus the
  first on/off-light endpoint with explicit cluster metadata, and it freezes an
  explicit Thread dependency contract so this Matter slice only depends on:
  dataset set/get, passphrase-derived dataset build, role observation,
  cooperative loop pumping, `Thread` transport, and `Preferences` storage
- that same target now exports an `otOperationalDataset` into staged
  `chip::Thread::OperationalDataset` TLV form, which is the first in-tree
  mechanical bridge from the staged Arduino Thread wrapper into Matter-facing
  onboarding data
- the new compile proof sketch now exists at:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnOffLightFoundationCompileTarget/MatterOnOffLightFoundationCompileTarget.ino`
- compile validation now passes locally for:
  - `MatterFoundationProbe` default build
  - `MatterFoundationProbe` with `clean_thread=stage,clean_matter=stage`
  - `MatterOnOffLightFoundationCompileTarget` with `clean_thread=stage,clean_matter=stage`
  - `ThreadExperimentalPskcUdpHello` with `clean_thread=stage,clean_matter=stage`
- Phase 5 now ends at an honest boundary:
  the repo has a compile-only first-device Matter path in-tree, but it still
  does not claim commissioning, secure sessions, or a user-facing Matter API

## Phase 6: Matter Commissioning And First Device

Goal:

- ship one real end-to-end `Matter` device type

Checklist:

- [ ] implement first commissioning flow
- [x] implement first device type state model
- [x] expose a small Arduino-facing API for that device type
- [ ] validate with a real commissioner
- [ ] validate with `Home Assistant`

Validation:

- [ ] commission on real hardware
- [ ] discover/control from `Home Assistant`
- [ ] reconnect / reboot behavior checked

Exit criteria:

- [ ] the repo has one honest working `Matter` device example on real hardware

Current status note:

- the first Phase 6 slices are now in-tree without claiming a commissioned
  Matter runtime yet
- the repo now has a repo-owned on/off-light device model in:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_onoff_light.h`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_onoff_light.cpp`
- that device model now covers the first bounded state surface for an on/off
  light:
  persisted on/off state, persisted start-up behavior, identify timer state,
  on/off callbacks, identify callbacks, and read helpers for the first
  on/off/identify attributes
- the first small Arduino-facing example for that device surface now exists at:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnOffLightApiDemo/MatterOnOffLightApiDemo.ino`
- that example drives the onboard LED from the Matter on/off state, uses the
  onboard button for toggle / start-up-behavior cycling, and uses the identify
  timer to blink the indicator without claiming commissioner connectivity
- the next repo-owned Phase 6 bootstrap slice now also exists in:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_onnetwork_onoff_light.h`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_onnetwork_onoff_light.cpp`
- that bootstrap layer now binds together the existing on/off-light state
  model, the staged Thread wrapper, the onboarding-code helper, and the first
  persisted setup identity into one sketch-facing node object for the bounded
  `on-network-only` path
- the first bootstrap example for that slice now exists at:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightNodeDemo/MatterOnNetworkOnOffLightNodeDemo.ino`
- that example now builds a Thread dataset from passphrase inputs, starts the
  internal staged Thread path, prints QR/manual onboarding codes, reports when
  the node is mechanically ready for on-network commissioning, and still keeps
  the onboard LED/button bound to the first on/off-light device state
- the next Phase 6 endpoint slice now also exists in:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_onoff_light_endpoint.h`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_onoff_light_endpoint.cpp`
- that endpoint layer now exposes the first repo-owned endpoint/cluster seam
  for the on/off-light path: endpoint-checked attribute reads for `OnOff`,
  `GlobalSceneControl`, and `IdentifyTime`, plus bounded command dispatch for
  `Off`, `On`, `Toggle`, and `Identify`
- the node object now owns that endpoint seam directly, so sketches can stay at
  the node level and still exercise future-command-shaped flows without
  pretending real CHIP exchange is present yet
- the same node object now also owns the first repo-backed commissioning-flow
  helpers for this staged path:
  bounded commissioning-window state (`closed`, `pending-readiness`, `open`,
  `expired`), a full commissioning bundle builder, OpenThread dataset TLV
  export, and staged CHIP Thread dataset hex export for the future controller
  handoff path
- the first command-surface demo for that slice now exists at:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightCommandSurfaceDemo/MatterOnNetworkOnOffLightCommandSurfaceDemo.ino`
- that example starts the same staged on-network node, then exposes a small
  serial console for `state`, `on`, `off`, `toggle`, `identify <seconds>`,
  `stop-identify`, `open-window <seconds>`, `close-window`, `bundle`,
  `manual`, and `qr`, all routed through the new endpoint command/attribute API
  and the new commissioning bundle/window API
- compile proof for this slice now lives at:
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/matter_phase6_latest/matter_onoff_api_demo_stage.compile.log`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/matter_phase6_latest/matter_onnetwork_onoff_light_command_surface_demo_stage.compile.log`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/matter_phase6_latest/matter_onnetwork_onoff_light_node_demo_stage.compile.log`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/matter_phase6_latest/matter_onoff_foundation_target_stage.compile.log`
  `/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/measurements/matter_phase6_latest/matter_foundation_probe_stage.compile.log`
- local compile validation now passes for:
  - `MatterOnNetworkOnOffLightCommandSurfaceDemo` with `clean_thread=stage,clean_matter=stage`
  - `MatterOnNetworkOnOffLightNodeDemo` with `clean_thread=stage,clean_matter=stage`
  - `MatterOnOffLightApiDemo` with `clean_matter=stage`
  - `MatterOnOffLightFoundationCompileTarget` with `clean_thread=stage,clean_matter=stage`
  - `MatterFoundationProbe` with `clean_thread=stage,clean_matter=stage`
- what still remains for Phase 6 is the real commissioner path, actual network
  control/discovery, and reboot/reconnect validation against a real controller

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

- [x] compile
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
