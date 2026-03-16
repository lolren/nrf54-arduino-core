#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static constexpr uint32_t kSpinLimit = 400000UL;
static constexpr uint32_t kPrintPeriodMs = 500UL;

static Saadc g_adc(nrf54l15::SAADC_BASE);
static uint32_t g_lastPrintMs = 0UL;

static bool sampleSingleEndedA0(int32_t* outMilliVolts) {
  if (outMilliVolts == nullptr) {
    return false;
  }

  return g_adc.configureSingleEnded(0U, kPinA0, AdcGain::k2over8, 159, 4, true) &&
         g_adc.sampleMilliVolts(outMilliVolts, kSpinLimit);
}

static bool sampleDifferentialA0A1(int32_t* outMilliVolts) {
  if (outMilliVolts == nullptr) {
    return false;
  }

  return g_adc.configureDifferential(0U, kPinA0, kPinA1, AdcGain::k2over8,
                                     159, 4, true) &&
         g_adc.sampleMilliVoltsSigned(outMilliVolts, kSpinLimit);
}

static void haltWithMessage(const char* message) {
  Serial.println(message);
  while (true) {
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::write(kPinUserLed, true);

  Serial.println("SaadcDifferentialProbe");
  Serial.println("Reads A0 single-ended and A0-A1 differential.");
  Serial.println("Using SAADC 8x oversampling with burst averaging.");
  Serial.println("Send 'c' over Serial to force an offset recalibration.");
  Serial.println("Keep A1 tied to a defined analog level; floating inputs will drift.");

  if (!g_adc.begin(AdcResolution::k12bit, AdcOversample::k8x, kSpinLimit)) {
    haltWithMessage("SAADC begin failed");
  }
}

void loop() {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch == 'c' || ch == 'C') {
      Serial.println(g_adc.calibrate(kSpinLimit) ? "SAADC recalibrated"
                                                 : "SAADC recalibration failed");
    }
  }

  const uint32_t now = millis();
  if ((now - g_lastPrintMs) < kPrintPeriodMs) {
    return;
  }
  g_lastPrintMs = now;

  int32_t a0MilliVolts = 0;
  int32_t diffMilliVolts = 0;
  const bool a0Ok = sampleSingleEndedA0(&a0MilliVolts);
  const bool diffOk = sampleDifferentialA0A1(&diffMilliVolts);

  if (diffOk) {
    (void)Gpio::write(kPinUserLed, !(diffMilliVolts > 0));
  }

  char line[160];
  snprintf(line, sizeof(line),
           "A0=%s(%ldmV) A0-A1=%s(%+ldmV) LED=%s\r\n",
           a0Ok ? "OK" : "FAIL", static_cast<long>(a0MilliVolts),
           diffOk ? "OK" : "FAIL", static_cast<long>(diffMilliVolts),
           (diffOk && diffMilliVolts > 0) ? "ON" : "OFF");
  Serial.print(line);
}
