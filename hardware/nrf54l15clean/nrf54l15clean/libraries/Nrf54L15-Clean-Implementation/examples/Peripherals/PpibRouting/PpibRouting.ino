/*
 * Ppib Routing — PPI Broker Instance Usage
 *
 * The nRF54L15 has multiple PPI broker instances for routing
 * peripheral events/tasks across domains:
 *   - PPIB11 (HF domain, non-secure alias 0x40084000)
 *   - PPIB21 (PERI domain, non-secure alias 0x400C4000)
 *   - PPIB22 (PERI domain, non-secure alias 0x400C5000)
 *   - PPIB30 (LP domain,   non-secure alias 0x40103000)
 *
 * Each broker has 64 channels. Channels can be enabled/disabled
 * individually via CHEN/CHENSET/CHENCLR registers.
 *
 * This example demonstrates creating a simple PPI channel on
 * PPIB21 that routes GPIOTE event to TIMER count.
 *
 * Hardware: XIAO nRF54L15
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

// PPIB21 base (PERI domain, non-secure)
static const uint32_t PPIB21_BASE = nrf54l15::PPIB21_BASE;

// PPIB register offsets
static const uint32_t PPIB_CHEN    = 0x500;
static const uint32_t PPIB_CHENSET = 0x504;
static const uint32_t PPIB_CHENCLR = 0x508;
static const uint32_t PPIB_CH      = 0x510;  // First channel, 4 bytes each

inline uint32_t ppib21Read(uint32_t off) {
  return *reinterpret_cast<const volatile uint32_t*>(PPIB21_BASE + off);
}

inline void ppib21Write(uint32_t off, uint32_t val) {
  *reinterpret_cast<volatile uint32_t*>(PPIB21_BASE + off) = val;
}

// Helper: configure a PPI channel
static bool ppib21SetChannel(uint8_t channel, uint32_t eventAddr, uint32_t taskAddr) {
  if (channel > 63) return false;
  ppib21Write(PPIB_CH + channel * 8 + 0, eventAddr);  // EPTR
  ppib21Write(PPIB_CH + channel * 8 + 4, taskAddr);   // TPTR
  ppib21Write(PPIB_CHENSET, 1U << channel);  // Enable channel
  return true;
}

static void ppib21DisableChannel(uint8_t channel) {
  if (channel > 63) return;
  ppib21Write(PPIB_CHENCLR, 1U << channel);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  PPIB21 — PPI Broker Routing Demo"));
  Serial.println(F("======================================"));
  Serial.println();

  // Verify PPIB21 base address
  Serial.print(F("  PPIB21 base: 0x"));
  Serial.println(PPIB21_BASE, HEX);
  Serial.println();

  // ---- List all PPI broker instances ----
  Serial.println(F("--- PPI Broker Instances ---"));
  Serial.print(F("  PPIB11 (HF):     0x"));
  Serial.println(nrf54l15::PPIB11_BASE, HEX);
  Serial.print(F("  PPIB21 (PERI):   0x"));
  Serial.println(nrf54l15::PPIB21_BASE, HEX);
  Serial.print(F("  PPIB22 (PERI):   0x"));
  Serial.println(nrf54l15::PPIB22_BASE, HEX);
  Serial.print(F("  PPIB30 (LP):     0x"));
  Serial.println(nrf54l15::PPIB30_BASE, HEX);
  Serial.println();

  // ---- Show current channel state ----
  Serial.println(F("--- PPIB21 Channel State ---"));
  uint32_t channels = ppib21Read(PPIB_CHEN);
  uint8_t activeCh = 0;
  for (int i = 0; i < 64; i++) {
    if (channels & (1U << i)) activeCh++;
  }
  Serial.print(F("  Active channels: "));
  Serial.println(activeCh);
  Serial.println();

  // ---- Create a test PPI channel (GPIOTE -> TIMER) ----
  Serial.println(F("--- Creating PPI Channel ---"));

  // We'll create a channel from TIMER10 compare event to TIMER20 clear task
  // as a demonstration. This is a safe cross-instance routing test.
  uint32_t timer10Base = nrf54l15::TIMER10_BASE;
  uint32_t timer20Base = nrf54l15::TIMER20_BASE;

  // TIMER10 EVENTS_COMPARE[0] -> TIMER20 TASKS_CLEAR
  uint32_t eventAddr = timer10Base + 0x140;  // EVENTS_COMPARE[0]
  uint32_t taskAddr  = timer20Base + 0x00C;  // TASKS_CLEAR

  bool ok = ppib21SetChannel(0, eventAddr, taskAddr);
  if (ok) {
    Serial.println(F("  Channel 0: TIMER10 CC0 -> TIMER20 CLEAR"));
    Serial.print(F("    EPTR: 0x"));
    Serial.println(ppib21Read(PPIB_CH + 0), HEX);
    Serial.print(F("    TPTR: 0x"));
    Serial.println(ppib21Read(PPIB_CH + 4), HEX);
  } else {
    Serial.println(F("  Failed to set channel 0"));
  }
  Serial.println();

  // ---- Verify channel is enabled ----
  channels = ppib21Read(PPIB_CHEN);
  Serial.print(F("  CHEN after enable: 0x"));
  Serial.println(channels, HEX);

  bool ch0Active = (channels & 0x1) != 0;
  Serial.print(F("  Channel 0 active: "));
  Serial.println(ch0Active ? F("Yes") : F("No"));
  Serial.println();

  // ---- Test the PPI routing ----
  Serial.println(F("--- Testing PPI Routing ---"));

  // Set up TIMER20 with a known counter value
  *reinterpret_cast<volatile uint32_t*>(timer20Base + 0x504) = 0; // MODE = TIMER
  *reinterpret_cast<volatile uint32_t*>(timer20Base + 0x508) = 3; // BITMODE = 32
  *reinterpret_cast<volatile uint32_t*>(timer20Base + 0x510) = 4; // PRESCALER = 4
  *reinterpret_cast<volatile uint32_t*>(timer20Base + 0x00C) = 1; // TASKS_CLEAR
  *reinterpret_cast<volatile uint32_t*>(timer20Base + 0x000) = 1; // TASKS_START

  // Read TIMER20 counter value (should be 0)
  uint32_t before = *reinterpret_cast<const volatile uint32_t*>(timer20Base + 0x54C);
  Serial.print(F("  TIMER20 counter before: "));
  Serial.println(before);

  // Wait a bit, then trigger TIMER10 compare event manually
  *reinterpret_cast<volatile uint32_t*>(timer10Base + 0x504) = 0; // MODE = TIMER
  *reinterpret_cast<volatile uint32_t*>(timer10Base + 0x508) = 3; // BITMODE = 32
  *reinterpret_cast<volatile uint32_t*>(timer10Base + 0x510) = 4; // PRESCALER = 4
  *reinterpret_cast<volatile uint32_t*>(timer10Base + 0x540) = 1000; // CC[0] = 1000
  *reinterpret_cast<volatile uint32_t*>(timer10Base + 0x00C) = 1; // TASKS_CLEAR
  *reinterpret_cast<volatile uint32_t*>(timer10Base + 0x000) = 1; // TASKS_START

  // Wait for compare event to fire (which should trigger TIMER20 CLEAR via PPIB21)
  delay(20);

  // Stop both timers
  *reinterpret_cast<volatile uint32_t*>(timer10Base + 0x004) = 1; // TASKS_STOP
  *reinterpret_cast<volatile uint32_t*>(timer20Base + 0x004) = 1; // TASKS_STOP

  // Read TIMER20 counter value (should have been cleared by PPI routing)
  uint32_t after = *reinterpret_cast<const volatile uint32_t*>(timer20Base + 0x54C);
  Serial.print(F("  TIMER20 counter after PPI: "));
  Serial.println(after);
  Serial.println();

  // ---- Cleanup ----
  ppib21DisableChannel(0);
  channels = ppib21Read(PPIB_CHEN);
  Serial.print(F("  CHEN after disable: 0x"));
  Serial.println(channels, HEX);
  Serial.println();

  Serial.println(F("======================================"));
  Serial.println(F("  PPIB21 demo complete."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
