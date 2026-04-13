# Channel Sounding VPR Continuation

This note is the resume point for the current Channel Sounding and VPR transport work.

## Current State

The clean core now has a real VPR-backed transport path for controller-style Channel Sounding bring-up.

The latest local slice moved the dedicated CS image past a stateless demo
responder:

- one real connection-scoped CS link session now lives on VPR
- the session binds on the first successful
  `Read Remote Supported Capabilities`
- later CS commands must use the same connection handle
- `Remove Config` now tears the active link session down fully
- the dedicated image now explicitly reinitializes its CS state on boot instead
  of relying on static-image data staying sane across reloads
- the reserved dedicated CS image window is now `0x2400` bytes at
  `0x2003DA00 - 0x2003FE00`
- the dedicated CS linker script now reserves an explicit stack inside that
  window instead of leaving the runtime to collide with code/rodata when the
  image grows
  - current reserved stack size is `0x280`

The same transport is also now proven beyond CS through the built-in generic
controller-service path:

- `VprSharedTransportProbe`
- `VprFnv1aOffloadProbe`
- `VprCrc32OffloadProbe`
- `VprCrc32cOffloadProbe`
- `VprTickerOffloadProbe`
- `VprTickerAsyncEventProbe`
- `VprHibernateContextProbe`
- `VprHibernateWakeProbe`
- `VprHibernateResumeProbe`
- `VprRestartLifecycleProbe`

Current validated generic service state on hardware:

- `svc=1.7`
- `opmask=0x3FF`
- `max_in=124`
- cold-boot command path is good
- autonomous ticker state progresses on VPR after the configure command returns
- queued unsolicited ticker/vendor events are validated through
  `VprTickerAsyncEventProbe`
- VPR hibernate now writes a nonzero saved-context image into the documented
  `0x2003FE00` / `512 B` window when the required MEMCONF retention bits are enabled
- loaded-image restart is now validated on both attached boards through
  `VprRestartLifecycleProbe`
  - the probe now auto-runs into a `.noinit` summary so the result can be read
    over SWD even when serial is silent
  - both attached boards completed `5/5` restart cycles and ended back in `READY`
- reset-after-hibernate is now characterized more honestly:
  - the host-side generic VPR CSR MMIO assumption was wrong; unaligned FLPR CSR
    numbers like `VPRNORDICSLEEPCTRL=0x7C1` are not safe CPUAPP MMIO offsets
  - the shared-transport restart helper now forces `BOOTING` before restart and
    requires a changed heartbeat before it treats the transport as alive again
  - the VPR stub now enables `MSTATUS.MIE` before `wfi`; Zephyr carries the
    same Nordic workaround because VPR wake can fail if sleep is entered with
    interrupts disabled
  - `VprHibernateResumeProbe` now passes on both attached boards
  - the cross-board-stable design is a deterministic reset-after-hibernate
    service restart that preserves retained host-side service state and sets
    the explicit "restored from hibernate" flag
  - the service restart path now intentionally disables raw VPR hardware
    context restore before reboot, because that hardware path was the unstable
    part across the attached boards
  - that means the reusable service lifecycle is now stable across both boards,
    but true raw VPR CPU-context resume should still be treated as an
    investigation topic rather than a finished generic lifecycle feature
- the CS controller path is now split onto a dedicated VPR image instead of
  sharing the same firmware image as the generic controller-service probes
  - generic service image:
    - `src/vpr_cs_transport_stub_firmware.h`
  - dedicated CS image:
    - `src/vpr_cs_controller_stub_firmware.h`
  - wrapper source:
    - `tools/vpr/vpr_cs_controller_stub.c`
  - generator:
    - `tools/generate_vpr_cs_controller_stub.py`
  - `BleCsControllerVprHost::loadDefaultTransportImage()` now boots the
    dedicated CS image

Implemented locally in the main repo:

- shared-memory VPR transport and boot helpers:
  - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_vpr.h`
  - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_vpr.cpp`
  - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_vpr_transport_shared.h`
- linker reservations for CPUAPP <-> VPR transport windows:
  - `hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/nrf54l15_linker_script.ld`
- controller-facing CS helpers:
  - HCI CS command builders
  - HCI event parsers
  - HCI subevent result reassembly
  - RTT step decode from controller-style packets
  - workflow / session / host / H4 / stream host layers
  - VPR-backed host wrapper
  - files:
    - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/ble_channel_sounding.h`
    - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/ble_channel_sounding.cpp`
- VPR stub firmware source and generated image:
  - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/tools/vpr/vpr_cs_transport_stub.c`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/vpr_cs_transport_stub_firmware.h`
  - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/vpr_cs_controller_stub_firmware.h`
  - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/tools/vpr/vpr_cs_controller_stub.c`
  - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/tools/generate_vpr_cs_controller_stub.py`

## Validated Result

The important milestone already passed:

- the VPR-backed CS transport boots
- the VPR-backed host wrapper drives the CS workflow
- the VPR stub has built-in fallback responders for the supported CS opcode set
- the initiator no longer needs to preload script responses from the sketch for the working demo path
- the dedicated CS image now owns one real handle-scoped controller session
  instead of only a globally permissive demo state

Working demo command on the initiator sketch:

- `hcivprtransportdemo`

Validated logs:

- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_final_run_initiator.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_final_run_reflector.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_builtin_run_initiator.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_builtin_run_reflector.log`
- `/home/lolren/Desktop/Nrf54L15/.build/vpr_hibernate_context_probe_ttyACM0.log`
- `/home/lolren/Desktop/Nrf54L15/.build/vpr_hibernate_context_probe_ttyACM1.log`
- `/home/lolren/Desktop/Nrf54L15/.build/vpr_restart_probe_live/board1_swd.log`
- `/home/lolren/Desktop/Nrf54L15/.build/vpr_restart_probe_live/board2_swd.log`
- `/home/lolren/Desktop/Nrf54L15/.build/vpr_resume_autorun/board1_swd_fix8.log`
- `/home/lolren/Desktop/Nrf54L15/.build/vpr_resume_autorun/board2_swd_fix8.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_dedicated_runtime/init.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_repo_final_runtime/init.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_connscoped_final/hcivprtracedemo.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_connscoped_final/hcivprtransportdemo.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_connscoped_final/hcivprstatedemo.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_connscoped_final/hcivprmultidemo.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_connscoped_final/hcivprlinkdemo.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_amplitude_runtime/hcivprtransportdemo.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_amplitude_runtime/hcivprdumpdemo.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_amplitude_runtime/hcivprmultidemo.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_phase_runtime/hcivprtransportdemo.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_phase_runtime/hcivprdumpdemo.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_phase_runtime/hcivprmultidemo.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_chunk_runtime5/hcivprchunkdemo.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_chunk_runtime5/hcivprmultidemo.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_header_runtime3/hcivprdumpdemo.extracted.log`
- `/home/lolren/Desktop/Nrf54L15/.build/cs_vpr_peerstage_runtime/hcivprmultidemo.log`

The key proof lines from the current built-in responder path are:

- `hcivprtracedemo ok=1 remote=0x0 create=0x0 security=0x0 setproc=0x0 procen=0x0 states=0x10/11/13/17/1F errs=0x0/0/0/0/0 ...`
- `hcivprtransportdemo ok=1 pumped=12 wrote=6/88 read=347/0 phase=ready ... ctrl_evt=11 peer_mark=1 peer_evt=2 cfg_ch=2,14,26,38 proc=1 dist_m=0.7499`
- `hcivprstatedemo ok=1 bad_create=0xC bad_setproc=0xC bad_range=0x12 remove=0x0 post_remove=0xC ...`
- `hcivprmultidemo ok=1 pumped=12 polled=5 proc=3 transitions=3 target=3 ctrl_evt=13 peer_mark=3 peer_evt=6 stopped=1 hb_gap=1297/1518 ... steps=5 perm=0,1,0,1,0 ch=26,38,2,14,26 dist_m=0.7499`
- `hcivprmultidemo ok=1 pumped=12 polled=5 proc=3 transitions=3 target=3 ctrl_evt=13 peer_mark=3 peer_evt=6 stopped=1 hb_gap=1281/1505 ... steps=5 perm=0,1,0,1,0 ql=0,1,0,1,0 ch=26,38,2,14,26 dist_m=0.7499`
- `hcivprdumpdemo ok=1 proc=1 dist_m=0.7497`
  - `local ... amp=896 ... amp=736 ...`
  - `peer ... amp=872 ... amp=711 ...`
- `hcivprmultidemo ok=1 pumped=12 polled=5 proc=3 transitions=3 target=3 ctrl_evt=13 peer_mark=3 peer_evt=6 stopped=1 hb_gap=1329/1514 ... steps=5 perm=0,1,0,1,0 ql=0,1,0,1,0 la=960,800,960,800,960 pa=887,727,887,726,887 ch=26,38,2,14,26 dist_m=0.7501`
- `hcivprdumpdemo ok=1 proc=1 dist_m=0.7497`
  - `local ... ph=0/90/180/-90 ... amp=896/736/...`
  - `peer ... ph=-17/-154/73/-61 ... amp=872/711/...`
- `hcivprdumpdemo ok=1 proc=1 dist_m=0.7497`
  - `local ... acl=0x1200 fc=0x10 pwr=-13 ant=3 ...`
  - `peer ... acl=0x1200 fc=0x10 pwr=-13 ant=3 ...`
- `hcivprmultidemo ok=1 pumped=12 polled=8 proc=3 transitions=3 target=3 ctrl_evt=13 peer_mark=3 peer_evt=6 stopped=1 hb_gap=1249/1686 ... ql=0,1,0,1,0 la=960,800,960,800,960 pa=887,727,887,726,887 lph=0,90,180,-90,0 pph=-107,119,163,26,-107 ch=26,38,2,14,26 dist_m=0.7501`
- `hcivprchunkdemo ok=1 pumped=12 polled=2 proc=1 ctrl_evt=11 peer_mark=1 peer_evt=1 local_flags=C-- peer_flags=C-- local_steps=3 peer_steps=3 local_bytes=24 peer_bytes=24 est=1 dist_m=0.7502`
- `hcivprmultidemo ok=1 pumped=12 polled=6 proc=3 transitions=3 target=3 ctrl_evt=13 peer_mark=3 peer_evt=5 stopped=1 hb_gap=1246/1478 ... ql=0,1,0,1,0 la=960,800,960,800,960 pa=887,727,887,726,887 lph=0,90,180,-90,0 pph=-107,119,163,26,-107 ch=26,38,2,14,26 dist_m=0.7501`
- `hcivprmultidemo ok=1 pumped=12 polled=6 proc=3 transitions=3 target=3 ctrl_evt=13 peer_mark=3 peer_evt=5 stopped=1 hb_gap=1157/1323 peer_gap=4 host_peer_gap=0/0 ... dist_m=0.7501`
- `hcivprabortdemo ok=1 pumped=12 pre_polls=1 post_polls=1 settle=0 built=1 wrote=1 pre_proc=1 stop_proc=1 final_proc=1 pre_mark=1 stop_mark=1 final_mark=1 flags=CSP- phase=ready ... dist_m=0.7491`
- `hcivprlinkdemo ok=1 wrong_status=0x12 wrong_reject=1 removed=1 closed=1 reopened=1 refresh=1 link_conn=0x41 flags=CSP- ...`

That proves:

- commands were sent through the VPR transport
- the workflow reached `ready`
- the supported opcode set was answered correctly by the VPR stub fallback
- the dedicated image now enforces handle-scoped CS ownership instead of
  accepting every command against a global demo slot
- the active link state now survives normal command sequencing correctly but is
  reset cleanly on boot and after `Remove Config`
- subevent data flowed back far enough to produce an estimate
- the synthetic peer-result path no longer needs sketch-local or CPUAPP library
  packet building for the VPR demo
- the reusable `BleCsControllerVprHost` now owns the default controller
  workflow setup through `fillDemoConfig(...)`
- the local-result channel profile is no longer hardcoded in the stub
  - the dedicated CS image now derives its four demo channels from the real
    `Create Config` channel map
  - the older shared transport host `reserved` word remains only as a fallback
    for compatibility while the command-driven path settles
- the CS path now has its own VPR image budget, so future controller-side CS
  work no longer has to compete directly with ticker/hash/hibernate service
  code in the generic probe image
- the dedicated CS image now tracks command-driven CS metadata instead of
  always using the old fixed demo values
  - `Create Config` / `Set Procedure Parameters` / `Procedure Enable` update the
    active CS `configId`
  - `Procedure Enable(enable=1)` advances a real procedure counter inside the
    dedicated image
  - `Config Complete` now reflects the active `Create Config` state instead of
    only the old fixed placeholder values
  - `Procedure Enable Complete` now reflects command-owned procedure state from
    `Set Procedure Parameters`, including procedure length/count and tone
    antenna selection
- the dedicated image now owns more of the procedure sequencing itself
    instead of stopping after the first built-in procedure
    - `Set Procedure Parameters.maxProcedureCount` now drives repeated built-in
      procedure publication on the VPR side
    - the initiator `hcivprmultidemo` proof now reaches
      `proc=3 transitions=3 peer_mark=3 peer_evt=6` on the two-board setup
    - the active four-channel window is now selected per procedure from the
      configured channel map instead of replaying one static packed set
      - the current multi-procedure proof ends on `ch=26,38,2,14`
      - that rotation comes from the VPR-owned procedure counter and current
        channel-selection state, not from sketch-side packet shaping
    - once the final configured procedure is staged, the dedicated image now
      clears its active `procedure enabled` flag instead of pretending the link
      is still mid-run forever
      - the current multi-procedure proof now reports `stopped=1` and VPR
        flags `CSP-` at the end of the run
    - later procedures are no longer published back-to-back as soon as the
      previous peer continuation packet is drained
      - the dedicated image now waits for a VPR-owned heartbeat interval derived
        from the configured procedure interval before it stages the next
        procedure
      - the current multi-procedure proof shows nonzero heartbeat spacing via
        `hb_gap=1297/1518`
    - the synthetic step payload is no longer fixed at four mode-2 steps for
      every procedure
      - the dedicated image now derives the staged step count from the
        configured `min/max main-mode steps`
      - the current multi-procedure proof ends on `steps=5`
    - the synthetic mode-2 step metadata is now less fixed too
      - the dedicated image now reports controller-owned antenna permutation
        indices from `toneAntennaConfigSelection` instead of hard-coding one
        permutation for every staged tone
      - the current multi-procedure proof ends on `perm=0,1,0,1,0`
    - the synthetic mode-2 quality metadata is no longer fixed to `high` on
      every staged step
      - the dedicated image now emits a controller-owned alternating
        `high/medium` quality pattern across staged steps
      - the current multi-procedure proof ends on `ql=0,1,0,1,0`
    - the synthetic mode-2 PCT amplitudes are no longer fixed either
      - the dedicated image now scales local and peer PCT samples per
        procedure/step while preserving the intended phase geometry
      - the current dump proof shows local `amp=896/736/...` and peer
        `amp=872/711/...`
      - the current multi-procedure proof ends on
        `la=960,800,960,800,960 pa=887,727,887,726,887`
    - the synthetic mode-2 PCT phase is no longer fixed either
      - the dedicated image now rotates local tone phase in quarter turns and
        counter-rotates the peer sample so the combined phase slope stays at
        the intended nominal distance
      - the current dump proof shows local `ph=0/90/180/-90` while the peer
        dump follows the compensating rotated phases
      - the current multi-procedure proof ends on
        `lph=0,90,180,-90,0 pph=-107,119,163,26,-107`
    - result packet chunking is no longer hard-coded to a half-and-half split
      - the dedicated image now decides whether a continuation is needed from
        the actual packet budget
      - a 3-step procedure now completes in one local packet and one peer
        packet with no continuation at all
      - the focused chunk proof ends on
        `peer_evt=1 local_flags=C-- peer_flags=C-- local_steps=3 peer_steps=3`
      - the normal multi-procedure proof now ends on `peer_evt=5` instead of
        the older fixed `6`, because the publication shape is no longer a
        forced two-packet peer path for every procedure
    - the dedicated image now has a validated mid-run disable path
      - the host can issue a raw `Procedure Enable(enable=0)` after procedure 1
      - the current abort proof ends on `pre_proc=1 final_proc=1` with
        `flags=CSP-`, so VPR stops the run cleanly instead of scheduling
        procedure 2
  - the dedicated image now rejects at least one real bad workflow edge instead
    of blindly succeeding for every CS command
    - `Set Procedure Parameters` before `Security Enable` now returns `0x0C`
      command-disallowed from the VPR side
  - `Remove Config` is now handled in the dedicated image and clears the active
    CS state instead of falling through to unknown-command behavior
    - a follow-up `Set Procedure Parameters` after `Remove Config` now returns
      `0x0C` command-disallowed from the VPR side
  - the dedicated image now also validates real CS payload content instead of
    only workflow ordering
    - `Create Config` with an empty channel map now returns `0x12`
      invalid-parameters from the VPR side
    - `Set Procedure Parameters` with invalid content like
      `maxProcedureCount=0` now returns `0x12` invalid-parameters from the
      VPR side
  - both the local and peer CS result headers now reflect that state too
    - start ACL event counter is no longer fixed
    - frequency compensation is no longer fixed
    - reference power is now derived from controller-side default settings and
      procedure parameters
    - reported antenna-path count now tracks the configured tone-antenna
      selection
  - CPUAPP no longer fabricates peer result packets for the dedicated-image path
- the synthetic built-in result pair is back at the intended nominal distance
  after giving the dedicated image real stack headroom
  - the earlier `~1.51 m` regression was not bad phase math
  - it came from a near-full VPR image window with no explicit stack reserve,
    which corrupted later peer step bytes at runtime
- the controller session now snapshots the last completed matched local/peer
  subevent-result pair into stable host-side storage
  - the new `hcivprdumpdemo` dump path now reports that completed pair instead
    of whichever in-flight procedure the reassemblers most recently touched
  - this keeps the VPR diagnostics coherent even when the local side has
    already advanced into the next procedure by the time the sketch prints

## Built-In VPR Stub Behavior

The VPR stub now has built-in fallback responders for these CS opcodes when no script entry matches:

- `0x208A` Read Remote Supported Capabilities
- `0x208D` Set Default Settings
- `0x2090` Create Config
- `0x208C` Security Enable
- `0x2093` Set Procedure Parameters
- `0x2094` Procedure Enable

The `Procedure Enable` fallback now emits:

- built-in local CS subevent result and continuation packets
- a small vendor source-marker event
- built-in peer CS subevent result and continuation packets

The dedicated CS image is now small enough that peer-result publication no
longer has to be synthesized on CPUAPP. The host only uses the marker event to
route the following controller packets into the peer reassembler.

The current validated live proof is:

- `hcivprtransportdemo ok=1 pumped=11 wrote=6/88 read=282/0 phase=ready ... ctrl_evt=11 peer_trig=0 peer_mark=1 peer_evt=2 cfg_ch=2,14,26,38 proc=1 dist_m=0.7499`
- `hcivprtransportdemo ok=1 pumped=12 wrote=6/88 read=282/0 phase=ready ... ctrl_evt=11 peer_trig=0 peer_mark=1 peer_evt=2 cfg_ch=2,14,26,38 cfg_steps=4-6 cfg_rep=2 proc=1 proc_cnt=5 proc_len=17 tone_sel=3 dist_m=0.7499`
- `hcivprstatedemo ok=1 bad_create=0x12 bad_setproc=0xC bad_range=0x12 remove=0x0 post_remove=0xC phase=ready proc=1 proc_cnt=0 cfg=1 dist_m=0.7508`
- `hcivprmultidemo ok=1 pumped=12 polled=5 proc=3 transitions=3 target=3 ctrl_evt=13 peer_mark=3 peer_evt=6 stopped=1 hb_gap=1297/1518 phase=ready steps=5 perm=0,1,0,1,0 ch=26,38,2,14,26 dist_m=0.7499`
- `hcivprmultidemo ok=1 pumped=12 polled=5 proc=3 transitions=3 target=3 ctrl_evt=13 peer_mark=3 peer_evt=6 stopped=1 hb_gap=1281/1505 phase=ready steps=5 perm=0,1,0,1,0 ql=0,1,0,1,0 ch=26,38,2,14,26 dist_m=0.7499`
- `hcivprabortdemo ok=1 pumped=12 pre_polls=1 post_polls=1 settle=0 built=1 wrote=1 pre_proc=1 stop_proc=1 final_proc=1 pre_mark=1 stop_mark=1 final_mark=1 flags=CSP- phase=ready dist_m=0.7491`

Those older `0.7499 m` demo-distance lines are now superseded by the current
connection-scoped run logs above.

## Current Remaining Gap

Current honest status:

- command/state ownership on VPR is working for one real link session
- the transport, state, multi-procedure, and link-handle demos are green
- the synthetic built-in ranging path is back at the intended nominal
  `~0.75 m` estimate
- the last completed matched local/peer pair is now held in stable host-side
  storage for diagnostics and future API use
- the dedicated image now also closes its active procedure lifetime honestly at
  the end of the configured run instead of leaving `procedure enabled` stuck
  high
- later procedures now wait for a VPR-owned interval before staging, so the
  dedicated image has started to own procedure pacing rather than only packet
  publication order
- the synthetic step count is now controller-owned too, not fixed at four steps
  regardless of the configured create-config range
- the synthetic mode-2 permutation metadata is now controller-owned too, not
  hard-coded to one permutation across the whole run
- the synthetic mode-2 quality metadata is now controller-owned too, not fixed
  to `high` for every staged tone
- the synthetic mode-2 PCT amplitudes are now controller-owned too, not fixed
  at one local/peer magnitude for every staged tone
- the synthetic mode-2 PCT phase is now controller-owned too, not fixed to one
  local/peer orientation pattern across every staged tone
- the synthetic result headers are now controller-owned too, not fixed to the
  earlier `acl=0x1234 fc=0 pwr=0 ant=2` placeholder set
- result packet chunking is now controller-owned too, not fixed to a constant
  halfway split and continuation count for every procedure
- local-to-peer result staging inside a procedure is now controller-owned too,
  not immediate back-to-back publication from one fixed drain sequence
- a direct host-issued `Procedure Enable(enable=0)` now stops the dedicated
  image cleanly mid-run instead of forcing the demo to run to the configured
  procedure count every time

So the next follow-up on the CS side is:

- keep the current connection-scoped VPR session model
- keep moving result ownership away from fixed synthetic publication and toward
  more controller-owned behavior on VPR
- extend the current per-procedure channel-window logic into broader
  controller-owned scheduling instead of replaying only one four-step synthetic
  shape per procedure

The same size budget applies to CS demo configuration. A dedicated vendor opcode
for demo-channel configuration was tested and worked functionally, but it pushed
the stub back over the image limit. The current stable design uses the existing
shared-transport host `reserved` word as a tiny CS demo mailbox instead.

## Known Good Design Choice

The stable path is:

- shared-memory windows
- host polling
- VPR stub firmware built from `vpr_cs_transport_stub.c`

One attempted variant should stay disabled unless reworked carefully:

- direct VPR -> host event signaling through the VPR event CSRs

That path regressed command flow after the second command even though shared memory stayed alive. The poll-based design is the proven baseline.

For the hibernate/resume investigation specifically, the current known-good
observations are:

- keep the poll-based shared-memory transport
- do not arm the earlier VPR CSR wake-path experiment before hibernate
- do not touch `clearBufferedSignals()` before the reset pulse in the resume path
- skip the pre-resume "wake by command" probe path when validating reset-based
  resume; it leaves a stale pending response that confuses the next command
- `MEMCONF.POWER[0].RET2.MEM[7]` must stay retained for VPR context restore as
  called out in the datasheet, and the current core now keeps that bit enabled
  when VPR context restore is turned on
- after a failed hibernate resume, recovery needs to clear the saved context and
  turn context restore back off before attempting a cold image restart;
  otherwise reset can keep chasing the stale restore path instead of booting the
  freshly loaded service image
- `VprControl::restartAfterHibernateReset()` is the honest name for the working
  `NDMRESET + CPURUN` path; the older `resumeRetainedContext()` entry point is
  still kept for compatibility, but the hardware result is still "service
  restart after hibernate reset", not a proven retained-state resume
- the stable retained service restart path now goes one step further and
  disables raw VPR hardware context restore before reboot; the retained state
  that survives across both attached boards is the host-side service state, not
  raw VPR CPU execution context
- `VprControl::start()` now forces a real hart reset before rerun; that change
  is what made repeated loaded-image restart pass on both attached boards
- `VprControl::rawSleepControl()` and `configureSleepControl(...)` are now
  exposed so the probes can inspect and tune the `VPRNORDICSLEEPCTRL` state
  directly from CPUAPP
- the VPR service transport info now has an explicit "restored from hibernate"
  flag, and the shared VPR transport `reserved` word mirrors that state for
  probe/debug inspection
- `VprRestartLifecycleProbe` is no longer only an investigation sketch; it is a
  useful regression example for loaded-image restart

## Remaining Gaps

This is still not a production BLE controller runtime.

Still missing. There are 2 major VPR capability areas left:

1. General VPR runtime/service depth:
   - a richer reusable VPR-side general controller/offload service instead of
     only the current built-in demo/vendor opcodes
2. Real BLE-controller integration:
   - a connected BLE controller service on VPR instead of the current CS demo
     responder
   - binding the CS workflow to real link state rather than demo-scripted
     behavior
   - broader result/error handling around real procedures
   - reliable raw RADIO RTT AUXDATA decode on the non-controller path
   - broader validation across more boards and phone hosts

## Immediate Next TODO

The next concrete implementation target should be the first real connected
BLE-controller slice on VPR, not more demo-only CS result shaping.

That work should do these things in order:

1. Move one real connected-state controller responsibility onto VPR.
   - start with connection-owned CS procedure/session state rather than only
     demo command handling
2. Bind CS procedure enable/disable and result flow to real link state on the
   host side.
   - no new synthetic sketch-side assumptions
   - no host-side fake peer packet construction for the new path
3. Make the dedicated CS image own procedure lifetime, result staging, and
   teardown for that one connected path.
4. Keep the current demo responders as a fallback regression harness until the
   real connected path is stable.
5. Use the existing regressions after each step:
   - `hcivprtransportdemo`
   - `hcivprstatedemo`
   - `hcivprmultidemo`
   - `hcivprcontinuedemo`
   - `hcivprsubeventdemo`

The immediate non-goal is Matter or Thread work. The current dependency chain
still runs through VPR ownership first, then BLE controller ownership, then
controller-backed CS, then higher wireless stacks.

## Suggested Next Steps

1. Keep the poll-based transport as the baseline.
2. Preserve the built-in responder path as the working demo harness.
3. Keep the current peer-result trigger split unless the VPR image budget is
   widened; full peer-result publication from the stub is too large for the
   current `0x1000` window.
4. Move one real controller function at a time from host-side synthetic behavior
   into VPR-side service code.
5. Keep `VprHibernateResumeProbe` as a regression example for the stable
   reset-after-hibernate retained service restart path.
6. Keep `VprRestartLifecycleProbe` as a regression example for loaded-image
   restart and rerun it after any VPR lifecycle change.
7. If true raw VPR CPU-context resume matters later, treat it as a separate
   investigation from the now-stable service restart path.
8. Add a second VPR firmware mode for real command dispatch instead of hardcoded
   fallback responses.
9. Only revisit CSR event signaling after the controller service is stable on
   shared-memory polling.
10. Keep shifting the dedicated CS image upward from byte-shaping toward real
    per-procedure layout ownership on VPR.

## Resume Checklist

When resuming this work:

1. Regenerate the VPR stub header:
   - `python3 hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/tools/generate_vpr_cs_transport_stub.py`
2. Rebuild the initiator example.
3. Flash initiator and reflector.
4. Run `hcivprtransportdemo`.
5. Run `hcivprmultidemo`.
6. Confirm the built-in responder path still reaches `phase=ready` and the
   multi-procedure demo advances the procedure counter more than once.
7. Only then start changing transport or controller behavior.
8. Re-run `hcivprdumpdemo` after any dedicated-image layout change and confirm
   the local/peer result dump still parses cleanly.

## Notes

- The generated firmware header path bug was already fixed in the generator.
- The initiator sketch still contains some now-unused demo helper code from the earlier script-driven phase. That is cleanup work, not a functional blocker.
- The current repo docs already describe the feature as partial. Keep that wording until a real controller/runtime exists.
- The dedicated CS image now publishes a mixed synthetic layout too:
  - one invalid/unavailable mode-1 timing step at the start of procedures with
    more than three phase steps
  - followed by the existing mode-2 phase-tone steps
  - continuation chunking is now based on actual encoded step sizes, not an
    old fixed `8 bytes per step` assumption
  - that inserted timing step is now gated by `createConfig.rttType`, so the
    dedicated image can cleanly fall back to a pure phase-only mode-2 layout
    when RTT is disabled
- The dedicated CS image now also exposes a VPR-owned procedure-interval
  selector in the shared link-state word, so the host can see that scheduling
  is actually using the configured min/max interval range rather than always
  the minimum interval.
- The dedicated CS image now owns repeated continuation publication too:
  - local and peer result publication no longer assume a fixed `initial +
    one continue` structure
  - the dedicated image keeps explicit chunk cursors for both sides and emits
    as many continuation packets as the controller-owned chunk budget requires
  - `hcivprcontinuedemo` now proves that path with one RTT-enabled six-step
    procedure that lands as `3` local result packets and `3` peer result
    packets while still estimating `~0.75 m`
- The dedicated CS image now also ties that chunk budget to configured
  subevent policy:
  - a tighter `minSubeventLen` now forces more continuation packets for the
    same six-step RTT-enabled synthetic procedure
  - `hcivprsubeventdemo` proves that by driving the same result layout to
    `6` local result packets and `6` peer result packets at
    `minSubeventLen = maxSubeventLen = 0x000100`
  - the default path stays at `3` local / `3` peer result packets for the
    same six-step procedure, so the dedicated image is now using configured
    subevent policy instead of only encoded byte count
- The dedicated CS image now also spaces continuation chunks from VPR-owned
  heartbeat timing derived from subevent policy:
  - repeated local and peer continuation packets are no longer drained
    back-to-back on the same controller heartbeat
  - `hcivprsubeventdemo` now proves nonzero packet gaps on both sides while
    still landing at `6` local and `6` peer result packets for the tight
    `minSubeventLen = maxSubeventLen = 0x000100` case
- The host/session layer now aggregates complete partial-procedure subevents
  into one completed procedure result pair:
  - VPR can now publish more than one complete subevent for the same procedure
    counter without the host estimating too early on the first subevent
  - the stable `completedLocalResult()` / `completedPeerResult()` pair now
    represents the full completed procedure, not only the last complete
    subevent that arrived
- The dedicated CS image now owns a higher-level procedure publication policy
  too:
  - larger RTT-enabled synthetic procedures can now be split into multiple
    complete subevents instead of only one complete subevent with many
    continuation packets
  - `hcivprmultisubdemo` proves that path with one seven-step
    RTT-enabled procedure that lands as `2` local subevents and `2` peer
    subevents while the host still reassembles one completed seven-step
    procedure estimate at `~0.75 m`
  - that subevent count is now derived from configured subevent policy for the
    larger seven-step procedure shape instead of staying fixed at `2`
  - the default policy still lands at `2` complete subevents
  - `hcivprsubcountdemo` proves the tighter
    `minSubeventLen = maxSubeventLen = 0x000100` path, which now lands at
    `3` local subevents and `3` peer subevents while still reassembling the
    same completed seven-step procedure estimate at `~0.75 m`
- The host/session layer now has a narrower controller-lifecycle reset seam:
  - workflow-driven `Create Config`, `Remove Config`,
    `Set Procedure Parameters`, and `Procedure Enable` commands now flush
    in-flight result assembly before the next controller-owned run shape starts
  - once the CS workflow is already `ready`, later out-of-band CS control
    replies (`Command Status`, `Command Complete`, and the matching CS LE meta
    completion events) are tolerated instead of failing the session parser
  - the direct VPR controller helper now preserves non-response H4 controller
    packets instead of dropping them while waiting for a direct reply
  - that helper also now waits for a writable shared-transport slot while VPR
    is still publishing CS result packets, instead of failing immediately on a
    busy transport window
  - direct out-of-band run-shape commands now also reset in-flight host
    procedure assembly before the command is sent, matching the existing
    workflow-driven command path
  - once the workflow is already `ready`, out-of-band CS control traffic now
    updates the workflow shadow state instead of only being tolerated
    - direct `Remove Config` now clears the ready-phase host shadow flags
    - direct `Read Remote Supported Capabilities`, `Set Default Settings`,
      `Create Config`, `Security Enable`, `Set Procedure Parameters`, and
      `Procedure Enable` now repopulate that shadow on the live VPR session
  - the dedicated CS image now emits a real
    `Config Complete(action=remove)` LE meta event on `Remove Config`, instead
    of only returning `Command Complete`
  - the stable live proofs for this slice are now:
    - `hcivprtransportdemo`
    - `hcivprsubcountdemo`
    - `hcivprabortdemo`
    - `hcivprmanualdemo`
    - `hcivprreconfigdemo`
    - `hcivprcfgswapdemo`
    - `hcivprmulticfgdemo`
  - `hcivprmanualdemo` is now the focused regression for direct out-of-band
    `Procedure Enable(enable=1/0/1/0)` control on the VPR path
  - `hcivprreconfigdemo` now proves direct out-of-band
    `Set Procedure Parameters` reconfiguration on the live VPR session:
    - one direct run with a wider subevent budget lands at `2` local and `2`
      peer complete subevents for the same seven-step synthetic procedure
    - a second direct run, on the same live session and without rebooting the
      transport, tightens subevent budget and lands at `3` local and `3` peer
      complete subevents for that same procedure shape
  - `hcivprcfgswapdemo` now proves a full direct config rebuild on one live
    VPR session without rebooting the transport:
    - direct `Remove Config`
    - direct `Read Remote Supported Capabilities`
    - direct `Set Default Settings`
    - direct `Create Config` with a new `configId`
    - direct `Security Enable`
    - direct `Set Procedure Parameters`
    - direct `Procedure Enable`
    - the rebuilt run now completes on `configId=2` with a pure mode-2
      four-step synthetic shape (`steps=0+4/0+4`)
  - `hcivprmulticfgdemo` now proves config bouncing on one live VPR session
    without transport reboot:
    - build and run an alternate `configId=2`
    - directly recreate `configId=1` from host-side cached workflow settings
      on that same live session
    - re-enable security, re-apply procedure parameters, and run `configId=1`
      again without `Remove Config` or a transport reboot
    - the two direct runs now land on different synthetic result shapes on the
      same live session (`0+4/0+4` for the alternate config, then `0+3/0+3`
      after bouncing back to the base config)
- The two attached boards were restored to `VprSharedTransportProbe` after the
  resume/restart experiments and both were left healthy on the known-good
  `svc=1.7` / `opmask=0x3FF` path.
- The newer resume logs changed that picture during investigation:
  - `/dev/ttyACM0` stayed on the known-good `VprSharedTransportProbe` path
  - `/dev/ttyACM1` was used for the evolving `VprHibernateResumeProbe`
    instrumentation while narrowing the reset/resume sequence
