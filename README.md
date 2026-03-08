# XIAO nRF54L15 Clean Arduino Core

Open-source Arduino board package for **Seeed XIAO nRF54L15** with a secure single-image, register-level implementation.

- Board package: `Nrf54L15-Clean-Implementation`
- Board: `XIAO nRF54L15 (Nrf54L15-Clean-Implementation)`
- FQBN: `nrf54l15clean:nrf54l15clean:xiao_nrf54l15`
- Runtime model: no Zephyr runtime, no nRF Connect SDK runtime
- Default build mode: secure single image

## What This Repo Provides

This repo ships two things:

1. A normal Arduino Boards Manager package for XIAO nRF54L15.
2. A bundled register-level HAL/BLE library used by the board core and exposed to sketches.

Current scope:

- GPIO, clock, SPI, I2C, UART, ADC, TIMER, PWM, GPIOTE, TEMP, WDT, PDM
- Raw `NRF_RADIO` and `NRF_I2S` register access for low-level library ports
- POWER / RESET / REGULATORS / GRTC control
- BLE legacy advertising, active/passive scan, connectable/scannable advertiser flow, and minimal ATT/GATT peripheral path
- Zigbee-oriented 802.15.4 PHY/MAC-lite examples
- Low-power `WFI` and true `SYSTEM OFF` paths on XIAO

## Install

Boards Manager URL:

```text
https://raw.githubusercontent.com/lolren/NRF54L15-Clean-Arduino-core/main/package_nrf54l15clean_index.json
```

Install `Nrf54L15-Clean-Implementation`, then select:

- board: `XIAO nRF54L15 (Nrf54L15-Clean-Implementation)`
- upload method: `Auto`

CLI:

```bash
arduino-cli core update-index \
  --additional-urls https://raw.githubusercontent.com/lolren/NRF54L15-Clean-Arduino-core/main/package_nrf54l15clean_index.json

arduino-cli core install nrf54l15clean:nrf54l15clean \
  --additional-urls https://raw.githubusercontent.com/lolren/NRF54L15-Clean-Arduino-core/main/package_nrf54l15clean_index.json
```

## Examples

### Board Examples

Board examples live under [`hardware/nrf54l15clean/nrf54l15clean/examples`](hardware/nrf54l15clean/nrf54l15clean/examples).

In Arduino IDE they should appear under:

- `File -> Examples -> Examples for XIAO nRF54L15 (Nrf54L15-Clean-Implementation) -> Basics`
- `... -> BLE`
- `... -> Memory`
- `... -> Peripherals`
- `... -> Power`
- `... -> Zigbee`

Suggested starting points:

- Basics: [`Blink`](hardware/nrf54l15clean/nrf54l15clean/examples/Basics/Blink), [`AnalogReadSerial`](hardware/nrf54l15clean/nrf54l15clean/examples/Basics/AnalogReadSerial)
- Power: [`LowPowerIdleTicker`](hardware/nrf54l15clean/nrf54l15clean/examples/Power/LowPowerIdleTicker), [`DelaySystemOffBlink`](hardware/nrf54l15clean/nrf54l15clean/examples/Power/DelaySystemOffBlink), [`IdleCpuScalingBlink`](hardware/nrf54l15clean/nrf54l15clean/examples/Power/IdleCpuScalingBlink)
- Peripherals: [`PeripheralProbe`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/PeripheralProbe), [`RawI2sTxLoop`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/RawI2sTxLoop), [`RawRadioPacketTx`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/RawRadioPacketTx), [`RawRadioPacketRx`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/RawRadioPacketRx), [`WireImuRemapScanner`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/WireImuRemapScanner), [`XiaoBoardControlPins`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/XiaoBoardControlPins)
- BLE: [`BleBeaconMinimal`](hardware/nrf54l15clean/nrf54l15clean/examples/BLE/BleBeaconMinimal), [`BleChannelSoundingReflector`](hardware/nrf54l15clean/nrf54l15clean/examples/BLE/BleChannelSoundingReflector), [`BleChannelSoundingInitiator`](hardware/nrf54l15clean/nrf54l15clean/examples/BLE/BleChannelSoundingInitiator), [`RawRadioRegisterProbe`](hardware/nrf54l15clean/nrf54l15clean/examples/BLE/RawRadioRegisterProbe)
- Memory: [`PreferencesBootCounter`](hardware/nrf54l15clean/nrf54l15clean/examples/Memory/PreferencesBootCounter), [`EEPROMBootCounter`](hardware/nrf54l15clean/nrf54l15clean/examples/Memory/EEPROMBootCounter)
- Zigbee: [`ZigbeeCoordinator`](hardware/nrf54l15clean/nrf54l15clean/examples/Zigbee/ZigbeeCoordinator), [`ZigbeeEndDevice`](hardware/nrf54l15clean/nrf54l15clean/examples/Zigbee/ZigbeeEndDevice)

### Library Examples

The bundled HAL/BLE library examples live under [`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples`](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples).

In Arduino IDE they now appear under:

- `File -> Examples -> Nrf54L15-Clean-Implementation -> BLE`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> LowPower`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Diagnostics`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Board`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Zigbee`

Recommended library examples:

- Low-power floor: `LowPowerZephyrParityBlink`
- Arduino-style timed system off: `LowPowerDelaySystemOff`
- Idle CPU scaling: `LowPowerIdleCpuScaling`
- Continuous low-power BLE: `BleAdvertiserLowestPowerContinuous`, `BleAdvertiserRfSwitchDutyCycle`
- Burst/beacon BLE: `BleAdvertiserHybridDutyCycle`, `BleAdvertiserBurstSystemOff`
- Zigbee: `ZigbeeCoordinator`, `ZigbeeRouter`, `ZigbeeEndDevice`, `ZigbeePingInitiator`, `ZigbeePongResponder`
- BLE diagnostics: `BleAdvertiserProbe`, `BlePassiveScanner`, `BleActiveScanner`, `BleConnectionPeripheral`, `BleGattBasicPeripheral`
- Bring-up: `CleanBringUp`, `PeripheralSelfTest`, `FeatureParitySelfTest`

## Power And Zephyr Parity

This core now reproduces the same **class of low-power behavior** we were chasing in Zephyr on XIAO nRF54L15.

What mattered was not only the final `SYSTEMOFF` write. The working path required:

- secure peripheral map
- Zephyr-like secure startup writes in `SystemInit()`
- oscillator trim and regulator setup parity
- correct secure-domain GRTC wake programming
- board rail shutdown before `SYSTEM OFF`
- optional RAM retention clear only for the explicit ultra-low-power paths

Practical result on the XIAO board from local validation:

- true `SYSTEM OFF` blink / burst-beacon paths: **tens of uA**
- continuous low-power BLE with RF-switch duty-cycling: about **0.1 mA**
- `delay()` / `yield()` low-power idle path: around **0.1 mA** on this board after the core WFI and tickless-delay fixes

That puts the `SYSTEM OFF` path in the same broad regime as the Zephyr result on this board.

New core-level low-power helpers:

- `delaySystemOff(ms)`: timed `SYSTEM OFF` sleep with cold-boot wake, preserving `.noinit` RAM by default
- `delaySystemOffNoRetention(ms)`: same path, but clears RAM retention for the lowest current
- `ClockControl::enableIdleCpuScaling(CpuFrequency::k64MHz)`: keep active code at the current CPU speed, but drop to 64 MHz around `delay()` / `yield()` idle windows and restore on wake

Important distinction:

- `SYSTEM OFF` parity is now real and reproducible in this core
- continuous BLE advertising is still not controller-equivalent to Zephyr, because this core manually emits raw legacy advertising events rather than running Zephyr's controller scheduler

Relevant docs:

- [Zephyr low-power parity](docs/low-power-zephyr-parity.md)
- [Low-power BLE patterns](docs/low-power-ble-patterns.md)
- [BLE advertising validation](docs/ble-advertising-validation.md)
- [Power profile measurements](POWER_PROFILE_MEASUREMENTS.md)

## Channel Sounding

The channel-sounding examples are a **two-board BLE advertising-channel RSSI tool**, not phase-based ranging.

How it works:

- The reflector advertises with a fixed static-random address and accepts scan requests.
- The initiator actively scans channels `37`, `38`, and `39` and filters for that reflector address.
- The initiator logs per-channel hit counts, average RSSI, scan-response RSSI, and a rough RSSI-derived distance estimate.
- The reflector logs scan-request and scan-response activity plus per-channel RSSI seen at the reflector side.

Use these board examples together:

- [`BleChannelSoundingReflector`](hardware/nrf54l15clean/nrf54l15clean/examples/BLE/BleChannelSoundingReflector)
- [`BleChannelSoundingInitiator`](hardware/nrf54l15clean/nrf54l15clean/examples/BLE/BleChannelSoundingInitiator)

What it is good for:

- identifying which advertising channel is currently strongest
- comparing channel quality between two placements
- getting a rough distance heuristic from RSSI

What it is not:

- not time-of-flight ranging
- not phase-based ranging
- not a calibrated distance system

## Board Notes

Default peripheral routes and board-control helpers are documented in [Board Reference](docs/board-reference.md).

Useful board-control calls:

```cpp
#include "nrf54l15_hal.h"
using namespace xiao_nrf54l15;

int32_t vbatMv = 0;
BoardControl::sampleBatteryMilliVolts(&vbatMv);
BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
BoardControl::setRfSwitchPowerEnabled(false);
```

## Troubleshooting

### Board Does Not Appear In Boards Manager

Most common cause: a local sketchbook override is shadowing the package.

Check:

- the package index URL matches the one in `Install`
- remove stale local override `~/Arduino/hardware/nrf54l15clean`
- refresh indexes:

```bash
rm -f ~/.arduino15/package_nrf54l15clean_index.json \
      ~/.arduino15/package_nrf54l15clean_stable_index.json
arduino-cli core update-index
```

### Examples Are Missing In Arduino IDE

Check:

- the selected board is `XIAO nRF54L15 (Nrf54L15-Clean-Implementation)`
- there is no stale `~/Arduino/hardware/nrf54l15clean` override
- restart Arduino IDE after reinstall so the example tree is rebuilt

CLI sanity check:

```bash
find ~/.arduino15/packages/nrf54l15clean/hardware -path '*/examples/*/*.ino' -print
```

### Upload Fails

Use `Upload Method = Auto` unless you have a reason to force a runner.

If Linux sees the CMSIS-DAP probe in `lsusb` but Arduino says there is no probe, add a udev rule:

```bash
cat <<'RULE' | sudo tee /etc/udev/rules.d/60-seeed-xiao-nrf54-cmsis-dap.rules >/dev/null
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="2886", ATTRS{idProduct}=="0066", MODE="0660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="2886", ATTR{idProduct}=="0066", MODE="0660", GROUP="plugdev", TAG+="uaccess"
RULE
sudo udevadm control --reload-rules
sudo udevadm trigger --attr-match=idVendor=2886 --attr-match=idProduct=0066
```

## More Docs

- [Board Reference](docs/board-reference.md)
- [Zephyr low-power parity](docs/low-power-zephyr-parity.md)
- [Low-power BLE patterns](docs/low-power-ble-patterns.md)
- [BLE advertising validation](docs/ble-advertising-validation.md)
- [Power profile measurements](POWER_PROFILE_MEASUREMENTS.md)
- [Development Notes](docs/development.md)
- [Bundled HAL / BLE library README](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/README.md)
- [Releases](https://github.com/lolren/NRF54L15-Clean-Arduino-core/releases)
