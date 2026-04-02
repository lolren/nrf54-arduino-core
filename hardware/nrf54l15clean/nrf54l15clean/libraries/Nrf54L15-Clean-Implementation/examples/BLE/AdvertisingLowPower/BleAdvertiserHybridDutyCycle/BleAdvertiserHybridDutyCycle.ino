#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {
/*
 * BleAdvertiserHybridDutyCycle
 *
 * Middle ground between always-on advertising and full SYSTEM OFF:
 *   - Stays in System ON (no cold-boot overhead on wake).
 *   - Emits short bursts of kBurstEvents events every kBurstPeriodMs.
 *   - Collapses the RF switch path between bursts (removes switch quiescent).
 *   - Idles with WFI between bursts (CPU clock gated by hardware).
 *
 * Trade-offs vs. SYSTEM OFF (BleAdvertiserBurstSystemOff):
 *   + Faster wake (microseconds vs. milliseconds cold boot).
 *   + RAM state preserved between bursts.
 *   - Slightly higher idle current (System ON vs. SYSTEM OFF).
 *
 * This is often the best practical choice for nodes that need to react quickly
 * to external events but still want to save RF switch power.
 */

// Middle ground between continuous advertising and burst-plus-SYSTEM-OFF.
// This sketch stays in System ON, emits short bursts, then idles with WFI and
// RF-switch collapse between bursts.

// kCeramic: on-board antenna. kExternal: only if physically attached.
constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;

// TX power in dBm for the raw legacy advertiser path.
constexpr int8_t kTxPowerDbm = -10;
// Burst schedule:
// - validated default: 2 events, 20 ms gap, 10 s period
// - sparser tested variant: keep events/gap, change period to 30000 ms
// kBurstEvents: number of advertising PDU triplets (ch37+38+39) per burst.
constexpr uint8_t kBurstEvents = 2U;
// kBurstGapUs: microsecond gap between events inside one burst.
constexpr uint32_t kBurstGapUs = 20000UL;
// kBurstPeriodMs: milliseconds between the start of each burst.
constexpr uint32_t kBurstPeriodMs = 10000UL;
// Core-specific advertiseEvent() timing knobs.
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
    BoardControl::enableRfPath(kAntennaPath);
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

  BoardControl::enableRfPath(kAntennaPath);
  bool ok = gBle.begin(kTxPowerDbm);
  if (ok) {
    // Keep the core in the low-power latency path while we remain in System ON.
    // Set after begin() so the radio subsystem is already configured.
    gPower.setLatencyMode(PowerLatencyMode::kLowPower);
  }
  if (ok) {
    // advertiseEvent() is TX-only on this HAL, so use a non-connectable PDU.
    ok = gBle.setAdvertisingPduType(BleAdvPduType::kAdvNonConnInd);
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

  // Hybrid mode stays in System ON. The current win here comes from WFI idle
  // and RF switch collapse, not from cold-boot wakeups.
  __asm volatile("wfi");
}
