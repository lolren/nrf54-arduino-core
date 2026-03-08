#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {
// Lowest validated continuous-advertising baseline for the current raw BLE path.
// This is not yet Zephyr-controller parity. The combinations below were not
// discoverable in local validation and are intentionally avoided here:
// - setDeviceAddress(...)
// - ADV_NONCONN_IND
// - 4 s interval at -10 dBm was not discoverable in the same practical scan window
// - manual HFXO stop/start between events
//
// Validated configuration on hardware:
// - clean_power=low
// - 3 s interval
// - -10 dBm
// - default FICR-derived address
// - ADV_IND
//
// This example stays continuously discoverable. It does not use SYSTEM OFF.
constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
constexpr int8_t kTxPowerDbm = -10;
// Sketch-owned preset:
// - validated default: 3000 ms
// - easier scanner visibility: 1000 ms
constexpr uint32_t kAdvertisingIntervalMs = 3000UL;
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
  // Low-power latency mode matters here because delay() is part of the idle
  // path between advertising events.
  gPower.setLatencyMode(PowerLatencyMode::kLowPower);

  bool ok = gBle.begin(kTxPowerDbm);
  if (ok) {
    ok = gBle.setAdvertisingName("X54-LP-3S", true);
  }
  if (ok) {
    ok = gBle.buildAdvertisingPacket();
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
