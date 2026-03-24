#include "SPI.h"
#include "Wire.h"

extern "C" void nrf54l15_analog_write_idle_service(void);
extern "C" void nrf54l15_ble_idle_service(void) __attribute__((weak));

extern "C" void nrf54l15_clean_idle_service(void) {
    nrf54l15_analog_write_idle_service();
    if (nrf54l15_ble_idle_service != 0) {
        nrf54l15_ble_idle_service();
    }
#if defined(NRF54L15_CLEAN_AUTO_GATE) && (NRF54L15_CLEAN_AUTO_GATE != 0)
    SPI.serviceAutoGate();
    Wire.serviceAutoGate();
    Wire1.serviceAutoGate();
#endif
}
