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

Bundled library examples for `EEPROM`, `Preferences`, and `Nrf54L15-Clean-Implementation` appear in their own library menus.

Current stack status is tracked in [Zigbee Feature Matrix](docs/ZIGBEE_FEATURE_MATRIX.md). The older checked-in coordinator/router/end-device sketches are still PHY/MAC-lite demos, while the clean stack now also includes joinable coordinator/light/dimmable-light/temperature-sensor demos on top of `zigbee_stack.h/.cpp`, along with a shared `zigbee_commissioning` end-device state machine for scan/association/rejoin, trust-center wait-state polling, retry/timeout handling, negotiated End Device Timeout requests, retained-network fallback scanning across configured channel masks, MAC orphan notification plus coordinator realignment for retained-key parent recovery, NWK-secured rejoin request/response before reassociation fallback, clean Identify/Groups/Scenes handling for HA light endpoints, ZDO bind/unbind plus IEEE/NWK-address handling, management leave support on the joinable HA endpoints including leave-with-rejoin handling, install-code-derived link-key support, persisted trust-center identity and inbound APS anti-replay state, retained-key demo rejoin behavior on the joinable examples, alternate demo network-key persistence plus APS-secured Switch Key acceptance on those end devices, APS-secured Update Device acceptance for the secure-rejoin follow-up, trust-center source/state validation for `Update Device` and `Switch Key`, bounded unicast APS retransmission plus duplicate suppression between the clean coordinator and joinable examples, timed permit-join enforcement in the clean coordinator demo, a polled demo network-key update rollout, and demo APS group-addressed light control. The execution plan for the first three remaining Zigbee 3.0 blockers is tracked in [Zigbee 3.0 Parity Plan](docs/ZIGBEE_3P0_PARITY_PLAN.md), and the coordinator-facing expected packet flow for future ZHA/Zigbee2MQTT bring-up is tracked in [Zigbee External Coordinator Flow](docs/ZIGBEE_EXTERNAL_COORDINATOR_FLOW.md).

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

- Low-power floor measurement: `LowPowerZephyrParityBlink` (`5 ms` pulse, meter-oriented)
- Visible timed system off check: `LowPowerDelaySystemOff`
- Idle CPU scaling: `LowPowerIdleCpuScaling`
- Continuous low-power BLE: `BleAdvertiserLowestPowerContinuous`, `BleAdvertiserRfSwitchDutyCycle`
- Burst/beacon BLE: `BleAdvertiserPhoneBeacon15s`, `BleAdvertiserHybridDutyCycle`, `BleAdvertiserBurstSystemOff`
- Zigbee: `ZigbeeCoordinator`, `ZigbeeRouter`, `ZigbeeEndDevice`, `ZigbeePingInitiator`, `ZigbeePongResponder`, `ZigbeeStackCodecSelfTest`, `ZigbeeHaCoordinatorJoinDemo`, `ZigbeeHaOnOffLightStatic`, `ZigbeeHaOnOffLightJoinable`, `ZigbeeHaDimmableLightStatic`, `ZigbeeHaDimmableLightJoinable`, `ZigbeeHaTemperatureSensorStatic`, `ZigbeeHaTemperatureSensorJoinable`
- BLE diagnostics: `BleAdvertiserProbe`, `BlePassiveScanner`, `BleActiveScanner`, `BleExtendedScanner`, `BleExtendedActiveScanner`, `BleLegacyAdv31Plus31`, `BleExtendedAdv251`, `BleExtendedScannableAdv251`, `BleExtendedAdv499`, `BleExtendedAdv995`, `BleConnectionPeripheral`, `BleGattBasicPeripheral`
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

The Zigbee examples now include a clean demo-network secured NWK path with AES-CCM* MIC-32 protection, persisted NWK and APS replay counters, install-code-derived or ZigBeeAlliance09 preconfigured link-key handling for APS-secured Transport Key install, learned or pinned trust-center IEEE checks on the joinable HA examples, a shared commissioning state machine for scan/association/Transport Key wait/rejoin transitions, parent polling while waiting for Transport Key or Update Device, retained-network fallback scanning across configured channel masks when the first secure-rejoin attempt misses, MAC orphan notification plus coordinator realignment before the reassociation fallback on retained-key rejoin, NWK-secured rejoin request/response before reassociation fallback, negotiated End Device Timeout requests after join or secure rejoin, staged alternate network-key persistence plus APS-secured Switch Key acceptance on the joinable HA examples, stricter trust-center source/state validation for `Update Device` and `Switch Key`, bounded unicast APS retransmission plus duplicate suppression for ZDO and HA application traffic on the clean demos, ZDO IEEE/NWK-address responses on the HA endpoints, and management leave handling that can either clear joined-state or transition into retained-key secure rejoin on those joinable examples depending on the leave flags. The joinable HA examples now also retain their demo network key, trust-center identity, counters, retry/failure state, and configurable channel masks for external-coordinator bring-up, and they now come back from persisted retained state through secure rejoin instead of assuming immediate joined-state success after restart, while the coordinator recognizes known nodes during reassociation, can answer orphan recovery with coordinator realignment, answer NWK rejoin and End Device Timeout requests, follow up with an APS-secured `Update Device` for the demo secure-rejoin path, and stage or switch an alternate demo network key on already joined children through APS-secured trust-center commands. This is still not Zigbee 3.0 BDB rejoin or third-party Trust Center interoperability; see `docs/ZIGBEE_FEATURE_MATRIX.md`, `docs/ZIGBEE_3P0_PARITY_PLAN.md`, and `docs/ZIGBEE_EXTERNAL_COORDINATOR_FLOW.md`.

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

- [`BleChannelSoundingReflector`](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/BleChannelSoundingReflector)
- [`BleChannelSoundingInitiator`](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/BleChannelSoundingInitiator)

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
