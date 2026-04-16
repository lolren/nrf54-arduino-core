# Channel Sounding Error Model

This note explains what the current clean-core Channel Sounding numbers mean,
which outputs are physically relevant, and which ones are only bring-up or
nominal regression surfaces.

Scope:

- this applies to the current shipped clean-core Channel Sounding examples
- it covers the two-board phase-based ranging path and the newer VPR nominal
  runtime summaries
- it does not claim Bluetooth-qualified interoperability or production-grade
  ranging accuracy

## Output Surfaces

There are two different kinds of CS outputs in the repo today.

### Real phase-based two-board path

Examples:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingInitiator`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingReflector`

Relevant fields:

- `dist_m/cm/mm`
- `phase_raw_m`
- `phase_cal_m`
- `median_raw_m`
- `median_cal_m`
- `tone_quality`
- `tone_total`
- `tone_used`
- `reject_quality`
- `reject_residual`
- `fit_delta_m`
- `accepted_sweeps`
- `display_ok`

This is the only current path that should be treated as a real physical ranging
surface.

### Nominal synthetic VPR summaries

Examples:

- `BleChannelSoundingVprServiceNominal`
- `BleChannelSoundingVprLinkedInitiator`
- `VprBleConnectionCsProcedureProbe`

Relevant fields:

- `nominal_dist_m`
- `summary=`
- `steps=`
- `modes=`
- `ch=`
- `hash=`

These are controller/runtime regressions. They are useful for transport,
workflow, and controller-state validation. They are **not** physical distance
claims.

## What The Real Two-Board Phase Path Is Doing

At a high level:

1. the initiator and reflector exchange tone-based CS data across BLE channels
2. the initiator gathers local and peer phase information
3. low-quality and inconsistent tones are rejected
4. a phase-slope fit is performed across the kept tones
5. the result is optionally corrected by user calibration
6. a rolling median is used for display stability

That means the displayed distance is not one raw radio sample. It is already a
filtered and gated estimate.

## Main Error Sources

### Stable board/link bias

Typical causes:

- antenna delay mismatch
- board routing delay
- analog front-end delay
- constant fixture/wiring bias

Symptoms:

- `phase_raw_m` is consistently offset from reality
- `fit_delta_m` stays small
- `tone_used` stays healthy
- `display_ok=1`

Action:

- use the calibration flow in `docs/channel-sounding-calibration.md`
- if you need a concrete measured example, start with
  `docs/channel-sounding-20cm-validation.md`

### Multipath and placement

Typical causes:

- nearby reflective surfaces
- antenna orientation changes
- hand/body proximity
- short-range bench clutter

Symptoms:

- `reject_residual` rises
- `fit_delta_m` becomes unstable
- median values drift more than raw bias would explain

Action:

- fix placement first
- then recalibrate only after the path is stable again

### Weak or inconsistent tone set

Typical causes:

- poor SNR
- marginal reflector capture
- too many unusable tones
- RF path mismatch

Symptoms:

- `tone_total` is normal but `tone_used` is low
- `reject_quality` is high
- `display_ok=0`

Action:

- do not trust the displayed distance
- improve RF conditions before changing calibration

### Median/display gating effects

The displayed fields are intentionally more conservative than the raw fit.

Implications:

- `phase_raw_m` can still move when `dist_m` is held stable
- `display_ok=0` means the raw estimate is still logged, but the display path
  is intentionally refusing to trust it
- `median_*` values describe the accepted estimate history, not every raw sweep

## Field Interpretation

### `phase_raw_m`

Raw phase-slope estimate before user calibration.

Use it for:

- bias analysis
- calibration fitting
- comparing placements

Do not use it as the final user-facing distance without calibration.

### `phase_cal_m`

Raw phase estimate after `scale` and `offset`.

Use it for:

- post-calibration comparison to known distances
- tuning a stable setup

### `median_raw_m` and `median_cal_m`

Rolling display median over accepted sweeps.

Use them for:

- stable user-facing output
- comparing repeated runs

Do not confuse them with instantaneous radio latency or one-shot phase samples.

### `tone_quality`

Median tone-pair quality score before the final fit.

Higher is better. This is a quality indicator, not a distance indicator.

### `tone_total` and `tone_used`

- `tone_total`: tones seen
- `tone_used`: tones kept for the fit

Healthy behavior means `tone_used` stays close enough to `tone_total` for a
stable fit. A collapsing `tone_used` count means the estimate is losing real
support.

### `reject_quality`

Tones dropped before the fit because they failed quality gating.

If this rises materially, calibration is not the first fix.

### `reject_residual`

Tones dropped because they do not fit the current slope model well.

This is a strong clue for multipath or unstable placement.

### `fit_delta_m`

Difference between the robust slope fit and the quality-weighted refinement.

Interpretation:

- small and stable: the fit is coherent
- large or noisy: the path is unstable even if one final number still prints

### `accepted_sweeps`

How many estimates passed the display gate and entered the rolling median.

If this stalls while raw logs keep printing, the system is still collecting
data but refusing to trust it for display.

### `display_ok`

Top-level trust flag for the displayed distance.

Practical rule:

- `display_ok=1`: estimate passed the current display gate
- `display_ok=0`: treat the output as diagnostic only

## Trust Rules

Treat the current two-board phase output as usable only when all of these are
true:

- `display_ok=1`
- `tone_used` is not collapsing relative to `tone_total`
- `reject_quality` is not persistently high
- `reject_residual` is not persistently high
- `fit_delta_m` is small and stable
- repeated placements at the same real distance converge to a narrow band

If those conditions are not true, calibration is not the right first move.

## What Calibration Can And Cannot Fix

Calibration can fix:

- stable additive bias
- stable multiplicative scale error

Calibration cannot fix:

- multipath
- poor tone quality
- unstable placement
- missing tones
- controller/runtime nominal synthetic outputs

## Current Repo Position

What is already true:

- the two-board phase path is a real hardware ranging bring-up surface
- the generic-service and linked VPR summaries are strong controller/runtime
  regressions
- the repo now has a measured latency note for those BLE/CS runtime paths
- the active `XIAO + XIAO` CS pair now has a checked-in board-pair RF / delay
  characterization note:
  [`channel-sounding-board-pair-characterization.md`](/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/docs/channel-sounding-board-pair-characterization.md)

What is still not done:

- a physically defensible calibrated distance model across multiple measured
  points and boards
- measured power characterization

That is why the checklist treats calibration/error documentation separately
from full physical calibration completion.
