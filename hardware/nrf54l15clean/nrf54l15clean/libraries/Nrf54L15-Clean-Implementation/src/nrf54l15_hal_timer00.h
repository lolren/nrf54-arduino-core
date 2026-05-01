#pragma once

#include <stdint.h>

#include "nrf54l15_regs.h"

namespace xiao_nrf54l15 {

// TIMER00 — MCU-Domain Timer (128 MHz pclk, 6 compare channels)
//
// TIMER00 runs in the MCU domain with a 128 MHz peripheral clock,
// giving ~7.8125 ns tick resolution at prescaler 0.
//
// Datasheet chapter 4.1.5 (TIMER — Timer).
class Timer00 {
 public:
  Timer00() : base_(nrf54l15::TIMER00_BASE) {}
  explicit Timer00(uint32_t base) : base_(base) {}

  // ---- Configuration ----

  bool begin(uint8_t bitWidth = 3,
             uint8_t prescaler = 4,
             bool counterMode = false) {
    if (base_ == 0) return false;
    wr(0x508, bitWidth & 0x3);      // BITMODE
    wr(0x510, prescaler & 0xF);     // PRESCALER
    wr(0x504, counterMode ? 1 : 0); // MODE
    return true;
  }

  uint32_t timerHz() const {
    uint8_t pres = rd(0x510) & 0xF;
    return 128000000UL >> pres;
  }

  uint32_t ticksFromMicros(uint32_t us) const {
    return (us * timerHz()) / 1000000;
  }

  // ---- Tasks ----

  void start() { wr(0x000, 1); }
  void stop()  { wr(0x004, 1); }
  void count() { wr(0x008, 1); }
  void clear() { wr(0x00C, 1); }

  // ---- Counter value ----

  uint32_t counterValue() {
    return rd(0x54C);  // CNTVAL
  }

  // ---- Compare channels (6 channels) ----

  bool setCompare(uint8_t channel, uint32_t value) {
    if (channel > 5) return false;
    wr(0x540 + channel * 4, value);
    return true;
  }

  uint32_t getCompare(uint8_t channel) {
    if (channel > 5) return 0;
    return rd(0x540 + channel * 4);
  }

  bool setCompareWithAction(uint8_t channel, uint32_t value,
                            bool autoStop = false,
                            bool autoClear = false) {
    if (!setCompare(channel, value)) return false;
    if (autoStop) {
      wr(0x200, rd(0x200) | (1U << (16 + channel * 1)));
    }
    return true;
  }

  // ---- Events ----

  bool pollCompare(uint8_t channel, bool clearEvent = true) {
    if (channel > 5) return false;
    uint32_t off = 0x140 + channel * 4;
    uint32_t val = rd(off);
    if (clearEvent) wr(off, 0);
    return val != 0;
  }

  // ---- Interrupts ----

  void enableCompareInterrupt(uint8_t channel, bool enable = true) {
    if (channel > 5) return;
    if (enable) {
      wr(0x304, (1UL << (16 + channel * 4)));
    } else {
      wr(0x308, (1UL << (16 + channel * 4)));
    }
  }

  // ---- One-shot mode ----

  void setOneShot(uint8_t channel, bool enable = true) {
    if (channel > 5) return;
    uint32_t val = rd(0x580);
    if (enable) val |= (1U << channel);
    else        val &= ~(1U << channel);
    wr(0x580, val);
  }

  // ---- PPI publish/subscribe ----

  volatile uint32_t* publishCompareRegister(uint8_t channel) const {
    if (channel > 5) return nullptr;
    return reinterpret_cast<volatile uint32_t*>(base_ + 0x1C0 + channel * 4);
  }

  volatile uint32_t* subscribeStartRegister() const {
    return reinterpret_cast<volatile uint32_t*>(base_ + 0x080);
  }

  volatile uint32_t* subscribeStopRegister() const {
    return reinterpret_cast<volatile uint32_t*>(base_ + 0x084);
  }

  volatile uint32_t* subscribeCountRegister() const {
    return reinterpret_cast<volatile uint32_t*>(base_ + 0x088);
  }

  volatile uint32_t* subscribeClearRegister() const {
    return reinterpret_cast<volatile uint32_t*>(base_ + 0x08C);
  }

  // ---- Base access ----

  uint32_t base() const { return base_; }

 private:
  uint32_t base_;

  inline uint32_t rd(uint32_t off) const {
    return *reinterpret_cast<const volatile uint32_t*>(base_ + off);
  }

  inline void wr(uint32_t off, uint32_t val) {
    *reinterpret_cast<volatile uint32_t*>(base_ + off) = val;
  }
};

}  // namespace xiao_nrf54l15
