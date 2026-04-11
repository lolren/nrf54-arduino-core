# HOLYIOT-25008 Probe

This is a bring-up helper for the `HOLYIOT-25008` module before the board has a
dedicated entry in the core.

What it verifies:

- the RGB LED pins from Zephyr board support
- the button pin from Zephyr board support

Current upstream pinout used by this probe:

- UART pads:
  - `P1.04` = `TX`
  - `P1.05` = `RX`
- button:
  - `P1.13`, active low with pull-up
- RGB LED:
  - red: `P2.09`, active low
  - green: `P1.10`, active low
  - blue: `P2.07`, active low

The sketch does not depend on a `HOLYIOT-25008` board definition. It drives the
GPIO directly, so it can be compiled with the current generic `nRF54L15` board
targets and flashed to the module for verification.

Behavior:

- power-up: a short all-colors flash so you know the probe is running
- idle: red -> green -> blue cycle
- button press: fast all-colors flash while the button is held

Recommended board selection for now:

- `Generic nRF54L15 Module (36-pad)`

If you are using Raspberry Pi Pico Debugprobe, the helper script assumes:

- `GP2 -> SWCLK`
- `GP3 -> SWDIO`
- `GND -> GND`
- `VTREF -> VDD`
- optional: `GP1 -> nRESET`

Run:

```bash
cd tools/holyiot_25008_probe
./run_holyiot_25008_probe.sh
```

Optional overrides:

```bash
FQBN=nrf54l15clean:nrf54l15clean:generic_nrf54l15_module_36pin ./run_holyiot_25008_probe.sh
PICO_PORT=/dev/ttyACM0 ./run_holyiot_25008_probe.sh
PICO_UID=E66118C4E3249A25 ./run_holyiot_25008_probe.sh
```
