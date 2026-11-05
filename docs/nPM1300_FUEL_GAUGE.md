# nPM1300 fuel gauge (XIAO nRF54LM20A)

The signed-current API and example below are available on `main` after
`1.0.22`, not in the `1.0.22` release.

Nordic's nRF Fuel Gauge library estimates a battery's state of charge from its voltage, current and temperature plus a model of the battery. The nPM1300 provides the measurements, and the algorithm runs on the nRF54 MCU. Nordic's application note 4490_439, "Using the nPM1300 Fuel Gauge", describes how it works.

This page explains how to use the library with this core and the nPM1300 driver. **The library and a battery model are not part of this core**, so you have to download them and add them to your project yourself.

## Licence

The library is distributed by Nordic under `LicenseRef-Nordic-5-Clause`. Read the licence in Nordic's repository before using or redistributing it. This core does not include or redistribute the library, and nothing on this page is legal advice.

## What you need

1. The nRF Fuel Gauge library: the header and the precompiled library for your core.
2. A battery model for your battery.
3. The nPM1300 driver functions in this core, including `npm1300_read_ibat_signed_ma()`.

## 1. Get the library

The library is in Nordic's `sdk-nrfxlib` repository, folder `nrf_fuel_gauge` (https://github.com/nrfconnect/sdk-nrfxlib/tree/main/nrf_fuel_gauge), and it is also part of the nRF Connect SDK. Contributor testing used library version 2.0.0.

Pick the right build:

- **CPU and float ABI:** this core compiles with `-mcpu=cortex-m33 -mfloat-abi=soft`, so use the `cortex-m33/soft-float` build. The `hard-float` build will not link: the linker refuses to mix the two ABIs.
- **Cell type:** use `libnrf_fuel_gauge.a` for rechargeable batteries (the "secondary" variant). `libnrf_fuel_gauge_primary.a` is for non-rechargeable cells. Don't link both.

## 2. Add it to a PlatformIO project

PlatformIO treats a precompiled library as a normal library in `lib/`:

```
lib/nrf_fuel_gauge/
    library.json
    include/nrf_fuel_gauge.h
    libnrf_fuel_gauge.a          (from cortex-m33/soft-float)
src/main.cpp
src/battery_model.inc
```

`lib/nrf_fuel_gauge/library.json`:

```json
{
  "name": "nrf_fuel_gauge",
  "version": "1.0.0",
  "description": "Nordic nRF Fuel Gauge, precompiled (cortex-m33, soft-float, secondary cell)",
  "build": {
    "flags": [
      "-Iinclude",
      "-L.",
      "-lnrf_fuel_gauge"
    ]
  }
}
```

Paths in `library.json` are relative to the library's own folder. PlatformIO only links the library if something in `src/` includes `nrf_fuel_gauge.h`.

If you use another build system, you need the same three things: the include path, the library path, and `-lnrf_fuel_gauge` with the soft-float build. Using the Arduino IDE is not covered here.

## 3. Get a battery model

The library needs a model of your battery. The easiest start is Nordic's nPM PowerUP app (part of nRF Connect for Desktop, a free download from Nordic), which includes generic battery models. The tested example used a generic 400 mAh LP503030 model from there. Each model is saved as a `.inc` file, which the example includes:

```cpp
static const struct battery_model battery_model = {
#include "battery_model.inc"
};
```

A model describes one type of cell. If your battery differs from the model's (capacity, internal resistance, age), the estimate will still run but won't reach the accuracy Nordic quotes. For a model made from your own cell, you can profile it with nPM PowerUP, an nPM1300 EK and the nPM Fuel Gauge Board (about 48 hours per test temperature, three temperatures recommended). The model must match the library variant: a secondary-cell model for a rechargeable battery.

## 4. Use it

See `hardware/nrf54l15clean/nrf54l15clean/examples/Power/nPM1300_Fuel_Gauge`. The pieces it relies on:

- **Enable the charger.** The nPM1300 does not charge until the host enables it. The example sets a termination voltage and charge current, then calls `npm1300_charger_enable(true)`. Set these for your own battery.
- **Read voltage and current.** `npm1300_read_vbat_mv()` and `npm1300_read_ibat_signed_ma()`. The signed read returns positive current while charging and negative while discharging.
- **Flip the current sign for the library.** The library expects **discharge-positive** current, the opposite of the driver. Pass `-1 × current`. With the wrong sign the estimate freezes under load, because the library believes the battery is charging.
- **Temperature.** The library wants the battery temperature. `npm1300_read_temp_mc()` is the PMIC die temperature, not the battery's. On this module the NTC pin is tied to ground through 10 kΩ, so there is no real battery temperature, and the example uses a fixed 25 °C.
- **Tell the library about VBUS and the charger state.** The example calls `nrf_fuel_gauge_ext_state_update()` when VBUS connects or disconnects and when the charger state changes. Without this, the state of charge stays stuck while charging.
- **Call `nrf_fuel_gauge_process()` regularly.** Nordic suggests every second while active and every five seconds when idle. Before an interval without measurements, use the library's `nrf_fuel_gauge_idle_set()` API with the battery voltage, temperature and expected average current so it can account for idle discharge. See the [Nordic API header](https://github.com/nrfconnect/sdk-nrfxlib/blob/main/nrf_fuel_gauge/include/nrf_fuel_gauge.h) for its argument and sign conventions. The example does not enter this idle mode.
- **Don't use the init macro from C++.** `NRF_FUEL_GAUGE_DEFAULT_INIT_PARAMETERS_SECONDARY` uses designated initializers that g++ can reject, so the example fills the struct field by field.

## Behaviour to expect

The board measurements and charge/discharge observations below were reported by the contributor.

- **Convergence takes minutes.** The estimate starts from the first voltage reading. If the battery is under load or was just charged, it can start several percent away from the final value and then settle over a few minutes. Starting from a rested battery is the most accurate.
- **Cold boots lose the state.** Hibernate and ship mode power the MCU off, so the gauge is initialised again at each boot. For a device that wakes after hours of rest, read the battery voltage right away, before the radio or LEDs draw current, and initialise from that. The library can also save and restore its state (`nrf_fuel_gauge_state_get()` and the `state` field of the init parameters), and the core's EEPROM library can hold it. That is not covered by the example.
- **Accuracy depends on the voltage measurement.** Near 4 V, the cell voltage moves roughly 5 to 14 mV per percent of state of charge (about 6 mV per percent mid-range, more at the top), so a voltage error of a few tens of millivolts is a few percent of state of charge. The nPM1300 datasheet gives the VBAT measurement an accuracy of ±1 % (3 V to 4.5 V), which is about ±40 mV. On one board tested against a Fluke 117, the reading was 13 mV high at 3.96 V and 27 mV high at 4.200 V (at zero battery current), both inside the specification and worth about 2 % of state of charge. With only two points and a 4.9 mV ADC step, the offset and the gain error can't be separated. Subtracting about 20 mV would leave this board within about ±7 mV across that range: set `VBAT_OFFSET_V` to `+0.020f`, because the example subtracts that value. The error varies from unit to unit, so calibrate each board if you need better than a few percent.
- **Single readings are noisy.** The ADC is 10 bits over 5 V, so one step is about 4.9 mV, and readings hop between adjacent steps. The library's filter smooths this out, but a single reading used to initialise the gauge carries a few millivolts of noise.
- **Don't measure with a series current meter in the battery path.** A current meter in ampere-meter mode (for example a Nordic PPK) puts a shunt and leads in series with the battery. In testing it lowered the voltage the board saw by about 45 mV at 60 mA, which biased the state of charge low by several percent. Measure with the battery connected directly.
- **The state of charge holds at 99 % during charging and goes to 100 % when the charger reports complete** (observed). The example reports the charger state to the library, which is what lets it reach 100 %.

## Saving state across power loss (optional)

The example does not save anything: it starts the gauge fresh at every boot, which is fine for a device that stays powered. The library does support saving and restoring its state, which is useful when the MCU loses power or RAM, for example in hibernate or ship mode:

- `nrf_fuel_gauge_state_get()` copies the library state into a buffer of `nrf_fuel_gauge_state_size` bytes.
- Pass that buffer back in the `state` and `state_size` fields of the init parameters to resume from it.
- After a library version change an old state is rejected (`-EBADF`). `nrf_fuel_gauge_state_compatible_check()` tests it first. The battery model must also be the same as when the state was saved.
- The header says a stored state converges more slowly than a fresh start if the battery voltage has moved a lot since it was saved.

Whether you need it depends on the product:

- **A device that is always on:** not needed.
- **A device that sleeps for hours between readings:** the battery is well rested at each wake, so a fresh init from the resting voltage (read before the radio or LEDs draw current) is already accurate. State saving then mainly preserves the long-term state-of-health estimate and the charge-cycle count.
- **State-of-health tracking:** the library only estimates it by observing full charge cycles, so it needs the cycle history to survive power loss.

If you only want state of health and the cycle count, you can store just those two values and a charge-since-last-cycle figure, and pass them back in the optional configuration parameters (`soh`, `cycle_count`, `cycle_count_cumulative_mah`, with `soh_estimate_enable` set). Writes to non-volatile storage should be rare: write when a value has changed meaningfully (a cycle counted, or state of health moved by about 0.5 %), at the end of a charge, or before a long sleep, and not on every wake. The core's `EEPROM` library can hold such a record. It was used this way in testing (restore after a reboot, and a save at the end of a charge taking about 8 ms), but it is not part of the example.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Link error about VFP register arguments | The `hard-float` library was used. Use `soft-float`. |
| Undefined `nrf_fuel_gauge_*` at link time | The `-L` or `-l` flags are wrong, or the library is not in the folder `library.json` points to. |
| `nrf_fuel_gauge_init` returns a negative number | The model or the library variant does not match, or the library state is incompatible. |
| State of charge frozen under load | Current sign is inverted. The library wants discharge-positive. |
| State of charge stuck while charging | The library was not told about VBUS and the charger state. |
| No charging | The charger was not enabled. Check the charger status and error registers. |
| `process` crashes after a good `init` | Compare your compile flags with the ones Nordic used to build the library. |

## Tested

The contributor tested library 2.0.0 (secondary variant), a 400 mAh Li-Po (LP503030) with a matching battery model, and a fixed 25 °C temperature, with the battery connected directly to the board:

- Discharging under a 60 to 70 mA load: the state of charge falls steadily and the predicted voltage tracks the measured voltage.
- A full charge at 100 mA: constant current, then constant voltage (about 13 minutes at this current), then the charger reporting complete. The state of charge rose through CC, held at 99 % during CV, and went to 100 % when the charger reported complete.

The battery voltage reading was compared with a Fluke 117 at two voltages (see above). Not tested: the primary-cell variant, other batteries, temperature effects, or long-term state of health.
