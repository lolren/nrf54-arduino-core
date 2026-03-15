#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static Timer g_timer;
static Gpiote g_gpiote;
static Dppic g_dppic;

static constexpr uint8_t kTimerCompareChannel = 0U;
static constexpr uint8_t kGpioteTaskChannel = 0U;
static constexpr uint8_t kDppiChannel = 7U;
static constexpr uint32_t kTogglePeriodUs = 250000UL;

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println("DppicHardwareBlink");
  Serial.println("The LED below is toggled by TIMER -> DPPIC -> GPIOTE, not by loop().");

  if (!g_timer.begin(TimerBitWidth::k32bit, 4U, false)) {
    Serial.println("timer begin failed");
    while (true) {
      delay(1000);
    }
  }
  if (!g_timer.setCompare(kTimerCompareChannel, g_timer.ticksFromMicros(kTogglePeriodUs),
                          true, false, false, false)) {
    Serial.println("timer compare setup failed");
    while (true) {
      delay(1000);
    }
  }
  if (!g_gpiote.configureTask(kGpioteTaskChannel, kPinUserLed,
                              GpiotePolarity::kToggle, true)) {
    Serial.println("gpiote task setup failed");
    while (true) {
      delay(1000);
    }
  }
  if (!g_dppic.connect(g_timer.publishCompareConfigRegister(kTimerCompareChannel),
                       g_gpiote.subscribeTaskOutConfigRegister(kGpioteTaskChannel),
                       kDppiChannel, true)) {
    Serial.println("dppic connect failed");
    while (true) {
      delay(1000);
    }
  }

  g_timer.start();
}

void loop() {
  static uint32_t lastPrintMs = 0U;
  const uint32_t now = millis();
  if ((now - lastPrintMs) >= 2000UL) {
    lastPrintMs = now;
    Serial.print("heartbeat ms=");
    Serial.println(now);
  }
  delay(10);
}
