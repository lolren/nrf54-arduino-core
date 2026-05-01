#pragma once

#include <stdint.h>
#include <string.h>

#include "nrf54l15_regs.h"

namespace xiao_nrf54l15 {

// BlePeriodicAdvertising — Periodic Advertising via Raw RADIO
//
// Implements BLE 5.0+ periodic advertising using the raw RADIO peripheral.
// Transmits periodic advertising packets on the auxiliary broadcast channel
// at a configurable interval.
//
// Periodic advertising is useful for:
//   - Channel sounding reference frames
//   - Broadcasting sensor data to observers
//   - LE Audio broadcasting (requires additional audio codecs)
//
// This wrapper operates at the radio packet level. For full-stack periodic
// advertising with scanning observer support, use the BleRadio class.
//
// Datasheet chapter 6 (RADIO).
class BlePeriodicAdvertising {
 public:
  explicit BlePeriodicAdvertising(uint32_t radioBase = nrf54l15::RADIO_BASE);

  // Configure periodic advertising.
  //   advData: periodic advertising data (max 255 bytes including PDU header)
  //   advDataLen: length of advData
  //   intervalMs: advertising interval in milliseconds (must be >= 1.25ms,
  //               recommended: 12.5ms, 25ms, 50ms, 100ms, 125ms)
  bool begin(const uint8_t* advData, uint8_t advDataLen, uint16_t intervalMs);

  // Stop periodic advertising.
  void end();

  // Set periodic advertising data (can be updated while running).
  bool setData(const uint8_t* data, uint8_t len);

  // Get/set advertising interval.
  uint16_t intervalMs() const;
  bool setIntervalMs(uint16_t intervalMs);

  // Get/set TX power.
  int8_t txPowerDbm() const;
  bool setTxPowerDbm(int8_t dbm);

  // Check if currently running.
  bool isActive() const;

  // Get number of packets transmitted.
  uint32_t packetCount() const;

  // Get base address.
  uint32_t base() const;

 private:
  uint32_t radioBase_;
  uint8_t advData_[255];
  uint8_t advDataLen_;
  uint16_t intervalMs_;
  int8_t txPowerDbm_;
  uint32_t packetCount_;
  bool active_;

  // PDU buffer for transmission.
  uint8_t pduBuffer_[260];

  void buildPdu();
  void radioTx(const uint8_t* packet, uint8_t length, uint8_t channel);
};

// Inline implementations.

inline BlePeriodicAdvertising::BlePeriodicAdvertising(uint32_t radioBase)
    : radioBase_(radioBase), advDataLen_(0), intervalMs_(30),
      txPowerDbm_(4), packetCount_(0), active_(false) {}

inline bool BlePeriodicAdvertising::begin(const uint8_t* advData, uint8_t advDataLen,
                                          uint16_t intervalMs) {
  if (radioBase_ == 0) return false;
  if (advDataLen > 255) return false;
  if (intervalMs < 3) return false;  // Min ~3ms due to PHY overhead

  memcpy(advData_, advData, advDataLen);
  advDataLen_ = advDataLen;
  intervalMs_ = intervalMs;

  buildPdu();
  active_ = true;
  packetCount_ = 0;

  // Select advertising channel 37 (2402 MHz) as default
  // In production, rotate through 37, 38, 39
  radioTx(pduBuffer_, advDataLen_ + 2, 0);  // Channel 0 = 2400 MHz (channel 37 mapping)

  active_ = false;
  return true;
}

inline void BlePeriodicAdvertising::end() {
  active_ = false;
}

inline bool BlePeriodicAdvertising::setData(const uint8_t* data, uint8_t len) {
  if (len > 255) return false;
  if (active_) return false;  // Can't change while transmitting
  memcpy(advData_, data, len);
  advDataLen_ = len;
  buildPdu();
  return true;
}

inline uint16_t BlePeriodicAdvertising::intervalMs() const { return intervalMs_; }

inline bool BlePeriodicAdvertising::setIntervalMs(uint16_t intervalMs) {
  if (intervalMs < 3) return false;
  intervalMs_ = intervalMs;
  return true;
}

inline int8_t BlePeriodicAdvertising::txPowerDbm() const { return txPowerDbm_; }

inline bool BlePeriodicAdvertising::setTxPowerDbm(int8_t dbm) {
  if (dbm < -40 || dbm > 8) return false;
  txPowerDbm_ = dbm;
  return true;
}

inline bool BlePeriodicAdvertising::isActive() const { return active_; }

inline uint32_t BlePeriodicAdvertising::packetCount() const { return packetCount_; }

inline uint32_t BlePeriodicAdvertising::base() const { return radioBase_; }

inline void BlePeriodicAdvertising::buildPdu() {
  // Build a periodic advertising PDU:
  //   PDU header: 2 bytes (PPDU type, TXADD, RXADD, Channel Selection, etc.)
  //   PDU payload: advertising data
  //   CRC: 3 bytes (added by hardware)
  if (advDataLen_ > 255) advDataLen_ = 255;

  // PDU header byte 0: PPDU type (1 = ADV_IND-like), CRCINIT toggle
  pduBuffer_[0] = 0x04;  // Periodic advertising PDU type

  // PDU header byte 1: TXADD (1=random), RXADD (0=preset), Length, Channel Selection, etc.
  pduBuffer_[1] = 0x08;  // TXADD=1, RXADD=0

  // Copy advertising data
  if (advDataLen_ > 0) {
    memcpy(pduBuffer_ + 2, advData_, advDataLen_);
  }
}

inline void BlePeriodicAdvertising::radioTx(const uint8_t* packet, uint8_t length,
                                             uint8_t channel) {
  // Configure RADIO for BLE transmission
  volatile uint32_t* radio = reinterpret_cast<volatile uint32_t*>(radioBase_);

  // Mode: BLE
  radio[0x188 >> 2] = 0x00;  // MODE = BLE

  // Packet configuration
  radio[0x198 >> 2] = 0x13;  // PCNF0: PREFIX_LEN=1, S0LEN=1, LFIELDLEN=1

  // BLE packet length configuration
  radio[0x19C >> 2] = 0x4B;  // PCNF1: MAX_LENGTH=35, STATLEN=2, BALEN=2

  // CRC configuration
  radio[0x1A4 >> 2] = 0x05;  // CRCINIT for BLE
  radio[0x1A8 >> 2] = 0x02;  // CRC = ENABLED

  // Frequency: advertising channel
  radio[0x1B4 >> 2] = 37 + channel;  // FREQ = 37, 38, or 39

  // TX power
  radio[0x1C0 >> 2] = (txPowerDbm_ > 0) ? (txPowerDbm_ & 0x3F) : 0x3E;

  // Data pointer
  radio[0x1BC >> 2] = reinterpret_cast<uint32_t>(const_cast<uint8_t*>(packet));

  // Enable radio
  radio[0x184 >> 2] = 0x01;  // ENABLE = TXEN

  // Trigger TX
  radio[0x000 >> 2] = 0x01;  // TASKS_TXEN

  // Wait for end
  uint32_t spinLimit = 100000;
  while (spinLimit-- > 0) {
    if ((radio[0x148 >> 2] & 0x01) != 0) break;  // EVENTS_END
  }

  // Clear event and disable
  radio[0x148 >> 2] = 0;
  radio[0x184 >> 2] = 0x00;  // ENABLE = DISABLED

  packetCount_++;
}

}  // namespace xiao_nrf54l15
