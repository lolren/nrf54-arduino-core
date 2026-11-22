# nRF54 Arduino Core 1.0.24

This patch adds explicit nPM1300 charger-error recovery and improves the
fuel-gauge example's fault reporting. It incorporates
[PR #123](https://github.com/lolren/nrf54-arduino-core/pull/123) by
[@bongo505](https://github.com/bongo505), preserving the original contributor
commit and its authorship.

## Charger Recovery

- Correct the name of charger task `0x00`: it releases the charger from its
  error state; it does not clear the error registers.
- Add `npm1300_charger_clear_error()` for release and latched-error clearing,
  and `npm1300_charger_clear_safety_timers()` for explicit timer reset. Normal
  charger enable never clears safety timers implicitly.
- Check charger-preparation writes and stop on failure rather than enabling
  after a failed I2C step. Refuse configuration when the enabled state cannot
  be read, and invalidate cached measurements before charger mutations.
- Report charger errors even when battery-current measurements fail. Keep
  bounded trickle-timeout retry behind the default-off
  `NPM1300_FUEL_GAUGE_TRICKLE_RETRY` option.

Resetting safety timers renews **both** trickle and charge limits. The example
does not infer cell safety from voltage alone. Opt-in retries require valid
raw voltage, the restricted error/status conditions and healthy VBUS, with
five attempts per RAM-only budget and a 60-second interval. Rebooting or
observing USB removal resets that budget; it is not a durable per-connection
safety limit. See the
[fuel-gauge recovery guide](https://github.com/lolren/nrf54-arduino-core/blob/v1.0.24/docs/nPM1300_FUEL_GAUGE.md#trickle-timeout-recovery)
before enabling this option.

## Validation And Limits

Focused regression coverage exercises production charger task ordering,
I2C failures, cache invalidation, default-disabled recovery, opt-in guards,
retry accounting and timer rollover. The fuel-gauge example is compile-checked
with recovery disabled and enabled against Nordic's 2.0.0 secondary-cell
soft-float library; the library and battery model remain external dependencies.

The contributor reported successful flat-cell recovery and subsequent bench
testing in PR #123. No new maintainer battery-charging or trickle-timeout
hardware test is claimed for this release. No BLE controller, radio-timing or
host-tool binary changes are included.

## Install

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.24"
```

PlatformIO projects can pin `v1.0.24` or use the complete
`platform-nrf54l15clean-1.0.24-<hash>.tar.bz2` asset URL from this release.
See the [PlatformIO guide](https://github.com/lolren/nrf54-arduino-core/blob/v1.0.24/docs/PLATFORMIO.md).

[Changes since v1.0.23](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.23...v1.0.24)
