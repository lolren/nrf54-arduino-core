#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static Comp g_comp;

static constexpr uint16_t kThresholdPermille = 500U;
static constexpr uint16_t kHysteresisPermille = 40U;

static bool g_aboveThreshold = false;

static void updateLed() {
  (void)Gpio::write(kPinUserLed, !g_aboveThreshold);
}

static void printState(const char* reason) {
  Serial.print(reason);
  Serial.print(": A0 is ");
  Serial.println(g_aboveThreshold ? "above threshold" : "below threshold");
}

void setup() {
  Serial.begin(115200);
  delay(250);

  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::write(kPinUserLed, true);

  Serial.println("CompThresholdMonitor");
  Serial.println("Wire a variable voltage into A0.");
  Serial.println("This uses COMP in single-ended mode with a threshold near 50% VDD.");

  if (!g_comp.beginThreshold(kPinA0, kThresholdPermille, kHysteresisPermille,
                             CompReference::kVdd, CompSpeedMode::kLowPower)) {
    Serial.println("COMP begin failed");
    while (true) {
      delay(1000);
    }
  }

  (void)g_comp.sample();
  g_aboveThreshold = g_comp.resultAbove();
  updateLed();
  printState("initial");
}

void loop() {
  bool changed = false;

  if (g_comp.pollUp(true)) {
    g_aboveThreshold = true;
    changed = true;
  }
  if (g_comp.pollDown(true)) {
    g_aboveThreshold = false;
    changed = true;
  }

  if (changed) {
    updateLed();
    printState("crossing");
  }

  delay(1);
}
