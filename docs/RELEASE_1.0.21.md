# nRF54 Arduino Core 1.0.21

This release corrects nRF54LM20A SAADC oversampling and is the first release
with the native PlatformIO integration previously available only on `main`.

## Changes

- Correct LM20 SAADC burst configuration, based on
  [PR #117](https://github.com/lolren/nrf54-arduino-core/pull/117) by
  [@hannesb](https://github.com/hannesb). LM20 uses one global `BURST` register;
  L15 retains its per-channel `CH[n].CONFIG.BURST` setting. The correction
  avoids treating LM20's global register as a per-channel register array.
- Add executable SAADC oversampling regressions to host CI and release checks
  for both MCU families, plus a `Peripherals/SaadcOversamplingProbe` example
  covering channels 0, 1 and 7 with internal, external and differential inputs.
- Show the board as **XIAO nRF54LM20A / Sense**. The existing
  `xiao_nrf54lm20b` identifier remains unchanged for compatibility.
- Update the RADIO ownership regression to match the existing hardware-aware
  DMA pointer readback check, and exclude generated sketch UF2 exports from
  source packaging.
- Ship a separate, self-contained native PlatformIO archive for all six board
  targets, including the Arduino framework, bundled libraries, upload adapters,
  and debugger setup. Arduino IDE and Arduino CLI are not required for
  PlatformIO projects. See the [PlatformIO guide](PLATFORMIO.md).
- Retain the existing upload identity safeguards: explicit or serial-derived
  probe UIDs use pyOCD, unmatched selections fail without programming another
  board, and native automatic selection requires exactly one probe. Select
  `board_upload.uid` and `monitor_port` separately when using multiple boards.

## Validation And Known Issues

Executable host tests passed for L15, LM20A and LM20B register layouts, all
nine oversampling settings, all three configuration paths, channels 0/1/7,
and both burst settings. The probe compiles for both supported MCU families.

The connected XIAO nRF54LM20A passed three consecutive 36-case probe sweeps,
including bypass, 2x and 256x conversion. External inputs were unconnected;
these checks establish conversion completion, not analog accuracy or noise
performance. L15 hardware validation remains pending.

The shipped `XiaoLM20A_ImuWhoAmI` example returned `WHO_AM_I=0x6A` repeatedly
on the connected Sense board and its serial interface remained available after
upload from Linux. This does not reproduce the reporter's macOS environment.
The release workflow checks archive integrity and representative Arduino and
PlatformIO builds before publication.

The missing serial output reported in
[issue #118](https://github.com/lolren/nrf54-arduino-core/issues/118) remains
undiagnosed. The board-label clarification is not a serial fix. A new
[macOS USB bridge diagnostic guide](ISSUE_118_USB_BRIDGE_DIAGNOSTICS.md)
separates USB enumeration, CDC serial discovery and CMSIS-DAP probe detection.

## Install

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.21"
```

PlatformIO projects can pin the `v1.0.21` Git tag or the complete
`platform-nrf54l15clean-1.0.21-<hash>.tar.bz2` asset URL from this release.
The Arduino Boards Manager archive is a different package format. This release
does not publish the platform to the PlatformIO registry.

[Changes since v1.0.20](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.20...v1.0.21)
