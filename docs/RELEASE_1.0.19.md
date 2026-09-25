# nRF54 Arduino Core 1.0.19

This patch addresses unnecessary BLE startup work and PMIC telemetry state
changes identified while investigating [issue #114](https://github.com/lolren/nrf54-arduino-core/issues/114).

## Changes

- `Bluefruit.begin()` no longer generates a software P-256 pairing key through
  the IO-capability setter. Advertising-only sketches avoid that computation.
  LE Secure Connections negotiation and explicit OOB preparation still generate
  keys when needed, with the existing BLE cooperation hook serving the link.
- nPM1300 telemetry reads save and restore the previous IBAT-enable and ADC
  configuration. Every failure after setup attempts both restores, and failed
  acquisition or cleanup cannot populate the measurement cache. Applications
  that explicitly enable automatic measurements retain their settings.
- `npm1300_is_present()` only probes the device; it no longer enables ADC
  measurement modes as a side effect. Charger temperature-monitoring behavior
  is preserved.
- Added `Diagnostics/ble_power_breakdown` with PPK2 digital markers for BLE
  initialization and separate idle/PMIC/advertising capture windows. Optional
  serial reporting prints timing and wake counters only after the captures.
- Added executable regressions for lazy key generation and PMIC acquisition
  failure cleanup, wired into CI and release checks.

## Validation and measurement status

Focused host tests cover the changed paths, including key-generation failure
and re-entry, PMIC bus failures, settings restoration, and cache behavior.
The affected power diagnostic and pairing examples are compiled for release.

No debug probes or PPK2 were connected during this session. Numerical current
savings and first-pairing timing have not been measured on this build. Issue
#114 remains open for a repeat of the reporter's captures and phone pairing.
The patch leaves GRTC timing and cold crystal startup unchanged; it does not
claim equal board current or eliminate the cost of waking from PMIC hibernate.

See the [investigation](ISSUE_114_POWER_ANALYSIS.md) and
[diagnostic instructions](../hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/examples/Diagnostics/ble_power_breakdown/README.md).

## Install

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.19"
```

[Changes since v1.0.18](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.18...v1.0.19)
