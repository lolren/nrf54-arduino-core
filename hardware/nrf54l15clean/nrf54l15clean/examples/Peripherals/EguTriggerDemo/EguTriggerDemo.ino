#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

Egu g_egu(nrf54l15::EGU20_BASE);
unsigned long g_lastTriggerMs = 0;
uint32_t g_count = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("EGU trigger demo");
  Serial.println("Commands: t trigger, c clear");
  g_egu.clearAllEvents();
}

void loop() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == 't' || c == 'T') {
      g_egu.trigger(0);
    } else if (c == 'c' || c == 'C') {
      g_egu.clearAllEvents();
      g_count = 0;
      Serial.println("cleared");
    }
  }

  if (millis() - g_lastTriggerMs >= 1000UL) {
    g_lastTriggerMs = millis();
    g_egu.trigger(0);
  }

  if (g_egu.pollTriggered(0)) {
    ++g_count;
    Serial.print("triggered count=");
    Serial.println(g_count);
    digitalWrite(LED_BUILTIN, (g_count & 1U) ? HIGH : LOW);
  }
}
