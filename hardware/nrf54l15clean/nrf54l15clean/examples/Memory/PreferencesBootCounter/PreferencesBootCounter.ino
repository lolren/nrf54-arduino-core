/*
  PreferencesBootCounter (XIAO nRF54L15 clean core)

  Demonstrates persistent key/value storage that survives reboot and power cycle.

  Serial commands:
    s -> save current A0 value in Preferences
    c -> clear this namespace
*/

#include <Preferences.h>

Preferences prefs;

static const char* kNamespace = "memory";

void printStoredState() {
  Serial.print("boot_count=");
  Serial.println(prefs.getUInt("boot_count", 0));

  Serial.print("last_a0=");
  Serial.println(prefs.getUShort("last_a0", 0));
}

void setup() {
  Serial.begin(115200);
  delay(250);

  if (!prefs.begin(kNamespace, false)) {
    Serial.println("Preferences begin failed");
    while (true) {
      delay(1000);
    }
  }

  const uint32_t boots = prefs.getUInt("boot_count", 0) + 1U;
  prefs.putUInt("boot_count", boots);

  Serial.println();
  Serial.println("Preferences demo ready");
  Serial.println("Values survive reset/reboot.");
  printStoredState();
  Serial.println("Commands: s=save A0, c=clear");
  Serial.println("Press RESET to verify boot_count persistence.");
}

void loop() {
  if (Serial.available() > 0) {
    const char cmd = static_cast<char>(Serial.read());

    if (cmd == 's' || cmd == 'S') {
      const uint16_t a0 = static_cast<uint16_t>(analogRead(A0));
      prefs.putUShort("last_a0", a0);
      Serial.print("Saved last_a0=");
      Serial.println(a0);
      printStoredState();
    } else if (cmd == 'c' || cmd == 'C') {
      prefs.clear();
      Serial.println("Namespace cleared.");
      printStoredState();
    }
  }

  delay(2);
}
