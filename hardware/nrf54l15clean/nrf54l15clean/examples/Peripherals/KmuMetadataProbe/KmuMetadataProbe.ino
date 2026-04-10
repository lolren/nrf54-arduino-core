#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

Kmu g_kmu;

uint8_t g_slot = 0;
unsigned long g_lastPollMs = 0;

void printMetadata(uint8_t slot) {
  uint32_t metadata = 0;
  const bool ok = g_kmu.readMetadata(slot, &metadata);
  Serial.print("slot=");
  Serial.print(slot);
  Serial.print(" ok=");
  Serial.print(ok ? 1 : 0);
  Serial.print(" ready=");
  Serial.print(g_kmu.ready() ? 1 : 0);
  Serial.print(" metadata=0x");
  Serial.println(metadata, HEX);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("KMU metadata probe");
  Serial.println("Commands: 0-9 select slot, r read selected slot");
  printMetadata(g_slot);
}

void loop() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c >= '0' && c <= '9') {
      g_slot = static_cast<uint8_t>(c - '0');
      printMetadata(g_slot);
    } else if (c == 'r' || c == 'R') {
      printMetadata(g_slot);
    }
  }

  if (millis() - g_lastPollMs >= 5000UL) {
    g_lastPollMs = millis();
    printMetadata(g_slot);
  }
}
