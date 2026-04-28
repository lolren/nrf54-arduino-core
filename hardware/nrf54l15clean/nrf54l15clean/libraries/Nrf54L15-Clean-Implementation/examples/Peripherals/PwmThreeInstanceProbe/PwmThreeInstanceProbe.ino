#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

struct InstanceConfig {
  const char* label;
  uint32_t base;
  Pin pin;
  uint32_t frequencyHz;
  uint16_t dutyPermille[4];
};

static constexpr uint32_t kProfileHoldMs = 2000UL;
static constexpr InstanceConfig kConfigs[] = {
    {"PWM20", nrf54l15::PWM20_BASE, kPinD0, 1000UL, {150U, 350U, 550U, 750U}},
    {"PWM21", nrf54l15::PWM21_BASE, kPinD1, 2000UL, {250U, 450U, 650U, 850U}},
    {"PWM22", nrf54l15::PWM22_BASE, kPinD2, 4000UL, {100U, 300U, 500U, 700U}},
};

static Pwm g_pwm20(nrf54l15::PWM20_BASE);
static Pwm g_pwm21(nrf54l15::PWM21_BASE);
static Pwm g_pwm22(nrf54l15::PWM22_BASE);
static Pwm* const g_instances[] = {&g_pwm20, &g_pwm21, &g_pwm22};
static uint8_t g_profileIndex = 0U;

static void printProfile(uint8_t profileIndex) {
  Serial.print("profile=");
  Serial.println(profileIndex);
  for (uint8_t i = 0U; i < (sizeof(kConfigs) / sizeof(kConfigs[0])); ++i) {
    Serial.print(kConfigs[i].label);
    Serial.print(" pin=");
    Serial.print(i);
    Serial.print(" hz=");
    Serial.print(kConfigs[i].frequencyHz);
    Serial.print(" duty=");
    Serial.println(kConfigs[i].dutyPermille[profileIndex]);
  }
}

static bool applyProfile(uint8_t profileIndex) {
  bool ok = true;
  for (uint8_t i = 0U; i < (sizeof(kConfigs) / sizeof(kConfigs[0])); ++i) {
    ok = g_instances[i]->setDutyPermille(kConfigs[i].dutyPermille[profileIndex]) && ok;
  }
  return ok;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println("PwmThreeInstanceProbe");
  Serial.println("Three independent hardware PWM instances on D0/D1/D2.");

  for (uint8_t i = 0U; i < (sizeof(kConfigs) / sizeof(kConfigs[0])); ++i) {
    if (!g_instances[i]->beginSingle(kConfigs[i].pin, kConfigs[i].frequencyHz,
                                     kConfigs[i].dutyPermille[0], true)) {
      Serial.print(kConfigs[i].label);
      Serial.println(" begin failed");
      while (true) {
        delay(1000);
      }
    }
    if (!g_instances[i]->start()) {
      Serial.print(kConfigs[i].label);
      Serial.println(" start failed");
      while (true) {
        delay(1000);
      }
    }
  }

  Serial.println("Pins: D0=PWM20, D1=PWM21, D2=PWM22");
  printProfile(g_profileIndex);
}

void loop() {
  delay(kProfileHoldMs);

  g_profileIndex =
      static_cast<uint8_t>((g_profileIndex + 1U) %
                           (sizeof(kConfigs[0].dutyPermille) /
                            sizeof(kConfigs[0].dutyPermille[0])));
  const bool ok = applyProfile(g_profileIndex);
  printProfile(g_profileIndex);
  if (!ok) {
    Serial.println("profile_update_failed");
  }
}
