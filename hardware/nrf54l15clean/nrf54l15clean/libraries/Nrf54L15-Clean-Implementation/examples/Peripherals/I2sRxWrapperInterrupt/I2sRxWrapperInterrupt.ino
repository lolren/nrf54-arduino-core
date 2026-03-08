#include <Arduino.h>
#include <cmsis.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

static constexpr uint32_t kFrameWordCount = 64U;
static constexpr uint32_t kHeartbeatMs = 1000U;
static constexpr uint32_t kStopCycleMs = 4000U;

alignas(4) uint32_t gFrames[2][kFrameWordCount];
I2sRx gI2s;
uint32_t gLastHeartbeatMs = 0U;
uint32_t gLastStopMs = 0U;

struct CaptureState {
  volatile uint32_t bufferCount = 0U;
  volatile uint32_t firstWord = 0U;
  volatile uint32_t xorWord = 0U;
};

CaptureState gCaptureState{};

void ledOn() { (void)Gpio::write(kPinUserLed, false); }
void ledOff() { (void)Gpio::write(kPinUserLed, true); }

void pulse(uint16_t onMs = 30U, uint16_t offMs = 0U) {
  ledOn();
  delay(onMs);
  ledOff();
  if (offMs > 0U) {
    delay(offMs);
  }
}

void captureBuffer(uint32_t* buffer, uint32_t wordCount, void* context) {
  if (buffer == nullptr || context == nullptr || wordCount != kFrameWordCount) {
    return;
  }

  auto* state = static_cast<CaptureState*>(context);
  uint32_t folded = 0U;
  for (uint32_t i = 0; i < wordCount; ++i) {
    folded ^= buffer[i];
  }

  state->firstWord = buffer[0];
  state->xorWord = folded;
  ++state->bufferCount;
}

void configureBoard() {
  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  ledOff();

  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && ((millis() - start) < 1500U)) {
  }

  BoardControl::setBatterySenseEnabled(false);
  BoardControl::setImuMicEnabled(false);
  BoardControl::collapseRfPathIdle();
}

I2sRxConfig makeConfig() {
  I2sRxConfig config;
  config.sdin = kPinD11;
  config.lrck = kPinD12;
  config.sck = kPinD13;
  config.mck = kPinD14;
  config.sdout = kPinDisconnected;
  config.autoRestart = true;
  return config;
}

}  // namespace

extern "C" void I2S20_IRQHandler(void) { I2sRx::irqHandler(); }

void setup() {
  configureBoard();

  if (!gI2s.begin(makeConfig(), gFrames[0], gFrames[1], kFrameWordCount)) {
    Serial.println(F("I2S RX wrapper begin failed"));
    while (true) {
      pulse(80U, 200U);
      delay(600U);
    }
  }

  gI2s.setReceiveCallback(captureBuffer, &gCaptureState);

  if (!gI2s.makeActive() || !gI2s.start()) {
    Serial.println(F("I2S RX wrapper start failed"));
    while (true) {
      pulse(80U, 200U);
      delay(600U);
    }
  }

  Serial.println(F("I2S RX wrapper interrupt example"));
  Serial.println(F("Pins: SDIN=D11 LRCK=D12 SCK=D13 MCK=D14"));
  Serial.println(F("Leave SDIN floating for IRQ-path smoke testing, or feed D11 from an external I2S source for real capture."));
  pulse(40U, 80U);
}

void loop() {
  gI2s.service();

  const uint32_t now = millis();
  if (gI2s.running() && ((now - gLastStopMs) >= kStopCycleMs)) {
    gLastStopMs = now;
    (void)gI2s.stop();
  }

  if ((now - gLastHeartbeatMs) >= kHeartbeatMs) {
    gLastHeartbeatMs = now;
    Serial.print(F("I2S RXPTRUPD="));
    Serial.print(gI2s.rxPtrUpdCount());
    Serial.print(F(" STOPPED="));
    Serial.print(gI2s.stoppedCount());
    Serial.print(F(" restarts="));
    Serial.print(gI2s.restartCount());
    Serial.print(F(" stop_cycles="));
    Serial.print(gI2s.manualStopCount());
    Serial.print(F(" callbacks="));
    Serial.print(gCaptureState.bufferCount);
    Serial.print(F(" first=0x"));
    Serial.print(gCaptureState.firstWord, HEX);
    Serial.print(F(" fold=0x"));
    Serial.print(gCaptureState.xorWord, HEX);
    Serial.print(F(" running="));
    Serial.println(gI2s.running() ? F("yes") : F("no"));
    pulse(10U, 0U);
  }

  delay(10U);
}
