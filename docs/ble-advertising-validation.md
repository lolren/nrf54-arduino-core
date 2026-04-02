# BLE Advertising Validation

This note records the local hardware validation done on the XIAO nRF54L15 for the
current raw Arduino BLE path in this core.

Validated environment:

- board: XIAO nRF54L15
- build mode: secure single image
- power profile: `Low Power (WFI Idle)`
- RF path: `BoardControl::setRfSwitchPowerEnabled(true)` + `BoardControl::setAntennaPath(BoardAntennaPath::kCeramic)`
- date: 2026-03-07
- scanner used for validation: local Linux `bluetoothctl` and nRF Connect

## What Worked

The only continuously discoverable advertiser validated end-to-end was:

- `BleRadio::begin(-10)`
- default FICR-derived BLE address
- `BleAdvPduType::kAdvInd`
- `setAdvertisingName(...)`
- no manual `ClockControl::stopHfxo()` / `startHfxo()` around events
- `3000 ms` intervals between `advertiseEvent(...)` calls
- `clean_power=low`
- core low-power GRTC tick path fixed to use the correct secure IRQ/vector/channel state

That is the configuration used by:

- `examples/BLE/Advertising/BleAdvertiserProbe/BleAdvertiserProbe.ino`
- `examples/BLE/AdvertisingLowPower/BleAdvertiserLowestPowerContinuous/BleAdvertiserLowestPowerContinuous.ino`

The following follow-on examples are intended to push board-level current lower:

- `examples/BLE/AdvertisingLowPower/BleAdvertiserRfSwitchDutyCycle/BleAdvertiserRfSwitchDutyCycle.ino`
  - Uses the same validated advertiser settings, but powers the RF switch only for the duration of each synchronous `advertiseEvent(...)`.
  - Leaves the RF switch control pin high-impedance and its supply disabled while idle.
- `examples/BLE/AdvertisingLowPower/BleAdvertiserBurstSystemOff/BleAdvertiserBurstSystemOff.ino`
  - Boots, emits a short advertising burst, then enters timed `SYSTEM OFF`.
  - This is burst beaconing, not continuous advertising.
- `examples/BLE/AdvertisingLowPower/BleAdvertiserPhoneBeacon15s/BleAdvertiserPhoneBeacon15s.ino`
  - Uses `ADV_NONCONN_IND`, keeps the short name in the primary advertising payload, emits a longer wake burst, then returns to timed `SYSTEM OFF`.
  - This is tuned for low average current with better scanner catch probability than the older short-burst system-off pattern.

Validated follow-on configurations:

- `BleAdvertiserRfSwitchDutyCycle`
  - name `X54-RF-GATE`
  - `ADV_IND`
  - default FICR-derived address
  - `-10 dBm`
  - `3000 ms` interval
  - `clean_power=low`
  - RF switch enabled only during each synchronous advertising event, then set to high-impedance + power off while idle
- `BleAdvertiserBurstSystemOff`
  - name `X54-BURST-OFF`
  - `ADV_IND`
  - default FICR-derived address
  - `-10 dBm`
  - `6` advertising events per boot with `20 ms` gaps
  - `1000 ms` timed `SYSTEM OFF` interval
  - validated with `clean_power=low` and the default boot path
- `BleAdvertiserPhoneBeacon15s`
  - name `X54-15S`
  - `ADV_NONCONN_IND`
  - primary advertising payload only, no scan response dependence
  - default FICR-derived address
  - `0 dBm`
  - `14` advertising events per boot with `70 ms` gaps
  - `14000 ms` timed `SYSTEM OFF` interval
  - validated as clustered detections on the peer scan rig with `clean_power=low`
- `BleAdvertiserHybridDutyCycle`
  - name `X54-HYBRID`
  - `ADV_IND`
  - default FICR-derived address
  - `-10 dBm`
  - `2` events per burst with `20 ms` gaps
  - `10000 ms` burst period
  - validated with `clean_power=low` and the default boot path

## What Did Not Validate

The following combinations were flashed and were not discoverable in local scans:

- `setDeviceAddress(...)`
- early sparse `BleAdvPduType::kAdvNonConnInd` attempts that still relied on the older short-burst timing model
- `BleRadio::begin(-12)` and below at long intervals
- manual `ClockControl::stopHfxo()` / `startHfxo()` between advertising events
- `4000 ms` at `-10 dBm` did not validate in the same practical scan window
- early `BleAdvertiserBurstSystemOff` baseline with `3` events per wake and `3000 ms` off-time

## Practical Consequence

This Arduino BLE implementation is not yet equivalent to Zephyr's BLE controller
path for low-power advertising. The current implementation manually emits legacy
advertising events from sketch code. Zephyr schedules controller-side advertising
with different timing, wake, and RF state handling.

If Zephyr-like low-power advertising is the target, the next engineering work is
not another sketch tweak. The next work is to fix the BLE core paths above or to
port a proper controller implementation.

One board-specific lever that is safe in this core is RF switch duty-cycling:

- `BleRadio::advertiseEvent(...)` is synchronous and TX-only in the current raw advertiser path.
- Because of that, the XIAO RF switch can be enabled just before the call and collapsed immediately after it returns.
- That trick does not make the BLE controller equivalent to Zephyr, but it does let testing isolate the board-level RF switch quiescent current from radio-idle current.
