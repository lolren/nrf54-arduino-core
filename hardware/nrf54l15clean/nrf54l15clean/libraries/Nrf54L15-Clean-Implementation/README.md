# Nrf54L15-Clean-Implementation

Clean, register-level Arduino implementation for the Seeed XIAO nRF54L15.

This package uses direct peripheral register access from the nRF54L15 datasheet and XIAO schematic pin mapping. It does not use Zephyr APIs or nRF Connect SDK APIs.

## Implemented HAL blocks

- `ClockControl`: HFXO control plus runtime CPU-frequency and idle clock-scaling helpers.
- `Gpio`: configure/read/write/toggle and open-drain style drive setup for I2C.
- `Spim`: SPI master with route-aware instance selection (`SPIM21` in USB-bridge `Serial` mode, `SPIM20` in header-UART `Serial` mode), plus EasyDMA transfer and runtime frequency control.
- `Twim`: I2C master on a caller-selected controller base (for example `TWIM22`
  for `D4/D5`, `TWIM30` for `D12/D11`), plus write/read/writeRead and runtime
  frequency control.
- `Uarte`: UART (UARTE21) with EasyDMA TX/RX.
- `Saadc`: single-ended ADC sampling and millivolt conversion.
- `Timer`: timer/counter setup, compare channels, shortcuts, and callback service.
- `Pwm`: PWM single-output setup with duty/frequency control.
- `Gpiote`: GPIO task/event channels with callback service.
- `Dppic`: DPPI channel helper for publish/subscribe wiring between peripherals.
- `Comp`: general-purpose comparator in single-ended threshold or differential mode.
- `Lpcomp`: low-power comparator with analog-detect behavior suited for wake use.
- `Qdec`: hardware quadrature decoder with accumulator and double-transition reporting.
- `PowerManager`: low-power/constant-latency mode, reset reason, retention registers, DCDC, System OFF.
- `Grtc`: global real-time counter setup, SYSCOUNTER readout, compare scheduling, wake timing.
- `TempSensor`: on-die temperature sampling in quarter-degree and milli-degree units.
- `Watchdog`: WDT configuration, start/stop (when enabled), feed, and status reads.
- `BoardControl`: board-level helpers for battery measurement path and antenna switch control.
- `Pdm`: digital microphone interface setup and blocking capture with EasyDMA.
- `I2sTx`: reusable TX-only `I2S20` wrapper with buffer rotation, IRQ service, optional auto-restart, and callback-based buffer refill.
- `I2sRx`: reusable RX-only `I2S20` wrapper with double-buffer capture, IRQ service, optional auto-restart, and callback-based buffer delivery.
- `I2sDuplex`: reusable full-duplex `I2S20` wrapper with shared stop/restart handling, TX refill callback, and RX delivery callback.
- `BleRadio`: register-level BLE 1M link layer + minimal ATT/GATT peripheral path via `RADIO`.
- `ZigbeeRadio`: IEEE 802.15.4 PHY/MAC-lite data-frame + MAC-command frame TX/RX helpers via `RADIO`.
- `RawRadioLink`: proprietary 1 Mbit packet TX/RX helper via `RADIO`.
- `nrf_to_nrf`: thin RF24-style compatibility wrapper for common sketch flows on top of `RawRadioLink`.

Raw peripheral compatibility exposed by the core:

- `NRF_DPPIC20`
- `NRF_RADIO`
- `NRF_COMP`
- `NRF_LPCOMP`
- `NRF_QDEC20`
- `NRF_QDEC21`
- `NRF_I2S20`
- `NRF_I2S0`
- `NRF_I2S`

RTC note:

- `nRF54L15` exposes `GRTC` for low-frequency timekeeping, alarms, and timed wake.
- This is an RTC-like monotonic counter/compare block, not a battery-backed calendar clock by itself.
- The `Grtc` HAL reports uptime in microseconds and supports relative/absolute compare alarms.

## Comparator guide

- `Comp` is the awake-time comparator.
- In single-ended mode it compares an analog pin against a threshold window derived from `VDD`, the internal `1.2 V` reference, or an external analog reference pin.
- In differential mode it compares one analog pin directly against another analog pin.
- `Lpcomp` is the lower-power comparator and is the one that matters when you want analog wake behavior through `SYSTEM OFF`.
- `Comp` and `Lpcomp` share the same underlying comparator block on `nRF54L15`, so only one of them can be active at a time.
- On the XIAO, the safest comparator demo pins are `A0..A3`. `A5`, `A6`, and `A7` are also analog-capable, but they overlap more board-specific functions.

Practical rule of thumb:

- Use `Comp` when the MCU is awake and you want threshold crossing or direct analog-to-analog comparison with minimal CPU overhead.
- Use `Lpcomp` when the comparison itself is the wake source and low-power behavior matters more than speed.

Board note:

- `NFCT` exists on the SoC, but it is still intentionally not wrapped here because the XIAO board does not expose a practical NFC antenna path for normal sketches.

## Board pin map

Pin mappings in `src/xiao_nrf54l15_pins.h` are taken from the XIAO nRF54L15 schematic pages:

- Header pins `D0..D10`, `A0..A7`
- Back pads `D11..D15`
- Onboard LED `kPinUserLed` (active low)
- User button `kPinUserButton` (active low)
- Battery sense `kPinVbatEnable`, `kPinVbatSense`

Default Arduino peripheral pin routes:

- `Wire` : `SDA=D4`, `SCL=D5`
- `Wire1`: `SDA=D12`, `SCL=D11`
- `SPI`  : `MOSI=D10`, `MISO=D9`, `SCK=D8`, `SS=D2`
- `Serial1`/`Serial2` (compat alias): `TX=D6`, `RX=D7`

Compatibility note:

- Arduino `Wire` now routes `D4/D5` to dedicated `TWIM22`, and `Wire1` routes
  `D12/D11` to dedicated `TWIM30`.
- Because those are separate from the serial-fabric UARTE instances used by
  `Serial` / `Serial1`, both I2C buses can coexist with serial logging.
- The lower-level `Twim` HAL remains explicit: choose the controller base that
  matches the pins you want to drive.

## BoardControl APIs

`BoardControl` is designed for low-power-friendly board-specific control:

```cpp
int32_t vbatMv = 0;
uint8_t vbatPct = 0;

BoardControl::sampleBatteryMilliVolts(&vbatMv);
BoardControl::sampleBatteryPercent(&vbatPct);

BoardControl::enableRfPath(BoardAntennaPath::kCeramic);
BoardControl::collapseRfPathIdle();
BoardControl::setAntennaPath(BoardAntennaPath::kExternal);
BoardControl::setImuMicEnabled(false);
```

Notes:

- VBAT sampling enables the divider path only during sampling, then disables it.
- `setAntennaPath(...)` powers the RF switch on so the selected route takes effect.
- `setRfSwitchPowerEnabled(false)` removes the RF switch quiescent current without changing the last selected route.
- `kControlHighImpedance` releases RF switch control GPIO drive (`P2.05`) and is useful when you want no active antenna-control drive.
- `enableRfPath(...)` and `collapseRfPathIdle()` are the recommended helpers for duty-cycling the XIAO RF switch around BLE TX/RX windows.

Arduino IDE organization:

- `File -> Examples -> Nrf54L15-Clean-Implementation -> BLE`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> LowPower`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Diagnostics`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Board`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Peripherals`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Zigbee`

## Example

`examples/Diagnostics/CleanBringUp/CleanBringUp.ino` initializes and exercises:

- GPIO/clock/UART
- I2C and SPI transfers
- ADC A0 and VBAT sampling
- TIMER periodic scheduling
- PWM LED breathing
- GPIOTE button edge capture and task output toggle

Advanced peripherals (`TIMER`, `PWM`, `GPIOTE`) are enabled by default in `CleanBringUp`.

`examples/Diagnostics/PeripheralSelfTest/PeripheralSelfTest.ino` runs explicit per-block PASS/FAIL checks for:

- CLOCK wrapper
- GPIO
- ADC
- SPI
- I2C
- UART
- TIMER
- PWM
- GPIOTE
- POWER + RESET
- GRTC
- TEMP
- WDT configuration
- PDM capture
- BLE advertising + scannable/connectable interaction path

`examples/Diagnostics/FeatureParitySelfTest/FeatureParitySelfTest.ino` runs focused checks for the
new non-BLE parity blocks:

- POWER/RESET/retention/DCDC controls
- GRTC compare scheduling in System ON
- Internal TEMP sensor sampling
- WDT configuration path
- PDM microphone capture path

`examples/LowPower/InterruptWatchdogLowPower/InterruptWatchdogLowPower.ino` demonstrates:

- Core `attachInterrupt()` behavior on the user button.
- HAL watchdog configuration/start/feed behavior.
- Low-power `WFI` idle loop pattern with periodic watchdog feed.

`examples/LowPower/LpcompSystemOffWake/LpcompSystemOffWake.ino` demonstrates:

- `Lpcomp` threshold detection on `A0` as a `SYSTEM OFF` wake source.
- Retained `.noinit` state across wake so you can prove it really woke from the comparator path.

`examples/Board/BoardBatteryAntennaBusControl/BoardBatteryAntennaBusControl.ino` demonstrates:

- VBAT measurement in millivolts and percent via `BoardControl`.
- Antenna routing commands (`ceramic`, `external`, `control-high-impedance`).
- Runtime I2C/SPI bus frequency tuning paths.

Peripheral examples:

- `examples/Peripherals/RawI2sTxInterrupt/RawI2sTxInterrupt.ino`
  - Uses `I2S20` with `TXPTRUPD` and `STOPPED` interrupts instead of polling.
  - Keeps the TX buffer armed from `I2S20_IRQHandler` and intentionally cycles stop/restart so both interrupt paths are visible over UART.
- `examples/Peripherals/I2sTxWrapperInterrupt/I2sTxWrapperInterrupt.ino`
  - Uses the reusable `I2sTx` wrapper instead of touching `NRF_I2S` directly in the sketch.
  - Keeps the same visible stop/restart cycle as the raw interrupt example, but with the register setup and IRQ state machine moved into the HAL.
  - Refills the next audio buffer from the wrapper callback path instead of managing the ring by hand in `loop()`.
- `examples/Peripherals/I2sRxWrapperInterrupt/I2sRxWrapperInterrupt.ino`
  - Uses the reusable `I2sRx` wrapper to keep a double-buffer receive path armed from `I2S20_IRQHandler`.
  - Delivers completed RX buffers through a callback and keeps the same visible stop/restart cycle as the TX wrapper.
  - Can be smoke-tested with `SDIN` floating, or fed from an external I2S source for real capture.
- `examples/Peripherals/I2sDuplexWrapperInterrupt/I2sDuplexWrapperInterrupt.ino`
  - Uses the reusable `I2sDuplex` wrapper to run TX and RX together on one `I2S20` instance.
  - Keeps the same visible stop/restart cycle while reporting both `TXPTRUPD` and `RXPTRUPD`.
  - Supports a one-board loopback by jumpering `SDOUT=D11` to `SDIN=D15`.

Callback note:

- `I2sTx::setRefillCallback(...)` runs from the `I2S20` IRQ context.
- `I2sRx::setReceiveCallback(...)` also runs from the `I2S20` IRQ context.
- `I2sDuplex::setTxRefillCallback(...)` and `I2sDuplex::setRxReceiveCallback(...)` run from the same `I2S20` IRQ context.
- Keep the callback short, non-blocking, and free of any serial/logging work.
- `examples/Peripherals/RawRadioPacketTx/RawRadioPacketTx.ino`
  - Uses `RawRadioLink` to send proprietary 1 Mbit packets on a fixed pipe and channel.
- `examples/Peripherals/RawRadioPacketRx/RawRadioPacketRx.ino`
  - Uses `RawRadioLink::armReceive()` and `pollReceive()` for non-blocking packet reception.
- `examples/Peripherals/RawRadioAckRequester/RawRadioAckRequester.ino`
  - Builds a simple software ACK exchange on top of `RawRadioLink::transmit()` and `waitForReceive()`.
- `examples/Peripherals/RawRadioAckResponder/RawRadioAckResponder.ino`
  - Companion responder for the requester example, still using the same raw proprietary helper.
- `examples/Peripherals/nrf_to_nrfGettingStarted/nrf_to_nrfGettingStarted.ino`
  - Common `nrf_to_nrf` / RF24-style TX-RX flow using `begin()`, pipes, `startListening()`, `stopListening()`, `write()`, `available()`, and `read()`.
- `examples/Peripherals/nrf_to_nrfAcknowledgementPayloads/nrf_to_nrfAcknowledgementPayloads.ino`
  - Common `nrf_to_nrf` ACK-payload flow using dynamic payloads and `writeAckPayload()`.
- `examples/Peripherals/GrtcUptimeClock/GrtcUptimeClock.ino`
  - Reads the `GRTC` SYSCOUNTER and prints formatted uptime once per second.
  - Shows the most honest RTC-like use on `nRF54L15`: stable uptime since boot.
- `examples/Peripherals/GrtcCompareAlarmTicker/GrtcCompareAlarmTicker.ino`
  - Arms a `GRTC` compare channel as a periodic alarm source and polls for compare events.
  - Shows how to build repeating alarm/ticker behavior without pretending the hardware is a wall clock.
- `examples/Peripherals/CompThresholdMonitor/CompThresholdMonitor.ino`
  - Uses `Comp` in single-ended mode to monitor whether `A0` is above or below a threshold near `50% VDD`.
  - This is the most practical comparator starting point for battery/sensor threshold projects.
- `examples/Peripherals/CompDifferentialProbe/CompDifferentialProbe.ino`
  - Uses `Comp` in differential mode to compare `A0` directly against `A1`.
  - Useful when you want analog-to-analog comparison without continuously sampling SAADC.
- `examples/Peripherals/QdecRotaryReporter/QdecRotaryReporter.ino`
  - Uses the hardware `QDEC` block to decode a rotary encoder on `D0/D1`.
  - Prints signed movement deltas and accumulated position without software edge decoding.
- `examples/Peripherals/DppicHardwareBlink/DppicHardwareBlink.ino`
  - Wires `TIMER -> DPPIC -> GPIOTE` so the LED toggles in hardware.
  - Demonstrates the useful part of DPPI: precise work with no CPU in the timing loop.

Compatibility note:

- The bundled `nrf_to_nrf` wrapper is aimed at source compatibility for common sketches.
- It currently covers the usual RF24-style flows used by `GettingStarted` and `AcknowledgementPayloads`.
- It is built on `RawRadioLink`, so this is not a claim of full upstream wire-level compatibility yet.
- The checked-in two-board regression entry point is `scripts/nrf_to_nrf_dual_board_regression.py`.

BLE examples:

- `examples/BLE/BleAdvertiser/BleAdvertiser.ino`
  - Legacy advertising on channels 37/38/39 with custom ADV payload.
- `examples/BLE/BlePassiveScanner/BlePassiveScanner.ino`
  - Passive scanner over channels 37/38/39 with RSSI and header parsing.
- `examples/BLE/BleConnectableScannableAdvertiser/BleConnectableScannableAdvertiser.ino`
  - Uses `ADV_IND`, listens for `SCAN_REQ`/`CONNECT_IND`, and sends `SCAN_RSP`.
  - Exposes interaction counters and peer addresses over UART.
- `examples/BLE/BleConnectionPeripheral/BleConnectionPeripheral.ino`
  - Accepts legacy `CONNECT_IND`, tracks connection parameters, and runs data-channel events.
  - Responds to common LL control PDUs and ATT requests, with link event metadata logs.
- `examples/BLE/BleConnectionTimingMetrics/BleConnectionTimingMetrics.ino`
  - Measures connection-event outcomes over rolling windows (RX ok/CRC fail/RX timeout/TX timeout).
  - Useful for comparing `BLE Timing Profile` tool options while keeping TX power explicit in sketch code.
- `examples/BLE/BleGattBasicPeripheral/BleGattBasicPeripheral.ino`
  - Connectable/scannable BLE peripheral with minimal GATT database (GAP/GATT/Battery).
  - Supports ATT MTU exchange and basic discovery/read requests over CID `0x0004`.
  - Supports Battery Level CCCD writes and Handle Value Notifications.
- `examples/BLE/BleCustomGattRuntime/BleCustomGattRuntime.ino`
  - Demonstrates runtime registration of custom 16-bit GATT service/characteristics.
  - Includes writable characteristic, CCCD-backed notification characteristic, and serial command hooks.
- `examples/BLE/BleBatteryNotifyPeripheral/BleBatteryNotifyPeripheral.ino`
  - Connectable/scannable BLE peripheral focused on Battery Level notifications.
  - Periodically updates battery percentage and emits notifications when CCCD notify is enabled.
- `examples/BLE/BlePairingEncryptionStatus/BlePairingEncryptionStatus.ino`
  - Shows LL control and encryption state transitions during pairing/encryption.
- `examples/BLE/BleBondPersistenceProbe/BleBondPersistenceProbe.ino`
  - Demonstrates bond retention across resets and reconnect-side encryption reuse.
  - Hold user button at boot to clear persistent bond state.
- `examples/BLE/BleChannelSoundingReflector/BleChannelSoundingReflector.ino`
  - Two-board phase-sounding reflector using `RADIO.CSTONES`/DFE capture on requested BLE channels.
  - Returns reflector-side IQ/magnitude terms for the initiator's phase-slope distance fit.
- `examples/BLE/BleChannelSoundingInitiator/BleChannelSoundingInitiator.ino`
  - Sweeps BLE data channels with tone-extended probes and combines both endpoints' IQ terms.
  - Reports `dist_m`, rolling `median_m`, `valid_channels`, and fit `residual` from the phase-slope estimator.

Zigbee examples:

- `examples/Zigbee/ZigbeeCoordinator/ZigbeeCoordinator.ino`
  - IEEE 802.15.4 coordinator-role demo with beaconing, discovery, and MAC-lite exchanges.
- `examples/Zigbee/ZigbeeRouter/ZigbeeRouter.ino`
  - Router-role relay/demo path for the current PHY/MAC-lite implementation.
- `examples/Zigbee/ZigbeeEndDevice/ZigbeeEndDevice.ino`
  - End-device role demo for low-duty-cycle 802.15.4 participation.
- `examples/Zigbee/ZigbeePingInitiator/ZigbeePingInitiator.ino`
  - Two-board request/response initiator for timing and RSSI checks.
- `examples/Zigbee/ZigbeePongResponder/ZigbeePongResponder.ino`
  - Companion responder for the two-board Zigbee ping flow.

## Low-Power Examples

The following sketches focus on low-power behavior called out in the nRF54L15 datasheet:

- CPU enters System ON idle using `WFI/WFE` when no work is pending
- Keep CPU frequency at 64 MHz when full performance is not needed
- Optionally run active work at 128 MHz and drop to 64 MHz automatically during `delay()` / `yield()`
- Duty-cycle peripherals so they are enabled only during short active windows
- Optional core-level peripheral auto-gating for idle windows (SPI/Wire)
- Use timed `SYSTEM OFF` when cold-boot wake semantics are acceptable

Examples:

- `examples/LowPower/LowPowerIdleWfi/LowPowerIdleWfi.ino`
  - Minimal heartbeat workload with `__WFI()` between events.
  - Uses 64 MHz CPU by default, switches to 128 MHz while button is held.
- `examples/LowPower/LowPowerIdleCpuScaling/LowPowerIdleCpuScaling.ino`
  - Demonstrates explicit idle CPU scaling: active work at 128 MHz, automatic idle drop to 64 MHz.
  - `delay()` / `yield()` restore the previous CPU speed after wake.
- `examples/LowPower/LowPowerDutyCycleAdc/LowPowerDutyCycleAdc.ino`
  - Periodic ADC sampling with SAADC enabled only during sample windows.
  - VBAT divider path is enabled only for the measurement interval.
- `examples/LowPower/LowPowerPeripheralGating/LowPowerPeripheralGating.ino`
  - SPI and I2C are opened for short probe windows and immediately disabled.
  - `__WFI()` is used for idle time between windows.
- `examples/LowPower/LowPowerSystemOffWakeButton/LowPowerSystemOffWakeButton.ino`
  - Enters true System OFF and wakes from the XIAO user button GPIO detect.
  - Uses `RESETREAS` + `GPREGRET` to report wake/reset path after reboot.
- `examples/LowPower/LowPowerSystemOffWakeRtc/LowPowerSystemOffWakeRtc.ino`
  - Enters true System OFF and wakes on a programmed GRTC compare timeout.
  - Uses `RESETREAS` + `GPREGRET` to verify timed wake path after reboot.
- `examples/LowPower/LowPowerDelaySystemOff/LowPowerDelaySystemOff.ino`
  - Demonstrates the Arduino-style `delaySystemOff(ms)` helper.
  - Uses timed `SYSTEM OFF` for long sleeps while preserving `.noinit` RAM by default.
- `examples/BLE/LowPowerBleBeaconDutyCycle/LowPowerBleBeaconDutyCycle.ino`
  - Sends short BLE advertising bursts, then sleeps with `WFI` between bursts.
  - Uses low-power latency mode and 64 MHz CPU clock to reduce average current.
- `examples/BLE/BleAdvertiserLowestPowerContinuous/BleAdvertiserLowestPowerContinuous.ino`
  - Lowest validated continuous BLE advertiser baseline on the current raw BLE path.
  - Uses `ADV_IND`, the default FICR-derived address, `-10 dBm`, and `3000 ms` intervals.
  - Use `Low Power (WFI Idle)` board profile; the core low-power GRTC tick path is now required for this example.
  - Intended for `System ON + WFI between advertising events`, not `SYSTEM OFF`.
- `examples/BLE/BleAdvertiserRfSwitchDutyCycle/BleAdvertiserRfSwitchDutyCycle.ino`
  - Continuous advertiser that powers/selects the RF switch only around each `advertiseEvent(...)`.
  - Leaves the RF switch off and the control pin high-impedance while idle to test board-level switch quiescent current.
  - Edit `kAdvertisingIntervalMs` in the sketch if you want a shorter scan-friendly cadence.
- `examples/BLE/BleAdvertiserHybridDutyCycle/BleAdvertiserHybridDutyCycle.ino`
  - Sends short advertising bursts in `System ON`, then idles with `WFI` and RF-switch collapse between bursts.
  - Intended as the middle operating point between persistent visibility and the lowest burst-beacon current.
  - Edit the top-of-sketch burst constants if you want a different burst period or event count.
- `examples/BLE/BleAdvertiserPhoneBeacon15s/BleAdvertiserPhoneBeacon15s.ino`
  - Non-connectable phone-tuned beacon pattern: longer wake burst, name in the primary ADV payload, no scan-response dependence, then timed `SYSTEM OFF`.
  - Intended for low-average-current beaconing where scanner catchability matters more than minimum RF-on time.
- `examples/BLE/BleAdvertiserBurstSystemOff/BleAdvertiserBurstSystemOff.ino`
  - Sends a short advertising burst, collapses the RF switch path, then enters timed `SYSTEM OFF`.
  - The validated baseline uses `6` events per boot, `20 ms` inter-burst gaps, `1000 ms` system-off intervals, and the default boot path.
  - Uses the explicit `NoRetention` system-off helpers so the low-power examples can drop RAM retention without changing the default `systemOff*()` semantics for retained `.noinit` users.
  - Intended for burst beaconing, not continuously discoverable BLE presence.
  - Edit `kSystemOffIntervalMs` in the sketch if you want a sparser or denser wake cadence.
- `examples/BLE/BleAdvertiserProbe/BleAdvertiserProbe.ino`
  - Diagnostic advertiser with LED stage codes for BLE bring-up failures.
  - Useful when scanning fails and you need to separate init errors from RF visibility.
- `examples/LowPower/LowPowerTelemetryDutyMetrics/LowPowerTelemetryDutyMetrics.ino`
  - Reports rolling active-vs-sleep duty metrics (microsecond accounting).
  - Pairs duty telemetry with ADC/VBAT duty-cycled sampling windows.
- `examples/LowPower/LowPowerAutoGatePolicy/LowPowerAutoGatePolicy.ino`
  - Demonstrates Tools-menu configurable idle auto-gating on core `SPI`/`Wire`.
  - Uses transfer windows without explicit `end()` and verifies idle disable via register state.

## BLE Scope

- Implemented now:
  - Legacy 1M advertising (including custom ADV payloads)
  - Passive scanning
  - Scannable/connectable advertising interaction handling (`SCAN_REQ`, `SCAN_RSP`, `CONNECT_IND` detect)
  - Legacy connection bring-up and data-channel event polling
  - LL control response subset (`FEATURE_REQ`, `VERSION_IND`, `LENGTH_REQ`, `PHY_REQ`, `PING_REQ`, unknown-op fallback)
  - LL control strict opcode-length validation with malformed-request reject path
  - LL connection update/channel-map instant validation and safer retransmission gating
  - LL encryption procedure subset (`ENC_REQ/RSP`, `START_ENC_REQ/RSP`, `PAUSE_ENC_REQ/RSP`) with collision/disallow reject handling
  - Minimal ATT/GATT responder on fixed L2CAP ATT channel (`0x0004`) for:
    - MTU exchange
    - Find By Type Value (primary service UUID search)
    - Primary service discovery
    - Characteristic discovery
    - Basic read/read-blob on GAP + Battery attributes
    - Read By Group Type edge-case handling (`Unsupported Group Type` vs `Invalid PDU`)
    - Discovery boundary-handle validation (`start=0`/invalid ranges -> `Invalid Handle`)
    - Service Changed CCCD writes + Handle Value indication confirmation handling
    - Battery Level CCCD writes + Handle Value notification emission
    - Prepare Write / Execute Write on selected writable CCCDs
  - L2CAP LE signaling (`CID 0x0005`) fallback handling:
    - Command Reject for unsupported signaling opcodes
    - Command Reject reason granularity (`Cmd Not Understood`, `Signaling MTU exceeded`, `Invalid CID`)
    - Connection Parameter Update Request -> rejected response (peripheral-role-only controller path)
    - LE Credit Based Connection Request -> response with `PSM not supported`
  - SMP legacy Just Works pairing subset:
    - Pairing Request/Response/Confirm/Random validation flow
    - Legacy `c1`/`s1` confirm and STK derivation
    - Encryption Information/Master Identification key distribution parsing
  - Optional protocol-level BLE trace path:
    - Enable Arduino Tools -> `BLE Trace` to emit LL/SMP trace markers for interop debugging
  - BLE timing profile controls:
    - Arduino Tools -> `BLE Timing Profile` adjusts connection-event RX/TX timing budgets.
    - `Interoperability`: widest timing windows.
    - `Balanced Low-Power`: moderate timing windows.
    - `Aggressive Low-Power`: shortest timing windows + lower-power preferred PPCP defaults.
  - BLE TX power controls:
    - BLE examples now pass TX power explicitly into `BleRadio::begin(txPowerDbm)`.
    - Trade off current consumption/range in sketch code, not via a board-level tools menu.
  - Bonding key persistence:
    - Retention-backed bond record in `.noinit` RAM
    - Optional callback hooks for flash-backed load/save/clear policies
- Not implemented yet:
  - Bluetooth Channel Sounding LL procedure and HCI/host control plane (full spec feature).
  - Full LL procedure/state-machine compliance (full control procedure matrix and deep corner-cases)
  - Full security feature set (LE Secure Connections, full key distribution matrix, signing)
  - Privacy (RPA rotation/resolving list)
  - Full L2CAP signaling and complete GATT server database/configuration model

## Power Profiling Workflow

- See repository root `POWER_PROFILE_MEASUREMENTS.md` for repeatable current-measurement
  procedure, capture matrix, and data template for Tools-menu profile comparisons.

## Notes

- This is a polling/service style HAL for Arduino runtime compatibility.
- `Timer::service()` and `Gpiote::service()` should be called frequently from `loop()`.
- For XIAO nRF54L15 USB serial bridge path (`Tools -> Serial Routing -> USB bridge on Serial`),
  set Serial Monitor baud to the same value used in `Serial.begin(...)`.
