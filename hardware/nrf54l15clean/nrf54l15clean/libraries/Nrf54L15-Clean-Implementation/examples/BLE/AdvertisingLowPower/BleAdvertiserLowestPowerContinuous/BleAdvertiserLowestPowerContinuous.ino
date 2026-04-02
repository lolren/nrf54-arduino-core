/*
 * BleAdvertiserLowestPowerContinuous
 *
 * The lowest-current continuously-discoverable advertising configuration
 * validated on nRF54L15 hardware. The device advertises forever in System ON,
 * spending most of the time in low-power WFI sleep between events.
 *
 * Key constraints that were validated (others caused non-discoverability):
 *   - Default FICR-derived address (no setDeviceAddress).
 *   - ADV_NONCONN_IND PDU type (advertiseEvent() is TX-only on this HAL).
 *   - 3 s advertising interval.
 *   - -10 dBm TX power.
 *
 * This sketch intentionally does not gate the RF switch between events.
 * If you want RF switch gating, use BleAdvertiserRfSwitchDutyCycle.
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {
// Lowest validated continuous-advertising baseline for the current raw BLE path.
// This is not yet Zephyr-controller parity. The combinations below are
// intentionally avoided here:
// - setDeviceAddress(...)
// - connectable/scannable PDUs with advertiseEvent()
// - 4 s interval at -10 dBm was not discoverable in the same practical scan window
// - manual HFXO stop/start between events
//
// Validated configuration on hardware:
// - clean_power=low
// - 3 s interval
// - -10 dBm
// - default FICR-derived address
// - ADV_NONCONN_IND
//
// This example stays continuously discoverable. It does not use SYSTEM OFF.
// kCeramic: use the on-board patch antenna (validated for this configuration).
// kExternal: only use if an external antenna is physically attached.
constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
// -10 dBm: validated for discoverability at ≤3 m. Increase if needed.
constexpr int8_t kTxPowerDbm = -10;
// Sketch-owned preset:
// - validated default: 3000 ms
// - easier scanner visibility: 1000 ms
constexpr uint32_t kAdvertisingIntervalMs = 3000UL;
// kInterChannelDelayUs: inter-channel gap inside one advertising event (us).
// kAdvertisingSpinLimit: max spin time waiting for the radio to be ready (us).
constexpr uint32_t kInterChannelDelayUs = 350UL;
constexpr uint32_t kAdvertisingSpinLimit = 700000UL;

BleRadio gBle;
PowerManager gPower;

[[noreturn]] void failStop() {
  while (true) {
    __asm volatile("wfi");
  }
}

void configureBoardForBleLowPower() {
  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  // Validation for this example was done with the on-board ceramic antenna.
  // Change kAntennaPath if you explicitly want the external connector path.
  BoardControl::enableRfPath(kAntennaPath);
}

}  // namespace

void setup() {
  configureBoardForBleLowPower();

  bool ok = gBle.begin(kTxPowerDbm);
  if (ok) {
    // Low-power latency mode matters here because delay() is part of the idle
    // path between advertising events. Set after begin() so the radio subsystem
    // is already configured.
    gPower.setLatencyMode(PowerLatencyMode::kLowPower);
  }
  if (ok) {
    ok = gBle.setAdvertisingPduType(BleAdvPduType::kAdvNonConnInd);
  }
  if (ok) {
    ok = gBle.setAdvertisingName("X54-LP-3S", true);
  }
  if (!ok) {
    failStop();
  }
}

void loop() {
  // advertiseEvent() emits one synchronous three-channel legacy advertising
  // event. The follow-on delay controls the cadence between events.
  (void)gBle.advertiseEvent(kInterChannelDelayUs, kAdvertisingSpinLimit);
  delay(kAdvertisingIntervalMs);
}
