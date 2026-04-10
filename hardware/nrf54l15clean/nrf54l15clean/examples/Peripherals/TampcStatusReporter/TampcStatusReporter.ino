#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

Tampc g_tampc;
unsigned long g_lastPrintMs = 0;
bool g_cracenMonitor = false;
bool g_activeShieldMonitor = false;

void printStatus() {
  Serial.print("status=0x");
  Serial.print(g_tampc.status(), HEX);
  Serial.print(" tamper=");
  Serial.print(g_tampc.tamperDetected() ? 1 : 0);
  Serial.print(" write_error=");
  Serial.print(g_tampc.writeErrorDetected() ? 1 : 0);
  Serial.print(" pend_tamper=");
  Serial.print(g_tampc.pendingTamperInterrupt() ? 1 : 0);
  Serial.print(" pend_write=");
  Serial.println(g_tampc.pendingWriteErrorInterrupt() ? 1 : 0);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("TAMPC status reporter");
  Serial.println("Commands: p print, c toggle CRACEN monitor, a toggle active shield monitor");
  printStatus();
}

void loop() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == 'p' || c == 'P') {
      printStatus();
    } else if (c == 'c' || c == 'C') {
      g_cracenMonitor = !g_cracenMonitor;
      Serial.print("setCracenTamperMonitor=");
      Serial.println(g_tampc.setCracenTamperMonitor(g_cracenMonitor) ? 1 : 0);
      printStatus();
    } else if (c == 'a' || c == 'A') {
      g_activeShieldMonitor = !g_activeShieldMonitor;
      Serial.print("setActiveShieldMonitor=");
      Serial.println(g_tampc.setActiveShieldMonitor(g_activeShieldMonitor) ? 1 : 0);
      printStatus();
    }
  }

  if (g_tampc.pollTamper()) {
    Serial.println("tamper event");
  }
  if (g_tampc.pollWriteError()) {
    Serial.println("write error event");
  }

  if (millis() - g_lastPrintMs >= 5000UL) {
    g_lastPrintMs = millis();
    printStatus();
  }
}
