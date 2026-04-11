# HOLYIOT-25007 Module Reference

This page documents the shared 36-pad module variant used by:

- `HOLYIOT-25007 nRF54L15 Module`
- `Generic nRF54L15 Module (36-pad)`

## Images

<p>
  <img src="boards/holyiot_25007_product.png" alt="HOLYIOT-25007 product photo" width="180" />
</p>

![HOLYIOT-25007 bottom pin map](boards/holyiot_25007_bottom.png)

![HOLYIOT-25007 quick reference](boards/holyiot_25007_peripheral_pinout.png)

## Naming Rule

Use three different names for three different jobs:

- `P2_08`, `P1_10`, `P0_03`: MCU GPIO identity in code and schematics
- `D6`, `D4`, `D11`: Arduino aliases in sketches
- `pad 25`, `pad 3`, `pad 33`: physical module pads for soldering only

Example:

- `pad 25 = P2.08 = P2_08 = D6`

## Default Peripheral Routes

| Peripheral | Default pins |
|---|---|
| `Serial` | `TX=D6/P2.08/pad 25`, `RX=D7/P2.07/pad 24` |
| `Serial1` | same default pins as `Serial` |
| `Wire` | `SDA=D4/P1.10/pad 3`, `SCL=D5/P1.11/pad 5` |
| `Wire1` | `SDA=D12/P0.04/pad 34`, `SCL=D11/P0.03/pad 33` |
| `SPI` | `SS=D2/P1.06/pad 14`, `SCK=D8/P2.01/pad 18`, `MISO=D9/P2.04/pad 21`, `MOSI=D10/P2.02/pad 19` |
| `LED_BUILTIN` | `P2.00/pad 17` as a separate compatibility LED pin |

## Arduino Pin Map

| Arduino pin | MCU pin | Module pad | Notes |
|---|---|---:|---|
| `D0` | `P1.04` | `12` | `A0` |
| `D1` | `P1.05` | `13` | `A1` |
| `D2` | `P1.06` | `14` | `SPI SS` |
| `D3` | `P1.07` | `15` | `A3` |
| `D4` | `P1.10` | `3` | `Wire SDA` |
| `D5` | `P1.11` | `5` | `Wire SCL` |
| `D6` | `P2.08` | `25` | `Serial TX` |
| `D7` | `P2.07` | `24` | `Serial RX` |
| `D8` | `P2.01` | `18` | `SPI SCK` |
| `D9` | `P2.04` | `21` | `SPI MISO` |
| `D10` | `P2.02` | `19` | `SPI MOSI` |
| `D11` | `P0.03` | `33` | `Wire1 SCL` |
| `D12` | `P0.04` | `34` | `Wire1 SDA` |
| `D13` | `P2.10` | `27` | GPIO |
| `D14` | `P2.09` | `26` | GPIO |
| `D15` | `P2.06` | `23` | GPIO |
| `D16` | `P1.02` | `10` | module-only extra GPIO |
| `D17` | `P1.03` | `11` | module-only extra GPIO |
| `LED_BUILTIN` | `P2.00` | `17` | separate Blink/demo pad, not `D13` |
| `BUTTON` | `P0.00` | `28` | compatibility button pin |
| `SAMD11_RX` | `P1.09` | `2` | XIAO compatibility alias |
| `SAMD11_TX` | `P1.08` | `16` | XIAO compatibility alias |
| `IMU_MIC_PWR` | `P0.01` | `29` | compatibility alias |
| `IMU_INT` | `P0.02` | `32` | compatibility alias |
| `PDM_CLK` | `P1.12` | `6` | compatibility alias |
| `A6` | `P1.13` | `7` | analog |
| `A7` | `P1.14` | `8` | analog / `VBAT_READ` |

Analog aliases:

- `A0=P1.04/D0`
- `A1=P1.05/D1`
- `A2=P1.06/D2`
- `A3=P1.07/D3`
- `A4=P1.10/D4`
- `A5=P1.11/D5`
- `A6=P1.13`
- `A7=P1.14`

## Built-in LED Behavior

The bare module variant sets:

- `LED_BUILTIN = P2.00 = pad 17`

That is a practical default for `Blink`, external LED bring-up, and test clips.
It does **not** mean the bare module has a guaranteed onboard LED on that pin.

## External Programmer Note

The module boards default to:

- `Upload Method = pyOCD (CMSIS-DAP, Default)`

That path is validated with Raspberry Pi Pico Debugprobe.

Expected Pico Debugprobe wiring:

- `GP2 -> SWCLK`
- `GP3 -> SWDIO`
- `VTREF -> VDD`
- `GND -> GND`
- optional: `GP1 -> nRESET`

Optional UART bridge wiring for `Tools > Serial Routing = Header UART`:

- target `D6/P2.08` -> Pico `GP5`
- target `D7/P2.07` -> Pico `GP4`
- common `GND`

## XIAO Compatibility Notes

The module variants intentionally keep the XIAO Arduino numbering on the shared
GPIO set so XIAO sketches can usually be rebuilt without source edits.

Board-specific XIAO helper symbols are also kept available so sketches still
compile, but unsupported RF antenna helpers do not drive any module pin:

- `xiaoNrf54l15SetAntenna(...)` is a harmless no-op on the module variants
- `arduinoXiaoNrf54l15SetAntenna(...)` returns `0` to report unsupported RF switch control
