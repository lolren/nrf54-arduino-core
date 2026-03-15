#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static Comp g_comp;
static bool g_a0AboveA1 = false;

static void updateLed() {
  (void)Gpio::write(kPinUserLed, !g_a0AboveA1);
}

static void printState(const char* reason) {
  Serial.print(reason);
  Serial.print(": ");
  Serial.println(g_a0AboveA1 ? "A0 > A1" : "A0 <= A1");
}

void setup() {
  Serial.begin(115200);
  delay(250);

  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::write(kPinUserLed, true);

  Serial.println("CompDifferentialProbe");
  Serial.println("Wire two analog voltages into A0 and A1.");
  Serial.println("LED on means A0 is above A1.");

  if (!g_comp.beginDifferential(kPinA0, kPinA1, CompSpeedMode::kLowPower, true)) {
    Serial.println("COMP differential begin failed");
    while (true) {
      delay(1000);
    }
  }

  (void)g_comp.sample();
  g_a0AboveA1 = g_comp.resultAbove();
  updateLed();
  printState("initial");
}

void loop() {
  if (!g_comp.pollCross(true)) {
    delay(1);
    return;
  }

  (void)g_comp.sample();
  g_a0AboveA1 = g_comp.resultAbove();
  updateLed();
  printState("crossing");
}
