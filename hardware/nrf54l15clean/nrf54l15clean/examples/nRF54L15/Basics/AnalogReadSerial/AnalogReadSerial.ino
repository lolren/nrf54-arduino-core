/*
  AnalogReadSerial (XIAO nRF54L15 clean core)

  Reads analog input A0 and prints the result to Serial.

  Note:
  Set Serial Monitor to the same baud used in Serial.begin(...).
*/

void setup() {
  Serial.begin(9600);
}

void loop() {
  int sensorValue = analogRead(A0);
  Serial.println(sensorValue);
  delay(5);
}
