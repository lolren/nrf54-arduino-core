# Channel Sounding Calibration

The clean Arduino channel-sounding examples now expose a simple calibration loop for the
phase-based ranging path.

This does **not** make the raw estimate physically perfect by itself. It gives you a clean
way to remove the obvious board/link bias that shows up even when the underlying phase fit
is stable.

## Runtime Commands

Use the initiator example:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingInitiator/BleChannelSoundingInitiator.ino`

Open the serial console at `115200` and send:

- `status`
- `clear`
- `zero`
- `ref <meters>`
- `offset <meters>`
- `scale <factor>`

Meaning:

- `zero`: force the current raw phase estimate to `0 m`
- `ref <meters>`: set the offset so the current raw phase estimate matches a known distance
- `offset <meters>`: apply a fixed additive correction
- `scale <factor>`: apply a multiplicative correction before the offset
- `clear`: restore `scale=1.0` and `offset=0.0`

If host-to-target serial RX is unavailable on your ACM port, set these defaults directly in the
initiator sketch instead:

- `kCalibrationScaleDefault`
- `kCalibrationOffsetMetersDefault`

The initiator log now keeps the raw and calibrated values separate:

- `dist_m/cm/mm`: calibrated display distance
- `phase_raw_m`: raw phase-slope estimate from the clean-core fit
- `phase_cal_m`: calibrated phase estimate after scale/offset
- `median_raw_m` / `median_cal_m`: rolling-median raw and calibrated phase distances
- `tone_quality`: median tone-pair quality score used before the phase fit
- `tone_total` / `tone_used`: tone pairs seen vs tone pairs kept for the final fit
- `reject_quality` / `reject_residual`: tone pairs dropped by quality gating and residual gating
- `fit_delta_m`: difference between the robust slope fit and the quality-weighted refinement
- `accepted_sweeps`: estimates accepted into the rolling display median
- `display_ok`: whether the current estimate passed the display gate

## Host-Side Fitting Tool

Use:

- [`scripts/channel_sounding_calibration.py`](/home/lolren/Desktop/Nrf54L15/repo/scripts/channel_sounding_calibration.py)

Single-log summary with a known reference distance:

```bash
python3 scripts/channel_sounding_calibration.py analyze capture.txt \
  --metric phase \
  --reference-distance 0.500
```

That prints robust summary stats and suggests:

- `recommended_offset_m`
- `single_point_scale`

For a multi-point fit, create a CSV like:

```csv
actual_m,log_path
0.200,logs/20cm.txt
0.500,logs/50cm.txt
1.000,logs/100cm.txt
```

Then run:

```bash
python3 scripts/channel_sounding_calibration.py fit calibration_points.csv
```

It prints:

- `recommended_scale`
- `recommended_offset_m`
- the matching serial commands to apply in the live initiator example
- the matching `kCalibrationScaleDefault` / `kCalibrationOffsetMetersDefault` constants if you
  prefer compile-time calibration

## Notes

- For clean-core captures, the script prefers `phase_raw_m=` and falls back to `phase_m=`.
- For Zephyr/NCS reference logs, the same script can also parse `Phase-Based Ranging method: ...`
  lines.
- If `reject_quality` or `reject_residual` stays high, fix placement or RF quality first and only
  then recalibrate.
- If `display_ok=0`, the raw estimate is still logged for debugging, but it is intentionally kept
  out of the displayed rolling distance.
- If the residual stays high or valid-channel count collapses, calibration will not fix the
  underlying RF quality problem. It only corrects stable bias.
