#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static Qdec g_qdec;
static int32_t g_position = 0;

static void pulseLed() {
  (void)Gpio::write(kPinUserLed, false);
  delay(10);
  (void)Gpio::write(kPinUserLed, true);
}

void setup() {
  Serial.begin(115200);
  delay(250);

  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::write(kPinUserLed, true);

  Serial.println("QdecRotaryReporter");
  Serial.println("Connect encoder A to D0, B to D1, and common to GND.");

  if (!g_qdec.begin(kPinD0, kPinD1, QdecSamplePeriod::k1024us,
                    QdecReportPeriod::k1Sample, true,
                    QdecInputPull::kPullUp)) {
    Serial.println("QDEC begin failed");
    while (true) {
      delay(1000);
    }
  }

  g_qdec.start();
}

void loop() {
  if (g_qdec.pollOverflow(true)) {
    Serial.println("QDEC accumulator overflow");
  }

  if (!g_qdec.pollReportReady(true)) {
    delay(1);
    return;
  }

  const int32_t delta = g_qdec.readAndClearAccumulator();
  const uint32_t dbl = g_qdec.readAndClearDoubleTransitions();
  if (delta == 0 && dbl == 0U) {
    return;
  }

  g_position += delta;
  Serial.print("delta=");
  Serial.print(delta);
  Serial.print(" position=");
  Serial.print(g_position);
  Serial.print(" dbl=");
  Serial.println(dbl);
  pulseLed();
}
