#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static PowerManager g_power;
static Grtc g_grtc;
static TempSensor g_temp;
static Watchdog g_wdt;
static Pdm g_pdm;
static BleRadio g_ble;

static uint32_t g_passCount = 0;
static uint32_t g_totalCount = 0;

static void reportResult(const char* name, bool pass, const char* detail) {
  Serial.print(pass ? "[PASS] " : "[FAIL] ");
  Serial.print(name);
  if (detail != nullptr && detail[0] != '\0') {
    Serial.print(" : ");
    Serial.print(detail);
  }
  Serial.print("\r\n");

  ++g_totalCount;
  if (pass) {
    ++g_passCount;
  }
}

static bool testPowerReset() {
  uint8_t previous = 0;
  bool ok = g_power.getRetention(0, &previous);
  if (ok) {
    ok = g_power.setRetention(0, 0xA5U);
  }
  uint8_t now = 0;
  if (ok) {
    ok = g_power.getRetention(0, &now) && (now == 0xA5U);
  }

  const uint32_t resetReason = g_power.resetReason();
  g_power.clearResetReason(resetReason);

  g_power.setLatencyMode(PowerLatencyMode::kConstantLatency);
  delayMicroseconds(50);
  const bool constLat = g_power.isConstantLatency();
  g_power.setLatencyMode(PowerLatencyMode::kLowPower);

  const bool dcdcOk = g_power.enableMainDcdc(true);
  g_power.setRetention(0, previous);

  char detail[96];
  snprintf(detail, sizeof(detail), "ret=0x%02X reset=0x%08lX constlat=%s dcdc=%s",
           now, static_cast<unsigned long>(resetReason),
           constLat ? "yes" : "no",
           dcdcOk ? "ok" : "fail");
  reportResult("POWER+RESET", ok && dcdcOk, detail);
  return ok && dcdcOk;
}

static bool testGrtc() {
  bool ok = g_grtc.begin(GrtcClockSource::kSystemLfclk);
  if (ok) {
    g_grtc.clear();
    ok = g_grtc.setWakeLeadLfclk(4U) &&
         g_grtc.setCompareOffsetUs(0U, 20000U, true);
  }

  bool fired = false;
  const uint32_t startMs = millis();
  while ((millis() - startMs) < 100U) {
    if (g_grtc.pollCompare(0U, true)) {
      fired = true;
      break;
    }
    __asm volatile("wfi");
  }

  const uint64_t nowUs = g_grtc.counter();
  g_grtc.end();

  char detail[96];
  snprintf(detail, sizeof(detail), "fired=%s counter=%llu",
           fired ? "yes" : "no",
           static_cast<unsigned long long>(nowUs));
  reportResult("GRTC", ok && fired, detail);
  return ok && fired;
}

static bool testTemp() {
  int32_t tempMilliC = 0;
  const bool ok = g_temp.sampleMilliDegreesC(&tempMilliC, 400000UL);
  const bool rangeOk = (tempMilliC > -50000) && (tempMilliC < 130000);

  char detail[64];
  snprintf(detail, sizeof(detail), "temp=%ldmC", static_cast<long>(tempMilliC));
  reportResult("TEMP", ok && rangeOk, detail);
  return ok && rangeOk;
}

static bool testWatchdogConfig() {
  const bool running = g_wdt.isRunning();
  bool ok = !running &&
            g_wdt.configure(3000U, 0U, true, false, true);
  const uint32_t reqStatus = g_wdt.requestStatus();
  const bool rr0Enabled = (reqStatus & WDT_REQSTATUS_RR0_Msk) != 0U;

  char detail[80];
  snprintf(detail, sizeof(detail), "running=%s req=0x%08lX",
           running ? "yes" : "no",
           static_cast<unsigned long>(reqStatus));
  reportResult("WDT", ok && rr0Enabled, detail);
  return ok && rr0Enabled;
}

static bool testBoardControl() {
  bool ok = BoardControl::setAntennaPath(BoardAntennaPath::kCeramic) &&
            BoardControl::setAntennaPath(BoardAntennaPath::kExternal) &&
            BoardControl::setAntennaPath(BoardAntennaPath::kControlHighImpedance) &&
            BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);

  int32_t vbatMilliVolts = -1;
  uint8_t vbatPercent = 0;
  const bool batteryOk = BoardControl::sampleBatteryMilliVolts(&vbatMilliVolts) &&
                         BoardControl::sampleBatteryPercent(&vbatPercent);
  ok = ok && batteryOk;

  char detail[96];
  snprintf(detail, sizeof(detail), "vbat=%ldmV pct=%u ant=%u",
           static_cast<long>(vbatMilliVolts),
           static_cast<unsigned>(vbatPercent),
           static_cast<unsigned>(BoardControl::antennaPath()));
  reportResult("BOARDCTRL", ok, detail);
  return ok;
}

static bool testPdm() {
  alignas(4) static int16_t pcm[64] = {};

  bool ok = g_pdm.begin(kPinMicClk, kPinMicData, true, 40U,
                        PDM_RATIO_RATIO_Ratio64, PdmEdge::kLeftRising);
  bool captured = false;
  if (ok) {
    captured = g_pdm.capture(pcm, 64U, 8000000UL);
  }
  g_pdm.end();

  long checksum = 0;
  for (size_t i = 0; i < 64U; ++i) {
    checksum += pcm[i];
  }

  char detail[96];
  snprintf(detail, sizeof(detail), "capture=%s sum=%ld first=%d",
           captured ? "ok" : "fail",
           checksum,
           static_cast<int>(pcm[0]));
  reportResult("PDM", ok && captured, detail);
  return ok && captured;
}

static bool testBleRadio() {
  bool ok = g_ble.begin(-8);
  if (ok) {
    ok = g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingName("PARITY-BLE", true) &&
         g_ble.setScanResponseName("PARITY-BLE-SCAN") &&
         g_ble.setGattDeviceName("PARITY-GATT") &&
         g_ble.setGattBatteryLevel(88U);
  }

  BleAdvInteraction interaction{};
  bool advertised = false;
  if (ok) {
    advertised = g_ble.advertiseInteractEvent(&interaction, 350U, 300000UL, 700000UL);
  }
  g_ble.end();

  char detail[112];
  snprintf(detail, sizeof(detail), "init=%s adv=%s scan_req=%s conn_ind=%s",
           ok ? "ok" : "fail",
           advertised ? "ok" : "fail",
           interaction.receivedScanRequest ? "yes" : "no",
           interaction.receivedConnectInd ? "yes" : "no");
  reportResult("BLE", ok && advertised, detail);
  return ok && advertised;
}

void setup() {
  Serial.begin(115200);
  delay(350);
  Serial.print("\r\nNrf54L15-Clean-Implementation FeatureParitySelfTest\r\n");

  testPowerReset();
  testGrtc();
  testTemp();
  testWatchdogConfig();
  testBoardControl();
  testPdm();
  testBleRadio();

  char summary[96];
  snprintf(summary, sizeof(summary), "SUMMARY: %lu/%lu PASS\r\n",
           static_cast<unsigned long>(g_passCount),
           static_cast<unsigned long>(g_totalCount));
  Serial.print(summary);
}

void loop() {
  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  if ((now - lastMs) < 2000UL) {
    return;
  }
  lastMs = now;
  Serial.print("alive\r\n");
}
