#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {
BleRadio gBle;
PowerManager gPower;

// Use kCeramic for the on-board antenna.
// Use kExternal only if an external antenna is attached to the connector path.
constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;

// Keep the name short so it fits in the primary advertising payload.
constexpr char kName[] = "X54-15S";

// Tuned for phone visibility first, while keeping average current low:
// - legacy ADV_NONCONN_IND
// - no scan response
// - no RX listen window
// - RF path enabled only while emitting an event
// - long timed System OFF between burst windows
//
// These are intentionally sketch-level knobs because they define the beacon
// behavior, not a board-wide core policy.
constexpr int8_t kTxPowerDbm = 0;
constexpr uint8_t kBurstEvents = 14U;
constexpr uint32_t kBurstGapMs = 70UL;
constexpr uint32_t kSystemOffIntervalMs = 14000UL;
// Core-specific advertiseEvent() timing knobs.
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
    BoardControl::enableRfPath(kAntennaPath);
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
  // Keep System ON in the low-power latency path while we are building the
  // packet and emitting the short wake burst.
  gPower.setLatencyMode(PowerLatencyMode::kLowPower);

  BoardControl::enableRfPath(kAntennaPath);
  bool ok = gBle.begin(kTxPowerDbm);
  if (ok) {
    // ADV_NONCONN_IND avoids connectability and scan-response timing. That is
    // part of why this pattern is a better fit for long-sleep beaconing.
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

  // This sketch is optimized for the lowest average current and explicitly
  // clears RAM retention before timed SYSTEM OFF.
  BoardControl::enterLowestPowerState();
  gPower.systemOffTimedWakeMsNoRetention(kSystemOffIntervalMs);
}

void loop() {
  __asm volatile("wfi");
}
