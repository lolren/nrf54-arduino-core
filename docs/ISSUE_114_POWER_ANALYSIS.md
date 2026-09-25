# Issue 114: startup charge and idle current

## Reported measurements

[Issue #114](https://github.com/lolren/nrf54-arduino-core/issues/114) compares
XIAO nRF54L15 and XIAO nRF54LM20A using core 1.0.18. The reporter supplies
3.8 V to the battery pads and periodically measures a BME280, advertises, then
uses `delay()`, timed System OFF, or nPM1300 hibernate. The
[original report and sketches](https://forum.seeedstudio.com/t/295800/5)
give these measured values:

| Region | XIAO nRF54L15 | XIAO nRF54LM20A |
| --- | --- | --- |
| Setup charge | 858 uC | 1400 uC |
| Average current during `delay()` | 4.1 uA | 7.8 uA |
| Idle floor between activity | 2.7 uA | 3.0 uA |
| Five-advertisement window | 96 uC | 110 uC |

These are the reporter's 1.0.18 measurements, not measurements of the patch.
The LM20A setup includes charger configuration and both sketches initialize
the BME280. The entire setup region cannot be attributed to `Bluefruit.begin()`.

## Confirmed software findings

`BleRadio::begin()` intends to defer Secure Connections key generation, but
`Bluefruit.begin()` subsequently calls `setSecurityIoCapabilities()`. That
setter generated a software P-256 keypair even when the application only
advertises and never pairs. The setter now only records the capability.
Secure Connections negotiation and explicit OOB preparation still generate
the key when needed. The existing ECC cooperation hook services the BLE link
during that calculation. First-pairing timing needs a hardware retest.

The LM20A telemetry helpers also left `ADCIBATMEASEN` enabled and overwrote
`ADCCONFIG` after an uncached reading. The patched acquisition saves and
attempts to restore both settings, including after an I2C error, and only caches
a result after successful acquisition and cleanup. Cleanup failures are returned
to the caller. Explicitly enabled application
settings are preserved. Charger temperature-monitoring behavior is retained.
The presence query now probes the chip without enabling measurement hardware.

The [nPM1300 System Monitor specification](https://docs.nordicsemi.com/r/bundle/ps_npm1300/page/chapters/core_components/saadc/doc/frontpage.html)
defines IBAT enable as a conversion following VBAT measurement. It does not
establish that leaving this bit enabled causes the reported 3.7 uA difference.
The telemetry correction is verified state cleanup; its current effect still
requires measurement.

## Remaining measurement boundaries

Both cores already disable the periodic SysTick in the Low Power profile and
use the same GRTC deadline-based `delay()` loop. With advertising, scanning,
and connections stopped, that loop can sleep until the requested deadline.
Source inspection found no LM20A-only periodic wakeup to remove.

Cold GRTC initialization still polls for LFXO startup. PMIC hibernate removes
MCU power and therefore repeats cold initialization; timed System OFF can
retain the GRTC clock. These are different startup conditions. This patch
does not change oscillator startup or GRTC sleep timing.

## Repeating the comparison

Use the bundled [ble_power_breakdown diagnostic](../hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/examples/Diagnostics/ble_power_breakdown/README.md)
to separate clock startup, BLE initialization, PMIC reads, and advertising.
Use the same board, power profile, CPU frequency, supply voltage, and sketch
for before/after captures. Its optional counter report helps distinguish CPU
wakeups from board-side current pulses.

Also repeat the original sketch to include its BME280, Wire, and charger
activity. Compare integrated charge for startup and steady-state current for
idle independently. No connected debug probe or PPK2 was available during
this patch session, so issue #114 remains open for those results.
