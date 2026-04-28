#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

static constexpr Pin kPins[] = {kPinD0, kPinD1, kPinD2, kPinD3};
static constexpr uint32_t kFrequencyHz = 2000UL;
static constexpr uint32_t kProfileHoldMs = 2000UL;
static constexpr uint16_t kProfiles[][4] = {
    {100U, 300U, 500U, 700U},
    {200U, 400U, 600U, 800U},
    {50U, 250U, 750U, 900U},
    {125U, 375U, 625U, 875U},
};

static Pwm g_pwm(nrf54l15::PWM20_BASE);
static uint8_t g_profileIndex = 0U;

static void printProfile(uint8_t profileIndex) {
  Serial.print("profile=");
  Serial.print(profileIndex);
  Serial.print(" duty_permille=");
  for (uint8_t ch = 0U; ch < 4U; ++ch) {
    if (ch != 0U) {
      Serial.print(',');
    }
    Serial.print(kProfiles[profileIndex][ch]);
  }
  Serial.println();
}

static bool applyProfile(uint8_t profileIndex) {
  bool ok = true;
  for (uint8_t ch = 0U; ch < 4U; ++ch) {
    ok = g_pwm.setDutyPermille(ch, kProfiles[profileIndex][ch]) && ok;
  }
  return ok;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println("PwmFourChannelIndependent");
  Serial.println("PWM20 four-channel independent duty example on D0-D3.");
  Serial.print("frequency_hz=");
  Serial.println(kFrequencyHz);
  Serial.println("Pins: D0, D1, D2, D3");

  Pwm::ChannelConfig channels[4];
  for (uint8_t ch = 0U; ch < 4U; ++ch) {
    channels[ch].outPin = kPins[ch];
    channels[ch].dutyPermille = kProfiles[0][ch];
    channels[ch].activeHigh = true;
  }

  if (!g_pwm.beginChannels(channels, 4U, kFrequencyHz)) {
    Serial.println("PWM begin failed");
    while (true) {
      delay(1000);
    }
  }

  if (!g_pwm.start()) {
    Serial.println("PWM start failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.print("configured_mask=0x");
  Serial.println(g_pwm.configuredChannelMask(), HEX);
  printProfile(0U);
}

void loop() {
  delay(kProfileHoldMs);

  g_profileIndex =
      static_cast<uint8_t>((g_profileIndex + 1U) %
                           (sizeof(kProfiles) / sizeof(kProfiles[0])));
  const bool ok = applyProfile(g_profileIndex);
  printProfile(g_profileIndex);
  if (!ok) {
    Serial.println("profile_update_failed");
  }
}
