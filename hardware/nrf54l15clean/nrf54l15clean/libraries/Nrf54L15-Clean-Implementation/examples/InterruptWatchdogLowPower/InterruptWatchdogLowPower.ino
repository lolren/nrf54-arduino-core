#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static Watchdog g_wdt;
static volatile uint32_t g_buttonEdges = 0;
static volatile bool g_buttonEvent = false;

static bool g_feedEnabled = true;
static bool g_ledOn = false;
static uint32_t g_lastFeedMs = 0;
static uint32_t g_lastLogMs = 0;

static void onButtonEdge() {
  ++g_buttonEdges;
  g_buttonEvent = true;
}

static void printHelp() {
  Serial.println("Commands:");
  Serial.println("  f - resume watchdog feeding");
  Serial.println("  r - stop feeding (expect watchdog reset)");
  Serial.println("  ? - show help");
}

void setup() {
  Serial.begin(115200);
  delay(250);

  pinMode(PIN_LED_BUILTIN, OUTPUT);
  digitalWrite(PIN_LED_BUILTIN, HIGH);  // LED off (active low)

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onButtonEdge, FALLING);

  const bool wdtConfigured = g_wdt.configure(4000U, 0U, true, false, false);
  if (wdtConfigured) {
    g_wdt.start();
    g_wdt.feed();
    g_lastFeedMs = millis();
  }

  Serial.println();
  Serial.println("InterruptWatchdogLowPower");
  Serial.println(wdtConfigured ? "WDT configured at 4 s timeout"
                               : "WDT configure failed");
  Serial.println("Button interrupt toggles LED.");
  printHelp();
}

void loop() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == 'f' || c == 'F') {
      g_feedEnabled = true;
      g_wdt.feed();
      g_lastFeedMs = millis();
      Serial.println("WDT feeding resumed");
    } else if (c == 'r' || c == 'R') {
      g_feedEnabled = false;
      Serial.println("WDT feeding stopped; board should reset in ~4 s");
    } else if (c == '?') {
      printHelp();
    }
  }

  if (g_buttonEvent) {
    noInterrupts();
    g_buttonEvent = false;
    interrupts();

    g_ledOn = !g_ledOn;
    digitalWrite(PIN_LED_BUILTIN, g_ledOn ? LOW : HIGH);
  }

  const uint32_t now = millis();
  if (g_feedEnabled && (now - g_lastFeedMs) >= 1000U) {
    g_wdt.feed();
    g_lastFeedMs = now;
  }

  if ((now - g_lastLogMs) >= 1000U) {
    g_lastLogMs = now;
    Serial.print("alive t=");
    Serial.print(now);
    Serial.print("ms edges=");
    Serial.print(static_cast<unsigned long>(g_buttonEdges));
    Serial.print(" feed=");
    Serial.println(g_feedEnabled ? "on" : "off");
  }

  __asm volatile("wfi");
}
