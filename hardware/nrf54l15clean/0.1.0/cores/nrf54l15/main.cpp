#include "Arduino.h"
#include "cmsis.h"

extern "C" void nrf54l15_clean_idle_service(void);
extern "C" size_t nrf54l15_heap_free_bytes(void);

extern "C" void __attribute__((weak)) init(void) {
    initSysTick();
}
extern "C" void __attribute__((weak)) initVariant(void) {}
extern "C" void __attribute__((weak)) yield(void) {
    nrf54l15_clean_idle_service();
#if defined(NRF54L15_CLEAN_POWER_LOW)
    static volatile uint32_t* const kScbScr = (volatile uint32_t*)0xE000ED10UL;
    static constexpr uint32_t kScbScrSleepDeep_Msk = (1UL << 2);
    static constexpr uint32_t kScbScrSleepOnExit_Msk = (1UL << 1);
    *kScbScr &= ~(kScbScrSleepDeep_Msk | kScbScrSleepOnExit_Msk);
    __asm volatile("wfi");
#else
    __asm volatile("nop");
#endif
}

extern "C" void __attribute__((weak)) softReset(void) {
    static constexpr uintptr_t kScbAircr = 0xE000ED0CUL;
    static constexpr uint32_t kAircrVectkey = (0x5FAUL << 16);
    static constexpr uint32_t kAircrSysResetReq = (1UL << 2);

    __DSB();
    *reinterpret_cast<volatile uint32_t*>(kScbAircr) =
        kAircrVectkey | kAircrSysResetReq;
    __DSB();
    __ISB();
    while (true) {
        __NOP();
    }
}

extern "C" void __attribute__((weak)) SoftReset(void) {
    softReset();
}

extern "C" uint32_t __attribute__((weak)) getFreeHeap(void) {
    return static_cast<uint32_t>(nrf54l15_heap_free_bytes());
}

extern "C" void setup(void) __attribute__((weak));
extern "C" void loop(void) __attribute__((weak));

int __attribute__((weak)) main(void) {
    init();
    initVariant();

    if (setup != nullptr) {
        setup();
    }

    while (true) {
        if (loop != nullptr) {
            loop();
        }
        yield();
    }

    return 0;
}
