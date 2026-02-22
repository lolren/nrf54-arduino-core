# Power Profile Measurements

Last updated: 2026-02-22

This document defines a repeatable measurement workflow for current consumption on
XIAO nRF54L15 with the `Nrf54L15-Clean-Implementation` core.

## 1. Measurement Objective

Compare average and peak current across Arduino Tools profiles and key sketches:

- CPU clock (`clean_cpu`)
- Power mode (`clean_power`)
- Peripheral auto-gating (`clean_autogate`)
- BLE enable (`clean_ble`)
- BLE TX power (`clean_ble_tx`)
- BLE timing profile (`clean_ble_timing`)

## 2. Required Equipment

- XIAO nRF54L15 board
- Current measurement instrument:
  - Preferred: Nordic PPK2, Joulescope, or Otii
- USB data cable
- Host with Arduino CLI

## 3. Wiring / Power Path

Use one of these methods:

1. `VBAT` rail method (recommended): power board through the measurement tool into `VBAT`.
2. `5V/VBUS` method: place meter in series with USB 5V path.

Keep the same method across all runs. Do not mix methods in one comparison table.

## 4. Build and Upload Commands

Base FQBN:

```text
nrf54l15clean:nrf54l15clean:xiao_nrf54l15
```

Example command (adjust sketch/options per row):

```bash
arduino-cli compile \
  --fqbn "nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_ble=off,clean_cpu=cpu64,clean_power=low,clean_autogate=aggressive" \
  hardware/nrf54l15clean/0.1.0/libraries/Nrf54L15-Clean-Implementation/examples/LowPowerIdleWfi

arduino-cli upload -p /dev/ttyACM0 \
  --fqbn "nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_ble=off,clean_cpu=cpu64,clean_power=low,clean_autogate=aggressive" \
  hardware/nrf54l15clean/0.1.0/libraries/Nrf54L15-Clean-Implementation/examples/LowPowerIdleWfi
```

## 5. Capture Procedure (Repeatable)

For each test row:

1. Flash sketch with exact FQBN options.
2. Let firmware warm up for 10 s.
3. Capture current for 60 s (minimum).
4. Export:
   - average current (mA)
   - 95th percentile current (mA)
   - peak current (mA)
5. Repeat 3 runs and compute arithmetic mean.

Keep constant:

- Same power source voltage
- Same BLE peer state (disconnected vs connected)
- Same serial monitor state (closed unless test explicitly needs it)
- Same ambient conditions

## 6. Software Duty-Metric Companion

For low-power sketches, run:

- `examples/LowPowerTelemetryDutyMetrics/LowPowerTelemetryDutyMetrics.ino`

Record active/sleep percentages in the same session window as the current capture.
This gives a software-side explanation for current changes across profile settings.

Capture and summarize:

```bash
arduino-cli monitor -p /dev/ttyACM0 --config baudrate=115200 > telemetry.log
python3 scripts/parse_power_telemetry.py --log telemetry.log
```

Automated BLE timing/TX-power sweep:

```bash
python3 scripts/ble_timing_sweep.py \
  --port /dev/ttyACM0 \
  --duration-s 20 \
  --runs 1 \
  --timing interop balanced aggressive \
  --tx n20 n8 p0 p8 \
  --require-windows
```

Outputs are written to `measurements/ble_timing_sweep_<timestamp>/`:

- `ble_timing_sweep_runs.csv` (run-level metrics)
- `ble_timing_sweep_summary.md` (aggregated comparison table)
- `raw_logs/*.log` (captured serial windows)

If no windows are captured, verify serial path and keep Tools serial routing at
`USB bridge on Serial` (`clean_serial=bridge`).

## 7. Measurement Matrix

Use this matrix and fill measured values from your instrument.
You can also fill `measurements/power_profiles_template.csv` directly.

| ID | Sketch | Tool Options | BLE Link State | Avg mA (run1/run2/run3) | Avg mA mean | P95 mA mean | Peak mA mean | Notes |
|---|---|---|---|---|---|---|---|---|
| P1 | `LowPowerIdleWfi` | `clean_cpu=cpu64,clean_power=low,clean_autogate=off,clean_ble=off` | N/A | TBD | TBD | TBD | TBD | Baseline idle |
| P2 | `LowPowerIdleWfi` | `clean_cpu=cpu64,clean_power=low,clean_autogate=aggressive,clean_ble=off` | N/A | TBD | TBD | TBD | TBD | Auto-gate impact |
| P3 | `LowPowerAutoGatePolicy` | `clean_cpu=cpu64,clean_power=low,clean_autogate=balanced,clean_ble=off` | N/A | TBD | TBD | TBD | TBD | SPI/TWI window workload |
| P4 | `LowPowerBleBeaconDutyCycle` | `clean_cpu=cpu64,clean_power=low,clean_ble=on,clean_ble_timing=balanced` | Advertising only | TBD | TBD | TBD | TBD | BLE burst mode |
| P5 | `BleConnectionPeripheral` | `clean_cpu=cpu64,clean_power=low,clean_ble=on,clean_ble_timing=interop` | Connected | TBD | TBD | TBD | TBD | Interop profile |
| P6 | `BleConnectionPeripheral` | `clean_cpu=cpu64,clean_power=low,clean_ble=on,clean_ble_timing=aggressive` | Connected | TBD | TBD | TBD | TBD | Aggressive BLE timing |
| P7 | `BleConnectionPeripheral` | `clean_cpu=cpu64,clean_power=low,clean_ble=on,clean_ble_timing=balanced,clean_ble_tx=n20` | Connected | TBD | TBD | TBD | TBD | TX power reduction impact |

## 8. Interpretation Rules

- Compare only rows with same BLE state and similar application behavior.
- For BLE connected rows, keep connection interval/latency comparable at the peer.
- Expect `clean_ble_timing=aggressive` to lower active time but potentially reduce
  margin against poor clock accuracy peers.

## 9. Current Status

- Measurement workflow: implemented.
- Tools-menu profile knobs for power tuning: implemented.
- Bench current captures with dedicated current instrument: pending user lab runs.
