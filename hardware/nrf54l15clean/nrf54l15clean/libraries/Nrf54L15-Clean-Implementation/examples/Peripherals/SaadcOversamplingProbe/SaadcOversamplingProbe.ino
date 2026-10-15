/*
  Exercises single-conversion oversampling on channels 0, 1 and 7.
  Internal AVDD readings need no wiring. External D0 and differential D0/D1
  readings may float without a signal source; their values are diagnostic,
  while PASS means configuration and conversion completed successfully.
*/

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {
constexpr uint32_t kSpinLimit = 2000000UL;
constexpr uint8_t kChannels[] = {0U, 1U, 7U};
struct SampleMode {
  AdcOversample oversample;
  bool burst;
  const char* name;
};
constexpr SampleMode kModes[] = {
    {AdcOversample::kBypass, false, "bypass"},
    {AdcOversample::kBypass, true, "bypass-burst"},
    {AdcOversample::k2x, true, "2x"},
    {AdcOversample::k256x, true, "256x"},
};
const char* const kPaths[] = {"AVDD", "D0", "D0-D1"};
Saadc g_adc;

bool sampleCase(uint8_t channel, uint8_t path, const SampleMode& mode) {
  const bool began = g_adc.begin(AdcResolution::k14bit, mode.oversample,
                                 kSpinLimit);
  bool configured = false;
  if (began && path == 0U) {
    configured = g_adc.configureSingleEnded(channel, AdcInternalInput::kAvdd,
        AdcGain::k2over8, 159U, 4U, mode.burst);
  } else if (began && path == 1U) {
    configured = g_adc.configureSingleEnded(channel, kPinD0,
        AdcGain::k2over8, 159U, 4U, mode.burst);
  } else if (began) {
    configured = g_adc.configureDifferential(channel, kPinD0, kPinD1,
        AdcGain::k2over8, 159U, 4U, mode.burst);
  }
  int32_t milliVolts = 0;
  const bool sampled = configured &&
      g_adc.sampleMilliVoltsSigned(&milliVolts, kSpinLimit);
  g_adc.end();
  Serial.printf("%s channel=%u input=%s mode=%s mv=%ld begin=%u config=%u\n",
      sampled ? "PASS" : "FAIL", static_cast<unsigned>(channel),
      kPaths[path], mode.name, static_cast<long>(milliVolts),
      static_cast<unsigned>(began), static_cast<unsigned>(configured));
  return sampled;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("SaadcOversamplingProbe");
}

void loop() {
  unsigned passed = 0U;
  unsigned failed = 0U;
  for (uint8_t channel : kChannels) {
    for (uint8_t path = 0U; path < 3U; ++path) {
      for (const SampleMode& mode : kModes) {
        if (sampleCase(channel, path, mode)) {
          ++passed;
        } else {
          ++failed;
        }
      }
    }
  }
  Serial.printf("SAADC_SUMMARY passed=%u failed=%u\n", passed, failed);
  delay(3000);
}
