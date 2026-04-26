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

- the former 22k-line `nrf54l15_hal.cpp` has been split into ordered fragments
  under `src/nrf54l15_hal_parts`
- the top-level `nrf54l15_hal.cpp` is now a small translation-unit wrapper that
  includes those fragments in the required order
- the BLE peripheral event fragments intentionally remain adjacent because they
  still share one connection-event state machine
- future cleanup should reduce cross-fragment helper sharing before turning any
  fragment into a separately compiled unit

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
- Thread is experimental, not production-claimed. The repo now has staged
  OpenThread core bring-up with fixed dataset, leader/child/router paths,
  PSKc/passphrase dataset helpers, and UDP examples. Joiner/commissioner,
  reference-network attach, reboot recovery, and sleepy-device depth remain
  open.
- Matter is foundation-only. The repo has staged `connectedhomeip` support,
  onboarding-code helpers, an on/off-light model, and a Thread dataset export
  seam. Real commissioning, discovery, commissioner/Home Assistant control, and
  reboot recovery remain open.

## Current Validation And Planning Docs

- [`NRF54L15_FEATURE_MATRIX.md`](NRF54L15_FEATURE_MATRIX.md)
- [`POWER_PROFILE_MEASUREMENTS.md`](../POWER_PROFILE_MEASUREMENTS.md)
- [`BLE_REGRESSION_RUNBOOK.md`](BLE_REGRESSION_RUNBOOK.md)
- [`BLE_CS_COMPLETION_CHECKLIST.md`](BLE_CS_COMPLETION_CHECKLIST.md)
- [`CHANNEL_SOUNDING_VPR_CONTINUATION.md`](CHANNEL_SOUNDING_VPR_CONTINUATION.md)
- [`THREAD_MATTER_IMPLEMENTATION_PLAN.md`](THREAD_MATTER_IMPLEMENTATION_PLAN.md)
- [`THREAD_RUNTIME_OWNERSHIP.md`](THREAD_RUNTIME_OWNERSHIP.md)
- [`MATTER_RUNTIME_OWNERSHIP.md`](MATTER_RUNTIME_OWNERSHIP.md)
- [`MATTER_FOUNDATION_MANIFEST.md`](MATTER_FOUNDATION_MANIFEST.md)
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
