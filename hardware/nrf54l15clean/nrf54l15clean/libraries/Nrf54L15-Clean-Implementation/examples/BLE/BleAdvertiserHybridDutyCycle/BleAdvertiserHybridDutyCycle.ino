#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

#ifndef NRF54L15_CLEAN_BLE_LP_BURST_EVENTS
#define NRF54L15_CLEAN_BLE_LP_BURST_EVENTS 2U
#endif

#ifndef NRF54L15_CLEAN_BLE_LP_BURST_GAP_US
#define NRF54L15_CLEAN_BLE_LP_BURST_GAP_US 20000UL
#endif

#ifndef NRF54L15_CLEAN_BLE_LP_BURST_PERIOD_MS
#define NRF54L15_CLEAN_BLE_LP_BURST_PERIOD_MS 10000UL
#endif

constexpr int8_t kTxPowerDbm = -10;
constexpr uint8_t kBurstEvents = NRF54L15_CLEAN_BLE_LP_BURST_EVENTS;
constexpr uint32_t kBurstGapUs = NRF54L15_CLEAN_BLE_LP_BURST_GAP_US;
constexpr uint32_t kBurstPeriodMs = NRF54L15_CLEAN_BLE_LP_BURST_PERIOD_MS;
constexpr uint32_t kInterChannelDelayUs = 350UL;
constexpr uint32_t kAdvertisingSpinLimit = 700000UL;

BleRadio gBle;
PowerManager gPower;
uint32_t g_nextBurstMs = 0UL;

[[noreturn]] void failStop() {
  BoardControl::collapseRfPathIdle();
  while (true) {
    __asm volatile("wfi");
  }
}

void configureBoardForHybridDutyCycle() {
  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  BoardControl::collapseRfPathIdle();
}

void sendBurst() {
  for (uint8_t i = 0; i < kBurstEvents; ++i) {
    BoardControl::enableRfPath(BoardAntennaPath::kCeramic);
    const bool ok = gBle.advertiseEvent(kInterChannelDelayUs, kAdvertisingSpinLimit);
    BoardControl::collapseRfPathIdle();
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
  configureBoardForHybridDutyCycle();
  gPower.setLatencyMode(PowerLatencyMode::kLowPower);

  BoardControl::enableRfPath(BoardAntennaPath::kCeramic);
  bool ok = gBle.begin(kTxPowerDbm);
  if (ok) {
    ok = gBle.setAdvertisingPduType(BleAdvPduType::kAdvInd);
  }
  if (ok) {
    ok = gBle.setAdvertisingName("X54-HYBRID", true);
  }
  if (ok) {
    ok = gBle.buildAdvertisingPacket();
  }
  BoardControl::collapseRfPathIdle();

  if (!ok) {
    failStop();
  }

  g_nextBurstMs = millis();
}

void loop() {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - g_nextBurstMs) >= 0) {
    sendBurst();
    g_nextBurstMs = now + kBurstPeriodMs;
    return;
  }

  __asm volatile("wfi");
}
