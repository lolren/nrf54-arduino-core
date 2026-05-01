#pragma once

#include <stdint.h>

#include "nrf54l15_regs.h"

namespace xiao_nrf54l15 {

// Oscillator Control (OSCILLATORS)
// Controls HFXO (32 MHz crystal), PLL, and LFCLK source selection.
// Datasheet chapter 5.4 (OSCILLATORS — Oscillator control).
class Oscillators {
 public:
  // ---- LFCLK source selection ----

  enum class LfclkSource : uint32_t {
    kLfrc   = 0,  // 32.768 kHz RC oscillator (internal)
    kLfxo   = 1,  // 32.768 kHz crystal oscillator (external)
    kLfSynt = 2,  // 32.768 kHz synthesized from HFCLK
  };

  // Set LFCLK source. Must be set BEFORE starting LFCLK.
  inline static void setLfclkSource(LfclkSource src) {
    writeReg(OSCILLATORS_LFCLKSRC, static_cast<uint32_t>(src));
  }

  // Get current LFCLK source setting.
  inline static LfclkSource getLfclkSource() {
    return static_cast<LfclkSource>(readReg(OSCILLATORS_LFCLKSRC) & 0x3);
  }

  // Get copy of LFCLK source that was active when LFCLK was started.
  inline static LfclkSource getLfclkSourceCopy() {
    return static_cast<LfclkSource>(readReg(OSCILLATORS_LFCLKSRCCOPY) & 0x3);
  }

  // ---- LFCLK control ----

  // Start LFCLK (using the currently configured source).
  inline static void startLfclk() {
    writeReg(OSCILLATORS_TASKS_LFCLKSTART, 1);
  }

  // Stop LFCLK.
  inline static void stopLfclk() {
    writeReg(OSCILLATORS_TASKS_LFCLKSTOP, 1);
  }

  // Check if LFCLK start task was triggered.
  inline static bool lfclkStartTriggered() {
    return (readReg(OSCILLATORS_LFCLKRUN) & 0x1) != 0;
  }

  // Check if LFCLK is currently running.
  inline static bool lfclkRunning() {
    return (readReg(OSCILLATORS_LFCLKSTAT) & 0x1) != 0;
  }

  // Check if LFCLK stopped event has fired.
  inline static bool lfclkStopped(bool clear = true) {
    uint32_t ev = readReg(OSCILLATORS_EVENTS_LFCLKSTOPPED);
    if (clear) writeReg(OSCILLATORS_EVENTS_LFCLKSTOPPED, 0);
    return ev != 0;
  }

  // Check if LFCLK started event has fired.
  inline static bool lfclkStarted(bool clear = true) {
    uint32_t ev = readReg(OSCILLATORS_EVENTS_LFCLKSTARTED);
    if (clear) writeReg(OSCILLATORS_EVENTS_LFCLKSTARTED, 0);
    return ev != 0;
  }

  // ---- HFXO (32 MHz crystal) control ----

  // Start HFXO (32 MHz crystal oscillator).
  inline static void startHfxo() {
    writeReg(OSCILLATORS_TASKS_XOSTART, 1);
  }

  // Stop HFXO.
  inline static void stopHfxo() {
    writeReg(OSCILLATORS_TASKS_XOSTOP, 1);
  }

  // Trigger HFXO tuning (using FICR trim values).
  inline static void tuneHfxo() {
    writeReg(OSCILLATORS_TASKS_XOTUNE, 1);
  }

  // Check if HFXO start task was triggered.
  inline static bool hfxoStartTriggered() {
    return (readReg(OSCILLATORS_XORUN) & 0x1) != 0;
  }

  // Check if HFXO is currently running.
  inline static bool hfxoRunning() {
    return (readReg(OSCILLATORS_XOSTAT) & 0x1) != 0;
  }

  // Check if HFXO tuning completed successfully.
  inline static bool hfxoTuned(bool clear = true) {
    uint32_t ev = readReg(OSCILLATORS_EVENTS_XOTUNED);
    if (clear) writeReg(OSCILLATORS_EVENTS_XOTUNED, 0);
    return ev != 0;
  }

  // Check if HFXO tuning failed.
  inline static bool hfxoTuneFailed(bool clear = true) {
    uint32_t ev = readReg(OSCILLATORS_EVENTS_XOTUNEFAILED);
    if (clear) writeReg(OSCILLATORS_EVENTS_XOTUNEFAILED, 0);
    return ev != 0;
  }

  // Check if HFXO started event has fired.
  inline static bool hfxoStarted(bool clear = true) {
    uint32_t ev = readReg(OSCILLATORS_EVENTS_XOSTARTED);
    if (clear) writeReg(OSCILLATORS_EVENTS_XOSTARTED, 0);
    return ev != 0;
  }

  // Check if HFXO stopped event has fired.
  inline static bool hfxoStopped(bool clear = true) {
    uint32_t ev = readReg(OSCILLATORS_EVENTS_XOSTOPPED);
    if (clear) writeReg(OSCILLATORS_EVENTS_XOSTOPPED, 0);
    return ev != 0;
  }

  // ---- PLL control ----

  // Start the PLL (used for HFCLK from HFXO).
  inline static void startPl1() {
    writeReg(OSCILLATORS_TASKS_PLLSTART, 1);
  }

  // Stop the PLL.
  inline static void stopPl1() {
    writeReg(OSCILLATORS_TASKS_PLLSTOP, 1);
  }

  // Check if PLL is running.
  inline static bool pllRunning() {
    return (readReg(OSCILLATORS_PLLSTAT) & 0x1) != 0;
  }

  // Check if PLL started event has fired.
  inline static bool pllStarted(bool clear = true) {
    uint32_t ev = readReg(OSCILLATORS_EVENTS_PLLSTARTED);
    if (clear) writeReg(OSCILLATORS_EVENTS_PLLSTARTED, 0);
    return ev != 0;
  }

  // ---- HFCLK source (via CLOCK peripheral, mirrored here for convenience) ----

  // HFCLK source selection (from CLOCK peripheral).
  enum class HfclkSource : uint32_t {
    kRcOsc = 0,   // Internal 64 MHz RC oscillator (default after reset)
    kHfxo  = 1,   // 32 MHz crystal oscillator + PLL (128 MHz)
    kSynt  = 2,   // Synthesized from LFCLK (64 MHz)
  };

  // Set HFCLK source (via CLOCK.HFCLKSRC).
  inline static void setHfclkSource(HfclkSource src) {
    // CLOCK peripheral (LP domain, secure).
    uint32_t clockBase = 0x4010E000UL;  // Non-secure alias
    *reinterpret_cast<volatile uint32_t*>(clockBase + 0x530) = static_cast<uint32_t>(src);
  }

  // Get HFCLK source copy (set when HFCLKSTART task triggered).
  inline static HfclkSource getHfclkSourceCopy() {
    uint32_t clockBase = 0x4010E000UL;
    return static_cast<HfclkSource>(*reinterpret_cast<const volatile uint32_t*>(clockBase + 0x534) & 0x3);
  }

  // Start HFCLK.
  inline static void startHfclk() {
    uint32_t clockBase = 0x4010E000UL;
    *reinterpret_cast<volatile uint32_t*>(clockBase + 0x000) = 1;  // TASKS_HFCLKSTART
  }

  // Stop HFCLK.
  inline static void stopHfclk() {
    uint32_t clockBase = 0x4010E000UL;
    *reinterpret_cast<volatile uint32_t*>(clockBase + 0x004) = 1;  // TASKS_HFCLKSTOP
  }

  // Check if HFCLK started event fired.
  inline static bool hfclkStarted(bool clear = true) {
    uint32_t clockBase = 0x4010E000UL;
    uint32_t ev = *reinterpret_cast<const volatile uint32_t*>(clockBase + 0x100);  // EVENTS_HFCLKSTARTED
    if (clear) *reinterpret_cast<volatile uint32_t*>(clockBase + 0x100) = 0;
    return ev != 0;
  }

  // Check if HFCLK stopped event fired.
  inline static bool hfclkStopped(bool clear = true) {
    uint32_t clockBase = 0x4010E000UL;
    uint32_t ev = *reinterpret_cast<const volatile uint32_t*>(clockBase + 0x104);  // EVENTS_HFCLKSTOPPED
    if (clear) *reinterpret_cast<volatile uint32_t*>(clockBase + 0x104) = 0;
    return ev != 0;
  }

  // Check if HFCLK is running.
  inline static bool hfclkRunning() {
    uint32_t clockBase = 0x4010E000UL;
    return (*reinterpret_cast<const volatile uint32_t*>(clockBase + 0x400) & 0x1) != 0;
  }

  // ---- Interrupts ----

  inline static void enableHfxoStartedInterrupt(bool enable = true) {
    if (enable) writeReg(OSCILLATORS_INTENSET, (1UL << 0));
    else         writeReg(OSCILLATORS_INTENCLR, (1UL << 0));
  }

  inline static void enableLfclkStartedInterrupt(bool enable = true) {
    if (enable) writeReg(OSCILLATORS_INTENSET, (1UL << 2));
    else         writeReg(OSCILLATORS_INTENCLR, (1UL << 2));
  }

  // ---- Low-level ----

  inline static uint32_t readReg(uint32_t offset) {
    return *reinterpret_cast<const volatile uint32_t*>(BASE + offset);
  }

  inline static void writeReg(uint32_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(BASE + offset) = value;
  }

 private:
  static constexpr uint32_t BASE = 0x40120000UL;  // OSCILLATORS base (PERI domain, non-secure alias)

  // Task offsets.
  static constexpr uint32_t OSCILLATORS_TASKS_XOSTART     = 0x000;
  static constexpr uint32_t OSCILLATORS_TASKS_XOSTOP      = 0x004;
  static constexpr uint32_t OSCILLATORS_TASKS_XOTUNE      = 0x008;
  static constexpr uint32_t OSCILLATORS_TASKS_PLLSTART    = 0x00C;
  static constexpr uint32_t OSCILLATORS_TASKS_PLLSTOP     = 0x010;
  static constexpr uint32_t OSCILLATORS_TASKS_LFCLKSTART  = 0x018;
  static constexpr uint32_t OSCILLATORS_TASKS_LFCLKSTOP   = 0x01C;

  // Event offsets.
  static constexpr uint32_t OSCILLATORS_EVENTS_XOSTARTED    = 0x100;
  static constexpr uint32_t OSCILLATORS_EVENTS_XOSTOPPED    = 0x104;
  static constexpr uint32_t OSCILLATORS_EVENTS_PLLSTARTED   = 0x108;
  static constexpr uint32_t OSCILLATORS_EVENTS_PLLSTOPPED   = 0x10C;
  static constexpr uint32_t OSCILLATORS_EVENTS_LFCLKSTARTED = 0x114;
  static constexpr uint32_t OSCILLATORS_EVENTS_LFCLKSTOPPED = 0x118;
  static constexpr uint32_t OSCILLATORS_EVENTS_XOTUNED      = 0x110;
  static constexpr uint32_t OSCILLATORS_EVENTS_XOTUNEFAILED = 0x120;
  static constexpr uint32_t OSCILLATORS_EVENTS_XOTUNEERROR  = 0x11C;

  // Config / status offsets.
  static constexpr uint32_t OSCILLATORS_LFCLKSRC      = 0x440;
  static constexpr uint32_t OSCILLATORS_LFCLKSRCCOPY  = 0x450;
  static constexpr uint32_t OSCILLATORS_XORUN         = 0x408;
  static constexpr uint32_t OSCILLATORS_XOSTAT        = 0x40C;
  static constexpr uint32_t OSCILLATORS_PLLRUN        = 0x428;
  static constexpr uint32_t OSCILLATORS_PLLSTAT       = 0x42C;
  static constexpr uint32_t OSCILLATORS_LFCLKRUN      = 0x448;
  static constexpr uint32_t OSCILLATORS_LFCLKSTAT     = 0x44C;
  static constexpr uint32_t OSCILLATORS_INTENSET      = 0x300;
  static constexpr uint32_t OSCILLATORS_INTENCLR      = 0x304;
};

}  // namespace xiao_nrf54l15
