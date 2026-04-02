# BLE Background Advertiser Note

This note captures the implemented controller-style BLE advertising milestones
for the clean nRF54L15 Arduino core.

## Scope Implemented

- Legacy `ADV_NONCONN_IND`
- One advertising set
- Single-channel background advertising
- 3-channel primary advertising rotation
- 3-channel TX compares pre-armed per event
- Fixed interval scheduling from the first primary channel, with optional
  Bluetooth random advertising delay handled inside the background scheduler
- No sketch-side repeated `advertiseEvent()` call
- CPU sleeps between events

Not implemented in this pass:

- Scan response / connect request handling
- Extended advertising / AUX chains

## Datasheet Anchors

Primary source: `Nordic_nRF54L15_Datasheet_v1.0.pdf` in the local workspace.

- `5.4.1 HFCLK controller`
  - RADIO requires HFXO
  - `XOTUNED` is required before using RADIO
- `11.9.1 32 MHz crystal oscillator (HFXO)`
  - example tuned start times are 300 us and 700 us typ depending on crystal
- `8.10 GRTC`
  - 1 us system counter
  - `CC[n]`, `EVENTS_COMPARE[n]`, `PUBLISH_COMPARE[n]`
- DPPI / PPIB bridge section
  - LP PD, PERI PD, and RADIO PD are separate domains
  - `PPIB30 <-> PPIB22 <-> PPIB21 <-> PPIB11` bridge chain is required
- `8.17 RADIO`
  - `TASKS_TXEN`
  - `EVENTS_DISABLED`
  - `SUBSCRIBE_TXEN`
  - `PUBLISH_DISABLED`
  - `SHORTS.READY_START`
  - `SHORTS.TXREADY_START`
  - `SHORTS.PHYEND_DISABLE`

## Implemented Architecture

Software still owns:

- guaranteed TX kickoff on the current functional XIAO runtime
- advertising payload construction
- single-channel selection or 37/38/39 channel register updates
- fixed interval value
- fallback random advertising delay state if CRACEN RNG is unavailable
- re-arming the next advertising cycle
- board RF path policy

Hardware now owns the compare schedule and the HFXO teardown path:

1. `GRTC.COMPARE[prewarm]`
   - published in LP domain to `CLOCK.TASKS_XOSTART`
   - on the current IRQ-driven XIAO runtime, the same prewarm compare also
     wakes the BLE IRQ, which mirrors `CLOCK.TASKS_XOSTART` in software
     without waiting; that keeps the hardware schedule but closes the
     reliability gap seen on this board when relying on the subscribe path
     alone
2. `GRTC.COMPARE[tx0/tx1/tx2]`
   - three TX compares can be armed up front for 37/38/39
   - still provide the hardware time base for each advertising packet
   - on the connected XIAO, the default runtime now uses the proven
     compare-IRQ-assisted kickoff path from boot
   - each TX compare is consumed by the BLE GRTC IRQ; the IRQ kicks
     `RADIO.TASKS_TXEN` immediately when HFXO is already running, otherwise it
     re-arms the same TX compare a few microseconds later and returns without
     spinning
   - the direct `GRTC -> DPPI/PPIB -> RADIO.TXEN` chain remains experimental on
     this board and is only available through an explicit override
   - the experimental direct path currently enables both
     `SHORTS.READY_START` and `SHORTS.TXREADY_START`, but that still has not
     made compare-only TX kickoff reliable enough to replace the IRQ-assisted
     launch path on the connected XIAO
3. `GRTC.COMPARE[cleanup/final-cleanup]`
   - the single-channel cleanup compare and the 3-channel final-cleanup compare
     both publish directly in LP domain to `CLOCK.TASKS_XOSTOP`
   - that keeps HFXO stop on the hardware-timed schedule instead of depending
     on a later software release
4. `RADIO.SHORTS`
   - `TXREADY_START`
   - `PHYEND_DISABLE`
5. `CRACEN RNG` when random delay is enabled
   - hardware entropy is used first for the Bluetooth 0-10 ms advertising delay
   - software LCG remains only as a fallback if CRACEN does not return data in
     budget

CPU involvement per interval:

- Single-channel mode:
  - one prewarm compare interrupt to restore board RF state and mirror
    `TASKS_XOSTART` without waiting
  - one TX compare interrupt to kick `TXEN` when HFXO is already running
  - additional TX compare interrupts only if the chosen HFXO lead is too short
  - one cleanup compare interrupt after the packet to re-arm the next interval
- 3-channel mode:
  - one prewarm compare interrupt before channel 37 to restore board RF state
    and mirror `TASKS_XOSTART` without waiting
  - three TX compare interrupts to kick `TXEN` for channels 37, 38, and 39
  - additional TX compare interrupts only if the chosen HFXO lead is too short
  - one GRTC service compare interrupt before channel 38
  - one GRTC service compare interrupt before channel 39
  - one GRTC cleanup compare interrupt after channel 39 to arm the next event
- In 3-channel mode, software updates channel state at the two stage interrupts,
  the TX compares for channels 37/38/39 are already programmed, and the final
  cleanup compare directly owns `CLOCK.TASKS_XOSTOP`
- the current functional runtime also issues a direct software `TASKS_XOSTOP`
  from the existing cleanup service only if `XO.STAT` still reports running;
  that is a fallback, not the primary stop schedule
- CPU returns to sleep after each re-arm

This is not "pure hardware BLE". It is controller-style offload:
software manages state and the remaining compare-IRQ work; hardware owns the
time base and the scheduled HFXO timing.

## File Map

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.h`
  - new public API
  - new background advertiser state
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp`
  - GRTC compare scheduling
  - LP/PERI/RADIO DPPI and PPIB bridge wiring
  - background advertiser start/stop/service logic
  - BLE idle/GRTC IRQ integration
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_regs.h`
  - DPPIC10/DPPIC30 and PPIB11/21/22/30 base addresses
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/Advertising/BleBackgroundAdvertiserSingleChannel/BleBackgroundAdvertiserSingleChannel.ino`
  - sketch example for the one-channel API
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/Advertising/BleBackgroundAdvertiser3Channel/BleBackgroundAdvertiser3Channel.ino`
  - sketch example for 37/38/39 background rotation
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/Advertising/BleBackgroundAdvertiser3ChannelDiagnostics/BleBackgroundAdvertiser3ChannelDiagnostics.ino`
  - board-side validation sketch that prints HAL scheduler counters while the
    background advertiser runs

## Public API

Current API:

- `bool BleRadio::beginBackgroundAdvertising(uint32_t intervalMs, BleAdvertisingChannel channel = BleAdvertisingChannel::k37, uint32_t hfxoLeadUs = 1200U);`
- `bool BleRadio::beginBackgroundAdvertising3Channel(uint32_t intervalMs, uint32_t interChannelDelayUs = 350U, uint32_t hfxoLeadUs = 1200U, bool addRandomDelay = false);`
- `void BleRadio::stopBackgroundAdvertising();`
- `bool BleRadio::isBackgroundAdvertisingEnabled() const;`
- `void BleRadio::getBackgroundAdvertisingDebugCounters(BleBackgroundAdvertisingDebugCounters* out) const;`
- `void BleRadio::clearBackgroundAdvertisingDebugCounters();`

Current rules:

- requires `ADV_NONCONN_IND`
- requires `begin()` first
- one of channels `37`, `38`, or `39`
- 3-channel mode currently requires `interChannelDelayUs >= 250`
- 3-channel mode optionally adds the Bluetooth-spec 0-10 ms advertising delay
- when that delay is enabled, background mode uses CRACEN RNG first and falls
  back to software state only if the hardware RNG does not return a word

## Exact Peripheral Wiring

GRTC compare channels:

- `CC[3]`: prewarm compare
- `CC[4]`: channel 37 TX launch compare
- `CC[6]`: single-channel cleanup compare
- `CC[7]`: stage-1 service compare before channel 38
- `CC[8]`: channel 38 TX launch compare
- `CC[9]`: stage-2 service compare before channel 39
- `CC[10]`: channel 39 TX launch compare
- `CC[11]`: 3-channel final cleanup compare

LP domain:

- `DPPIC30 ch0`: `GRTC.PUBLISH_COMPARE[3] -> CLOCK.SUBSCRIBE_XOSTART`
- `DPPIC30 ch1`: experimental direct-launch path only,
  `GRTC.PUBLISH_COMPARE[4|8|10] -> PPIB30.SUBSCRIBE_SEND[0]`
- `DPPIC30 ch2`: `GRTC.PUBLISH_COMPARE[6|11] -> CLOCK.SUBSCRIBE_XOSTOP`

PERI domain:

- `DPPIC20 ch5`: experimental direct-launch relay only,
  `PPIB22.PUBLISH_RECEIVE[0] -> PPIB21.SUBSCRIBE_SEND[0]`

RADIO domain:

- default runtime:
  `TX compare IRQ -> RADIO.TASKS_TXEN`
- experimental direct-launch relay:
  - `DPPIC10 ch1`: `PPIB11.PUBLISH_RECEIVE[0] -> RADIO.SUBSCRIBE_PLLEN`
  - `DPPIC10 ch0`: `RADIO.PUBLISH_PLLREADY -> RADIO.SUBSCRIBE_TXEN`

RADIO shortcuts:

- `SHORTS.TXREADY_START`
- `SHORTS.PHYEND_DISABLE`

Interrupts:

- existing BLE GRTC IRQ group
- `CLOCK_POWER_IRQn` only for `XOTUNED` / `XOTUNEERROR` / `XOTUNEFAILED`
  handling while the IRQ-assisted TX kickoff path is active
- no new RADIO IRQ
- no busy-spin timing wait in the background advertising path

## Tradeoffs In This Milestone

Improved now:

- sketch loop no longer emits advertising events
- interval ownership moved into BLE HAL scheduler
- scheduled TX timing still comes from hardware compares, but the current
  functional runtime now uses a nonblocking prewarm IRQ plus compare re-arm
  retries instead of waiting in place for HFXO readiness
- CPU can use `WFI` between events
- the idle hook no longer polls background advertising on every `yield()` /
  low-power `delay()` cycle when the GRTC IRQ path is healthy
- if the GRTC IRQ path is unavailable, fallback service now consumes the
  hardware `EVENTS_COMPARE` bits directly instead of polling the wall clock
- HFXO start/stop scheduling for each event is hardware-driven
- 3-channel primary advertising no longer depends on sketch-loop pacing
- 3-channel primary TX compares are armed once at event start, not reprogrammed
  between 37/38/39
- cleanup compare `CC[6]` and final-cleanup compare `CC[11]` now drive
  `CLOCK.TASKS_XOSTOP` directly in LP domain, which removed the fragile
  `RADIO.DISABLED -> PPIB -> CLOCK.XOSTOP` bridge from the steady-state path
- if the cleanup service finds `XO.STAT` still running after the radio has
  settled, it now issues a direct software `TASKS_XOSTOP` fallback during the
  already-required service wake instead of leaving HFXO up through the interval
- common-case GRTC re-arm no longer burns a fixed `delayMicroseconds(93)`
- `ClockControl::startHfxo(true)` now waits for `XOTUNED`, not just
  `XOSTARTED`
- the default compare-IRQ TX path no longer uses the full blocking transmit
  helper; it now performs a minimal `TXEN` kick and lets the radio finish under
  `SHORTS`
- the prewarm compare path now mirrors `CLOCK.TASKS_XOSTART` in software
  without waiting, which keeps the prewarm time hardware-scheduled but avoids
  the board-specific reliability gap seen when using the HFXO subscribe path
  alone
- the TX compare path no longer spins on HFXO readiness; it re-arms the same
  compare channel if HFXO is not running yet and only kicks `TXEN` once the
  oscillator is ready
- the default runtime now attempts the pure direct launch path first, but if
  the service point sees no `READY` or `PHYEND` progress it reconfigures once
  into the proven IRQ-assisted TX kickoff path and keeps advertising instead of
  silently dying
- if a TX compare and its service/cleanup compare arrive in the same BLE GRTC
  IRQ, the runtime now re-arms the service compare from the current timebase
  instead of servicing immediately after the late kick; that closed the
  remaining functional gap in the plain single-channel example on the connected
  XIAO
- `bleTimingUs()` now reads the coherent GRTC counter instead of a raw low word,
  which fixed a real scheduling bug where compares could be armed far in the
  past
- optional background random delay now uses CRACEN RNG first instead of relying
  only on software arithmetic
- when random delay is enabled, CRACEN RNG is started ahead of time and the
  advertising scheduler reads from the hardware FIFO without waiting; software
  fallback is only used if no hardware word is ready
- legacy advertising configuration setters that rebuild live packets now use a
  transactional stop / mutate / rebuild / restart flow:
  - `setDeviceAddress()`
  - `setAdvertisingPduType()`
  - `setAdvertisingChannelSelectionAlgorithm2()`
  - `setAdvertisingData()`
  - `setScanResponseData()`
  - `setAdvertisingServiceUuid128()`
  - if the new payload or restart fails, the HAL restores the previous packet
    state and resumes the old background advertiser instead of leaving it down
- managed `CONSTLAT` support remains available as an opt-in path, but it is now
  disabled by default for the background advertiser:
  - the current service lead is `250 us`
  - the datasheet's CPU wake figure for System ON low-power sleep is `13 us`
    typical, versus `9 us` in constant-latency sub-power mode
  - the timing-critical schedule is already hardware-timed by `GRTC`, and the
    current functional runtime only wakes at the compare points that still need
    software ownership, so paying an extra software wake penalty every event
    just to enter `CONSTLAT` is not a power win on this board
  - when someone explicitly enables managed latency with
    `NRF54L15_CLEAN_BLE_BACKGROUND_MANAGED_CONSTLAT=1`, the implementation
    still tries both hardware routes first:
    - direct prewarm-channel `POWER.SUBSCRIBE_CONSTLAT`
    - LP-to-PERI `PPIB30 -> PPIB22` bridge into `POWER.SUBSCRIBE_CONSTLAT`
  - on the connected XIAO nRF54L15, neither pure-hardware path asserted
    `POWER.CONSTLATSTAT`, so the opt-in implementation falls back to one
    exact-timed prewarm GRTC IRQ that pulses `POWER.TASKS_CONSTLAT`

Still software-controlled:

- TX kickoff at each scheduled compare in the current functional runtime
- next-interval programming
- payload rebuilds
- mid-event 37/38/39 channel register updates before TX38/TX39
- RF switch collapse/restore on the XIAO board
- fallback random advertising delay state if CRACEN RNG is unavailable
- release back to variable-latency mode after each event when opt-in managed
  latency is enabled

Why variable-latency release is still software-owned:

- `POWER.TASKS_LOWPWR` is available as hardware task, but the core already has
  other constant-latency users, notably the shared UARTE ownership path for P2
  pins
- forcing `LOWPWR` blindly from `RADIO.DISABLED` would let the advertiser
  override a different subsystem's explicit constant-latency request
- when managed latency is explicitly enabled, this pass uses the hardware
  prewarm schedule to try to assert `CONSTLAT` at the advertiser's prewarm
  point, then releases back to `LOWPWR` in software only when the advertiser
  started from low-power mode and no core-owned constant-latency user is active

Why `GRTC.INTERVAL` / periodic `CC[0]` is not used here:

- the nRF54L15 datasheet limits `INTERVAL` to `CC[0]` only
- `INTERVAL.VALUE` is 16-bit, so the hardware periodic reload tops out at
  `65535 us`
- that is below the practical and standards-compliant legacy non-connectable
  advertising cadence used for this milestone, so it cannot replace the main
  event scheduler here

Why RF switch duty-cycle is still software-owned:

- the XIAO RF switch is on `P2.05` (`PIN_RF_SW_CTL`)
- the nRF54L15 exposes GPIOTE task blocks for `P0` and `P1`, but not `P2`
- there is no matching hardware task path to collapse or restore that board pin
  from the advertiser schedule
- this pass now still uses the board hardware piece itself:
  - it collapses the RF switch control path to `Hi-Z` and powers the switch off
    between advertising events
  - it restores the requested ceramic/external path only at the advertiser's
    prewarm compare
  - the trigger is software because the XIAO board placed the switch on a pin
    bank that does not have a usable task engine here
- the implementation also separates desired antenna selection from observed
  current board state, so collapsing to `Hi-Z` no longer destroys the chosen
  active route that must be restored before the next event

Why `RADIO.CSTONES.NEXTFREQUENCY` is not used for legacy advertising:

- the nRF54L15 datasheet places `NEXTFREQUENCY` under `RADIO.CSTONES`
- `TASKS_CSTONESSTART` is defined as starting tone processing for channel
  sounding
- the surrounding `CSTONES.*` registers are documented as tone-processing /
  channel-sounding controls and results, not generic packet-TX channel state
- the datasheet does not provide a corresponding hardware path for the BLE
  advertising-channel whitening change, which still depends on `DATAWHITE.IV`
- because of that, wiring `CSTONES.NEXTFREQUENCY` into legacy non-connectable
  advertising would be guesswork rather than a primary-source-backed
  controller implementation

## Validation Status

Compilation and flash:

- `BleBackgroundAdvertiserSingleChannel` compiles for
  `nrf54l15clean:nrf54l15clean:xiao_nrf54l15`
- `BleBackgroundAdvertiser3Channel` compiles for the same target
- `BleBackgroundAdvertiser3ChannelDiagnostics` compiles for the same target
- `BleAdvertiserLowestPowerContinuous` still compiles as the software-driven
  baseline
- validated with `clean_power=low`
- the board can be flashed with either sketch via `pyocd`

On-target serial validation after flashing
`BleBackgroundAdvertiser3ChannelDiagnostics` on March 28, 2026:

- the sketch prints background scheduler counters while `loop()` only does
  `WFI` plus a once-per-second report
- observed cadence at `100 ms` interval:
  - `eventArmCount` increases by about `10` per second
  - `eventCompleteCount` increases by about `10` per second
  - `stageAdvanceCount`, `irqCompareCount`, and `serviceRunCount` increase by
    about `30` per second
  - `idleFallbackCount` stays at `0` during the healthy IRQ-driven run
- that matches the implemented 3-channel controller path:
  - one full event every `100 ms`
  - three service/IRQ points per full event
- it also confirms the advertising scheduler is no longer being polled from the
  generic idle hook during normal operation
- this board-side test proves the background advertiser is advancing
  independently of sketch-loop pacing even when no host BLE scan is involved
- after validating the opt-in latency-managed path on the same board:
  - `managed=1`
  - `constlat_now=1`
  - `constlatServiceObservedCount` tracked `serviceRunCount`
  - `lowPowerReleaseCount` tracked `eventCompleteCount`
  - `constlat_sw_prewarm` tracked `eventArmCount`
  - `constlat_hw_prewarm` stayed `0`
- that confirmed the connected XIAO still needed the timed software fallback
  whenever managed latency was forced on
- the default low-power run was then revalidated on the same board:
  - `managed=0`
  - `constlat_now=0`
  - `constlatServiceObservedCount = 0`
  - `lowPowerReleaseCount = 0`
  - `constlat_hw_prewarm = 0`
  - `constlat_sw_prewarm = 0`
- with managed latency disabled, the scheduler still held the same `100 ms`
  cadence and `idleFallbackCount` stayed `0`, so the advertiser now avoids the
  extra prewarm CPU wake while preserving controller-style timing
- the same diagnostics run now also validates XIAO RF-switch duty-cycling:
  - `rf_managed=1`
  - `rf_restore` tracks `eventArmCount`
  - `rf_collapse` tracks `eventArmCount`
  - interval snapshots are phase-biased and often still land in the active
    prewarm/TX slice, so `rf_live_power` can read back as `1`
  - the accumulated interval flags prove both states occurred between prints:
    - `rf_seen_low=1`
    - `rf_seen_high=1`
    - `rf_seen_hiz=1`
    - `rf_seen_active=1`
- that confirms the board RF switch is actually being powered down and its
  control pin released between events, then restored again at the prewarm point
- the diagnostics sketch can also be compiled with
  `NRF54L15_BG_DIAG_STOP_AFTER_MS=<ms>` to validate stop-time board-state
  restore
  - validated on March 28, 2026 with `NRF54L15_BG_DIAG_STOP_AFTER_MS=2600`
  - after the sketch printed `background advertiser stopped`, the live board
    state reported `rf_live_power=1` and `rf_live_path=0` (ceramic), matching
    the original pre-advertising setup from `configureBoardForBleLowPower()`
  - that confirms the background advertiser no longer leaves the XIAO RF switch
    collapsed after stop; it restores the sketch-owned board RF state
- the diagnostics sketch can also be compiled with
  `NRF54L15_BG_DIAG_RENAME_AFTER_MS=<ms>` to validate live advertising-data
  mutation while the controller-style scheduler is running
  - validated on March 28, 2026 with `NRF54L15_BG_DIAG_RENAME_AFTER_MS=1800`
  - the sketch printed `background advertiser renamed=1`
  - after the rename-triggered stop/restart, `enabled` returned to `1` and the
    counters resumed at the expected `100 ms` / 3-channel rate with
    `idleFallbackCount = 0`
  - that confirms a live `setAdvertisingName()` update no longer leaves the
    background advertiser stopped or pushes event timing back into the sketch
- the diagnostics sketch can also be compiled with
  `NRF54L15_BG_DIAG_UNSUPPORTED_PDU_AFTER_MS=<ms>` to validate rollback on a
  configuration change that the background advertiser cannot support
  - validated on March 28, 2026 with
    `NRF54L15_BG_DIAG_UNSUPPORTED_PDU_AFTER_MS=1800`
  - the sketch printed `background advertiser unsupported_pdu=0`
  - after that failed `setAdvertisingPduType(kAdvScanInd)` attempt, `enabled`
    still returned to `1` and the counters resumed at the expected
    `100 ms` / 3-channel rate
  - that confirms the old non-connectable background advertiser is restored
    when a sketch-side configuration change fails its restart path
- the diagnostics sketch can also be compiled with
  `NRF54L15_BG_DIAG_SERVICE_UUID_AFTER_MS=<ms>` to validate a composite
  advertising-data update that may also need to populate the scan response
  buffer
  - validated on March 28, 2026 with
    `NRF54L15_BG_DIAG_SERVICE_UUID_AFTER_MS=1800`
  - the sketch first cleared scan response data in `setup()` so the runtime
    `setAdvertisingServiceUuid128()` path had to own both the advertising
    payload rewrite and the fallback scan-response-name placement
  - the sketch printed `background advertiser service_uuid=1`
  - after that timed update, `enabled` returned to `1` and the scheduler
    resumed the expected `100 ms` / 3-channel cadence with `idleFallbackCount = 0`
  - that confirms the two-buffer legacy update no longer leaves the controller
    stopped or half-updated when applied during background advertising
- the diagnostics sketch can also be compiled with
  `NRF54L15_BG_DIAG_RANDOM_DELAY=1` to verify hardware-backed random delay and
  report `rng_hw` / `rng_sw` counts
  - use `compiler.cpp.extra_flags`, not `build.extra_flags`, when adding that
    sketch-only define from `arduino-cli`
  - `build.extra_flags` is the board package's main flag bundle; overriding it
    strips `-DNRF_APPLICATION`, power-mode flags, and BLE-enable flags, which
    produces an invalid image for this target
  - known-good CLI form:

```bash
arduino-cli compile \
  --clean \
  --build-path /tmp/bgdiag_rng_build \
  --fqbn "nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_power=low" \
  --build-property 'compiler.cpp.extra_flags=-include "{build.path}/generated/CoreVersionGenerated.h" -DNRF54L15_BG_DIAG_RANDOM_DELAY=1' \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/Advertising/BleBackgroundAdvertiser3ChannelDiagnostics
```

- when SWD-based validation is preferred over USB serial, the diagnostics sketch
  now mirrors state and counters into `g_ble_bg_diag_shadow`:
  - `[0]` magic `0x42474431`
  - `[1]` setup state
  - `[2]` setup/failure detail
  - `[3]` idle fallback count
  - `[4]` hardware RNG count
  - `[5]` software RNG fallback count
  - `[6]` event arm count
  - `[7]` event complete count
  - `[8]` last random delay in microseconds
  - `[9]` service run count
  - `[10]` stage advance count
  - `[11]` IRQ compare count
  - `[12]` last advertising channel
  - `[13]` current stage
  - `[14]` enabled flag
  - `[15]` three-channel flag
  - `[16]` service runs that observed `POWER.CONSTLATSTAT=1`
  - `[17]` guarded releases back to `TASKS_LOWPWR`
  - `[18]` whether the advertiser is managing latency for this run
  - `[19]` latest sampled `POWER.CONSTLATSTAT`
  - `[20]` prewarm points where hardware had already asserted `CONSTLAT`
  - `[21]` prewarm points where software had to pulse `TASKS_CONSTLAT`
  - `[22]` RF-path restores executed at the prewarm point
  - `[23]` RF-path collapses executed after arming the next event
  - `[24]` whether the advertiser is managing the board RF switch
  - `[25]` latest sampled RF-switch power state captured by the HAL
  - `[26]` live RF-switch power state sampled by the sketch
  - `[27]` live antenna path sampled by the sketch
  - `[28]` whether RF-switch power was observed low since the previous print
  - `[29]` whether RF-switch power was observed high since the previous print
  - `[30]` whether the RF control pin was observed `Hi-Z` since the previous
    print
  - `[31]` whether an active ceramic/external RF path was observed since the
    previous print
  - `[32]` live background-advertising enabled flag
  - `[33]` last stop reason
  - `[34]` TX `READY` observations
  - `[35]` TX kickoff count
  - `[36]` TX compare retry count
  - `[37]` one-time direct-launch fallback count
  - `[38]` TX settle timeouts
  - `[39]` TX `PHYEND` observations
  - `[40]` TX `DISABLED` observations
  - `[41]` last sampled radio state
  - `[42]` clock IRQ count
  - `[43]` `XOTUNED` observations
  - `[44]` `XOTUNEERROR` observations
  - `[45]` `XOTUNEFAILED` observations
  - `[46]` live HFXO running flag
  - `[47]` live HFXO run-requested flag
  - `[48]` whether HFXO was seen low since the previous print
  - `[49]` whether HFXO was seen high since the previous print
- the diagnostics sketch now treats `RENAME_AFTER_MS`,
  `SERVICE_UUID_AFTER_MS`, `UNSUPPORTED_PDU_AFTER_MS`, and `STOP_AFTER_MS` as
  startup-relative delays rather than absolute `millis()` thresholds, because
  the GRTC-backed Arduino timebase on this board can remain monotonic across
  debug resets

On-target SWD validation after flashing the RNG-enabled diagnostics build on
March 28, 2026:

- `g_ble_bg_diag_shadow` showed `state=5` (`advertising running`) with
  `detail=1` (`random delay enabled`)
- after the run settled, the shadow showed:
  - `idleFallbackCount = 0`
  - `randomHardwareCount = 250`
  - `randomFallbackCount = 0`
  - `eventArmCount = 251`
  - `eventCompleteCount = 250`
  - `lastRandomDelayUs = 4510`
  - `serviceRunCount = 751`
- that confirms the optional random advertising delay is being supplied by the
  hardware CRACEN RNG path during the controller-style 3-channel run, without
  falling back to the software LCG in the steady state

On-target register readback after flashing `BleBackgroundAdvertiser3Channel` on
March 28, 2026:

- `DPPIC10.CHEN = 0x00000005`
- `DPPIC20.CHEN = 0x00000005`
- `DPPIC30.CHEN = 0x0000000B`
- `DPPIC10.SUBSCRIBE_CHG[0].EN = 0x80000002`
- `DPPIC10.SUBSCRIBE_CHG[0].DIS = 0x80000001`
- `DPPIC20.SUBSCRIBE_CHG[0].EN = 0x80000002`
- `DPPIC20.SUBSCRIBE_CHG[0].DIS = 0x80000001`
- `DPPIC30.SUBSCRIBE_CHG[0].EN = 0x80000003`
- `DPPIC30.SUBSCRIBE_CHG[0].DIS = 0x80000002`
- `DPPIC10.CHG[0] = 0x00000002`
- `DPPIC20.CHG[0] = 0x00000002`
- `DPPIC30.CHG[0] = 0x00000004`
- `CLOCK.SUBSCRIBE_XOSTART = 0x80000000`
- `CLOCK.SUBSCRIBE_XOSTOP = 0x80000002`
- `RADIO.SUBSCRIBE_TXEN = 0x80000000`
- `RADIO.PUBLISH_DISABLED = 0x80000001`
- `GRTC.PUBLISH_COMPARE[9] = 0x80000003`
- `GRTC.PUBLISH_COMPARE[4] = 0x80000001`
- `GRTC.PUBLISH_COMPARE[8] = 0x80000001`
- `GRTC.PUBLISH_COMPARE[10] = 0x80000001`

That historical readback captured the earlier stop-bridge experiment. The
current runtime still uses hardware publish/subscribe for prewarm, but the
steady-state HFXO stop path has since been simplified to direct cleanup-compare
publishes in LP domain. The default runtime does not rely on the direct TX
publish/subscribe chain on this board; it uses the TX compare IRQ to issue a
minimal `TXEN` kick once HFXO is running.

Host-side BLE discovery:

- confirmed on March 28, 2026 from the local Linux adapter
- `bleak` discovered `D0:AC:F9:59:22:6E | X54-BG-DIAG` while the diagnostics
  sketch was running
- after the late-service reschedule fix, `bleak` discovered
  `D0:AC:F9:59:22:6E | X54-BG-1CH` from the plain
  `BleBackgroundAdvertiserSingleChannel` example
- after reflashing the plain runtime image, `bleak` discovered
  `D0:AC:F9:59:22:6E | X54-BG-3CH`
- `bluetoothctl` also reported `Device D0:AC:F9:59:22:6E X54-BG-DIAG`
- that closes the functional gap for this milestone: the controller-style
  scheduler is no longer just advancing counters, it is advertising over the
  air independently of sketch-loop timing
- after switching the TX compare path to nonblocking retry and validating the
  repo-local library build on March 28, 2026:
  - `bleak` discovered `D0:AC:F9:59:22:6E | X54-BG-1CH` from the plain
    single-channel runtime built with `--library` pointing at this repo
  - `bleak` discovered `D0:AC:F9:59:22:6E | X54-BG-3CH` from the plain
    3-channel runtime built with `--library` pointing at this repo
  - with the 3-channel diagnostics sketch rebuilt at `HFXO_LEAD_US=50`,
    `bleak` still discovered `D0:AC:F9:59:22:6E | X54-BG-DIAG`
- after validating the hardware-first experimental path and its automatic
  fallback on March 28, 2026:
  - the diagnostics build with direct launch enabled showed
    `tx_kick_fallback=1` while `txReadyCount`, `txPhyendCount`, and
    `txDisabledCount` kept advancing with `stop_reason=0`
  - that proved the runtime could self-recover from the non-functional direct
    launch chain on the connected XIAO instead of requiring a compile-time
    rescue
  - `bleak` discovered `D0:AC:F9:59:22:6E | X54-BG-DIAG` during that
    auto-fallback run
  - serial diagnostics at `HFXO_LEAD_US=50` showed `txKickRetryCount = 7`
    while `txReadyCount`, `txPhyendCount`, and `txDisabledCount` all kept
    advancing, confirming that the no-spin TX compare retry path was active and
    still delivering over-the-air advertising
- after adding a session-local direct-launch blacklist and validating with a
  startup-relative timed rename restart on March 28, 2026:
  - the first run still showed `tx_kick_fallback=1`, proving the direct chain
    was tried once
  - the diagnostics sketch then printed `background advertiser renamed=1`
  - after that transactional stop/restart, counters restarted from low values
    but `tx_kick_fallback=0` while `txReadyCount`, `txPhyendCount`, and
    `txDisabledCount` advanced normally
  - that confirms later background-advertiser restarts in the same boot do not
    re-pay the known-bad direct-launch experiment on the connected XIAO
  - after those changes, both plain runtime examples were revalidated over the
    air again:
    - `D0:AC:F9:59:22:6E | X54-BG-1CH`
    - `D0:AC:F9:59:22:6E | X54-BG-3CH`
- after comparing this with Zephyr's official Nordic clock-control code on
  March 28, 2026:
  - Zephyr's `drivers/clock_control/clock_control_nrf.c` also treats HFXO
    readiness through the `NRFX_CLOCK_EVT_XO_TUNED` event path rather than a
    pure publish/subscribe gate
  - that aligns with the repo's functional XIAO default: keep the hardware
    schedule, but use the tuned-event / IRQ path for the final TX kickoff
    decision on this board
- after reducing the default TX compare IRQ path to a minimal kickoff on March
  28, 2026, SWD shadow validation on the diagnostics sketch showed:
  - `eventArmCount = 186`
  - `eventCompleteCount = 185`
  - `serviceRunCount = 555`
  - `irqCompareCount = 555`
  - `txReadyCount = 554`
  - `txPhyendCount = 554`
  - `txDisabledCount = 554`
  - `txSettleTimeoutCount = 0`
  - `stop_reason = 0`
- that confirms the current default path no longer busy-waits through the
  packet in the compare IRQ, while still completing the 100 ms 3-channel run
  correctly on the connected XIAO
- after extending the single-channel API to own optional random delay on March
  28, 2026:
  - the single-channel diagnostics sketch rebuilt with
    `NRF54L15_BG_DIAG_SINGLE_CHANNEL=1` and `NRF54L15_BG_DIAG_RANDOM_DELAY=1`
  - serial showed the background single-channel scheduler completing normally
    with `idle=0`, `rng_hw` increasing, `rng_sw=0`, `txReadyCount`,
    `txPhyendCount`, and `txDisabledCount` all increasing, and `stop_reason=0`
  - `bleak` also discovered `D0:AC:F9:59:22:6E | X54-BG-1CH` during that
    randomized single-channel run
  - that closes the single-channel parity gap: both legacy background
    advertising entry points can now own Bluetooth-style random advertising
    delay without sketch-loop timing and with CRACEN hardware entropy in the
    steady state
- the pure `GRTC -> DPPI/PPIB -> RADIO.TXEN` launch experiment was re-tested on
  March 28, 2026 with:
  - `SHORTS.READY_START` added alongside `SHORTS.TXREADY_START`
  - the default `1200 us` HFXO lead
  - an exaggerated `3000 us` HFXO lead
  - the connected XIAO still stopped before a successful first packet, with the
    diagnostics image reporting `live_enabled=0` and `stop_reason=2`
  - the local datasheet exposes `CLOCK.EVENTS_XOTUNED` but does not expose a
    publish register for it, so there is no primary-source-backed hardware
    path here to gate `RADIO.TXEN` directly on `XOTUNED`
  - because of that boundary, the compare-IRQ-assisted TX kickoff remains the
    current functional default
- the direct-launch experiment was pushed one step further later on March 28,
  2026 using the datasheet-style PERI relay split and radio-domain PLL gating:
  - `DPPIC20 ch5` relayed `PPIB22.PUBLISH_RECEIVE[0] ->
    PPIB21.SUBSCRIBE_SEND[0]`
  - `PPIB11.PUBLISH_RECEIVE[0]` drove `RADIO.SUBSCRIBE_PLLEN`
  - `RADIO.PUBLISH_PLLREADY` drove `RADIO.SUBSCRIBE_TXEN`
  - live secure-alias register reads showed the bridge was armed:
    - `DPPIC10.CHEN = 0x00000003`
    - `DPPIC20.CHEN = 0x00000020`
    - `DPPIC30.CHEN = 0x00000007`
    - `RADIO.SUBSCRIBE_PLLEN = 0x80000001`
    - `RADIO.SUBSCRIBE_TXEN = 0x80000000`
    - `RADIO.PUBLISH_PLLREADY = 0x80000000`
  - but the functional result still failed:
    - `RADIO.EVENTS_PLLREADY = 0`
    - diagnostics kept `txReadyCount = 0` and `txPhyendCount = 0`
    - `bleak` did not discover the direct-launch sketch over the air
  - that confirms the direct bridge remains experimental and non-functional on
    the connected XIAO, even after moving the final readiness gate inside the
    radio domain
- after replacing the old `RADIO.DISABLED -> XOSTOP` bridge with direct
  cleanup-compare `XOSTOP` publishes and validating on March 28, 2026:
  - the 3-channel diagnostics sketch reported `xo_running=0` on its periodic
    prints while still showing `xo_seen_low=1` and `xo_seen_high=1`
  - `clk_irq` and `clk_tuned` advanced once per advertising event, which
    matches HFXO being restarted for each event instead of staying high across
    the gap
  - the same diagnostics run kept `idle=0`, `txKickRetryCount=0`,
    `txReadyCount`, `txPhyendCount`, and `txDisabledCount` advancing normally,
    so the cleanup stop fix did not break the radio schedule
  - after that fix, `bleak` discovered both plain repo-local runtime examples
    again:
    - `D0:AC:F9:59:22:6E | X54-BG-1CH`
    - `D0:AC:F9:59:22:6E | X54-BG-3CH`
  - that closes the remaining functional gap in this milestone: the plain
    background advertiser now maintains cadence independently of `loop()`,
    sleeps with HFXO down between events, and still advertises over the air on
    the connected XIAO

Automated stability regression on March 28, 2026:

- a repeatable regression harness now lives at
  `scripts/ble_background_advertiser_regression.py`
- it compiles the repo-local diagnostics sketch variants with `arduino-cli`,
  flashes them to the connected XIAO, resets the board with `pyocd`, captures
  serial diagnostics, performs host-side `bleak` scans, and writes a machine-
  readable result bundle under `measurements/`
- the first saved matrix result is
  `measurements/background_advertiser_regression_20260328_200709/summary.json`
- the executed matrix was:
  - `3ch-default`: `40 s` soak, `4` reboot cycles, target name
    `X54-BG-DIAG`
  - `3ch-random`: `30 s` soak, `3` reboot cycles, target name
    `X54-BG-DIAG`
  - `1ch-default`: `30 s` soak, `3` reboot cycles, target name `X54-BG-1CH`
  - `1ch-random`: `30 s` soak, `3` reboot cycles, target name `X54-BG-1CH`
- every case passed:
  - `serial_ok = true`
  - `scan_ok = true`
  - `stop_reason = 0`
  - `tx_settle_to = 0`
  - `clk_err = 0`
  - `clk_fail = 0`
  - `idle = 0`
- the random-delay cases also confirmed the intended entropy split:
  - `rng_hw > 0`
  - `rng_sw = 0`
- the default cases confirmed the current XIAO runtime is staying on the
  proven default path without emergency recovery:
  - `tx_kick_fallback = 0` through the soak and reboot cycles
- after the matrix completed, the plain `BleBackgroundAdvertiser3Channel`
  runtime was reflashed and `bleak` again discovered
  `D0:AC:F9:59:22:6E | X54-BG-3CH`

## Next Milestones

1. Clean up the advertising API around background-set configuration.
2. Assess whether the direct `GRTC -> DPPI/PPIB -> RADIO.TXEN` chain can fully
   replace compare-IRQ TX kickoff on this board without losing reliability.
3. Assess extended advertising / AUX feasibility on top of the same scheduler.
