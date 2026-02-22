# Nrf54L15-Clean-Implementation

Clean, register-level Arduino implementation for the Seeed XIAO nRF54L15.

This package uses direct peripheral register access from the nRF54L15 datasheet and XIAO schematic pin mapping. It does not use Zephyr APIs or nRF Connect SDK APIs.

## Implemented HAL blocks

- `ClockControl`: HFXO control wrapper (runtime-managed no-op on this Arduino core).
- `Gpio`: configure/read/write/toggle and open-drain style drive setup for I2C.
- `Spim`: SPI master (SPIM21 on XIAO header by default) with EasyDMA transfer.
- `Twim`: I2C master (TWIM21 on XIAO header by default) with write/read/writeRead.
- `Uarte`: UART (UARTE21) with EasyDMA TX/RX.
- `Saadc`: single-ended ADC sampling and millivolt conversion.
- `Timer`: timer/counter setup, compare channels, shortcuts, and callback service.
- `Pwm`: PWM single-output setup with duty/frequency control.
- `Gpiote`: GPIO task/event channels with callback service.
- `PowerManager`: low-power/constant-latency mode, reset reason, retention registers, DCDC, System OFF.
- `Grtc`: global real-time counter setup, SYSCOUNTER readout, compare scheduling, wake timing.
- `TempSensor`: on-die temperature sampling in quarter-degree and milli-degree units.
- `Watchdog`: WDT configuration, start/stop (when enabled), feed, and status reads.
- `Pdm`: digital microphone interface setup and blocking capture with EasyDMA.
- `BleRadio`: register-level BLE 1M link layer + minimal ATT/GATT peripheral path via `RADIO`.

## Board pin map

Pin mappings in `src/xiao_nrf54l15_pins.h` are taken from the XIAO nRF54L15 schematic pages:

- Header pins `D0..D10`, `A0..A7`
- Back pads `D11..D15`
- Onboard LED `kPinUserLed` (active low)
- User button `kPinUserButton` (active low)
- Battery sense `kPinVbatEnable`, `kPinVbatSense`

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
- `examples/BleGattBasicPeripheral/BleGattBasicPeripheral.ino`
  - Connectable/scannable BLE peripheral with minimal GATT database (GAP/GATT/Battery).
  - Supports ATT MTU exchange and basic discovery/read requests over CID `0x0004`.
  - Supports Battery Level CCCD writes and Handle Value Notifications.
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
  - Bonding key persistence:
    - Retention-backed bond record in `.noinit` RAM
    - Optional callback hooks for flash-backed load/save/clear policies
- Not implemented yet:
  - Full LL procedure/state-machine compliance (full control procedure matrix and deep corner-cases)
  - Full security feature set (LE Secure Connections, full key distribution matrix, signing)
  - Privacy (RPA rotation/resolving list)
  - Full L2CAP signaling and complete GATT server database/configuration model

## Notes

- This is a polling/service style HAL for Arduino runtime compatibility.
- `Timer::service()` and `Gpiote::service()` should be called frequently from `loop()`.
