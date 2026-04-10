#include <HardwareSerial.h>
#include <Wire.h>

// Compile-oriented probe for the extra serial-fabric instances that were
// still missing from the compatibility layer. This does not drive both blocks
// at once on the same fabric instance; it simply proves they can be named and
// configured from sketches.

HardwareSerial g_uart22(NRF_UARTE22, PIN_D0, PIN_D1);
HardwareSerial g_uart30(NRF_UARTE30, PIN_D2, PIN_D3);
TwoWire g_wire22(NRF_TWIM22, PIN_WIRE_SDA, PIN_WIRE_SCL);
TwoWire g_wire30(NRF_TWIM30, PIN_WIRE1_SDA, PIN_WIRE1_SCL);

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("extra serial-fabric probe");

  g_uart22.begin(115200);
  g_uart22.end();

  g_uart30.begin(115200);
  g_uart30.end();

  g_wire22.begin();
  g_wire22.end();

  g_wire30.begin();
  g_wire30.end();

  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  delay(500);
}
