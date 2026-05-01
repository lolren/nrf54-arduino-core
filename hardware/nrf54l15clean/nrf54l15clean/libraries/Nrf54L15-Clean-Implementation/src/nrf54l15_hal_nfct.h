#pragma once

#include <stdint.h>

#include "nrf54l15_regs.h"

namespace xiao_nrf54l15 {

// NFC-A Target (NFCT)
// NFC-A listening device per ISO/IEC 14443-A.
// Datasheet chapter 4.2.9 (NFCT — NFC-A target).
class Nfct {
 public:
  // ---- NFCT control ----

  // Start NFCT (begin listening for NFC field).
  inline static void start() {
    writeReg(NFCT_TASKS_START, 1);
  }

  // Stop NFCT.
  inline static void stop() {
    writeReg(NFCT_TASKS_STOP, 1);
  }

  // Check if started event fired.
  inline static bool started(bool clear = true) {
    uint32_t ev = readReg(NFCT_EVENTS_STARTED);
    if (clear) writeReg(NFCT_EVENTS_STARTED, 0);
    return ev != 0;
  }

  // Check if stopped event fired.
  inline static bool stopped(bool clear = true) {
    uint32_t ev = readReg(NFCT_EVENTS_STOPPED);
    if (clear) writeReg(NFCT_EVENTS_STOPPED, 0);
    return ev != 0;
  }

  // ---- Tag header (NFCID1) ----

  // Set NFCID1_3RD_LAST (byte 0-1 of UID, 16-bit value).
  inline static void setNfcId3rdLast(uint16_t value) {
    writeReg(NFCT_TAGHEADER0, (readReg(NFCT_TAGHEADER0) & ~0xFFFF) | value);
  }

  // Set NFCID1_2ND_LAST (byte 2-3 of UID, 16-bit value).
  inline static void setNfcId2ndLast(uint16_t value) {
    writeReg(NFCT_TAGHEADER1, (readReg(NFCT_TAGHEADER1) & ~0xFFFF0000) | (value << 16));
  }

  // Set NFCID1_LAST (byte 4-5 of UID + MFGID).
  inline static void setNfcIdLast(uint8_t uidByte4, uint8_t uidByte5, uint8_t mfgId) {
    writeReg(NFCT_TAGHEADER2,
      ((uint32_t)mfgId) |
      (((uint32_t)uidByte5) << 8) |
      (((uint32_t)uidByte4) << 16));
  }

  // Get current tag header values.
  inline static uint32_t tagHeader0() { return readReg(NFCT_TAGHEADER0); }
  inline static uint32_t tagHeader1() { return readReg(NFCT_TAGHEADER1); }
  inline static uint32_t tagHeader2() { return readReg(NFCT_TAGHEADER2); }
  inline static uint32_t tagHeader3() { return readReg(NFCT_TAGHEADER3); }

  // ---- I/O control ----

  // Set NFCI/O pin polarity.
  inline static void setIoPolarity(bool activeHigh) {
    writeReg(NFCT_IOCONFIG, activeHigh ? 1 : 0);
  }

  // Enable auto-response mode.
  inline static void enableAutoResponse(bool enable = true) {
    writeReg(NFCT_AUTACKRESPONSE, enable ? 1 : 0);
  }

  // ---- Frame handling ----

  // Check if frame received event fired.
  inline static bool frameReceived(bool clear = true) {
    uint32_t ev = readReg(NFCT_EVENTS_RXDONE);
    if (clear) writeReg(NFCT_EVENTS_RXDONE, 0);
    return ev != 0;
  }

  // Check if frame transmitted event fired.
  inline static bool frameTransmitted(bool clear = true) {
    uint32_t ev = readReg(NFCT_EVENTS_TXFRAMESTART);
    if (clear) writeReg(NFCT_EVENTS_TXFRAMESTART, 0);
    return ev != 0;
  }

  // Error events.
  inline static bool errorDetected(bool clear = true) {
    uint32_t ev = readReg(NFCT_EVENTS_ERROR);
    if (clear) writeReg(NFCT_EVENTS_ERROR, 0);
    return ev != 0;
  }

  // Check for field presence (wake-on-field).
  inline static bool fieldDetected(bool clear = true) {
    uint32_t ev = readReg(NFCT_EVENTS_FIELDDETECTED);
    if (clear) writeReg(NFCT_EVENTS_FIELDDETECTED, 0);
    return ev != 0;
  }

  inline static bool fieldLost(bool clear = true) {
    uint32_t ev = readReg(NFCT_EVENTS_FIELDLOST);
    if (clear) writeReg(NFCT_EVENTS_FIELDLOST, 0);
    return ev != 0;
  }

  // ---- SENS_RES response ----

  // Set SENS_RES response value.
  inline static void setSensRes(uint8_t value) {
    writeReg(NFCT_SENSRES, value);
  }

  // Set SEL_RES response value.
  inline static void setSelRes(uint8_t value) {
    writeReg(NFCT_SELRES, value);
  }

  // ---- Modulation and data ----

  // Set TX data pointer and length.
  inline static void setTxBuffer(const uint8_t* ptr, uint16_t len) {
    writeReg(NFCT_TXDATA_PTR, reinterpret_cast<uint32_t>(const_cast<uint8_t*>(ptr)));
    writeReg(NFCT_TXDATA_MAXLEN, len);
  }

  // Set RX data pointer and max length.
  inline static void setRxBuffer(uint8_t* ptr, uint16_t maxLen) {
    writeReg(NFCT_RXDATA_PTR, reinterpret_cast<uint32_t>(ptr));
    writeReg(NFCT_RXDATA_MAXLEN, maxLen);
  }

  // Get number of bytes actually received.
  inline static uint16_t rxByteCount() {
    return static_cast<uint16_t>(readReg(NFCT_RXDATA_BYTECNTR));
  }

  // Get number of bytes actually transmitted.
  inline static uint16_t txByteCount() {
    return static_cast<uint16_t>(readReg(NFCT_TXDATA_BYTECNTR));
  }

  // ---- Enable/disable ----

  inline static void enable(bool en = true) {
    writeReg(NFCT_ENABLE, en ? 1 : 0);
  }

  // ---- Interrupts ----

  inline static void enableErrorInterrupt(bool en = true) {
    if (en) writeReg(NFCT_INTENSET, (1UL << 0));
    else    writeReg(NFCT_INTENCLR, (1UL << 0));
  }

  // ---- Low-level ----

  inline static uint32_t readReg(uint32_t offset) {
    return *reinterpret_cast<const volatile uint32_t*>(BASE + offset);
  }

  inline static void writeReg(uint32_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(BASE + offset) = value;
  }

 private:
  static constexpr uint32_t BASE = 0x400ED000UL;  // NFCT base (PERI domain, non-secure alias)

  // Tasks.
  static constexpr uint32_t NFCT_TASKS_START = 0x000;
  static constexpr uint32_t NFCT_TASKS_STOP  = 0x004;

  // Events.
  static constexpr uint32_t NFCT_EVENTS_STARTED        = 0x100;
  static constexpr uint32_t NFCT_EVENTS_STOPPED        = 0x104;
  static constexpr uint32_t NFCT_EVENTS_RXDONE         = 0x10C;
  static constexpr uint32_t NFCT_EVENTS_TXFRAMESTART   = 0x118;
  static constexpr uint32_t NFCT_EVENTS_ERROR          = 0x128;
  static constexpr uint32_t NFCT_EVENTS_FIELDDETECTED  = 0x130;
  static constexpr uint32_t NFCT_EVENTS_FIELDLOST      = 0x134;

  // Config.
  static constexpr uint32_t NFCT_ENABLE       = 0x500;
  static constexpr uint32_t NFCT_IOCONFIG     = 0x554;
  static constexpr uint32_t NFCT_AUTACKRESPONSE = 0x558;
  static constexpr uint32_t NFCT_SENSRES      = 0x508;
  static constexpr uint32_t NFCT_SELRES       = 0x50C;

  // Tag header.
  static constexpr uint32_t NFCT_TAGHEADER0  = 0x528;
  static constexpr uint32_t NFCT_TAGHEADER1  = 0x52C;
  static constexpr uint32_t NFCT_TAGHEADER2  = 0x530;
  static constexpr uint32_t NFCT_TAGHEADER3  = 0x534;

  // DMA.
  static constexpr uint32_t NFCT_TXDATA_PTR     = 0x600;
  static constexpr uint32_t NFCT_TXDATA_MAXLEN  = 0x604;
  static constexpr uint32_t NFCT_TXDATA_BYTECNTR = 0x608;
  static constexpr uint32_t NFCT_RXDATA_PTR     = 0x700;
  static constexpr uint32_t NFCT_RXDATA_MAXLEN  = 0x704;
  static constexpr uint32_t NFCT_RXDATA_BYTECNTR = 0x708;

  // Interrupt.
  static constexpr uint32_t NFCT_INTENSET = 0x300;
  static constexpr uint32_t NFCT_INTENCLR = 0x304;
};

}  // namespace xiao_nrf54l15
