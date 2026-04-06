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
- POWER / RESET / REGULATORS / GRTC control
- BLE legacy and extended advertising, active/passive scan, stable connected-link scheduling, ATT/GATT peripheral and client flows, Nordic UART Service transport, and Bluefruit/Seeed-style wrapper support
- Zigbee HA coordinator / light / sensor examples plus lower-level 802.15.4 bring-up helpers
- early channel sounding bring-up hooks and examples
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

## BLE Status

BLE is no longer in first-pass bring-up. The practical BLE paths shipped in the
core are working on real XIAO hardware and are the main feature set that should
be used today.

Validated and working:

- legacy advertising, scannable/connectable advertising, and extended
  advertising examples
- active and passive scanning
- stable peripheral and central links on nRF54<->nRF54 and nRF54<->nRF52840
  pairs
- bundled ATT/GATT examples for 16-bit and 128-bit custom services
- Nordic UART Service (NUS), including the native bridge sketches and the
  Bluefruit BLE UART wrapper flow
- Bluefruit-style central/peripheral wrapper examples used for nRF52 sketch
  compatibility

What that means in practice:

- advertising and discovery work on the normal tested paths
- central-side discovery/notify regressions from earlier releases are fixed
- NUS transport works in both directions on the validated host and board tests
- Bluefruit compatibility is good enough that most BLE sketch ports do not need
  to be rewritten from scratch

What is still not “complete BLE coverage”:

- not every Bluetooth LE feature in the spec is implemented
- phone-specific scanner quirks may still exist on some devices
- channel sounding is only partial/experimental, not full end-user support
- the supported BLE API surface is the tested shipped surface, not a promise
  that every possible Bluefruit example from upstream has full runtime parity

## nRF52840 Sketch Compatibility

The repo bundles a `Bluefruit52Lib` compatibility layer plus a small set of
nRF52-style core shims so a large part of the XIAO nRF52840 / Seeed nRF52 /
Bluefruit sketch style now carries over to the XIAO nRF54L15.

The goal here is sketch compatibility, not a new API. For common BLE ports, the
intended path is:

- keep the same Bluefruit-style sketch structure
- reuse the familiar BLEUart / scanner / central / peripheral calls
- only adjust the sketch where it depends on an extra library or on an
  upstream feature that is still outside this core’s tested surface

The package exposes that compatibility in Arduino IDE with:

- curated `Bluefruit52Lib -> nRF52Compat` examples for direct porting
- broader Bluefruit example menus grouped by use case:
  `Advertising`, `Central`, `Diagnostics`, `DualRoles`, `HID`, `Projects`,
  `Security`, and `Services`

In practice, the compatibility layer is in good shape for the standard BLE
paths that matter most when moving sketches from XIAO nRF52840 to nRF54L15:

- BLE UART / NUS
- scanning and device discovery
- common peripheral services
- common central discovery and notify flows

Remaining compile misses are generally optional third-party library
dependencies, not the wrapper itself.

## Zigbee Status

Zigbee is now past demo-only radio bring-up and into real interoperability
work.

What is working and tested:

- local coordinator / joinable end-device flows on two XIAO nRF54L15 boards
- Home Assistant / Zigbee2MQTT style join and interview on the validated HA
  light path
- on/off light control after join
- retained network state and secure rejoin on the tested HA examples
- example coverage for coordinator, router, end device, lights, sensors,
  low-power sleepy devices, and simple interoperability demos

The shipped Zigbee examples are now organized by device role and use case so
they read more like real products than lab fragments. There are dedicated
examples for:

- coordinator bring-up
- basic routers and end devices
- HA on/off and dimmable lights
- temperature/battery sensor style endpoints
- sleepy low-power sensor examples with periodic wake/report cycles
- simple ping/pong style interoperability tests

Still incomplete on Zigbee:

- this is not full Zigbee 3.0 parity yet
- color / RGB light clusters are not implemented
- the external coordinator story is now real for the validated path, but not
  every cluster/profile combination has been exercised
- Matter over Thread / Matter over Wi-Fi is not implemented here

## Low Power Status

Low-power support is present and usable, but it should be understood in layers.

Working:

- `WFI` idle examples
- true `SYSTEM OFF` examples with wake sources
- Zigbee sleepy end-device examples with configurable wake/report intervals

Not yet fully productized:

- there is no full board-by-board current table in the README
- the examples are validated functionally first; some scenarios still need more
  current-draw characterization

## What Is Still Missing

This project is already useful for real BLE and Zigbee work, but it is not a
finished “everything included” wireless stack.

Still missing or intentionally incomplete:

- full user-facing channel sounding support
- Matter
- broad color-light / richer Zigbee HA device coverage
- complete parity with every upstream Bluefruit example and every optional
  dependency
- broader automated runtime coverage across more phones and external Zigbee
  coordinators

## Examples

### Board Examples

Board examples live under [`hardware/nrf54l15clean/nrf54l15clean/examples`](hardware/nrf54l15clean/nrf54l15clean/examples).
This menu is intentionally curated for board/core-specific sketches that are not already covered better by the implementation library.
Most peripheral, BLE, and Zigbee demos now live under the library example menu instead of the board menu.

In Arduino IDE they should appear under:

- `File -> Examples -> Examples for XIAO nRF54L15 (Nrf54L15-Clean-Implementation) -> Basics`
- `... -> Peripherals`
- `... -> Power`

Suggested starting points:

- Basics: [`CoreVersionProbe`](hardware/nrf54l15clean/nrf54l15clean/examples/Basics/CoreVersionProbe)
- Peripherals: [`RuntimePeripheralPinRemap`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/RuntimePeripheralPinRemap), [`WireImuRemapScanner`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/WireImuRemapScanner), [`XiaoBoardControlPins`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/XiaoBoardControlPins), [`VbatReadViaAnalogRead`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/VbatReadViaAnalogRead), [`WireRepeatedStartProbe`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/WireRepeatedStartProbe), [`WireTargetResponder`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/WireTargetResponder), [`InterruptPwmApiProbe`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/InterruptPwmApiProbe), [`PeripheralProbe`](hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/PeripheralProbe)
- Power: [`DelayAutoLowPowerMeasure`](hardware/nrf54l15clean/nrf54l15clean/examples/Power/DelayAutoLowPowerMeasure), [`SystemOffWakeDiag`](hardware/nrf54l15clean/nrf54l15clean/examples/Power/SystemOffWakeDiag), [`SystemOffWakeOnceDiag`](hardware/nrf54l15clean/nrf54l15clean/examples/Power/SystemOffWakeOnceDiag)

Bundled library examples for `EEPROM`, `Preferences`, `Nrf54L15-Clean-Implementation`, and `Bluefruit52Lib` appear in their own library menus.

For deeper Zigbee details, use the checked-in docs instead of relying on the
README as a changelog:

- [Zigbee Feature Matrix](docs/ZIGBEE_FEATURE_MATRIX.md)
- [Zigbee 3.0 Parity Plan](docs/ZIGBEE_3P0_PARITY_PLAN.md)
- [Zigbee External Coordinator Flow](docs/ZIGBEE_EXTERNAL_COORDINATOR_FLOW.md)

### Library Examples

The bundled HAL/BLE library examples live under [`hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples`](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples).
The bundled Bluefruit compatibility examples live under [`hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/examples`](hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/examples).

In Arduino IDE they now appear under:

- `File -> Examples -> Nrf54L15-Clean-Implementation -> BLE`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> LowPower`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Diagnostics`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Board`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Peripherals`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Zigbee`
- `File -> Examples -> Bluefruit52Lib -> Advertising`
- `File -> Examples -> Bluefruit52Lib -> Central`
- `File -> Examples -> Bluefruit52Lib -> Diagnostics`
- `File -> Examples -> Bluefruit52Lib -> DualRoles`
- `File -> Examples -> Bluefruit52Lib -> HID`
- `File -> Examples -> Bluefruit52Lib -> nRF52Compat`
- `File -> Examples -> Bluefruit52Lib -> Peripheral`
- `File -> Examples -> Bluefruit52Lib -> Projects`
- `File -> Examples -> Bluefruit52Lib -> Security`
- `File -> Examples -> Bluefruit52Lib -> Services`

Recommended library examples:

- Low-power floor measurement: `LowPowerZephyrParityBlink` (`5 ms` pulse, meter-oriented)
- Visible timed system off check: `LowPowerDelaySystemOff`
- Idle CPU scaling: `LowPowerIdleCpuScaling`
- Continuous low-power BLE: `BleAdvertiserLowestPowerContinuous`, `BleAdvertiserRfSwitchDutyCycle`
- Burst/beacon BLE: `BleAdvertiserPhoneBeacon15s`, `BleAdvertiserHybridDutyCycle`, `BleAdvertiserBurstSystemOff`
- Zigbee: `ZigbeeCoordinator`, `ZigbeeRouter`, `ZigbeeEndDevice`, `ZigbeePingInitiator`, `ZigbeePongResponder`, `ZigbeeStackCodecSelfTest`, `ZigbeeHaCoordinatorJoinDemo`, `ZigbeeHaOnOffLightStatic`, `ZigbeeHaOnOffLightJoinable`, `ZigbeeHaDimmableLightStatic`, `ZigbeeHaDimmableLightJoinable`, `ZigbeeHaTemperatureSensorStatic`, `ZigbeeHaTemperatureSensorJoinable`
- BLE diagnostics: `BleAdvertiserProbe`, `BlePassiveScanner`, `BleActiveScanner`, `BleExtendedScanner`, `BleExtendedActiveScanner`, `BleLegacyAdv31Plus31`, `BleExtendedAdv251`, `BleExtendedScannableAdv251`, `BleExtendedAdv499`, `BleExtendedAdv995`, `BleConnectionPeripheral`, `BleGattBasicPeripheral`
- Bluefruit/nRF52 compatibility starter pack: `central_bleuart`, `central_scan`, `central_notify`, `dual_bleuart`, `beacon`, `custom_hrm`, `notify_peripheral`, `pairing_pin`
- Bluefruit wrapper categories: `Advertising` (`adv_advanced`, `beacon`, `eddystone_url`), `Central` (`central_bleuart_multi`, `central_hid`, `central_pairing`, `central_scan_advanced`), `Diagnostics` (`throughput`, `rssi_callback`, `rssi_poll`), `HID` (`blehid_keyboard`, `blehid_mouse`, `blehid_gamepad`), `Security` (`pairing_passkey`, `clearbonds`), `Services` (`bleuart`, `bleuart_multi`, `custom_hrm`, `custom_htm`, `client_cts`, `ancs`), `Projects` (`rssi_proximity_central`, `rssi_proximity_peripheral`), plus the existing `Peripheral` menu
- Peripheral bring-up: `RawI2sTxInterrupt`, `I2sTxWrapperInterrupt`, `I2sRxWrapperInterrupt`, `I2sDuplexWrapperInterrupt`, `RawRadioPacketTx`, `RawRadioPacketRx`, `RawRadioAckRequester`, `RawRadioAckResponder`
- `I2sTxWrapperInterrupt` shows the callback-based refill path, where the next buffer is generated from the I2S IRQ instead of managed manually in the sketch loop
- `I2sRxWrapperInterrupt` shows the matching receive path, where completed RX buffers are handed to a callback from the same `I2S20` IRQ service model
- `I2sDuplexWrapperInterrupt` combines both directions on one `I2S20` instance and supports a simple one-board loopback with a jumper from `D11` to `D15`

Two-board extended advertising regression:

- `scripts/ble_extended_adv_dual_board_regression.py --advertiser 995`
- `scripts/ble_extended_adv_dual_board_regression.py --advertiser 499`
- expects two XIAO nRF54L15 boards on separate `/dev/ttyACM*` ports
- auto-resolves each board's CMSIS-DAP UID from the serial port and flashes deterministically
- Host-side NUS HCI trace regression: `scripts/ble_nus_btmon_regression.py --iterations 12`
- Pure-BLE NUS loopback regression: `scripts/ble_nus_loopback_btmon_regression.py --iterations 64`
- Bring-up: `CleanBringUp`, `PeripheralSelfTest`, `FeatureParitySelfTest`

The Zigbee examples now cover more than raw radio bring-up. The practical set
for this release is:

- coordinator and joinable HA light demos for two-board work
- Zigbee2MQTT / Home Assistant join and interview on the validated light path
- retained network state and secure rejoin on the shipped HA examples
- sleepy sensor demos that wake, report, poll, and return to `SYSTEM OFF`

It is still not a full Zigbee 3.0 stack with every cluster profile or every
coordinator combination exercised. The detailed state of that work stays in the
Zigbee docs listed above.

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
- `delay()` / `yield()` low-power idle path: low-power `delay()` again uses the
  XIAO board-collapse heuristic for long idle sleeps when the bridge is inactive,
  while `yield()` stays board-state-neutral

On the XIAO low-power profile, plain `delay()` now restores the old
save/collapse/restore behavior for sufficiently long sleeps unless
`NRF54L15_CLEAN_DELAY_KEEP_BOARD_STATE=1` is defined at build time.
For the explicit always-collapse helper, use `delayLowPowerIdle(ms)`.

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

Use these library examples together:

- [`BleChannelSoundingReflector`](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingReflector)
- [`BleChannelSoundingInitiator`](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/ChannelSounding/BleChannelSoundingInitiator)

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

### XIAO Pinout

![XIAO nRF54L15 pinout](docs/xiao_nrf54l15_default_pin_routes.png)

PWM on this pinout:

- `D0-D5`: real hardware PWM pins
- `D6-D15`: software PWM fallback
- `LED_BUILTIN`: not an `analogWrite()` PWM pin in this core

### PWM On XIAO nRF54L15

- `analogWrite()` PWM is available on `D0-D15`.
- `D0-D5` are the real hardware PWM pins. They are `P1` pins and use the shared `PWM20` path for normal `analogWrite()`.
- `analogWriteFrequency(hz)` sets the shared/default PWM frequency. On `D0-D5` it changes the shared `PWM20` frequency, and on `D6-D15` it changes the default software-PWM period.
- `analogWritePinFrequency(pin, hz)` is the per-pin API for `D0-D5`. It uses `TIMER20-24 + GPIOTE20 + DPPIC20`, so sketches can give individual `D0-D5` pins different PWM frequencies.
- The shared `PWM20` path can drive up to 4 hardware channels at once.
- The per-pin timer-backed path can drive up to 5 independent `D0-D5` pins at once. If a sketch asks for more pin-specific frequencies than that, extra outputs fall back to software PWM.
- `D6-D15` are software PWM only.
- `LED_BUILTIN` is still not an `analogWrite()` PWM pin on this board.

Practical rule:

- use `analogWrite(pin, value)` on `D0-D5` when you just want normal hardware PWM
- use `analogWritePinFrequency(pin, hz)` before `analogWrite(...)` when you want a different frequency on a specific `D0-D5` pin
- use `D6-D15` only when software PWM is acceptable
- start with `AnalogWriteHardwarePwmFade` for the shared `PWM20` path and `AnalogWritePerPinFrequency` for the timer-backed per-pin path

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
  A symlink there also counts as an override and can shadow the Boards Manager package.
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
  A symlink there can make Arduino IDE show a different package/example tree than the installed release.
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
