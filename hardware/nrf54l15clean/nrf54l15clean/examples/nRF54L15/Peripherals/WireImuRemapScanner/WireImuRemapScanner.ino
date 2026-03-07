/*
  WireImuRemapScanner

  Probes the optional IMU/back-pad I2C bus on D12/D11 while keeping USB Serial
  logging active.

  Historical note:
  - this example kept its original filename for continuity with issue links
  - current main no longer needs a Wire remap workaround for this use case
  - use Wire1 directly for the D12/D11 bus

  Notes:
  - The D11/D12 IMU footprint is only populated on XIAO nRF54L15 Sense boards.
  - This example intentionally probes the known IMU addresses instead of doing
    a full 1..126 scan, because an empty bus can take a long time to scan with
    the current software timeout path.

  Hardware:
  - IMU/back-pad SDA: D12 / P0.04
  - IMU/back-pad SCL: D11 / P0.03
  - IMU power gate:   IMU_MIC_EN / P0.01
*/

#include <Wire.h>

static constexpr uint8_t kImuWhoAmIReg = 0x0FU;
static const uint8_t kImuAddresses[] = {0x6AU, 0x6BU};

static bool readWhoAmI(TwoWire& bus, uint8_t address, uint8_t* whoAmI) {
  if (whoAmI == nullptr) {
    return false;
  }

  bus.beginTransmission(address);
  bus.write(kImuWhoAmIReg);
  if (bus.endTransmission(false) != 0U) {
    return false;
  }

  const int received = bus.requestFrom(static_cast<int>(address), 1, 1);
  if (received != 1 || bus.available() <= 0) {
    return false;
  }

  *whoAmI = static_cast<uint8_t>(bus.read());
  return true;
}

static void probeImuBus(TwoWire& bus) {
  bool found = false;

  Serial.println("Probing D12/D11 for optional IMU...");
  for (uint8_t i = 0; i < (sizeof(kImuAddresses) / sizeof(kImuAddresses[0])); ++i) {
    const uint8_t address = kImuAddresses[i];
    uint8_t whoAmI = 0U;
    if (readWhoAmI(bus, address, &whoAmI)) {
      Serial.print("IMU responded at 0x");
      if (address < 16U) {
        Serial.print('0');
      }
      Serial.print(address, HEX);
      Serial.print(" WHO_AM_I=0x");
      if (whoAmI < 16U) {
        Serial.print('0');
      }
      Serial.println(whoAmI, HEX);
      found = true;
    }
  }

  if (!found) {
    Serial.println("No IMU response at 0x6A/0x6B on D12/D11");
    Serial.println("This is expected on non-Sense boards or if the optional IMU is not populated.");
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(1);
  }

  pinMode(IMU_MIC_EN, OUTPUT);
  digitalWrite(IMU_MIC_EN, HIGH);
  delay(10);

  Wire1.begin();
  Wire1.setClock(400000);

  Serial.println("Wire1 active on D12/D11 for optional IMU probe");
}

void loop() {
  probeImuBus(Wire1);
  delay(1000);
}
