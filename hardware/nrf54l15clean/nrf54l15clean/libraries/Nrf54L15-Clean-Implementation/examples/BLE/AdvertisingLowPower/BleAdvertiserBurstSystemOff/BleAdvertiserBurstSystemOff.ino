#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {
/*
 * BleAdvertiserBurstSystemOff
 *
 * Battery-optimised beacon pattern using true SYSTEM OFF between bursts:
 *   1. Wake from reset (cold boot via RTC timer after SYSTEM OFF).
 *   2. Initialise BLE and emit kBurstEvents ADV_NONCONN_IND packets.
 *   3. Collapse the RF switch path (removes switch quiescent current).
 *   4. Call systemOffTimedWakeMsNoRetention() → enters SYSTEM OFF.
 *   5. After kSystemOffIntervalMs the RTC fires and the board cold-boots again.
 *
 * Unlike LowPowerBleBeaconDutyCycle (System ON + WFI), every wake here is a
 * full cold boot: setup() runs from scratch, all RAM is cleared.
 *
 * Gotcha: because RAM is cleared, no state persists across wake cycles. If you
 * need to keep a counter or sequence number, write it to flash or use a
 * retained-RAM region (and call systemOffTimedWakeMsWithRetention instead).
 *
 * For better phone discoverability increase kBurstEvents or switch to
 * BleAdvertiserPhoneBeacon15s which uses a longer burst window.
 */

// This sketch is a battery-first beacon pattern:
// 1) wake from reset
// 2) emit a very short burst of legacy advertising events
// 3) collapse the RF switch path
// 4) enter true timed SYSTEM OFF
//
// It is not intended to be continuously discoverable. If you want easier phone
// discovery, use BleAdvertiserPhoneBeacon15s instead.

// Select which board antenna path is used while the radio is active.
// - kCeramic: on-board ceramic antenna
// - kExternal: external connector/u.FL path, only valid if an external antenna is attached
constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;

// Radio TX power in dBm. This is written into RADIO->TXPOWER by the core.
// Higher values may not show a dramatic RSSI change on a phone at very short
// distance, so validate at real separation if you are comparing settings.
constexpr int8_t kTxPowerDbm = -10;
// Burst profile:
// - kBurstEvents: how many advertising events are emitted per wake
// - kBurstGapUs: gap between events inside one wake window
// - kSystemOffIntervalMs: sleep time before the next cold-boot wake
//
// Validated default:
// - 6 events
// - 20 ms gap
// - 1000 ms timed SYSTEM OFF
//
// Sparser tested variant:
// - keep events/gap
// - change interval to 5000 ms
constexpr uint8_t kBurstEvents = 6U;
constexpr uint32_t kBurstGapUs = 20000UL;
constexpr uint32_t kSystemOffIntervalMs = 1000UL;
// Low-level radio timing knobs for one synchronous advertiseEvent() call.
// These are core-specific, not standard Arduino concepts.
constexpr uint32_t kInterChannelDelayUs = 350UL;
constexpr uint32_t kAdvertisingSpinLimit = 700000UL;

BleRadio gBle;
PowerManager gPower;

void collapseRfPathIdle() {
  // Put RF_SW_CTL into high-Z and remove RF switch power to eliminate the
  // board-side RF switch quiescent current between wake windows.
  BoardControl::collapseRfPathIdle();
}

void enableCeramicRfPath() {
  BoardControl::enableRfPath(kAntennaPath);
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

  enableCeramicRfPath();
  bool ok = gBle.begin(kTxPowerDbm);
  if (ok) {
    // Variable-latency low-power System ON mode before we drop into SYSTEM OFF.
    // Set after begin() so the radio subsystem is already configured.
    gPower.setLatencyMode(PowerLatencyMode::kLowPower);
  }
  if (ok) {
    // advertiseEvent() is TX-only on this HAL, so use a non-connectable PDU.
    ok = gBle.setAdvertisingPduType(BleAdvPduType::kAdvNonConnInd);
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
  // NoRetention is deliberate here: this sketch is optimized for the lowest
  // current, not for preserving .noinit RAM or retained state.
  BoardControl::enterLowestPowerState();
  gPower.systemOffTimedWakeMsNoRetention(kSystemOffIntervalMs);
}

void loop() {
  __asm volatile("wfi");
}
