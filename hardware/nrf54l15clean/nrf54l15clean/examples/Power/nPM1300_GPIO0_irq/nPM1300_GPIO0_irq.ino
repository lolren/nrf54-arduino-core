/*
 * nPM1300 SHPHLD button interrupt example (XIAO nRF54LM20A)
 *
 * The nPM1300 GPIO0 pin is wired to the MCU on PIN_PMIC_IRQ (P1.25). This
 * sketch sets it up as the PMIC's interrupt output, enables the SHPHLD button
 * press and release events, and prints each event with the hold time.
 *
 * The ISR only sets a flag. The I2C reads and writes happen in loop(), because
 * they cannot be done from interrupt context.
 */
#include <Arduino.h>
#include "npm1300.h"

// SHPHLD event registers (MAIN block) and pin status (SHIP block)
constexpr uint8_t EVT_SHPHLD_SET   = 0x12;   // read: pending events
constexpr uint8_t EVT_SHPHLD_CLR   = 0x13;   // write 1 to clear
constexpr uint8_t INTEN_SHPHLD_SET = 0x14;   // write 1 to enable
constexpr uint8_t SHPHLD_STATUS    = 0x05;   // bit0 = pin level, 0 = pressed
constexpr uint8_t BTN_PRESS   = 1 << 0;
constexpr uint8_t BTN_RELEASE = 1 << 1;

volatile bool npmIrq = false;
void onNpmIrq() { npmIrq = true; }

static bool     havePress = false;
static uint32_t pressMs   = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!npm1300_is_present()) {
    Serial.println("nPM1300 not found");
    while (true) delay(1000);
  }

  // PMIC GPIO0 -> interrupt output
  npm1300_gpio_set_mode(0, NPM1300_GPIO_GPOIRQ);

  // Drop stale events, then enable press and release
  npm1300_write_reg(NPM1300_BASE_MAIN, EVT_SHPHLD_CLR, 0xFF);
  npm1300_write_reg(NPM1300_BASE_MAIN, INTEN_SHPHLD_SET, BTN_PRESS | BTN_RELEASE);

  // The line goes high when an enabled event is pending
  pinMode(PIN_PMIC_GPIO0, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_PMIC_GPIO0), onNpmIrq, RISING);

  Serial.println("Press the SHPHLD button");
}

void loop() {
  if (!npmIrq) return;
  npmIrq = false;

  // Read until nothing is pending. An event that arrives while another is
  // being handled keeps the line high, so there would be no new rising edge.
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t evt = 0;
    if (!npm1300_read_reg(NPM1300_BASE_MAIN, EVT_SHPHLD_SET, &evt) || evt == 0) break;
    npm1300_write_reg(NPM1300_BASE_MAIN, EVT_SHPHLD_CLR, evt);   // clear what we saw

    if (evt & BTN_PRESS) {
      havePress = true;
      pressMs = millis();
      Serial.println("pressed");
    }
    if (evt & BTN_RELEASE) {
      Serial.print("released");
      if (havePress) {
        Serial.print(" after ");
        Serial.print(millis() - pressMs);
        Serial.print(" ms");
        havePress = false;
      }
      Serial.println();
    }
  }
}
