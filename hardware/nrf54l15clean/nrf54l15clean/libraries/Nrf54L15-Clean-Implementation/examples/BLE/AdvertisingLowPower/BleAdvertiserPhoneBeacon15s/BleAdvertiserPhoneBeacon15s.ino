/*
 * BleAdvertiserPhoneBeacon15s
 *
 * Produces the highest phone-detection probability at very low average current
 * by combining two strategies:
 *   1. A "burst" of 14 ADV_NONCONN_IND events spaced 70 ms apart.
 *   2. A 14-second timed SYSTEM OFF sleep between bursts.
 *
 * Why burst? Phones scan with a ~5-10 s window. Sending several events 70 ms
 * apart within that window dramatically increases the chance that at least one
 * event lands inside the phone's active scan slot.
 *
 * Why SYSTEM OFF (not WFI)? SYSTEM OFF cuts almost all silicon leakage and is
 * the lowest power state on nRF54L15. The device wakes via an RTC timer and
 * re-runs from setup() as though it were a cold boot.
 *
 * After setup() completes the burst and enters SYSTEM OFF, loop() is never
 * reached. The WFI in loop() is a safety net only.
 *
 * Tip: use kTxPowerDbm = 0 dBm for phone visibility. Lower power saves energy
 * but phones may not see the beacon across a room.
 */

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
constexpr int8_t kTxPowerDbm = 0;       // 0 dBm maximises phone visibility.
constexpr uint8_t kBurstEvents = 14U;   // Number of ad events per wake window.
constexpr uint32_t kBurstGapMs = 70UL; // Gap between events in one burst (ms).
// Total burst window ≈ kBurstEvents * kBurstGapMs ≈ 980 ms,
// which covers most phone scan windows.
constexpr uint32_t kSystemOffIntervalMs = 14000UL; // Sleep between bursts (ms).
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
    // advertiseEvent() is TX-only on this HAL, so use a non-connectable PDU.
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
  // enterLowestPowerState() powers down peripherals and the RF switch.
  // systemOffTimedWakeMsNoRetention() enters SYSTEM OFF; execution resumes
  // from the very beginning of setup() after kSystemOffIntervalMs elapses.
  // "NoRetention" means RAM is not preserved – all globals reset on wake.
  BoardControl::enterLowestPowerState();
  gPower.systemOffTimedWakeMsNoRetention(kSystemOffIntervalMs);
}

void loop() {
  __asm volatile("wfi");
}
