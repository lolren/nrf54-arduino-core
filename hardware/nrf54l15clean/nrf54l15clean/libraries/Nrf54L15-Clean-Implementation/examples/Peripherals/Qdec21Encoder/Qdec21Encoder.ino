/*
 * QDEC21 Encoder — Second Quadrature Decoder Instance
 *
 * The nRF54L15 has two QDEC interfaces (QDEC20 and QDEC21).
 * QDEC20 is used by the Arduino RotaryEncoder library.
 * QDEC21 can be used for a second rotary encoder.
 *
 * This example demonstrates configuring and reading QDEC21.
 *
 * Hardware: XIAO nRF54L15
 * Pins:     A -> P0.10, B -> P0.11 (example, adjust for your board)
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static const uint8_t QDEC21_A_PIN = 10;
static const uint8_t QDEC21_B_PIN = 11;

// QDEC21 base from regs.h
static const uint32_t QDEC21_BASE = nrf54l15::QDEC21_BASE;

// QDEC register offsets (same layout across instances)
static const uint32_t QDEC_TASKS_START   = 0x000;
static const uint32_t QDEC_TASKS_STOP    = 0x004;
static const uint32_t QDEC_TASKS_REPORTRDy = 0x008;
static const uint32_t QDEC_EVENTS_READY  = 0x100;
static const uint32_t QDEC_EVENTS_REPORTED = 0x104;
static const uint32_t QDEC_ENABLE         = 0x500;
static const uint32_t QDEC_PSEL_A         = 0x544;
static const uint32_t QDEC_PSEL_B         = 0x548;
static const uint32_t QDEC_PSEL_LED         = 0x550;
static const uint32_t QDEC_SAMPLEPER      = 0x528;
static const uint32_t QDEC_DBFCTR         = 0x52C;
static const uint32_t QDEC_LEDPRE         = 0x530;
static const uint32_t QDEC_LEDPOL         = 0x534;
static const uint32_t QDEC_REPORTPER      = 0x540;
static const uint32_t QDEC_ACCURA         = 0x554;
static const uint32_t QDEC_ACCB           = 0x558;
static const uint32_t QDEC_ACCCD          = 0x55C;

inline uint32_t qdec21Read(uint32_t off) {
  return *reinterpret_cast<const volatile uint32_t*>(QDEC21_BASE + off);
}

inline void qdec21Write(uint32_t off, uint32_t val) {
  *reinterpret_cast<volatile uint32_t*>(QDEC21_BASE + off) = val;
}

inline uint32_t makePsel(uint8_t port, uint8_t pin) {
  return pin | (static_cast<uint32_t>(port) << 5);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  QDEC21 — Second Quadrature Decoder"));
  Serial.println(F("======================================"));
  Serial.println();

  // Verify QDEC21 base address
  Serial.print(F("  QDEC21 base: 0x"));
  Serial.println(QDEC21_BASE, HEX);
  Serial.println();

  // ---- Disconnect pins (safety) ----
  qdec21Write(QDEC_PSEL_A, 0xFFFFFFFFUL);
  qdec21Write(QDEC_PSEL_B, 0xFFFFFFFFUL);
  qdec21Write(QDEC_PSEL_LED, 0xFFFFFFFFUL);

  // ---- Configure pins ----
  qdec21Write(QDEC_PSEL_A, makePsel(0, QDEC21_A_PIN));
  qdec21Write(QDEC_PSEL_B, makePsel(0, QDEC21_B_PIN));
  // LED disconnected (0xFFFFFFFF)
  qdec21Write(QDEC_PSEL_LED, 0xFFFFFFFFUL);

  Serial.println(F("  QDEC21 pins configured:"));
  Serial.print(F("    A -> P0."));
  Serial.println(QDEC21_A_PIN);
  Serial.print(F("    B -> P0."));
  Serial.println(QDEC21_B_PIN);
  Serial.println(F("    LED -> Disconnected"));
  Serial.println();

  // ---- Configure QDEC21 ----
  qdec21Write(QDEC_SAMPLEPER, 0);  // SAMPLEPER = 1 (10 us)
  qdec21Write(QDEC_DBFCTR, 8);     // Debounce 8 us
  qdec21Write(QDEC_REPORTPER, 7);  // REPORTPER = 256
  qdec21Write(QDEC_LEDPRE, 0);     // LED off

  // Clear accumulators
  qdec21Write(QDEC_ACCURA, 0);
  qdec21Write(QDEC_ACCB, 0);
  qdec21Write(QDEC_ACCCD, 0);

  // ---- Enable ----
  qdec21Write(QDEC_ENABLE, 7); // Enable = 7 for QDEC

  Serial.println(F("  QDEC21 enabled (sample period 10 us, debounce 8 us)"));
  Serial.println();

  // ---- Start decoding ----
  qdec21Write(QDEC_TASKS_START, 1);

  // Wait for encoder to stabilize
  delay(1000);

  // Read accumulators
  int32_t accA = static_cast<int32_t>(qdec21Read(QDEC_ACCURA));
  int32_t accB = static_cast<int32_t>(qdec21Read(QDEC_ACCB));
  int32_t accCD = static_cast<int32_t>(qdec21Read(QDEC_ACCCD));

  Serial.println(F("--- QDEC21 Accumulators ---"));
  Serial.print(F("  ACCURA: "));
  Serial.println(accA);
  Serial.print(F("  ACCB: "));
  Serial.println(accB);
  Serial.print(F("  ACCCD: "));
  Serial.println(accCD);
  Serial.println();

  if (accA == 0 && accB == 0 && accCD == 0) {
    Serial.println(F("  (No encoder rotation detected)"));
    Serial.println(F("  Connect an encoder and rotate to see values change"));
  } else {
    Serial.print(F("  Total steps: "));
    Serial.println(accA + accB);
  }
  Serial.println();

  // Stop QDEC21 for cleanup
  qdec21Write(QDEC_TASKS_STOP, 1);
  qdec21Write(QDEC_ENABLE, 0);

  Serial.println(F("======================================"));
  Serial.println(F("  QDEC21 demo complete."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
