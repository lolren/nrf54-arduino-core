# Feature Parity: Nrf54L15-Clean-Implementation

Last updated: 2026-02-24

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
| GATT GAP/GATT/BAS minimal server | Partial | Service set is intentionally minimal/static. |
| ATT discovery/read/write subset | Partial | Core operations implemented and validated. |
| Battery notifications / Service Changed indications | Done | CCCD flows validated. |
| LL control handling subset | Partial | Broad subset implemented; full edge-case matrix pending. |
| SMP legacy pairing flow | Partial | Request/confirm/random + LL encryption entry are implemented. Pair/bond now succeeds more often in regression runs, but remains non-deterministic across repeated host runs. |
| Bond persistence | Partial | Record format + retention path implemented; durable power-cycle persistence and stable bonded reconnect still pending. |
| Central role / multi-role | Planned | Not implemented. |
| Extended advertising / periodic advertising | Planned | Not implemented. |
| Channel sounding / AoA/AoD parity | Planned | Not implemented; tracked as advanced PHY work. |

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

Additional BLE-security regression evidence (post-fix):

- `measurements/ble_pair_bond_regression_20260224_064411`
  - 2/2 pair attempts reached `Paired: yes`; 1/2 reached `Bonded: yes`.
  - No target-side short-PDU termination observed in successful bonded attempt.
  - Intel host crash (`Hardware Error 0x0c`) still present in one attempt.
- `measurements/ble_pair_bond_regression_20260224_064748`
  - Single-attempt bond pass (`Paired: yes`, `Bonded: yes`) with host crash signature.

## Known BLE security gap (current blocker)

Packet/trace evidence shows:

- SMP `Pairing Request/Confirm/Random` exchange occurs.
- LL encryption procedure entry remains partial under host interop:
  some runs complete to `Paired: yes` / `Bonded: yes`, while others fail with auth timeout or host-controller instability.
- A frequent target-side failure mode (`ENC_RX_SHORT_PDU` immediately after encryption enable) was reduced by transition-window hardening, but deterministic pairing is not yet achieved.
- Interactive agent-led tests can progress to host `LE Start Encryption`,
  but still show intermittent MIC/auth failures and Intel host-controller crash artifacts in repeatable runs.

This keeps SMP/bond parity in `Partial` state.

## Practical summary

- Non-BLE peripherals, IO, timing, interrupts, watchdog, and low-power controls are close to production Arduino usage.
- BLE peripheral interoperability is strong for advertise/connect/discover/notify use-cases.
- BLE security/bonding remains the primary parity gap before feature parity can be considered complete.
