/*
 * nPM1300 watchdog example (XIAO nRF54LM20A)
 *
 * Starts the PMIC watchdog, kicks it for a while, then stops kicking so that
 * it expires and power-cycles the board. On the next boot the nPM1300 reset
 * cause shows that the reset came from the watchdog.
 *
 * The watchdog shares the PMIC's single timer with the hibernate wake-up
 * timer. The enter-ship and hibernate functions stop it for you, but if your
 * firmware sleeps in any other way where it cannot kick (for example an MCU
 * System OFF), call npm1300_watchdog_stop() first.
 */
#include <Arduino.h>
#include "npm1300.h"

constexpr uint32_t WATCHDOG_MS      = 10000;   // timeout, in 16 ms steps
constexpr uint32_t KICK_INTERVAL_MS = 2500;    // kick well inside the timeout
constexpr uint32_t HEALTHY_FOR_MS   = 20000;   // then stop kicking on purpose

static uint32_t bootMs   = 0;
static uint32_t lastKick = 0;

static void printResetCause() {
  uint8_t c = 0xFF;
  if (!npm1300_reset_cause(&c)) {
    Serial.println("reset cause: read failed");
    return;
  }
  Serial.print("nPM1300 reset cause: 0x");
  Serial.println(c, HEX);
  if (c & NPM1300_RSTCAUSE_WATCHDOG)   Serial.println("  watchdog reset");
  if (c & NPM1300_RSTCAUSE_LONG_PRESS) Serial.println("  long-press reset");
  if (c & NPM1300_RSTCAUSE_SHIP_EXIT)  Serial.println("  woke from ship mode");
  npm1300_clear_reset_cause();   // also clears the charger error log
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  printResetCause();

  if (!npm1300_watchdog_start(WATCHDOG_MS)) {
    Serial.println("watchdog start failed");
    while (true) delay(1000);
  }
  Serial.print("watchdog running, timeout ");
  Serial.print(WATCHDOG_MS);
  Serial.println(" ms");

  bootMs = lastKick = millis();
}

void loop() {
  const uint32_t now = millis();

  if (now - bootMs < HEALTHY_FOR_MS) {
    // Only kick while the firmware is actually healthy
    if (now - lastKick >= KICK_INTERVAL_MS) {
      npm1300_watchdog_kick();
      lastKick = now;
      Serial.println("kick");
    }
  } else {
    static bool said = false;
    if (!said) {
      Serial.println("no longer kicking: the board should reset in about 10 s");
      said = true;
    }
  }
  delay(10);
}
