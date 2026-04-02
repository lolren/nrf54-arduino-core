/*
 * BleBackgroundAdvertiserSingleChannel
 *
 * First controller-offload milestone for the clean nRF54L15 Arduino core:
 * one legacy ADV_NONCONN_IND packet on one primary advertising channel.
 *
 * The sketch configures advertising once in setup(). After that the BLE HAL
 * owns the cadence:
 *   - GRTC compare prewarms HFXO in hardware
 *   - the prewarm IRQ mirrors TASKS_XOSTART without waiting on this XIAO path
 *   - on the XIAO default build, a scheduled TX compare wakes the BLE IRQ only
 *     when transmit kickoff is needed
 *   - the TX IRQ kicks TXEN immediately when HFXO is already running, or
 *     re-arms the same compare a few microseconds later instead of spinning
 *   - the pure direct GRTC -> DPPI/PPIB -> RADIO launch chain remains
 *     available only as an explicit experimental override
 *   - RADIO SHORTS run READY->START and PHYEND->DISABLE
 *   - the cleanup compare hardware-stops HFXO
 *   - a cleanup compare wakes the CPU only to re-arm the next interval
 *
 * Current milestone limits:
 *   - single advertising set
 *   - single primary channel
 *   - fixed interval
 *   - software still owns TX kickoff and next-event scheduling
 *   - non-connectable legacy advertising only
 */

#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;
constexpr int8_t kTxPowerDbm = -10;
constexpr uint32_t kAdvertisingIntervalMs = 100;
constexpr uint32_t kHfxoLeadUs = 1200;

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
    ok = gBle.setAdvertisingName("X54-BG-1CH", true);
  }
  if (ok) {
    ok = gBle.beginBackgroundAdvertising(
        kAdvertisingIntervalMs, BleAdvertisingChannel::k37, kHfxoLeadUs);
  }
  if (!ok) {
    failStop();
  }
}

void loop() {
  __asm volatile("wfi");
}
