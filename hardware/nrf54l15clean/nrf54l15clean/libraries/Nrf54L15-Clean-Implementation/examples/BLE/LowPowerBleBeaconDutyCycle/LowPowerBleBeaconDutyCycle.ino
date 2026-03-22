#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

/*
 * LowPowerBleBeaconDutyCycle
 *
 * System ON burst-beacon that lowers average current by grouping advertising
 * events into bursts separated by WFI (Wait For Interrupt) sleep. Unlike the
 * SYSTEM OFF examples, this sketch stays resident in RAM and wakes via a
 * software timer – much faster wake-up but higher quiescent current.
 *
 * Strategy:
 *   Every kBurstPeriodMs:
 *     Send kBurstEvents advertising events, kBurstGapMs apart.
 *     → sleep with WFI until the next burst is due.
 *
 * Also reduces active CPU current by dropping PLL to 64 MHz.
 *
 * Note: this sketch does NOT gate the RF switch between events. If you need
 * that, use BleAdvertiserHybridDutyCycle instead.
 */

// Older System ON burst-beacon example.
//
// This sketch lowers average current by sending short bursts and sleeping with
// WFI between bursts. It does not enter SYSTEM OFF, and it does not use the
// RF-switch duty-cycling helpers added later in the core.

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_nextBurstMs = 0;
static uint32_t g_bursts = 0;

// -20 dBm: minimum power; change to 0 for more typical discoverability.
static constexpr int8_t kTxPowerDbm = -20;
// kBurstEvents: number of advertising events emitted before sleeping.
static constexpr uint8_t kBurstEvents = 3U;
// kBurstGapMs: delay between consecutive events in one burst (milliseconds).
static constexpr uint32_t kBurstGapMs = 15UL;
// kBurstPeriodMs: how long to wait between burst windows (milliseconds).
static constexpr uint32_t kBurstPeriodMs = 2000UL;
// Core-specific timing knobs for each synchronous advertiseEvent() call.
static constexpr uint32_t kInterChannelDelayUs = 350U;
static constexpr uint32_t kAdvertisingSpinLimit = 700000UL;

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.print("\r\nLowPowerBleBeaconDutyCycle start\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);

  // Keep System ON in low-power latency mode.
  g_power.setLatencyMode(PowerLatencyMode::kLowPower);

  // Lowering the CPU clock from 128 MHz to 64 MHz reduces dynamic current
  // during active TX. The BLE radio timing is unaffected because it uses its
  // own clock source.
  NRF_OSCILLATORS->PLL.FREQ = OSCILLATORS_PLL_FREQ_FREQ_CK64M;

  bool ok = g_ble.begin(kTxPowerDbm);
  if (ok) {
    ok = g_ble.setAdvertisingName("XIAO54-LP", true);
  }

  Serial.print("BLE init: ");
  Serial.print(ok ? "OK" : "FAIL");
  Serial.print("\r\n");

  g_nextBurstMs = millis();
}

void loop() {
  const uint32_t now = millis();
  // Signed comparison handles millis() rollover correctly.
  if (static_cast<int32_t>(now - g_nextBurstMs) >= 0) {
    bool burstOk = true;
    // Emit kBurstEvents advertising events, each separated by kBurstGapMs.
    // Each advertiseEvent() call transmits on channels 37, 38, and 39 in turn.
    for (uint8_t i = 0; i < kBurstEvents; ++i) {
      burstOk = burstOk && g_ble.advertiseEvent(kInterChannelDelayUs, kAdvertisingSpinLimit);
      delay(kBurstGapMs);
    }

    ++g_bursts;
    Gpio::write(kPinUserLed, (g_bursts & 0x1U) == 0U);  // Toggle LED each burst.

    char line[112];
    snprintf(line, sizeof(line),
             "t=%lu burst=%lu status=%s cpu=%s\r\n",
             static_cast<unsigned long>(now),
             static_cast<unsigned long>(g_bursts),
             burstOk ? "OK" : "FAIL",
             (NRF_OSCILLATORS->PLL.CURRENTFREQ == OSCILLATORS_PLL_FREQ_FREQ_CK64M)
                 ? "64MHz"
                 : "other");
    Serial.print(line);

    // Still System ON: this is a WFI-idle interval, not a cold-boot wake cycle.
    g_nextBurstMs = now + kBurstPeriodMs;
  }

  __asm volatile("wfi");
}
