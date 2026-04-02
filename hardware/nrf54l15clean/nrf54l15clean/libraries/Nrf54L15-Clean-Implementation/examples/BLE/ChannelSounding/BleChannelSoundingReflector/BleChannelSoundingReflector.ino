/*
 * BleChannelSoundingReflector
 *
 * Passive partner for BleChannelSoundingInitiator in a two-board BLE Channel
 * Sounding distance measurement setup.
 *
 * The Reflector simply listens on the control channel and echoes back tone
 * probes from the Initiator. It does not compute a distance estimate itself;
 * all processing is done on the Initiator board.
 *
 * Connection: both boards must be within radio range and use the same
 * control channel (37 by default). The Initiator will find the Reflector
 * automatically when the Reflector is running and listening.
 *
 * Serial output:
 *   replies = how many probe exchanges the Reflector has completed.
 *   LED blinks briefly on each successful reply.
 */

#include <Arduino.h>

#include "ble_channel_sounding.h"
#include "nrf54l15_hal.h"

#ifdef ledOn
#undef ledOn
#endif
#ifdef ledOff
#undef ledOff
#endif

using namespace xiao_nrf54l15;

namespace {

// kCeramic: on-board antenna (validated). kExternal: only if physically attached.
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
  config.txPowerDbm = -8;             // TX power for echo tones.
  config.controlChannel = 37U;        // Must match the Initiator's controlChannel.
  config.probeToReportDelayUs = 1200U;// Must match the Initiator.
  config.controlListenWindowUs = 20000U; // How long to listen for a control message.
  config.probeListenWindowUs = 8000U; // How long to listen for each probe tone.
  config.minToneMagnitude = 16U;      // Reject probes below this signal strength.

  // begin() arms the reflector; it will start listening immediately.
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
  // listenAndReflectOnce() blocks until one complete probe exchange is done
  // (or a timeout occurs). Returns true if the exchange was successful.
  // This call should be as tight as possible to avoid missing probe windows.
  if (gCs.listenAndReflectOnce()) {
    ++gReplyCount;
    pulse(1U, 8U, 0U);  // Brief LED blink on successful reply.
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
