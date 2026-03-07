/*
  EEPROMBootCounter (XIAO nRF54L15 clean core)

  Demonstrates EEPROM-compatible storage that survives reboot and power cycle.

  Serial commands:
    s -> save current A0 value to EEPROM
    c -> clear first 32 bytes (0xFF)
*/

#include <EEPROM.h>

static constexpr size_t kEepromSize = 64U;
static constexpr int kBootCountAddr = 0;
static constexpr int kLastA0Addr = 4;

void printStoredState() {
  uint32_t bootCount = 0U;
  uint16_t lastA0 = 0U;

  EEPROM.get(kBootCountAddr, bootCount);
  EEPROM.get(kLastA0Addr, lastA0);

  if (bootCount == 0xFFFFFFFFUL) {
    bootCount = 0U;
  }

  Serial.print("boot_count=");
  Serial.println(bootCount);
  Serial.print("last_a0=");
  Serial.println(lastA0);
}

void setup() {
  Serial.begin(115200);
  delay(250);

  if (!EEPROM.begin(kEepromSize)) {
    Serial.println("EEPROM begin failed");
    while (true) {
      delay(1000);
    }
  }

  uint32_t bootCount = 0U;
  EEPROM.get(kBootCountAddr, bootCount);
  if (bootCount == 0xFFFFFFFFUL) {
    bootCount = 0U;
  }

  ++bootCount;
  EEPROM.put(kBootCountAddr, bootCount);
  EEPROM.commit();

  Serial.println();
  Serial.println("EEPROM demo ready");
  Serial.println("Values survive reset/reboot.");
  printStoredState();
  Serial.println("Commands: s=save A0, c=clear first 32 bytes");
  Serial.println("Press RESET to verify boot_count persistence.");
}

void loop() {
  if (Serial.available() > 0) {
    const char cmd = static_cast<char>(Serial.read());

    if (cmd == 's' || cmd == 'S') {
      const uint16_t a0 = static_cast<uint16_t>(analogRead(A0));
      EEPROM.put(kLastA0Addr, a0);
      EEPROM.commit();

      Serial.print("Saved last_a0=");
      Serial.println(a0);
      printStoredState();
    } else if (cmd == 'c' || cmd == 'C') {
      for (int i = 0; i < 32; ++i) {
        EEPROM.write(i, 0xFFU);
      }
      EEPROM.commit();
      Serial.println("First 32 bytes cleared.");
      printStoredState();
    }
  }

  delay(2);
}
