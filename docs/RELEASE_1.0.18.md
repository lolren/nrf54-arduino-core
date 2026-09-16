# nRF54 Arduino Core 1.0.18

This patch addresses issue #111, where calling `Bluefruit.begin()` on the XIAO
nRF54LM20A could leave `delay()` at substantially higher idle current than the
BLE-disabled baseline.

## LM20A low-power fix

- BLE and `delay()` now share the LM20A/LM20B GRTC sleep timing of
  `TIMEOUT=6` and `WAKETIME=4` LFCLK ticks, preserving the required one-tick
  wake guard.
- The LM20A BLE-first path prepares the direct LFXO GRTC source before the
  counter starts. Existing active or System-OFF-retained GRTC state is left
  untouched.
- The existing RC-clock fallback remains bounded if the external 32.768 kHz
  crystal cannot start.
- Added `Diagnostics/ble_begin_idle_power`, a repeatable PPK2 comparison for
  pre-BLE idle, BLE-initialized idle, advertising, and post-advertising idle.

## Validation

The new measurement sketch compiles for both XIAO nRF54LM20A and XIAO nRF54L15.
Focused GRTC/BLE idle-power contracts and the core I/O regression suite pass.
Hardware current results still need confirmation with the reporter's PPK2
setup; issue #111 remains open for that retest.

## Install

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.18"
```

[Full changes since v1.0.17](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.17...v1.0.18)
