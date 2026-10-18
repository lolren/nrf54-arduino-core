# nRF54 Arduino Core 1.0.22

This patch addresses the macOS upload regression reported with Arduino IDE
1.8.19 in [issue #119](https://github.com/lolren/nrf54-arduino-core/issues/119).
It changes host-side upload selection only; firmware, peripherals and radio
behavior are unchanged.

## Upload Selection

- Add a macOS IOService registry fallback when the selected serial port cannot
  be mapped to its USB probe UID through Python serial-port metadata. This
  does not require pyserial in the Python interpreter running the uploader.
- Match the selected `/dev/cu.*` or `/dev/tty.*` device to its USB device's
  serial number, rather than guessing from a port-name suffix or choosing an
  unrelated connected board.
- Keep the existing fail-closed identity checks for upload and reset. An
  unmapped or ambiguous selection does not become permission to program
  another probe. Explicit probe UID selection remains supported; it is not
  required when the selected port maps successfully.

## Validation And Known Issues

Host-side regression coverage exercises macOS registry fixtures, selected-port
matching and rejection paths alongside the existing uploader and PlatformIO
transport tests. No macOS hardware was available for validation, so
[issue #119](https://github.com/lolren/nrf54-arduino-core/issues/119) remains
open for reporter retesting. This does not establish a fix for the separate
missing-serial-port report in
[issue #118](https://github.com/lolren/nrf54-arduino-core/issues/118).

## Install

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.22"
```

PlatformIO projects can pin `v1.0.22` or use the complete
`platform-nrf54l15clean-1.0.22-<hash>.tar.bz2` asset URL from this release.
See the [PlatformIO guide](PLATFORMIO.md).

[Changes since v1.0.21](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.21...v1.0.22)
