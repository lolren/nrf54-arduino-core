# Low-Power BLE Patterns

This note captures the three BLE operating modes now implemented in the Arduino core
for the XIAO nRF54L15, along with the helper APIs that make the board-specific
current savings reproducible.

## The Board-Level Lever That Mattered

On XIAO nRF54L15, the external RF switch path itself costs current if left powered.
For BLE sketches, the important board-specific pattern is:

1. power/select the RF path only for the active TX/RX window
2. release `RF_SW_CTL` to high-impedance when idle
3. turn off RF switch power on `P2.03` while idle

Those steps are now exposed as library APIs instead of sketch-local glue:

```cpp
BoardControl::enableRfPath(BoardAntennaPath::kCeramic);
BoardControl::collapseRfPathIdle();
BoardControl::setImuMicEnabled(false);
BoardControl::enterLowestPowerState();
PowerManager power;
power.systemOffTimedWakeMsNoRetention(1000);
```

The explicit `NoRetention` helpers are for the lowest-current sketches. The
default `systemOff*()` helpers preserve `.noinit` RAM so retained state and BLE
bond data are not discarded implicitly.

The low-power BLE examples also honor Arduino Tools -> `BLE LP Example Preset`,
which injects compile-time overrides for:

- `NRF54L15_CLEAN_BLE_LP_ADV_INTERVAL_MS`
- `NRF54L15_CLEAN_BLE_LP_BURST_EVENTS`
- `NRF54L15_CLEAN_BLE_LP_BURST_GAP_US`
- `NRF54L15_CLEAN_BLE_LP_BURST_PERIOD_MS`
- `NRF54L15_CLEAN_BLE_LP_SYSTEMOFF_MS`

## Mode 1: Continuous Advertising With RF-Switch Duty-Cycling

Example:

- `examples/BLE/BleAdvertiserRfSwitchDutyCycle/BleAdvertiserRfSwitchDutyCycle.ino`

Intent:

- stay continuously discoverable
- remain in `System ON`
- collapse only the board RF switch path between advertising events

Tradeoff:

- best mode when the device should remain easy to discover
- higher average current than a burst beacon because the system stays awake enough to keep advertising cadence alive

Observed result in local testing:

- user-measured around `0.1 mA` with the validated sketch configuration
- use `BLE LP Example Preset -> Scan-Friendly` if you want a shorter advertising cadence without editing the sketch

## Mode 2: Hybrid Burst Advertising In System ON

Example:

- `examples/BLE/BleAdvertiserHybridDutyCycle/BleAdvertiserHybridDutyCycle.ino`

Intent:

- send short bursts instead of a single event cadence
- stay in `System ON`
- collapse the RF path and idle with `WFI` between bursts

Why it exists:

- lower average current than a persistent advertiser
- easier to detect than a burst-plus-`SYSTEM OFF` design
- no cold boot on every burst

Default sketch profile:

- `2` events per burst
- `20 ms` gap inside the burst
- one burst every `10 s`
- `-10 dBm`
- `ADV_IND`

Status:

- compiled successfully in this turn
- reflashed and revalidated on hardware in this turn as `X54-HYBRID`

## Mode 3: Burst Advertising Plus Timed SYSTEM OFF

Example:

- `examples/BLE/BleAdvertiserBurstSystemOff/BleAdvertiserBurstSystemOff.ino`

Intent:

- advertise briefly
- shut down board-side RF path
- enter true timed `SYSTEM OFF`
- cold boot on wake and repeat

Tradeoff:

- lowest average current
- not continuously visible to scanners
- visibility depends on burst density and scan timing

Observed result in local testing:

- user-measured in the `tens of uA`

Validated baseline:

- `6` events per wake
- `20 ms` burst gaps
- `1000 ms` timed `SYSTEM OFF`
- `clean_power=low`
- default boot profile
- use `BLE LP Example Preset -> Beacon 5 s` if you want a sparser default wake cadence without editing the sketch

## Choosing A Mode

Use mode 1 when:

- you need practical continuous discoverability
- `~100 uA` class current is acceptable

Use mode 2 when:

- you want a middle ground
- scanners should still have a reasonable chance to catch advertisements
- you do not want a cold boot every cycle

Use mode 3 when:

- the lowest average current matters more than continuous discoverability
- burst beaconing is acceptable
- cold-boot behavior on every cycle is acceptable

## Important Constraint

These patterns are still built on the raw Arduino BLE path in this core, not a full
Zephyr-style BLE controller scheduler. The power floor now comes from matching the
board-level RF gating and `SYSTEM OFF` behavior, not from controller parity.
