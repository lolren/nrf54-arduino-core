/*
 * PDM21 Microphone — Second PDM Instance (Digital Microphone)
 *
 * The nRF54L15 has two PDM interfaces (PDM20 and PDM21).
 * PDM20 is used by the Arduino Audio library on XIAO nRF54L15 Sense.
 * PDM21 can be used for a second microphone or as an alternate
 * microphone when PDM20 is unavailable.
 *
 * This example demonstrates configuring and reading PDM21.
 *
 * Hardware: XIAO nRF54L15
 * Pins:     PDM21 CLK on P0.04, DIN on P0.05 (example, adjust for your board)
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include <algorithm>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static const uint8_t PDM21_CLK_PIN  = 4;
static const uint8_t PDM21_DIN_PIN  = 5;
static const uint8_t SAMPLES        = 256;

// PDM21 base from regs.h
static const uint32_t PDM21_BASE = nrf54l15::PDM21_BASE;

// PDM register offsets (same layout across instances)
static const uint32_t PDM_TASKS_START   = 0x000;
static const uint32_t PDM_TASKS_STOP    = 0x004;
static const uint32_t PDM_TASKS_SAMPLE  = 0x008;
static const uint32_t PDM_EVENTS_STARTED = 0x100;
static const uint32_t PDM_EVENTS_END     = 0x104;
static const uint32_t PDM_ENABLE         = 0x500;
static const uint32_t PDM_PSEL_CLK       = 0x530;
static const uint32_t PDM_PSEL_DIN       = 0x534;
static const uint32_t PDM_MODE           = 0x508;
static const uint32_t PDM_GAINL          = 0x50C;
static const uint32_t PDM_GAINR          = 0x510;
static const uint32_t PDM_MAXSAMPLES     = 0x514;
static const uint32_t PDM_RATIO          = 0x518;
static const uint32_t PDM_RESULT_PTR     = 0x600;
static const uint32_t PDM_RESULT_MAXCNT  = 0x604;
static const uint32_t PDM_STATUS         = 0x540;

inline uint32_t pdm21Read(uint32_t off) {
  return *reinterpret_cast<const volatile uint32_t*>(PDM21_BASE + off);
}

inline void pdm21Write(uint32_t off, uint32_t val) {
  *reinterpret_cast<volatile uint32_t*>(PDM21_BASE + off) = val;
}

inline uint32_t makePsel(uint8_t port, uint8_t pin) {
  return pin | (static_cast<uint32_t>(port) << 5);
}

static int16_t sampleBuffer[SAMPLES];

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  PDM21 — Second PDM Instance"));
  Serial.println(F("======================================"));
  Serial.println();

  // Verify PDM21 base address
  Serial.print(F("  PDM21 base: 0x"));
  Serial.println(PDM21_BASE, HEX);
  Serial.println();

  // ---- Disconnect pins (safety) ----
  pdm21Write(PDM_PSEL_CLK, 0xFFFFFFFFUL);
  pdm21Write(PDM_PSEL_DIN, 0xFFFFFFFFUL);

  // ---- Configure pins ----
  // On XIAO nRF54L15 Sense, PDM CLK=D0(P0.04), DIN=D1(P0.05)
  pdm21Write(PDM_PSEL_CLK, makePsel(0, PDM21_CLK_PIN));
  pdm21Write(PDM_PSEL_DIN, makePsel(0, PDM21_DIN_PIN));

  Serial.println(F("  PDM21 pins configured:"));
  Serial.print(F("    CLK -> P0."));
  Serial.println(PDM21_CLK_PIN);
  Serial.print(F("    DIN -> P0."));
  Serial.println(PDM21_DIN_PIN);
  Serial.println();

  // ---- Configure PDM21 ----
  // Mono mode, 1024/3 ratio, gain 24 dB each channel
  pdm21Write(PDM_MODE, 0);  // Mono, right channel disabled, no edge
  pdm21Write(PDM_GAINL, 4); // 24 dB
  pdm21Write(PDM_GAINR, 4);
  pdm21Write(PDM_RATIO, 1); // 1024/3
  pdm21Write(PDM_MAXSAMPLES, SAMPLES);

  // ---- DMA result buffer ----
  pdm21Write(PDM_RESULT_PTR, reinterpret_cast<uint32_t>(sampleBuffer));
  pdm21Write(PDM_RESULT_MAXCNT, SAMPLES * 2); // Each sample is int16_t

  // ---- Enable ----
  pdm21Write(PDM_ENABLE, 4); // Enable = 4 for PDM

  Serial.println(F("  PDM21 enabled (mono, ratio 1024/3, 24 dB gain)"));
  Serial.println();

  // ---- Read some samples ----
  pdm21Write(PDM_TASKS_START, 1);

  // Wait for a brief period then stop
  delay(50);

  pdm21Write(PDM_TASKS_STOP, 1);

  // Check status
  uint32_t status = pdm21Read(PDM_STATUS);
  Serial.print(F("  PDM21 status: 0x"));
  Serial.println(status, HEX);
  Serial.println();

  // Print first 20 samples
  Serial.println(F("--- First 20 Samples ---"));
  for (int i = 0; i < 20; i++) {
    Serial.print(F("  ["));
    Serial.print(i, DEC);
    Serial.print(F("]: "));
    Serial.println(sampleBuffer[i]);
  }
  Serial.println();

  // Compute RMS
  long sumSquares = 0;
  for (int i = 0; i < SAMPLES; i++) {
    sumSquares += sampleBuffer[i];
  }
  int avg = sumSquares / SAMPLES;

  Serial.print(F("  Average sample: "));
  Serial.println(avg);
  Serial.print(F("  Max sample: "));
  Serial.println(*std::max_element(sampleBuffer, sampleBuffer + SAMPLES));
  Serial.print(F("  Min sample: "));
  Serial.println(*std::min_element(sampleBuffer, sampleBuffer + SAMPLES));
  Serial.println();

  // Disable PDM21 for cleanup
  pdm21Write(PDM_ENABLE, 0);
  pdm21Write(PDM_PSEL_CLK, 0xFFFFFFFFUL);
  pdm21Write(PDM_PSEL_DIN, 0xFFFFFFFFUL);

  Serial.println(F("======================================"));
  Serial.println(F("  PDM21 demo complete."));
  Serial.println(F("  (If no microphone is connected, samples will be near zero)"));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
