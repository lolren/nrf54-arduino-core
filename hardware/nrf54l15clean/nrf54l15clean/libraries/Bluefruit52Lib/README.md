## Bluefruit52Lib Compatibility Layer

`Bluefruit52Lib` is the bundled nRF52/nRF52840 compatibility library for the
XIAO nRF54L15 clean core.

It keeps the familiar Bluefruit-style API available on top of the clean nRF54
HAL, including `Bluefruit`, `BLEUart`, `BLEClientUart`, scanner/client
helpers, Device Information, Battery Service, and common advertising helpers.

Examples in Arduino IDE:

- `File -> Examples -> Bluefruit52Lib -> Advertising`
- `File -> Examples -> Bluefruit52Lib -> Central`
- `File -> Examples -> Bluefruit52Lib -> Diagnostics`
- `File -> Examples -> Bluefruit52Lib -> DualRoles`
- `File -> Examples -> Bluefruit52Lib -> HID`
- `File -> Examples -> Bluefruit52Lib -> nRF52Compat`
- `File -> Examples -> Bluefruit52Lib -> Peripheral`
- `File -> Examples -> Bluefruit52Lib -> Projects`
- `File -> Examples -> Bluefruit52Lib -> Security`
- `File -> Examples -> Bluefruit52Lib -> Services`

`nRF52Compat` is the starter pack for direct sketch-porting examples:

- `central_bleuart`
- `central_scan`
- `central_notify`
- `dual_bleuart`
- `beacon`
- `custom_hrm`
- `notify_peripheral`
- `pairing_pin`

These are unchanged upstream-style sketches included locally because they are
known to compile on the nRF54 wrapper and give users concrete migration
starting points.

For the simplest custom notification flow, use `notify_peripheral` together
with `central_notify`.

The broader Bluefruit menus now ship the practical wrapper examples by role:

- `Advertising`: `adv_advanced`, `beacon`, `eddystone_url`
- `Central`: `central_bleuart_multi`, `central_custom_hrm`, `central_hid`, `central_pairing`, `central_scan_advanced`, `central_throughput`
- `Diagnostics`: `throughput`, `rssi_callback`, `rssi_poll`
- `DualRoles`: `dual_bleuart`
- `HID`: `blehid_keyboard`, `blehid_mouse`, `blehid_gamepad`, `blehid_camerashutter`
- `Projects`: `rssi_proximity_central`, `rssi_proximity_peripheral`
- `Security`: `pairing_passkey`, `pairing_pin`, `clearbonds`
- `Services`: `bleuart`, `bleuart_multi`, `custom_hrm`, `custom_htm`, `client_cts`, `ancs`

The supported surface is the shipped example set above. Common BLE UART,
scanner, custom notify, and central discovery flows are validated on the nRF54
wrapper and are the recommended starting point for nRF52 sketch ports.
