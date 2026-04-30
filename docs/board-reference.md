# XIAO nRF54L15 / Sense Reference

This page is the board reference for `XIAO nRF54L15 / Sense`.

For the module boards, use:

- [HOLYIOT-25008 Module Reference](holyiot-25008-module-reference.md)
- [HOLYIOT-25007 Module Reference](holyiot-25007-module-reference.md)

## Default Peripheral Routes

| Peripheral | Default pins | Notes |
|---|---|---|
| `Wire` | `SDA=D4(P1.10)`, `SCL=D5(P1.11)` | Dedicated `TWIM22` controller |
| `Wire1` | `SDA=D12(P0.04)`, `SCL=D11(P0.03)` | Dedicated `TWIM30` controller |
| `SPI` | `MOSI=D10(P2.02)`, `MISO=D9(P2.04)`, `SCK=D8(P2.01)`, `SS=D2(P1.06)` | Runtime clock via `SPISettings` |
| `Serial1` / `Serial2` | `TX=D6(P2.08)`, `RX=D7(P2.07)` | `Serial2` is alias of `Serial1` |
| `Serial` | USB bridge by default | Can be switched to header UART via Tools |

## Arduino Pin Map

| Arduino pin | MCU pin | ADC input | Typical role |
|---|---|---|---|
| `D0` / `A0` | `P1.04` | `AIN0` | GPIO / ADC |
| `D1` / `A1` | `P1.05` | `AIN1` | GPIO / ADC |
| `D2` / `A2` | `P1.06` | `AIN2` | GPIO / ADC / `SS` |
| `D3` / `A3` | `P1.07` | `AIN3` | GPIO / ADC |
| `D4` / `A4` | `P1.10` | N/A | `Wire SDA` |
| `D5` / `A5` | `P1.11` | `AIN4` | `Wire SCL` |
| `D6` | `P2.08` | N/A | `Serial1/2 TX` |
| `D7` | `P2.07` | N/A | `Serial1/2 RX` |
| `D8` | `P2.01` | N/A | `SPI SCK` |
| `D9` | `P2.04` | N/A | `SPI MISO` |
| `D10` | `P2.02` | N/A | `SPI MOSI` |
| `D11` | `P0.03` | N/A | Back pad / `Wire1 SCL` |
| `D12` | `P0.04` | N/A | Back pad / `Wire1 SDA` |
| `D13` | `P2.10` | N/A | Back pad GPIO |
| `D14` | `P2.09` | N/A | Back pad GPIO |
| `D15` | `P2.06` | N/A | Back pad GPIO |
| `LED_BUILTIN` (`16`) | `P2.00` | N/A | User LED (active-low) |
| `PIN_BUTTON` (`17`) | `P0.00` | N/A | User button (active-low) |
| `PIN_SAMD11_RX` (`18`) | `P1.09` | N/A | USB bridge route |
| `PIN_SAMD11_TX` (`19`) | `P1.08` | N/A | USB bridge route |
| `IMU_MIC_EN` / `IMU_MIC` (`20`) | `P0.01` | N/A | Sense IMU/MIC power enable |
| `RF_SW` (`21`) | `P2.03` | N/A | RF switch power enable |
| `RF_SW_CTL` (`22`) | `P2.05` | N/A | RF path select (`LOW=ceramic`, `HIGH=external`) |
| `VBAT_EN` (`23`) | `P1.15` | N/A | VBAT divider enable |
| `VBAT_READ` / `A7` | `P1.14` | `AIN7` | VBAT divider sense input |

## PWM Capability

| Pins | `analogWrite()` mode | Frequency control | Notes |
|---|---|---|---|
| `D0-D5` | Direct hardware PWM plus timer-backed per-pin PWM | `analogWriteFrequency(hz)` for the shared/default rate, `analogWritePinFrequency(pin, hz)` for pin-specific rate | `D0-D5` are `P1` pins, so they can use the direct nRF54L15 PWM peripheral path. Pin-specific timer-backed PWM supports all 6 `D0-D5` pins on hardware using `TIMER20-24 + TIMER10 + GPIOTE20 + DPPIC20 + DPPIC10`. Same-frequency groups pack up to 5 pins into one 16 MHz timer group, with the 6th pin moved onto a second hardware group instead of software fallback. |
| `D6-D15` | Timer-backed fallback PWM | `analogWriteFrequency(hz)` sets the shared/default fallback rate, and `analogWritePinFrequency(pin, hz)` overrides it per pin | These pins are not direct `PWM20/21/22` outputs on the XIAO. `D11-D12` use the timer + GPIOTE fallback path, and the `P2` pins (`D6-D10`, `D13-D15`) use a timer-interrupt-backed fallback path because `P2` does not expose GPIOTE on nRF54L15. The fallback path is now hardware-timed and no longer depends on sketch yielding to keep the waveform moving. |
| `LED_BUILTIN` | Not supported for `analogWrite()` PWM | N/A | The built-in LED remains outside the PWM map. |

Practical use:

- Use `analogWrite(pin, value)` on `D0-D5` for normal hardware PWM.
- Use `analogWritePinFrequency(pin, hz)` before `analogWrite(...)` when you need a different PWM frequency on a specific `D0-D5` pin.
- Treat `analogWriteFrequency(hz)` as a shared/global setting for the direct hardware PWM path and for the timer-backed fallback path.
- On `D6-D15`, `analogWritePinFrequency(pin, hz)` is valid and hardware-timed, but it is still a fallback path rather than the direct `PWM20/21/22` silicon route used on `D0-D5`.

## MCU Pin Map

| MCU pin | Arduino alias |
|---|---|
| `P0.00` | `PIN_BUTTON` |
| `P0.01` | `IMU_MIC_EN` / `IMU_MIC` |
| `P0.03` | `D11` |
| `P0.04` | `D12` |
| `P1.04` | `D0/A0` |
| `P1.05` | `D1/A1` |
| `P1.06` | `D2/A2` |
| `P1.07` | `D3/A3` |
| `P1.08` | `PIN_SAMD11_TX` |
| `P1.09` | `PIN_SAMD11_RX` |
| `P1.10` | `D4/A4` |
| `P1.11` | `D5/A5` |
| `P1.14` | `A7` / `VBAT_READ` |
| `P1.15` | `VBAT_EN` |
| `P2.00` | `LED_BUILTIN` |
| `P2.01` | `D8` |
| `P2.02` | `D10` |
| `P2.03` | `RF_SW` |
| `P2.04` | `D9` |
| `P2.05` | `RF_SW_CTL` |
| `P2.06` | `D15` |
| `P2.07` | `D7` |
| `P2.08` | `D6` |
| `P2.09` | `D14` |
| `P2.10` | `D13` |

## Board Control Helpers

```cpp
#include "nrf54l15_hal.h"
using namespace xiao_nrf54l15;

int32_t vbatMv = 0;
uint8_t vbatPct = 0;
BoardControl::sampleBatteryMilliVolts(&vbatMv);
BoardControl::sampleBatteryPercent(&vbatPct);

BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
BoardControl::setAntennaPath(BoardAntennaPath::kExternal);
BoardControl::setAntennaPath(BoardAntennaPath::kControlHighImpedance);
BoardControl::setRfSwitchPowerEnabled(false);
```

Arduino-level aliases are also available:

```cpp
pinMode(IMU_MIC_EN, OUTPUT);
pinMode(RF_SW, OUTPUT);
pinMode(RF_SW_CTL, OUTPUT);
pinMode(VBAT_EN, OUTPUT);

digitalWrite(IMU_MIC_EN, HIGH);
digitalWrite(RF_SW, HIGH);
digitalWrite(RF_SW_CTL, LOW);
digitalWrite(VBAT_EN, HIGH);
int raw = analogRead(VBAT_READ);
```

RF notes:

- `Tools > Antenna` sets the startup default only
- sketches can override the RF switch later through `BoardControl`, `digitalWrite(...)`, or direct register writes
- BLE startup will not force the Tools antenna route back over sketch changes
- `BoardControl::setAntennaPath(...)` powers `RF_SW` on so the selected route takes effect
- `BoardControl::setRfSwitchPowerEnabled(false)` or `digitalWrite(RF_SW, LOW)` powers the RF switch off
- `kControlHighImpedance` releases `P2.05` drive; it does not power-gate the RF switch IC

## Sense Examples

In the Arduino IDE, the new Sense-specific sketches are under:

- `File > Examples > Examples for XIAO nRF54L15 / Sense > XiaoSense`

Current Sense examples:

- `XiaoSenseImuWhoAmI`
- `XiaoSenseImuAccelGyro`
- `XiaoSenseMicLevel`

## Tools Menu Options

- Upload method: Auto Recover / pyOCD / OpenOCD / UF2 Bootloader (manual optional)
- CPU frequency: 64 MHz / 128 MHz
- BLE support: On / Off
- BLE timing profile: Interop / Balanced low-power / Aggressive low-power
- BLE trace: Off / On
- Zigbee support: On / Off
- Zigbee channel: `11`, `15`, `20`, `25`
- Zigbee PAN ID: `0x1234`, `0x1A2B`, `0xBEEF`
- Power profile: Balanced / Low power (WFI idle)
- Peripheral auto-gating: Off / 2 ms / 200 us
- Antenna route: Ceramic / External U.FL
- Serial routing: USB bridge / Header UART
