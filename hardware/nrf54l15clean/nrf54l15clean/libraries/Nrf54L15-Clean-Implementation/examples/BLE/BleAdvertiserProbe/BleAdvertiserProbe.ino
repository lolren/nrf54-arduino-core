/*
 * BleAdvertiserProbe
 *
 * Step-by-step BLE bring-up diagnostic sketch. Each initialisation stage is
 * guarded by a failStage() that blinks the LED N times per second if that
 * stage fails, making it easy to identify which step broke without a debugger.
 *
 * Stage map:
 *   failStage(2): g_ble.begin() failed  – BLE radio or HFXO issue.
 *   failStage(3): setAdvertisingName() failed.
 *   failStage(4): buildAdvertisingPacket() failed.
 *   failStage(5): advertiseEvent() failed in loop() – radio timing issue.
 *
 * The LED pulses briefly every 200 ms during normal advertising to show
 * liveness. Use BlePassiveScanner on a second board to confirm packets are
 * being received over the air.
 *
 * Tip: the RF path is kept permanently enabled (no duty cycling) in this
 * sketch to remove one class of variables during bring-up debugging.
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

BleRadio gBle;
PowerManager gPower;
// Permanently route the radio to the on-board ceramic antenna.
constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
// 0 dBm: full power chosen for bring-up; diagnosis is easier with strong signals.
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
