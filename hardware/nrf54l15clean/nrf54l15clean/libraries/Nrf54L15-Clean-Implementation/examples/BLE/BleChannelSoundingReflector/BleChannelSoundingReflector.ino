#include <Arduino.h>

#include "ble_channel_sounding.h"
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

static constexpr BoardAntennaPath kAntennaPath = BoardAntennaPath::kCeramic;

static BleChannelSoundingRadio gCs;
static uint32_t gReplyCount = 0U;
static uint32_t gLastLogMs = 0U;

void ledOn() {
  (void)Gpio::write(kPinUserLed, false);
}

void ledOff() {
  (void)Gpio::write(kPinUserLed, true);
}

void pulse(uint8_t count, uint16_t onMs = 25U, uint16_t offMs = 45U) {
  for (uint8_t i = 0U; i < count; ++i) {
    ledOn();
    delay(onMs);
    ledOff();
    if ((i + 1U) < count) {
      delay(offMs);
    }
  }
}

[[noreturn]] void failStage(uint8_t stage) {
  while (true) {
    pulse(stage, 90U, 120U);
    delay(900U);
  }
}

void configureBoard() {
  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  ledOff();

  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && (static_cast<uint32_t>(millis() - start) < 1500U)) {
  }

  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  if (!BoardControl::enableRfPath(kAntennaPath)) {
    failStage(1);
  }
}

}  // namespace

void setup() {
  configureBoard();

  BleCsConfig config;
  config.txPowerDbm = -8;
  config.controlChannel = 37U;
  config.probeToReportDelayUs = 1200U;
  config.controlListenWindowUs = 20000U;
  config.probeListenWindowUs = 8000U;
  config.minToneMagnitude = 16U;

  if (!gCs.begin(config)) {
    failStage(2);
  }

  Serial.println(F("CoreBleChannelSoundingReflector start"));
  Serial.println(F("mode=phase_sounding"));
  Serial.println(F("control_channel=37"));
  Serial.println(F("pair_with=CoreBleChannelSoundingInitiator"));
  pulse(1U, 45U, 80U);
}

void loop() {
  if (gCs.listenAndReflectOnce()) {
    ++gReplyCount;
    pulse(1U, 8U, 0U);
  }

  const uint32_t now = millis();
  if ((now - gLastLogMs) >= 1000U) {
    gLastLogMs = now;
    Serial.print(F("t="));
    Serial.print(now);
    Serial.print(F(" replies="));
    Serial.println(gReplyCount);
  }
}
