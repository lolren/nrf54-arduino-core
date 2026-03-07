#include <Arduino.h>

#include <stdio.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static constexpr bool kEnableAdvancedPeripherals = true;

static Spim g_spi21(nrf54l15::SPIM21_BASE, 128000000UL);
static Twim g_i2c21(nrf54l15::TWIM21_BASE);
static Saadc g_adc(nrf54l15::SAADC_BASE);

static Timer g_timer20(nrf54l15::TIMER20_BASE, 16000000UL, 6);
static Pwm g_pwm20(nrf54l15::PWM20_BASE);
static Gpiote g_gpiote20(nrf54l15::GPIOTE20_BASE, 8);

static volatile uint32_t g_timerTicks = 0;
static volatile uint32_t g_buttonEdges = 0;
static volatile bool g_buttonPressed = false;

static uint32_t g_lastTickMs = 0;
static bool g_ledState = false;

static void logLine(const char* text) {
  if (text == nullptr) {
    return;
  }
  Serial.print(text);
}

static void onTimerCompare(uint8_t channel, void* context) {
  (void)channel;
  (void)context;
  ++g_timerTicks;
}

static void onButtonEdge(uint8_t channel, void* context) {
  (void)channel;
  (void)context;

  bool levelHigh = true;
  if (Gpio::read(kPinUserButton, &levelHigh)) {
    g_buttonPressed = !levelHigh;
  }
  ++g_buttonEdges;
}

static bool doI2cWhoAmI(uint8_t* valueOut) {
  if (valueOut == nullptr) {
    return false;
  }

  uint8_t reg = 0x0F;
  uint8_t value = 0;

  if (!g_i2c21.begin(kDefaultI2cScl, kDefaultI2cSda, TwimFrequency::k400k)) {
    return false;
  }

  const bool ok = g_i2c21.writeRead(0x6A, &reg, 1, &value, 1, 200000UL);
  g_i2c21.end();

  *valueOut = value;
  return ok;
}

static bool doSpiProbe(uint8_t* valueOut) {
  if (valueOut == nullptr) {
    return false;
  }

  const uint8_t tx = 0x9F;
  uint8_t rx = 0;

  if (!g_spi21.begin(kDefaultSpiSck, kDefaultSpiMosi, kDefaultSpiMiso,
                     kPinDisconnected, 8000000UL, SpiMode::kMode0, false)) {
    return false;
  }

  const bool ok = g_spi21.transfer(&tx, &rx, 1, 200000UL);
  g_spi21.end();

  *valueOut = rx;
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  logLine("Nrf54L15-Clean-Implementation boot\r\n");

  Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinUserLed, true);
  Gpio::configure(kPinUserButton, GpioDirection::kInput, GpioPull::kDisabled);

  Gpio::configure(kPinVbatEnable, GpioDirection::kOutput, GpioPull::kDisabled);
  Gpio::write(kPinVbatEnable, false);

  const bool hfxoOk = ClockControl::startHfxo(true);
  const bool adcOk = g_adc.begin(AdcResolution::k12bit);

  uint8_t spiFirst = 0;
  const bool spiOk = doSpiProbe(&spiFirst);

  uint8_t imuFirst = 0;
  const bool i2cOk = doI2cWhoAmI(&imuFirst);

  bool timerOk = false;
  bool pwmOk = false;
  bool gpioteOk = false;

  if (kEnableAdvancedPeripherals) {
    timerOk = g_timer20.begin(TimerBitWidth::k32bit, 4, false);
    if (timerOk) {
      const uint32_t periodTicks = g_timer20.ticksFromMicros(100000UL);
      g_timer20.attachCompareCallback(0, onTimerCompare, nullptr);
      timerOk = g_timer20.setCompare(0, periodTicks, true, false, false, false);
      if (timerOk) {
        g_timer20.start();
      }
    }

    pwmOk = g_pwm20.beginSingle(kPinUserLed, 1000UL, 150, false);
    if (pwmOk) {
      pwmOk = g_pwm20.start(0, 200000UL);
    }

    gpioteOk = g_gpiote20.configureEvent(0, kPinUserButton, GpiotePolarity::kToggle,
                                         false) &&
               g_gpiote20.attachInCallback(0, onButtonEdge, nullptr);
  }

  char line[256];
  snprintf(line, sizeof(line),
           "init: hfxo=%s adc=%s spi=%s(0x%02X) i2c=%s(0x%02X) timer=%s pwm=%s gpiote=%s adv=%s\r\n",
           hfxoOk ? "OK" : "FAIL",
           adcOk ? "OK" : "FAIL",
           spiOk ? "OK" : "FAIL", spiFirst,
           i2cOk ? "ACK" : "NACK", imuFirst,
           timerOk ? "OK" : (kEnableAdvancedPeripherals ? "FAIL" : "OFF"),
           pwmOk ? "OK" : (kEnableAdvancedPeripherals ? "FAIL" : "OFF"),
           gpioteOk ? "OK" : (kEnableAdvancedPeripherals ? "FAIL" : "OFF"),
           kEnableAdvancedPeripherals ? "ON" : "OFF");
  logLine(line);
}

void loop() {
  if (kEnableAdvancedPeripherals) {
    g_timer20.service();
    g_gpiote20.service();

    if (g_timerTicks == 0U) {
      return;
    }

    static uint32_t lastTimerTicks = 0;
    if (g_timerTicks == lastTimerTicks) {
      return;
    }
    lastTimerTicks = g_timerTicks;
  } else {
    const uint32_t now = millis();
    if ((now - g_lastTickMs) < 1000UL) {
      return;
    }
    g_lastTickMs = now;

    g_ledState = !g_ledState;
    Gpio::write(kPinUserLed, !g_ledState);
  }

  bool buttonHigh = true;
  Gpio::read(kPinUserButton, &buttonHigh);
  const bool buttonPressed = !buttonHigh;

  int32_t a0mV = -1;
  if (g_adc.configureSingleEnded(0, kPinA0, AdcGain::k2over8)) {
    g_adc.sampleMilliVolts(&a0mV);
  }

  int32_t vbatmV = -1;
  Gpio::write(kPinVbatEnable, true);
  delayMicroseconds(200);
  if (g_adc.configureSingleEnded(0, kPinVbatSense, AdcGain::k2over8)) {
    int32_t vbatHalf = -1;
    if (g_adc.sampleMilliVolts(&vbatHalf)) {
      vbatmV = vbatHalf * 2;
    }
  }
  Gpio::write(kPinVbatEnable, false);

  uint8_t whoamiVal = 0;
  const bool imuOk = doI2cWhoAmI(&whoamiVal);

  uint8_t spiRx = 0;
  const bool spiOk = doSpiProbe(&spiRx);

  char line[224];
  snprintf(line, sizeof(line),
           "t=%lu btn=%s edges=%lu A0=%ldmV VBAT=%ldmV I2C=%s(0x%02X) SPI=%s(0x%02X)\r\n",
           static_cast<unsigned long>(millis()),
           buttonPressed ? "PRESSED" : "released",
           static_cast<unsigned long>(g_buttonEdges),
           static_cast<long>(a0mV),
           static_cast<long>(vbatmV),
           imuOk ? "ACK" : "NACK", whoamiVal,
           spiOk ? "OK" : "FAIL", spiRx);
  logLine(line);
}
