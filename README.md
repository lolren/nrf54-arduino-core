# Nrf54L15-Clean-Implementation

Open-source Arduino board package for **Seeed XIAO nRF54L15** with a clean register-level implementation.

- Core name: `Nrf54L15-Clean-Implementation`
- Board: `XIAO nRF54L15 (Nrf54L15-Clean-Implementation)`
- FQBN: `nrf54l15clean:nrf54l15clean:xiao_nrf54l15`
- No Zephyr runtime dependency
- No nRF Connect SDK runtime dependency

## Why this repo exists

This repo is intentionally different from the Zephyr-based core:

- Uses direct register programming for core + HAL behavior.
- Ships as a normal Arduino Boards Manager package.
- Does not require users to install large external SDKs to build/upload.

## Boards Manager install

Add this URL to Arduino IDE / Arduino CLI Additional Boards Manager URLs:

```text
https://raw.githubusercontent.com/lolren/NRF54L15-Clean-Arduino-core/main/package_nrf54l15clean_stable_index.json
```

If your IDE/CLI already cached an older index, remove cache and update indexes:

- Linux: `rm -f ~/.arduino15/package_nrf54l15clean_index.json ~/.arduino15/package_nrf54l15clean_stable_index.json`
- Then run: `arduino-cli core update-index`

Then install package **`Nrf54L15-Clean-Implementation`** and select board:

- `XIAO nRF54L15 (Nrf54L15-Clean-Implementation)`

## Troubleshooting

If Arduino IDE shows two identical `XIAO nRF54L15 (Nrf54L15-Clean-Implementation)` entries:

- remove any local legacy sketchbook platform copy/symlink (common culprit):
  `~/Arduino/hardware/nrf54l15clean/0.1.0`
- keep only the Boards Manager package (`nrf54l15clean:nrf54l15clean`).

If `AnalogReadSerial` is stuck near full-scale (for example always `1023`):

- update to the latest Boards Manager release of this core
- make sure no previous sketch is driving `A0` as a digital output
- retest with `File > Examples > 01.Basics > AnalogReadSerial`

If serial output starts showing random characters during sustained prints:

- update to the latest Boards Manager release (UART TX timeout handling was
  hardened in `0.1.5`)
- verify Serial Monitor baud matches the sketch `Serial.begin(...)` value

If `SSD1306wire` examples fail with `B00101100`/`Bxxxx` compile errors:

- update to `0.1.6` or newer (legacy binary literal compatibility was added)
- for this library version, use `oled.begin(&SSD1306_128x64, 0x3C)` argument order
- note: this specific library declares `avr` architecture and may still show
  warnings on ARM cores

If Adafruit GFX / BusIO / SSD1306 examples fail with missing symbols like
`BitOrder`, `__FlashStringHelper`, `digitalPinToPort`, or `wiring_private.h`:

- update to `0.1.7` or newer
- this release adds compatibility shims used by those libraries and compiles
  the common Adafruit SSD1306 example set for this board

## Linux CMSIS-DAP permissions (one-time)

If upload reports `No connected debug probes` but `lsusb` shows
`2886:0066 Seeed Studio XIAO nrf54 CMSIS-DAP`, add a udev rule:

```bash
cat <<'RULE' | sudo tee /etc/udev/rules.d/60-seeed-xiao-nrf54-cmsis-dap.rules >/dev/null
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="2886", ATTRS{idProduct}=="0066", MODE="0660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="2886", ATTR{idProduct}=="0066", MODE="0660", GROUP="plugdev", TAG+="uaccess"
RULE
sudo udevadm control --reload-rules
sudo udevadm trigger --attr-match=idVendor=2886 --attr-match=idProduct=0066
```

## Pinout

![XIAO nRF54L15 default pin routes](docs/xiao_nrf54l15_default_pin_routes.png)

## Default peripheral routes

| Peripheral | Default pins | Notes |
|---|---|---|
| `Wire` (I2C primary) | `SDA=D4(P1.10)`, `SCL=D5(P1.11)` | Sketch compatibility default |
| `Wire1` (I2C secondary) | `SDA=D12(P0.04)`, `SCL=D11(P0.03)` | Back-pad bus |
| `SPI` | `MOSI=D10(P2.02)`, `MISO=D9(P2.04)`, `SCK=D8(P2.01)`, `SS=D2(P1.06)` | Runtime clock via `SPISettings` |
| `Serial1` / `Serial2` | `TX=D6(P2.08)`, `RX=D7(P2.07)` | `Serial2` is alias of `Serial1` |
| `Serial` | USB bridge (default) | Can be switched to header UART via Tools menu |

Peripheral instance routing note:
- `Serial` is intentionally routed to the opposite serial-fabric instance from default `Wire` and `SPI`, so `Serial` + `Wire` + `SPI` can run together in both Serial Routing modes.
- `Wire1` shares an instance with `Serial`, and `Serial1/Serial2` share an instance with default `Wire`/`SPI`.

## Arduino pin map (Arduino -> MCU)

| Arduino pin | MCU pin | ADC input | Typical role |
|---|---|---|---|
| `D0` / `A0` | `P1.04` | `AIN0` | GPIO / ADC |
| `D1` / `A1` | `P1.05` | `AIN1` | GPIO / ADC |
| `D2` / `A2` | `P1.06` | `AIN2` | GPIO / ADC / `SS` |
| `D3` / `A3` | `P1.07` | `AIN3` | GPIO / ADC |
| `D4` / `A4` | `P1.10` | N/A | `Wire SDA` |
| `D5` / `A5` | `P1.11` | `AIN4` | `Wire SCL` |
| `D6` | `P2.08` | N/A | `Serial1/2 TX` |
| `D7` | `P2.07` | N/A | `Serial1/2 RX` |
| `D8` | `P2.01` | N/A | `SPI SCK` |
| `D9` | `P2.04` | N/A | `SPI MISO` |
| `D10` | `P2.02` | N/A | `SPI MOSI` |
| `D11` | `P0.03` | N/A | Back pad / `Wire1 SCL` |
| `D12` | `P0.04` | N/A | Back pad / `Wire1 SDA` |
| `D13` | `P2.10` | N/A | Back pad GPIO |
| `D14` | `P2.09` | N/A | Back pad GPIO |
| `D15` | `P2.06` | N/A | Back pad GPIO |
| `LED_BUILTIN` (`16`) | `P2.00` | N/A | User LED (active-low) |
| `PIN_BUTTON` (`17`) | `P0.00` | N/A | User button (active-low) |
| `PIN_SAMD11_RX` (`18`) | `P1.09` | N/A | USB bridge route |
| `PIN_SAMD11_TX` (`19`) | `P1.08` | N/A | USB bridge route |

Additional board-control nets exposed in HAL:

| Function | MCU pin | Symbol |
|---|---|---|
| VBAT divider enable | `P1.15` | `kPinVbatEnable` |
| VBAT ADC sense | `P1.14` | `kPinVbatSense` / `A7` |
| RF switch control | `P2.05` | `kPinRfSwitchCtl` |

## MCU pin map (MCU -> Arduino)

| MCU pin | Arduino alias |
|---|---|
| `P0.00` | `PIN_BUTTON` |
| `P0.03` | `D11` |
| `P0.04` | `D12` |
| `P1.04` | `D0/A0` |
| `P1.05` | `D1/A1` |
| `P1.06` | `D2/A2` |
| `P1.07` | `D3/A3` |
| `P1.08` | `PIN_SAMD11_TX` |
| `P1.09` | `PIN_SAMD11_RX` |
| `P1.10` | `D4/A4` |
| `P1.11` | `D5/A5` |
| `P1.14` | `A7` / VBAT sense |
| `P1.15` | VBAT enable |
| `P2.00` | `LED_BUILTIN` |
| `P2.01` | `D8` |
| `P2.02` | `D10` |
| `P2.04` | `D9` |
| `P2.05` | RF switch control |
| `P2.06` | `D15` |
| `P2.07` | `D7` |
| `P2.08` | `D6` |
| `P2.09` | `D14` |
| `P2.10` | `D13` |

## Implemented Arduino core APIs

- GPIO: `pinMode`, `digitalRead`, `digitalWrite`, `attachInterrupt`, `detachInterrupt`
- ADC/PWM: `analogRead`, `analogReadResolution(bits)`, `analogWrite`, `analogWriteResolution`
- UART: `Serial`, `Serial1`, `Serial2`
- I2C: `Wire` + `Wire1`, repeated-start, target/slave callbacks
- SPI: transactions + runtime frequency/mode/order
- Timing/power: `millis`, `micros`, delays, optional low-power idle profile

## HAL blocks in bundled library

Library path:

- `hardware/nrf54l15clean/0.1.0/libraries/Nrf54L15-Clean-Implementation`

Implemented blocks:

- `ClockControl`, `Gpio`, `Spim`, `Twim`, `Uarte`
- `Saadc`, `Timer`, `Pwm`, `Gpiote`
- `PowerManager`, `Grtc`, `TempSensor`, `Watchdog`, `Pdm`
- `BleRadio` (custom peripheral LL + ATT/GATT subset)
- `BoardControl` (battery sense + antenna route control)

## Board control helpers (battery + RF switch)

```cpp
#include "nrf54l15_hal.h"
using namespace xiao_nrf54l15;

int32_t vbatMv = 0;
uint8_t vbatPct = 0;
BoardControl::sampleBatteryMilliVolts(&vbatMv);
BoardControl::sampleBatteryPercent(&vbatPct);

BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
BoardControl::setAntennaPath(BoardAntennaPath::kExternal);
BoardControl::setAntennaPath(BoardAntennaPath::kControlHighImpedance);
```

Important RF note:

- `kControlHighImpedance` releases `P2.05` drive; it does **not** power-gate the RF switch IC.

## Tools menu options

- Upload method: Auto / pyOCD / OpenOCD
- CPU frequency: 64 MHz / 128 MHz
- BLE support: On / Off
- BLE TX power: `-20`, `-8`, `0`, `+8` dBm
- BLE timing profile: Interop / Balanced low-power / Aggressive low-power
- BLE trace: Off / On
- Power profile: Balanced / Low power (WFI idle)
- Peripheral auto-gating: Off / 2 ms / 200 us
- Antenna route: Ceramic / External U.FL
- Serial routing: USB bridge / Header UART

## Example sketches

Core examples:

- `hardware/nrf54l15clean/0.1.0/examples/01.Basics/AnalogReadSerial/AnalogReadSerial.ino`
- `hardware/nrf54l15clean/0.1.0/examples/03.Peripherals/PeripheralProbe/PeripheralProbe.ino`
- `hardware/nrf54l15clean/0.1.0/examples/03.Peripherals/InterruptPwmApiProbe/InterruptPwmApiProbe.ino`
- `hardware/nrf54l15clean/0.1.0/examples/03.Peripherals/WireRepeatedStartProbe/WireRepeatedStartProbe.ino`
- `hardware/nrf54l15clean/0.1.0/examples/03.Peripherals/WireTargetResponder/WireTargetResponder.ino`

Library examples (HAL + BLE + power):

- `.../examples/BoardBatteryAntennaBusControl/BoardBatteryAntennaBusControl.ino`
- `.../examples/InterruptWatchdogLowPower/InterruptWatchdogLowPower.ino`
- `.../examples/BleAdvertiser/BleAdvertiser.ino`
- `.../examples/BlePassiveScanner/BlePassiveScanner.ino`
- `.../examples/BleActiveScanner/BleActiveScanner.ino`
- `.../examples/BleConnectionPeripheral/BleConnectionPeripheral.ino`
- `.../examples/BleGattBasicPeripheral/BleGattBasicPeripheral.ino`
- `.../examples/BleCustomGattRuntime/BleCustomGattRuntime.ino`
- `.../examples/BleBatteryNotifyPeripheral/BleBatteryNotifyPeripheral.ino`
- `.../examples/BlePairingEncryptionStatus/BlePairingEncryptionStatus.ino`
- `.../examples/BleBondPersistenceProbe/BleBondPersistenceProbe.ino`
- `.../examples/BleConnectionTimingMetrics/BleConnectionTimingMetrics.ino`

## BLE status (current)

Validated and stable with host adapter + hardware:

- Advertising
- Passive scanning
- Active scanning (`SCAN_REQ` / `SCAN_RSP`)
- Connect/disconnect
- GATT discovery/read
- Battery notify CCCD flow

Current gap (tracked):

- Pairing/bond persistence is still **partial**.
  - Successful `Paired: yes` / `Bonded: yes` outcomes are observed.
  - Repeated runs are still unstable due intermittent auth timeouts and Intel host-controller crash artifacts (`Hardware Error 0x0c`).

Channel sounding status:

- BLE channel sounding / AoA/AoD style feature parity is **not implemented yet** in this clean core.
- It is tracked as advanced future work due significant PHY + timing + controller complexity.

## Validation artifacts

- `FEATURE_PARITY.md`
- `TODO.md`
- `POWER_PROFILE_MEASUREMENTS.md`
- `docs/BLE_CLI_MATRIX_SUMMARY.md`
- `docs/BLE_REGRESSION_RUNBOOK.md`
- `docs/BUG_TRACKER.md`
- `scripts/ble_cli_matrix.sh`
- `scripts/ble_pair_bond_regression.sh`

## Local development workflow

Use one of:

- `~/Arduino/hardware/...` sketchbook override (active development)
- `~/.arduino15/packages/...` package-layout override

Example compile:

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  hardware/nrf54l15clean/0.1.0/examples/03.Peripherals/InterruptPwmApiProbe/InterruptPwmApiProbe.ino
```

Example BLE matrix run:

```bash
bash scripts/ble_cli_matrix.sh --port /dev/ttyACM0 --sudo
```

Example pair/bond regression run:

```bash
bash scripts/ble_pair_bond_regression.sh --port /dev/ttyACM0 --sudo --attempts 10 --mode pair-bond
```

Example bonded reconnect regression run:

```bash
bash scripts/ble_pair_bond_regression.sh --port /dev/ttyACM0 --sudo --attempts 5 --mode bonded-reconnect --example BleBondPersistenceProbe
```

If multiple host adapters are present:

```bash
bash scripts/ble_pair_bond_regression.sh --sudo --controller AA:BB:CC:DD:EE:FF --btmon-iface hci1
```

## Support

If this project helps you, you can support it here:

- https://buymeacoffee.com/lolren
