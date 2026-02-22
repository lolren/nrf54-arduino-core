# Feature Parity: Nrf54L15-Clean-Implementation

Last updated: 2026-02-22

This document tracks parity against the practical feature surface expected from a production Arduino core for XIAO nRF54L15, while keeping this project strictly clean and OSS:

- No Zephyr runtime
- No nRF Connect SDK runtime
- No proprietary Nordic SoftDevice dependency
- Register-level HAL only

## 1. Scope Definition

Parity target in this document means:

1. Arduino-user-visible behavior parity for core APIs and board features.
2. Peripheral functionality parity for common embedded workflows (IO, ADC, SPI, I2C, UART, timers, interrupts, low power).
3. BLE peripheral interoperability parity for mainstream phone/app interactions (connect, discover, read, write CCCD, notify/indicate).

It does **not** currently mean full Bluetooth Core conformance parity with a mature full-stack controller+host implementation.

## 2. Distinction From Zephyr-Based Core

This repository is intentionally different from `lolren/NRF54L15-Arduino-core` (Zephyr-based):

- Architecture:
  - Zephyr-based core: RTOS + Zephyr drivers/network stack.
  - Clean core: direct register programming with Arduino-style polling/service loops.
- Dependencies:
  - Zephyr-based core: requires Zephyr/NCS toolchain/runtime components.
  - Clean core: Arduino core package + GCC + uploader tools only.
- BLE model:
  - Zephyr-based core: Zephyr host/controller stack.
  - Clean core: minimal custom BLE LL + ATT/GATT path focused on peripheral interoperability.

## 3. Current Parity Matrix

Legend:
- `Done`: implemented and integrated.
- `Partial`: implemented subset; known limitations remain.
- `Planned`: not yet implemented.

### 3.1 Core / Arduino API Surface

| Area | Status | Notes |
|---|---|---|
| Digital IO (`pinMode`, `digitalRead`, `digitalWrite`) | Done | GPIO config/read/write/toggle available. |
| ADC (`analogRead`) | Done | SAADC single-ended path + mV conversion helpers. |
| PWM (`analogWrite`) | Done | Hardware PWM on XIAO PWM-capable pins; auto-stop behavior integrated. |
| External interrupts (`attachInterrupt`) | Done | GPIOTE IRQ-backed `RISING`/`FALLING`/`CHANGE`. |
| UART (`Serial`) | Done | UARTE path with USB bridge/header routing options. |
| SPI (`SPI`) | Done | SPIM EasyDMA transfers. |
| I2C master (`Wire`) | Done | TWIM with repeated-start support. |
| I2C target/slave (`Wire.begin(address)`) | Done | TWIS IRQ path with `onReceive`/`onRequest`. |
| Timing (`millis`, `micros`, delays) | Done | Core runtime integrated. |

### 3.2 Peripheral HAL Parity

| HAL Block | Status | Notes |
|---|---|---|
| GPIO | Done | Direction, pull, output drive, reads/writes. |
| CLOCK wrapper | Done | Runtime-managed no-op behavior documented. |
| SPIM | Done | Master transfer path validated. |
| TWIM/TWIS | Done | Master + target mode available. |
| UARTE | Done | DMA TX/RX path. |
| SAADC | Done | Sampling + scaling helpers. |
| TIMER | Done | Compare/shortcuts/callback service. |
| PWM | Done | Single-output control + duty/freq update. |
| GPIOTE | Done | Channel event/task service + IRQ integration. |
| POWER | Done | Latency mode, reset reason, DCDC, GPREGRET retention. |
| GRTC | Done | Counter + compare scheduling, wake-oriented usage. |
| TEMP | Done | On-die temperature sample API. |
| WDT | Done | Configure/start/feed/status path. |
| PDM | Done | Blocking capture over EasyDMA. |

### 3.3 Low-Power Parity

| Capability | Status | Notes |
|---|---|---|
| System ON idle via `WFI/WFE` | Done | Low-power examples and power profile option included. |
| CPU frequency menu (64/128 MHz) | Done | Arduino Tools menu option. |
| Peripheral duty-cycling patterns | Done | Examples for ADC/SPI/I2C gating. |
| System OFF wake (button) | Done | Example uses GPIO detect + reset reason. |
| System OFF wake (timer/GRTC) | Done | Example uses compare wake path. |

### 3.4 BLE Parity (Peripheral Role)

| Capability | Status | Notes |
|---|---|---|
| Legacy advertising (37/38/39) | Done | Custom payload + naming helpers. |
| Passive scan | Done | RSSI + payload parse helpers. |
| Scannable/connectable ADV interaction | Done | `SCAN_REQ`/`SCAN_RSP`/`CONNECT_IND` handling. |
| Legacy connection bring-up | Done | Data-channel event scheduling active. |
| LL control subset responses | Partial | Includes feature/version/length/phy/ping/param handling, unknown fallback, and explicit response/indication handling for newer known opcodes. |
| LL malformed control PDU handling | Partial | Strict opcode-length validation for handled procedures, reject responses for malformed requests, and consistent unsupported-feature rejects for selected unsupported requests. |
| LL instant validation | Partial | Connection update + channel map instant checks implemented. |
| Retransmission gating safety | Partial | New payload consume now gated by TX ACK state. |
| ATT MTU exchange | Done | Basic negotiation path. |
| ATT discovery/read family | Partial | Includes Find Info, Read By Type, Read By Group Type, Read, Read Blob, Read Multiple, Find By Type Value, with stricter boundary-handle validation. |
| ATT queued writes (selected attrs) | Partial | Prepare/Execute Write implemented for selected writable CCCD attributes. |
| GATT services (GAP/GATT/BAS) | Partial | Minimal static database. |
| Service Changed indication flow | Done | CCCD write + indication + confirmation handling. |
| Battery Level notify flow | Done | CCCD write + notification TX when value changes. |
| L2CAP signaling fallback | Partial | Command reject with granular reasons, conn param update reject, and LE CoC request deterministic response in peripheral-only model. |
| SMP handling | Partial | Deterministic Pairing Failed (Not Supported) fallback. |

## 4. Gaps vs Full Stack Behavior

These are the major items not yet at full-stack parity:

1. Full LL procedure matrix and edge-case state machine coverage.
2. BLE security completion (pairing/encryption/key distribution/privacy).
3. Full L2CAP signaling coverage beyond current fallback subset.
4. Dynamic GATT database model (currently static minimal server model).
5. Central role and multi-role BLE behavior.
6. Extended advertising / periodic advertising / coded PHY feature set.

## 5. Validation Status

Validated in Arduino CLI with the clean core package (`nrf54l15clean:nrf54l15clean:xiao_nrf54l15`) on 2026-02-22:

- BLE examples compile and upload (`pyOCD`) to connected XIAO nRF54L15.
- Low-power/non-BLE examples compile across BLE-on/BLE-off build options.
- Tools menu options for CPU, power, BLE, antenna, serial routing, and uploader are active.

## 6. Practical Parity Summary

For non-BLE peripherals and low-power control, parity is close to production-ready Arduino usage.

For BLE peripheral use-cases, the clean core now supports practical app-level workflows (connect/discover/read/write CCCD/notify/indicate) but is still intentionally narrower than a complete Zephyr-class Bluetooth stack.
