#include <Arduino.h>
#include <nrf54l15.h>

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

alignas(4) uint32_t gI2sFrames[64];
bool gI2sStarted = false;
uint32_t gLastHeartbeatMs = 0U;

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

void fillStereoSquareWave() {
  for (size_t i = 0; i < (sizeof(gI2sFrames) / sizeof(gI2sFrames[0])); ++i) {
    const int16_t sample = ((i / 8U) & 1U) ? 12000 : -12000;
    const uint16_t word = (uint16_t)sample;
    gI2sFrames[i] = ((uint32_t)word << 16U) | word;
  }
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

  NRF_I2S->TXD.PTR =
      reinterpret_cast<uint32_t>(gI2sFrames) & I2S_TXD_PTR_PTR_Msk;
  NRF_I2S->RXTXD.MAXCNT =
      ((sizeof(gI2sFrames) / sizeof(gI2sFrames[0])) << I2S_RXTXD_MAXCNT_MAXCNT_Pos) &
      I2S_RXTXD_MAXCNT_MAXCNT_Msk;
  NRF_I2S->EVENTS_TXPTRUPD = 0U;
  NRF_I2S->EVENTS_STOPPED = 0U;

  NRF_I2S->ENABLE =
      (I2S_ENABLE_ENABLE_Enabled << I2S_ENABLE_ENABLE_Pos) &
      I2S_ENABLE_ENABLE_Msk;
}

void startI2s() {
  NRF_I2S->TASKS_START = I2S_TASKS_START_TASKS_START_Trigger;
  gI2sStarted = true;
}

void keepTxBufferArmed() {
  if (NRF_I2S->EVENTS_TXPTRUPD != 0U) {
    NRF_I2S->EVENTS_TXPTRUPD = 0U;
    NRF_I2S->TXD.PTR =
        reinterpret_cast<uint32_t>(gI2sFrames) & I2S_TXD_PTR_PTR_Msk;
  }

  if (NRF_I2S->EVENTS_STOPPED != 0U) {
    NRF_I2S->EVENTS_STOPPED = 0U;
    startI2s();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  fillStereoSquareWave();
  configureI2s();
  startI2s();

  Serial.println(F("Raw NRF_I2S TX loop"));
  Serial.print(F("I2S base: 0x"));
  Serial.println((uint32_t)(uintptr_t)NRF_I2S, HEX);
  Serial.println(F("Pins: SDOUT=D11 LRCK=D12 SCK=D13 MCK=D14"));
}

void loop() {
  keepTxBufferArmed();
  const uint32_t now = millis();
  if ((now - gLastHeartbeatMs) >= 1000U) {
    gLastHeartbeatMs = now;
    Serial.println(F("I2S TX running"));
  }

  delay(10);
}
