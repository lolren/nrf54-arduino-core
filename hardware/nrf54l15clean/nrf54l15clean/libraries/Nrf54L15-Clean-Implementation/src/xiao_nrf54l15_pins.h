#pragma once

#include <stdint.h>

namespace xiao_nrf54l15 {

struct Pin {
  uint8_t port;
  uint8_t pin;
};

constexpr Pin kPinDisconnected{0xFF, 0xFF};

inline constexpr bool isConnected(const Pin& p) {
  return p.port <= 2U && p.pin <= 31U;
}

// XIAO 14-pin header from schematic page "04 Debug & XIAO Header".
constexpr Pin kPinD0{1, 4};
constexpr Pin kPinD1{1, 5};
constexpr Pin kPinD2{1, 6};
constexpr Pin kPinD3{1, 7};
constexpr Pin kPinD4{1, 10};  // SDA on default header bus
constexpr Pin kPinD5{1, 11};  // SCL on default header bus
constexpr Pin kPinD6{2, 8};   // TX
constexpr Pin kPinD7{2, 7};   // RX
constexpr Pin kPinD8{2, 1};   // SCK
constexpr Pin kPinD9{2, 4};   // MISO
constexpr Pin kPinD10{2, 2};  // MOSI

// Back pads from schematic page "04 Debug & XIAO Header".
constexpr Pin kPinD11{0, 3};
constexpr Pin kPinD12{0, 4};
constexpr Pin kPinD13{2, 10};
constexpr Pin kPinD14{2, 9};
constexpr Pin kPinD15{2, 6};

// Analog aliases used by Arduino sketches on XIAO style boards.
constexpr Pin kPinA0 = kPinD0;  // P1.04 / AIN0
constexpr Pin kPinA1 = kPinD1;  // P1.05 / AIN1
constexpr Pin kPinA2 = kPinD2;  // P1.06 / AIN2
constexpr Pin kPinA3 = kPinD3;  // P1.07 / AIN3
constexpr Pin kPinA4 = kPinD4;  // P1.10 (not SAADC-capable on this package)
constexpr Pin kPinA5 = kPinD5;  // P1.11 / AIN4
constexpr Pin kPinA6{1, 13};    // P1.13 / AIN6 (pad)
constexpr Pin kPinA7{1, 14};    // P1.14 / AIN7 (pad)

// Onboard controls and utility nets from schematic page "06 Peripherals".
constexpr Pin kPinUserLed{2, 0};     // Active low (anode tied to 3V3)
constexpr Pin kPinUserButton{0, 0};  // Active low with external pull-up

// USB bridge UART nets from schematic page "04 Debug & XIAO Header".
// - SAMD11_TX drives nRF RX (P1.08)
// - SAMD11_RX receives nRF TX (P1.09)
constexpr Pin kPinSAMD11Tx{1, 8};
constexpr Pin kPinSAMD11Rx{1, 9};

// Battery measurement control from schematic page "03 Power".
constexpr Pin kPinVbatEnable{1, 15};
constexpr Pin kPinVbatSense{1, 14};

// Optional IMU bus pads (Sense variant footprint from schematic page "06 Peripherals").
constexpr Pin kPinImuScl{0, 3};
constexpr Pin kPinImuSda{0, 4};
constexpr Pin kPinImuInt{0, 2};
constexpr Pin kPinImuMicPowerEnable{0, 1};

// Optional PDM microphone pads (Sense variant footprint from schematic page "06 Peripherals").
constexpr Pin kPinMicClk{1, 12};
constexpr Pin kPinMicData{1, 13};

// RF switch control from schematic page "05 nRF54L15".
// 0 -> RF1 (onboard chip antenna), 1 -> RF2 (alternate path).
constexpr Pin kPinRfSwitchPower{2, 3};
constexpr Pin kPinRfSwitchCtl{2, 5};

// Default peripheral pin groups used in the HAL examples.
constexpr Pin kDefaultI2cScl = kPinD5;
constexpr Pin kDefaultI2cSda = kPinD4;
constexpr Pin kDefaultSpiSck = kPinD8;
constexpr Pin kDefaultSpiMosi = kPinD10;
constexpr Pin kDefaultSpiMiso = kPinD9;
constexpr Pin kDefaultUartTx = kPinD6;
constexpr Pin kDefaultUartRx = kPinD7;
constexpr Pin kDefaultUsbBridgeUartTx = kPinSAMD11Rx;
constexpr Pin kDefaultUsbBridgeUartRx = kPinSAMD11Tx;

inline constexpr int8_t saadcInputForPin(const Pin& pin) {
  if (pin.port != 1U) {
    return -1;
  }
  switch (pin.pin) {
    case 4:
      return 0;  // AIN0
    case 5:
      return 1;  // AIN1
    case 6:
      return 2;  // AIN2
    case 7:
      return 3;  // AIN3
    case 11:
      return 4;  // AIN4
    case 12:
      return 5;  // AIN5
    case 13:
      return 6;  // AIN6
    case 14:
      return 7;  // AIN7
    default:
      return -1;
  }
}

}  // namespace xiao_nrf54l15
