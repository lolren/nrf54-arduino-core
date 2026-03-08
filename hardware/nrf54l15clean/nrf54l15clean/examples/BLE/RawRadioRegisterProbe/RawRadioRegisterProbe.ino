#include <Arduino.h>
#include <nrf54l15.h>

namespace {

// Raw RADIO register bring-up example.
//
// This does not transmit on-air. It only shows the minimum BLE advertising-mode
// register configuration so low-level ports have a concrete starting point.

static constexpr uint32_t kBleAdvertisingAccessAddress = 0x8E89BED6UL;
static constexpr uint32_t kBleAdvertisingCrcInit = 0x555555UL;
static constexpr uint32_t kBleCrcPolynomial = 0x00065BUL;
static constexpr uint8_t kBleAdvertisingChannelIndex = 37U;

uint8_t gPacket[16] = {
    2U, 0x01U, 0x06U,
    7U, 0x09U, 'R', 'A', 'D', 'I', 'O', '0',
};

uint32_t bleAccessAddressBase(uint32_t accessAddress) {
  return accessAddress >> 8U;
}

uint32_t bleAccessAddressPrefix(uint32_t accessAddress) {
  return accessAddress & 0xFFU;
}

uint32_t bleFrequencyRegister(uint8_t channelIndex) {
  uint32_t mhz = 2U;
  if (channelIndex == 37U) {
    mhz = 2U;
  } else if (channelIndex == 38U) {
    mhz = 26U;
  } else if (channelIndex == 39U) {
    mhz = 80U;
  }
  return ((mhz << RADIO_FREQUENCY_FREQUENCY_Pos) &
          RADIO_FREQUENCY_FREQUENCY_Msk) |
         (0UL << RADIO_FREQUENCY_MAP_Pos);
}

void configureRadioOffAir() {
  // Keep this example deterministic: program the registers, but do not start
  // actual TX or RX tasks.
  NRF_RADIO->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  NRF_RADIO->TASKS_SOFTRESET = RADIO_TASKS_SOFTRESET_TASKS_SOFTRESET_Trigger;

  NRF_RADIO->MODE =
      (RADIO_MODE_MODE_Ble_1Mbit << RADIO_MODE_MODE_Pos) &
      RADIO_MODE_MODE_Msk;
  NRF_RADIO->TXPOWER =
      (RADIO_TXPOWER_TXPOWER_0dBm << RADIO_TXPOWER_TXPOWER_Pos) &
      RADIO_TXPOWER_TXPOWER_Msk;
  NRF_RADIO->FREQUENCY = bleFrequencyRegister(kBleAdvertisingChannelIndex);

  uint32_t pcnf0 = 0U;
  pcnf0 |= (8UL << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk;
  pcnf0 |= (1UL << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk;
  pcnf0 |= (RADIO_PCNF0_S1INCL_Automatic << RADIO_PCNF0_S1INCL_Pos) &
           RADIO_PCNF0_S1INCL_Msk;
  pcnf0 |= (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos) &
           RADIO_PCNF0_PLEN_Msk;
  pcnf0 |= (RADIO_PCNF0_CRCINC_Exclude << RADIO_PCNF0_CRCINC_Pos) &
           RADIO_PCNF0_CRCINC_Msk;
  NRF_RADIO->PCNF0 = pcnf0;

  uint32_t pcnf1 = 0U;
  pcnf1 |= (sizeof(gPacket) << RADIO_PCNF1_MAXLEN_Pos) &
           RADIO_PCNF1_MAXLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk;
  pcnf1 |= (3UL << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk;
  pcnf1 |= (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) &
           RADIO_PCNF1_ENDIAN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) &
           RADIO_PCNF1_WHITEEN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEOFFSET_Include << RADIO_PCNF1_WHITEOFFSET_Pos) &
           RADIO_PCNF1_WHITEOFFSET_Msk;
  NRF_RADIO->PCNF1 = pcnf1;

  NRF_RADIO->BASE0 = bleAccessAddressBase(kBleAdvertisingAccessAddress);
  NRF_RADIO->PREFIX0 =
      ((bleAccessAddressPrefix(kBleAdvertisingAccessAddress)
        << RADIO_PREFIX0_AP0_Pos) &
       RADIO_PREFIX0_AP0_Msk);
  NRF_RADIO->TXADDRESS =
      (0UL << RADIO_TXADDRESS_TXADDRESS_Pos) &
      RADIO_TXADDRESS_TXADDRESS_Msk;
  NRF_RADIO->RXADDRESSES =
      (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos) &
      RADIO_RXADDRESSES_ADDR0_Msk;

  uint32_t crccnf = 0U;
  crccnf |= (RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos) &
            RADIO_CRCCNF_LEN_Msk;
  crccnf |= (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos) &
            RADIO_CRCCNF_SKIPADDR_Msk;
  NRF_RADIO->CRCCNF = crccnf;
  NRF_RADIO->CRCPOLY = kBleCrcPolynomial & RADIO_CRCPOLY_CRCPOLY_Msk;
  NRF_RADIO->CRCINIT = kBleAdvertisingCrcInit & RADIO_CRCINIT_CRCINIT_Msk;
  NRF_RADIO->PACKETPTR =
      reinterpret_cast<uint32_t>(gPacket) & RADIO_PACKETPTR_PTR_Msk;
}

void printRegisterSnapshot() {
  Serial.println(F("Raw NRF_RADIO register probe"));
  Serial.print(F("RADIO base: 0x"));
  Serial.println((uint32_t)(uintptr_t)NRF_RADIO, HEX);
  Serial.print(F("MODE: 0x"));
  Serial.println(NRF_RADIO->MODE, HEX);
  Serial.print(F("TXPOWER: 0x"));
  Serial.println(NRF_RADIO->TXPOWER, HEX);
  Serial.print(F("FREQUENCY: 0x"));
  Serial.println(NRF_RADIO->FREQUENCY, HEX);
  Serial.print(F("PCNF0: 0x"));
  Serial.println(NRF_RADIO->PCNF0, HEX);
  Serial.print(F("PCNF1: 0x"));
  Serial.println(NRF_RADIO->PCNF1, HEX);
  Serial.print(F("BASE0: 0x"));
  Serial.println(NRF_RADIO->BASE0, HEX);
  Serial.print(F("PREFIX0: 0x"));
  Serial.println(NRF_RADIO->PREFIX0, HEX);
  Serial.print(F("PACKETPTR: 0x"));
  Serial.println(NRF_RADIO->PACKETPTR, HEX);
  Serial.println(F("Configured off-air only. Use as a register-level starting point."));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  configureRadioOffAir();
}

void loop() {
  static uint32_t lastPrintMs = 0U;
  const uint32_t now = millis();
  if ((now - lastPrintMs) >= 1000U) {
    lastPrintMs = now;
    printRegisterSnapshot();
  }

  delay(10);
}
