# BLE and PMIC power breakdown

This diagnostic separates the operations discussed in
[issue #114](https://github.com/lolren/nrf54-arduino-core/issues/114) on
XIAO nRF54L15 and XIAO nRF54LM20A. Select the **Low Power** profile with BLE
enabled. No sensor, external I2C bus, connection, or phone is required.

The default build does not initialize Serial, drive an LED, or change charger
settings. On LM20A it reads VBAT and IBAT once through the bundled nPM1300 API.
On L15 the same stage is an additional idle control window.

## Capture markers

Connect the board ground and the following header pins to the PPK2 digital
inputs. Leave these pins free of other loads. D4 is a marker in this sketch,
not an I2C connection.

| Pin | Meaning |
| --- | --- |
| D0 | High immediately before `Bluefruit.begin()`, low immediately after it returns |
| D1 | High around the first explicit `micros()` call in `setup()` |
| D2 | Stage bit 0, least significant bit |
| D3 | Stage bit 1 |
| D4 | Stage bit 2 |

Read the stage as `(D4 << 2) | (D3 << 1) | D2`. The three pins change
sequentially, so ignore brief intermediate codes at transitions.

| Stage | D4 D3 D2 | Operation | Duration |
| --- | --- | --- | --- |
| 0 | 0 0 0 | Marker setup and first explicit clock read | Variable |
| 1 | 0 0 1 | Baseline `delay()`, before BLE initialization | 10 seconds |
| 2 | 0 1 0 | BLE initialized, advertising stopped | 10 seconds |
| 3 | 0 1 1 | Idle after one VBAT and IBAT read on LM20A; idle control on L15 | 10 seconds |
| 4 | 1 0 0 | Non-connectable advertising at approximately 1-second intervals, 0 dBm | 10 seconds |
| 5 | 1 0 1 | Advertising stopped again | 10 seconds |
| 6 | 1 1 0 | Measurement complete | Indefinite |
| 7 | 1 1 1 | BLE initialization, PMIC probe/measurement, or advertising setup failed | Indefinite |

Stage markers change immediately before each `delay()`. Preparation for the
following stage occurs at the end of the preceding marker interval:
`Bluefruit.begin()` follows the stage 1 delay, PMIC reads follow stage 2,
and advertising configuration follows stage 3. Use the middle eight seconds
of stages 1, 2, 3, and 5 for idle averages. D0 independently bounds BLE
initialization. Exclude stage 7 captures.

## What to compare

Use the same board supply voltage, supply input, and core menu settings for
each before/after capture. Capture current with the USB supply and debugger
disconnected. A cold start gives a complete startup trace; restarting with a
retained GRTC after System OFF is a different measurement and should be
reported separately.

Compare stage 2 against stage 1 for the cost of leaving BLE initialized,
stage 3 against stage 2 for residual current after PMIC measurement, and
stage 5 against stage 3 for residual current after advertising. A difference
between boards includes their regulators and other board hardware, not only
CPU execution. This sketch does not establish the lowest possible board
current.

The D0 interval measures BLE initialization after a working clock and ten
seconds of baseline idle. It does not include the entire boot or the original
weather-monitor sketch's sensor/charger setup. D1 shows any timebase startup
still pending at the first explicit clock call. The core or board startup may
already have initialized that timebase before `setup()`; therefore D1 is not
guaranteed to include crystal startup. Measure power-on through the first
stage 1 transition separately when investigating total cold-start charge.

## Optional wake-count summary

Set `BLE_POWER_BREAKDOWN_SERIAL` to `1` in the sketch or define it in the build
flags to store the existing core idle counters around each window. Serial
starts at 115200 only after stage 5 completes, then prints BLE initialization
time, PMIC values where applicable, and CSV rows:

```text
stage,outer_loops,wfi_entries,grtc_irqs,delay_irqs,skip_wfi
```

These are counter differences over each ten-second delay. They identify CPU
sleep entries, GRTC interrupts, and deliberately busy BLE processing;
they do not count PMIC switching pulses. An otherwise inactive system normally
needs only the final delay compare, whereas advertising adds scheduled work.
Other enabled interrupts can also wake the CPU, and WFI entries are not a
count of all interrupt handlers. The counters are diagnostic globals, not a
stable application API.

Use this as a separate diagnostic run if the serial connection changes the
power setup. Serial activity after stage 5 is outside the current capture
windows. The default build requires no serial connection and generates no
serial output.
