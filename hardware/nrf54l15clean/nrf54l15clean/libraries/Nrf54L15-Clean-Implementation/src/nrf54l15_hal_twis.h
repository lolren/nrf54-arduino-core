#pragma once

#include <stdint.h>
#include <string.h>

#include "nrf54l15_regs.h"

namespace xiao_nrf54l15 {

// TWIS — Two-Wire Interface Slave (I2C Target)
//
// Supports I2C slave mode with programmable address, receive/transmit buffers,
// and events for address match, read/write commands.
//
// Datasheet chapter 4.2.6 (TWIS — Two-wire interface slave).
class Twis {
 public:
  explicit Twis(uint32_t base = nrf54l15::TWIS21_BASE);

  bool begin(const Pin& scl, const Pin& sda,
             uint8_t addr,
             uint8_t* rxBuf = nullptr, size_t rxLen = 0,
             const uint8_t* txBuf = nullptr, size_t txLen = 0);

  void end();

  bool setAddress(uint8_t idx, uint8_t addr);

  void setRxBuf(uint8_t* buf, size_t len);
  void setTxBuf(const uint8_t* buf, size_t len);

  bool stopped(bool clear = true);
  bool errorDetected(bool clear = true);
  bool writeReceived(bool clear = true);
  bool readReceived(bool clear = true);

  bool isEnabled() const;
  uint32_t base() const;

  uint16_t txAmount() const;
  uint16_t rxAmount() const;

  void setOverReadChar(uint8_t orc);

  void enableWriteInterrupt(bool en = true);
  void enableReadInterrupt(bool en = true);
  void enableStoppedInterrupt(bool en = true);
  void enableErrorInterrupt(bool en = true);

 private:
  uint32_t base_;
  uint8_t* rxBuf_;
  const uint8_t* txBuf_;
  size_t rxBufLen_;
  size_t txBufLen_;

  inline uint32_t rd(uint32_t off) const;
  inline void wr(uint32_t off, uint32_t val);
};

// Inline implementations.

inline Twis::Twis(uint32_t base)
    : base_(base), rxBuf_(nullptr), txBuf_(nullptr),
      rxBufLen_(0), txBufLen_(0) {}

inline bool Twis::begin(const Pin& scl, const Pin& sda,
                        uint8_t addr,
                        uint8_t* rxBuf, size_t rxLen,
                        const uint8_t* txBuf, size_t txLen) {
  if (base_ == 0) return false;

  // Disconnect pins first
  wr(0x600, 0xFFFFFFFFUL);  // PSEL.SCL
  wr(0x604, 0xFFFFFFFFUL);  // PSEL.SDA

  // Connect pins using make_psel
  wr(0x600, nrf54l15::make_psel(scl.port, scl.pin));
  wr(0x604, nrf54l15::make_psel(sda.port, sda.pin));

  setAddress(0, addr);
  setRxBuf(rxBuf, rxLen);
  setTxBuf(txBuf, txLen);

  // Enable TWIS (ENABLE = 7)
  wr(0x500, 7);
  return true;
}

inline void Twis::end() {
  wr(0x500, 0);
  wr(0x600, 0xFFFFFFFFUL);
  wr(0x604, 0xFFFFFFFFUL);
}

inline bool Twis::setAddress(uint8_t idx, uint8_t addr) {
  if (idx > 1) return false;
  wr(0x588 + idx * 4, addr & 0x7F);
  return true;
}

inline void Twis::setRxBuf(uint8_t* buf, size_t len) {
  rxBuf_ = buf;
  rxBufLen_ = len;
  if (buf) {
    wr(0x704, reinterpret_cast<uint32_t>(buf));
    wr(0x708, len);
  }
}

inline void Twis::setTxBuf(const uint8_t* buf, size_t len) {
  txBuf_ = buf;
  txBufLen_ = len;
  if (buf) {
    wr(0x744, reinterpret_cast<uint32_t>(const_cast<uint8_t*>(buf)));
    wr(0x748, len);
  }
}

inline bool Twis::stopped(bool clear) {
  uint32_t val = rd(0x104);
  if (clear) wr(0x104, 0);
  return val != 0;
}

inline bool Twis::errorDetected(bool clear) {
  uint32_t val = rd(0x114);
  if (clear) wr(0x114, 0);
  return val != 0;
}

inline bool Twis::writeReceived(bool clear) {
  uint32_t val = rd(0x13C);
  if (clear) wr(0x13C, 0);
  return val != 0;
}

inline bool Twis::readReceived(bool clear) {
  uint32_t val = rd(0x140);
  if (clear) wr(0x140, 0);
  return val != 0;
}

inline bool Twis::isEnabled() const {
  return (rd(0x500) & 0x01) != 0;
}

inline uint32_t Twis::base() const { return base_; }

inline uint16_t Twis::txAmount() const {
  return static_cast<uint16_t>(rd(0x74C));
}

inline uint16_t Twis::rxAmount() const {
  return static_cast<uint16_t>(rd(0x70C));
}

inline void Twis::setOverReadChar(uint8_t orc) {
  wr(0x5C0, orc);
}

inline void Twis::enableWriteInterrupt(bool en) {
  if (en) wr(0x304, (1UL << 5));
  else     wr(0x308, (1UL << 5));
}

inline void Twis::enableReadInterrupt(bool en) {
  if (en) wr(0x304, (1UL << 6));
  else     wr(0x308, (1UL << 6));
}

inline void Twis::enableStoppedInterrupt(bool en) {
  if (en) wr(0x304, (1UL << 1));
  else     wr(0x308, (1UL << 1));
}

inline void Twis::enableErrorInterrupt(bool en) {
  if (en) wr(0x304, (1UL << 2));
  else     wr(0x308, (1UL << 2));
}

inline uint32_t Twis::rd(uint32_t off) const {
  return *reinterpret_cast<const volatile uint32_t*>(base_ + off);
}

inline void Twis::wr(uint32_t off, uint32_t val) {
  *reinterpret_cast<volatile uint32_t*>(base_ + off) = val;
}

}  // namespace xiao_nrf54l15
