#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {
// Middle ground between continuous advertising and burst-plus-SYSTEM-OFF.
// This sketch stays in System ON, emits short bursts, then idles with WFI and
// RF-switch collapse between bursts.

constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;

// TX power in dBm for the raw legacy advertiser path.
constexpr int8_t kTxPowerDbm = -10;
// Burst schedule:
// - validated default: 2 events, 20 ms gap, 10 s period
// - sparser tested variant: keep events/gap, change period to 30000 ms
constexpr uint8_t kBurstEvents = 2U;
constexpr uint32_t kBurstGapUs = 20000UL;
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
  // Keep the core in the low-power latency path while we remain in System ON.
  gPower.setLatencyMode(PowerLatencyMode::kLowPower);

  BoardControl::enableRfPath(kAntennaPath);
  bool ok = gBle.begin(kTxPowerDbm);
  if (ok) {
    // ADV_IND is the practical default for easy scanner interoperability.
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

  // Hybrid mode stays in System ON. The current win here comes from WFI idle
  // and RF switch collapse, not from cold-boot wakeups.
  __asm volatile("wfi");
}
