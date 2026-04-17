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
- Bluefruit/Seeed compatibility helpers: `digitalToggle`, `suspendLoop/resumeLoop`, `Print::printf`, `printBuffer`, `printBufferReverse`, `LED_STATE_ON`, `LED_RED/GREEN/BLUE`
- Persistent storage: `Preferences` key/value API (flash-backed)
- EEPROM compatibility: `EEPROM` (`begin/read/write/update/get/put/commit/end`)
- Legacy compatibility hooks: `cli()/sei()`, `makeWord(...)`, AVR-like port access helpers
- Network base compatibility headers: `IPAddress`, `Client`, `Server`, `Udp/UDP`

## Bundled HAL Library

Library path:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation`

Implemented blocks:

- `ClockControl`, `Gpio`, `Spim`, `Spis`, `Twim`, `Uarte`
- `Saadc`, `Timer`, `Pwm`, `Gpiote`
- `PowerManager`, `Grtc`, `TempSensor`, `Watchdog`, `Pdm`
- `Dppic`, `Egu`
- `Kmu`, `CracenIkg`, `Tampc`
- `BleRadio` (custom peripheral LL + minimal central/initiate + ATT/GATT subset)
- `ZigbeeRadio` (IEEE 802.15.4 PHY/MAC-lite data frame TX/RX helpers)
- `BoardControl` (battery sense + antenna route control)
- `CtrlApMailbox`, `VprControl`, `VprSharedTransportStream`

HAL structure note:

- the original monolithic `nrf54l15_hal.cpp` has been partially split
- support/timebase/board-policy/event/security code now lives in separate units
- `nrf54l15_hal.cpp` is still large because the BLE/Zigbee/runtime core is still concentrated there
- the next meaningful split target is the BLE-heavy/runtime-heavy part of the monolith, not the low-level helper surface

## BLE Status

Validated and stable with host adapter + hardware:

- Advertising
- Passive scanning
- Active scanning (`SCAN_REQ` / `SCAN_RSP`)
- Connect/disconnect
- Central initiate + basic ATT client request flow
- GATT discovery/read
- Battery notify CCCD flow

Current gap:

- pairing/bond persistence is still partial
- central support is still intentionally minimal (fixed-handle client flows and basic ATT request queueing, not a full generic host stack)
- `Bluefruit52Lib` still targets the common peripheral/runtime subset first, but the active-scan wrapper now emits real separate `SCAN_RSP` callback reports with the correct `scan_response` bit instead of collapsing them into the ADV path
- full Bluetooth channel sounding / AoA / AoD parity is not implemented yet; the current clean-core path now includes two-board phase sounding plus raw DFE capture hooks, HCI-style step parsing helpers, raw HCI subevent-result reassembly, controller-style step-buffer estimation helpers, transport-agnostic HCI CS command/completion packet helpers, a workflow/session/host layer for sequencing CS command exchange, a `Stream`-friendly H4 transport bridge with framing helpers that can tolerate interleaved ACL traffic, controller-standard RTT step decode / RTT distance estimation from HCI CS result packets, a working VPR-backed CS controller transport path inside the core, and a VPR-side CS demo responder for the supported opcode set, but it still does not have a real production BLE controller/runtime, and raw RADIO RTT AUXDATA decode is still not reliable
- the KMU path now includes a real `KMU -> CRACEN IKG` seed proof, the VPR side now has a generic shared-transport proof, a reusable host-side controller-service wrapper, validated non-CS VPR offload proofs for `FNV1a`, `CRC32`, `CRC32C`, an autonomous ticker service, queued async ticker/vendor events, and real VPR hibernate saved-context probes, plus a live capability probe showing `svc=1.7` / `opmask=0x3FF`; there are now dedicated local probes for hibernate resume, hibernate wake, and loaded-image restart, repeated loaded-image restart is hardware-validated on both attached boards through `VprRestartLifecycleProbe`, and `VprHibernateResumeProbe` now passes on both attached boards through a deterministic reset-after-hibernate service restart that preserves retained host-side service state while disabling raw VPR hardware context restore for the restart path; richer VPR-side service/runtime work is still open, and true raw VPR CPU-context resume is still an investigation topic rather than a finished public feature; the public `Tampc` wrapper now covers active-shield / glitch / domain-debug / AP-debug configuration with a live config probe, and the extra serial-fabric `22` / `30` paths now have a runtime probe
- full Zigbee stack layers (commissioning, NWK/APS/ZCL/security profiles) are not implemented yet; current support is IEEE 802.15.4 PHY/MAC-lite with coordinator/router/end-device role demos
- Thread/Matter is still not implemented as a working runtime, but the repo now
  includes official `OpenThread` public headers plus a compile-valid PAL
  skeleton (`alarm`, `entropy`, `settings`, `logging`, `diag`, `radio`) plus
  repo-backed RNG/AES/key-ref crypto shims, and a dedicated
  `OpenThreadPlatformSkeletonProbe`

## Validation Artifacts

- [`FEATURE_PARITY.md`](../FEATURE_PARITY.md)
- [`TODO.md`](../TODO.md)
- [`POWER_PROFILE_MEASUREMENTS.md`](../POWER_PROFILE_MEASUREMENTS.md)
- [`HARDWARE_IMPLEMENTATION_GAP_MAP.md`](HARDWARE_IMPLEMENTATION_GAP_MAP.md)
- [`HARDWARE_IMPLEMENTATION_PHASES.md`](HARDWARE_IMPLEMENTATION_PHASES.md)
- [`CHANNEL_SOUNDING_VPR_CONTINUATION.md`](CHANNEL_SOUNDING_VPR_CONTINUATION.md)
- [`THREAD_MATTER_IMPLEMENTATION_PLAN.md`](THREAD_MATTER_IMPLEMENTATION_PLAN.md)
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
  hardware/nrf54l15clean/nrf54l15clean/examples/Peripherals/InterruptPwmApiProbe/InterruptPwmApiProbe.ino
```

Example BLE matrix run:

```bash
bash scripts/ble_cli_matrix.sh --port /dev/ttyACM0 --sudo
```

Example pair/bond regression run:

```bash
bash scripts/ble_pair_bond_regression.sh --port /dev/ttyACM0 --sudo --attempts 10 --mode pair-bond
```
