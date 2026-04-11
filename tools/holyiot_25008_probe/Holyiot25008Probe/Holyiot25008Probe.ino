#include <Arduino.h>
#include <nrf54l15.h>

namespace {

struct RawPin {
  NRF_GPIO_Type* gpio;
  uint8_t pin;
};

constexpr RawPin kLedRed{NRF_P2, 9};
constexpr RawPin kLedGreen{NRF_P1, 10};
constexpr RawPin kLedBlue{NRF_P2, 7};
constexpr RawPin kButton{NRF_P1, 13};

constexpr uint32_t bitMask(uint8_t pin) {
  return (1UL << pin);
}

void configureOutput(const RawPin& pin, bool high) {
  const uint32_t bit = bitMask(pin.pin);
  uint32_t cnf = pin.gpio->PIN_CNF[pin.pin];
  cnf &= ~(GPIO_PIN_CNF_DIR_Msk |
           GPIO_PIN_CNF_INPUT_Msk |
           GPIO_PIN_CNF_PULL_Msk |
           GPIO_PIN_CNF_DRIVE0_Msk |
           GPIO_PIN_CNF_DRIVE1_Msk |
           GPIO_PIN_CNF_SENSE_Msk |
           GPIO_PIN_CNF_CTRLSEL_Msk);
  cnf |= (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos);
  cnf |= GPIO_PIN_CNF_INPUT_Disconnect;
  cnf |= GPIO_PIN_CNF_PULL_Disabled;
  cnf |= GPIO_PIN_CNF_DRIVE_S0S1;
  cnf |= GPIO_PIN_CNF_SENSE_Disabled;
  cnf |= ((GPIO_PIN_CNF_CTRLSEL_GPIO << GPIO_PIN_CNF_CTRLSEL_Pos) & GPIO_PIN_CNF_CTRLSEL_Msk);

  if (high) {
    pin.gpio->OUTSET = bit;
  } else {
    pin.gpio->OUTCLR = bit;
  }
  pin.gpio->DIRSET = bit;
  pin.gpio->PIN_CNF[pin.pin] = cnf;
}

void configureInputPullup(const RawPin& pin) {
  const uint32_t bit = bitMask(pin.pin);
  uint32_t cnf = pin.gpio->PIN_CNF[pin.pin];
  cnf &= ~(GPIO_PIN_CNF_DIR_Msk |
           GPIO_PIN_CNF_INPUT_Msk |
           GPIO_PIN_CNF_PULL_Msk |
           GPIO_PIN_CNF_DRIVE0_Msk |
           GPIO_PIN_CNF_DRIVE1_Msk |
           GPIO_PIN_CNF_SENSE_Msk |
           GPIO_PIN_CNF_CTRLSEL_Msk);
  cnf |= (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos);
  cnf |= GPIO_PIN_CNF_INPUT_Connect;
  cnf |= GPIO_PIN_CNF_PULL_Pullup;
  cnf |= GPIO_PIN_CNF_DRIVE_S0S1;
  cnf |= GPIO_PIN_CNF_SENSE_Disabled;
  cnf |= ((GPIO_PIN_CNF_CTRLSEL_GPIO << GPIO_PIN_CNF_CTRLSEL_Pos) & GPIO_PIN_CNF_CTRLSEL_Msk);

  pin.gpio->DIRCLR = bit;
  pin.gpio->PIN_CNF[pin.pin] = cnf;
}

void writeRaw(const RawPin& pin, bool high) {
  if (high) {
    pin.gpio->OUTSET = bitMask(pin.pin);
  } else {
    pin.gpio->OUTCLR = bitMask(pin.pin);
  }
}

bool readRaw(const RawPin& pin) {
  return (pin.gpio->IN & bitMask(pin.pin)) != 0U;
}

void setLedChannel(const RawPin& pin, bool on) {
  // The HOLYIOT-25008 board support describes the RGB LED pins as active low.
  writeRaw(pin, !on);
}

void setRgb(bool red, bool green, bool blue) {
  setLedChannel(kLedRed, red);
  setLedChannel(kLedGreen, green);
  setLedChannel(kLedBlue, blue);
}

void flashRgb(bool red, bool green, bool blue, uint32_t onMs, uint32_t offMs, uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    setRgb(red, green, blue);
    delay(onMs);
    setRgb(false, false, false);
    if (i + 1U < count) {
      delay(offMs);
    }
  }
}

bool buttonPressed() {
  return !readRaw(kButton);
}

}  // namespace

void setup() {
  configureOutput(kLedRed, true);
  configureOutput(kLedGreen, true);
  configureOutput(kLedBlue, true);
  configureInputPullup(kButton);

  setRgb(false, false, false);
  delay(100);
  flashRgb(true, true, true, 120, 80, 2);
  setRgb(false, false, false);
}

void loop() {
  if (buttonPressed()) {
    flashRgb(true, true, true, 60, 60, 3);
    while (buttonPressed()) {
      setRgb(true, true, true);
      delay(20);
    }
    setRgb(false, false, false);
    delay(120);
    return;
  }

  setRgb(true, false, false);
  delay(250);
  setRgb(false, true, false);
  delay(250);
  setRgb(false, false, true);
  delay(250);
  setRgb(false, false, false);
  delay(120);
}
