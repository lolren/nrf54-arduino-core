# Nrf54L15-Clean-Implementation

Clean, register-level Arduino implementation for the Seeed XIAO nRF54L15.

This package uses direct peripheral register access from the nRF54L15 datasheet and XIAO schematic pin mapping. It does not use Zephyr APIs or nRF Connect SDK APIs.

## Implemented HAL blocks

- `ClockControl`: HFXO control wrapper (runtime-managed no-op on this Arduino core).
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
- `PowerManager`: low-power/constant-latency mode, reset reason, retention registers, DCDC, System OFF.
- `Grtc`: global real-time counter setup, SYSCOUNTER readout, compare scheduling, wake timing.
- `TempSensor`: on-die temperature sampling in quarter-degree and milli-degree units.
- `Watchdog`: WDT configuration, start/stop (when enabled), feed, and status reads.
- `BoardControl`: board-level helpers for battery measurement path and antenna switch control.
- `Pdm`: digital microphone interface setup and blocking capture with EasyDMA.
- `BleRadio`: register-level BLE 1M link layer + minimal ATT/GATT peripheral path via `RADIO`.

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

BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
BoardControl::setAntennaPath(BoardAntennaPath::kExternal);
BoardControl::setAntennaPath(BoardAntennaPath::kControlHighImpedance);
BoardControl::setRfSwitchPowerEnabled(false);
```

Notes:

- VBAT sampling enables the divider path only during sampling, then disables it.
- `setAntennaPath(...)` powers the RF switch on so the selected route takes effect.
- `setRfSwitchPowerEnabled(false)` removes the RF switch quiescent current without changing the last selected route.
- `kControlHighImpedance` releases RF switch control GPIO drive (`P2.05`) and is useful when you want no active antenna-control drive.

## Example

`examples/CleanBringUp/CleanBringUp.ino` initializes and exercises:

- GPIO/clock/UART
- I2C and SPI transfers
- ADC A0 and VBAT sampling
- TIMER periodic scheduling
- PWM LED breathing
- GPIOTE button edge capture and task output toggle

Advanced peripherals (`TIMER`, `PWM`, `GPIOTE`) are enabled by default in `CleanBringUp`.

`examples/PeripheralSelfTest/PeripheralSelfTest.ino` runs explicit per-block PASS/FAIL checks for:

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

`examples/FeatureParitySelfTest/FeatureParitySelfTest.ino` runs focused checks for the
new non-BLE parity blocks:

- POWER/RESET/retention/DCDC controls
- GRTC compare scheduling in System ON
- Internal TEMP sensor sampling
- WDT configuration path
- PDM microphone capture path

`examples/InterruptWatchdogLowPower/InterruptWatchdogLowPower.ino` demonstrates:

- Core `attachInterrupt()` behavior on the user button.
- HAL watchdog configuration/start/feed behavior.
- Low-power `WFI` idle loop pattern with periodic watchdog feed.

`examples/BoardBatteryAntennaBusControl/BoardBatteryAntennaBusControl.ino` demonstrates:

- VBAT measurement in millivolts and percent via `BoardControl`.
- Antenna routing commands (`ceramic`, `external`, `control-high-impedance`).
- Runtime I2C/SPI bus frequency tuning paths.

BLE examples:

- `examples/BleAdvertiser/BleAdvertiser.ino`
  - Legacy advertising on channels 37/38/39 with custom ADV payload.
- `examples/BlePassiveScanner/BlePassiveScanner.ino`
  - Passive scanner over channels 37/38/39 with RSSI and header parsing.
- `examples/BleConnectableScannableAdvertiser/BleConnectableScannableAdvertiser.ino`
  - Uses `ADV_IND`, listens for `SCAN_REQ`/`CONNECT_IND`, and sends `SCAN_RSP`.
  - Exposes interaction counters and peer addresses over UART.
- `examples/BleConnectionPeripheral/BleConnectionPeripheral.ino`
  - Accepts legacy `CONNECT_IND`, tracks connection parameters, and runs data-channel events.
  - Responds to common LL control PDUs and ATT requests, with link event metadata logs.
- `examples/BleConnectionTimingMetrics/BleConnectionTimingMetrics.ino`
  - Measures connection-event outcomes over rolling windows (RX ok/CRC fail/RX timeout/TX timeout).
  - Useful for comparing `BLE Timing Profile` and `BLE TX Power` tool options on real links.
- `examples/BleGattBasicPeripheral/BleGattBasicPeripheral.ino`
  - Connectable/scannable BLE peripheral with minimal GATT database (GAP/GATT/Battery).
  - Supports ATT MTU exchange and basic discovery/read requests over CID `0x0004`.
  - Supports Battery Level CCCD writes and Handle Value Notifications.
- `examples/BleCustomGattRuntime/BleCustomGattRuntime.ino`
  - Demonstrates runtime registration of custom 16-bit GATT service/characteristics.
  - Includes writable characteristic, CCCD-backed notification characteristic, and serial command hooks.
- `examples/BleBatteryNotifyPeripheral/BleBatteryNotifyPeripheral.ino`
  - Connectable/scannable BLE peripheral focused on Battery Level notifications.
  - Periodically updates battery percentage and emits notifications when CCCD notify is enabled.
- `examples/BlePairingEncryptionStatus/BlePairingEncryptionStatus.ino`
  - Shows LL control and encryption state transitions during pairing/encryption.
- `examples/BleBondPersistenceProbe/BleBondPersistenceProbe.ino`
  - Demonstrates bond retention across resets and reconnect-side encryption reuse.
  - Hold user button at boot to clear persistent bond state.

## Low-Power Examples

The following sketches focus on low-power behavior called out in the nRF54L15 datasheet:

- CPU enters System ON idle using `WFI/WFE` when no work is pending
- Keep CPU frequency at 64 MHz when full performance is not needed
- Duty-cycle peripherals so they are enabled only during short active windows
- Optional core-level peripheral auto-gating for idle windows (SPI/Wire)

Examples:

- `examples/LowPowerIdleWfi/LowPowerIdleWfi.ino`
  - Minimal heartbeat workload with `__WFI()` between events.
  - Uses 64 MHz CPU by default, switches to 128 MHz while button is held.
- `examples/LowPowerDutyCycleAdc/LowPowerDutyCycleAdc.ino`
  - Periodic ADC sampling with SAADC enabled only during sample windows.
  - VBAT divider path is enabled only for the measurement interval.
- `examples/LowPowerPeripheralGating/LowPowerPeripheralGating.ino`
  - SPI and I2C are opened for short probe windows and immediately disabled.
  - `__WFI()` is used for idle time between windows.
- `examples/LowPowerSystemOffWakeButton/LowPowerSystemOffWakeButton.ino`
  - Enters true System OFF and wakes from the XIAO user button GPIO detect.
  - Uses `RESETREAS` + `GPREGRET` to report wake/reset path after reboot.
- `examples/LowPowerSystemOffWakeRtc/LowPowerSystemOffWakeRtc.ino`
  - Enters true System OFF and wakes on a programmed GRTC compare timeout.
  - Uses `RESETREAS` + `GPREGRET` to verify timed wake path after reboot.
- `examples/LowPowerBleBeaconDutyCycle/LowPowerBleBeaconDutyCycle.ino`
  - Sends short BLE advertising bursts, then sleeps with `WFI` between bursts.
  - Uses low-power latency mode and 64 MHz CPU clock to reduce average current.
- `examples/LowPowerTelemetryDutyMetrics/LowPowerTelemetryDutyMetrics.ino`
  - Reports rolling active-vs-sleep duty metrics (microsecond accounting).
  - Pairs duty telemetry with ADC/VBAT duty-cycled sampling windows.
- `examples/LowPowerAutoGatePolicy/LowPowerAutoGatePolicy.ino`
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
    - Arduino Tools -> `BLE TX Power` sets default `BleRadio::begin()` transmit power.
    - Trade off current consumption/range without sketch code changes.
  - Bonding key persistence:
    - Retention-backed bond record in `.noinit` RAM
    - Optional callback hooks for flash-backed load/save/clear policies
- Not implemented yet:
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
