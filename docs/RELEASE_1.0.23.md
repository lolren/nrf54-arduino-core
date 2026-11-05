# nRF54 Arduino Core 1.0.23

This release expands XIAO nRF54LM20A nPM1300 power management and corrects
timer reconfiguration that could reuse an earlier timeout. It includes
[PR #120](https://github.com/lolren/nrf54-arduino-core/pull/120) by
[@bongo505](https://github.com/bongo505), with all eight original contributor
commits preserved.

## nPM1300 Features

- Expose PMIC GPIO0 on P1.25 and GPIO1 on P1.26 as `PIN_PMIC_GPIO0` and
  `PIN_PMIC_GPIO1`, including MCU interrupt support.
- Add watchdog start, kick and stop APIs, wake-button debounce configuration,
  first-latched PMIC reset-cause access, and charge-positive/discharge-negative
  battery-current readings.
- Add `Power/nPM1300_GPIO0_irq`, `Power/nPM1300_watchdog` and
  `Power/nPM1300_Fuel_Gauge` examples, plus
  [power-management](https://github.com/lolren/nrf54-arduino-core/blob/v1.0.23/docs/nPM1300_POWER_MANAGEMENT.md)
  and [fuel-gauge integration](https://github.com/lolren/nrf54-arduino-core/blob/v1.0.23/docs/nPM1300_FUEL_GAUGE.md)
  guides.
- Keep Nordic's Fuel Gauge library and the battery model as external
  dependencies. Neither is bundled or redistributed by this core.

## Fixes

- Load and strobe the shared timer target, allow 1 ms for it to settle, and
  only then select the timer mode. A previously configured status bit does
  not prove that a new target has reached the slow clock domain.
- Check watchdog shutdown before Ship or USB System OFF entry. Hibernate
  stops incompatible timer modes without cancelling a prepared wake timer.
- Arm the MCU interrupt before enabling PMIC events, and retry pending IRQ
  work after I2C failures or a missed edge.
- Correct the fuel-gauge sketch filename, stale documentation calls and
  calibration-offset sign. Clarify that SHPHLD is the separate TP21 pad,
  not the onboard USER button on P0.09.

## Validation And Limits

USB testing on one LM20A passed GPIO0 interrupt delivery using
software-latched PMIC events, GPIO1 levels, debounce write/readback and
restoration, VBUS Ship/Hibernate rejection, watchdog kick/stop, and real
watchdog reset with cause `0x04`. A warm transition previously made a
requested 4096 ms watchdog reuse a 2048 ms wake target. After the fix, two
repeated warm transitions reset after about 4.1 s. USB System OFF woke through
GRTC with no PMIC watchdog reset.

The three examples compile with ARM GCC 7.2.1. The enabled fuel-gauge branch
also compiles and links against Nordic's real 2.0.0 secondary-cell soft-float
library. That compile used a placeholder model, not a runtime battery test.
Executable regressions cover timer ordering, sticky configured status,
warm transitions, sleep guards and bus failures; core I/O sanitizer checks
also passed.

Maintainer testing did not validate battery-powered Ship/Hibernate timing or
current, charging, fuel-gauge accuracy, or a physical external SHPHLD switch.
The contributor reported battery testing with a 400 mAh Li-Po in PR #120;
those reports are separate from the USB validation above. This release makes
no BLE controller or radio-timing changes.

## Install

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.23"
```

PlatformIO projects can pin `v1.0.23` or use the complete
`platform-nrf54l15clean-1.0.23-<hash>.tar.bz2` asset URL from this release.
See the [PlatformIO guide](https://github.com/lolren/nrf54-arduino-core/blob/v1.0.23/docs/PLATFORMIO.md).

[Changes since v1.0.22](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.22...v1.0.23)
