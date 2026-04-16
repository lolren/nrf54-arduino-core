# Channel Sounding 20 cm Validation

This note captures the current measured one-point calibration run for the real
two-board phase-based Arduino Channel Sounding path.

Session:

- date: 2026-04-16
- boards: `XIAO nRF54L15 / Sense` initiator + reflector
- spacing: approximately `0.20 m`
- path: `BleChannelSoundingInitiator` + `BleChannelSoundingReflector`
- build option: `clean_vpr=off`

## Baseline

Measured from:

- `.build/cs_20cm_baseline.log`

Using:

```bash
python3 scripts/channel_sounding_calibration.py analyze .build/cs_20cm_baseline.log \
  --metric distance \
  --reference-distance 0.200
```

Observed baseline summary:

- samples: `9`
- measured median: `0.3944 m`
- MAD: `0.1347 m`
- recommended offset: `-0.1944 m`
- board-pair bias: `+0.1944 m`
- board-pair equivalent delay: `+0.6484 ns`
- symmetric per-board equivalent delay: `+0.3242 ns`

That means the current board pair and bench geometry were over-reporting the
20 cm spacing by about 19.4 cm before calibration.

## Calibrated Check

Applied live on the initiator:

```text
offset -0.1944
```

Measured from:

- `.build/cs_20cm_calibrated.log`

Using:

```bash
python3 scripts/channel_sounding_calibration.py analyze .build/cs_20cm_calibrated.log \
  --metric distance \
  --reference-distance 0.200
```

Observed calibrated summary:

- samples: `7`
- measured median: `0.2099 m`
- MAD: `0.0365 m`

Representative calibrated console lines:

```text
t=81796 ... dist_m=0.1942 ... median_cal_m=0.1942 ...
t=91721 ... dist_m=0.2099 ... median_cal_m=0.2099 ...
```

## What This Proves

- the repo now has a measured physical-distance calibration profile flow tied
  to a real two-board capture, not only ad-hoc manual `offset` changes
- the current phase-based path can be anchored to a real known spacing and
  brought close to that spacing on the same board pair and bench setup
- for the active `XIAO nRF54L15 + XIAO nRF54L15` ceramic-antenna pair, the repo
  now has a checked-in distance-equivalent board-pair delay characterization

## What This Does Not Prove

- it is not a full multi-point range calibration
- it is not a factory-safe board-agnostic antenna-delay constant
- it does not prove that other supported boards share the same delay terms
- it does not close the remaining physically defensible ranging work
