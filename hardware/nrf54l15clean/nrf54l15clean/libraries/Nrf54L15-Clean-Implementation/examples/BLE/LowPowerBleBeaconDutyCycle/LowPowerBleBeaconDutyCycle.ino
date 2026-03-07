#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static BleRadio g_ble;
static PowerManager g_power;

static uint32_t g_nextBurstMs = 0;
static uint32_t g_bursts = 0;

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

  bool ok = g_ble.begin(-20);
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
    for (uint8_t i = 0; i < 3U; ++i) {
      burstOk = burstOk && g_ble.advertiseEvent(350U, 700000UL);
      delay(15);
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

    // Long sleep interval: advertise burst every 2 seconds.
    g_nextBurstMs = now + 2000UL;
  }

  __asm volatile("wfi");
}
