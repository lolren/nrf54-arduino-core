#include <SPI.h>
#include <Wire.h>

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  Wire.begin();
  Wire.setClock(400000);

  SPI.begin();
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  (void)SPI.transfer(0x9F);
  SPI.endTransaction();

  int adc = analogRead(A0);
  digitalWrite(LED_BUILTIN, (adc > 512) ? HIGH : LOW);
}

void loop() {
  delay(250);
}
