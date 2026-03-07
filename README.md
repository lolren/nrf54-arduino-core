# XIAO nRF54L15 Clean Arduino Core

Open-source Arduino board package for **Seeed XIAO nRF54L15** with a clean register-level implementation.

- Board package: `Nrf54L15-Clean-Implementation`
- Board: `XIAO nRF54L15 (Nrf54L15-Clean-Implementation)`
- FQBN: `nrf54l15clean:nrf54l15clean:xiao_nrf54l15`
- No Zephyr runtime dependency
- No nRF Connect SDK runtime dependency

## What This Is

This repo provides a normal Arduino Boards Manager package for XIAO nRF54L15.

Compared with the Zephyr-based core, it:

- uses direct register programming for core + HAL behavior
- keeps build/install flow inside normal Arduino IDE / Arduino CLI workflows
- does not require a large external SDK just to compile and upload sketches

## Quick Start

Use this Additional Boards Manager URL:

```text
https://raw.githubusercontent.com/lolren/NRF54L15-Clean-Arduino-core/main/package_nrf54l15clean_index.json
```

The `package_nrf54l15clean_stable_index.json` URL is kept for compatibility, but the main index above is the supported/default install path.

Then install **`Nrf54L15-Clean-Implementation`** and select:

- board: `XIAO nRF54L15 (Nrf54L15-Clean-Implementation)`
- upload method: `Auto`

CLI install example:

```bash
arduino-cli core update-index \
  --additional-urls https://raw.githubusercontent.com/lolren/NRF54L15-Clean-Arduino-core/main/package_nrf54l15clean_index.json

arduino-cli core install nrf54l15clean:nrf54l15clean \
  --additional-urls https://raw.githubusercontent.com/lolren/NRF54L15-Clean-Arduino-core/main/package_nrf54l15clean_index.json
```

If the board does not appear in Boards Manager, jump to `Troubleshooting` first. The most common cause is a local sketchbook override hiding the package entry.

## First Upload

Default `Upload Method = Auto` behavior:

1. Try `pyocd` first.
2. If `pyocd` is missing, try a one-time install through the active Python.
3. If that fails, fall back to OpenOCD.

Protected target recovery:

- if the target is locked (`APPROTECT`, `FAULT ACK`, `Failed to read memory at 0xe000ed00`, `DP initialisation failed`), the uploader will try a recovery erase and retry flashing

First-time requirements:

- Python 3 + `pip`
- internet access for one-time `pyocd` bootstrap if `pyocd` is not already installed

Linux note:

- if the CMSIS-DAP probe is visible in `lsusb` but upload says `No connected debug probes`, add the udev rule shown in `Troubleshooting`

## Board Overview

![XIAO nRF54L15 default pin routes](docs/xiao_nrf54l15_default_pin_routes.png)

### Default Peripheral Routes

| Peripheral | Default pins | Notes |
|---|---|---|
| `Wire` | `SDA=D4(P1.10)`, `SCL=D5(P1.11)` | Dedicated `TWIM22` controller |
| `Wire1` | `SDA=D12(P0.04)`, `SCL=D11(P0.03)` | Dedicated `TWIM30` controller |
| `SPI` | `MOSI=D10(P2.02)`, `MISO=D9(P2.04)`, `SCK=D8(P2.01)`, `SS=D2(P1.06)` | Runtime clock via `SPISettings` |
| `Serial1` / `Serial2` | `TX=D6(P2.08)`, `RX=D7(P2.07)` | `Serial2` is alias of `Serial1` |
| `Serial` | USB bridge by default | Can be switched to header UART via Tools |

Routing note:

- `Serial` / `Serial1` use the serial-fabric UARTE instances (`20` / `21`)
- `Wire` and `Wire1` use dedicated I2C controllers (`TWIM22` / `TWIM30`)
- `Serial` + `Wire` + `Wire1` can run together

Board-control snippet:

```cpp
#include "nrf54l15_hal.h"
using namespace xiao_nrf54l15;

int32_t vbatMv = 0;
BoardControl::sampleBatteryMilliVolts(&vbatMv);
BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
BoardControl::setRfSwitchPowerEnabled(false);
```

For the full pin maps, Tools menu summary, and board-control notes, see [Board Reference](docs/board-reference.md).

## Examples

Core examples live under [`hardware/nrf54l15clean/nrf54l15clean/examples`](hardware/nrf54l15clean/nrf54l15clean/examples).

Suggested starting points:

- Basics: [`Blink`](hardware/nrf54l15clean/nrf54l15clean/examples/01.Basics/Blink), [`AnalogReadSerial`](hardware/nrf54l15clean/nrf54l15clean/examples/01.Basics/AnalogReadSerial)
- Peripherals: [`PeripheralProbe`](hardware/nrf54l15clean/nrf54l15clean/examples/03.Peripherals/PeripheralProbe), [`WireImuRemapScanner`](hardware/nrf54l15clean/nrf54l15clean/examples/03.Peripherals/WireImuRemapScanner), [`XiaoBoardControlPins`](hardware/nrf54l15clean/nrf54l15clean/examples/03.Peripherals/XiaoBoardControlPins)
- Memory: [`PreferencesBootCounter`](hardware/nrf54l15clean/nrf54l15clean/examples/05.Memory/PreferencesBootCounter), [`EEPROMBootCounter`](hardware/nrf54l15clean/nrf54l15clean/examples/05.Memory/EEPROMBootCounter)
- Power / HAL / BLE examples: [`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples`](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples)

Recommended library examples:

- Power: `LowPowerIdleWfi`, `InterruptWatchdogLowPower`, `BoardBatteryAntennaBusControl`
- BLE: `BleAdvertiser`, `BlePassiveScanner`, `BleActiveScanner`, `BleConnectionPeripheral`, `BleGattBasicPeripheral`

## Troubleshooting

### Board Does Not Appear In Boards Manager

Check these first:

- use the standard package index URL shown in `Quick Start`
- remove any local sketchbook override at `~/Arduino/hardware/nrf54l15clean`
- clear cached indexes and refresh:

```bash
rm -f ~/.arduino15/package_nrf54l15clean_index.json \
      ~/.arduino15/package_nrf54l15clean_stable_index.json
arduino-cli core update-index
```

A local sketchbook platform copy or symlink can prevent the Boards Manager package from appearing or being installable.

If `package_nrf54l15clean_stable_index.json` does not show the board either, treat that as the same problem. In a clean Arduino CLI home, both index URLs install the package correctly.

### `Platform 'nrf54l15clean:0.1.0' not found`

This means a stale FQBN is still being used from an older local-platform layout.

- use FQBN: `nrf54l15clean:nrf54l15clean:xiao_nrf54l15`
- reselect board in IDE: `Tools -> Board -> XIAO nRF54L15 (Nrf54L15-Clean-Implementation)`
- if needed, remove local sketchbook override `~/Arduino/hardware/nrf54l15clean` and reinstall from Boards Manager

### Upload Fails With Probe Errors

If upload reports:

- `No connected debug probes`
- `Failed to read memory at 0xe000ed00`
- `DP initialisation failed`
- `AP write error`

then:

- keep `Upload Method` on `Auto` or explicitly choose `pyOCD`
- if Linux permissions are the issue, add this rule:

```bash
cat <<'RULE' | sudo tee /etc/udev/rules.d/60-seeed-xiao-nrf54-cmsis-dap.rules >/dev/null
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="2886", ATTRS{idProduct}=="0066", MODE="0660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="2886", ATTR{idProduct}=="0066", MODE="0660", GROUP="plugdev", TAG+="uaccess"
RULE
sudo udevadm control --reload-rules
sudo udevadm trigger --attr-match=idVendor=2886 --attr-match=idProduct=0066
```

### Sketch Compiles Or Runs Wrong On An Older Release

Before debugging behavior, update the core to the latest release.

That especially applies if you see:

- missing sketch aliases like `IMU_MIC_EN`, `RF_SW`, `VBAT_EN`
- `Wire1.begin()` / USB serial interaction from older releases
- low-power issues on old builds
- RF switch / BLE antenna routing surprises from old builds
- old Adafruit / SSD1306 compatibility failures already fixed in newer releases

### `AnalogReadSerial` Is Stuck Near Full Scale

- make sure a previous sketch is not still driving `A0` as a digital output
- retest with [`AnalogReadSerial`](hardware/nrf54l15clean/nrf54l15clean/examples/01.Basics/AnalogReadSerial)

### Serial Output Looks Corrupted

- verify Serial Monitor baud matches `Serial.begin(...)`
- update to the latest release before investigating further

### `Wire1` And USB Logging

Current main routes:

- `Wire` -> `TWIM22` on `D4/D5`
- `Wire1` -> `TWIM30` on `D12/D11`

So `Serial` + `Wire` + `Wire1` can run together. For the `D11/D12` bus, use `Wire1.begin()` directly.

## More Docs

- [Board Reference](docs/board-reference.md)
- [Development Notes](docs/development.md)
- [Bundled HAL / BLE library README](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/README.md)
- [Releases](https://github.com/lolren/NRF54L15-Clean-Arduino-core/releases)

## Support

If this project helps you, you can support it here:

- https://buymeacoffee.com/lolren
