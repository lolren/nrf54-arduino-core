#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

Uarte g_uart22(nrf54l15::UARTE22_BASE);
Uarte g_uart30(nrf54l15::UARTE30_BASE);
Twim g_twim22(nrf54l15::TWIM22_BASE);
Twim g_twim30(nrf54l15::TWIM30_BASE);
Spim g_spim22(nrf54l15::SPIM22_BASE);
Spim g_spim30(nrf54l15::SPIM30_BASE);

bool g_lastUart22 = false;
bool g_lastUart30 = false;
bool g_lastTwim22 = false;
bool g_lastTwim30 = false;
bool g_lastSpim22 = false;
bool g_lastSpim30 = false;

void runProbe() {
  g_lastUart22 = g_uart22.begin(kPinD0, kPinD1, UarteBaud::k115200);
  g_uart22.end();

  g_lastUart30 = g_uart30.begin(kPinD2, kPinD3, UarteBaud::k115200);
  g_uart30.end();

  g_lastTwim22 = g_twim22.begin(kDefaultI2cScl, kDefaultI2cSda, TwimFrequency::k400k);
  g_twim22.end();

  g_lastTwim30 = g_twim30.begin(kPinD11, kPinD12, TwimFrequency::k400k);
  g_twim30.end();

  g_lastSpim22 = g_spim22.begin(kDefaultSpiSck, kDefaultSpiMosi, kDefaultSpiMiso,
                                kPinDisconnected, 4000000UL);
  g_spim22.end();

  g_lastSpim30 = g_spim30.begin(kPinD13, kPinD14, kPinD15, kPinD12, 4000000UL);
  g_spim30.end();
}

void printStatus() {
  Serial.print("uart22=");
  Serial.print(g_lastUart22 ? 1 : 0);
  Serial.print(" uart30=");
  Serial.print(g_lastUart30 ? 1 : 0);
  Serial.print(" twim22=");
  Serial.print(g_lastTwim22 ? 1 : 0);
  Serial.print(" twim30=");
  Serial.print(g_lastTwim30 ? 1 : 0);
  Serial.print(" spim22=");
  Serial.print(g_lastSpim22 ? 1 : 0);
  Serial.print(" spim30=");
  Serial.println(g_lastSpim30 ? 1 : 0);
}

void printHelp() {
  Serial.println("Commands: r rerun probe, s status");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("serial-fabric runtime probe");
  printHelp();
  runProbe();
  printStatus();
}

void loop() {
  if (!Serial.available()) {
    delay(20);
    return;
  }
  const int incoming = Serial.read();
  if (incoming < 0) {
    return;
  }
  switch (static_cast<char>(incoming)) {
    case 'r':
      runProbe();
      printStatus();
      break;
    case 's':
      printStatus();
      break;
    default:
      printHelp();
      break;
  }
}
