// Pulse the XIAO user LED, then enter timed SYSTEM OFF.
//
// This intentionally uses the same raw LED path as the proven Zephyr-parity
// blink example: LED = P2.0 and it is active-low on XIAO nRF54L15.
//
// This uses the already-proven no-retention path because it is the cleanest
// Zephyr-parity system-off demo on this board.
//
// Use delaySystemOff(ms) instead if you explicitly need .noinit retention.
//
// Custom options:
// - kBlinkOnUs: visible LED pulse width
// - kSleepMs: timed SYSTEM OFF interval before cold-boot wake

#include <variant.h>

static constexpr uint8_t kLedPin = 0U;  // XIAO LED = P2.0, active low.
static constexpr unsigned long kBlinkOnUs = 80000UL;
static constexpr unsigned long kSleepMs = 920UL;

static void ledInit() {
  NRF_P2->DIRSET = (1UL << kLedPin);
  NRF_P2->OUTSET = (1UL << kLedPin);
}

static void ledOn() { NRF_P2->OUTCLR = (1UL << kLedPin); }

static void ledOff() { NRF_P2->OUTSET = (1UL << kLedPin); }

void setup() {
  // Shut down board-side rails and release shared control nets before using the
  // raw LED path and entering timed SYSTEM OFF.
  xiaoNrf54l15EnterLowestPowerBoardState();
  ledInit();
}

void loop() {
  ledOn();
  delayMicroseconds(kBlinkOnUs);
  ledOff();

  // Cold boot after wake. This is lower current than delay(), but it is not a
  // normal Arduino sleep/resume semantic.
  delaySystemOffNoRetention(kSleepMs);
}
