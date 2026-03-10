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
- `RawRadioLink` helper for proprietary 1 Mbit packet TX/RX on `RADIO`
- thin `nrf_to_nrf`-style compatibility layer for common RF24-like sketch flows on top of `RawRadioLink`
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
- Power: [`DelayLowPowerIdleBlink`](hardware/nrf54l15clean/nrf54l15clean/examples/Power/DelayLowPowerIdleBlink), [`LowPowerIdleTicker`](hardware/nrf54l15clean/nrf54l15clean/examples/Power/LowPowerIdleTicker), [`DelaySystemOffBlink`](hardware/nrf54l15clean/nrf54l15clean/examples/Power/DelaySystemOffBlink), [`IdleCpuScalingBlink`](hardware/nrf54l15clean/nrf54l15clean/examples/Power/IdleCpuScalingBlink)
- Peripherals: [`PeripheralProbe`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/PeripheralProbe), [`RawI2sTxLoop`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/RawI2sTxLoop), [`RawI2sTxInterrupt`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/RawI2sTxInterrupt), [`I2sTxWrapperInterrupt`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/I2sTxWrapperInterrupt), [`I2sRxWrapperInterrupt`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/I2sRxWrapperInterrupt), [`I2sDuplexWrapperInterrupt`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/I2sDuplexWrapperInterrupt), [`RawRadioPacketTx`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/RawRadioPacketTx), [`RawRadioPacketRx`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/RawRadioPacketRx), [`RawRadioAckRequester`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/RawRadioAckRequester), [`RawRadioAckResponder`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/RawRadioAckResponder), [`nrf_to_nrfGettingStarted`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/nrf_to_nrfGettingStarted), [`nrf_to_nrfAcknowledgementPayloads`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/nrf_to_nrfAcknowledgementPayloads), [`WireImuRemapScanner`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/WireImuRemapScanner), [`XiaoBoardControlPins`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/XiaoBoardControlPins)
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
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Peripherals`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Zigbee`

Recommended library examples:

- Low-power floor: `LowPowerZephyrParityBlink`
- Arduino-style timed system off: `LowPowerDelaySystemOff`
- Idle CPU scaling: `LowPowerIdleCpuScaling`
- Continuous low-power BLE: `BleAdvertiserLowestPowerContinuous`, `BleAdvertiserRfSwitchDutyCycle`
- Burst/beacon BLE: `BleAdvertiserPhoneBeacon15s`, `BleAdvertiserHybridDutyCycle`, `BleAdvertiserBurstSystemOff`
- Zigbee: `ZigbeeCoordinator`, `ZigbeeRouter`, `ZigbeeEndDevice`, `ZigbeePingInitiator`, `ZigbeePongResponder`
- BLE diagnostics: `BleAdvertiserProbe`, `BlePassiveScanner`, `BleActiveScanner`, `BleConnectionPeripheral`, `BleGattBasicPeripheral`
- Peripheral bring-up: `RawI2sTxInterrupt`, `I2sTxWrapperInterrupt`, `I2sRxWrapperInterrupt`, `I2sDuplexWrapperInterrupt`, `RawRadioPacketTx`, `RawRadioPacketRx`, `RawRadioAckRequester`, `RawRadioAckResponder`, `nrf_to_nrfGettingStarted`, `nrf_to_nrfAcknowledgementPayloads`
- `I2sTxWrapperInterrupt` shows the callback-based refill path, where the next buffer is generated from the I2S IRQ instead of managed manually in the sketch loop
- `I2sRxWrapperInterrupt` shows the matching receive path, where completed RX buffers are handed to a callback from the same `I2S20` IRQ service model
- `I2sDuplexWrapperInterrupt` combines both directions on one `I2S20` instance and supports a simple one-board loopback with a jumper from `D11` to `D15`

Two-board `nrf_to_nrf` regression:

- `scripts/nrf_to_nrf_dual_board_regression.py --example ack-payloads`
- `scripts/nrf_to_nrf_dual_board_regression.py --example getting-started`
- expects two XIAO nRF54L15 boards on separate `/dev/ttyACM*` ports
- auto-resolves each board's CMSIS-DAP UID from the serial port and flashes deterministically
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
- long-sleep phone-tuned beaconing is now available as `BleAdvertiserPhoneBeacon15s`, which keeps the payload in the primary ADV packet, avoids scan-response dependence, and spends most of its cycle in true `SYSTEM OFF`
- `delay()` / `yield()` low-power idle path: around **0.1 mA** on this board after the core WFI and tickless-delay fixes
- `delayLowPowerIdle(ms)` now provides an explicit Zephyr-like `System ON` measurement helper without changing normal `delay()` semantics or adding another Tools profile

That puts the `SYSTEM OFF` path in the same broad regime as the Zephyr result on this board.

New core-level low-power helpers:

- `delaySystemOff(ms)`: timed `SYSTEM OFF` sleep with cold-boot wake, preserving `.noinit` RAM by default
- `delaySystemOffNoRetention(ms)`: same path, but clears RAM retention for the lowest current
- `delayLowPowerIdle(ms)`: timed `System ON` idle that temporarily collapses the XIAO board-control rails and restores the previous GPIO state after wake
- avoid active bridge `Serial` traffic during the low-power window; the helper temporarily releases the SAMD11 bridge pins at the GPIO level, but it does not suspend an already-running UARTE session for you
- use `Low Power (WFI Idle)` for the lowest current; in balanced mode the helper still resumes normally, but wake timing remains SysTick-driven
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

The channel-sounding examples are a **two-board phase-based BLE tone-sounding tool** built on the nRF54L15 radio's `CSTONES`/DFE hardware.

How it works:

- The initiator sends a control packet on BLE channel `37`, then sweeps BLE data channels `0..36` with a tone-extended probe.
- The reflector captures the probe tone with `RADIO.CSTONES`, returns its own captured IQ term in a report, and repeats for each requested channel.
- The initiator captures the response tone locally, combines both endpoints' IQ terms, and fits phase slope versus frequency to estimate distance.
- The initiator logs calibrated `dist_m`, `median_raw_m`, `median_cal_m`, `accepted_sweeps`, `display_ok`, `valid_channels`, `residual`, plus tone-quality counters so you can distinguish stable estimates from noisy sweeps.
- The initiator also supports live serial calibration commands (`status`, `zero`, `ref <m>`, `offset <m>`, `scale <factor>`, `clear`) and now logs both raw and calibrated phase distances.
- The reflector logs reply throughput so you can verify the two-board exchange is alive.

Use these board examples together:

- [`BleChannelSoundingReflector`](hardware/nrf54l15clean/nrf54l15clean/examples/BLE/BleChannelSoundingReflector)
- [`BleChannelSoundingInitiator`](hardware/nrf54l15clean/nrf54l15clean/examples/BLE/BleChannelSoundingInitiator)

What it is good for:

- validating a clean register-level phase-sounding path on two XIAO nRF54L15 boards
- experimenting with near-field ranging and channel-by-channel phase behavior
- checking sweep quality from `valid_channels` and fit quality from `residual`

What it is not:

- not the full Bluetooth Channel Sounding Link Layer procedure
- not a calibrated production ranging stack
- not yet interoperable with a standard BLE CS controller/host stack

Controller-backed reference validation:

- [Zephyr/NCS channel-sounding validation](docs/zephyr-channel-sounding-validation.md)
- [Channel-sounding calibration workflow](docs/channel-sounding-calibration.md)
- the official Zephyr `connected_cs` sample has been built, flashed, and observed on the same XIAO hardware to enable CS procedures and emit both RTT and phase estimates

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

Linux upload note:

- when multiple XIAO nRF54L15 boards are connected, `arduino-cli upload -p /dev/ttyACM* ...` now maps the selected serial port back to the matching CMSIS-DAP probe automatically

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
