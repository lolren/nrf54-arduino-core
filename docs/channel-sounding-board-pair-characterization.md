# Channel Sounding Board-Pair RF Characterization

This note records the current checked-in RF / delay characterization for the
active two-board Arduino Channel Sounding path.

Current characterized pair:

- initiator: `XIAO nRF54L15 / Sense`
- reflector: `XIAO nRF54L15 / Sense`
- antenna path: onboard ceramic antenna on both boards
- spacing used for characterization: approximately `0.20 m`
- source capture: [`channel-sounding-20cm-validation.md`](/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/docs/channel-sounding-20cm-validation.md)

## Characterized Terms

From the measured baseline median:

- actual spacing: `0.2000 m`
- measured median: `0.3944 m`
- board-pair bias: `+0.1944 m`

Converted to a distance-equivalent delay term:

- board-pair equivalent delay: `+0.6484 ns`
- symmetric per-board equivalent delay: `+0.3242 ns`

That symmetric split is only a practical estimate for this same-board pair. It
is useful because both boards are the same model and antenna path, but it is
not a claim that each board has been independently instrumented.

## Checked-In Artifacts

- [`channel-sounding-profiles/xiao_nrf54l15_pair_20cm.json`](/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/docs/channel-sounding-profiles/xiao_nrf54l15_pair_20cm.json)
- [`channel-sounding-profiles/xiao_nrf54l15_pair_20cm.h`](/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/docs/channel-sounding-profiles/xiao_nrf54l15_pair_20cm.h)

Those artifacts now carry:

- `board_pair_bias_m`
- `board_pair_equivalent_delay_ns`
- `symmetric_per_board_delay_ns`
- `validated_median_m`
- `validated_mad_m`
- `validated_p90_abs_error_m`

## Zephyr / NCS Cross-Reference

The same analyzer path also accepts Zephyr controller-backed ranging logs by
parsing lines like:

```text
Phase-Based Ranging method: ...
```

That matters because it keeps the characterization format stack-agnostic:

- the local Arduino phase-based path can be characterized with the same script
- future or parallel Zephyr `connected_cs` captures on the same boards can be
  summarized into the same distance / delay units

Reference:

- [`zephyr-channel-sounding-validation.md`](/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/docs/zephyr-channel-sounding-validation.md)

## Boundary

What this closes:

- board-pair RF / delay characterization for the currently used `XIAO + XIAO`
  Channel Sounding setup
- the bounded physical-output surface for that same active board pair

What this does not close:

- multi-point physically defensible ranging
- board-independent factory constants
- equivalent characterization for `HOLYIOT-25007`, `HOLYIOT-25008`, or mixed
  board pairs
