#include <Arduino.h>

#include "nrf54l15_hal.h"
#include <nrf54l15.h>

using namespace xiao_nrf54l15;

namespace {

static constexpr uint8_t kRadioFrequencyOffsetMhz = 8U;  // 2408 MHz
static constexpr uint32_t kAddressBase0 = 0xC2C2C2C2UL;
static constexpr uint8_t kAddressPrefix0 = 0xC2U;
static constexpr uint32_t kCrcPolynomial = 0x11021UL;
static constexpr uint32_t kCrcInit = 0xFFFFUL;
static constexpr uint32_t kRadioSpinLimit = 3000000UL;

static constexpr uint8_t kPacketTypeData = 0xA1U;
static constexpr uint8_t kPacketTypeAck = 0xA2U;
static constexpr uint8_t kDataPayloadLength = 6U;
static constexpr uint8_t kAckPayloadLength = 3U;

PowerManager gPower;
alignas(4) uint8_t gRxPacket[1U + 16U];
alignas(4) uint8_t gTxPacket[1U + kAckPayloadLength];
bool gReceiverArmed = false;
uint32_t gAckedCount = 0U;
uint32_t gCrcErrorCount = 0U;
uint32_t gIgnoredCount = 0U;
uint32_t gLastHeartbeatMs = 0U;

void ledOn() {
  (void)Gpio::write(kPinUserLed, false);
}

void ledOff() {
  (void)Gpio::write(kPinUserLed, true);
}

void pulse(uint8_t count, uint16_t onMs = 20U, uint16_t offMs = 35U) {
  for (uint8_t i = 0; i < count; ++i) {
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
    for (uint8_t i = 0; i < stage; ++i) {
      pulse(1U, 90U, 120U);
    }
    delay(900U);
  }
}

bool waitRadioDisabled(uint32_t spinLimit) {
  while (spinLimit-- > 0U) {
    if (NRF_RADIO->EVENTS_DISABLED != 0U) {
      return true;
    }
    const uint32_t state =
        (NRF_RADIO->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (state == RADIO_STATE_STATE_Disabled) {
      return true;
    }
  }
  return false;
}

void clearRadioEvents() {
  NRF_RADIO->EVENTS_READY = 0U;
  NRF_RADIO->EVENTS_TXREADY = 0U;
  NRF_RADIO->EVENTS_RXREADY = 0U;
  NRF_RADIO->EVENTS_ADDRESS = 0U;
  NRF_RADIO->EVENTS_PAYLOAD = 0U;
  NRF_RADIO->EVENTS_END = 0U;
  NRF_RADIO->EVENTS_PHYEND = 0U;
  NRF_RADIO->EVENTS_DISABLED = 0U;
  NRF_RADIO->EVENTS_CRCOK = 0U;
  NRF_RADIO->EVENTS_CRCERROR = 0U;
  NRF_RADIO->EVENTS_RXADDRESS = 0U;
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
  if (!BoardControl::enableRfPath(BoardAntennaPath::kCeramic)) {
    failStage(1);
  }
}

bool configureRadio() {
  if (!ClockControl::startHfxo(true, 1500000UL)) {
    return false;
  }

  gPower.setLatencyMode(PowerLatencyMode::kConstantLatency);

  NRF_RADIO->SHORTS = 0U;
  NRF_RADIO->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  (void)waitRadioDisabled(200000UL);
  NRF_RADIO->TASKS_SOFTRESET = RADIO_TASKS_SOFTRESET_TASKS_SOFTRESET_Trigger;
  clearRadioEvents();

  NRF_RADIO->TIMING =
      (RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) & RADIO_TIMING_RU_Msk;
  NRF_RADIO->MODE =
      (RADIO_MODE_MODE_Nrf_1Mbit << RADIO_MODE_MODE_Pos) &
      RADIO_MODE_MODE_Msk;
  NRF_RADIO->TXPOWER =
      (RADIO_TXPOWER_TXPOWER_Neg8dBm << RADIO_TXPOWER_TXPOWER_Pos) &
      RADIO_TXPOWER_TXPOWER_Msk;
  NRF_RADIO->FREQUENCY =
      ((static_cast<uint32_t>(kRadioFrequencyOffsetMhz)
        << RADIO_FREQUENCY_FREQUENCY_Pos) &
       RADIO_FREQUENCY_FREQUENCY_Msk) |
      (0UL << RADIO_FREQUENCY_MAP_Pos);

  uint32_t pcnf0 = 0U;
  pcnf0 |= (8UL << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk;
  pcnf0 |= (RADIO_PCNF0_S1INCL_Automatic << RADIO_PCNF0_S1INCL_Pos) &
           RADIO_PCNF0_S1INCL_Msk;
  pcnf0 |= (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos) &
           RADIO_PCNF0_PLEN_Msk;
  pcnf0 |= (RADIO_PCNF0_CRCINC_Exclude << RADIO_PCNF0_CRCINC_Pos) &
           RADIO_PCNF0_CRCINC_Msk;
  NRF_RADIO->PCNF0 = pcnf0;

  uint32_t pcnf1 = 0U;
  pcnf1 |= (16UL << RADIO_PCNF1_MAXLEN_Pos) & RADIO_PCNF1_MAXLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk;
  pcnf1 |= (4UL << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk;
  pcnf1 |= (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) &
           RADIO_PCNF1_ENDIAN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos) &
           RADIO_PCNF1_WHITEEN_Msk;
  NRF_RADIO->PCNF1 = pcnf1;

  NRF_RADIO->BASE0 = kAddressBase0 & RADIO_BASE0_BASE0_Msk;
  NRF_RADIO->PREFIX0 =
      ((static_cast<uint32_t>(kAddressPrefix0) << RADIO_PREFIX0_AP0_Pos) &
       RADIO_PREFIX0_AP0_Msk);
  NRF_RADIO->TXADDRESS =
      (0UL << RADIO_TXADDRESS_TXADDRESS_Pos) & RADIO_TXADDRESS_TXADDRESS_Msk;
  NRF_RADIO->RXADDRESSES =
      (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos) &
      RADIO_RXADDRESSES_ADDR0_Msk;

  uint32_t crccnf = 0U;
  crccnf |= (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) &
            RADIO_CRCCNF_LEN_Msk;
  crccnf |= (RADIO_CRCCNF_SKIPADDR_Include << RADIO_CRCCNF_SKIPADDR_Pos) &
            RADIO_CRCCNF_SKIPADDR_Msk;
  NRF_RADIO->CRCCNF = crccnf;
  NRF_RADIO->CRCPOLY = kCrcPolynomial & RADIO_CRCPOLY_CRCPOLY_Msk;
  NRF_RADIO->CRCINIT = kCrcInit & RADIO_CRCINIT_CRCINIT_Msk;

  return true;
}

void armReceiver() {
  memset(gRxPacket, 0, sizeof(gRxPacket));
  clearRadioEvents();
  NRF_RADIO->PACKETPTR =
      reinterpret_cast<uint32_t>(gRxPacket) & RADIO_PACKETPTR_PTR_Msk;
  NRF_RADIO->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);
  NRF_RADIO->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  gReceiverArmed = true;
}

void buildAckPacket(uint8_t sequence) {
  gTxPacket[0] = kAckPayloadLength;
  gTxPacket[1] = kPacketTypeAck;
  gTxPacket[2] = sequence;
  gTxPacket[3] = 'K';
}

bool sendAck(uint8_t sequence) {
  buildAckPacket(sequence);
  clearRadioEvents();
  NRF_RADIO->PACKETPTR =
      reinterpret_cast<uint32_t>(gTxPacket) & RADIO_PACKETPTR_PTR_Msk;
  NRF_RADIO->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);
  NRF_RADIO->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  return waitRadioDisabled(kRadioSpinLimit) && (NRF_RADIO->EVENTS_END != 0U);
}

void pollReceiver() {
  if (!gReceiverArmed) {
    armReceiver();
    return;
  }

  if ((NRF_RADIO->EVENTS_CRCOK == 0U) && (NRF_RADIO->EVENTS_CRCERROR == 0U) &&
      (NRF_RADIO->EVENTS_END == 0U)) {
    return;
  }

  if (!waitRadioDisabled(kRadioSpinLimit)) {
    failStage(2);
  }

  const bool crcOk = (NRF_RADIO->EVENTS_CRCOK != 0U) ||
                     (((NRF_RADIO->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
                       RADIO_CRCSTATUS_CRCSTATUS_Pos) ==
                      RADIO_CRCSTATUS_CRCSTATUS_CRCOk);

  const uint8_t payloadLen = min<uint8_t>(gRxPacket[0], sizeof(gRxPacket) - 1U);
  const bool frameOk = crcOk && (payloadLen >= kDataPayloadLength) &&
                       (gRxPacket[1] == kPacketTypeData);

  if (!frameOk) {
    if (crcOk) {
      ++gIgnoredCount;
    } else {
      ++gCrcErrorCount;
    }
    gReceiverArmed = false;
    return;
  }

  const uint8_t sequence = gRxPacket[2];
  const uint8_t attempt = gRxPacket[3];
  const bool acked = sendAck(sequence);

  Serial.print(F("RX seq="));
  Serial.print(sequence);
  Serial.print(F(" attempt="));
  Serial.print(attempt);
  Serial.print(F(" ack="));
  Serial.println(acked ? F("sent") : F("failed"));

  if (acked) {
    ++gAckedCount;
    pulse(2U);
  } else {
    ++gIgnoredCount;
  }

  gReceiverArmed = false;
}

}  // namespace

void setup() {
  configureBoard();
  if (!configureRadio()) {
    failStage(3);
  }

  Serial.println(F("Raw RADIO ACK responder"));
  Serial.println(F("Responder listens for REQ and answers with ACK"));
  Serial.println(F("Pair with RawRadioAckRequester on the same RF channel"));
  pulse(1U, 45U, 80U);
}

void loop() {
  pollReceiver();

  const uint32_t now = millis();
  if ((now - gLastHeartbeatMs) >= 1000U) {
    gLastHeartbeatMs = now;
    Serial.print(F("ACK sent="));
    Serial.print(gAckedCount);
    Serial.print(F(" ignored="));
    Serial.print(gIgnoredCount);
    Serial.print(F(" crcerr="));
    Serial.println(gCrcErrorCount);
  }

  delay(5U);
}
