# <picture><source media="(prefers-color-scheme: dark)"><img align="right" width="80" src="https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/docs/xiao_nrf54l15_default_pin_routes.png"></picture> nRF54L Arduino Core

<div align="center">

**Bare-metal Cortex-M33 + RISC-V development with the Arduino API. No Zephyr
runtime or external nRF Connect SDK installation.**

[![Release](https://img.shields.io/github/v/release/lolren/nrf54-arduino-core?color=00d4ff&label=latest)](https://github.com/lolren/nrf54-arduino-core/releases)
[![Boards](https://img.shields.io/badge/board_targets-6-00d4ff)](#supported-boards)
[![License](https://img.shields.io/badge/license-MIT%20%2B%20third--party-00d4ff)](LICENSE)

*A register-level Arduino core for Nordic's nRF54L family, with a mature
peripheral surface, a practical Bluetooth LE stack, low-power board support,
and direct access to the VPR RISC-V coprocessor.*

</div>

---

## Project Scope

The `1.0.x` line focuses on a dependable Arduino, peripheral, power-management,
and Bluetooth LE experience on supported nRF54L boards. The core is suitable
for real BLE prototyping and embedded applications within the documented
single-link security/privacy scope. Release-critical feature probes are compiled
from the exact packaged archive, the source checkout receives broader example
coverage, and the release gate exercises both a XIAO nRF54L15 and a XIAO
nRF54LM20A.

The following protocol work is included for evaluation, but is **not finished
or production-ready**:

| Area | Current boundary |
|---|---|
| **Zigbee** | Experimental partial stack and device demonstrations; incomplete Zigbee PRO coverage, routing, clusters, OTA, and ecosystem interoperability |
| **Thread** | Staged OpenThread FTD/MeshCoP/SRP/UDP with settings recovery, commissioner/joiner, sleepy-child, and mixed-board test paths; certification and broader interoperability remain |
| **Matter** | Staged system, crypto, packet-buffer, IPv6/UDP, ACL, onboarding, and custom PASE/CASE demo work; not an upstream wire-compatible or certifiable Matter device stack |
| **Channel Sounding** | Experimental two-board controller-backed LE CS Test; not connected-ACL Channel Sounding, calibrated ranging, cross-vendor interoperability, or a qualification claim |

The Thread and Matter entries describe the experimental platform work included
in `1.0.8`; those menu targets remain explicitly outside the stable feature
claim.

These boundaries keep the stable claims precise: unfinished protocol examples
are useful engineering work, but they are not presented as complete standards
implementations.

## Why Use This Core?

| Capability | Practical benefit |
|---|---|
| **Arduino workflow** | Install from Boards Manager, select a board, compile normal `.ino` sketches, and upload over a connected CMSIS-DAP probe |
| **No Zephyr runtime** | Shorter build cycles, smaller applications, and fewer framework layers between a sketch and the hardware |
| **Direct peripheral access** | GPIO, ADC, PWM, serial buses, audio, NFC, DPPI, GRTC, watchdog, comparators, and power controls are implemented against nRF54L hardware |
| **Useful BLE depth** | Peripheral, central, dual-role, GATT server/client, HID, BLE UART, PHY/DLE/MTU control, LE Secure Connections, bonding, OOB, Numeric Comparison, privacy/RPA, and CSRK signed writes |
| **Low-power board integration** | System ON idle, timed System OFF, reset-cause APIs, XIAO RF-switch control, LM20A external-flash power-down, and nPM1300 hibernate support |
| **Dual-core access** | Use the 128 MHz Cortex-M33 application CPU and the VPR RISC-V coprocessor without adopting a vendor RTOS |
| **Observable validation** | Public diagnostics, regression scripts, release-archive compilation, and a repeatable two-board hardware gate document what was actually tested |
| **Cross-board build isolation** | L15 and LM20A objects carry linker-checked SoC identities, and Arduino automatically clears stale objects when the selected board or installed platform changes |

This core is a good fit when Arduino productivity and close control over timing,
memory, radio behavior, or power matter more than adopting the full nRF Connect
SDK application model. For a product that requires a Bluetooth, Matter, Thread,
or Zigbee qualification program, treat this core as engineering source and
validate the complete product against the relevant conformance suite.

## Contents

- [Quick install](#quick-install)
- [Getting started](#getting-started)
- [Supported boards](#supported-boards)
- [Feature matrix](#feature-matrix)
- [Bluetooth LE](#bluetooth-le)
- [Power consumption](#power-consumption)
- [SPI and QSPI](#spi-speed-and-routing)
- [System OFF and PMIC APIs](#timed-system-off-apis)
- [Maturity and limitations](#stack-maturity)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## Quick Install

```text
https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/package_nrf54l15clean_index.json
```

Add this URL in **Arduino IDE → Preferences → Additional Boards Manager URLs**, then install **nRF54L15 Boards** from the Boards Manager.

Normal uploads use the bundled native [**nRF OCD**](https://github.com/lolren/open-nrf-ocd) tool on Linux and Windows, so Windows does not need a separate Python install just to upload. If native upload fails, switch **Tools -> Upload Method** to **pyOCD Recovery**; its first setup installs pinned dependencies into the packaged tool-local runtime and requires access to the configured Python package index. **J-Link via pyOCD (Experimental)** is also available after installing SEGGER's official J-Link driver package; this path has software-only coverage and still needs user hardware validation.

### Arduino CLI Install And Updates

Arduino IDE users can update through **Tools -> Board -> Boards Manager** by searching for **nRF54L15 Boards** and clicking **Update** when a newer release is available.

Linux/macOS:

```bash
BOARD_URL="https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/package_nrf54l15clean_index.json"

arduino-cli config add board_manager.additional_urls "$BOARD_URL"
arduino-cli core update-index
arduino-cli core install nrf54l15clean:nrf54l15clean

# Check whether this core has an available update.
arduino-cli core list --updatable

# Update this core to the latest Board Manager release.
arduino-cli core upgrade nrf54l15clean:nrf54l15clean
```

Windows PowerShell:

```powershell
$BoardUrl = "https://raw.githubusercontent.com/lolren/nrf54-arduino-core/main/package_nrf54l15clean_index.json"

arduino-cli config add board_manager.additional_urls $BoardUrl
arduino-cli core update-index
arduino-cli core install nrf54l15clean:nrf54l15clean

# Check whether this core has an available update.
arduino-cli core list --updatable

# Update this core to the latest Board Manager release.
arduino-cli core upgrade nrf54l15clean:nrf54l15clean
```

`arduino-cli outdated` is also useful when you want to see all updatable cores and libraries. If `core list --updatable` prints nothing for `nrf54l15clean:nrf54l15clean`, the installed core is already current.

---

## Getting Started

### Arduino IDE

1. Install the package with the Boards Manager URL above.
2. Connect the board and select **Tools -> Board -> nRF54L15 Boards**, then
   choose **XIAO nRF54L15 / Sense** or **XIAO nRF54LM20A**.
3. Keep **CPU Frequency -> 64 MHz** for normal sketches. Use 128 MHz only when
   an example explicitly requires it, including the Channel Sounding pair.
4. Start with one of the examples below and click **Upload**. The default native
   nRF OCD uploader selects the connected CMSIS-DAP probe; the recovery uploader
   is available from **Tools -> Upload Method**. Select **J-Link via pyOCD
   (Experimental)** only when using an onboard or standalone SEGGER J-Link.
5. Open the Serial Monitor at the baud rate selected by the sketch. If serial
   output is missing, verify **Tools -> Serial Routing** and close other programs
   that own the port.

On the XIAO nRF54L15, the default **USB bridge when powered** route keeps the
SAMD11 bridge pins high-impedance until the host opens the serial bridge. This
allows BLE sketches to boot safely from battery; choose **Header UART** when a
physical D6/D7 UART is required.

Recommended first examples:

| Goal | Arduino example |
|---|---|
| Verify the board and core | `Nrf54L15-Clean-Implementation > Diagnostics > CleanBringUp` |
| Create a phone-visible BLE UART peripheral | `Bluefruit52Lib > Peripheral > bleuart` |
| Scan and connect as a BLE central | `Bluefruit52Lib > Central > central_bleuart` |
| Exercise authenticated pairing | `Bluefruit52Lib > Security > pairing_numeric_comparison` |
| Enter and recover from timed System OFF | `Nrf54L15-Clean-Implementation > LowPower > LowPowerGrtcPwmSystemOff` |
| Inspect LM20A external flash | `nRF54 Board Examples > XIAO-nRF54LM20A > QspiFlashInfo` |
| Read the LM20A Sense microphone | `nRF54 Board Examples > XIAO-nRF54LM20A-Sense > XiaoLM20A_MicLevel` |

Hardware-specific sketches are grouped under `File > Examples > nRF54 Board Examples`.
The `XIAO-nRF54L15-Sense` and `XIAO-nRF54LM20A-Sense` submenus
contain the matching onboard IMU and microphone sketches; base-board flash,
RGB, and control examples remain in their XIAO submenu, while HOLYIOT-25008
and the Nordic nRF54L15 DK have separate submenus.

### Arduino CLI

The board identifiers deliberately retain the original internal LM20B name for
package compatibility:

```bash
# XIAO nRF54L15 / Sense
arduino-cli compile \
  --fqbn "nrf54l15clean:nrf54l15clean:xiao_nrf54l15" \
  "$HOME/Arduino/MySketch"

# XIAO nRF54LM20A / Sense
arduino-cli compile \
  --fqbn "nrf54l15clean:nrf54l15clean:xiao_nrf54lm20b" \
  "$HOME/Arduino/MySketch"

# The core can identify a single attached probe automatically.
arduino-cli upload \
  --fqbn "nrf54l15clean:nrf54l15clean:xiao_nrf54l15" \
  "$HOME/Arduino/MySketch"
```

When two boards are attached, select the target explicitly with the port or UID
reported by `arduino-cli board list` or `pyocd list`. Disable experimental
Thread, Matter, or Zigbee build options unless the sketch actually uses them;
this reduces compile/link surface and makes the intended runtime clear.

### Experimental J-Link upload

Install the official [SEGGER J-Link Software and Documentation
Pack](https://www.segger.com/downloads/jlink/) first, reconnect the debugger,
and confirm that `pyocd list --probes` reports a J-Link. Then select **Tools ->
Upload Method -> J-Link via pyOCD (Experimental)**. The Nordic nRF54L15 DK's
onboard J-Link and a standalone J-Link are both intended to work.

If a standalone J-Link does not expose a serial/VCOM port, select **Tools ->
Programmer -> J-Link via pyOCD (Experimental)** and use **Sketch -> Upload Using
Programmer**. This programmer route does not require a fabricated serial port.
The equivalent CLI invocation is:

```bash
arduino-cli upload -b nrf54l15clean:nrf54l15clean:nrf54l15dk_pca10156 \
  -P jlink /path/to/sketch
```

When multiple J-Links are connected, set `NRF54L15_JLINK_UID` to the intended
SEGGER serial number. The uploader refuses interactive or cross-transport
auto-selection in that situation.

This mode restricts pyOCD discovery to the J-Link plug-in, leaves target-power
control disabled, and does not fall back to another attached debug probe. It
uses pyOCD's nRF54 target support rather than adding the proprietary J-Link
transport to `open-nrf-ocd`. It has not yet been flashed on maintainer J-Link
hardware, so please report the J-Link model, operating system, and complete
upload output if it fails.

### Sketch Compatibility

Normal Arduino APIs such as `pinMode`, `digitalWrite`, `analogRead`,
`analogWrite`, `Wire`, `SPI`, and `Serial` are available. BLE sketches should
use the bundled `Bluefruit52Lib` compatibility API or the lower-level clean-core
BLE interfaces shown in the packaged examples. `ArduinoBLE` is not the native
BLE library for this core.

Board-specific hardware still matters. The Sense IMU is on `Wire1`; the LM20A
external header SPI and onboard QSPI flash use different controllers; and the
L15 antenna selection controls an external RF switch. Consult the
[board reference](docs/board-reference.md) before using fixed-function pins or
direct register access.

---

## Current BLE Highlights

- LE Secure Connections security now includes asynchronous Numeric Comparison
  accept/reject handling, request-scoped keyboard/passkey input, explicit
  bonding/MITM/SC/key-size policy, and OOB records supplied mutually or in
  either one-way direction.
- SMP security now reduces encryption keys to the negotiated 7-16 octet size,
  enforces the 30-second transaction timeout with single-peer repeated-attempt
  throttling, and aborts without deterministic fallback if CRACEN entropy is
  unavailable.
- BLE privacy now keeps the stable local identity separate from the active RPA,
  derives the local IRK from the identity root, distributes identity keys during
  bonding, resolves bonded peers through hardware AAR, and reuses the retained
  bond after an RPA change.
- SMP can exchange and retain CSRKs for both roles. ATT Signed Write Commands
  use AES-CMAC signatures, monotonically persisted counters, replay rejection,
  and a Bluefruit client `writeSigned()` entry point.
- Bond storage now retains up to eight peers with power-loss-safe replicated
  records, per-peer CCCDs and Service Changed state, role-correct legacy key
  tuples, LRU replacement, indexed inspection/deletion, and RPA resolution.
- GATT Robust Caching includes a spec-generated 128-bit Database Hash,
  persistent per-bond Client Supported Features, Database Out Of Sync handling,
  change-aware transitions, and notification/indication suppression while a
  client cache is stale.
- Bluefruit central support now includes safe service/characteristic object
  lifetimes, handle-range discovery, bulk characteristic discovery,
  `readCharByUuid()`, central indications, and bonded reconnect encryption.
- Custom GATT characteristics now enforce fixed and maximum lengths for local
  updates, remote writes, and prepare/execute writes. Service permissions are
  inherited by their characteristics. Dynamic read/write authorization runs
  callbacks outside the radio ISR, supports approve, reject, and replacement
  values, and fails closed on timeout or disconnect. Battery Service database
  writes and explicit notifications now have distinct behavior.
- `BLEUart::bufferTXD(true)` now coalesces small writes into the current ATT
  notification payload, sends a full packet automatically, and lets sketches
  explicitly flush a partial packet without discarding it on backpressure.
- Bluefruit central reads continue with Read Blob requests across ATT fragments,
  characteristic discovery determines complete handle ranges, and the HID
  client selects generic reports through their Report Reference descriptors.
- ATT responses and callbacks now act only on fresh link-layer packets, and
  service, characteristic, descriptor, and Read Blob pagination rejects
  malformed lengths, out-of-range handles, and non-advancing peer responses.
- Connection setup now rejects malformed access addresses, timing, channel-map,
  window, and hop fields before changing live state. Accepted central-side LL
  and L2CAP connection-parameter requests proceed to a scheduled connection
  update, and reserved SMP fields are rejected.
- Locally initiated connections now derive their access address and CRC seed
  from fail-closed hardware entropy before radio ownership changes, including
  the additional transition constraints required by LE Coded PHY.
- The ANCS client now sends complete notification/app attribute commands,
  reassembles fragmented Data Source responses, and implements title, subtitle,
  message, app-name, message-size, date, action-label, and action APIs. Fragmented
  response parsing is covered by host-side regression tests; live iOS behavior
  still depends on the phone's ANCS permissions and interoperability.
- The full [two-board release gate](docs/TWO_BOARD_RELEASE_GATE.md) exercises
  positive and rejected Numeric Comparison, all three OOB directions, RPA
  rotation, identity-key distribution, privacy-aware bonded reconnects, and
  signed-write counter persistence/replay rejection on XIAO nRF54L15 and XIAO
  nRF54LM20A.
- Timed System OFF now verifies reset-reason clearing before entry and exposes
  an abort-stage diagnostic; the gate requires two consecutive GRTC wake-reset
  cycles after the deliberate reset boundary on each connected board.
- XIAO nRF54LM20A Sense PDM uses the product-specific EDGE/RATIO encodings and
  a 1.28 MHz clock for 16 kHz PCM. The driver uses byte-addressed EasyDMA,
  re-arms LM20 clock/filter configuration after STOP, fences DMA ownership at
  `STOPPED`, and invalidates stale cache lines before samples are read. A final
  simultaneous 70-second hardware soak completed **115/115** captures on each
  Sense board with no timeout, underfill, guard, or DMA error: L15 captures took
  503-505 ms and LM20A captures 504-505 ms. Live peak-to-peak response reached
  4,626 counts on L15 and 3,994 counts on LM20A.
- Arduino IDE examples are grouped by board and Sense hardware. LM20A flash and
  RGB sketches, both XIAO Sense IMU/microphone sets, HOLYIOT-25008, and the
  Nordic DK now have distinct menus, with duplicate platform sketches removed.

Install the stable release from the normal Boards Manager feed shown above, or
request the exact version with Arduino CLI:

```bash
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.19"
```

- Controller-backed Bluetooth LE Channel Sounding Test is now available through
  the two public Arduino examples: `BleChannelSoundingInitiator` and
  `BleChannelSoundingReflector`.
- Nordic SDC/MPSL runs the timing-critical LE CS Test. The examples establish a
  per-cycle session token before the test and verify that same token when the
  reflector returns its controller result, so delayed results are not paired
  with a new sounding cycle.
- The public pair is hardware-validated with XIAO nRF54L15 and XIAO nRF54LM20A
  in both initiator and reflector roles. It requires the 128 MHz CPU profile.

This is an experimental two-board LE CS Test path, not a connected-ACL Channel
Sounding implementation or a Bluetooth qualification claim. See the
[Channel Sounding status](docs/CHANNEL_SOUNDING_CURRENT_STATUS.md) for the
protocol boundary, measurements, and validation evidence.

---

## Supported Boards

<div align="center">

| | |
|---|---|
| <img src="docs/xiao_nrf54l15_default_pin_routes.png" width="280"><br>**[XIAO nRF54L15 / Sense](https://wiki.seeedstudio.com/xiao_nrf54l15_sense_getting_started/)**<br>`xiao_nrf54l15` | <img src="docs/nrf54lm20a_front_pinout.png" width="280"><br>**[XIAO nRF54LM20A / Sense](https://wiki.seeedstudio.com/xiao_nrf54lm20a_with_onboard/)**<br>`xiao_nrf54lm20b` |
| <img src="docs/boards/holyiot_25007_product.png" width="280"><br>**[HOLYIOT-25007](docs/holyiot-25007-module-reference.md)**<br>`holyiot_25007_nrf54l15` | <img src="docs/boards/holyiot_25008_product.jpg" width="280"><br>**[HOLYIOT-25008](docs/holyiot-25008-module-reference.md)**<br>`holyiot_25008_nrf54l15` |

</div>

| Product | Arduino board selection | Specs / notes |
|---|---|---|
| **[Seeed Studio XIAO nRF54L15](https://wiki.seeedstudio.com/xiao_nrf54l15_sense_getting_started/)** | `XIAO nRF54L15 / Sense` (`xiao_nrf54l15`) | 128 MHz M33 + 128 MHz RISC-V coprocessor · 1.5 MB NVM · 256 KB RAM |
| **[Seeed Studio XIAO nRF54L15 Sense](https://wiki.seeedstudio.com/xiao_nrf54l15_sense_getting_started/)** | `XIAO nRF54L15 / Sense` (`xiao_nrf54l15`) | Same core board support as XIAO nRF54L15 · onboard LSM6DS3TR-C IMU + MSM261DGT006 PDM mic |
| **[Seeed Studio XIAO nRF54LM20A](https://wiki.seeedstudio.com/xiao_nrf54lm20a_getting_started/)** | `XIAO nRF54LM20A` (`xiao_nrf54lm20b`) | 128 MHz M33 + 128 MHz RISC-V coprocessor · 2 MB NVM · 512 KB RAM · nPM1300 PMIC · onboard 8 MB PY25Q64 QSPI flash · [back pinout](docs/nrf54lm20a_back_pinout.png) |
| **[Seeed Studio XIAO nRF54LM20A Sense](https://wiki.seeedstudio.com/xiao_nrf54lm20a_with_onboard/)** | `XIAO nRF54LM20A` (`xiao_nrf54lm20b`) | Same core board support as XIAO nRF54LM20A · onboard LSM6DS3TR-C IMU + MSM261DGT006 PDM mic · nPM1300 PMIC · onboard 8 MB PY25Q64 QSPI flash · [back pinout](docs/nrf54lm20a_back_pinout.png) |
| **[HOLYIOT-25007](docs/holyiot-25007-module-reference.md)** | `HOLYIOT-25007 nRF54L15 Module` (`holyiot_25007_nrf54l15`) | 18.0 x 14.8 mm · PCB antenna |
| **[HOLYIOT-25008](docs/holyiot-25008-module-reference.md)** | `HOLYIOT-25008 nRF54L15 Module` (`holyiot_25008_nrf54l15`) | 23.2 x 17.5 mm · PCB antenna |
| **Generic nRF54L15 36-pad module** | `Generic nRF54L15 Module (36-pad)` (`generic_nrf54l15_module_36pin`) | Reference module target; verify the power, clock, antenna, and pin design of the carrier board |
| **Nordic nRF54L15 DK** | `Nordic PCA10156 nRF54L15 DK` (`nrf54l15dk_pca10156`) | Development-kit target for bring-up and peripheral testing |

> See [board reference](docs/board-reference.md) for detailed pin assignments and schematics.

---

## Why Bare Metal?

| | This Core | nRF Connect SDK |
|---|---|---|
| **RTOS** | None (opt-in Thread/Matter) | Zephyr RTOS mandatory |
| **Binary size** | ~12 KB blink | ~100 KB+ blink |
| **Compiler** | GCC | GCC + Zephyr build system |
| **Peripheral access** | Direct register writes | Vendor HAL + DTS |
| **BLE stack** | Custom register-level; bundled Nordic SDC/MPSL for Channel Sounding | SoftDevice / Zephyr BLE |
| **RISC‑V coprocessor** | Direct boot/control and offload access; general runtime remains partial | Limited Arduino access |
| **Learning curve** | Datasheet + Arduino API | Zephyr + DTS + Kconfig |

**This core is for developers who want full hardware control with Arduino convenience.**

---

## Feature Matrix

This matrix describes the current `main` branch. The Boards Manager release can
lag behind it. A feature is not marked complete merely because a header,
constant, codec, or demonstration exists. The tables enumerate public protocol
families and major user-facing capabilities; related optional specification
procedures are grouped instead of pretending that every standards clause is a
separate feature.

| Status | Meaning |
|---|---|
| **Implemented** | Available through a supported API and exercised within the boundary stated in the row. This does not imply standards certification. |
| **Partial** | A useful subset works, but important behavior, roles, or interoperability are missing. |
| **Experimental** | Evaluation code and examples exist; APIs or behavior may change and production use is not claimed. |
| **Missing** | No supported standards-compatible implementation is provided. A stub or private test protocol may still exist. |
| **Hardware boundary** | The nRF54L device or supported board does not expose the required hardware. |

### Protocol Inventory

| Protocol or interface | Status | What users get |
|---|---|---|
| **Bluetooth Low Energy** | **Implemented** | Practical single-active-link central/peripheral operation, GAP, ATT/GATT, common services, security, bonding, privacy, HID, NUS, PHY/DLE/MTU control, and Bluefruit-compatible APIs. It is not a qualified complete Bluetooth controller. |
| **Bluetooth LE extended advertising/scanning** | **Partial** | Local extended payload transmission and scanner reassembly through 995 bytes; broad cross-vendor and connectable-extended interoperability remain open. |
| **Bluetooth LE Channel Sounding** | **Experimental** | Standalone two-board, single-antenna controller-backed LE CS Test. No connected-ACL ranging or cross-vendor claim. |
| **Bluetooth Mesh** | **Missing** | No Bluetooth Mesh provisioning, bearer, transport, model or relay stack is shipped. |
| **IEEE 802.15.4** | **Partial** | 2.4 GHz PHY, TX/RX, CCA/ED, filtering, ACK and frame-pending primitives. A complete reusable MAC transaction engine is missing. |
| **Proprietary raw 2.4 GHz radio** | **Implemented** | Raw packet TX/RX and acknowledgement examples for direct radio experimentation. |
| **Zigbee** | **Experimental** | Direct-device examples, secure join/rejoin pieces, selected ZDO/ZCL paths, sleepy examples, and Zigbee2MQTT integration. It is not a complete Zigbee PRO stack. |
| **Thread** | **Experimental** | A real imported OpenThread FTD core with an nRF54 platform port, local two-board UDP and MeshCoP validation, and staged SRP/sleepy-device paths. |
| **Matter over Thread** | **Experimental** | CHIP system, crypto, packet-buffer and Inet foundations plus private onboarding/session demos. There is no standards-compatible Matter device runtime yet. |
| **NFC-A tag** | **Partial** | NFCT sense, activation, frame and EasyDMA APIs plus a tag setup example; antenna and phone interoperability depend on the board design and remain lightly validated. |
| **UART/UARTE** | **Implemented** | `Serial`, `Serial1`, routing options, buffered I/O, and lower-level UARTE access. |
| **I2C/TWI** | **Partial** | `Wire`, `Wire1`, repeated starts and controller mode are established; TWIS target APIs exist with narrower multi-instance validation. |
| **SPI** | **Partial** | Arduino `SPI`, lower-level SPIM, board-specific high-speed paths, and SPIS target examples. |
| **I2S, TDM, and PDM audio** | **Partial** | I2S TX/RX/duplex on nRF54L15 and PDM capture on supported Sense boards. nRF54LM20A uses TDM instead of I2S, and no TDM wrapper is exposed yet. |
| **External QSPI flash** | **Implemented** | LM20A onboard PY25Q64 access through `SPI_HS`, Adafruit SPIFlash compatibility, and explicit deep-power-down support. |
| **VPR IPC/RPC** | **Partial** | RISC-V boot/control, shared transport and selected offload/service probes; not a general production softperipheral runtime. |
| **CMSIS-DAP/SWD upload transport** | **Implemented** | Packaged native nRF OCD upload with pyOCD recovery and UID-bound two-board validation tooling. |
| **Native USB device / TinyUSB** | **Hardware boundary** | The XIAO boards use an external SAMD11 USB bridge. TinyUSB headers are compile-compatibility stubs and report unsupported at runtime. |
| **Wi-Fi / Wi-Fi scanning** | **Hardware boundary** | No Wi-Fi PHY or MAC is present; the 2.4 GHz radio cannot decode or associate with 802.11 networks. |
| **Bluetooth Classic** | **Hardware boundary** | The radio supports Bluetooth LE, not BR/EDR. |
| **ANT and Nordic ESB** | **Missing** | Raw proprietary-radio examples exist, but no supported ANT or Enhanced ShockBurst protocol stack is shipped. |
| **Ethernet and CAN** | **Missing** | No native stack or controller API is shipped. External devices can be driven by user libraries over SPI or UART. |
| **Wireless OTA / secure DFU** | **Missing** | No production BLE DFU, Zigbee OTA, Matter OTA Requestor/BDX, or generic wireless boot-update path is provided. |

### Bluetooth Low Energy

Implemented BLE rows refer to the documented single-active-link scope. They do
not assert complete Bluetooth Core conformance, complete Bluefruit parity, or
Bluetooth SIG qualification.

| Capability | Status | Available now | Important boundary |
|---|---|---|---|
| Legacy advertising and scan response | **Implemented** | Connectable, scannable, non-connectable, background, and directed advertising with bounded high-duty timeout. | Continue broad phone and long-duration regression testing. |
| Active/passive scanning | **Implemented** | Filters, callbacks, scan responses, and central connection initiation. | Cross-platform stress coverage remains narrower than controller qualification. |
| Central and peripheral roles | **Implemented** | Both roles and dual-role examples are available. | The supported claim is one active connection; requested Bluefruit link counts do not create a production multi-link controller. |
| PHY, DLE, and ATT MTU | **Implemented** | LE 1M, 2M, Coded S2/S8, DLE through 251 bytes, and MTU through 247 bytes when requested. | Broader fallback and hostile-procedure timing tests remain. |
| Connection control | **Partial** | Parameter updates, reason reporting, reset recovery, and malformed `CONNECT_IND` validation. | Interleaved LL control-procedure collisions and multi-link stress are not complete. |
| Extended advertising/scanning | **Partial** | Non-connectable payload chains and local scanner reassembly through 995 bytes. | Extended-connectable, cross-vendor and extended scan-response coverage is incomplete. |
| Periodic advertising | **Missing** | The capability probe fails closed. | No periodic advertising scheduler or synchronization path. |
| GATT server and client | **Implemented** | 16/128-bit UUIDs, discovery, reads/writes, CCCDs, notifications, and confirmed indications. | Formal ATT/GATT qualification is not complete. |
| Long values and descriptors | **Implemented** | Read Blob, Prepare/Execute Write, fixed/maximum lengths, and `0x2901`, `0x2904`, `0x2908` descriptors. | Broader phone/desktop edge-case interoperability remains. |
| Permissions and authorization | **Implemented** | Open, encrypted, authenticated/MITM and signed permissions plus deferred sketch-context authorization. | Host negative-test coverage remains incomplete. |
| Service Changed | **Implemented** | Per-bond database fingerprint, pending ranges, reconnect retry, and confirmation handling. | Not PTS-qualified. |
| GATT Robust Caching | **Implemented** | Database Hash, Client Supported Features, change-awareness and Database Out Of Sync behavior on the single ATT bearer. | EATT/multiple-bearer behavior is unavailable. |
| LE Secure Connections | **Implemented** | Just Works, fixed passkey, passkey input, Numeric Comparison, and mutual/one-way OOB. | A broader Android/iOS/desktop malformed, timeout and OOB matrix remains. |
| Bond database and key distribution | **Implemented** | Eight peers, LRU replacement, LTK/IRK/CSRK state, CCCDs, identity data, signing counters, enumeration and deletion. | Product provisioning and Bluetooth qualification remain separate. |
| Privacy and RPA | **Partial** | Stable identity, IRK exchange, rotating RPAs, hardware AAR, privacy-aware reconnects, and an application-managed resolving list. | No automatic controller-enforced resolving-list/allow-list policy. |
| Authenticated signed writes | **Implemented** | CSRK distribution, AES-CMAC, durable monotonic counters and replay rejection. | Broad phone/desktop signed-write interoperability is not established. |
| BLE UART / Nordic UART Service | **Implemented** | Peripheral/client APIs, buffered MTU-aware TX, notification/write and bridge examples. | Web/device bridge behavior remains in the regression matrix. |
| HID over GATT | **Implemented** | Keyboard, mouse, consumer control, gamepad, Report/Boot modes, Report Reference and keyboard LEDs. | Broad OS report parsing, gamepad and boot-mode testing remain. |
| Other bundled services | **Implemented** | Device Information, Battery, Current Time, ANCS parsing, beacons and custom services. | This is not every adopted Bluetooth profile or service. |
| Bluefruit52 compatibility | **Partial** | Common advertising, scanning, GATT, central/client, security, HID and service APIs compile and run. | Complete nRF52 Bluefruit API and behavioral parity is not claimed. |
| LE Credit-Based CoC and EATT | **Missing** | Fixed ATT/SMP/signaling L2CAP channels only. | No LE CoC or enhanced ATT bearers. |
| ISO, LE Audio, PAwR, AoA/AoD | **Missing** | No supported CIS/BIS, BIG, LE Audio, PAwR, direction-finding or mesh implementation. | These are outside the current practical BLE scope. |
| Nordic secure DFU | **Missing** | `BLEDfu::begin()` returns `ERROR_NOT_SUPPORTED`. | No non-working DFU service is advertised. |
| Phone/desktop interoperability | **Partial** | Multiple Android/iOS HID sessions and Linux diagnostic workflows have been exercised. | Repeatable Android, iOS, Windows, macOS and Linux coverage is not yet a release gate. |
| Bluetooth qualification | **Missing** | Repository tests and the two-board gate provide regression evidence. | No PTS/BQB, RF-PHY, controller, host, profile or end-product qualification claim. |

See [BLE implementation and qualification status](docs/BLE_COMPLIANCE_RESUME.md)
and the [two-board release gate](docs/TWO_BOARD_RELEASE_GATE.md).

### IEEE 802.15.4

| Capability | Status | Available now | Important boundary |
|---|---|---|---|
| 2.4 GHz PHY | **Implemented** | 250 kbit/s radio setup, 127-byte frames, CRC, channel/TX power, RSSI, CCA and energy detection. | Regulatory and RF-PHY qualification are product responsibilities. |
| ACK, filtering and receive queue | **Implemented** | TX ACK wait, automatic ACK, frame-pending callback, application/PAL filter callback, and buffered IRQ receive. | Hardware address matching is disabled; filtering policy is supplied by the caller. OpenThread installs one, while bundled Zigbee examples do not. |
| MAC frame/control primitives | **Partial** | Data, beacon, ACK, association, orphan, realignment and data-request helpers. | No complete reusable MAC service/PIB runtime. |
| CSMA-CA and retry engine | **Missing** | The radio API exposes one optional CCA check, but bundled Zigbee example transmissions currently disable it and perform one TX/ACK attempt. | No randomized BE backoff, retry queue or general duplicate table. OpenThread supplies its own software MAC behavior. |
| Raw-radio diagnostics | **Implemented** | Packet TX/RX, ACK, source-match and OpenThread PAL diagnostic examples. | These tests do not establish Zigbee or Thread conformance. |

### Zigbee

All Zigbee support remains experimental. An implemented primitive below does
not make the overall stack Zigbee PRO compliant.

| Capability | Status | Available now | Important boundary |
|---|---|---|---|
| Stack runtime architecture | **Experimental** | Shared codecs, security, persistence and commissioning helpers are used by bundled sketches. | Protocol behavior remains split across example-owned loops; there is no single event-driven coordinator/router/end-device runtime. |
| Direct NWK framing and security | **Partial** | Direct unicast codecs and AES-CCM* secured NWK frames. | Source-route and multicast frames are rejected by the generic codec. |
| NWK command set | **Partial** | Rejoin and End Device Timeout request/response commands. | Route Request/Reply/Record, Link/Network Status and Network Report/Update are absent. |
| Mesh routing and repair | **Missing** | Neighbor/routing table structures and management responses can expose sketch-owned entries. | No route discovery, aging, repair, many-to-one/source routing or production multi-hop forwarding. |
| APS group addressing and membership | **Partial** | APS group codecs and local group membership. | Fan-out is example-specific and not integrated with routing. |
| NWK broadcast/multicast forwarding | **Missing** | None. | No broadcast transaction table, rebroadcast jitter, multicast forwarding or general duplicate suppression. |
| Sleepy-child indirect delivery | **Experimental** | Frame-pending ACKs, polling, End Device Timeout negotiation and example-owned pending payloads. | No shared persistent child/indirect queue manager or long-duration parent test. |
| APS data/command/ACK codecs | **Partial** | Direct data/group/command frames, APS ACK, and Trust Center key commands. | Extended APS headers are not supported. |
| APS reliability | **Experimental** | A small retry/ACK mechanism exists in the HA coordinator example. | No reusable transaction manager, delivery backpressure or stack-wide retry policy. |
| APS fragmentation/reassembly | **Missing** | None. | Large application payload fragmentation is unavailable. |
| Binding, Groups and Scenes | **Partial** | Eight-entry binding tables, group codecs and local Groups/Scenes state. | Fan-out is example-specific and not integrated with production routing. |
| Scanning, association and rejoin | **Partial** | End-device steering, association, secure rejoin, leave handling and timeout negotiation helpers. | Full Zigbee Base Device Behavior and all-role commissioning policy are missing. |
| Cryptographic primitives | **Implemented** | AES-CCM*, NWK/APS security headers, ZigbeeAlliance09 key and install-code CRC/key derivation. | Primitive coverage does not establish a secure product lifecycle. |
| Keys and Trust Center lifecycle | **Partial** | Active/alternate network-key transport and switch paths. | No production device/link-key table, full authorization policy or distributed-security runtime. |
| Replay counters and persistence | **Partial** | Network identity, keys, counters, reporting and bindings can persist. | Incoming counters are not per-neighbor and storage is not proven brownout-atomic. |
| ZDO | **Partial** | Address/descriptor discovery, match, bind/unbind, leave and management table responses. | No general transaction timeout/retry/concurrency or Network Update manager. |
| ZCL foundation | **Partial** | Read/write/discover/reporting/default-response helpers for a limited type set. | Structured and manufacturer-specific data plus general endpoint stores are incomplete. |
| ZCL clusters | **Partial** | Selected Basic, Power, Identify, Groups, Scenes, On/Off, Level, Color, Temperature and Humidity server paths. | Complete client/server commands and broad cluster coverage are not present. |
| End-device and sleepy examples | **Experimental** | Joinable lights, sensors and buttons, including 15/60-second sleepy examples. | Behavior is sketch-specific and unqualified. |
| Router role | **Experimental** | Rx-on router-capable examples can join. | Without route discovery/relay/repair this is not a production Zigbee PRO router. |
| Coordinator / Trust Center | **Experimental** | The HA coordinator can form a network and admit/interview bundled examples. | Fixed small RAM tables and sketch-owned policy are not restart-safe production Trust Center behavior. |
| Zigbee OTA Upgrade | **Missing** | Only the cluster identifier is present. | No client/server, image verification, resume or boot handoff. |
| Green Power Proxy Basic | **Missing** | None. | Required for a Zigbee 3.0 routing-capable certification claim. |
| Touchlink, Zigbee Direct, WWAH, NCP/RCP | **Missing** | None. | Outside the current implementation. |
| Zigbee2MQTT / Home Assistant | **Experimental** | Documented join/interview/state tests and an external converter for bundled models. | The converter is not upstream and coverage is not a broad ecosystem matrix. |
| Example and host regression coverage | **Partial** | Thirty bundled Zigbee sketches and local serial/MQTT validation scripts exist. | Main CI compiles five Zigbee sketches, and no native Zigbee codec/security test suite exists under `tests/`. |
| Multi-hop, soak and certification | **Missing** | Two-board and coordinator integration scripts exist. | No three-node route/repair gate, mixed-vendor soak, ZUTH result or compliant-platform certification. |

See the [Zigbee full-support handoff](docs/ZIGBEE_FULL_SUPPORT_HANDOFF.md)
and [Zigbee2MQTT integration guide](docs/ZIGBEE2MQTT_INTEGRATION.md).

### Thread

Thread uses a real imported OpenThread FTD core. The experimental status refers
to this nRF54 platform port and its validation, not to the quality or
certification status of upstream OpenThread.

| Capability | Status | Available now | Important boundary |
|---|---|---|---|
| Imported OpenThread FTD core | **Implemented** | Pinned upstream source is compiled when the experimental Thread stage is selected. | This nRF54 platform port is not a certified OpenThread platform. |
| nRF54 radio/platform layer | **Experimental** | Radio, alarm, entropy, reset and settings PAL plus cooperative processing. | Cooperative polling and incomplete power/interoperability evidence remain. |
| Formation and FTD roles | **Experimental** | Dataset setup and Leader, Router and Child paths; mixed L15/LM20A Leader/Child hardware validation. | No production multi-hop/router topology matrix. |
| IPv6, 6LoWPAN and mesh | **Experimental** | Upstream OpenThread networking exercised on a local two-board network. | No external OTBR, cross-vendor, interference or long-duration route test. |
| UDP, multicast and fragmentation | **Implemented** | Four sockets, explicit close, both unicast directions, multicast and payloads through 512 bytes. | This is local mixed-board transport evidence, not general interoperability. |
| Application CoAP / Secure CoAP APIs | **Missing** | MeshCoP internally uses CoAP/DTLS components. | `OPENTHREAD_CONFIG_COAP_API_ENABLE` and `OPENTHREAD_CONFIG_COAP_SECURE_API_ENABLE` remain disabled; no supported application API or interoperability gate is shipped. |
| MeshCoP Commissioner and Joiner | **Experimental** | Fresh PSKd join, reset-only dataset restore and exact wrong-PSKd rejection pass on two boards. | External Commissioner/Border Agent interoperability and broader negatives are missing. |
| Operational Dataset persistence | **Partial** | Normal restart preserves settings and reported storage failures retain the prior mapping. | Shared Preferences storage is not journaled/dual-bank or proven under controlled brownout. |
| SRP client; DNS client with Matter stage | **Partial** | SRP client and auto-start are built into Thread stage; the DNS client is enabled only when Matter stage also enables DNS-SD. | No external OTBR/SRP registration, removal, reboot or DNS-SD visibility gate. mDNS/server paths are disabled. |
| Thread security and entropy | **Experimental** | Upstream mbedTLS plus fail-closed CRACEN entropy; correct/wrong PSKd paths pass locally. | No Thread security conformance or fault/attack campaign. |
| Sleepy End Device | **Experimental** | Sleepy-child attach and poll APIs/examples; software CSL parameters are recorded. | The polling PAL has no hardware-timed CSL wakeups; sleep current, missed-poll recovery, parent interoperability and long soaks remain. |
| Border Router / Border Agent / Backbone Router | **Missing** | None in the shipped profile. | Use an external OTBR; no onboard NAT64, infrastructure routing or Border Agent. |
| Advanced optional services | **Missing** | TCP, channel manager/monitor, jam detection, history, network time and the full link-metrics manager are disabled; upstream CLI, POSIX, NCP/RCP and spinel transport were omitted from the import. | Upstream header presence does not make these available. |
| External interoperability | **Missing** | Repo-owned two-board gates only. | No completed OTBR, cross-vendor, three-node, RF-loss or long-duration matrix. |
| Thread certification | **Missing** | None. | No Thread conformance harness or product certification has passed. |

See [Thread and Matter hardening status](docs/THREAD_MATTER_FINISH_PLAN.md).

### Matter

The Matter entries rate standards-compatible behavior. Private PASE-like,
CASE-like, certificate and command demos are useful tests but are not marked as
standard Matter features.

| Capability | Status | Available now | Important boundary |
|---|---|---|---|
| Upstream connectedhomeip foundation intake | **Partial** | Selected support, System Layer, PacketBuffer, error/key/time and Thread-dataset units are imported. | There is no complete upstream Matter server/runtime. |
| System clock, timers and work queue | **Implemented** | Monotonic clock and mutation-safe cooperative timer/work dispatch with host regressions. | A platform foundation is not a Matter device stack. |
| Crypto foundations | **Implemented** | SHA-256, HKDF, PBKDF2, AES-CCM, P-256 ECDH/ECDSA and DRBG entry points with fail-closed entropy. | P-256 is software and these primitives are not connected to an upstream Secure Channel. |
| CHIP PacketBuffer, IPv6 and UDP Inet | **Experimental** | Four queued endpoints, 1280-byte IPv6 datagrams, a private multicast transport probe and bidirectional payloads through 1200 bytes on two boards. | Transport validation only. |
| Setup payload helpers | **Experimental** | Manual/QR payload and setup identity helpers. | They do not prove successful standard commissioning. |
| DNS-SD / SRP discovery | **Partial** | Demo commissionable records can be built, queued and removed through OpenThread SRP. | No external OTBR/controller has verified publication or discovery. |
| Standard PASE / SPAKE2+ | **Missing** | A fail-closed private PASE-like experiment exists. | Its framing/transcript is not Matter wire-compatible and cannot commission with a standard controller. |
| BLE commissioning rendezvous | **Missing** | None; the current profile is on-network only. | No standard Matter BLE commissioning service or transport. |
| Standard CASE sessions | **Missing** | A private Sigma-style tamper/replay experiment exists. | Private certificates, encodings and key schedule are not wire-compatible CASE. |
| Exchange Manager and reliable messaging | **Missing** | None from the upstream runtime. | Local message headers/counters do not replace Matter exchanges and retransmission. |
| Interaction Model and generated data model | **Missing** | Project-specific On/Off, Level, Identify and Scenes objects exist. | No upstream IM engine, subscription/reporting engine or generated endpoint model. |
| Group communication and key management | **Missing** | None from the upstream runtime. | No Group Data Provider, group sessions/key sets, secured multicast commands or standard Groups-cluster integration. |
| Access control | **Experimental** | Local ACL checks fail closed unless complete subject/fabric/node context is supplied. | Not connected to standard fabrics, CASE subjects or Access Control cluster persistence. |
| Fabric and operational credentials | **Missing** | A small in-memory demo fabric table exists. | No standard NOC/RCAC/ICAC provisioning, persisted multi-fabric lifecycle, keystore or fail-safe transaction. |
| Device Attestation | **Missing** | Test-only PAA/PAI/DAC-like objects and regressions. | Keys are regenerated and encodings are private; no standard DAC chain or Certification Declaration. |
| Persistence | **Partial** | Demo setup identity, Thread dataset, factory data and light state use Preferences. | Standard fabrics, ACLs, credentials, sessions and counters are not durably managed or brownout-proven. |
| OTA Requestor and BDX | **Missing** | Excluded from the staged upstream intake. | No Matter OTA runtime. |
| ICD / low-power Matter device | **Missing** | Thread has a separate experimental sleepy-child path. | No upstream ICD server, check-in protocol or Matter-aware sleepy lifecycle. |
| Ecosystem interoperability | **Missing** | Private two-board demos only. | No successful commissioning/control with `chip-tool`, Home Assistant, Google Home, Apple Home or SmartThings. |
| Matter certification | **Missing** | None. | No CSA test harness, production credentials or ecosystem certification. |

See [Thread and Matter hardening status](docs/THREAD_MATTER_FINISH_PLAN.md).

### Bluetooth LE Channel Sounding

| Capability | Status | Available now | Important boundary |
|---|---|---|---|
| Controller-backed LE CS Test | **Experimental** | Nordic SDC/MPSL runs the controller procedure on two supported boards. | This is a test procedure, not a complete connected BLE Channel Sounding product path. |
| Initiator and reflector examples | **Implemented** | Two public examples with a CRC-protected session/result exchange. | Both sides must use this core's private transport. |
| Mode and antenna support | **Experimental** | Single-antenna Mode 2/Submode 1 with AA-only RTT. | No multi-antenna, AoA/AoD or full mode matrix. |
| Connected-ACL Channel Sounding | **Missing** | None. | No standards-complete LL control workflow attached to a normal BLE ACL. |
| Calibrated distance estimation | **Missing** | Raw/test results and diagnostics only. | No production calibration, accuracy or environmental model. |
| Cross-vendor interoperability and qualification | **Missing** | Local two-board hardware validation only. | No Bluetooth qualification or cross-vendor controller evidence. |

See [Channel Sounding current status](docs/CHANNEL_SOUNDING_CURRENT_STATUS.md).

### Arduino APIs, Buses, Storage, and Board Hardware

| Capability | Status | Available now | Important boundary |
|---|---|---|---|
| Startup, `millis`, `micros`, `delay` | **Implemented** | Arduino sketch lifecycle and a shared monotonic timebase across supported MCU variants. | Continue long-duration drift and mixed-radio regression testing. |
| GPIO | **Implemented** | `pinMode`, `digitalRead`, `digitalWrite` and lower-level port access. | Board pin routing still applies. |
| External interrupts | **Partial** | GPIOTE-backed rising/falling/change on interrupt-capable ports. | Port P2 has no interrupt or wake capability in hardware. |
| ADC / SAADC | **Implemented** | `analogRead`, resolution control, internal-supply helpers, gain and oversampling APIs. | Board-specific calibration affects absolute accuracy. |
| PWM | **Implemented** | `analogWrite`, global/per-pin frequency paths and hardware/timer/software allocation. | Frequencies and channels share finite PWM/timer resources. |
| UART/UARTE | **Implemented** | `Serial`, `Serial1`, compatible extra routes and lower-level UARTE APIs. | The XIAO USB serial path is an external bridge, not native USB CDC. |
| I2C controller (`Wire`) | **Implemented** | `Wire`, `Wire1`, repeated starts and lower-level TWIM access. | Pin mux and serial-fabric ownership are board-specific. |
| I2C target (TWIS) | **Partial** | Target callbacks and TWIS21/TWIS30 examples. | Multi-instance and stress validation are narrower than controller mode. |
| SPI controller | **Implemented** | Arduino `SPI`, lower-level SPIM and selected multi-instance examples. | Maximum usable speed depends on instance, pins and board routing. |
| SPI target (SPIS) | **Partial** | SPIS wrapper and target echo example. | Broad multi-instance/high-speed validation is incomplete. |
| `SPI_HS` / external QSPI pads | **Implemented** | LM20A onboard flash and deliberate high-speed/QSPI-pad access. | `SPI_HS` is not the normal XIAO header `SPI` bus. |
| I2S (nRF54L15 only) | **Implemented** | TX, RX and duplex wrappers with interrupt examples. | nRF54LM20A has TDM rather than I2S; this core does not expose a TDM API. |
| TDM (nRF54LM20A) | **Missing** | None. | The LM20A TDM peripheral is not wrapped by this core. |
| PDM | **Implemented** | PDM20/PDM21 capture and board microphone examples. | Pin/base selection and microphone wiring differ by board. |
| QDEC | **Implemented** | QDEC20/QDEC21 wrapper and encoder example. | External encoder hardware is required. |
| NFC-A / NFCT | **Partial** | Low-level NFCT and tag setup path. | Most supported XIAO boards do not provide a ready-to-use NFC antenna. |
| Comparator / LPCOMP | **Implemented** | Threshold, window and wake examples. | Analog routing and threshold accuracy are board-dependent. |
| Watchdog | **Implemented** | WDT wrapper and examples. | Multi-instance product policy remains application-owned. |
| GRTC / low-frequency PWM | **Partial** | Timekeeping, compare, wake and fixed-pin GRTC PWM examples. | Continuous waveform validation is limited by board pin conflicts. |
| DPPI / EGU | **Implemented** | Hardware event/task routing wrappers and examples. | Domain and channel ownership must be coordinated by the sketch. |
| EEPROM / Preferences | **Implemented** | RRAM-backed Arduino-style persistence used by bonds and staged protocol state. | Shared storage is capacity-limited and not a journaled brownout-atomic database. |
| LM20A external flash | **Implemented** | PY25Q64 JEDEC/read/write/erase and deep-power-down paths plus SPIFlash examples. | LM20A-specific; other boards may use different or no external flash. |
| VPR RISC-V control | **Partial** | Boot/reset, shared memory, RPC transport, ticker and checksum/offload probes. | No stable general-purpose coprocessor framework or resource scheduler. |
| VPR SoftPeripheral / sQSPI | **Experimental** | Host wrappers, RPC definitions, firmware scaffolding and a flash probe exist. | The complete firmware pair and low-power restoration are not established across supported boards. |
| nPM1300 PMIC | **Implemented** | Charger, VBUS limit, rails, telemetry, hibernate and battery-current examples. | Not present on the L15 XIAO. |
| System ON/OFF power APIs | **Implemented** | Low-power idle, timed System OFF, reset-cause APIs, RF-switch and board-specific shutdown helpers. | Measured current depends on debugger, LEDs, sensors, USB and board configuration. |
| Sense IMU and microphone | **Implemented** | LSM6DS3TR-C IMU and board-specific PDM microphone examples for supported Sense variants. | Selecting a non-Sense board does not add the sensors. |
| TinyUSB / native USB device | **Missing** | Compile-compatibility headers return unsupported. | Upload and serial use the board's external CMSIS-DAP/SAMD11 bridge. |
| I3C | **Hardware boundary** | None. | nRF54L exposes I2C controller/target blocks rather than I3C. |

See the [board reference](docs/board-reference.md) for pin, peripheral-instance,
sensor and routing details.

### Crypto, Keys, and Tamper

| Capability | Status | Available now | Important boundary |
|---|---|---|---|
| CRACEN entropy/RNG | **Implemented** | Hardware entropy source used fail-closed by BLE and staged Thread/Matter paths. | Product health monitoring and formal validation remain product concerns. |
| Hardware AAR, ECB and CCM | **Implemented** | Address resolution and BLE AES operations. | This does not automatically accelerate all Thread/Matter crypto. |
| Software hashes, HMAC, HKDF, PBKDF2 and AES-CCM | **Implemented** | mbedTLS-backed CHIP primitives plus the local streaming SHA-256/HMAC/PBKDF2 helper, with host vectors. | Full protocol conformance depends on the surrounding standard stack. |
| Software secp256r1 | **Implemented** | P-256 key generation/ECDH for BLE Secure Connections and ECDH/ECDSA for private Matter experiments. | Software arithmetic is comparatively slow; the Matter path is not connected to a standard upstream Secure Channel. |
| CRACEN PKE acceleration | **Missing** | Nordic publishes Nordic-IC-only PKE microcode and an accompanying NCS driver for CRACEN Base devices such as nRF54L15. | This core does not ship/load that path; nRF54LM20A uses CRACEN Lite and is not a target for this PKE microcode. |
| KMU | **Partial** | Low-level slot metadata/task APIs and constrained IKG paths. | No complete product provisioning, rotation and protocol-consumer lifecycle. |
| Tamper/glitch detection | **Partial** | TAMPC/control wrappers and diagnostic examples. | External tamper, reset behavior and secure product policy need hardware characterization. |
| Production credentials | **Missing** | Development/test credentials exist in experimental paths. | No manufacturing provisioning system for Matter DACs, Zigbee Trust Center identity, or comparable product secrets. |

---

## Bluetooth LE

Bluetooth LE is the core's primary wireless surface. It uses a clean,
register-level host/controller implementation for normal BLE operation and
ships a `Bluefruit52Lib` compatibility layer so familiar Adafruit-style
sketches can be moved to nRF54L with limited changes.

### Implemented Surface

| Layer | Available functionality |
|---|---|
| **GAP** | Advertising, active/passive scanning, central and peripheral roles, dual-role examples, connection parameter updates, RSSI, 1M/2M/Coded PHY, and reconnect behavior |
| **ATT/GATT** | Server and client discovery, 16-bit and 128-bit UUIDs, services, characteristics, descriptors, CCCDs, long reads/writes, fixed/maximum value lengths, notifications, indications, MTU 247, and DLE 251 paths |
| **Compatibility services** | BLE UART/NUS, Device Information, Battery, HID keyboard/mouse/gamepad, Current Time, ANCS notification and fragmented attribute handling, beacons, and custom services |
| **Security** | LE Secure Connections, Just Works, passkey/PIN flows, asynchronous Numeric Comparison, mutual and one-way OOB, bonding, negotiated 7-16-byte key sizes, CSRK signed writes, and authenticated access permissions |
| **Privacy** | Stable identity, local IRK derivation, rotating RPAs, identity-key distribution, eight-peer bond storage, direct/identity/RPA selection, and privacy-aware directed reconnect |
| **Reliability** | Encrypted retransmission/counter handling, signed-write anti-replay counters, SMP timeout and repeated-attempt controls, fail-closed CRACEN entropy, and release-gate positive/negative pairing tests |

The two-board release gate checks advertising, discovery, CCCDs, PHY changes,
MTU/DLE, long notifications, pairing, bond reload, encrypted reconnect,
Numeric Comparison acceptance and rejection, all three OOB directions, RPA
rotation, identity-key distribution, AAR-based bond resolution, and signed
writes with persisted monotonic counters plus replay rejection. See
[Two-Board Release Gate](docs/TWO_BOARD_RELEASE_GATE.md) for the exact scope and
test procedure.

### Choosing An API

- Use `#include <bluefruit.h>` for the largest example set and compatibility
  with common Bluefruit sketches.
- Start with `Bluefruit52Lib > Peripheral > bleuart` for a phone or computer
  peripheral, or `Bluefruit52Lib > Central > central_bleuart` for a central.
- Use `BLEService` and `BLECharacteristic` for custom GATT services. Configure
  properties, permissions, fixed or variable length, and callbacks before
  calling `begin()`.
- Use `Bluefruit.Security` and characteristic permissions when a value must be
  encrypted or authenticated. Numeric Comparison requires an explicit
  asynchronous accept/reject response; the bundled example shows the complete
  flow.
- Use `Bluefruit.Security.enumerateBonds()`, `getBondInfo()`, `deleteBond()`,
  and `clearBonds()` to manage up to eight built-in peer records. The legacy
  custom persistence callback interface remains intentionally capacity-one.
- Use the lower-level examples under `Nrf54L15-Clean-Implementation` for radio,
  privacy, crypto, and hardware diagnostics where compatibility wrappers would
  hide the behavior being tested.
A normal peripheral sketch follows this order:

1. Create service and characteristic objects globally.
2. Call `Bluefruit.begin()`, set the device name and TX power, and register
   connection callbacks.
3. Configure security before starting protected services.
4. Configure and start each GATT service.
5. Add flags, advertised services, and the device name to the advertising and
   scan-response payloads.
6. Enable restart-on-disconnect, select advertising intervals, and call
   `Bluefruit.Advertising.start()`.

### BLE Scope Boundary

The core's BLE claim is intentionally narrower than "all Bluetooth LE." It does
not claim Bluetooth SIG qualification or complete Core conformance. Locally
generated legacy bond-key distribution and the built-in multi-bond privacy
store are implemented; automatic controller-enforced allow-list policy, broad
cross-vendor negative testing, and PTS/BQB remain outside the validated scope.
The built-in store retains up
to eight peers with isolated signing counters, CCCDs, privacy identities, and
Service Changed state. Nordic secure DFU is not
implemented, and `BLEDfu::begin()` returns `ERROR_NOT_SUPPORTED`. Legacy
directed advertising is supported with explicit target selection and compliant
high-duty scheduling. Service Changed tracking persists a structural database
fingerprint and pending handle range per bond, retries on encrypted
reconnects, and clears the range only after ATT confirmation. Channel Sounding
is separate from the normal connected BLE stack and remains experimental, as
documented below.

---

## Power Consumption

The core has board-specific low-power behavior rather than treating sleep as a
CPU-only operation. On XIAO nRF54L15 it controls the external RF switch around
radio activity. On XIAO nRF54LM20A it coordinates the nPM1300, oscillator state,
RAM retention, and the onboard QSPI flash's deep-power-down mode.

For the `1.0.18` L15/LM20A startup-charge and idle-current comparison, see
the [issue #114 investigation](docs/ISSUE_114_POWER_ANALYSIS.md) and the
[`ble_power_breakdown` PPK2 diagnostic](hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/examples/Diagnostics/ble_power_breakdown/README.md).
Version `1.0.19` removes eager BLE pairing-key generation and restores
PMIC measurement settings after telemetry reads. Updated current measurements
are pending; the historical captures below remain attributed to their original
versions.

### Community PPK2 Measurements

These are **measured board-level snapshots**, not datasheet limits or guaranteed
production figures. They depend on the core version, sketch, radio settings,
power path, board revision, instrument wiring, temperature, attached probes,
and peripherals. USB/debug connections can dominate microamp measurements.

#### Delay and System OFF, side by side

[![PPK2 current traces comparing XIAO nRF54LM20A and XIAO nRF54L15 during returning delay and no-retention System OFF](https://github.com/user-attachments/assets/b76a3790-2665-4bff-b382-a28630701ab9)](https://github.com/lolren/nrf54-arduino-core/discussions/76#discussioncomment-17293706)

*Community PPK2 comparison posted 14 June 2026: the LM20A trace (left)
labels returning `delay()` at about 7 uA and no-retention System OFF at about
3 uA; the L15 trace (right) labels the same paths at about 4 uA and 2.5 uA.
This is a point-in-time comparison, not a guaranteed `1.0.0` current limit.
[Measurement and image by @msfujino in Discussion #76](https://github.com/lolren/nrf54-arduino-core/discussions/76#discussioncomment-17293706).*

#### XIAO nRF54L15

| Test snapshot | Measured result | Source |
|---|---:|---|
| Returning `delay()` / System ON sleep | **about 4 uA** | [Discussion #76 comparison](https://github.com/lolren/nrf54-arduino-core/discussions/76#discussioncomment-17293706) |
| `delaySystemOffNoRetention()` | **about 2.5 uA** | [Discussion #76 comparison](https://github.com/lolren/nrf54-arduino-core/discussions/76#discussioncomment-17293706) |

The post does not identify the exact core version used for this comparison.

#### XIAO nRF54LM20A

The latest posted trace used core `v0.9.222` after the LFXO, external-flash DPD,
nPM1300 cleanup, and System OFF fixes. It repeated the `v0.9.219` result and
reported:

| Test snapshot | Measured result | Source |
|---|---:|---|
| Returning `delay()` / System ON sleep | **8.7 uA** | [Discussion #94 v0.9.222 trace](https://github.com/lolren/nrf54-arduino-core/discussions/94#discussioncomment-17608738) |
| `delaySystemOffNoRetention()` | **3.1 uA** | [Discussion #94 v0.9.222 trace](https://github.com/lolren/nrf54-arduino-core/discussions/94#discussioncomment-17608738) |
| nPM1300 hibernate in the earlier `v0.9.59` test | **0.5 uA** | [Discussion #94 hibernate trace](https://github.com/lolren/nrf54-arduino-core/discussions/94) |

[![PPK2 trace of XIAO nRF54LM20A v0.9.222 returning delay at 8.7 uA and no-retention System OFF at 3.1 uA](https://github.com/user-attachments/assets/f66e453b-03ba-4de2-87d8-1384dbe4b260)](https://github.com/lolren/nrf54-arduino-core/discussions/94#discussioncomment-17608738)

*Latest posted LM20A core trace, measured with `v0.9.222`: returning
`delay()` is labelled 8.7 uA and `delaySystemOffNoRetention()` is labelled
3.1 uA. The graph's 2.75 uA board-component total is an estimate rather than
a separately measured rail. [Discussion #94 source](https://github.com/lolren/nrf54-arduino-core/discussions/94#discussioncomment-17608738).*

[![PPK2 trace of XIAO nRF54LM20A v0.9.59 entering nPM1300 hibernate at 0.5 uA and waking](https://github.com/user-attachments/assets/2966f283-3c47-486f-b934-4e5a0bb681a3)](https://github.com/lolren/nrf54-arduino-core/discussions/94)

*Earlier LM20A `v0.9.59` hibernate capture: the test sequence labels
returning `delay(2000)` at 7.2 uA and nPM1300 hibernate at 0.5 uA before the
board wakes. It documents that test, not a guarantee for every board and
measurement setup. Both LM20A images and measurements are by
[@msfujino in Discussion #94](https://github.com/lolren/nrf54-arduino-core/discussions/94).*

The `v0.9.222` graph estimates the always-present LM20A board component floor at
about **2.75 uA** before measurement and environmental effects. Hibernate is a
PMIC-controlled cold-boot path; it is not interchangeable with a returning
`delay()`.

### Selecting A Sleep Path

| Requirement | API / pattern | Wake behavior |
|---|---|---|
| Continue after a timed idle | `delayLowPowerIdle(ms)` or normal low-power `delay(ms)` | Returns to the next statement |
| Lowest CPU System OFF with retained RAM banks | `delaySystemOff(ms)` | Cold reset; explicitly retained `.noinit` state may survive |
| System OFF without RAM retention | `delaySystemOffNoRetention(ms)` | Cold reset |
| Timed LM20A cold boot (lowest-current path on VBAT) | `npm1300_enter_timed_hibernate_ms(ms)` | PMIC wake on VBAT or GRTC System OFF wake on USB; both cold-reset |

Before entering System OFF, send any device-specific sleep command required by
external I2C or SPI peripherals. The core quiesces its own controllers, but it
cannot infer how an attached sensor should enter its lowest-power state. Also
ensure that configured GPIO wake inputs are inactive before entry; an already
asserted wake level legitimately restarts the board immediately.

For reproducible measurements, power from VBAT through a PPK2/Joulescope/Otii,
disconnect USB/VBUS, close serial/debug sessions, keep voltage and temperature
constant, wait for the board to settle, and record both average and peak current
over multiple runs. The full repeatable workflow and blank measurement matrix
are in [Power Profile Measurements](POWER_PROFILE_MEASUREMENTS.md).

---

## SPI Speed And Routing

| Board family | External Arduino `SPI` pins | Implemented max on exposed pins | `SPI_HS` / 32 MHz status |
|---|---|---|---|
| **XIAO nRF54L15 / Sense** | `D8=SCK`, `D9=MISO`, `D10=MOSI`, `D2=SS` on the dedicated P2 `SPIM00` route | Up to 32 MHz from SPIM00's fixed 128 MHz source | `SPI_HS` uses the same physical pins and does not change the CPU clock profile |
| **HOLYIOT-25007 / 25008 / nRF54L15 module boards** | Board/module `D8/D9/D10/D2` route on dedicated P2 `SPIM00` | Up to 32 MHz from SPIM00's fixed 128 MHz source | `SPI_HS` uses the same physical P2.01/P2.04/P2.02 route without changing the CPU clock profile |
| **XIAO nRF54LM20A / Sense** | `D8=SCK`, `D9=MISO`, `D10=MOSI`, `D2=SS` on a serial-fabric SPIM | 8 MHz on the exposed XIAO header pins | `SPI_HS` uses `SPIM00`, but those pins are the onboard PY25Q64 QSPI flash bus, not the XIAO header |

Notes:

- On **nRF54L15**, the exposed Arduino SPI pins can only use `SPIM00`. `SPI` and `SPI_HS` are separate logical objects that retain separate settings, but share that one physical controller and must be used sequentially.
- On **nRF54L15**, SPIM00 has a fixed 128 MHz peripheral source, so `SPI_HS` can request 32 MHz without changing the CPU clock profile.
- L15 SPIM00 cannot clock below about 1.016 MHz; lower requests clamp to that documented minimum rather than using nonexistent divider values.
- On **LM20A**, external BMP388/SD/MCP2515-style devices should use normal `SPI` on `D8/D9/D10`; that path is working but is limited to 8 MHz by the board/peripheral route.
- On **L15**, `SPI_HS` is plain 1-bit SPI on the P2 high-speed route, not Quad SPI.
- On **XIAO L15**, `SPI_HS` defaults to `D2` for software-controlled chip select. P2.05 remains reserved for the RF switch.
- On **LM20A**, the 32 MHz `SPI_HS` path is useful for the onboard QSPI flash and deliberate advanced probing of the flash pads. The schematic does not expose that HS bus on the normal XIAO header.
- The L15 implementation follows the documented P2 high-speed pad requirements, including E0/E1 output drive, maximum `HSBIAS` slew above 8 MHz, and the nRF54L SPIM anomaly 8 workaround.
- Examples: `File > Examples > SPI > HighSpeedSpi32MHzProbe` and `File > Examples > nRF54 Board Examples > XIAO-nRF54LM20A`, where the onboard-flash sketches are grouped with the board that provides the hardware.

## XIAO nRF54LM20A Onboard QSPI Flash

XIAO nRF54LM20A includes an onboard PY25Q64-class 8 MB flash on the dedicated QSPI/HS-SPI pads. Nordic DK-style MX25R6435F QSPI flash is also recognized by the bundled SPIFlash compatibility layer. The core exposes this in two layers:

- `XiaoQspiFlash` for board-specific low-level control, including JEDEC read, read/write/erase, and `prepareForSleep()`.
- `Adafruit_SPIFlash` compatibility with `Adafruit_FlashTransport_QSPI_NRF54`, so sketches can use common `begin()`, `readBuffer()`, `writeBuffer()`, `eraseSector()`, `readJEDECID()`, and `runCommand(0xB9)` style calls.

For low-current sleep on LM20A, put the external flash into deep power-down before sleeping. Use `XiaoQspiFlash.prepareForSleep()` or `flash.runCommand(0xB9); flash.end();` from the SPIFlash-compatible API.

Examples:

- `File > Examples > nRF54 Board Examples > XIAO-nRF54LM20A > QspiFlashInfo`
- `File > Examples > nRF54 Board Examples > XIAO-nRF54LM20A > QspiFlashReadWrite`
- `File > Examples > nRF54 Board Examples > XIAO-nRF54LM20A > FlashInfo`
- `File > Examples > Bluefruit52Lib > Diagnostics > lm20a_spiflash_sleep_adv`

---

## Timed System Off APIs

- `delayLowPowerIdle(ms)` is the returning System ON sleep API; the sketch continues at the next statement.
- `delaySystemOff(ms)` enters real timed `SYSTEMOFF` with RAM retention enabled. Wake is a cold reset, so only explicitly retained data such as `.noinit` can survive and execution starts again from `setup()`.
- `delaySystemOffNoRetention(ms)` enters real timed `SYSTEMOFF` after disabling RAM retention. It also cold-resets and never returns.
- `systemOffWakeReset(ms)` is the compatibility spelling for timed no-retention System OFF.
- `nrf54ResetReason()`, `nrf54ClearResetReason(mask)`, `wasSystemOffWakeReset()`, `wasSystemOffWakeFromGrtc()`, and `clearSystemOffWakeResetReason()` expose the reset-cause snapshot captured before constructors. See `File > Examples > Power > SystemOffWakeReset`.

## XIAO nRF54LM20A nPM1300 Charging Notes

- `npm1300_charger_set_current(ma)` sets the battery charge-current target and now also updates the nPM1300 VBUS input-current limiter so the default 100 mA input limit does not throttle higher charge currents.
- `npm1300_vbus_set_input_current_limit_ma(ma)` and `npm1300_vbus_get_input_current_limit_ma()` expose the VBUS limiter directly. This limit is the allowed USB/VBUS input draw, not the measured battery charge or discharge current.
- `npm1300_enter_timed_hibernate_ms(ms)` provides one timed cold-boot API for either supply path. With VBUS absent it programs the nPM1300 wake timer and enters true PMIC Hibernate, the lowest-current LM20A path. With USB/VBUS present it uses nRF54 GRTC System OFF because the nPM1300 requires VBUS to be disconnected before Hibernate entry. Both paths accept 1 through `NPM1300_HIBERNATE_TIMER_MAX_MS`, normally do not return after successful entry, and restart from `setup()` on wake. Disconnect USB/debug wiring and power from VBAT when measuring the sub-uA PMIC path.
- Use `File > Examples > Nrf54L15-Clean-Implementation > PMIC > nPM1300_ChargerControl` or `File > Examples > Power > nPM1300_BatteryCurrent` to compare `IBAT` with the configured `VBUS_ILIM`.

---

## Stack Maturity

| Area | Current user-facing status | Standards complete? | Formally qualified? |
|---|---|---|---|
| **Arduino core and common peripherals** | Supported on the listed boards within the board-specific limits above | Not applicable | Not applicable |
| **Bluetooth LE** | Supported for the documented practical single-active-link scope | **No**; optional controller features and broad conformance remain | **No** Bluetooth SIG qualification |
| **IEEE 802.15.4 radio/MAC primitives** | Useful for raw radio, OpenThread PAL work and experimental Zigbee | **No** complete reusable MAC service | **No** RF/MAC platform qualification |
| **Zigbee** | Experimental device and coordinator demonstrations | **No** Zigbee PRO runtime | **No** compliant-platform or product certification |
| **Thread** | Experimental OpenThread platform port with local two-board gates | **No** external/multi-hop/conformance completion | **No** Thread certification |
| **Matter** | Experimental platform foundations and private protocol demos | **No** standard device runtime or commissioning | **No** CSA Matter certification |
| **Bluetooth LE Channel Sounding** | Experimental standalone two-board CS Test | **No** connected product path | **No** Bluetooth qualification |
| **NFC-A** | Partial low-level tag path; board antenna dependent | **No** broad reader/tag interoperability claim | **No** NFC Forum certification |
| **VPR RISC-V / SoftPeripheral** | Partial boot, IPC and offload path; sQSPI remains experimental | Not applicable | Not applicable |
| **nPM1300 and board power integration** | Supported on LM20A for the documented charger, telemetry and hibernate APIs | Not applicable | Not applicable |

---

## Known Limitations

- **P-256 arithmetic is software-only for BLE Secure Connections and the staged Matter paths.** CRACEN supplies fail-closed entropy. Nordic publishes PKE microcode and its NCS driver for CRACEN Base devices under the [Nordic five-clause license](https://github.com/nrfconnect/sdk-nrf/blob/v3.3.0/LICENSE), but this core does not integrate them. BLE Secure Connections key generation and private Matter session operations can therefore consume substantial CPU time.
- **Thread and Matter remain staged.** Thread uses the imported OpenThread core with platform persistence/radio integration. Reported settings API failures retain the prior mapping, but the shared Preferences RRAM blob is not an atomic brownout transaction. Matter currently combines selected upstream CHIP platform units with project-specific onboarding, PASE/CASE, and command-surface code; those custom messages are not a substitute for the standard Matter Secure Channel and Interaction Model. Home Assistant/OTBR commissioning must not be claimed until it passes against an external border router and Matter controller.
- **Selected Zigbee examples are functional within the documented direct-device paths, but the stack is incomplete.** Many ZCL clusters, OTA, and automatic route maintenance / production multi‑hop routing are still missing. ZDO neighbor/routing management responses are available and can expose sketch-configured table entries. A Zigbee2MQTT external converter for the bundled CleanCore HA examples is in `extras/zigbee2mqtt/`.
- **LM20A has two SPI paths:** `SPI` stays on the XIAO header pins; `SPI_HS` is the onboard QSPI flash bus and is only for deliberate HS-SPI/QSPI-pad use.
- **P2 GPIO port has no interrupt/wake capability** (hardware limitation).
- **Channel Sounding has one supported experimental path:** the
  `BleChannelSoundingInitiator` / `BleChannelSoundingReflector` pair. Nordic
  SDC/MPSL executes LE CS Test and emits HCI CS result events. A separate
  CRC-protected Arduino protocol establishes a per-cycle session token before
  the test and returns a session-correlated reflector step buffer after SDC
  releases RADIO. That transport is not a connected ACL Channel Sounding
  procedure. Bluetooth SIG
  qualification, cross-vendor interoperability, calibrated accuracy, and
  production power figures are not claimed.

### Channel Sounding Setup

Select **Tools -> CPU Frequency -> 128 MHz** before building either Channel
Sounding example. The equivalent Arduino CLI option is `cpu_freq=128m`; the
runtime fails explicitly instead of starting SDC/MPSL with the default 64 MHz
profile.

The only public Channel Sounding examples are:

- `BleChannelSoundingInitiator`
- `BleChannelSoundingReflector`

The supported profile is single-antenna Mode 2/Submode 1 with AA-only RTT.
The test uses a proprietary CRC-protected session/result exchange before and
after the controller-owned sounding operation. It is intentionally separate
from the core's normal connected BLE stack.

The controller binaries are Nordic components, revision
`7a07f89ee8c32658ebfd2034b4cae92fde63e122`
(`v3.4.0-rc1-12-g7a07f89ee`). They are covered by the included
[Nordic 5-Clause license](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/LICENSE),
not the core's MIT license. The license restricts use to Nordic Semiconductor
integrated circuits and prohibits reverse engineering, decompiling, modifying,
or disassembling the binary software. See the
[attribution notice](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/LICENSE-ATTRIBUTION.txt)
and [version record](hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/VERSION).

---

## PWM (`analogWrite`)

### Pin Allocation (LM20A)

| Instance | Channels | Pins |
|---|---|---|
| **PWM20** | 0–3 | D0, D1, D2, D3 |
| **PWM21** | 0–3 | D4, D5, D6, D7 |
| **PWM22** | 0–3 | D8, LED_R (28), LED_B (29), LED_G (30) |
| Software | — | D9–D15 |

D0-D8 and all three onboard RGB LEDs use dedicated PWM-peripheral channels by
default. D9-D15 use the software fallback.

### Frequency Control

- **`analogWriteFrequency(hz)`** — Sets the shared base frequency for all
  hardware PWM instances (1 kHz default).
- **`analogWritePinFrequency(pin, hz)`** — Per-pin frequency selection. D0-D5
  can use the dedicated timer + DPPI + GPIOTE path. Five timer slots provide up
  to five distinct hardware-timed frequencies; pins using the same frequency
  can share a slot. Later pins use the software fallback.
- Pins without a custom frequency inherit their instance's shared frequency.

### Examples

```cpp
analogWriteResolution(8);

// Same frequency on shared instance (PWM20):
analogWrite(D0, 128);   // 50% duty, 1 kHz
analogWrite(D1, 64);    // 25% duty, 1 kHz

// Different frequencies per pin (timer-backed):
analogWritePinFrequency(D0, 500);   // 500 Hz
analogWritePinFrequency(D1, 2000);  // 2 kHz
analogWrite(D0, 128);
analogWrite(D1, 128);
```

### Limitations

- Pins sharing the same PWM instance run at the same base frequency unless `analogWritePinFrequency()` is used.
- Per-pin timer-backed PWM is limited to D0-D5 and five timer slots. D6 and
  later use the software path for a custom per-pin frequency.
- **D9-D15** use the CPU-driven software PWM fallback.
- D8-D10 share pins with SPI (P1.04-P1.06). When SPI is active, those pin
  functions are unavailable for PWM output.

## Troubleshooting

### Experimental J-Link upload cannot find a probe

Run `pyocd list --probes`. If no SEGGER probe appears, install or update the
official J-Link driver package and reconnect the probe. The pyOCD Python package
contains the J-Link plug-in, but it cannot communicate with hardware without
SEGGER's host driver/library. Do not select the J-Link upload method for the
XIAO's normal CMSIS-DAP bridge. For a standalone model without VCOM, use
**Sketch -> Upload Using Programmer** with the experimental J-Link programmer.

### Linux: Upload fails with "hidraw access denied"

```text
ERROR: CMSIS-DAP probe 2886:0068 is present but hidraw access is denied.
```

**Fix:** Copy the udev rules and replug the board:

```bash
sudo cp ~/.arduino15/packages/nrf54l15clean/tools/nrf54l15hosttools/*/setup/60-seeed-xiao-nrf54-cmsis-dap.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# Unplug and replug the board
```

> This is a one-time setup. The udev rule grants plugdev group access to `/dev/hidraw*` and the USB device node. Covers both L15 (0066) and LM20A (0068).

### Linux Mint: No probe detected even after udev rules

If `pyocd list` shows no probes but `lsusb` shows the board, check:

```bash
ls -la /dev/bus/usb/*/???   # USB device should be rw for plugdev
ls -la /dev/hidraw*          # hidraw should be rw for plugdev
groups                       # verify you're in plugdev group
```

If USB device is root-only, the udev rule didn't trigger. Replug the board or run `sudo udevadm trigger`.

### XIAO nRF54LM20A disappeared after BUCK2 control

On core versions through `1.0.15`, `npm1300_buck2_enable(false)` can turn off
the `VSYS_3V3` rail that powers both the nRF54LM20A and its SAMD11 USB/debug
bridge. Version `1.0.16` and later block this request and BUCK2 voltage changes
by default.

To recover an affected board, disconnect both USB and the battery so the PMIC
fully loses power, then reconnect USB. If the battery cannot be disconnected,
connect only the paired back pads `TP21` (`SHPHLD`) and `TP22` (GND) for
more than 10 seconds while the board is powered. This requests the nPM1300
whole-system power cycle and restores the strapped 3.3 V BUCK2 state.

If the installed sketch disables BUCK2 again at startup, hold the nRF54 RESET
button while restoring PMIC power, select **Tools -> Upload Method -> pyOCD
Recovery**, and replace the sketch using its connect-under-reset path. The
[official Seeed schematic](https://files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/XIAO_nRF54LM20A_Schematic.pdf)
identifies the paired pads. Do not bridge any other test pads.

---

## Documentation

- **[Board Reference & Pinouts](docs/board-reference.md)**
- **[Development Guide](docs/development.md)**
- **[BLE Status & Resume Checklist](docs/BLE_COMPLIANCE_RESUME.md)**
- **[Two-Board Release Gate](docs/TWO_BOARD_RELEASE_GATE.md)**
- **[Zigbee2MQTT Integration](docs/ZIGBEE2MQTT_INTEGRATION.md)**
- **[Zigbee Full-Support Handoff](docs/ZIGBEE_FULL_SUPPORT_HANDOFF.md)**
- **[Channel Sounding Status](docs/CHANNEL_SOUNDING_CURRENT_STATUS.md)**
- **[Thread & Matter Implementation Plan](docs/archive/THREAD_MATTER_IMPLEMENTATION_PLAN.md)**
- **[Thread & Matter Hardening Status](docs/THREAD_MATTER_FINISH_PLAN.md)**
- **[Power Profile Measurements](POWER_PROFILE_MEASUREMENTS.md)**

## License

Project-owned contributions are available under the [MIT License](LICENSE).
Imported and adapted components retain their own terms; the
[third-party notice](THIRD_PARTY_NOTICES.md) identifies the principal bundled
licenses. The Board Manager archive includes the same project license, detailed
platform notice, and component license texts so installed copies remain
self-describing.

## Special Thanks

This core would not have been possible without the invaluable help of:

- **[msfujino](https://github.com/msfujino)** — Extensive testing, debugging, and feedback across PWM, analogWrite, and countless other areas. Every oscilloscope trace, every edge case report, every patient retest helped shape this core into what it is today.
- **[lyusupov](https://github.com/lyusupov)** — Deep technical contributions, critical bug fixes, and ongoing collaboration that pushed this project forward.

Thank you both for your time, expertise, and dedication. ❤️

## Adafruit Attribution

[![Adafruit](https://img.shields.io/badge/Adafruit-Open%20Source%20Examples-000000?logo=adafruit&logoColor=white)](https://www.adafruit.com/)

Some bundled Bluefruit52Lib examples, HID sketches, TinyUSB compatibility code, and SPIFlash-compatible APIs preserve Adafruit open-source example text, naming, and API compatibility. The Adafruit name and logo are shown here to acknowledge that origin and to make clear why Adafruit attribution appears in redistributed example sketches.

---

<div align="center">

**Bare-metal Arduino for nRF54L, with clearly identified and separately licensed
third-party components.**

</div>

## Support The Project

If this core saves you development time, please consider supporting its ongoing
maintenance, hardware testing, and release work.

<a href="https://buymeacoffee.com/lolren"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" height="50"></a>
