# Continue Report

## Update After The Latest Hardware And Compile Pass

This report was partially superseded by the work now on `main`.

What changed after the original checkpoint:

- The nRF54<nrf54 BLE stability issue was fixed in the low-level controller.
  The key fix was re-anchoring peripheral connection timing from the actual
  received central packet on every successful event instead of only during the
  initial sync window.
- The Bluefruit central/GATT runtime layer is no longer just a stub. The
  wrapper now supports:
  `BLEClientService`, `BLEClientCharacteristic`, `BLEClientUart`,
  `BLEClientDis`, `BLEClientBas`, scanner filters, central connect flow, and
  notification delivery into Bluefruit client objects.
- Hardware validation now covers both same-core and mixed-core links:
  - nRF54 central <-> nRF54 peripheral: stable
  - nRF54 central <-> Seeed/Bluefruit nRF52840 peripheral: stable
  - Seeed/Bluefruit nRF52840 central <-> nRF54 peripheral: stable

Compile validation also moved much further than the earlier handoff implied.

Using the unchanged upstream Seeed `Bluefruit52Lib` example sketches and the
nRF54 board package:

- BLE-oriented examples under `Central/`, `DualRoles/`, `Peripheral/`, and
  `Projects/` compiled `37` pass / `13` fail.
- Every one of the `13` failures was caused by a missing extra dependency in
  the local machine, not by an nRF54 wrapper/core API gap.
- Bluefruit hardware examples compiled `20` pass / `0` fail after adding
  narrow nRF52-style register-compat shims for `hwinfo` and `nfc_to_gpio`.

The remaining BLE example misses from the compile sweep were:

- `StandardFirmataBLE`: missing `Servo.h`
- `arduino_science_journal`: missing `PDM.h`
- `blemidi`: missing `MIDI.h`
- `bluefruit_playground`: missing `SdFat.h`
- `ancs_arcada`, `pairing_passkey_arcada`: missing `Adafruit_Arcada.h`
- `image_eink_transfer`: missing `Adafruit_EPD.h`
- `image_transfer`: missing `Adafruit_ILI9341.h`
- `neomatrix`, `neopixel`, `nrf_blinky`, `tf4micro-motion-kit`: missing
  `Adafruit_NeoPixel.h`
- `homekit_lightbulb`: missing `BLEHomekit.h`

So the next compatibility work, if continued, should focus on deciding which of
those external stacks belong in the nRF54 package versus which should remain
optional user-installed libraries.

## Scope Of This Checkpoint

This checkpoint is the in-progress `0.2.0` compatibility overhaul intended to let more Seeed/Bluefruit-style XIAO nRF52840 BLE sketches build and run on the XIAO nRF54L15 clean core.

What is already in this repo at this checkpoint:

- `Bluefruit52Lib/` compatibility library added under:
  `hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/`
- Core helpers added for Seeed/Bluefruit sketch compatibility:
  `digitalToggle()`, `suspendLoop()`, `resumeLoop()`, `Print::printf()`,
  `printBuffer()`, `printBufferReverse()`, LED aliases, etc.
- Existing low-level BLE work from earlier sessions is still present:
  notify examples, central baseline, connection stability fixes, ATT/GATT plumbing.
- Version metadata in the working tree has already been bumped to `0.2.0`.

This report focuses on the live runtime BLE findings from the latest hardware session and what still needs to be finished.

## The Most Important Current Conclusion

The current blocker is no longer basic advertising or basic connection establishment for the Bluefruit wrapper.

The real remaining gap is:

- `Bluefruit52Lib` central/client classes are still mostly compile-time shims.
- The low-level controller can now advertise correctly and a second nRF54L15 can connect to the Bluefruit `bleuart` peripheral.
- What still needs implementation is the actual Bluefruit central/client runtime layer:
  `BLEClientService`, `BLEClientCharacteristic`, `BLEClientUart`,
  `BLEClientDis`, `BLEClientBas`, plus the scanner/central connection orchestration that upstream Bluefruit examples expect.

In short:

- Peripheral-side Bluefruit compatibility is much further along.
- Central/client-side Bluefruit compatibility is the next real implementation target.

## Important Clarification: Low-Level Central/GATT Work Is Already On `main`

The low-level BLE central and ATT client work is already present in the pushed `main` checkpoint.

That includes the central-side controller primitives in:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp`

Specifically, `main` already contains:

- `BleConnectionRole`
- `initiateConnection(...)`
- `queueAttRequest(...)`
- `queueAttReadRequest(...)`
- `queueAttWriteRequest(...)`
- `queueAttCccdWrite(...)`
- `connectionRole()`
- `startCentralConnection(...)`
- `pollCentralConnectionEvent(...)`

The notify demo pair is also already on `main`:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/BleNotifyCentral/BleNotifyCentral.ino`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/BleNotifyPeripheral/BleNotifyPeripheral.ino`

So the remaining missing work is not the low-level central controller path itself.

The remaining missing work is the higher-level Bluefruit compatibility runtime on top of it, especially:

- `BLEClientService`
- `BLEClientCharacteristic`
- `BLEClientUart`
- `BLEClientDis`
- `BLEClientBas`
- scanner/central wrapper behavior expected by unchanged upstream Bluefruit examples

## Files That Matter Most Next

High priority implementation area:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/src/bluefruit.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/src/bluefruit.cpp`

Low-level BLE layer that the Bluefruit client code should wrap:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp`

Examples already involved in this effort:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/BleNotifyCentral/`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/BleNotifyPeripheral/`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/BLE/BleGattBasicPeripheral/`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/examples/`

## Boards Used In Validation

Two XIAO nRF54L15 boards were used throughout this session:

- board A / peripheral probe UID: `E91217E8`
- board B / central/scanner probe UID: `DD7B1F42`

The local Arduino CLI override core path used for testing was:

- `/home/lolren/Arduino/hardware/nrf54l15clean/nrf54l15clean`

The repo copy was synced there during the session with `rsync`.

## Critical Debugging Trap That Caused False Leads

`pyocd commander` halts the target.

That matters a lot for BLE validation.

Earlier in the session, some misleading results came from reading memory over SWD and then immediately testing advertising or scanning again without resetting the board. This can make it look like:

- advertising disappeared
- the host sees a device but the second nRF54L15 does not
- connection attempts fail for mysterious reasons

because the target that was read over SWD may still be halted.

Rule for future testing:

- after any `pyocd commander` memory read on a live BLE target, explicitly run `reset` before doing further over-the-air tests
- avoid touching the active peripheral board over SWD while validating scan/connect behavior from the host or the other board

## What Was Proven On Hardware In The Latest Session

### 1. The plugged peripheral board really uses address `D0:AC:F9:59:22:6E`

Board A FICR confirmed:

- `DEVICEADDRTYPE = 1` -> random address
- raw address bytes from FICR: `6e 22 59 f9 ac d0`
- human-readable address: `D0:AC:F9:59:22:6E`

This matched the host-visible Bluefruit wrapper advertiser.

### 2. The fresh unchanged upstream `bleuart` sketch is advertising correctly

Host-side scan found:

- name: `XIAO nRF54L15`
- address: `D0:AC:F9:59:22:6E`
- address type reported by BlueZ: `random`
- UUID: `Nordic UART Service (6e400001-b5a3-f393-e0a9-e50e24dcca9e)`

This means the current wrapper peripheral is at least good enough for:

- legacy advertising visibility
- scan response name visibility
- NUS service UUID exposure

### 3. The second nRF54L15 scanner sees the same peripheral correctly

Using the temporary `scan_probe` sketch on board B and reading globals over SWD, after resetting board A and not touching it again:

- target address seen by board B: `6e 22 59 f9 ac d0`
- `matchHeader = 0x40`
- `matchPayloadLength = 0x1e`
- `matchRandom = 1`
- `matchScanRsp = 1`

Interpretation:

- `0x40` means `ADV_IND` with random/static address
- the peripheral is connectable and scannable
- scan response is actually being received by the second board

This is important because it disproves the earlier suspicion that Bluefruit `bleuart` was still advertising as non-connectable `ADV_NONCONN_IND`.

That earlier conclusion was based on stale firmware / stale target state and was not the final truth.

### 4. The second nRF54L15 central can connect to the Bluefruit `bleuart` peripheral

Using the temporary `bluefruit_probe_central` sketch on board B:

- `g_connectAttemptCount = 1`
- `g_lastErrorCode = 0`
- `g_serviceStartHandle = 0x0025`
- `g_serviceEndHandle = 0x002a`
- `g_txHandle = 0x0027`

Interpretation:

- connection to the Bluefruit wrapper peripheral succeeded
- primary service discovery for NUS succeeded
- at least one BLE UART characteristic handle was discovered

The probe ended in failure state only because the probe logic itself was incomplete, not because connection failed.

## Important Probe Limitation That Must Not Be Misread As A Core Failure

The temporary `bluefruit_probe_central` sketch currently assumes both 128-bit BLE UART characteristics will appear in a single `Read By Type` response.

That assumption is wrong with ATT MTU 23.

For 128-bit characteristic discovery:

- each characteristic declaration entry is large
- a single `Read By Type Response` may only contain one entry
- the client must continue discovery from the next handle until the service end handle

So this probe can currently produce:

- service discovered
- one characteristic handle discovered
- final failure state

even when the underlying connection and service discovery are correct.

This is a probe limitation and also a direct pointer to the missing runtime work in `Bluefruit52Lib`: proper iterative characteristic discovery logic.

## What Was Checked In The Bluefruit Wrapper Object State

Using a temporary `bluefruit_adv_state` sketch and SWD globals on board B:

- `Bluefruit.Advertising.adv_type_` before start: `0`
- `Bluefruit.Advertising.adv_type_` after start: `0`
- advertising payload length: `24`
- scan response payload length: `15`
- running flag: `1`

Interpretation:

- the wrapper object itself is still selecting `ADV_IND`
- there is no current evidence that the advertising type is being clobbered inside `Bluefruit52Lib`

That earlier debugging path can be considered closed for the fresh build.

## What Is Still Not Finished

### A. Bluefruit central/client runtime compatibility

This is the main unfinished area.

Current status of these classes:

- `BLEClientService`
- `BLEClientCharacteristic`
- `BLEClientUart`
- `BLEClientDis`
- `BLEClientBas`

They still behave mostly like stubs in `bluefruit.h`.

What needs to be implemented:

- actual service discovery against the low-level central connection
- iterative characteristic discovery across handle ranges
- CCCD discovery / subscription
- notify RX path for `BLEClientUart`
- write path for central-to-peripheral BLE UART TX
- read helpers for Battery Service and Device Information Service

### B. Scanner/Central API behavior for unchanged upstream examples

The compile side is already broad, but runtime still needs proper behavior for examples like:

- `examples/Central/central_bleuart`
- `examples/Central/central_scan`
- `examples/Central/central_scan_advanced`
- `examples/Central/central_custom_hrm`

The main example that should become the runtime target first is:

- `Bluefruit52Lib/examples/Central/central_bleuart/central_bleuart.ino`

because it is the most useful proof that Bluefruit-style central compatibility is real.

### C. Host-side Linux GATT connection needs revalidation in a clean window

There were host-side `gatttool` timeouts earlier.

Because the session included:

- stale firmware confusion
- boards halted by SWD reads
- board-to-board central activity

those host connection results should be treated as provisional until re-run cleanly.

A clean rerun should be done with:

- board A advertising
- board B idle or disconnected
- no recent SWD reads against board A

Then re-check:

- `gatttool --primary`
- `gatttool --characteristics`
- `bluetoothctl connect`
- Android nRF Connect / NUS if possible

## What The Next Implementation Pass Should Do

### 1. Build a real Bluefruit client-side discovery layer

Recommended approach:

- add a small client state machine in `Bluefruit52Lib/src/bluefruit.cpp`
- keep it wrapper-side instead of stuffing Bluefruit-specific state into `BleRadio` unless clearly necessary
- reuse the existing low-level APIs:
  - `initiateConnection(...)`
  - `queueAttRequest(...)`
  - `queueAttReadRequest(...)`
  - `queueAttWriteRequest(...)`
  - `queueAttCccdWrite(...)`
  - `pollConnectionEvent(...)`

What that state machine should support first:

- discover primary service by UUID
- iteratively discover all characteristics in a service range
- discover CCCD handles with `Find Information`
- enable notifications on the BLE UART TX characteristic
- route `Handle Value Notification` payloads into `BLEClientUart`

### 2. Make `BLEClientUart` the first real runtime client

This should be the first fully working client because it unlocks the best runtime validation path.

Needed behavior:

- `begin()`
- `discover(conn_hdl)`
- `discovered()`
- `enableTXD()`
- `available()`
- `read()`
- `write()`
- `setRxCallback()`

Concrete requirement from the upstream example:

- `central_bleuart` should run unchanged against the unchanged upstream `Peripheral/bleuart`

### 3. Then implement the small read-only clients

After `BLEClientUart`, do:

- `BLEClientBas::read()`
- `BLEClientDis::getManufacturer()`
- `BLEClientDis::getModel()`

These are simpler because they only need service discovery plus attribute reads.

### 4. Then revisit scanner/central wrapper callbacks

Need runtime behavior for:

- `Bluefruit.Scanner.setRxCallback(...)`
- `Bluefruit.Scanner.start(...)`
- `Bluefruit.Scanner.resume()`
- `Bluefruit.Central.connect(report)`
- central connect/disconnect callbacks

This can initially target:

- single central connection
- BLE UART first

That is enough for practical compatibility progress.

## Suggested Test Matrix When Hardware Is Available Again

### Priority 1: Bluefruit BLE UART

1. Flash unchanged upstream `Peripheral/bleuart` to board A.
2. Flash unchanged upstream `Central/central_bleuart` to board B.
3. Confirm:
   - scan callback fires
   - central connects
   - NUS service discovery succeeds
   - TXD notify enable succeeds
   - data typed on one side is received on the other

### Priority 2: Service read clients

Run central examples or a small test sketch for:

- Device Information read
- Battery Service read

### Priority 3: Reconnect / soak

1. Keep BLE UART connected for 5 to 10 minutes.
2. Reset peripheral board.
3. Confirm central reconnects and re-discovers.
4. Reset central board.
5. Confirm clean reconnect path again.

### Priority 4: Host / Android

1. Linux host:
   - `bluetoothctl scan on`
   - `bluetoothctl connect <addr>`
   - `gatttool --primary`
   - `gatttool --characteristics`
2. Android:
   - nRF Connect
   - any NUS app or custom Android bridge if later needed

## Exact Helpful Observations From This Session

### Board A live BLE UART peripheral

- host address: `D0:AC:F9:59:22:6E`
- address type: random static
- name: `XIAO nRF54L15`
- NUS UUID visible to BlueZ

### `scan_probe` good run result on board B

From SWD globals after board A was reset and left untouched:

- `g_matchAddress = 6e 22 59 f9 ac d0`
- `g_matchHeader = 0x40`
- `g_matchPayloadLength = 0x1e`
- `g_matchRandom = 1`
- `g_matchScanRsp = 1`

### `bluefruit_probe_central` good run result on board B

- connected successfully
- NUS service discovered at `0x0025 .. 0x002a`
- one characteristic handle discovered: `0x0027`
- probe then failed only because discovery logic was incomplete

## Commands That Were Useful

Compile a sketch:

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 <sketch-dir> --build-path <build-dir>
```

Flash a board by CMSIS-DAP UID:

```bash
pyocd load -u E91217E8 -t nrf54l <hex>
pyocd load -u DD7B1F42 -t nrf54l <hex>
```

Read RAM / globals:

```bash
pyocd commander -u DD7B1F42 -t nrf54l -c "read8 0x20001658 40"
```

Reset a board after SWD access:

```bash
pyocd commander -u E91217E8 -t nrf54l -c "reset"
```

Host scan:

```bash
bluetoothctl --timeout 8 scan on
```

## Final Practical Recommendation For The Next Session

Do not spend more time re-proving the advertising path unless it regresses again.

The most productive next step is:

1. implement real Bluefruit central/client runtime logic in `Bluefruit52Lib`
2. target unchanged `central_bleuart` as the first runtime compatibility proof
3. only after that, re-run Linux and Android validation

That is the shortest path to converting the current `0.2.0` checkpoint from "broad compile compatibility plus partial runtime proof" into "real Bluefruit compatibility".
