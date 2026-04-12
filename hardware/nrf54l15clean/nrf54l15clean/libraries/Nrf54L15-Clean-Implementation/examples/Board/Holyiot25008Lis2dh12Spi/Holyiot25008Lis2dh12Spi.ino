#if !defined(ARDUINO_HOLYIOT_25008_NRF54L15)
#error "Select Tools > Board > HOLYIOT-25008 nRF54L15 Module for this example."
#endif

#include <SPI.h>

namespace {

constexpr uint8_t kRegWhoAmI = 0x0F;
constexpr uint8_t kRegCtrl1 = 0x20;
constexpr uint8_t kRegCtrl4 = 0x23;
constexpr uint8_t kRegOutXL = 0x28;
constexpr uint8_t kWhoAmIExpected = 0x33;

void setChannel(uint8_t pin, bool on) {
  digitalWrite(pin, on ? LED_STATE_ON : !LED_STATE_ON);
}

void setRgb(bool red, bool green, bool blue) {
  setChannel(LED_RED, red);
  setChannel(LED_GREEN, green);
  setChannel(LED_BLUE, blue);
}

void selectAccel(bool selected) {
  digitalWrite(PIN_LIS2DH12_CS, selected ? LOW : HIGH);
}

uint8_t readRegister(uint8_t reg) {
  SPI.beginTransaction(SPISettings(4000000UL, MSBFIRST, SPI_MODE0));
  selectAccel(true);
  SPI.transfer(static_cast<uint8_t>(reg | 0x80U));
  const uint8_t value = SPI.transfer(0x00U);
  selectAccel(false);
  SPI.endTransaction();
  return value;
}

void writeRegister(uint8_t reg, uint8_t value) {
  SPI.beginTransaction(SPISettings(4000000UL, MSBFIRST, SPI_MODE0));
  selectAccel(true);
  SPI.transfer(reg & 0x3FU);
  SPI.transfer(value);
  selectAccel(false);
  SPI.endTransaction();
}

void readRegisters(uint8_t startReg, uint8_t* out, size_t len) {
  if (out == nullptr || len == 0U) {
    return;
  }

  SPI.beginTransaction(SPISettings(4000000UL, MSBFIRST, SPI_MODE0));
  selectAccel(true);
  SPI.transfer(static_cast<uint8_t>(startReg | 0xC0U));
  for (size_t i = 0; i < len; ++i) {
    out[i] = SPI.transfer(0x00U);
  }
  selectAccel(false);
  SPI.endTransaction();
}

bool beginAccel() {
  SPI.setPins(PIN_LIS2DH12_SCK, PIN_LIS2DH12_MISO, PIN_LIS2DH12_MOSI, PIN_LIS2DH12_CS);
  SPI.begin();

  pinMode(PIN_LIS2DH12_CS, OUTPUT);
  selectAccel(false);

  if (readRegister(kRegWhoAmI) != kWhoAmIExpected) {
    return false;
  }

  writeRegister(kRegCtrl1, 0x57U);
  writeRegister(kRegCtrl4, 0x88U);
  return true;
}

void readAxes(int16_t* x, int16_t* y, int16_t* z) {
  uint8_t raw[6] = {0};
  readRegisters(kRegOutXL, raw, sizeof(raw));

  if (x != nullptr) {
    *x = static_cast<int16_t>((static_cast<int16_t>(raw[1]) << 8) | raw[0]) >> 4;
  }
  if (y != nullptr) {
    *y = static_cast<int16_t>((static_cast<int16_t>(raw[3]) << 8) | raw[2]) >> 4;
  }
  if (z != nullptr) {
    *z = static_cast<int16_t>((static_cast<int16_t>(raw[5]) << 8) | raw[4]) >> 4;
  }
}

void showDominantAxis(int16_t x, int16_t y, int16_t z) {
  int32_t ax = (x < 0) ? -static_cast<int32_t>(x) : static_cast<int32_t>(x);
  int32_t ay = (y < 0) ? -static_cast<int32_t>(y) : static_cast<int32_t>(y);
  int32_t az = (z < 0) ? -static_cast<int32_t>(z) : static_cast<int32_t>(z);

  if (ax >= ay && ax >= az) {
    setRgb(true, false, false);
  } else if (ay >= ax && ay >= az) {
    setRgb(false, true, false);
  } else {
    setRgb(false, false, true);
  }
}

void blinkMissingSensor() {
  setRgb(true, false, false);
  delay(100);
  setRgb(false, false, false);
  delay(100);
}

bool g_accelReady = false;

}  // namespace

void setup() {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  setRgb(false, false, false);
  g_accelReady = beginAccel();
}

void loop() {
  if (!g_accelReady) {
    blinkMissingSensor();
    return;
  }

  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
  readAxes(&x, &y, &z);
  showDominantAxis(x, y, z);
  delay(80);
}
