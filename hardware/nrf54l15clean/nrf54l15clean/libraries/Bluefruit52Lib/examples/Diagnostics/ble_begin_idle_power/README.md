# BLE initialization idle-current measurement

Reproduces the comparison in [issue #111](https://github.com/lolren/nrf54-arduino-core/issues/111)
on XIAO nRF54LM20A or XIAO nRF54L15. Select the **Low Power** power profile
and keep BLE enabled.

The sketch produces these consecutive windows after startup:

| Window | Operation | Duration |
| --- | --- | --- |
| 1 | `delay()` before BLE initialization | 10 seconds |
| 2 | `Bluefruit.begin()`, then idle `delay()` | 10 seconds |
| 3 | Non-connectable advertising at approximately 1-second intervals | 10 seconds |
| 4 | Advertising stopped; BLE remains initialized | Indefinite |

There is no serial output and the connection LED is disabled. A continuously
flashing LED at 5 Hz indicates initialization/advertising failure; exclude that
capture. A scanner can see `nRF54-IdlePower` only during window 3.

Use the same board power setup for baseline and patched captures. For the idle
current measurement, power the board through the PPK2 with USB and SWD detached.
Begin the capture from a cold power-on: LM20A's GRTC clock selection is latched
when the timer starts and can remain internally retained through System OFF.

Exclude boot, the crystal-startup interval, BLE initialization, and advertising
transitions when selecting steady-state averages. Compare the middle of windows
1, 2, and 4, and report their average current and core version. This example
leaves board peripherals in their startup state; it measures the additional
current caused by BLE, not an independently characterized minimum board floor.

With the issue #111 clock fix, the first cold use of the LM20A GRTC can wait for
the 32.768 kHz crystal to start (datasheet typical startup: 0.43 seconds).
Subsequent `delay()` calls reuse the running timebase. Failure to start the
crystal takes a bounded fallback to the existing RC clock path, which has lower
accuracy and is not the normal low-power configuration.
