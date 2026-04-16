# Channel Sounding Physical Output

This repo now exposes a bounded physical-distance surface for the active
`XIAO nRF54L15 / Sense + XIAO nRF54L15 / Sense` bench profile.

That is intentionally narrower than a blanket claim that every board or every
distance is solved.

## What Changed

The initiator example now supports:

- `profile xiao20cm`
- `profile clear`

When the measured `xiao20cm` profile is active, the log exposes:

- `phys_m`
- `phys_pm_m`
- `phys_p90_m`
- `phys_lo_m`
- `phys_hi_m`
- `phys_ok`

Meaning:

- `phys_m`: calibrated physical-distance display value
- `phys_pm_m`: typical half-width from the validated profile MAD
- `phys_p90_m`: conservative half-width from the validated profile p90 absolute error
- `phys_lo_m` / `phys_hi_m`: conservative lower/upper bounds using `phys_p90_m`
- `phys_ok`: whether the current displayed value is inside the validated physical-output path

The physical path is intentionally stricter than the raw display path:

- it requires a validated calibration profile
- it suppresses degenerate `0 m` outputs instead of pretending they are valid
- it suppresses outputs that fall outside the validated physical band for the
  active profile

## Current Validated Profile

Profile:

- [`channel-sounding-profiles/xiao_nrf54l15_pair_20cm.json`](/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/docs/channel-sounding-profiles/xiao_nrf54l15_pair_20cm.json)

Validation numbers from the checked-in 20 cm run:

- reference distance: `0.2000 m`
- validated sample count: `6`
- validated median: `0.2282 m`
- validated MAD: `0.0234 m`
- validated p90 absolute error: `0.1283 m`

This means the current physically defensible surface is:

- a calibrated XIAO/XIAO near-field bench output
- with explicit typical and conservative bounds
- not a claim of board-agnostic or multi-meter ranging accuracy

## Boundary

What this closes:

- the repo no longer prints only an unbounded calibrated distance for the active
  characterized pair
- the active pair now has a bounded physical output path rooted in measured
  validation data
- when the live estimate leaves the validated band, the repo now reports
  `phys_ok=0` instead of pretending the output is still physically validated

What this does not close:

- measured power characterization
- multi-point calibration across wider distances
- equivalent bounded profiles for other supported board pairs
