## Bluefruit52Lib Compatibility Layer

`Bluefruit52Lib` is the bundled nRF52/nRF52840 compatibility library for the
XIAO nRF54L15 clean core.

It keeps the familiar Bluefruit-style API available on top of the clean nRF54
HAL, including `Bluefruit`, `BLEUart`, `BLEClientUart`, scanner/client
helpers, Device Information, Battery Service, and common advertising helpers.

Examples in Arduino IDE:

- `File -> Examples -> Bluefruit52Lib -> nRF52Compat`
- `File -> Examples -> Bluefruit52Lib -> Peripheral`

Curated compatibility examples shipped locally:

- `central_bleuart`
- `central_scan`
- `dual_bleuart`
- `beacon`
- `custom_hrm`
- `pairing_pin`

These are unchanged upstream-style sketches included locally because they are
known to compile on the nRF54 wrapper and give users concrete migration
starting points.
