/*
  VbatReadViaAnalogRead

  Reads VBAT via analogRead(VBAT_READ).
  On this board the VBAT divider is gated, so VBAT_EN must be HIGH during reads.
*/

void setup() {
  Serial.begin(115200);
  delay(1200);

  pinMode(VBAT_EN, OUTPUT);
  digitalWrite(VBAT_EN, LOW);

  Serial.println("VBAT analog read demo");
}

void loop() {
  digitalWrite(VBAT_EN, HIGH);
  delay(5);

  int raw = analogRead(VBAT_READ);
  float dividerNodeVolts = (3.6f * raw) / 1023.0f;
  float batteryVolts = dividerNodeVolts * 2.0f;

  digitalWrite(VBAT_EN, LOW);

  Serial.print("raw=");
  Serial.print(raw);
  Serial.print(" vbat~");
  Serial.print(batteryVolts, 3);
  Serial.println("V");

  delay(1000);
}
