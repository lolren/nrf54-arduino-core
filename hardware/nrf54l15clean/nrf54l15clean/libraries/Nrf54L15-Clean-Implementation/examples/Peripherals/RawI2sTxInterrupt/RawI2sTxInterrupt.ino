#include <Arduino.h>
#include <cmsis.h>
#include <nrf54l15.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

// Default XIAO back-pad route:
// D11 -> SDOUT
// D12 -> LRCK
// D13 -> SCK
// D14 -> MCK
static constexpr uint8_t kI2sSdoutPin = PIN_D11;
static constexpr uint8_t kI2sLrckPin = PIN_D12;
static constexpr uint8_t kI2sSckPin = PIN_D13;
static constexpr uint8_t kI2sMckPin = PIN_D14;
static constexpr uint8_t kI2sSdinPin = 0xFFU;

static constexpr uint32_t kFrameWordCount = 64U;
static constexpr uint32_t kHeartbeatMs = 1000U;
static constexpr uint32_t kStopCycleMs = 4000U;

alignas(4) uint32_t gI2sFrames[2][kFrameWordCount];
volatile uint32_t gTxPtrUpdCount = 0U;
volatile uint32_t gStoppedCount = 0U;
volatile uint32_t gRestartCount = 0U;
volatile uint32_t gManualStopCount = 0U;
volatile uint8_t gNextBufferIndex = 1U;
volatile bool gRestartRequested = false;
volatile bool gStreamRunning = false;
uint32_t gLastHeartbeatMs = 0U;
uint32_t gLastStopMs = 0U;

void ledOn() {
  (void)Gpio::write(kPinUserLed, false);
}

void ledOff() {
  (void)Gpio::write(kPinUserLed, true);
}

void pulse(uint16_t onMs = 30U, uint16_t offMs = 0U) {
  ledOn();
  delay(onMs);
  ledOff();
  if (offMs > 0U) {
    delay(offMs);
  }
}

uint32_t makeI2sPsel(uint8_t pin) {
  if (pin == 0xFFU) {
    return 0xFFFFFFFFUL;
  }

  uint8_t port = 0U;
  uint8_t pinInPort = 0U;
  if (!pinToPortPin(pin, &port, &pinInPort)) {
    return 0xFFFFFFFFUL;
  }

  return ((uint32_t)pinInPort << I2S_PSEL_SCK_PIN_Pos) |
         ((uint32_t)port << I2S_PSEL_SCK_PORT_Pos) |
         (I2S_PSEL_SCK_CONNECT_Connected << I2S_PSEL_SCK_CONNECT_Pos);
}

void fillStereoBuffer(uint32_t *frames, uint8_t phaseOffset) {
  for (uint32_t i = 0; i < kFrameWordCount; ++i) {
    const bool high = (((i / 8U) + phaseOffset) & 1U) != 0U;
    const int16_t sample = high ? 12000 : -12000;
    const uint16_t word = static_cast<uint16_t>(sample);
    frames[i] = ((uint32_t)word << 16U) | word;
  }
}

void fillStereoBuffers() {
  fillStereoBuffer(gI2sFrames[0], 0U);
  fillStereoBuffer(gI2sFrames[1], 1U);
}

void armTxBuffer(uint8_t bufferIndex) {
  NRF_I2S->TXD.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gI2sFrames[bufferIndex])) &
      I2S_TXD_PTR_PTR_Msk;
}

void clearI2sEvents() {
  NRF_I2S->EVENTS_TXPTRUPD = 0U;
  NRF_I2S->EVENTS_STOPPED = 0U;
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

void configureI2s() {
  NRF_I2S->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  NRF_I2S->ENABLE =
      (I2S_ENABLE_ENABLE_Disabled << I2S_ENABLE_ENABLE_Pos) &
      I2S_ENABLE_ENABLE_Msk;

  NRF_I2S->PSEL.MCK = makeI2sPsel(kI2sMckPin);
  NRF_I2S->PSEL.SCK = makeI2sPsel(kI2sSckPin);
  NRF_I2S->PSEL.LRCK = makeI2sPsel(kI2sLrckPin);
  NRF_I2S->PSEL.SDIN = makeI2sPsel(kI2sSdinPin);
  NRF_I2S->PSEL.SDOUT = makeI2sPsel(kI2sSdoutPin);

  NRF_I2S->CONFIG.MODE =
      (I2S_CONFIG_MODE_MODE_Master << I2S_CONFIG_MODE_MODE_Pos) &
      I2S_CONFIG_MODE_MODE_Msk;
  NRF_I2S->CONFIG.RXEN =
      (I2S_CONFIG_RXEN_RXEN_Disabled << I2S_CONFIG_RXEN_RXEN_Pos) &
      I2S_CONFIG_RXEN_RXEN_Msk;
  NRF_I2S->CONFIG.TXEN =
      (I2S_CONFIG_TXEN_TXEN_Enabled << I2S_CONFIG_TXEN_TXEN_Pos) &
      I2S_CONFIG_TXEN_TXEN_Msk;
  NRF_I2S->CONFIG.MCKEN =
      (I2S_CONFIG_MCKEN_MCKEN_Enabled << I2S_CONFIG_MCKEN_MCKEN_Pos) &
      I2S_CONFIG_MCKEN_MCKEN_Msk;
  NRF_I2S->CONFIG.MCKFREQ = I2S_CONFIG_MCKFREQ_MCKFREQ_32MDIV8;
  NRF_I2S->CONFIG.RATIO =
      (I2S_CONFIG_RATIO_RATIO_256X << I2S_CONFIG_RATIO_RATIO_Pos) &
      I2S_CONFIG_RATIO_RATIO_Msk;
  NRF_I2S->CONFIG.SWIDTH =
      (I2S_CONFIG_SWIDTH_SWIDTH_16Bit << I2S_CONFIG_SWIDTH_SWIDTH_Pos) &
      I2S_CONFIG_SWIDTH_SWIDTH_Msk;
  NRF_I2S->CONFIG.ALIGN =
      (I2S_CONFIG_ALIGN_ALIGN_Left << I2S_CONFIG_ALIGN_ALIGN_Pos) &
      I2S_CONFIG_ALIGN_ALIGN_Msk;
  NRF_I2S->CONFIG.FORMAT =
      (I2S_CONFIG_FORMAT_FORMAT_I2S << I2S_CONFIG_FORMAT_FORMAT_Pos) &
      I2S_CONFIG_FORMAT_FORMAT_Msk;
  NRF_I2S->CONFIG.CHANNELS =
      (I2S_CONFIG_CHANNELS_CHANNELS_Stereo << I2S_CONFIG_CHANNELS_CHANNELS_Pos) &
      I2S_CONFIG_CHANNELS_CHANNELS_Msk;

  NRF_I2S->RXTXD.MAXCNT =
      (kFrameWordCount << I2S_RXTXD_MAXCNT_MAXCNT_Pos) &
      I2S_RXTXD_MAXCNT_MAXCNT_Msk;

  NRF_I2S->INTENCLR = I2S_INTENCLR_TXPTRUPD_Msk | I2S_INTENCLR_STOPPED_Msk;
  clearI2sEvents();
  armTxBuffer(0U);

  NRF_I2S->ENABLE =
      (I2S_ENABLE_ENABLE_Enabled << I2S_ENABLE_ENABLE_Pos) &
      I2S_ENABLE_ENABLE_Msk;

  NVIC_SetPriority(I2S20_IRQn, 3U);
  NVIC_EnableIRQ(I2S20_IRQn);
}

void startI2sStream() {
  clearI2sEvents();
  gNextBufferIndex = 1U;
  armTxBuffer(0U);
  NRF_I2S->INTENSET = I2S_INTENSET_TXPTRUPD_Msk | I2S_INTENSET_STOPPED_Msk;
  gStreamRunning = true;
  NRF_I2S->TASKS_START = I2S_TASKS_START_TASKS_START_Trigger;
}

void requestStopCycle() {
  if (!gStreamRunning) {
    return;
  }

  ++gManualStopCount;
  NRF_I2S->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
}

void printStatus() {
  noInterrupts();
  const uint32_t txPtrUpdCount = gTxPtrUpdCount;
  const uint32_t stoppedCount = gStoppedCount;
  const uint32_t restartCount = gRestartCount;
  const uint32_t manualStopCount = gManualStopCount;
  const bool streamRunning = gStreamRunning;
  interrupts();

  Serial.print(F("I2S IRQ TXPTRUPD="));
  Serial.print(txPtrUpdCount);
  Serial.print(F(" STOPPED="));
  Serial.print(stoppedCount);
  Serial.print(F(" restarts="));
  Serial.print(restartCount);
  Serial.print(F(" stop_cycles="));
  Serial.print(manualStopCount);
  Serial.print(F(" running="));
  Serial.println(streamRunning ? F("yes") : F("no"));
}

}  // namespace

extern "C" void I2S20_IRQHandler(void) {
  if (NRF_I2S->EVENTS_TXPTRUPD != 0U) {
    NRF_I2S->EVENTS_TXPTRUPD = 0U;
    armTxBuffer(gNextBufferIndex);
    gNextBufferIndex ^= 1U;
    ++gTxPtrUpdCount;
  }

  if (NRF_I2S->EVENTS_STOPPED != 0U) {
    NRF_I2S->EVENTS_STOPPED = 0U;
    gStreamRunning = false;
    gRestartRequested = true;
    ++gStoppedCount;
  }
}

void setup() {
  configureBoard();
  fillStereoBuffers();
  configureI2s();
  startI2sStream();

  Serial.println(F("Raw NRF_I2S TX interrupt example"));
  Serial.print(F("I2S base: 0x"));
  Serial.println((uint32_t)(uintptr_t)NRF_I2S, HEX);
  Serial.println(F("Pins: SDOUT=D11 LRCK=D12 SCK=D13 MCK=D14"));
  Serial.println(F("Interrupts: TXPTRUPD keeps the stream armed, STOPPED triggers restart."));
  pulse(40U, 80U);
}

void loop() {
  if (gRestartRequested) {
    noInterrupts();
    const bool restartRequested = gRestartRequested;
    gRestartRequested = false;
    interrupts();

    if (restartRequested) {
      ++gRestartCount;
      startI2sStream();
    }
  }

  const uint32_t now = millis();
  if (gStreamRunning && ((now - gLastStopMs) >= kStopCycleMs)) {
    gLastStopMs = now;
    requestStopCycle();
  }

  if ((now - gLastHeartbeatMs) >= kHeartbeatMs) {
    gLastHeartbeatMs = now;
    printStatus();
    pulse(10U, 0U);
  }

  delay(10);
}
