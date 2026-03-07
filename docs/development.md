# Development Notes

## Implemented Arduino Core APIs

- GPIO: `pinMode`, `digitalRead`, `digitalWrite`, `attachInterrupt`, `detachInterrupt`
- ADC/PWM: `analogRead`, `analogReadResolution(bits)`, `analogWrite`, `analogWriteResolution`
- UART: `Serial`, `Serial1`, `Serial2`
- I2C: `Wire` + `Wire1`, repeated-start, target/slave callbacks
- SPI: transactions + runtime frequency/mode/order
- Timing/power: `millis`, `micros`, delays, optional low-power idle profile
- Stream parser helpers: `setTimeout`, `find*`, `parseInt/parseFloat`, `readBytes*`, `readString*`
- Print/Printable compatibility: `Printable`, `print(const Printable&)`, `println(const Printable&)`
- Persistent storage: `Preferences` key/value API (flash-backed)
- EEPROM compatibility: `EEPROM` (`begin/read/write/update/get/put/commit/end`)
- Legacy compatibility hooks: `cli()/sei()`, `makeWord(...)`, AVR-like port access helpers
- Network base compatibility headers: `IPAddress`, `Client`, `Server`, `Udp/UDP`

## Bundled HAL Library

Library path:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation`

Implemented blocks:

- `ClockControl`, `Gpio`, `Spim`, `Twim`, `Uarte`
- `Saadc`, `Timer`, `Pwm`, `Gpiote`
- `PowerManager`, `Grtc`, `TempSensor`, `Watchdog`, `Pdm`
- `BleRadio` (custom peripheral LL + ATT/GATT subset)
- `BoardControl` (battery sense + antenna route control)

## BLE Status

Validated and stable with host adapter + hardware:

- Advertising
- Passive scanning
- Active scanning (`SCAN_REQ` / `SCAN_RSP`)
- Connect/disconnect
- GATT discovery/read
- Battery notify CCCD flow

Current gap:

- pairing/bond persistence is still partial
- BLE channel sounding / AoA / AoD style feature parity is not implemented yet

## Validation Artifacts

- [`FEATURE_PARITY.md`](../FEATURE_PARITY.md)
- [`TODO.md`](../TODO.md)
- [`POWER_PROFILE_MEASUREMENTS.md`](../POWER_PROFILE_MEASUREMENTS.md)
- [`docs/BLE_CLI_MATRIX_SUMMARY.md`](BLE_CLI_MATRIX_SUMMARY.md)
- [`docs/BLE_REGRESSION_RUNBOOK.md`](BLE_REGRESSION_RUNBOOK.md)
- [`docs/BUG_TRACKER.md`](BUG_TRACKER.md)
- `scripts/ble_cli_matrix.sh`
- `scripts/ble_pair_bond_regression.sh`

## Local Development Workflow

Use one of:

- `~/Arduino/hardware/...` sketchbook override
- `~/.arduino15/packages/...` package-layout override

Important:

- a sketchbook override at `~/Arduino/hardware/nrf54l15clean` takes precedence over Boards Manager and can make the packaged core disappear from installs/search results until you move or remove it

Example compile:

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  hardware/nrf54l15clean/nrf54l15clean/examples/03.Peripherals/InterruptPwmApiProbe/InterruptPwmApiProbe.ino
```

Example BLE matrix run:

```bash
bash scripts/ble_cli_matrix.sh --port /dev/ttyACM0 --sudo
```

Example pair/bond regression run:

```bash
bash scripts/ble_pair_bond_regression.sh --port /dev/ttyACM0 --sudo --attempts 10 --mode pair-bond
```
