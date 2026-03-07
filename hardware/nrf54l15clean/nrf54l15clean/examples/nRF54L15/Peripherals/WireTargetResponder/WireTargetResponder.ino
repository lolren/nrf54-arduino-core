/*
 * Wire target/slave example for nRF54L15 clean core.
 *
 * Device behavior:
 * - Exposes I2C target at 7-bit address 0x42.
 * - Stores the latest write payload received from a controller.
 * - Returns a compact status frame on read:
 *     [0] = incrementing read request counter
 *     [1] = length of last write payload
 *     [2..] = last write payload bytes (up to 30 bytes)
 *
 * Test from a Linux host with i2c-tools:
 *   i2ctransfer -y 1 w3@0x42 0xAA 0x55 0x11
 *   i2ctransfer -y 1 r8@0x42
 */

#include <Arduino.h>
#include <Wire.h>

static volatile uint8_t g_lastWrite[BUFFER_LENGTH];
static volatile uint8_t g_lastWriteLen = 0;
static volatile uint32_t g_writeEvents = 0;
static volatile uint32_t g_readEvents = 0;

void onWireReceive(int count) {
  g_lastWriteLen = 0;
  while (Wire.available() > 0 && g_lastWriteLen < BUFFER_LENGTH) {
    int v = Wire.read();
    if (v < 0) {
      break;
    }
    g_lastWrite[g_lastWriteLen++] = static_cast<uint8_t>(v);
  }
  ++g_writeEvents;
  (void)count;
}

void onWireRequest() {
  uint8_t out[BUFFER_LENGTH];
  uint8_t outLen = 0;

  out[outLen++] = static_cast<uint8_t>(g_readEvents & 0xFFU);
  out[outLen++] = g_lastWriteLen;

  uint8_t copyLen = g_lastWriteLen;
  if (copyLen > (BUFFER_LENGTH - outLen)) {
    copyLen = static_cast<uint8_t>(BUFFER_LENGTH - outLen);
  }
  for (uint8_t i = 0; i < copyLen; ++i) {
    out[outLen++] = g_lastWrite[i];
  }

  Wire.write(out, outLen);
  ++g_readEvents;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Wire target responder on 0x42");

  Wire.begin(0x42);
  Wire.onReceive(onWireReceive);
  Wire.onRequest(onWireRequest);
}

void loop() {
  static uint32_t lastPrint = 0;
  const uint32_t now = millis();
  if ((now - lastPrint) >= 1000U) {
    lastPrint = now;
    Serial.print("writeEvents=");
    Serial.print(static_cast<uint32_t>(g_writeEvents));
    Serial.print(" readEvents=");
    Serial.print(static_cast<uint32_t>(g_readEvents));
    Serial.print(" lastWriteLen=");
    Serial.println(static_cast<uint32_t>(g_lastWriteLen));
  }
}
