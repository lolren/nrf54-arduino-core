#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {
// This is the practical low-current continuous advertiser:
// the BLE event stays synchronous and legacy, but the XIAO RF switch path is
// only powered/selected for the duration of each advertiseEvent() call.

constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;

// Radio TX power in dBm.
constexpr int8_t kTxPowerDbm = -10;
// Sketch-owned preset:
// - tested low-power default: 3000 ms
// - easier scanner visibility: 1000 ms
constexpr uint32_t kAdvertisingIntervalMs = 3000UL;
// Core-specific advertiseEvent() timing knobs.
constexpr uint32_t kInterChannelDelayUs = 350UL;
constexpr uint32_t kAdvertisingSpinLimit = 700000UL;

BleRadio gBle;
PowerManager gPower;

void collapseRfPathIdle() {
  BoardControl::collapseRfPathIdle();
}

void enableCeramicRfPath() {
  BoardControl::enableRfPath(kAntennaPath);
}

[[noreturn]] void failStop() {
  collapseRfPathIdle();
  while (true) {
    __asm volatile("wfi");
  }
}

void configureBoardForBleLowPower() {
  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  collapseRfPathIdle();
}

}  // namespace

void setup() {
  configureBoardForBleLowPower();
  // This keeps the core on the low-power System ON path between events.
  gPower.setLatencyMode(PowerLatencyMode::kLowPower);

  enableCeramicRfPath();
  bool ok = gBle.begin(kTxPowerDbm);
  if (ok) {
    // ADV_IND remains the most interoperable choice on the current raw BLE path.
    ok = gBle.setAdvertisingPduType(BleAdvPduType::kAdvInd);
  }
  if (ok) {
    ok = gBle.setAdvertisingName("X54-RF-GATE", true);
  }
  if (ok) {
    ok = gBle.buildAdvertisingPacket();
  }
  collapseRfPathIdle();

  if (!ok) {
    failStop();
  }
}

void loop() {
  // advertiseEvent() is a synchronous three-channel TX-only legacy advertising
  // event in this core, so the RF switch can be collapsed immediately after it.
  enableCeramicRfPath();
  const bool ok = gBle.advertiseEvent(kInterChannelDelayUs, kAdvertisingSpinLimit);
  collapseRfPathIdle();

  if (!ok) {
    failStop();
  }

  delay(kAdvertisingIntervalMs);
}
