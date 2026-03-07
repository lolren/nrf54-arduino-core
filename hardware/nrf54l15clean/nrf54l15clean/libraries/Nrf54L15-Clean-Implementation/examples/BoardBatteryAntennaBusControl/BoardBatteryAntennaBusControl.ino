#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static const uint32_t kI2cProfilesHz[] = {100000UL, 400000UL, 1000000UL};
static const uint32_t kSpiProfilesHz[] = {1000000UL, 4000000UL, 8000000UL};

static uint8_t g_i2cProfile = 1U;
static uint8_t g_spiProfile = 1U;

static uint32_t g_lastPrintMs = 0;
static int32_t g_vbatMilliVolts = -1;
static uint8_t g_vbatPercent = 0U;

static const char* antennaName(BoardAntennaPath path) {
  switch (path) {
    case BoardAntennaPath::kExternal:
      return "external";
    case BoardAntennaPath::kControlHighImpedance:
      return "control-hiz";
    case BoardAntennaPath::kCeramic:
    default:
      return "ceramic";
  }
}

static void applyBusProfiles() {
  Wire.setClock(kI2cProfilesHz[g_i2cProfile]);
}

static bool probeImuWhoAmI(uint8_t* outWhoAmI) {
  if (outWhoAmI == nullptr) {
    return false;
  }

  const uint8_t imuAddr = 0x6AU;
  const uint8_t whoAmIReg = 0x0FU;

  Wire.beginTransmission(imuAddr);
  Wire.write(whoAmIReg);
  if (Wire.endTransmission(false) != 0U) {
    return false;
  }

  const int got = Wire.requestFrom(static_cast<int>(imuAddr), 1, 1);
  if (got != 1 || Wire.available() <= 0) {
    return false;
  }

  *outWhoAmI = static_cast<uint8_t>(Wire.read());
  return true;
}

static uint8_t spiLoopbackProbe() {
  pinMode(SS, OUTPUT);
  digitalWrite(SS, LOW);
  SPI.beginTransaction(SPISettings(kSpiProfilesHz[g_spiProfile], MSBFIRST, SPI_MODE0));
  const uint8_t rx = SPI.transfer(0x9FU);
  SPI.endTransaction();
  digitalWrite(SS, HIGH);
  return rx;
}

static void printStatus(const char* prefix) {
  uint8_t whoAmI = 0U;
  const bool imuAck = probeImuWhoAmI(&whoAmI);
  const uint8_t spiRx = spiLoopbackProbe();
  const BoardAntennaPath antenna = BoardControl::antennaPath();

  Serial.print(prefix);
  Serial.print(" VBAT=");
  Serial.print(g_vbatMilliVolts);
  Serial.print("mV(");
  Serial.print(g_vbatPercent);
  Serial.print("%) ANT=");
  Serial.print(antennaName(antenna));
  Serial.print(" I2C=");
  Serial.print(static_cast<unsigned long>(kI2cProfilesHz[g_i2cProfile]));
  Serial.print("Hz");
  Serial.print(" IMU=");
  if (imuAck) {
    Serial.print("ACK(0x");
    if (whoAmI < 0x10U) {
      Serial.print('0');
    }
    Serial.print(whoAmI, HEX);
    Serial.print(')');
  } else {
    Serial.print("NACK");
  }
  Serial.print(" SPI=");
  Serial.print(static_cast<unsigned long>(kSpiProfilesHz[g_spiProfile]));
  Serial.print("Hz rx=0x");
  if (spiRx < 0x10U) {
    Serial.print('0');
  }
  Serial.println(spiRx, HEX);
}

static void printHelp() {
  Serial.println("Commands:");
  Serial.println("  b - sample battery now");
  Serial.println("  c - select ceramic antenna path");
  Serial.println("  e - select external antenna path");
  Serial.println("  z - set RF switch control pin to high-impedance");
  Serial.println("  i - cycle I2C frequency (100k/400k/1M)");
  Serial.println("  s - cycle SPI frequency (1M/4M/8M)");
  Serial.println("  ? - show help");
}

static void sampleBatteryAndReport(const char* prefix) {
  const bool mvOk = BoardControl::sampleBatteryMilliVolts(&g_vbatMilliVolts);
  const bool pctOk = BoardControl::sampleBatteryPercent(&g_vbatPercent);
  if (!mvOk || !pctOk) {
    g_vbatMilliVolts = -1;
    g_vbatPercent = 0U;
  }
  printStatus(prefix);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin();
  SPI.begin();
  applyBusProfiles();
  BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);

  Serial.println();
  Serial.println("BoardBatteryAntennaBusControl");
  Serial.println("Defaults: Wire SDA=D4/SCL=D5, SPI MOSI=D10 MISO=D9 SCK=D8 SS=D2");
  printHelp();
  sampleBatteryAndReport("boot");
}

void loop() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == 'b' || c == 'B') {
      sampleBatteryAndReport("manual");
    } else if (c == 'c' || c == 'C') {
      BoardControl::setAntennaPath(BoardAntennaPath::kCeramic);
      printStatus("antenna");
    } else if (c == 'e' || c == 'E') {
      BoardControl::setAntennaPath(BoardAntennaPath::kExternal);
      printStatus("antenna");
    } else if (c == 'z' || c == 'Z') {
      BoardControl::setAntennaPath(BoardAntennaPath::kControlHighImpedance);
      printStatus("antenna");
    } else if (c == 'i' || c == 'I') {
      g_i2cProfile = static_cast<uint8_t>((g_i2cProfile + 1U) %
                                          (sizeof(kI2cProfilesHz) / sizeof(kI2cProfilesHz[0])));
      applyBusProfiles();
      printStatus("i2c");
    } else if (c == 's' || c == 'S') {
      g_spiProfile = static_cast<uint8_t>((g_spiProfile + 1U) %
                                          (sizeof(kSpiProfilesHz) / sizeof(kSpiProfilesHz[0])));
      printStatus("spi");
    } else if (c == '?') {
      printHelp();
    }
  }

  const uint32_t now = millis();
  if ((now - g_lastPrintMs) >= 5000U) {
    g_lastPrintMs = now;
    sampleBatteryAndReport("periodic");
  }
}
