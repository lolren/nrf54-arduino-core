/*
 * BleAdvertiserRfSwitchDutyCycle
 *
 * Continuously advertises using legacy ADV_NONCONN_IND but gates the XIAO RF switch
 * path: the switch is powered only during each advertiseEvent() call, then
 * collapsed to high-impedance during the idle delay. This eliminates the
 * RF switch quiescent current between advertising events.
 *
 * This sketch stays in System ON the whole time (no SYSTEM OFF / cold boot).
 * It is a good baseline for measuring "what does the board draw between
 * advertising events when the RF switch is off".
 *
 * Compare with:
 *   BleAdvertiserLowestPowerContinuous – no RF switch gating
 *   BleAdvertiserHybridDutyCycle       – burst + WFI sleep in System ON
 *   BleAdvertiserBurstSystemOff        – burst + true SYSTEM OFF
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {
// This is the practical low-current continuous advertiser:
// the BLE event stays synchronous and legacy, but the XIAO RF switch path is
// only powered/selected for the duration of each advertiseEvent() call.

// kAntennaPath: kCeramic = on-board ceramic patch antenna (default).
//               kExternal = external connector/u.FL, only if an antenna is attached.
constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;

// Radio TX power in dBm.
// Valid range on nRF54L15: -40 dBm to +3 dBm (check datasheet for exact steps).
constexpr int8_t kTxPowerDbm = -10;
// Sketch-owned preset:
// - tested low-power default: 3000 ms
// - easier scanner visibility: 1000 ms
constexpr uint32_t kAdvertisingIntervalMs = 3000UL;
// Core-specific advertiseEvent() timing knobs.
// kInterChannelDelayUs: pause between transmissions on ch37/38/39 (microseconds).
// kAdvertisingSpinLimit: max time to wait for the radio-ready flag (microseconds).
//   Increase if the first event after begin() sometimes fails.
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

  enableCeramicRfPath();
  bool ok = gBle.begin(kTxPowerDbm);
  if (ok) {
    // This keeps the core on the low-power System ON path between events.
    // Set after begin() so the radio subsystem is already configured.
    gPower.setLatencyMode(PowerLatencyMode::kLowPower);
  }
  if (ok) {
    // advertiseEvent() is TX-only on this HAL, so use a non-connectable PDU.
    ok = gBle.setAdvertisingPduType(BleAdvPduType::kAdvNonConnInd);
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
  // Pattern: enable RF path → transmit → collapse RF path → long delay.
  // This cycle keeps average current low while maintaining a fixed ad interval.
  enableCeramicRfPath();
  const bool ok = gBle.advertiseEvent(kInterChannelDelayUs, kAdvertisingSpinLimit);
  collapseRfPathIdle();

  if (!ok) {
    failStop();
  }

  delay(kAdvertisingIntervalMs);
}
