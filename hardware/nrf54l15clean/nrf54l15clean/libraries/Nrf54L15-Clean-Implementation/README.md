# Nrf54L15-Clean-Implementation

Clean, register-level Arduino implementation for `nRF54L15` boards.

Arduino IDE board package surface:

- package: `nRF54L15 Boards`
- boards:
  - `XIAO nRF54L15 / Sense`
  - `HOLYIOT-25008 nRF54L15 Module`
  - `HOLYIOT-25007 nRF54L15 Module`
  - `Generic nRF54L15 Module (36-pad)`

This package uses direct peripheral register access from the `nRF54L15`
datasheet and board-variant pin mapping. It does not use Zephyr APIs or nRF
Connect SDK APIs.

## Implemented HAL blocks

- `ClockControl`: HFXO control plus runtime CPU-frequency and idle clock-scaling helpers.
- `Gpio`: configure/read/write/toggle and open-drain style drive setup for I2C.
- `Spim`: SPI master with route-aware instance selection (`SPIM21` in USB-bridge `Serial` mode, `SPIM20` in header-UART `Serial` mode), plus EasyDMA transfer and runtime frequency control.
- `Spis`: SPI target/slave mode with EasyDMA buffers, semaphore handover, and transaction-complete polling.
- `Twim`: I2C master on a caller-selected controller base (for example `TWIM22`
  for `D4/D5`, `TWIM30` for `D12/D11`), plus write/read/writeRead and runtime
  frequency control.
- `Uarte`: UART (UARTE21) with EasyDMA TX/RX.
- `Saadc`: single-ended or differential ADC sampling with oversampling, explicit recalibration, and signed millivolt conversion.
- `Timer`: timer/counter setup, compare channels, shortcuts, and callback service.
- `Pwm`: PWM single-output setup with duty/frequency control.
- `Gpiote`: GPIO task/event channels with callback service.
- `Dppic`: DPPI channel helper for publish/subscribe wiring between peripherals.
- `CracenRng`: hardware entropy through the `CRACEN` / `CRACENCORE` RNG FIFO.
- `Aar`: hardware BLE address resolution against one or more IRKs through `AAR00`.
- `Ecb`: hardware AES-128 ECB block encryption through the `ECB00` peripheral and EasyDMA job lists.
- `Ccm`: hardware BLE packet encryption/decryption through `CCM00`.
- `Comp`: general-purpose comparator in single-ended threshold or differential mode.
- `Lpcomp`: low-power comparator with analog-detect behavior suited for wake use.
- `Qdec`: hardware quadrature decoder with accumulator and double-transition reporting.
- `PowerManager`: low-power/constant-latency mode, reset reason, retention registers, DCDC, POF warning, System OFF.
- `Grtc`: global real-time counter setup, SYSCOUNTER readout, compare scheduling, wake timing.
- `TempSensor`: on-die temperature sampling in quarter-degree and milli-degree units.
- `Watchdog`: WDT configuration, start/stop (when enabled), feed, and status reads.
- `BoardControl`: board-level helpers for battery measurement path and antenna switch control.
- `Pdm`: digital microphone interface setup and blocking capture with EasyDMA.
- `I2sTx`: reusable TX-only `I2S20` wrapper with buffer rotation, IRQ service, optional auto-restart, and callback-based buffer refill.
- `I2sRx`: reusable RX-only `I2S20` wrapper with double-buffer capture, IRQ service, optional auto-restart, and callback-based buffer delivery.
- `I2sDuplex`: reusable full-duplex `I2S20` wrapper with shared stop/restart handling, TX refill callback, and RX delivery callback.
- `BleRadio`: register-level BLE 1M link layer + minimal ATT/GATT peripheral path plus central-initiate/client baseline via `RADIO`.
- `ZigbeeRadio`: IEEE 802.15.4 PHY/MAC-lite data-frame + MAC-command frame TX/RX helpers via `RADIO`.
- `RawRadioLink`: proprietary 1 Mbit packet TX/RX helper via `RADIO`.

Raw peripheral compatibility exposed by the core:

- `NRF_DPPIC20`
- `NRF_RADIO`
- `NRF_AAR00`
- `NRF_CCM00`
- `NRF_ECB00`
- `NRF_CRACEN`
- `NRF_CRACENCORE`
- `NRF_SPIS00`
- `NRF_SPIS20`
- `NRF_SPIS21`
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

## ECB note

- `ECB00` is the hardware AES-128 block encryptor.
- It shares the same AES core as `AAR00` and `CCM00`, so `ECB` always has the lowest priority and can abort if another block needs the shared crypto core.
- On `nRF54L15`, the `KEY.VALUE[n]` register byte order is reversed relative to older `nRF52` and `nRF53` families.
- The `Ecb` wrapper accepts the key in normal byte order and handles the reversed register packing internally.

## CCM note

- `CCM00` is the hardware packet-mode AES-CCM block that BLE uses for encrypted data PDUs.
- The first public wrapper here is intentionally BLE-focused: one header byte as AAD, payload bytes as MDATA, and a `4-byte` MIC.
- `encryptBlePacket(...)` returns `ciphertext || mic` because the BLE header byte stays in the clear.
- `decryptBlePacket(...)` returns `false` when the MAC check fails, and the optional `outMacValid` pointer lets sketches report that cleanly.
- The wrapper defaults to `125 Kbit` timing because it is an off-radio helper; if you want to mirror a live link more closely, pass `k1Mbit` or `k2Mbit` explicitly.
- `CCM00` shares the same AES core as `AAR00` and `ECB00`, so it is still a shared crypto resource.

## AAR note

- `AAR00` resolves Bluetooth resolvable private addresses against an IRK list in hardware.
- The `Aar` wrapper takes the raw six address bytes in `HASH[0..2] + PRAND[0..2]` order, which matches the DMA layout expected by the peripheral.
- The IRK list is passed as contiguous `16-byte` Bluetooth IRKs in normal byte order; the wrapper swaps them into the hardware's big-endian AAR layout internally.
- On `nRF54L15`, the output job list format is a little stricter than the datasheet implies; the wrapper hides that quirk and returns the resolved index through the normal API.
- `AAR00` shares the same AES core as `ECB00` and `CCM00`, so it can still lose arbitration against higher-priority crypto users.

## CRACEN RNG note

- `CRACEN` gates the hardware RNG block, and `CRACENCORE.RNGCONTROL` exposes the conditioned entropy FIFO.
- `CracenRng` is the clean wrapper for that path. It polls the FIFO, checks the health/status flags, and returns real random bytes to sketches.
- This is separate from Arduino `random()`, which is still just a pseudo-random generator until you seed it.
- If you want true entropy directly, use `CracenRng::fill(...)`. If you want legacy Arduino helpers with a real seed, read a `uint32_t` from `CracenRng` and pass it to `randomSeed(...)`.

## Power-fail comparator note

- `POF` is a supply warning comparator inside the power block, exposed through `PowerManager`.
- It monitors `VDD`, not the raw battery pin. On a regulated board, that means it is best for brownout or supply-sag warning, not direct battery gauging.
- If you want raw cell voltage, use `BoardControl::sampleBatteryMilliVolts(...)`. If you want "warn me before the rail collapses", use `POF`.

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
- Core `Wire`, `SPI`, and `Serial` now support runtime `setPins(...)` remapping.

Module-board note:

- the package now also ships a dedicated `HOLYIOT-25008 nRF54L15 Module`
  variant with onboard RGB LED, button, LIS2DH12 aliases, and a
  `Serial Routing` tools menu that can free `D0/D1` as GPIO
- the package now also ships a shared 36-pad module variant used by
  `HOLYIOT-25007 nRF54L15 Module` and
  `Generic nRF54L15 Module (36-pad)`
- that variant keeps real GPIO aliases like `P2_08` and `P1_10` alongside
  Arduino aliases like `D21` and `D1`
- module defaults are documented in the root docs at
  `docs/holyiot-25007-module-reference.md`
  and `docs/holyiot-25008-module-reference.md`
- dedicated library examples for the 25008 onboard hardware now appear under
  `File -> Examples -> Nrf54L15-Clean-Implementation -> Board`

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

## BLE Address Helpers

BLE addresses are still stored as raw 6-byte arrays internally, but the library
now also accepts and formats the normal human-readable text form:

```cpp
g_ble.setDeviceAddressString("C0:DE:54:15:00:21",
                             BleAddressType::kRandomStatic);

char addressText[kBleAddressStringLength];
g_ble.getDeviceAddressString(addressText, sizeof(addressText));
// addressText => "C0:DE:54:15:00:21"
```

You can also convert scan results or other raw address arrays with
`formatBleAddressString(...)`, and parse text into raw bytes with
`parseBleAddressString(...)`.

Arduino IDE organization:

- `File -> Examples -> Nrf54L15-Clean-Implementation -> BLE`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> LowPower`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Diagnostics`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Board`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Peripherals`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> VPR`
- `File -> Examples -> Nrf54L15-Clean-Implementation -> Zigbee`

The VPR probes are now surfaced directly under the library example menu as
well as the board-package `Peripherals` menu, so they are easier to find in
Arduino IDE.

These examples assume `Tools -> VPR Support -> Enabled (Default)`. If you turn
`VPR Support` off to reclaim RAM for a normal sketch, leave the VPR examples
and VPR-backed channel-sounding paths alone.

Current VPR probe set includes:

- `VprSharedTransportProbe`
- `VprFnv1aOffloadProbe`
- `VprCrc32OffloadProbe`
- `VprCrc32cOffloadProbe`
- `VprTickerOffloadProbe`
- `VprTickerAsyncEventProbe`
- `VprBleLegacyAdvertisingProbe`
- `VprBleConnectionStateProbe`
- `VprBleConnectionCsBindProbe`
- `VprBleConnectionCsWorkflowProbe`
- `VprBleConnectionCsProcedureProbe`
- `VprBleConnectionCsHandoffProbe`
- `VprHibernateContextProbe`
- `VprHibernateWakeProbe`
- `VprHibernateResumeProbe`
- `VprRestartLifecycleProbe`

Zigbee example organization:

- `Coordinator`: personal-area coordinator sketches and HA interview demos.
- `Router`: mesh-extending router examples.
- `EndDevices`: generic end-device commissioning baselines.
- `Lights`: Home Automation on/off and dimmable light examples.
- `Sensors`: always-on temperature + battery sensor examples.
- `LowPower`: sleepy sensor examples that wake, report state, check for pending work, then return to `SYSTEM OFF`.
- `Interoperability`: small ping/pong sketches for radio-path validation.

Current Zigbee Home Automation device coverage in this core:

- on/off light
- dimmable light / level control
- temperature sensor with battery/power cluster reporting

Current Zigbee non-goal:

- RGB/color-light clusters are not implemented yet, so there is no true color-bulb example in this release.

## Bluefruit Compatibility

`Bluefruit52Lib` is the compatibility layer used to bring common XIAO nRF52840
/ Seeed Bluefruit sketches across to the nRF54L15 core with minimal rewrites.

Current validated target:

- common central and peripheral BLE flows used by the shipped examples
- common services/helpers such as `BLEDfu`, `BLEDis`, `BLEBas`, `BLEUart`
- advertising helpers including name/appearance/manufacturer data/service UUIDs
- Seeed-style sketch helpers such as `digitalToggle()`, `LED_STATE_ON`,
  `suspendLoop()`, `Print::printf()`, `printBuffer()`, and beacon helpers

Runtime that is known to work on real hardware includes:

- Bluefruit BLE UART / NUS
- central service discovery and notification flows covered by the compatibility
  examples
- mixed nRF54 <-> nRF54 and nRF54 <-> nRF52840 pairings on the validated paths

Still outside the intended scope:

- full SoftDevice-equivalent parity for every API edge case
- full HID/MIDI/ANCS/HomeKit service coverage

Representative unchanged Seeed/Bluefruit examples compile with the local core:

- `Peripheral/nrf_blinky`
- `Peripheral/bleuart`
- `Peripheral/bleuart_multi`
- `Peripheral/throughput`
- `Peripheral/custom_hrm`
- `Peripheral/custom_htm`
- `Peripheral/adv_advanced`
- `Peripheral/adv_AdafruitColor`
- `Peripheral/beacon`
- `Peripheral/blinky_ota`
- `Central/central_scan`
- `Central/central_scan_advanced`
- `Central/central_bleuart`
- `Central/central_custom_hrm`

Validation note:

- unchanged upstream-style `bleuart` advertising and connection were validated
  on attached hardware
- central notify / discovery regressions from earlier releases were fixed, and
  the shipped compatibility examples are the supported surface

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
- ECB hardware AES-128 block encryption against the datasheet known vector
- CCM hardware BLE packet encryption/decryption against a Bluetooth spec vector
- AAR hardware BLE private-address resolution against a generated IRK list

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

`examples/Board/PofWarningMonitor/PofWarningMonitor.ino` demonstrates:

- `PowerManager` power-fail warning threshold configuration from `1.7 V` to `3.2 V`.
- Polling `POFWARN` and reading the current comparator state without needing an IRQ handler.
- The practical distinction between `VDD` supply warning and raw VBAT measurement on the XIAO board.

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
- `examples/Peripherals/SaadcDifferentialProbe/SaadcDifferentialProbe.ino`
  - Samples `A0` single-ended and `A0-A1` differential using SAADC `8x` oversampling.
  - Shows explicit recalibration and signed millivolt reporting for differential inputs.
- `examples/Peripherals/SpisTargetEcho/SpisTargetEcho.ino`
  - Uses the new `Spis` wrapper to act as an SPI target on `CS=D2 SCK=D8 MISO=D9 MOSI=D10`.
  - Demonstrates the semaphore-based target flow: preload DMA buffers, release the transaction, and inspect what the controller actually clocked.
- `examples/Peripherals/CracenRandomBytes/CracenRandomBytes.ino`
  - Reads a 32-byte burst directly from the hardware entropy FIFO and prints it in hex.
  - Good first check when you want to prove you are getting real silicon entropy instead of pseudo-random state.
- `examples/Peripherals/CracenSeedArduinoRandom/CracenSeedArduinoRandom.ino`
  - Reads a hardware-random seed and hands it to Arduino `randomSeed(...)`.
  - This is the most practical bridge for existing sketches that already rely on Arduino `random()`.
- `examples/Peripherals/EcbAesKnownVector/EcbAesKnownVector.ino`
  - Uses the `Ecb` wrapper with the exact AES-128 sample vector from the `nRF54L15` datasheet.
  - Good first check when you want to confirm the key-byte-order handling and EasyDMA job-list setup are both correct.
- `examples/Peripherals/CcmBleSpecVector/CcmBleSpecVector.ino`
  - Uses the `Ccm` wrapper with a Bluetooth LE CCM spec packet and checks both encrypt and decrypt.
  - Good first check when you want to confirm the BLE nonce packing, AAD handling, and MIC generation are all correct.
- `examples/Peripherals/CcmBlePacketTamperDetect/CcmBlePacketTamperDetect.ino`
  - Encrypts an application-style BLE payload, decrypts it back, then flips one byte to prove the hardware rejects a bad MIC.
  - This is the most practical "what does CCM buy me?" demo because it shows both confidentiality and authenticity.
- `examples/Peripherals/AarResolvePrivateAddress/AarResolvePrivateAddress.ino`
  - Verifies both a known spec-style RPA and a generated RPA against the hardware `AAR` block.
  - Good first check when you want to confirm the job-list setup, address byte order, and resolved-index reporting are all correct on real hardware.
- `examples/Peripherals/QdecRotaryReporter/QdecRotaryReporter.ino`
  - Uses the hardware `QDEC` block to decode a rotary encoder on `D0/D1`.
  - Prints signed movement deltas and accumulated position without software edge decoding.
- `examples/Peripherals/DppicHardwareBlink/DppicHardwareBlink.ino`
  - Wires `TIMER -> DPPIC -> GPIOTE` so the LED toggles in hardware.
  - Demonstrates the useful part of DPPI: precise work with no CPU in the timing loop.

BLE examples:

- The BLE tree is grouped by task so the Arduino IDE menu stays navigable:
  - `Advertising` for legacy/extended advertisers and background advertiser demos
  - `AdvertisingLowPower` for low-duty-cycle or system-off advertising patterns
  - `Scanning` for passive/active and extended scanners
  - `Connections` for connected link bring-up and timing instrumentation
  - `GATT` for services, characteristics, notifications, and central discovery flows
  - `NordicUart` for NUS bridge, console, and probe sketches
  - `Security` for pairing, encryption, and bond persistence checks
  - `ChannelSounding` for two-board channel probing examples
  - `Diagnostics` for stress rigs and issue-focused debug probes

- `examples/BLE/Advertising/BleAdvertiser/BleAdvertiser.ino`
  - Legacy advertising on channels 37/38/39 with custom ADV payload.
- `examples/BLE/Scanning/BlePassiveScanner/BlePassiveScanner.ino`
  - Passive scanner over channels 37/38/39 with RSSI and header parsing.
- `examples/BLE/Scanning/BleActiveScanner/BleActiveScanner.ino`
  - Active scanner that logs both the raw legacy payload length (`AdvA + data`, up to `37`) and the AD-data length (`0..31`).
  - Useful for verifying whether bytes live in the primary ADV payload or only in the scan response.
- `examples/BLE/Advertising/BleLegacyAdv31Plus31/BleLegacyAdv31Plus31.ino`
  - Fills the full legacy `31`-byte `AdvData` budget and the full legacy `31`-byte `ScanRspData` budget.
  - Uses `ADV_SCAN_IND` so a phone or `BleActiveScanner` can verify the practical legacy maximum without opening a connection.
- `examples/BLE/Advertising/BleExtendedAdv251/BleExtendedAdv251.ino`
  - Demonstrates minimal Extended Advertising with `ADV_EXT_IND` on the primary channels and one `AUX_ADV_IND` payload on a fixed secondary channel.
  - Current scope is LE 1M, non-connectable, non-scannable, single auxiliary payload, up to `251` bytes of AdvData.
- `examples/BLE/Advertising/BleExtendedAdv499/BleExtendedAdv499.ino`
  - Demonstrates the same TX path with one `AUX_CHAIN_IND` after the first auxiliary packet.
  - Current chained limit is `499` bytes of AdvData total.
- `examples/BLE/Advertising/BleExtendedAdv995/BleExtendedAdv995.ino`
  - Fills the current multi-chain TX limit with one `AUX_ADV_IND` and three `AUX_CHAIN_IND` follow-ups.
  - Current maximum is `995` bytes of AdvData total.
- `examples/BLE/Advertising/BleExtendedScannableAdv251/BleExtendedScannableAdv251.ino`
  - Demonstrates scannable Extended Advertising with `ADV_EXT_IND` on the primary channels, one `AUX_ADV_IND`, and one `AUX_SCAN_RSP` payload on a fixed secondary channel.
  - Current scope is LE 1M, non-connectable, scannable, with up to `251` bytes of ScanRspData in the single-response path.
- `examples/BLE/Scanning/BleExtendedScanner/BleExtendedScanner.ino`
  - Passively follows `ADV_EXT_IND -> AUX_ADV_IND -> AUX_CHAIN_IND` and reassembles the current extended AdvData payload.
  - Current RX scope is LE 1M only and matches the core's non-connectable/non-scannable extended TX path.
- `examples/BLE/Scanning/BleExtendedActiveScanner/BleExtendedActiveScanner.ino`
  - Actively scans the scannable extended path and follows `ADV_EXT_IND -> AUX_ADV_IND -> AUX_SCAN_RSP`, with optional `AUX_CHAIN_IND` follow-ups after the first scan response packet.
  - Current RX scope is LE 1M only and matches the core's non-connectable/scannable extended TX path.
- `examples/BLE/Advertising/BleConnectableScannableAdvertiser/BleConnectableScannableAdvertiser.ino`
  - Uses `ADV_IND`, listens for `SCAN_REQ`/`CONNECT_IND`, and sends `SCAN_RSP`.
  - Exposes interaction counters and peer addresses over UART.
- Legacy payload note:
  - `AdvData` and `ScanRspData` are each limited to `31` bytes in legacy BLE.
  - The on-air PDU body can still be `37` bytes because it includes `AdvA` (`6` bytes) plus the `31` bytes of AD data.
- Extended Advertising note:
  - This core now includes both a non-scannable `ADV_EXT_IND -> AUX_ADV_IND -> AUX_CHAIN_IND` TX path and a scannable `ADV_EXT_IND -> AUX_ADV_IND -> AUX_SCAN_RSP` TX path.
  - The scanner side can passively reassemble the non-scannable chain and actively scan the scannable path, including chained `AUX_CHAIN_IND` follow-ups after `AUX_SCAN_RSP`.
  - Current limits are LE 1M only, non-connectable only, up to `995` bytes of AdvData on the non-scannable path, and up to `995` bytes of ScanRspData on the scannable path.
  - Extended connectable advertising is still not implemented.
- `examples/BLE/Connections/BleConnectionPeripheral/BleConnectionPeripheral.ino`
  - Accepts legacy `CONNECT_IND`, tracks connection parameters, and runs data-channel events.
  - Responds to common LL control PDUs and ATT requests, with link event metadata logs.
- `examples/BLE/Connections/BleConnectionTimingMetrics/BleConnectionTimingMetrics.ino`
  - Measures connection-event outcomes over rolling windows (RX ok/CRC fail/RX timeout/TX timeout).
  - Useful for comparing `BLE Timing Profile` tool options while keeping TX power explicit in sketch code.
- `examples/BLE/GATT/BleGattBasicPeripheral/BleGattBasicPeripheral.ino`
  - Connectable/scannable BLE peripheral with minimal GATT database (GAP/GATT/Battery).
  - Supports ATT MTU exchange and basic discovery/read requests over CID `0x0004`.
  - Supports Battery Level CCCD writes and Handle Value Notifications.
- `examples/BLE/GATT/BleCustomGattRuntime/BleCustomGattRuntime.ino`
  - Demonstrates runtime registration of custom 16-bit GATT service/characteristics.
  - Includes writable characteristic, CCCD-backed notification characteristic, and serial command hooks.
- `examples/BLE/GATT/BleNotifyEchoPeripheral/BleNotifyEchoPeripheral.ino`
  - Minimal notify-oriented custom GATT example for issue-driven bring-up and debugging.
  - Exposes writable `0xFFF1` and notify `0xFFF2` characteristics under service `0xFFF0`.
  - Pairs with `scripts/ble_notify_echo_central.py` for a simple host-side central that writes text and prints notifications.
- `examples/BLE/NordicUart/BleNordicUartBridge/BleNordicUartBridge.ino`
  - Exposes the standard Nordic UART Service peripheral and bridges BLE RX/TX to USB `Serial`.
  - Good first smoke test with nRF Connect or any NUS-capable phone/desktop client.
- `examples/BLE/NordicUart/BleNordicUartCommandConsole/BleNordicUartCommandConsole.ino`
  - Uses the same NUS transport as a simple text command console.
  - Includes `help`, `status`, `led on`, `led off`, `led toggle`, and `echo <text>` commands.
- `examples/BLE/GATT/BleBatteryNotifyPeripheral/BleBatteryNotifyPeripheral.ino`
  - Connectable/scannable BLE peripheral focused on Battery Level notifications.
  - Periodically updates battery percentage and emits notifications when CCCD notify is enabled.
- `examples/BLE/GATT/BleNotifyPeripheral/BleNotifyPeripheral.ino`
  - Minimal custom notify peripheral using one runtime-registered 16-bit characteristic.
  - Companion sketch for the central notify example.
- `examples/BLE/GATT/BleNotifyCentral/BleNotifyCentral.ino`
  - Minimal central role example that scans, sends `CONNECT_IND`, and runs a master-side connection loop.
  - Discovers the custom primary service, characteristic, and CCCD over ATT before enabling notifications.
- `examples/BLE/Security/BlePairingEncryptionStatus/BlePairingEncryptionStatus.ino`
  - Shows LL control and encryption state transitions during pairing/encryption.
- `examples/BLE/Security/BleBondPersistenceProbe/BleBondPersistenceProbe.ino`
  - Demonstrates bond retention across resets and reconnect-side encryption reuse.
  - Hold user button at boot to clear persistent bond state.
- `examples/BLE/ChannelSounding/BleChannelSoundingReflector/BleChannelSoundingReflector.ino`
  - Two-board phase-sounding reflector using `RADIO.CSTONES`/DFE capture on requested BLE channels.
  - Returns reflector-side IQ/magnitude terms for the initiator's phase-slope distance fit.
- `examples/BLE/ChannelSounding/BleChannelSoundingInitiator/BleChannelSoundingInitiator.ino`
  - Sweeps BLE data channels with tone-extended probes and combines both endpoints' IQ terms.
  - Reports `dist_m`, rolling `median_m`, `valid_channels`, and fit `residual` from the phase-slope estimator.
- `examples/BLE/ChannelSounding/BleChannelSoundingVprLinkedInitiator/BleChannelSoundingVprLinkedInitiator.ino`
  - Uses the generic VPR BLE link snapshot as the source for the dedicated CS image.
  - Runs one imported-link CS workflow without the SWD-summary probe harness and prints the nominal regression estimate over `Serial`.
- `examples/BLE/ChannelSounding/BleChannelSoundingVprServiceNominal/BleChannelSoundingVprServiceNominal.ino`
  - Uses the generic VPR BLE controller service in-place without booting the dedicated CS image.
  - Single-board nominal example: no reflector is required, and `nominal_dist_m` remains synthetic regression output only.
  - Prints controller-owned completed-result summary fields as `summary=`, `steps=`, `modes=`, `ch=`, and `hash=`.

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
  - Periodic ADC sampling with SAADC enabled only during sample windows and `8x` oversampling enabled.
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
- `examples/BLE/AdvertisingLowPower/LowPowerBleBeaconDutyCycle/LowPowerBleBeaconDutyCycle.ino`
  - Sends short BLE advertising bursts, then sleeps with `WFI` between bursts.
  - Uses low-power latency mode and 64 MHz CPU clock to reduce average current.
- `examples/BLE/AdvertisingLowPower/BleAdvertiserLowestPowerContinuous/BleAdvertiserLowestPowerContinuous.ino`
  - Lowest validated continuous BLE advertiser baseline on the current raw BLE path.
  - Uses `ADV_IND`, the default FICR-derived address, `-10 dBm`, and `3000 ms` intervals.
  - Use `Low Power (WFI Idle)` board profile; the core low-power GRTC tick path is now required for this example.
  - Intended for `System ON + WFI between advertising events`, not `SYSTEM OFF`.
- `examples/BLE/AdvertisingLowPower/BleAdvertiserRfSwitchDutyCycle/BleAdvertiserRfSwitchDutyCycle.ino`
  - Continuous advertiser that powers/selects the RF switch only around each `advertiseEvent(...)`.
  - Leaves the RF switch off and the control pin high-impedance while idle to test board-level switch quiescent current.
  - Edit `kAdvertisingIntervalMs` in the sketch if you want a shorter scan-friendly cadence.
- `examples/BLE/AdvertisingLowPower/BleAdvertiserHybridDutyCycle/BleAdvertiserHybridDutyCycle.ino`
  - Sends short advertising bursts in `System ON`, then idles with `WFI` and RF-switch collapse between bursts.
  - Intended as the middle operating point between persistent visibility and the lowest burst-beacon current.
  - Edit the top-of-sketch burst constants if you want a different burst period or event count.
- `examples/BLE/AdvertisingLowPower/BleAdvertiserPhoneBeacon15s/BleAdvertiserPhoneBeacon15s.ino`
  - Non-connectable phone-tuned beacon pattern: longer wake burst, name in the primary ADV payload, no scan-response dependence, then timed `SYSTEM OFF`.
  - Intended for low-average-current beaconing where scanner catchability matters more than minimum RF-on time.
- `examples/BLE/AdvertisingLowPower/BleAdvertiserBurstSystemOff/BleAdvertiserBurstSystemOff.ino`
  - Sends a short advertising burst, collapses the RF switch path, then enters timed `SYSTEM OFF`.
  - The validated baseline uses `6` events per boot, `20 ms` inter-burst gaps, `1000 ms` system-off intervals, and the default boot path.
  - Uses the explicit `NoRetention` system-off helpers so the low-power examples can drop RAM retention without changing the default `systemOff*()` semantics for retained `.noinit` users.
  - Intended for burst beaconing, not continuously discoverable BLE presence.
  - Edit `kSystemOffIntervalMs` in the sketch if you want a sparser or denser wake cadence.
- `examples/BLE/Advertising/BleAdvertiserProbe/BleAdvertiserProbe.ino`
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
  - Central-initiated BLE connections on the in-tree Arduino controller path. The notify companion for `BleNotifyEchoPeripheral` is host-side (`scripts/ble_notify_echo_central.py`) for that reason.
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
