/*
 * WireRepeatedStartProbe
 *
 * Demonstrates I2C repeated-start transactions using:
 *   endTransmission(false) + requestFrom(..., true)
 *
 * Default target is the optional on-board IMU (if populated):
 * - Address: 0x6A
 * - WHO_AM_I register: 0x0F
 */

#include <Wire.h>

static const uint8_t kI2cAddr = 0x6A;
static const uint8_t kWhoAmIReg = 0x0F;

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin();
  Wire.setClock(400000);

  Serial.println("WireRepeatedStartProbe start");
}

void loop() {
  Wire.beginTransmission(kI2cAddr);
  Wire.write(kWhoAmIReg);

  const uint8_t txStatus = Wire.endTransmission(false);
  if (txStatus != 0) {
    Serial.print("TX failed, code=");
    Serial.println(txStatus);
    delay(500);
    return;
  }

  const uint8_t n = Wire.requestFrom(kI2cAddr, (uint8_t)1, (uint8_t)true);
  if (n != 1 || Wire.available() < 1) {
    Serial.print("RX failed, count=");
    Serial.println(n);
    delay(500);
    return;
  }

  const int whoami = Wire.read();
  Serial.print("WHO_AM_I=0x");
  Serial.println((uint8_t)whoami, HEX);

  delay(500);
}
