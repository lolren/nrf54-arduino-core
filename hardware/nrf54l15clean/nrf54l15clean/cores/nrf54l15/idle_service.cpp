#include "SPI.h"
#include "Wire.h"

extern "C" void nrf54l15_analog_write_idle_service(void);
extern "C" void nrf54l15_clean_ble_idle_service(void) __attribute__((weak));
extern "C" void nrf54l15_clean_ble_yield_service(void) __attribute__((weak));
extern "C" void nrf54l15_serial_idle_service(void);

extern "C" void nrf54l15_clean_idle_service(void) {
    nrf54l15_analog_write_idle_service();
    nrf54l15_serial_idle_service();
    if (nrf54l15_clean_ble_idle_service != nullptr) {
        nrf54l15_clean_ble_idle_service();
    }
#if defined(NRF54L15_CLEAN_AUTO_GATE) && (NRF54L15_CLEAN_AUTO_GATE != 0)
    SPI.serviceAutoGate();
    Wire.serviceAutoGate();
    Wire1.serviceAutoGate();
#endif
}

extern "C" void nrf54l15_clean_yield_service(void) {
    nrf54l15_analog_write_idle_service();
    nrf54l15_serial_idle_service();
    if (nrf54l15_clean_ble_yield_service != nullptr) {
        nrf54l15_clean_ble_yield_service();
    }
#if defined(NRF54L15_CLEAN_AUTO_GATE) && (NRF54L15_CLEAN_AUTO_GATE != 0)
    SPI.serviceAutoGate();
    Wire.serviceAutoGate();
    Wire1.serviceAutoGate();
#endif
}
