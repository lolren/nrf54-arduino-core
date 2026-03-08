#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <nrf54l15.h>

// Demonstrates core-level peripheral auto-gating:
// 1) Keep SPI/Wire initialized once.
// 2) Perform short transfer windows.
// 3) Let the core idle service disable TWIM/SPIM when idle timeout expires.

static constexpr uint8_t kImuAddress = 0x6A;
static constexpr uint8_t kImuWhoAmIReg = 0x0F;
// Window cadence and visibility settings.
static constexpr uint32_t kWindowPeriodMs = 8000UL;
static constexpr uint32_t kLedPulseMs = 6UL;
static constexpr uint32_t kIdleProbeDelayMs = 20UL;

static inline void cpuIdleWfi() {
  __asm volatile("wfi");
}

static void sleepUntilMs(uint32_t deadlineMs) {
  while (static_cast<int32_t>(millis() - deadlineMs) < 0) {
    cpuIdleWfi();
  }
}

static uint32_t spim20EnableReg() {
  return NRF_SPIM20->ENABLE;
}

static uint32_t twim20EnableReg() {
  return NRF_TWIM20->ENABLE;
}

static void printEnableRegs(const char* tag) {
  Serial.print(tag);
  Serial.print(" SPIM20.EN=0x");
  Serial.print(spim20EnableReg(), HEX);
  Serial.print(" TWIM20.EN=0x");
  Serial.println(twim20EnableReg(), HEX);
}

void setup() {
  Serial.begin(115200);
  delay(250);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // active-low LED off

  NRF_OSCILLATORS->PLL.FREQ = OSCILLATORS_PLL_FREQ_FREQ_CK64M;

  SPI.begin();
  Wire.begin();

  Serial.println("LowPowerAutoGatePolicy: started");
  Serial.println("This example is meaningful only when the core auto-gating option is enabled.");
#if defined(NRF54L15_CLEAN_AUTO_GATE) && (NRF54L15_CLEAN_AUTO_GATE != 0)
  Serial.print("Peripheral Auto-Gating enabled, idle_us=");
  Serial.println(static_cast<unsigned long>(NRF54L15_CLEAN_AUTO_GATE_IDLE_US));
#else
  Serial.println("Peripheral Auto-Gating disabled in Tools menu.");
#endif

  printEnableRegs("boot");
}

void loop() {
  const uint32_t t0 = millis();

  digitalWrite(LED_BUILTIN, LOW);
  delay(kLedPulseMs);
  digitalWrite(LED_BUILTIN, HIGH);

  SPI.beginTransaction(SPISettings(8000000UL, MSBFIRST, SPI_MODE0));
  const uint8_t spiRx = SPI.transfer(0x9F);
  SPI.endTransaction();

  Wire.beginTransmission(kImuAddress);
  Wire.write(kImuWhoAmIReg);
  const uint8_t txErr = Wire.endTransmission(false);
  const uint8_t rxLen = Wire.requestFrom(kImuAddress, static_cast<uint8_t>(1), static_cast<uint8_t>(true));
  const int whoAmI = (rxLen == 1U) ? Wire.read() : -1;

  Serial.print("window t=");
  Serial.print(t0);
  Serial.print(" spi=0x");
  Serial.print(spiRx, HEX);
  Serial.print(" i2c_tx_err=");
  Serial.print(txErr);
  Serial.print(" whoami=");
  if (whoAmI >= 0) {
    Serial.println(whoAmI, HEX);
  } else {
    Serial.println("N/A");
  }

  printEnableRegs("after-xfer");
  delay(kIdleProbeDelayMs);
  printEnableRegs("after-idle");

  sleepUntilMs(t0 + kWindowPeriodMs);
}
