# Post-0.5.0 Implementation Plan

This document is the concrete follow-on plan after the `0.5.0` release.

Master completion checklist:

- `docs/BLE_CS_COMPLETION_CHECKLIST.md`

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

Current checkpoint inside Phase 2:

- the generic VPR service now has its first BLE-controller-facing skeleton
  slices through `VprBleLegacyAdvertisingProbe` and
  `VprBleConnectionStateProbe`
- those slices own legacy non-connectable advertising scheduler state,
  retained legacy advertising payload storage/readback, single-link connected
  session state, CPUAPP-readable shared link-state reporting, and async event
  publication on VPR
- one real CPUAPP-side CS workflow can now import that VPR-owned connected
  handle into the dedicated CS image through
  `VprBleConnectionCsHandoffProbe`, run one nominal synthetic CS procedure,
  and produce a valid completed estimate on the imported link
- the generic VPR service now also owns one nominal CS workflow runtime and
  completion summary on the current encrypted live BLE link through
  `VprBleConnectionCsProcedureProbe`
- that completed generic-service summary now includes controller-owned result
  layout fields beyond `distanceQ4`: local/peer subevent counts, local/peer
  step counts, local/peer mode1/mode2 counts, one packed demo-channel window,
  and distinct local/peer completed-result hashes
- the generic VPR service now also returns controller-produced completed
  local/peer CS result payload bytes through the same host boundary, and the
  normal `BleChannelSoundingVprServiceNominal` example now parses and validates
  those returned payloads instead of only trusting the reduced summary fields
- that same generic-service runtime is now reusable outside the probes through
  `beginFreshBleConnectedCsWorkflow(...)`,
  `disconnectBleConnectionAndWait(...)`, and
  `runFreshBleConnectedCsWorkflow(...)`
- there is now also a normal library example for that in-place path:
  `BleChannelSoundingVprServiceNominal`
- the imported-link CS workflow startup is now host-boundary code instead of
  probe-local sequencing:
  `beginFreshWorkflowFromBleConnection(...)`,
  `directStartConfiguredWorkflow(...)`, and
  `pollUntilCompletedProcedureResult(...)`
- the actual BLE radio launch path is still CPUAPP-owned, so the next Phase 2
  step should now be physical calibration / power work and then less synthetic
  result shaping, not more boundary packaging
- the repo now also has a checked-in real-hardware latency note for the current
  BLE -> CS paths:
  - `docs/ble-cs-latency-characterization.md`
  - generic in-place nominal path currently characterizes at
    `~22/2/4/28 ms` for `begin/complete/disconnect/total`
  - two-board imported-link dedicated path currently characterizes at
    `~16/2/9/15/0/55 ms` for
    `source_boot/source_connect/handoff_boot/start/complete/total`
- calibration/error-model documentation is now explicit instead of being only
  implied by serial field names:
  - `docs/channel-sounding-calibration.md`
  - `docs/channel-sounding-error-model.md`

## Phase 3: Full Channel Sounding Completion

Goal:

- move channel sounding from bring-up status to a real controller-backed feature

Deliverables:

- real connected CS procedure flow on top of the BLE/VPR controller path
- controller-owned capability/config/security/procedure handling
- physically defensible CS result delivery instead of only the current nominal
  synthetic payloads
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

## Immediate Saved TODO

The next implementation slice after the current VPR/CS demo work is:

1. first real connected BLE-controller ownership on VPR
2. then controller-backed CS on top of that connected path

More concretely, the next saved todo is to replace part of the current
dedicated-image CS demo responder with a real connected BLE/VPR controller
service slice:

- VPR owns one real connection-scoped controller state machine
- CPUAPP hosts the boundary and link bookkeeping only
- CS procedure enable/disable and result staging are bound to that real link
- existing demo commands remain as regressions until the connected path is
  proven

Current checkpoint on that todo:

- one real handle-scoped CS session now exists on the dedicated VPR image
- `Remove Config` closes that session again cleanly
- the control-plane regressions are green:
  - `hcivprtracedemo`
  - `hcivprstatedemo`
  - `hcivprmultidemo`
  - `hcivprlinkdemo`

Immediate next follow-up from this checkpoint:

- keep replacing synthetic built-in result publication with more actual
  controller-owned behavior on VPR
- the earlier built-in `~1.51 m` regression is now fixed
  - root cause was a near-full dedicated VPR image window with no explicit
    stack reserve
  - the dedicated image now has a larger window and explicit stack headroom,
    and the demo estimate is back at `~0.75 m`
- the host/session diagnostics now hold a stable completed local/peer pair
  instead of exposing whichever in-flight procedure most recently updated the
  reassemblers
- the dedicated image now also rotates its four-channel demo window per
  procedure from the configured channel map instead of replaying one static set
- the dedicated image now drops its active procedure-enabled flag when the
  configured run finishes, so VPR session state no longer stays falsely
  "running" after the last staged procedure
- the dedicated image now also spaces later procedures with a VPR-owned
  heartbeat interval derived from the configured procedure interval instead of
  publishing the whole run back-to-back
- the dedicated image now varies its staged mode-2 step count from the
  configured `min/max main-mode steps` instead of always emitting a fixed
  four-step synthetic payload
- the dedicated image now varies its reported mode-2 antenna permutation index
  from `toneAntennaConfigSelection` instead of hard-coding one permutation for
  every staged tone
- the dedicated image now has a validated mid-run disable path, so a raw
  `Procedure Enable(enable=0)` can halt a configured run after the first
  completed procedure instead of always letting the demo run to exhaustion
- the dedicated image now varies its synthetic mode-2 quality nibble across
  staged steps instead of reporting fixed `high` quality for every tone
- the dedicated image now varies synthetic local/peer mode-2 PCT amplitudes
  per procedure/step while keeping the nominal phase-slope distance stable
- the dedicated image now varies synthetic local/peer mode-2 PCT phase
  orientation per step while keeping the nominal phase-slope distance stable
- the dedicated image now decides CS result continuation chunking from actual
  packet budget instead of hard-coding one half-and-half split for every
  procedure
- the dedicated image now drives synthetic CS result header fields from
  controller-side state too, including ACL event counter, frequency
  compensation, reference power, and reported antenna-path count
- the dedicated image now inserts a small VPR-owned delay between the last
  local result packet and the peer-result side of the same procedure instead
  of publishing both sides back-to-back in one fixed drain pattern
- the dedicated image now owns part of the within-procedure layout too:
  - procedures with more than three phase steps start with one invalid mode-1
    timing step before the mode-2 tone steps
  - this changes the result layout without perturbing the current `~0.75 m`
    phase-distance regression because the synthetic RTT step is explicitly
    unavailable
  - continuation splitting is now derived from actual encoded step sizes, so
    the mixed mode-1/mode-2 layout still chunks correctly on the dedicated
    image
  - that mode-1 insertion is now tied to `createConfig.rttType`, so the
    dedicated image can also publish a pure mode-2 layout when RTT is turned
    off and still keep the nominal `~0.75 m` estimate stable
- the dedicated image now derives next-procedure spacing from the configured
  `min/max procedure interval` range instead of always using the minimum, and
  the VPR-owned interval selector is now exposed through shared state so the
  host regression can prove the policy moved across the range
- the dedicated image now owns repeated CS result continuation publication as
  well:
  - local and peer result publication keep explicit VPR-side chunk cursors
    instead of assuming one initial packet and at most one continuation
  - `hcivprcontinuedemo` is the single-procedure regression that proves the
    multi-continuation path on the live two-board setup
  - the current proof point is `3` local result packets and `3` peer result
    packets for one RTT-enabled six-step procedure while the estimate stays at
    `~0.75 m`
- the dedicated image now also derives that continuation budget from
  configured subevent policy:
  - tighter `minSubeventLen` values force more continuation packets for the
    same encoded six-step procedure
  - `hcivprsubeventdemo` proves the tight-budget path with
    `minSubeventLen = maxSubeventLen = 0x000100`, landing at `6` local result
    packets and `6` peer result packets while still estimating `~0.75 m`
- the dedicated image now also spaces repeated continuation packets from
  VPR-owned heartbeat timing derived from `minSubeventLen`, so the local and
  peer packet trains are no longer drained back-to-back within the same
  controller step
- the host/session layer now aggregates multiple complete subevents with the
  same procedure counter into one completed procedure result pair, so the
  procedure-level estimate waits for the final complete subevent instead of
  triggering on the first one
- the dedicated image can now split a larger RTT-enabled synthetic procedure
  across multiple complete subevents instead of only shaping one fixed
  subevent plus continuations
- that larger-procedure subevent count is now also derived from configured
  subevent policy:
  - the default seven-step RTT-enabled path lands at `2` complete subevents
  - the tighter `minSubeventLen = maxSubeventLen = 0x000100` path lands at
    `3` complete subevents for the same seven-step procedure while the host
    still reassembles one completed procedure estimate
- the host/session layer now also tolerates later out-of-band CS control
  replies once the workflow is already `ready`, and workflow-driven
  controller-shape changes now flush in-flight result assembly before the next
  run starts
- the direct VPR controller helper now also:
  - preserves non-response H4 controller packets encountered while waiting for
    a direct reply
  - retries command writes until the shared transport has a free slot while
    VPR is still publishing CS result packets
  - resets in-flight host procedure assembly before direct run-shape commands
    (`Create Config`, `Remove Config`, `Set Procedure Parameters`,
    `Procedure Enable`) so out-of-band control behaves like the workflow-driven
    command path
  - feeds ready-phase direct control events back into workflow shadow state
    instead of only tolerating them
- the dedicated CS image now publishes `Config Complete(action=remove)` on
  direct `Remove Config`, which closes the gap between the controller-facing
  transport and the host-side workflow shadow
- the current live proofs for that controller-lifecycle cleanup are now:
  - `hcivprtransportdemo`
  - `hcivprsubcountdemo`
  - `hcivprabortdemo`
  - `hcivprmanualdemo`
  - `hcivprreconfigdemo`
  - `hcivprcfgswapdemo`
  - `hcivprmulticfgdemo`
  - `hcivprrmstoredemo`
- `hcivprreconfigdemo` now proves direct out-of-band
  `Set Procedure Parameters` reconfiguration on one live VPR session by
  changing the same seven-step procedure from `2` complete subevents per side
  to `3` complete subevents per side without rebooting the transport
- `hcivprcfgswapdemo` now proves a direct full-session rebuild on one live VPR
  session:
  - remove the current config
  - reopen the VPR CS session with direct `Read Remote Supported Capabilities`
    and `Set Default Settings`
  - create a new config with a new `configId`
  - re-enable security, apply parameters, and run it to completion
- `hcivprmulticfgdemo` now proves stored config run switching on one live VPR
  session:
  - run a direct alternate `configId=2`
  - then run stored `configId=1` again with `Procedure Enable(configId=1)` only
  - then run stored `configId=2` again with `Procedure Enable(configId=2)` only
  - those follow-on runs no longer need config recreate, security re-enable, or
    another procedure-parameter write
- `hcivprrmstoredemo` now proves inactive stored-config removal on one live
  VPR session:
  - run alternate `configId=2`
  - switch back to stored base `configId=1`
  - remove inactive `configId=2`
  - rerun stored base `configId=1`
  - verify direct `Procedure Enable(configId=2)` is rejected with `0x12`
- `hcivprinventorydemo` now proves controller-owned config inventory reporting
  on one live VPR session:
  - base ready state reports `1`
  - direct create of alternate `configId=2` reports `2`
  - inactive remove of `configId=2` reports `1`
  - remove of the last active base config reports `0` and closes the session
- the supporting controller/VPR fixes for that slice are:
  - ready-phase host workflow shadow no longer clears live session/config
    ownership when `Config Complete(action=remove)` targets an inactive config
  - direct `Remove Config` now resets procedure-run assembly after its direct
    response/drain succeeds, which avoids stale trailing result packets from the
    previous run poisoning the direct control path
  - the dedicated CS image now removes all stored copies of a removed
    `configId`, including the previous-slot fallback, so removed configs stop
    being runnable through stored-slot selection
- the VPR shared-state seam now also exports stored-config count explicitly
  through the host wrapper, so controller-side inventory is no longer inferred
  only from a stream of complete events
- the same shared-state seam now also exports controller-owned config-slot
  metadata:
  - active `configId`
  - slot0 / slot1 / previous-slot `configId`
  - active primary slot index
  - free primary slot count
  - slot occupancy flags
- the same shared-state seam now also exports controller-owned runnable
  metadata:
  - selected-config runnable flag
  - slot0 / slot1 / previous-slot runnable flags
- the same shared-state seam now also exports controller-owned slot readiness
  metadata:
  - selected / slot0 / slot1 / previous-slot security-enabled flags
  - selected / slot0 / slot1 / previous-slot
    procedure-parameters-applied flags
- `hcivprslotdemo` now proves those slot transitions on one live VPR session:
  - base ready state: `slot0=1 slot1=0 previous=0`
  - alternate create: `slot0=1 slot1=2 previous=1`
  - direct stored-base rerun flips activity back to slot0 with
    `previous=2`
- `hcivprselectdemo` now proves stored-config selection on VPR without a run:
  - initial ready state reports stored base `configId=1` as selected and
    runnable
  - direct create of alternate `configId=2` selects it immediately but leaves
    it not runnable until security and procedure parameters are applied while
    the stored base config remains ready in slot metadata
  - direct `Security Enable` on selected `configId=2` now shows the selected
    config becoming security-enabled before it becomes runnable
  - direct `Set Procedure Parameters(configId=2)` after security flips the
    selected-config runnable flag high without needing `Procedure Enable`, and
    the VPR state now reports `security=1` and
    `procedureParamsApplied=1` for the selected config and its stored slot
  - direct `Set Procedure Parameters(configId=1)` selects stored base config
    again and returns active ownership to slot0 while both stored primary slots
    remain runnable and ready
  - direct `Set Procedure Parameters(configId=2)` selects stored alternate
    config again and returns active ownership to slot1 with runnable state
    preserved
- `hcivprrmactivedemo` now proves active-config removal promotes a remaining
  stored config on VPR instead of dropping selected-config ownership to zero:
  - armed alternate `configId=2` is kept stored+runnable
  - direct selection of base `configId=1` moves active ownership back to base
  - direct `Remove Config(configId=1)` promotes stored alternate `configId=2`
    back to selected+runnable with stored-count `2 -> 1`
  - direct `Procedure Enable(configId=2)` still runs immediately after that
    promotion, while direct `Procedure Enable(configId=1)` is rejected with
    `0x12`
- the host ready-phase workflow shadow is now reconciled against controller-
  owned VPR state for these direct-control paths:
  - `procedureEnabled` now drops back to `false` when VPR reports the run has
    stopped, instead of sticking high in the host shadow after direct runs
  - active-remove promotion now restores a consistent shadow state
    (`RDCSP-`) after VPR reselects the remaining stored config
  - inactive-remove event semantics are still preserved; the host keeps the
    `Config Complete(action=remove)` view for the removed inactive config
    instead of overwriting that event during reconciliation
- the host shared-transport write path now invalidates CPU cache before
  checking the shared pending flags, which fixed a real stale-cache direct
  command failure on later `Remove Config` traffic
- the current remaining direct-control gap is no longer basic manual
  start/abort/restart, direct parameter reconfiguration, or basic inventory
  reporting. The next slice is
  richer controller ownership on VPR above that transport/control seam.
- that seam is now stronger than simple count/slot reporting:
  - VPR now owns controller-side stored-config count, slot metadata,
    runnable/readiness flags, active-remove promotion, and a real
    `2 primary + 1 overflow(previous)` third-config policy
  - `hcivprthirdcfgdemo` now proves direct create/select/run of stored
    `configId=3` on one live VPR session without recreating `configId=1/2`
  - `hcivprevictdemo` now proves direct fourth-config overflow eviction on one
    live VPR session, with controller-owned `lastEvictedConfigId=3` reporting
    and immediate `0x12` rejection for the evicted stored config
  - `hcivprpromotedemo` now proves direct selection-time promotion of stored
    overflow configs into a primary slot, with displaced primary ownership
    demoted back into `previous` before the run starts
  - `hcivprmulticfgdemo` still stays green after that overflow-slot slice

That retained-authority slice is now in:

- VPR reports explicit `selected > fallback1 > fallback2` retained-config
  authority IDs through the shared-state seam
- `hcivprselectdemo` now validates that controller-owned authority order on the
  normal initiator path
- the hard validation path now uses a retained `.noinit` summary build, so this
  checkpoint no longer depends on live CDC capture timing

The next cleanup slice after that is also in:

- the retained-config direct-control demos now route through reusable
  `BleCsControllerVprHost` direct helpers instead of rebuilding raw HCI command
  packets in the sketch
- that helper surface now covers:
  `Read Remote Supported Capabilities`, `Set Default Settings`,
  `Create Config`, `Remove Config`, `Security Enable`,
  `Set Procedure Parameters`, and `Procedure Enable`
- the retained-config initiator demos now share that same boundary instead of
  each carrying their own `parseDirectStatus` / `sendDirectCommand` plumbing
- the remaining raw direct transport usage in the initiator example is now
  intentionally limited to lower-level diagnostics like the direct abort,
  link-boundary, and trace demos
- repo-local compile proof for this checkpoint is:
  `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_boundary_compile`

The next cleanup slice after that is also in:

- retained-config state-heavy demos now use helper methods on
  `BleCsControllerVprHostState` instead of duplicating raw `vprState()` field
  comparisons in local sketch lambdas
- that helper surface now covers:
  slot matching, runnable matching, readiness matching, and retained-authority
  matching/packing
- demos like `hcivprslotdemo`, `hcivprselectdemo`, `hcivprthirdcfgdemo`, and
  `hcivprevictdemo` now rely on those host-state helpers instead of each
  rederiving the same policy view
- repo-local compile proof for this checkpoint is:
  `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_stateview_compile`

The next cleanup slice after that is also in:

- retained-config wait semantics now live in `BleCsControllerVprHost` instead
  of repeated sketch-local polling loops
- that host wait surface now covers:
  stopped-on-config, stopped-after-procedure-count, selected-state,
  retained-slot state, retained full state, retained selection state, and
  settled direct idle
- the retained-config initiator demos now use those host waiters instead of
  each carrying their own `while (!failed()) { poll(); ... }` control loops
- repo-local compile proof for this checkpoint is:
  `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_waiters_compile`

The next cleanup slice after that is also in:

- retained-config expectation packing now lives in the reusable VPR host
  boundary instead of giant positional argument lists in the sketch
- `ble_channel_sounding.h` now defines typed retained-config expectation
  structs for selected-state, retained-selection, retained-slot,
  retained-runnability, retained-readiness, and retained full-state checks
- `BleCsControllerVprHostState` and `BleCsControllerVprHost` now accept those
  typed expectations directly, so the host API no longer needs twenty-field
  retained-state polling signatures at the sketch call site
- `hcivprselectdemo`, `hcivprthirdcfgdemo`, and `hcivprevictdemo` now use
  those typed expectations instead of open-coding retained-state wait
  contracts in local lambdas
- repo-local compile proof for this checkpoint is:
  `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_expectations_compile`

The next cleanup slice after that is also in:

- the retained-config direct-control sketch surface is now thinner too
- `BleChannelSoundingInitiator.ino` no longer carries the extra top-level
  `sendVprDirect*` shim layer that only forwarded into
  `BleCsControllerVprHost::direct*`
- retained-config demos now call the reusable host direct-control surface
  directly, which removes another sketch-local boundary layer without changing
  controller behavior
- repo-local compile proof for this checkpoint is:
  `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_directsurface_compile`

The next cleanup slice after that is also in:

- generic direct lifecycle waits now also live on the reusable VPR host
  boundary instead of being open-coded in individual non-retained demos
- `BleCsControllerVprHost` now owns reusable helpers for:
  current-config direct enable, running-with-procedure-count, stopped, and
  run-complete-by-subevent-count
- `hcivprmanualdemo` and `hcivprreconfigdemo` now use those host lifecycle
  waits instead of sketch-local `while (!failed()) { poll(); ... }` loops
- that moves another piece of generic controller run-control semantics out of
  CPUAPP sketch code and into the reusable VPR host boundary
- repo-local compile proof for this checkpoint is:
  `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_lifecyclewait_compile`

The next cleanup slice after that is also in:

- reusable host bring-up now also lives on the VPR host boundary
- `BleCsControllerVprHost::beginFreshHost(...)` now owns the common
  `reset transport -> load default image -> boot -> begin host -> pump until
  ready` sequence
- most VPR demos in `BleChannelSoundingInitiator.ino` now use that helper
  instead of each carrying their own startup-loop boilerplate
- the remaining manual startup paths are the intentionally special ones that
  still need extra transport diagnostics around bring-up
- repo-local compile proof for this checkpoint is:
  `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_starthost_compile`

The next slice after this checkpoint is broader than retained-config policy:

- start moving from the strong CS-specific VPR path toward a more general
  BLE-controller-facing VPR service boundary
- keep reducing sketch/host-side inference so CPUAPP acts more like a boundary
  layer and less like the place that still owns controller policy
