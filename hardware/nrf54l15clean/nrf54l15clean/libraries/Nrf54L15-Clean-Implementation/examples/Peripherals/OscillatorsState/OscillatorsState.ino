/*
 * Oscillators State — HFCLK / LFCLK / HFXO / PLL State Query
 *
 * Reads and reports the current state of all clock sources
 * on the nRF54L15. Demonstrates the Oscillators wrapper class.
 *
 * Hardware: XIAO nRF54L15
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static const char* lfclkSourceName(Oscillators::LfclkSource src) {
  switch (src) {
    case Oscillators::LfclkSource::kLfrc:   return "LFRC  (32.768 kHz RC)";
    case Oscillators::LfclkSource::kLfxo:   return "LFXO  (32.768 kHz XTAL)";
    case Oscillators::LfclkSource::kLfSynt: return "LF_SYNTH (from HFCLK)";
    default:                                return "Unknown";
  }
}

static const char* hfclkSourceName(Oscillators::HfclkSource src) {
  switch (src) {
    case Oscillators::HfclkSource::kRcOsc: return "RC (internal 64 MHz)";
    case Oscillators::HfclkSource::kHfxo:  return "HFXO (32 MHz XTAL + PLL)";
    case Oscillators::HfclkSource::kSynt:  return "SYNTH (from LFCLK)";
    default:                               return "Unknown";
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  OSCILLATORS — Clock State Report"));
  Serial.println(F("======================================"));
  Serial.println();

  // ---- HFCLK ----
  Serial.println(F("--- HFCLK ---"));
  Serial.print(F("  Running: "));
  Serial.println(Oscillators::hfclkRunning() ? F("Yes") : F("No"));

  Oscillators::HfclkSource hfSrc = Oscillators::getHfclkSourceCopy();
  Serial.print(F("  Source: "));
  Serial.println(hfclkSourceName(hfSrc));

  bool hfStarted = Oscillators::hfclkStarted();
  Serial.print(F("  Start event fired: "));
  Serial.println(hfStarted ? F("Yes") : F("No"));
  Serial.println();

  // ---- HFXO (32 MHz crystal) ----
  Serial.println(F("--- HFXO (32 MHz XTAL) ---"));
  Serial.print(F("  Running: "));
  Serial.println(Oscillators::hfxoRunning() ? F("Yes") : F("No"));
  Serial.print(F("  Start triggered: "));
  Serial.println(Oscillators::hfxoStartTriggered() ? F("Yes") : F("No"));

  bool xosStarted = Oscillators::hfxoStarted();
  Serial.print(F("  Started event fired: "));
  Serial.println(xosStarted ? F("Yes") : F("No"));

  bool xosTuned = Oscillators::hfxoTuned();
  Serial.print(F("  Tuned event fired: "));
  Serial.println(xosTuned ? F("Yes") : F("No"));
  Serial.println();

  // ---- LFCLK ----
  Serial.println(F("--- LFCLK ---"));
  Oscillators::LfclkSource lfSrc = Oscillators::getLfclkSource();
  Serial.print(F("  Configured source: "));
  Serial.println(lfclkSourceName(lfSrc));

  Oscillators::LfclkSource lfCopy = Oscillators::getLfclkSourceCopy();
  Serial.print(F("  Active source: "));
  Serial.println(lfclkSourceName(lfCopy));

  Serial.print(F("  Running: "));
  Serial.println(Oscillators::lfclkRunning() ? F("Yes") : F("No"));
  Serial.print(F("  Start triggered: "));
  Serial.println(Oscillators::lfclkStartTriggered() ? F("Yes") : F("No"));
  Serial.println();

  // ---- PLL ----
  Serial.println(F("--- PLL ---"));
  Serial.print(F("  Running: "));
  Serial.println(Oscillators::pllRunning() ? F("Yes") : F("No"));
  bool pllStarted = Oscillators::pllStarted();
  Serial.print(F("  Started event fired: "));
  Serial.println(pllStarted ? F("Yes") : F("No"));
  Serial.println();

  // ---- Oscillator trim values from FICR ----
  Serial.println(F("--- Oscillator Trim (from FICR) ---"));
  Serial.print(F("  XOSC32M slope: "));
  Serial.println(Ficr::xosc32mSlope());
  Serial.print(F("  XOSC32M offset: "));
  Serial.println(Ficr::xosc32mOffset());
  Serial.print(F("  XOSC32K slope: "));
  Serial.println(Ficr::xosc32kSlope());
  Serial.print(F("  XOSC32K offset: "));
  Serial.println(Ficr::xosc32kOffset());
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  Oscillator state report complete."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
