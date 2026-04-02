/*
 * BleBackgroundAdvertiser3Channel
 *
 * Second controller-offload milestone for the clean nRF54L15 Arduino core:
 * legacy ADV_NONCONN_IND on all three primary advertising channels without
 * sketch-loop event emission.
 *
 * The sketch configures advertising once in setup(). After that the BLE HAL:
 *   - uses GRTC compare to prewarm HFXO before the first primary channel
 *   - mirrors TASKS_XOSTART from the prewarm IRQ without waiting on this XIAO
 *     path
 *   - uses scheduled GRTC compare points to wake the BLE IRQ only where
 *     software still owns work
 *   - on the XIAO default build, kicks TXEN from the TX compare IRQ only after
 *     HFXO is ready; the pure direct GRTC -> DPPI/PPIB -> RADIO launch chain
 *     remains available only as an explicit experimental override
 *   - if that IRQ path arrives before HFXO is ready, it re-arms the same
 *     compare a few microseconds later instead of spinning
 *   - uses RADIO SHORTS for READY->START and PHYEND->DISABLE
 *   - keeps HFXO running between 37/38/39 so the CPU only handles the
 *     software-owned channel transition
 *   - hardware-stops HFXO after the final primary channel
 *
 * Current milestone limits:
 *   - single advertising set
 *   - non-connectable legacy advertising only
 *   - software still owns TX kickoff, channel rotation state, and next-event
 *     scheduling
 *   - no scan response / connect request handling
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
constexpr int8_t kTxPowerDbm = -10;
constexpr uint32_t kAdvertisingIntervalMs = 100;
constexpr uint32_t kInterChannelDelayUs = 350;
#if defined(NRF54L15_BG_EXAMPLE_HFXO_LEAD_US)
constexpr uint32_t kHfxoLeadUs =
    static_cast<uint32_t>(NRF54L15_BG_EXAMPLE_HFXO_LEAD_US);
#else
constexpr uint32_t kHfxoLeadUs = 1200;
#endif
constexpr bool kAddRandomDelay = false;

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
  BoardControl::enableRfPath(kAntennaPath);
}

}  // namespace

void setup() {
  configureBoardForBleLowPower();

  bool ok = gBle.begin(kTxPowerDbm);
  if (ok) {
    gPower.setLatencyMode(PowerLatencyMode::kLowPower);
  }
  if (ok) {
    ok = gBle.setAdvertisingPduType(BleAdvPduType::kAdvNonConnInd);
  }
  if (ok) {
    ok = gBle.setAdvertisingName("X54-BG-3CH", true);
  }
  if (ok) {
    ok = gBle.beginBackgroundAdvertising3Channel(
        kAdvertisingIntervalMs, kInterChannelDelayUs, kHfxoLeadUs,
        kAddRandomDelay);
  }
  if (!ok) {
    failStop();
  }
}

void loop() {
  __asm volatile("wfi");
}
