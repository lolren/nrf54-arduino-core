# HOLYIOT-25008 Bring-Up Notes

This note captures the currently available `HOLYIOT-25008` pin information from
the upstream Zephyr board support, so board bring-up does not depend on guesswork.

Primary source:

- Zephyr board docs:
  `https://github.com/zephyrproject-rtos/zephyr/tree/main/boards/holyiot/holyiot_25008`

Relevant upstream statements:

- UART: `P1.04` (`TX`), `P1.05` (`RX`)
- Button: `P1.13`, active low with pull-up
- RGB LED:
  - red: `P2.09`, active low
  - green: `P1.10`, active low
  - blue: `P2.07`, active low
- LIS2DH12 accelerometer (SPI):
  - `P2.01` = `SCK`
  - `P2.02` = `MOSI`
  - `P2.04` = `MISO`
  - `P2.05` = `CS`
  - `P2.00` = `INT1`
  - `P2.03` = `INT2`

What this likely means on the physical module:

- the two extra pads are very likely the console UART pads:
  - `P1.04` = `TX`
  - `P1.05` = `RX`
- the currently published upstream board support describes an `RGB` LED, not an
  `RGBW` LED

Practical next step:

- use the probe sketch under [tools/holyiot_25008_probe](/home/lolren/Desktop/Nrf54L15/NRF54L15-Clean-Arduino-core/tools/holyiot_25008_probe)
  to confirm the LED and button pins on real hardware before adding a dedicated
  board definition to the core.
