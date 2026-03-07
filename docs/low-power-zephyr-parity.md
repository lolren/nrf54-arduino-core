# Zephyr Low-Power Parity on XIAO nRF54L15

This note explains the Arduino-side path that reproduced the Zephyr-class `SYSTEM OFF`
current floor on XIAO nRF54L15.

## Current State

The default board build in this core is a secure single-image build. The working
low-power path uses the secure peripheral map:

- `NRF_GRTC = 0x500E2000`
- `NRF_MEMCONF = 0x500CF000`
- `NRF_RESET = 0x5010E000`
- `NRF_REGULATORS = 0x50120000`

For pure floor measurements, use:

- BLE disabled
- Zigbee disabled
- the low-power examples under `File -> Examples -> Nrf54L15-Clean-Implementation -> LowPower`

## What Moved Into The Core

The parity work is no longer hidden in a one-off sketch.

Secure startup parity now runs from
[`system_nrf54l15.c`](../hardware/nrf54l15clean/nrf54l15clean/cores/nrf54l15/system_nrf54l15.c)
inside `SystemInit()`:

- PLL frequency selection
- FICR trim copy loop
- errata writes matching the Zephyr/nrfx startup path
- `RRAMC LOWPOWERCONFIG = 3`
- glitch detector disable
- LFXO/HFXO internal capacitor trim programming
- `VREGMAIN` DCDC enable
- instruction cache enable

The shared power-off path lives in
[`nrf54l15_hal.cpp`](../hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp):

- secure-domain GRTC wake programming
- LFCLK -> LFXO path
- Zephyr-style wake compare channel selection for XIAO
- board low-power hook before `SYSTEM OFF`
- optional RAM retention clear through `MEMCONF` for the explicit `NoRetention` helpers
- reset-reason clear
- final `REGULATORS->SYSTEMOFF`

## Example Sketch

Use:

- [`LowPowerZephyrParityBlink`](../hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/LowPower/LowPowerZephyrParityBlink/LowPowerZephyrParityBlink.ino)

Behavior:

- pulse LED on `P2.0` for `5 ms`
- arm GRTC wake for `1 s`
- enter `SYSTEM OFF`
- cold boot and repeat

This sketch uses the explicit `NoRetention` system-off helper so it can chase the
lowest current floor without changing the default `systemOff*()` behavior for the
rest of the core.

## Why It Worked

The final `SYSTEMOFF` write was not enough by itself.

The Zephyr-class result depended on the full chain:

1. secure build
2. Zephyr-like secure startup writes
3. oscillator and regulator trim parity
4. correct GRTC domain, IRQ group, and compare channel setup
5. board rail shutdown before `SYSTEM OFF`
6. optional RAM retention shutdown only for the explicit floor-measurement path

Leaving out any of those was enough to stay in the sub-mA range instead of the
microamp regime.

## Practical Result

Local validation on the XIAO board reached:

- `SYSTEM OFF` blink and burst-beacon paths in the **tens of uA**
- continuous low-power advertising around **0.1 mA** when the RF switch path is duty-cycled

That means the Arduino `SYSTEM OFF` path is now in the same broad current regime as
the Zephyr result on this board.

## BLE Note

This parity note is about the `SYSTEM OFF` floor.

Continuous BLE advertising is a different problem:

- `SYSTEM OFF` spends almost all time fully shut down
- continuous BLE must keep radio timing alive between events
- so BLE low-power work targets `System ON + sleep between events`, not the pure `SYSTEM OFF` floor

See also:

- [Low-power BLE patterns](low-power-ble-patterns.md)
- [BLE advertising validation](ble-advertising-validation.md)
