#include <Arduino.h>
#include <nrf54l15_hal.h>
#include <variant.h>

using namespace xiao_nrf54l15;

#if defined(NRF_TRUSTZONE_NONSECURE)
#error "LowPowerZephyrParityBlink requires Security Domain = Secure."
#endif

namespace {

constexpr uint32_t kBlinkOnUs = 5000UL;
constexpr uint32_t kSystemOffUs = 1000000UL;
constexpr uint8_t kLedPin = 0U;  // XIAO nRF54L15 LED = P2.0, active low.

PowerManager gPower;

void ledInit() {
  NRF_P2->DIRSET = (1UL << kLedPin);
  NRF_P2->OUTSET = (1UL << kLedPin);
}

void ledOn() { NRF_P2->OUTCLR = (1UL << kLedPin); }

void ledOff() { NRF_P2->OUTSET = (1UL << kLedPin); }

}  // namespace

void setup() {
  xiaoNrf54l15EnterLowestPowerBoardState();
  ledInit();
}

void loop() {
  ledOn();
  delayMicroseconds(kBlinkOnUs);
  ledOff();

  gPower.systemOffTimedWakeUsNoRetention(kSystemOffUs);
}
