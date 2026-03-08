#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

// Older System ON burst-beacon example.
//
// This sketch lowers average current by sending short bursts and sleeping with
// WFI between bursts. It does not enter SYSTEM OFF, and it does not use the
// RF-switch duty-cycling helpers added later in the core.

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_nextBurstMs = 0;
static uint32_t g_bursts = 0;

static constexpr int8_t kTxPowerDbm = -20;
static constexpr uint8_t kBurstEvents = 3U;
static constexpr uint32_t kBurstGapMs = 15UL;
static constexpr uint32_t kBurstPeriodMs = 2000UL;
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

  // Lower CPU frequency to 64 MHz to reduce active current.
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
  if (static_cast<int32_t>(now - g_nextBurstMs) >= 0) {
    bool burstOk = true;
    for (uint8_t i = 0; i < kBurstEvents; ++i) {
      burstOk = burstOk && g_ble.advertiseEvent(kInterChannelDelayUs, kAdvertisingSpinLimit);
      delay(kBurstGapMs);
    }

    ++g_bursts;
    Gpio::write(kPinUserLed, (g_bursts & 0x1U) == 0U);

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
