/*
  RuntimePeripheralPinRemap

  Demonstrates runtime pin remapping for Serial and SPI so one firmware image
  can be reused across boards that expose the same peripherals on different
  headers.

  Notes:
  - Call setPins(...) before begin(...) when you know the target route up front.
  - Calling setPins(...) after begin(...) is also supported; the core will stop
    and restart the peripheral with the last active configuration.
  - The defaults below match the XIAO nRF54L15 route, so the sketch runs on the
    stock board without further changes.

  Serial ordering:
  - Serial.setPins(rx, tx)

  SPI ordering:
  - SPI.setPins(sck, miso, mosi, ss)
*/

#include <SPI.h>

static constexpr int8_t kSerialRxPin = PIN_SERIAL_RX;
static constexpr int8_t kSerialTxPin = PIN_SERIAL_TX;

static constexpr int8_t kSpiSckPin = PIN_SPI_SCK;
static constexpr int8_t kSpiMisoPin = PIN_SPI_MISO;
static constexpr int8_t kSpiMosiPin = PIN_SPI_MOSI;
static constexpr int8_t kSpiSsPin = PIN_SPI_SS;

void setup() {
  const bool serialRemapped = Serial.setPins(kSerialRxPin, kSerialTxPin);
  Serial.begin(115200);
  while (!Serial) {
    delay(1);
  }

  const bool spiRemapped = SPI.setPins(kSpiSckPin, kSpiMisoPin, kSpiMosiPin, kSpiSsPin);
  SPI.begin();

  Serial.println("Runtime peripheral pin remap");
  Serial.print("Serial remap result: ");
  Serial.println(serialRemapped ? "ok" : "failed");
  Serial.print("SPI remap result: ");
  Serial.println(spiRemapped ? "ok" : "failed");
  Serial.println("Replace the pin constants at the top of this sketch for another board route.");
}

void loop() {
  static uint8_t counter = 0;

  Serial.print("tick ");
  Serial.println(counter++);

  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  (void)SPI.transfer(0x9F);
  SPI.endTransaction();

  delay(1000);
}
