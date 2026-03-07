#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

#ifndef NRF54L15_CLEAN_BLE_LP_BURST_EVENTS
#define NRF54L15_CLEAN_BLE_LP_BURST_EVENTS 6U
#endif

#ifndef NRF54L15_CLEAN_BLE_LP_BURST_GAP_US
#define NRF54L15_CLEAN_BLE_LP_BURST_GAP_US 20000UL
#endif

#ifndef NRF54L15_CLEAN_BLE_LP_SYSTEMOFF_MS
#define NRF54L15_CLEAN_BLE_LP_SYSTEMOFF_MS 1000UL
#endif

constexpr int8_t kTxPowerDbm = -10;
constexpr uint8_t kBurstEvents = NRF54L15_CLEAN_BLE_LP_BURST_EVENTS;
constexpr uint32_t kBurstGapUs = NRF54L15_CLEAN_BLE_LP_BURST_GAP_US;
constexpr uint32_t kSystemOffIntervalMs = NRF54L15_CLEAN_BLE_LP_SYSTEMOFF_MS;
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
  BoardControl::enterLowestPowerState();
  while (true) {
    __asm volatile("wfi");
  }
}

void configureBoardForBleBurst() {
  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  collapseRfPathIdle();
}

void sendBurst() {
  for (uint8_t i = 0; i < kBurstEvents; ++i) {
    enableCeramicRfPath();
    const bool ok = gBle.advertiseEvent(kInterChannelDelayUs, kAdvertisingSpinLimit);
    collapseRfPathIdle();
    if (!ok) {
      failStop();
    }
    if ((i + 1U) < kBurstEvents) {
      delayMicroseconds(kBurstGapUs);
    }
  }
}

}  // namespace

void setup() {
  configureBoardForBleBurst();
  gPower.setLatencyMode(PowerLatencyMode::kLowPower);

  enableCeramicRfPath();
  bool ok = gBle.begin(kTxPowerDbm);
  if (ok) {
    ok = gBle.setAdvertisingPduType(BleAdvPduType::kAdvInd);
  }
  if (ok) {
    ok = gBle.setAdvertisingName("X54-BURST-OFF", true);
  }
  if (ok) {
    ok = gBle.buildAdvertisingPacket();
  }
  collapseRfPathIdle();

  if (!ok) {
    failStop();
  }

  sendBurst();
  BoardControl::enterLowestPowerState();
  gPower.systemOffTimedWakeMsNoRetention(kSystemOffIntervalMs);
}

void loop() {
  __asm volatile("wfi");
}
