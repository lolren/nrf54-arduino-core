#include <Arduino.h>
#include <nrf54l15.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

// Datasheet-guided low-power strategy:
// - Run at lower CPU frequency for periodic housekeeping tasks.
// - Keep SPI/I2C peripherals disabled except for short transaction windows.
// - Enter WFI between windows so HFCLK requests can drop when idle.

static constexpr uint32_t kWindowPeriodMs = 12000UL;
static constexpr uint32_t kLedPulseMs = 5UL;

static Spim g_spi21(nrf54l15::SPIM21_BASE, 128000000UL);
static Twim g_i2c21(nrf54l15::TWIM21_BASE);

static inline void cpuIdleWfi() {
  __asm volatile("wfi");
}

static void sleepUntilMs(uint32_t deadlineMs) {
  while (static_cast<int32_t>(millis() - deadlineMs) < 0) {
    cpuIdleWfi();
  }
}

static bool spiProbe(uint8_t* outRx) {
  if (outRx == nullptr) {
    return false;
  }

  const uint8_t tx = 0x9FU;
  uint8_t rx = 0U;

  bool ok = g_spi21.begin(kDefaultSpiSck, kDefaultSpiMosi, kDefaultSpiMiso,
                          kPinDisconnected, 8000000UL, SpiMode::kMode0, false);
  if (ok) {
    ok = g_spi21.transfer(&tx, &rx, 1, 300000UL);
  }
  g_spi21.end();

  *outRx = rx;
  return ok;
}

static bool i2cProbe(uint8_t* outWhoAmI) {
  if (outWhoAmI == nullptr) {
    return false;
  }

  uint8_t reg = 0x0FU;
  uint8_t val = 0U;

  bool ok = g_i2c21.begin(kDefaultI2cScl, kDefaultI2cSda, TwimFrequency::k400k);
  if (ok) {
    ok = g_i2c21.writeRead(0x6A, &reg, 1, &val, 1, 300000UL);
  }
  g_i2c21.end();

  *outWhoAmI = val;
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  (void)Gpio::configure(kPinUserLed, GpioDirection::kOutput, GpioPull::kDisabled);
  (void)Gpio::write(kPinUserLed, true);  // LED off

  NRF_OSCILLATORS->PLL.FREQ = OSCILLATORS_PLL_FREQ_FREQ_CK64M;
  Serial.println("LowPowerPeripheralGating: started");
}

void loop() {
  const uint32_t t0 = millis();

  (void)Gpio::write(kPinUserLed, false);
  delay(kLedPulseMs);
  (void)Gpio::write(kPinUserLed, true);

  uint8_t spiRx = 0U;
  uint8_t whoAmI = 0U;

  const bool spiOk = spiProbe(&spiRx);
  const bool i2cOk = i2cProbe(&whoAmI);

  char line[128];
  snprintf(line, sizeof(line), "window t=%lu SPI=%s(0x%02X) I2C=%s(0x%02X)\r\n",
           static_cast<unsigned long>(t0),
           spiOk ? "OK" : "FAIL", spiRx,
           i2cOk ? "ACK" : "NACK", whoAmI);
  Serial.print(line);

  sleepUntilMs(t0 + kWindowPeriodMs);
}
