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
- `VprHibernateContextProbe`
- `VprHibernateResumeProbe`
- `VprRestartLifecycleProbe`

Current validated generic service state on hardware:

- `svc=1.4`
- `opmask=0x1FF`
- `max_in=124`
- cold-boot command path is good
- autonomous ticker state progresses on VPR after the configure command returns
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

The key proof line from the built-in responder path is:

- `hcivprtransportdemo ok=1 pumped=12 wrote=6/88 read=212/63 phase=ready ... proc=7 dist_m=0.7501`

That proves:

- commands were sent through the VPR transport
- the workflow reached `ready`
- the supported opcode set was answered correctly by the VPR stub fallback
- subevent data flowed back far enough to produce an estimate

## Built-In VPR Stub Behavior

The VPR stub now has built-in fallback responders for these CS opcodes when no script entry matches:

- `0x208A` Read Remote Supported Capabilities
- `0x208D` Set Default Settings
- `0x2090` Create Config
- `0x208C` Security Enable
- `0x2093` Set Procedure Parameters
- `0x2094` Procedure Enable

The `Procedure Enable` fallback also emits built-in local CS subevent result and continuation packets so the host workflow can complete without sketch-side scripted injection.

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
3. Move one real controller function at a time from host-side synthetic behavior into VPR-side service code.
4. Keep `VprHibernateResumeProbe` as a regression example for the stable
   reset-after-hibernate retained service restart path.
5. Keep `VprRestartLifecycleProbe` as a regression example for loaded-image
   restart and rerun it after any VPR lifecycle change.
6. If true raw VPR CPU-context resume matters later, treat it as a separate
   investigation from the now-stable service restart path.
7. Add a second VPR firmware mode for real command dispatch instead of hardcoded fallback responses.
8. Only revisit CSR event signaling after the controller service is stable on shared-memory polling.

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
  `svc=1.4` / `opmask=0x1FF` path.
- The newer resume logs changed that picture during investigation:
  - `/dev/ttyACM0` stayed on the known-good `VprSharedTransportProbe` path
  - `/dev/ttyACM1` was used for the evolving `VprHibernateResumeProbe`
    instrumentation while narrowing the reset/resume sequence
