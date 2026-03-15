#include <Arduino.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static PowerManager g_power;

static constexpr PowerFailThreshold kThresholds[] = {
    PowerFailThreshold::k1V7, PowerFailThreshold::k1V8,
    PowerFailThreshold::k1V9, PowerFailThreshold::k2V0,
    PowerFailThreshold::k2V1, PowerFailThreshold::k2V2,
    PowerFailThreshold::k2V3, PowerFailThreshold::k2V4,
    PowerFailThreshold::k2V5, PowerFailThreshold::k2V6,
    PowerFailThreshold::k2V7, PowerFailThreshold::k2V8,
    PowerFailThreshold::k2V9, PowerFailThreshold::k3V0,
    PowerFailThreshold::k3V1, PowerFailThreshold::k3V2,
};

static uint8_t g_thresholdIndex = 13U;  // 3.0 V
static uint32_t g_lastStatusMs = 0U;

static int32_t thresholdMilliVolts(PowerFailThreshold threshold) {
  return 1700 + (static_cast<int32_t>(threshold) * 100);
}

static bool applyThreshold() {
  return g_power.configurePowerFailComparator(kThresholds[g_thresholdIndex], true);
}

static void updateLed() {
  const bool enabled = g_power.powerFailComparatorEnabled();
  const bool below = enabled && g_power.powerBelowPowerFailThreshold();
  digitalWrite(LED_BUILTIN, below ? LOW : HIGH);
}

static void printHelp() {
  Serial.println("Commands:");
  Serial.println("  n - next POF threshold");
  Serial.println("  p - previous POF threshold");
  Serial.println("  e - enable POF comparator");
  Serial.println("  d - disable POF comparator");
  Serial.println("  c - clear POFWARN event");
  Serial.println("  b - print board battery sample too");
  Serial.println("  ? - show help");
}

static void printStatus(const char* prefix, bool includeBattery = false) {
  const bool enabled = g_power.powerFailComparatorEnabled();
  const PowerFailThreshold threshold = g_power.powerFailThreshold();
  const bool below = enabled && g_power.powerBelowPowerFailThreshold();

  Serial.print(prefix);
  Serial.print(" enabled=");
  Serial.print(enabled ? "yes" : "no");
  Serial.print(" threshold=");
  Serial.print(thresholdMilliVolts(threshold));
  Serial.print("mV");
  Serial.print(" state=");
  Serial.print(below ? "below" : "above");

  if (includeBattery) {
    int32_t vbatMilliVolts = -1;
    uint8_t vbatPercent = 0U;
    if (BoardControl::sampleBatteryMilliVolts(&vbatMilliVolts) &&
        BoardControl::sampleBatteryPercent(&vbatPercent)) {
      Serial.print(" vbat=");
      Serial.print(vbatMilliVolts);
      Serial.print("mV(");
      Serial.print(vbatPercent);
      Serial.print("%)");
    } else {
      Serial.print(" vbat=unavailable");
    }
  }

  Serial.println();
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("PofWarningMonitor");
  Serial.println("POF monitors VDD supply, not raw battery voltage.");
  Serial.println("Use this for supply-sag / brownout warning, not as an ADC replacement.");
  Serial.println("If USB keeps VDD tightly regulated, the comparator will likely stay above threshold.");

  (void)applyThreshold();
  updateLed();
  printHelp();
  printStatus("boot", true);
}

void loop() {
  if (g_power.pollPowerFailWarning(true)) {
    Serial.println("POFWARN event: supply dropped below configured threshold");
  }

  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == 'n' || c == 'N') {
      g_thresholdIndex =
          static_cast<uint8_t>((g_thresholdIndex + 1U) %
                               (sizeof(kThresholds) / sizeof(kThresholds[0])));
      (void)applyThreshold();
      printStatus("threshold");
    } else if (c == 'p' || c == 'P') {
      if (g_thresholdIndex == 0U) {
        g_thresholdIndex = static_cast<uint8_t>((sizeof(kThresholds) / sizeof(kThresholds[0])) - 1U);
      } else {
        --g_thresholdIndex;
      }
      (void)applyThreshold();
      printStatus("threshold");
    } else if (c == 'e' || c == 'E') {
      (void)applyThreshold();
      printStatus("enable");
    } else if (c == 'd' || c == 'D') {
      g_power.disablePowerFailComparator();
      printStatus("disable");
    } else if (c == 'c' || c == 'C') {
      g_power.clearPowerFailWarning();
      printStatus("clear");
    } else if (c == 'b' || c == 'B') {
      printStatus("battery", true);
    } else if (c == '?') {
      printHelp();
    }
  }

  updateLed();

  const uint32_t now = millis();
  if ((now - g_lastStatusMs) >= 3000U) {
    g_lastStatusMs = now;
    printStatus("periodic");
  }
}
