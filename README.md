# Nrf54L15-Clean-Implementation

Open-source Arduino core package for **Seeed XIAO nRF54L15** using a clean register-level implementation (no Zephyr/NCS runtime dependency).

## What this package contains

- Core name: **Nrf54L15-Clean-Implementation**
- Board: `XIAO nRF54L15 (Nrf54L15-Clean-Implementation)`
- Architecture/FQBN namespace: `nrf54l15clean:nrf54l15clean`
- Core + variant + linker/startup files
- Built-in library bundle: `Nrf54L15-Clean-Implementation`
- Upload helper supporting `pyocd` and `openocd`
- Core Arduino API parity additions:
  - `attachInterrupt()`/`detachInterrupt()` via GPIOTE IRQs (`RISING`, `FALLING`, `CHANGE`)
  - hardware `analogWrite()` PWM on `D6..D9` with auto-stop when no channel needs waveform output
  - `Wire` repeated-start support via `endTransmission(false)` and `requestFrom(..., sendStop)`
  - `Wire` target/slave mode via `begin(address)`, `onReceive()`, and `onRequest()` on TWIS IRQ path
- Extended BLE peripheral interoperability:
  - broader LL control opcode handling
  - LL connection update/channel-map instant validation and safer retransmission gating
  - GATT Service Changed characteristic + CCCD + indication/confirmation flow
  - GATT Battery Level CCCD + notification flow
  - ATT Read Multiple + Find By Type Value support
  - stricter ATT Read By Group Type edge-case error behavior
  - L2CAP LE signaling fallback responses (`Command Reject`, `Conn Param Update Response`)
  - improved scan cycle robustness
- Arduino Tools menu options for:
  - Upload method (`Auto`, `pyOCD`, `OpenOCD`)
  - CPU frequency (`64 MHz`, `128 MHz`)
  - BLE support (`Enabled`, `Disabled`)
  - Power profile (`Balanced`, `Low Power/WFI idle`)
  - Antenna (`Ceramic`, `External U.FL`)
  - Serial routing (`USB bridge on Serial`, `Header UART on Serial`)

## Project tracking docs

- `FEATURE_PARITY.md` : current parity status, implemented scope, and known gaps.
- `TODO.md` : prioritized backlog for parity, security, power, and DX work.

## Automation

- CI workflow: `.github/workflows/ci.yml`
  - Compile matrix over representative BLE and low-power sketches.
  - Package/index consistency validation (checksum, size, index sync).
- Release workflow: `.github/workflows/release.yml`
  - On `vX.Y.Z` tags, builds release artifacts and publishes them to GitHub Releases.

## Folder layout

- `hardware/nrf54l15clean/0.1.0/` : installable Arduino platform payload
- `package_nrf54l15clean_index.json` : Boards Manager index file template
- `scripts/build_release.py` : creates release archive + index with checksum/size

## Quick local test (without publishing)

From this repository root:

```bash
mkdir -p ~/Arduino/hardware/nrf54l15clean
ln -sfn "$PWD/Nrf54L15-Clean-BoardPackage/hardware/nrf54l15clean/0.1.0" \
  ~/Arduino/hardware/nrf54l15clean/nrf54l15clean

arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  ./Nrf54L15-Clean-BoardPackage/hardware/nrf54l15clean/0.1.0/examples/03.Peripherals/InterruptPwmApiProbe/InterruptPwmApiProbe.ino
```

## Publish for Arduino Boards Manager (GitHub)

1. Push this package to your GitHub repo.
2. Build release artifacts:

```bash
cd Nrf54L15-Clean-BoardPackage
./scripts/build_release.py \
  --repo-url "https://github.com/<user>/<repo>" \
  --release-base-url "https://github.com/<user>/<repo>/releases/download/v{version}"
```

This generates:

- `dist/nrf54l15clean-0.1.0.tar.bz2`
- `dist/package_nrf54l15clean_index.json`

3. Create GitHub release tag `v0.1.0` and upload:
- `dist/nrf54l15clean-0.1.0.tar.bz2`

4. Commit/publish `dist/package_nrf54l15clean_index.json` (or copy it to repo root as `package_nrf54l15clean_index.json`).

5. Users add this URL in Arduino IDE / Arduino CLI additional Boards Manager URLs:

```text
https://raw.githubusercontent.com/<user>/<repo>/main/package_nrf54l15clean_index.json
```

Then install package `Nrf54L15-Clean-Implementation` and compile/upload.

## Toolchain and uploader dependencies

The package index declares:

- `arduino:arm-none-eabi-gcc` `7-2017q4`
- `arduino:openocd` `0.11.0-arduino2`

For pyOCD upload mode, install pyOCD on host:

```bash
pip install pyocd
```
