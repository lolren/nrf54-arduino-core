#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

BleRadio gBle;
PowerManager gPower;

// Keep the name short so it fits in the primary advertising payload.
constexpr char kName[] = "X54-15S";

// Tuned for phone visibility first, while keeping average current low:
// - legacy ADV_NONCONN_IND
// - no scan response
// - no RX listen window
// - RF path enabled only while emitting an event
// - long timed System OFF between burst windows
constexpr int8_t kTxPowerDbm = 0;
constexpr uint8_t kBurstEvents = 14U;
constexpr uint32_t kBurstGapMs = 70UL;
constexpr uint32_t kSystemOffIntervalMs = 14000UL;
constexpr uint32_t kInterChannelDelayUs = 350UL;
constexpr uint32_t kAdvertisingSpinLimit = 900000UL;

[[noreturn]] void failStop() {
  BoardControl::enterLowestPowerState();
  while (true) {
    __asm volatile("wfi");
  }
}

void configureBoard() {
  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  BoardControl::collapseRfPathIdle();
}

void advertiseBurst() {
  for (uint8_t i = 0; i < kBurstEvents; ++i) {
    BoardControl::enableRfPath(BoardAntennaPath::kCeramic);
    const bool ok = gBle.advertiseEvent(kInterChannelDelayUs, kAdvertisingSpinLimit);
    BoardControl::collapseRfPathIdle();
    if (!ok) {
      failStop();
    }
    if ((i + 1U) < kBurstEvents) {
      delay(kBurstGapMs);
    }
  }
}

}  // namespace

void setup() {
  configureBoard();
  gPower.setLatencyMode(PowerLatencyMode::kLowPower);

  BoardControl::enableRfPath(BoardAntennaPath::kCeramic);
  bool ok = gBle.begin(kTxPowerDbm);
  if (ok) {
    ok = gBle.setAdvertisingPduType(BleAdvPduType::kAdvNonConnInd);
  }
  if (ok) {
    ok = gBle.setAdvertisingName(kName, true);
  }
  if (ok) {
    ok = gBle.buildAdvertisingPacket();
  }
  BoardControl::collapseRfPathIdle();

  if (!ok) {
    failStop();
  }

  advertiseBurst();

  BoardControl::enterLowestPowerState();
  gPower.systemOffTimedWakeMsNoRetention(kSystemOffIntervalMs);
}

void loop() {
  __asm volatile("wfi");
}
