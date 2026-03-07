#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

#ifndef NRF54L15_CLEAN_BLE_LP_ADV_INTERVAL_MS
#define NRF54L15_CLEAN_BLE_LP_ADV_INTERVAL_MS 3000UL
#endif

constexpr int8_t kTxPowerDbm = -10;
constexpr uint32_t kAdvertisingIntervalMs = NRF54L15_CLEAN_BLE_LP_ADV_INTERVAL_MS;
constexpr uint32_t kInterChannelDelayUs = 350UL;
constexpr uint32_t kAdvertisingSpinLimit = 700000UL;

BleRadio gBle;
PowerManager gPower;

void collapseRfPathIdle() {
  BoardControl::collapseRfPathIdle();
}

void enableCeramicRfPath() {
  BoardControl::enableRfPath(BoardAntennaPath::kCeramic);
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
  gPower.setLatencyMode(PowerLatencyMode::kLowPower);

  enableCeramicRfPath();
  bool ok = gBle.begin(kTxPowerDbm);
  if (ok) {
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
