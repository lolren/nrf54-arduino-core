# Feature Parity: Nrf54L15-Clean-Implementation

Last updated: 2026-03-09

This project targets practical Arduino parity for XIAO nRF54L15 using a clean OSS core:

- No Zephyr runtime
- No nRF Connect SDK runtime
- No proprietary SoftDevice dependency

## Scope definition

Parity target here means:

1. Arduino-user-visible API parity for IO/peripheral workflows.
2. Board-level parity for XIAO pin mapping, battery path, RF switch control, upload/tool options.
3. BLE peripheral interoperability parity for common app flows.

It does not yet mean full Bluetooth Core conformance parity with mature controller+host stacks.

## Distinction from Zephyr-based implementation

This clean core differs from `lolren/NRF54L15-Arduino-core` by architecture and dependencies:

- Clean core: register-level, Arduino package only.
- Zephyr core: RTOS + Zephyr stack and runtime.

## Current parity matrix

Legend:

- `Done`: implemented and validated.
- `Partial`: implemented but with known behavior gaps.
- `Planned`: not implemented.

### Core API surface

| Area | Status | Notes |
|---|---|---|
| GPIO (`pinMode`, `digitalRead`, `digitalWrite`) | Done | Core digital API stable. |
| Interrupts (`attachInterrupt`/`detachInterrupt`) | Done | GPIOTE-backed `RISING/FALLING/CHANGE`. |
| ADC (`analogRead`, `analogReadResolution`) | Done | SAADC path integrated; compatibility API preserved. |
| PWM (`analogWrite`) | Done | Hardware PWM on supported pins with auto-stop behavior. |
| UART (`Serial`, `Serial1`, `Serial2`) | Done | USB/header routing options + compatibility alias `Serial2`. |
| SPI (`SPI`) | Done | Transactions + runtime frequency/mode/order behavior. |
| I2C (`Wire`, `Wire1`) | Done | Repeated-start + target/slave callbacks supported. |
| Timing (`millis`, `micros`, delays) | Done | Stable runtime timing primitives. |

### Peripheral HAL blocks

| HAL block | Status | Notes |
|---|---|---|
| Clock / GPIO / SPIM / TWIM / UARTE | Done | Core peripheral plumbing complete. |
| SAADC / TIMER / PWM / GPIOTE | Done | Sampling, timing, waveform, interrupts validated. |
| Power / GRTC / Temp / WDT / PDM | Done | Includes system-off and watchdog examples. |
| Board control (VBAT + RF switch) | Done | Divider-enable sampling and antenna route control. |

### Low-power parity

| Capability | Status | Notes |
|---|---|---|
| WFI idle profile | Done | Tools menu + examples available. |
| CPU freq selection (64/128 MHz) | Done | Tools menu integrated. |
| Peripheral auto-gating | Done | Tools menu policy + examples. |
| BLE TX power/timing profile controls | Done | Tools menu integrated. |
| Structured power measurement workflow | Done | `POWER_PROFILE_MEASUREMENTS.md`. |

### BLE parity (peripheral role)

| Capability | Status | Notes |
|---|---|---|
| Advertising / passive scan / active scan / connect | Done | Legacy ADV + passive scan + active scan (SCAN_REQ/SCAN_RSP) + connect paths validated. |
| GATT GAP/GATT/BAS + optional runtime custom services | Partial | Default GAP/GATT/BAS remains built-in; runtime 16-bit custom service/characteristic registration (read/write/notify/indicate + CCCD) is now available via `BleRadio` API. |
| ATT discovery/read/write subset | Partial | Core operations implemented and validated, including runtime custom 16-bit attributes. |
| Battery notifications / Service Changed indications | Done | CCCD flows validated. |
| LL control handling subset | Partial | Broad subset implemented; same-event response path now covers new LL control requests when TX freshness permits, and early-LL ACK progression is hardened for `FEATURE_RSP`/`VERSION_IND`/`LENGTH_RSP`/`PHY_RSP`/`PING_RSP`/`CONNECTION_PARAM_RSP`. Host interop edge cases still remain. |
| SMP legacy pairing flow | Partial | Request/confirm/random + LL encryption entry are implemented. On 2026-03-09, `BlePairingEncryptionStatus` on `hci0` reached `Paired: yes`, `Bonded: yes`, and `Encryption Change: Success`; remaining instability is now dominated by the Intel host-controller crash path and by intermittent `BleBondPersistenceProbe` failures. |
| Bond persistence | Partial | Record format + retention path + default flash-backed RRAM persistence implemented. Bonded state is retained on the host after successful pairing, but stable bonded reconnect validation is still blocked by Intel host-controller instability and by `BleBondPersistenceProbe` flakiness on `hci0`. |
| Zigbee / IEEE 802.15.4 support | Partial | `ZigbeeRadio` PHY/MAC-lite support now includes role-oriented coordinator/router/end-device join + app messaging examples (`ZigbeeCoordinator` / `ZigbeeEndDevice` / `ZigbeeRouter`) plus ping/pong examples, with RSSI-based `dist_cm`/`dist_mm` output. Full Zigbee stack layers (commissioning, NWK/APS/ZCL/security profiles) are not implemented. |
| Central role / multi-role | Planned | Not implemented. |
| Extended advertising / periodic advertising | Planned | Not implemented. |
| Channel sounding / AoA/AoD parity | Partial | Two-board phase-based channel sounding is now implemented in both core examples and the bundled HAL library (`BleChannelSoundingInitiator`/`BleChannelSoundingReflector`) using `RADIO.CSTONES`/DFE tone capture plus phase-slope distance estimation (`dist_m`, `median_m`, `residual`). Full Bluetooth CS/AoA/AoD Link Layer interoperability is still not implemented. |

## CLI validation status (hardware-tested)

Validated on 2026-02-24 with connected XIAO nRF54L15 + host BLE adapter using:

- `scripts/ble_cli_matrix.sh`
- `scripts/ble_pair_bond_regression.sh`
- Summary committed in repo: `docs/BLE_CLI_MATRIX_SUMMARY.md`

Current matrix summary (`ble_cli_matrix_post_enc_rsp`):

- Pass: advertising scan visibility, passive scanner serial output, connection peripheral, GATT basic peripheral, battery notify peripheral, connection timing metrics.
- Partial/fail: pairing and bond probe remain unstable across runs; success and failure outcomes both observed.
- Intermittent host-side `bluetoothctl info Connected` reporting gaps observed for one connectable-advertiser case despite successful connection attempts.

Additional runtime parity check (active scan):

- `measurements/active_scan_parity_20260223_230813`
- Observed `scan_rsp=1` and resolved scan-response local name (`scan_name=ATC_264EDA`).

Additional runtime parity check (custom runtime GATT registration):

- `measurements/custom_gatt_runtime_20260224_225041_connect`
  - Custom-advertiser endpoint (`X54-CUSTOM`) is discoverable and connect attempts reach `Connected: yes` before host-local LE abort in this run.
  - Full remote-attribute dump remains host-tooling-sensitive in this session; compile and on-target runtime path are verified.

Additional BLE-security regression evidence (post-fix):

- `measurements/ble_pair_bond_regression_20260224_064411`
  - 2/2 pair attempts reached `Paired: yes`; 1/2 reached `Bonded: yes`.
  - No target-side short-PDU termination observed in successful bonded attempt.
  - Intel host crash (`Hardware Error 0x0c`) still present in one attempt.
- `measurements/ble_pair_bond_regression_20260224_064748`
  - Single-attempt bond pass (`Paired: yes`, `Bonded: yes`) with host crash signature.
- `measurements/ble_pair_bond_regression_20260224_071104`
  - Pair-bond smoke run with new regression format (`target_verdict`, `overall_verdict`, `host_unstable`).
- `measurements/ble_pair_bond_regression_20260224_071149`
  - Bonded-reconnect smoke run with reconnect metrics (`reconnect_connected`, `reconnect_bonded`, `reconnect_enc_seen`).
- `measurements/ble_pair_bond_regression_20260224_212703`
  - `BlePairingEncryptionStatus` on Broadcom (`hci1`, sudo capture): `4/5` pair-bond pass, `1/5` host-inconclusive.
- `measurements/ble_pair_bond_regression_20260224_212347`
  - `BleBondPersistenceProbe` with clean bond storage still fails at encryption start (connection-timeout disconnect), so bonded parity remains partial.
- `measurements/ble_pair_bond_regression_20260224_224348`
  - `BlePairingEncryptionStatus` on Broadcom (`hci1`) produced `2/2` host-inconclusive attempts (`host_unstable=yes`), no target-crash signature.
- `measurements/ble_pair_bond_regression_20260224_224649`
  - `BlePairingEncryptionStatus` on the alternate adapter (`hci0`) produced `1/1 fail_target` (`Paired/Bonded: no`), reinforcing that security parity remains open.
- `measurements/ble_pair_bond_regression_20260309_pairstatus_post_startencrsp_gate_sudo`
  - `BlePairingEncryptionStatus` on `hci0`: `Paired: yes`, `Bonded: yes`, `Encryption Change: Success`.
  - Attempt verdict remains `inconclusive_host` because the Intel adapter reports `Hardware Error 0x0c` after the successful encryption transition.
- `measurements/ble_pair_bond_regression_20260309_pairstatus_bonded_reconnect_reset_sudo`
  - Bonded-reconnect validation on the same pair-status path retains `Paired: yes` / `Bonded: yes` in reconnect-phase host info, but the actual reconnect attempt is still host-inconclusive (`Hardware Error 0x0c`, no `Connected: yes` observed).

Regression tooling now supports explicit host-vs-target classification and reconnect-mode probing:

- `scripts/ble_pair_bond_regression.sh --mode pair-bond`
- `scripts/ble_pair_bond_regression.sh --mode bonded-reconnect`
- optional controller selection for multi-adapter hosts:
  - `--controller <controller_addr>`
  - `--btmon-iface <hciX>`

## Known BLE security gap (current blocker)

Packet/trace evidence shows:

- SMP `Pairing Request/Confirm/Random` exchange now reaches `LE Start Encryption` and `Encryption Change: Success` on the clean stack.
- The old `LL_FEATURE_REQ`/`0x3e` deadlock and the later `LL_START_ENC_RSP` ciphertext-as-control bug are fixed on the passing pair-status path.
- Remaining instability is now concentrated in two places:
  - Intel host-controller crashes (`Hardware Error 0x0c`) that turn successful pair/bond runs into `inconclusive_host`.
  - intermittent `BleBondPersistenceProbe` failures on `hci0`, which still prevent a clean bonded-reconnect proof on that example path.

This keeps SMP/bond parity in `Partial` state.

## Practical summary

- Non-BLE peripherals, IO, timing, interrupts, watchdog, and low-power controls are close to production Arduino usage.
- BLE peripheral interoperability is strong for advertise/connect/discover/notify use-cases.
- BLE security/bonding remains the primary parity gap before feature parity can be considered complete.
