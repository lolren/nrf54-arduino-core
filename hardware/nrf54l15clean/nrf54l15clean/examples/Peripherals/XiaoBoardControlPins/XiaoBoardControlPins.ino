/*
  XiaoBoardControlPins

  Demonstrates Arduino-level access to XIAO board-control nets:
  - IMU_MIC / IMU_MIC_EN (P0.01)
  - RF_SW (P2.03)
  - RF_SW_CTL (P2.05)
  - VBAT_EN (P1.15)
  - VBAT_READ (A7 / P1.14)

  Notes:
  - RF_SW powers the RF switch path control.
  - RF_SW_CTL selects path: LOW=ceramic, HIGH=external.
  - Drive RF_SW LOW when you want the switch current to drop to idle.
  - VBAT_EN must be HIGH while reading VBAT_READ.
*/

void setup() {
  Serial.begin(115200);
  delay(1200);

  pinMode(IMU_MIC_EN, OUTPUT);
  pinMode(RF_SW, OUTPUT);
  pinMode(RF_SW_CTL, OUTPUT);
  pinMode(VBAT_EN, OUTPUT);

  // Safe defaults.
  digitalWrite(IMU_MIC_EN, LOW);
  digitalWrite(RF_SW, HIGH);
  digitalWrite(RF_SW_CTL, LOW);
  digitalWrite(VBAT_EN, LOW);

  Serial.println("XIAO board-control pin demo");
}

void loop() {
  static bool state = false;
  state = !state;

  digitalWrite(IMU_MIC_EN, state ? HIGH : LOW);
  digitalWrite(RF_SW, HIGH);
  digitalWrite(RF_SW_CTL, state ? HIGH : LOW);

  digitalWrite(VBAT_EN, HIGH);
  delay(5);
  int raw = analogRead(VBAT_READ);
  digitalWrite(VBAT_EN, LOW);

  Serial.print("IMU_MIC=");
  Serial.print(state ? "HIGH" : "LOW");
  Serial.print(" RF_SW_CTL=");
  Serial.print(state ? "HIGH(ext)" : "LOW(cer)");
  Serial.print(" VBAT_RAW=");
  Serial.println(raw);

  delay(1000);
}
