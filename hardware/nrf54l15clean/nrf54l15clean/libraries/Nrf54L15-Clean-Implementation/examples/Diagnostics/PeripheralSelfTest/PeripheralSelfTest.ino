#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static Spim g_spi21(nrf54l15::SPIM21_BASE, 128000000UL);
static Twim g_i2c21(nrf54l15::TWIM21_BASE);
static Uarte g_uart21(nrf54l15::UARTE21_BASE);
static Saadc g_adc(nrf54l15::SAADC_BASE);
static Timer g_timer20(nrf54l15::TIMER20_BASE, 16000000UL, 6);
static Pwm g_pwm20(nrf54l15::PWM20_BASE);
static Gpiote g_gpiote20(nrf54l15::GPIOTE20_BASE, 8);
static PowerManager g_power;
static Grtc g_grtc;
static TempSensor g_temp;
static Watchdog g_wdt;
static Pdm g_pdm;
static BleRadio g_ble;

static volatile uint32_t g_timerTicks = 0;
static volatile uint32_t g_buttonEdges = 0;

static uint32_t g_passCount = 0;
static uint32_t g_totalCount = 0;

static bool g_i2cAck = false;
static uint8_t g_i2cWhoAmI = 0;
static uint8_t g_spiRx = 0;
static int32_t g_a0mV = -1;
static int32_t g_vbatmV = -1;
static int32_t g_tempMilliC = 0;
static bool g_pdmCaptureOk = false;
static bool g_bleAdvOk = false;

static void onTimerCompare(uint8_t channel, void* context) {
  (void)channel;
  (void)context;
  ++g_timerTicks;
}

static void onButtonEdge(uint8_t channel, void* context) {
  (void)channel;
  (void)context;
  ++g_buttonEdges;
}

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

static bool testGpio() {
  bool buttonHigh = true;
  const bool ok =
      Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled) &&
      Gpio::configure(kPinUserButton, GpioDirection::kInput, GpioPull::kDisabled) &&
      Gpio::write(kPinUserLed, false) &&
      Gpio::write(kPinUserLed, true) &&
      Gpio::read(kPinUserButton, &buttonHigh);

  char detail[64];
  snprintf(detail, sizeof(detail), "button=%s",
           buttonHigh ? "released" : "pressed");
  reportResult("GPIO", ok, detail);
  return ok;
}

static bool testAdc() {
  bool ok = g_adc.begin(AdcResolution::k12bit, 400000UL);

  if (ok) {
    ok = g_adc.configureSingleEnded(0, kPinA0, AdcGain::k2over8) &&
         g_adc.sampleMilliVolts(&g_a0mV, 400000UL);
  }

  if (ok) {
    ok = Gpio::configure(kPinVbatEnable, GpioDirection::kOutput, GpioPull::kDisabled) &&
         Gpio::write(kPinVbatEnable, true);
    if (ok) {
      delayMicroseconds(200);
      int32_t vbatHalf = -1;
      ok = g_adc.configureSingleEnded(0, kPinVbatSense, AdcGain::k2over8) &&
           g_adc.sampleMilliVolts(&vbatHalf, 400000UL);
      if (ok) {
        g_vbatmV = vbatHalf * 2;
      }
      Gpio::write(kPinVbatEnable, false);
    }
  }

  char detail[80];
  snprintf(detail, sizeof(detail), "A0=%ldmV VBAT=%ldmV",
           static_cast<long>(g_a0mV),
           static_cast<long>(g_vbatmV));
  reportResult("ADC", ok, detail);
  return ok;
}

static bool testSpi() {
  const uint8_t tx = 0x9FU;
  uint8_t rx = 0U;

  bool ok = g_spi21.begin(kDefaultSpiSck, kDefaultSpiMosi, kDefaultSpiMiso,
                          kPinDisconnected, 8000000UL, SpiMode::kMode0, false);
  if (ok) {
    ok = g_spi21.setFrequency(8000000UL);
  }
  if (ok) {
    ok = g_spi21.transfer(&tx, &rx, 1, 300000UL);
  }
  g_spi21.end();

  g_spiRx = rx;

  char detail[64];
  snprintf(detail, sizeof(detail), "rx=0x%02X", g_spiRx);
  reportResult("SPI", ok, detail);
  return ok;
}

static bool testI2c() {
  uint8_t reg = 0x0FU;
  uint8_t whoami = 0U;

  const bool beginOk =
      g_i2c21.begin(kDefaultI2cScl, kDefaultI2cSda, TwimFrequency::k400k);
  bool ack = false;
  if (beginOk) {
    (void)g_i2c21.setFrequency(TwimFrequency::k400k);
    ack = g_i2c21.writeRead(0x6A, &reg, 1, &whoami, 1, 300000UL);
  }
  g_i2c21.end();

  g_i2cAck = ack;
  g_i2cWhoAmI = whoami;

  // IMU is not assembled on all board variants; bus bring-up is begin() success.
  const bool pass = beginOk;

  char detail[80];
  snprintf(detail, sizeof(detail), "imu=%s whoami=0x%02X",
           g_i2cAck ? "ACK" : "NACK", g_i2cWhoAmI);
  reportResult("I2C", pass, detail);
  return pass;
}

static bool testUart() {
  static const uint8_t kMsg[] = "UART21 self-test TX\r\n";

  bool ok =
      g_uart21.begin(kDefaultUartTx, kDefaultUartRx, UarteBaud::k115200, false);
  if (ok) {
    ok = g_uart21.write(kMsg, sizeof(kMsg) - 1U, 300000UL);
  }
  g_uart21.end();

  reportResult("UART", ok, "tx on D6 (P2.08)");
  return ok;
}

static bool testTimer() {
  g_timerTicks = 0;
  bool ok = g_timer20.begin(TimerBitWidth::k32bit, 4, false);
  if (ok) {
    const uint32_t periodTicks = g_timer20.ticksFromMicros(100000UL);
    ok = g_timer20.attachCompareCallback(0, onTimerCompare, nullptr) &&
         g_timer20.setCompare(0, periodTicks, true, false, false, false);
  }

  if (ok) {
    g_timer20.start();
    const uint32_t startMs = millis();
    while ((millis() - startMs) < 350UL) {
      g_timer20.service();
      delay(1);
    }
    g_timer20.stop();
    ok = (g_timerTicks >= 2U);
  }

  char detail[64];
  snprintf(detail, sizeof(detail), "ticks=%lu",
           static_cast<unsigned long>(g_timerTicks));
  reportResult("TIMER", ok, detail);
  return ok;
}

static bool testPwm() {
  bool ok = g_pwm20.beginSingle(kPinUserLed, 1000UL, 200U, false);
  if (ok) {
    ok = g_pwm20.start(0, 300000UL);
  }
  if (ok) {
    delay(120);
    ok = g_pwm20.setDutyPermille(800U);
    delay(120);
  }
  if (ok) {
    ok = g_pwm20.stop(300000UL);
  }
  g_pwm20.end();

  reportResult("PWM", ok, "LED duty sweep");
  return ok;
}

static bool testGpiote() {
  g_buttonEdges = 0;

  bool ok = g_gpiote20.configureEvent(0, kPinUserButton, GpiotePolarity::kToggle, false) &&
            g_gpiote20.attachInCallback(0, onButtonEdge, nullptr) &&
            g_gpiote20.configureTask(1, kPinUserLed, GpiotePolarity::kToggle, true) &&
            g_gpiote20.triggerTaskOut(1) &&
            g_gpiote20.triggerTaskOut(1);

  const uint32_t startMs = millis();
  while ((millis() - startMs) < 50UL) {
    g_gpiote20.service();
  }

  char detail[64];
  snprintf(detail, sizeof(detail), "edges=%lu",
           static_cast<unsigned long>(g_buttonEdges));
  reportResult("GPIOTE", ok, detail);
  return ok;
}

static bool testPowerReset() {
  uint8_t previous = 0;
  bool ok = g_power.getRetention(0, &previous) &&
            g_power.setRetention(0, 0xA5U);

  uint8_t current = 0;
  if (ok) {
    ok = g_power.getRetention(0, &current) && (current == 0xA5U);
  }

  const uint32_t resetReason = g_power.resetReason();
  g_power.clearResetReason(resetReason);

  g_power.setLatencyMode(PowerLatencyMode::kConstantLatency);
  delayMicroseconds(50);
  const bool constLat = g_power.isConstantLatency();
  g_power.setLatencyMode(PowerLatencyMode::kLowPower);
  const bool dcdcOk = g_power.enableMainDcdc(true);
  g_power.setRetention(0, previous);

  char detail[112];
  snprintf(detail, sizeof(detail), "ret=0x%02X reset=0x%08lX constlat=%s dcdc=%s",
           current, static_cast<unsigned long>(resetReason),
           constLat ? "yes" : "no",
           dcdcOk ? "ok" : "fail");
  reportResult("POWER+RESET", ok && dcdcOk, detail);
  return ok && dcdcOk;
}

static bool testGrtc() {
  bool ok = g_grtc.begin(GrtcClockSource::kSystemLfclk) &&
            g_grtc.setWakeLeadLfclk(4U) &&
            g_grtc.setCompareOffsetUs(0U, 20000U, true);

  bool fired = false;
  const uint32_t startMs = millis();
  while ((millis() - startMs) < 100U) {
    if (g_grtc.pollCompare(0U, true)) {
      fired = true;
      break;
    }
    __asm volatile("wfi");
  }

  const uint64_t counterUs = g_grtc.counter();
  g_grtc.end();

  char detail[96];
  snprintf(detail, sizeof(detail), "fired=%s counter=%llu",
           fired ? "yes" : "no",
           static_cast<unsigned long long>(counterUs));
  reportResult("GRTC", ok && fired, detail);
  return ok && fired;
}

static bool testTemp() {
  const bool ok = g_temp.sampleMilliDegreesC(&g_tempMilliC, 400000UL);
  const bool rangeOk = (g_tempMilliC > -50000) && (g_tempMilliC < 130000);

  char detail[64];
  snprintf(detail, sizeof(detail), "temp=%ldmC",
           static_cast<long>(g_tempMilliC));
  reportResult("TEMP", ok && rangeOk, detail);
  return ok && rangeOk;
}

static bool testWatchdog() {
  const bool running = g_wdt.isRunning();
  const bool ok = !running && g_wdt.configure(3000U, 0U, true, false, true);
  const uint32_t reqStatus = g_wdt.requestStatus();
  const bool rr0Enabled = (reqStatus & WDT_REQSTATUS_RR0_Msk) != 0U;

  char detail[80];
  snprintf(detail, sizeof(detail), "running=%s req=0x%08lX",
           running ? "yes" : "no",
           static_cast<unsigned long>(reqStatus));
  reportResult("WDT", ok && rr0Enabled, detail);
  return ok && rr0Enabled;
}

static bool testPdm() {
  alignas(4) static int16_t pcm[64] = {};

  bool ok = g_pdm.begin(kPinMicClk, kPinMicData, true, 40U,
                        PDM_RATIO_RATIO_Ratio64, PdmEdge::kLeftRising);
  if (ok) {
    g_pdmCaptureOk = g_pdm.capture(pcm, 64U, 8000000UL);
  } else {
    g_pdmCaptureOk = false;
  }
  g_pdm.end();

  long checksum = 0;
  for (size_t i = 0; i < 64U; ++i) {
    checksum += pcm[i];
  }

  char detail[96];
  snprintf(detail, sizeof(detail), "capture=%s sum=%ld first=%d",
           g_pdmCaptureOk ? "ok" : "fail",
           checksum,
           static_cast<int>(pcm[0]));
  reportResult("PDM", ok && g_pdmCaptureOk, detail);
  return ok && g_pdmCaptureOk;
}

static bool testBle() {
  bool ok = g_ble.begin(-8);
  if (ok) {
    ok = g_ble.setAdvertisingPduType(BleAdvPduType::kAdvInd) &&
         g_ble.setAdvertisingName("SELFTEST-BLE", true) &&
         g_ble.setScanResponseName("SELFTEST-BLE-SCAN") &&
         g_ble.setGattDeviceName("SELFTEST-GATT") &&
         g_ble.setGattBatteryLevel(77U);
  }
  BleAdvInteraction interaction{};
  if (ok) {
    g_bleAdvOk = g_ble.advertiseInteractEvent(&interaction, 350U, 300000UL, 700000UL);
  } else {
    g_bleAdvOk = false;
  }
  g_ble.end();

  reportResult("BLE", ok && g_bleAdvOk, g_bleAdvOk ? "legacy adv ok" : "adv fail");
  return ok && g_bleAdvOk;
}

void setup() {
  Serial.begin(115200);
  delay(400);

  Serial.print("\r\n");
  Serial.print("Nrf54L15-Clean-Implementation PeripheralSelfTest\r\n");

  const bool clockOk = ClockControl::startHfxo(true, 100000UL);
  reportResult("CLOCK", clockOk, "runtime-managed by core");

  testGpio();
  testAdc();
  testSpi();
  testI2c();
  testUart();
  testTimer();
  testPwm();
  testGpiote();
  testPowerReset();
  testGrtc();
  testTemp();
  testWatchdog();
  testPdm();
  testBle();

  char summary[96];
  snprintf(summary, sizeof(summary), "SUMMARY: %lu/%lu PASS\r\n",
           static_cast<unsigned long>(g_passCount),
           static_cast<unsigned long>(g_totalCount));
  Serial.print(summary);
}

void loop() {
  g_gpiote20.service();

  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  if ((now - lastMs) < 1000UL) {
    return;
  }
  lastMs = now;

  bool buttonHigh = true;
  Gpio::read(kPinUserButton, &buttonHigh);

  char line[208];
  snprintf(line, sizeof(line),
           "alive t=%lu btn=%s edges=%lu A0=%ldmV VBAT=%ldmV TEMP=%ldmC I2C=%s(0x%02X) PDM=%s BLE=%s\r\n",
           static_cast<unsigned long>(now),
           buttonHigh ? "released" : "pressed",
           static_cast<unsigned long>(g_buttonEdges),
           static_cast<long>(g_a0mV),
           static_cast<long>(g_vbatmV),
           static_cast<long>(g_tempMilliC),
           g_i2cAck ? "ACK" : "NACK", g_i2cWhoAmI,
           g_pdmCaptureOk ? "ok" : "fail",
           g_bleAdvOk ? "ok" : "fail");
  Serial.print(line);
}
