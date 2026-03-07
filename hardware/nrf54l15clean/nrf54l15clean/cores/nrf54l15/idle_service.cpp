#include "SPI.h"
#include "Wire.h"

extern "C" void nrf54l15_clean_idle_service(void) {
#if defined(NRF54L15_CLEAN_AUTO_GATE) && (NRF54L15_CLEAN_AUTO_GATE != 0)
    SPI.serviceAutoGate();
    Wire.serviceAutoGate();
#endif
}
