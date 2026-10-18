# PlatformIO

Native PlatformIO support uses this core's Arduino implementation, not the
nRF52 platform or a Zephyr wrapper. It includes all six board definitions,
the bundled libraries, compiler settings, upload adapters, and debugger setup.
Arduino IDE and Arduino CLI are not prerequisites.

This integration is included in `1.0.21` and later. Existing releases up to
`1.0.20` do not contain it. Follow the
[README quick start](../README.md#platformio-quick-start) for Git installation.
Arduino Boards Manager archives use a different package format.

## Start A Project

Install [PlatformIO IDE](https://docs.platformio.org/en/latest/integration/ide/vscode.html)
or [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)
6.1.18 or newer. The first build downloads the matching ARM GCC 7.2.1
toolchain. This matches the Arduino core's declared compiler version.
Debug builds additionally download an ARM GDB 8.3.1 package for Cortex-M33
register support. Only its debugger is used; firmware still compiles with
GCC 7.2.1.

For a local checkout, create a project containing `platformio.ini` and
`src/main.cpp`. Point `platform` at the **repository root**, not the inner
Arduino hardware folder:

```ini
[env:xiao]
platform = symlink:///absolute/path/to/nrf54-arduino-core
board = xiao_nrf54l15
framework = arduino
monitor_speed = 115200
```

On Windows use a forward-slash absolute path, for example
`symlink://C:/src/nrf54-arduino-core`. For Git checkouts, enable Git symlinks
before cloning (`git config --global core.symlinks true`) and enable Windows
Developer Mode; upstream headers contain
tracked symlinks. Release packages dereference these links and do not need
that setup.

```cpp
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println(millis());
  delay(500);
}
```

```bash
pio run
pio run -t upload
pio device monitor
```

The checkout includes complete projects in
[`examples/platformio/blink`](../examples/platformio/blink) and
[`examples/platformio/ble_uart`](../examples/platformio/ble_uart).
Their relative `symlink://../../..` setting uses the surrounding checkout.
Run `pio run -d examples/platformio/blink -e xiao_nrf54lm20a` from the
repository root to build the LM20A example.

A Git installation pinned to this release can use:

```ini
platform = https://github.com/lolren/nrf54-arduino-core.git#v1.0.22
```

Use `#main` to follow development, another `#<commit-or-tag>` to pin a snapshot,
or the
complete `platform-nrf54l15clean-<version>-<hash>.tar.bz2` download URL from
the corresponding GitHub release. The release script creates a separate,
self-contained PlatformIO archive with the same framework bytes as the
Arduino archive. This platform is not currently published to the PlatformIO
registry; bare `platform = nrf54l15clean` is not an installation instruction.

## Board IDs

| Board | `board` |
|---|---|
| XIAO nRF54L15 / Sense | `xiao_nrf54l15` |
| XIAO nRF54LM20A / Sense | `xiao_nrf54lm20b` |
| HOLYIOT-25007 | `holyiot_25007_nrf54l15` |
| HOLYIOT-25008 | `holyiot_25008_nrf54l15` |
| Generic nRF54L15 36-pad module | `generic_nrf54l15_module_36pin` |
| Nordic PCA10156 nRF54L15 DK | `nrf54l15dk_pca10156` |

The `xiao_nrf54lm20b` ID is retained for compatibility; it selects the
**nRF54LM20A** core, register definitions, flash algorithm and memory layout.
It is not an assertion of support for a different LM20B chip.

## Arduino Menu Options

Options use the same names and values as `boards.txt`, prefixed with
`board_build.`. Defaults and valid values are resolved from that file on
every build, including the correct linker script and available RAM.

| Setting | Values | Availability/default |
|---|---|---|
| `board_build.cpu_freq` | `64m`, `128m` | All boards; `64m` |
| `board_build.clean_ble` | `on`, `off` | All boards; `on` |
| `board_build.clean_ble_trace` | `off`, `on` | All boards; `off` |
| `board_build.clean_vpr` | `on`, `off` | L15 boards; `on`. LM20A has no VPR menu |
| `board_build.clean_zigbee` | `off`, `on` | All boards; experimental, `off` |
| `board_build.clean_thread` | `off`, `stage` | All boards; experimental, `off` |
| `board_build.clean_matter` | `off`, `stage` | All boards; experimental, `off` |
| `board_build.clean_antenna` | `ceramic`, `external` | XIAO L15; `ceramic` |
| `board_build.clean_serial` | `auto`, `header`, `disabled` | XIAO L15; `auto` |
| `board_build.clean_serial` | `header`, `disabled` | HOLYIOT-25008; `header` |
| `board_build.clean_serial` | `header`, `altuart`, `disabled` | Nordic DK; `header` |

Unsupported menu choices fail rather than silently compiling another board
configuration. `board_build.f_cpu = 128000000L` is also accepted; conflicting
`f_cpu` and `cpu_freq` settings are rejected. Use these settings instead of
manually defining clock, SoC, VPR or protocol macros in `build_flags`.

For the Channel Sounding examples use `board_build.cpu_freq = 128m`.
For the staged Matter foundation examples set both `clean_thread = stage`
and `clean_matter = stage`. PlatformIO support does **not** complete or
certify BLE Channel Sounding, Thread, Matter, or Zigbee. The
[protocol matrices](../README.md#feature-matrix)
remain authoritative about implemented and missing functionality.

## Upload And Serial Monitoring

| `upload_protocol` | Transport |
|---|---|
| `auto` (default) | Bundled `nrf_ocd` on Linux/Windows x86-64; pyOCD on other hosts |
| `nrf_ocd` | Native CMSIS-DAP uploader on a supported host; selected UIDs use pyOCD |
| `pyocd` | CMSIS-DAP with the core's nRF54 flash algorithms |
| `pyocd_vm` | pyOCD safe-mode timings for VM/USB passthrough |
| `jlink` | Experimental J-Link transport through pyOCD; requires SEGGER drivers |
| `uf2` | Copy to an already-installed compatible UF2 bootloader |
| `custom` | Standard PlatformIO `upload_command` |

The stock XIAO upload path is CMSIS-DAP, **not** a USB bootloader. Generating
`firmware.uf2` does not install a bootloader. No 1200-baud serial touch is
performed. The native uploader is not an OpenOCD replacement for debugging;
use pyOCD for GDB.

With two boards, select each probe explicitly. The UID is the debugger's USB
serial number shown by `pyocd list`, not a BLE address:

```ini
[env:l15]
platform = symlink:///absolute/path/to/nrf54-arduino-core
framework = arduino
board = xiao_nrf54l15
upload_protocol = pyocd
board_upload.uid = YOUR_L15_PROBE_UID
monitor_port = /dev/serial/by-id/YOUR_L15_SERIAL_DEVICE
monitor_speed = 115200

[env:lm20a]
extends = env:l15
board = xiao_nrf54lm20b
board_upload.uid = YOUR_LM20A_PROBE_UID
monitor_port = /dev/serial/by-id/YOUR_LM20A_SERIAL_DEVICE
```

`upload_port` can also select a serial device that the upload helper maps to
its probe. For programming, `board_upload.uid` is less ambiguous. Set
`monitor_port` separately; changing a monitor port alone does not select a
programming probe. Windows ports use names such as `COM7`.

Explicit or serial-derived probe UIDs stay on pyOCD for upload, recovery and
reset. The bundled native uploader has a permissive unmatched-UID fallback;
the Python adapter deliberately bypasses it when an identity is selected.
An unmatched UID or an unmappable selected serial port fails instead of
programming a different board. Native automatic selection requires exactly
one probe. These guarantees describe the PlatformIO/Python adapter, not direct
invocation of the native executable.
An explicit UF2 upload never falls back to programming through a debug probe
if its bootloader drive is unavailable.

Advanced transport settings are available as `board_upload.pyocd_safe`
(`auto`, `true`, `false`), `board_upload.uf2_drive`,
`board_upload.uf2_labels` and `board_upload.uf2_timeout`. Additional helper
arguments can be supplied through the usual `upload_flags` option.

The upload adapter reuses this core's existing recovery logic. The first
pyOCD invocation can install the pinned Python dependencies privately within
the platform tools directory. Host USB permissions/drivers are still needed;
see the [upload troubleshooting](../README.md#troubleshooting).

## Debugging

```ini
debug_tool = pyocd
debug_init_break = tbreak setup
debug_speed = 1000
build_type = debug
```

Use the PlatformIO IDE debugger or `pio debug --interface=gdb -- -x .pioinit`
in an interactive terminal. `board_upload.uid` selects
the debug probe too. The LM20A target registration and flash algorithm are
loaded automatically. `debug_speed` is in kHz; `debug_port = localhost:3333`
is the GDB TCP endpoint, **not** a USB port or probe UID. A user-provided
`debug_server` overrides the supplied server configuration.

Debug attach disables automatic unlock/erase of a protected target. Explicit
firmware loading still programs flash. Halting a running BLE connection can
cause its peer to disconnect; use trace/serial diagnostics for timing-sensitive
radio behavior. J-Link debugging remains experimental.

## Libraries, Output And Build Safety

- Normal `.ino` sketches are supported, including prototype generation. In
  `main.cpp`, include `Arduino.h` and declare functions before use as usual.
- Bundled Bluefruit, HAL, Preferences, EEPROM and SPIFlash libraries are
  discovered without `lib_deps`. Core-owned Wire/SPI/Serial headers work
  directly. Third-party libraries can use normal PlatformIO `lib_deps`.
- The default library dependency finder is `deep`, so dependencies used only
  in implementation files (such as Thread's Preferences storage) are found.
  An explicitly selected `lib_ldf_mode` overrides this default; narrower
  scanning modes can require explicit project dependencies.
- `lib_archive = no` is enforced to match Arduino's direct library-object
  linking. This retains strong IRQ handlers that would otherwise be lost
  behind weak startup definitions. The board variant is linked directly too.
- Every compilation includes the version header and SoC guard, including
  assembly and library files. Cached objects from the wrong SoC fail to link.
- Builds produce `firmware.elf`, `.map`, `.hex`, `.bin` and `.uf2` inside
  `.pio/build/<environment>/`. Upload uses HEX to retain sparse address regions.
- `pio run -t uf2`, `pio run -t size`, `pio run -t compiledb` and
  `pio run -t clean` use native PlatformIO targets.
- VPR firmware is already embedded in generated headers. Ordinary projects
  do not need a RISC-V compiler or a separate VPR flashing step.

The Blink project also includes a Unity hardware smoke test for clock
configuration and delay/timekeeping:

```bash
pio test -d examples/platformio/blink -e xiao_nrf54l15 --without-uploading --without-testing
pio test -d examples/platformio/blink -e xiao_nrf54l15
```

The first command compiles only. The second programs the selected board and
reads test results from its serial port. With multiple probes set
`board_upload.uid`, and pass `--test-port` to select the matching serial port.

## Validation

Host-side checks cover board/menu parity, upload/debug command construction,
archive determinism and framework parity. The package verifier compiles the
real BLE HID mouse and Wire scanner for both chip families and verifies
strong variant/IRQ symbols, SoC guards, generated artifacts and UF2 family IDs.
An extended pass covers all six boards and the staged CS/Thread/Matter builds.

```bash
python3 scripts/test_platformio_integration.py
python3 scripts/test_platformio_transport.py
python3 scripts/verify_platformio_package.py --platform . --extended
```

The CI configuration includes Linux x86-64, Windows x86-64 and Intel macOS
builds. These jobs do not constitute hardware validation. Apple Silicon/native
ARM-host compiler availability is not claimed by this initial integration.

Local hardware validation on Linux (2026-10-04) used XIAO nRF54LM20A boards:

| Check | Result |
|---|---|
| Explicit-UID pyOCD upload through `pio run -t upload` | Passed |
| Wrong probe UID | Rejected without falling back to another probe |
| Serial monitor and Blink heartbeat | Passed |
| `pio test` Unity clock and delay checks | 2 tests passed unattended |
| `pio debug` breakpoint at `setup()` | Passed; backtrace and 64 MHz clock inspected |
| BLE UART discovery, serial-to-BLE and BLE-to-serial | Passed with Linux BlueZ/Bleak |

The BLE UART example advertises as `X54-PIO`; the LM20A antenna must be attached
for radio testing. The L15 family has compile coverage, but this PlatformIO
hardware pass did not include an L15, simultaneous two-probe selection, J-Link,
a UF2 bootloader, Windows/macOS hardware, or phone pairing. Those remain
separate acceptance checks; a successful PlatformIO build does not establish
protocol interoperability.
