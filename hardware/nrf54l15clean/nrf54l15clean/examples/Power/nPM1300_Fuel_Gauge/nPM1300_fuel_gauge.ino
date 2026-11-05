/*
 * nPM1300 fuel gauge example (XIAO nRF54LM20A)
 *
 * Feeds battery voltage and current from the nPM1300 to Nordic's nRF Fuel
 * Gauge library once a second and prints the estimated state of charge.
 *
 * The library and a battery model are NOT part of this core. See
 * docs/npm1300_fuel_gauge.md for where to get them and how to add them to a
 * PlatformIO project.
 *
 * Needs:
 *  - nrf_fuel_gauge.h and libnrf_fuel_gauge.a (cortex-m33, soft-float,
 *    secondary cell / rechargeable variant)
 *  - battery_model.inc for your battery, next to this file
 *  - npm1300_read_ibat_signed_ma() in the nPM1300 driver
 *
 * Notes:
 *  - The library wants DISCHARGE-positive current. The driver's signed read is
 *    charge-positive, so the sign is flipped (LIB_CURRENT_SIGN).
 *  - The charger does nothing until it is enabled, and the settings below are
 *    for a 400 mAh Li-Po. Set them for your own battery.
 *  - This module has no battery NTC reading, so a fixed temperature is used.
 */
#include <Arduino.h>
#include <string.h>
#include "npm1300.h"

// The library and a battery model are not part of the core. Without them this
// sketch still builds and just prints a reminder.
#if __has_include("nrf_fuel_gauge.h") && __has_include("battery_model.inc")
#include "nrf_fuel_gauge.h"

#define OutputSerial Serial   // use Serial1 for a UART adapter

static const struct battery_model battery_model = {
#include "battery_model.inc"
};

constexpr float    BATTERY_TEMP_C    = 25.0f;   // no NTC reading: fixed estimate
constexpr float    LIB_CURRENT_SIGN  = -1.0f;   // library: discharge positive
constexpr uint32_t INTERVAL_MS       = 1000;    // 1 s while active
constexpr float    MIN_BATTERY_V     = 2.5f;    // below this, assume no battery
constexpr uint16_t CHARGE_CURRENT_MA = 100;     // set for your battery (<= 0.5C)
constexpr uint16_t CHARGE_TERM_MV    = 4200;    // Li-Po termination voltage
constexpr float    VBAT_OFFSET_V     = 0.0f;    // optional per-board calibration, see the docs
constexpr bool     PRINT_DEBUG       = false;   // also print the library's internal state

static uint32_t lastMs = 0;
static struct nrf_fuel_gauge_state_info lastInfo;

// Decodes BCHGCHARGESTATUS (charger block 0x34).
static const char *chargeName(uint8_t s) {
  if (s & 0x40) return "paused (die temp)";
  if (s & 0x02) return "complete";
  if (s & 0x10) return "CV";
  if (s & 0x08) return "CC";
  if (s & 0x04) return "trickle";
  if (s & 0x20) return "recharge needed";
  return "idle";
}

static enum nrf_fuel_gauge_charge_state libChargeState(uint8_t s) {
  if (s & 0x02) return NRF_FUEL_GAUGE_CHARGE_STATE_COMPLETE;
  if (s & 0x10) return NRF_FUEL_GAUGE_CHARGE_STATE_CV;
  if (s & 0x08) return NRF_FUEL_GAUGE_CHARGE_STATE_CC;
  if (s & 0x04) return NRF_FUEL_GAUGE_CHARGE_STATE_TRICKLE;
  return NRF_FUEL_GAUGE_CHARGE_STATE_IDLE;
}

// Tells the library about VBUS and charger state changes. Without this the
// library does not know when the battery is charging, and the state of charge
// stays stuck during charging.
static void updateLibraryChargeInfo(uint8_t chg, uint8_t vbus) {
  static int lastVbus  = -1;
  static int lastState = -1;

  const int present = (vbus & NPM1300_VBUS_STATUS_PRESENT) ? 1 : 0;
  if (present != lastVbus) {
    lastVbus = present;
    nrf_fuel_gauge_ext_state_update(present ? NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_CONNECTED
                                            : NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_DISCONNECTED,
                                    nullptr);
    if (present) {
      union nrf_fuel_gauge_ext_state_info_data d;
      memset(&d, 0, sizeof(d));
      d.charge_current_limit = CHARGE_CURRENT_MA / 1000.0f;   // amperes
      nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_CURRENT_LIMIT, &d);
    }
  }

  const int state = (int)libChargeState(chg);
  if (state != lastState) {
    lastState = state;
    union nrf_fuel_gauge_ext_state_info_data d;
    memset(&d, 0, sizeof(d));
    d.charge_state = (enum nrf_fuel_gauge_charge_state)state;
    nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_STATE_CHANGE, &d);
  }
}

// Battery voltage [V] and current [A, charge positive] from one ADC burst.
static bool readBattery(float &v, float &i) {
  const int32_t mv = npm1300_read_vbat_mv();
  int32_t ma = 0;
  if (mv < 0 || !npm1300_read_ibat_signed_ma(&ma)) return false;
  v = mv / 1000.0f - VBAT_OFFSET_V;   // subtract the measured ADC offset, if you have one
  i = ma / 1000.0f;
  return true;
}

void setup() {
  OutputSerial.begin(115200);
  delay(1000);

  if (!npm1300_is_present()) {
    OutputSerial.println("nPM1300 not found");
    while (true) delay(1000);
  }

  // The charger does nothing until the host enables it.
  npm1300_charger_set_term_voltage(CHARGE_TERM_MV);
  npm1300_charger_set_current(CHARGE_CURRENT_MA);
  npm1300_charger_enable(true);

  OutputSerial.print("fuel gauge library ");
  OutputSerial.print(nrf_fuel_gauge_version);
  OutputSerial.print(", variant ");
  OutputSerial.println(nrf_fuel_gauge_variant == NRF_FUEL_GAUGE_VARIANT_SECONDARY
                         ? "secondary (rechargeable)" : "primary");

  // Wait for a battery: initialising from a no-battery reading would start the
  // gauge from a wrong state.
  float v = 0, i = 0;
  while (true) {
    if (readBattery(v, i) && v >= MIN_BATTERY_V) break;
    OutputSerial.print("waiting for battery, V=");
    OutputSerial.println(v, 3);
    delay(1000);
  }

  // Filled in by hand: the init macro in the header uses designated
  // initializers in an order that g++ can reject.
  struct nrf_fuel_gauge_init_parameters p;
  memset(&p, 0, sizeof(p));
  p.v0 = v;
  p.i0 = LIB_CURRENT_SIGN * i;
  p.t0 = BATTERY_TEMP_C;
  p.model = &battery_model;

  const int ret = nrf_fuel_gauge_init(&p, nullptr);
  OutputSerial.print("nrf_fuel_gauge_init: ");
  OutputSerial.println(ret);
  if (ret < 0) while (true) delay(1000);

  lastMs = millis();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastMs < INTERVAL_MS) { delay(10); return; }

  float v = 0, i = 0, soc = 0;
  if (!readBattery(v, i)) {
    OutputSerial.println("battery read failed");
    lastMs = now;
    return;
  }

  const float dt = (now - lastMs) / 1000.0f;   // seconds since the previous call
  lastMs = now;

  uint8_t chg = 0, vbus = 0;
  const bool haveChg  = npm1300_charger_status(&chg);
  const bool haveVbus = npm1300_vbus_status(&vbus);
  if (haveChg && haveVbus) updateLibraryChargeInfo(chg, vbus);

  const int ret = nrf_fuel_gauge_process(v, LIB_CURRENT_SIGN * i, BATTERY_TEMP_C, dt, &soc, &lastInfo);

  OutputSerial.print("V=");   OutputSerial.print(v, 3);
  OutputSerial.print(" I=");  OutputSerial.print(i * 1000.0f, 1);   // mA, charge positive
  OutputSerial.print(" mA SoC=");
  if (ret == 0) OutputSerial.print(soc, 1); else OutputSerial.print("err");
  OutputSerial.print(" %");
  if (haveChg) {
    OutputSerial.print(" chg=");
    OutputSerial.print(chargeName(chg));
  }
  if (PRINT_DEBUG && ret == 0) {
    OutputSerial.print(" | yhat=");  OutputSerial.print(lastInfo.yhat, 3);     // predicted voltage
    OutputSerial.print(" r0=");     OutputSerial.print(lastInfo.r0, 3);        // resistance estimate
    OutputSerial.print(" raw=");    OutputSerial.print(lastInfo.soc_raw, 2);   // unrounded SoC
  }
  OutputSerial.println();
}

#else  // library or model missing

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("nRF Fuel Gauge library or battery_model.inc not found.");
  Serial.println("See docs/npm1300_fuel_gauge.md.");
}

void loop() { delay(1000); }

#endif
