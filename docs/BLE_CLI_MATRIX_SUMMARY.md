# BLE CLI Matrix Summary

Date: 2026-02-23

Source run:

- `measurements/ble_cli_matrix_post_enc_rsp/report.md`
- Script: `scripts/ble_cli_matrix.sh`
- Hardware: XIAO nRF54L15 + host BLE adapter (`bluetoothctl`/`btmon`)

## Results

| Example | Mode | Compile | Upload | Scan | Connect | Info connected | Extra |
|---|---|---|---|---|---|---|---|
| `BleAdvertiser` | `scan_nonconn` | pass | pass | pass | skip | skip | skip |
| `BlePassiveScanner` | `serial` | pass | pass | skip | skip | skip | pass |
| `BleConnectableScannableAdvertiser` | `connect` | pass | pass | pass | pass | fail | skip |
| `BleConnectionPeripheral` | `connect` | pass | pass | pass | pass | pass | skip |
| `BleGattBasicPeripheral` | `gatt` | pass | pass | pass | pass | pass | pass |
| `BleBatteryNotifyPeripheral` | `notify` | pass | pass | pass | pass | pass | pass |
| `BlePairingEncryptionStatus` | `pair` | pass | pass | pass | pass | pass | fail |
| `BleBondPersistenceProbe` | `bond` | pass | pass | pass | pass | pass | fail |
| `BleConnectionTimingMetrics` | `connect` | pass | pass | pass | pass | pass | skip |

## Notes

- BLE advertise/scan/connect/GATT/notify paths are working in repeated CLI tests.
- Pair/bond flows are still partial: host reports `Paired: no` / `Bonded: no`.
- In additional interactive-agent tests, host reaches `LE Start Encryption`,
  but the flow still terminates before bonded completion (`AuthenticationFailed`/`AuthenticationCanceled`).
