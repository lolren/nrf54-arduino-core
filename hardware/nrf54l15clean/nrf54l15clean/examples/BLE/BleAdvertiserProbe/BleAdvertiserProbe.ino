#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

BleRadio gBle;
PowerManager gPower;
constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
constexpr int8_t kTxPowerDbm = 0;

void ledOn() {
  (void)Gpio::write(kPinUserLed, false);
}

void ledOff() {
  (void)Gpio::write(kPinUserLed, true);
}

void pulse(uint8_t count, uint16_t onMs = 80U, uint16_t offMs = 140U) {
  for (uint8_t i = 0; i < count; ++i) {
    ledOn();
    delay(onMs);
    ledOff();
    delay(offMs);
  }
}

[[noreturn]] void failStage(uint8_t stage) {
  ClockControl::stopHfxo();
  while (true) {
    pulse(stage, 90U, 180U);
    delay(1000);
  }
}

void configureBoard() {
  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  ledOff();

  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  // Probe example keeps the RF path continuously enabled to remove one class
  // of variables while debugging BLE bring-up.
  BoardControl::enableRfPath(kAntennaPath);
}

}  // namespace

void setup() {
  configureBoard();
  pulse(1, 40U, 120U);

  gPower.setLatencyMode(PowerLatencyMode::kLowPower);

  // 0 dBm is chosen here as a practical default for bring-up. The point of
  // this sketch is stage-by-stage diagnostics, not minimum current.
  bool ok = gBle.begin(kTxPowerDbm);
  if (!ok) {
    failStage(2);
  }

  ok = gBle.setAdvertisingName("X54-BLE-BASE", true);
  if (!ok) {
    failStage(3);
  }

  ok = gBle.buildAdvertisingPacket();
  if (!ok) {
    failStage(4);
  }

  pulse(2, 35U, 100U);
}

void loop() {
  const bool ok = gBle.advertiseEvent(350U, 700000UL);
  if (!ok) {
    failStage(5);
  }

  // The short LED pulse is just a liveness cue. It is not part of the BLE path.
  pulse(1, 15U, 30U);
  delay(200);
}
