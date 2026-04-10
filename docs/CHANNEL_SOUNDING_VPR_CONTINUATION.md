# Channel Sounding VPR Continuation

This note is the resume point for the current Channel Sounding and VPR transport work.

## Current State

The clean core now has a real VPR-backed transport path for controller-style Channel Sounding bring-up.

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

The key proof lines from the current built-in responder path are:

- `hcivprtransportdemo ok=1 pumped=12 wrote=6/88 read=217/63 phase=ready ... ctrl_evt=11 peer_trig=1 peer_evt=2 cfg_ch=2,14,26,38 proc=1 dist_m=0.7499`

That proves:

- commands were sent through the VPR transport
- the workflow reached `ready`
- the supported opcode set was answered correctly by the VPR stub fallback
- subevent data flowed back far enough to produce an estimate
- the synthetic peer-result path no longer needs sketch-local or CPUAPP library
  packet building for the VPR demo
- the reusable `BleCsControllerVprHost` now owns the default controller
  workflow setup through `fillDemoConfig(...)`
- the local-result channel profile is no longer hardcoded in the stub
  - `BleCsControllerVprHost::beginHost(...)` now packs the four demo channels
    into the shared transport host `reserved` word
  - the VPR stub reads that mailbox word when it builds the local mode-2 steps
- the CS path now has its own VPR image budget, so future controller-side CS
  work no longer has to compete directly with ticker/hash/hibernate service
  code in the generic probe image
- the dedicated CS image now tracks command-driven CS metadata instead of
  always using the old fixed demo values
  - `Create Config` / `Set Procedure Parameters` / `Procedure Enable` update the
    active CS `configId`
  - `Procedure Enable(enable=1)` advances a real procedure counter inside the
    dedicated image
  - both the local and peer CS result headers now reflect that state
  - CPUAPP no longer fabricates peer result packets for the dedicated-image path

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

## Resume Checklist

When resuming this work:

1. Regenerate the VPR stub header:
   - `python3 hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/tools/generate_vpr_cs_transport_stub.py`
2. Rebuild the initiator example.
3. Flash initiator and reflector.
4. Run `hcivprtransportdemo`.
5. Confirm the built-in responder path still reaches `phase=ready`.
6. Only then start changing transport or controller behavior.

## Notes

- The generated firmware header path bug was already fixed in the generator.
- The initiator sketch still contains some now-unused demo helper code from the earlier script-driven phase. That is cleanup work, not a functional blocker.
- The current repo docs already describe the feature as partial. Keep that wording until a real controller/runtime exists.
- The two attached boards were restored to `VprSharedTransportProbe` after the
  resume/restart experiments and both were left healthy on the known-good
  `svc=1.7` / `opmask=0x3FF` path.
- The newer resume logs changed that picture during investigation:
  - `/dev/ttyACM0` stayed on the known-good `VprSharedTransportProbe` path
  - `/dev/ttyACM1` was used for the evolving `VprHibernateResumeProbe`
    instrumentation while narrowing the reset/resume sequence
