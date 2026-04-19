#include "Arduino.h"
#include "cmsis.h"

extern "C" void nrf54l15_clean_idle_service(void);
extern "C" void nrf54l15_software_timer_service(void);
extern "C" size_t nrf54l15_heap_total_bytes(void);
extern "C" size_t nrf54l15_heap_used_bytes(void);
extern "C" size_t nrf54l15_heap_free_bytes(void);
#if defined(NRF54L15_CLEAN_POWER_LOW)
extern "C" void nrf54l15_core_bootstrap_low_power_timebase(void);
#endif

namespace {
volatile bool g_loop_suspended = false;
}  // namespace

extern "C" void __attribute__((weak)) init(void) {
#if !defined(NRF54L15_CLEAN_LOWPOWER_BOOT_MINIMAL)
    initSysTick();
#if defined(NRF54L15_CLEAN_POWER_LOW)
    // Keep the GRTC low-power timebase lazy. BLE sketches can run setup-time
    // Serial and radio initialization without taking GRTC compare ownership.
#endif
#endif
}
extern "C" void __attribute__((weak)) initVariant(void) {}
extern "C" void __attribute__((weak)) yield(void) {
    nrf54l15_software_timer_service();
    nrf54l15_clean_idle_service();
#if defined(NRF54L15_CLEAN_POWER_LOW)
    // Low-power mode uses a tickless GRTC-backed timebase. Unlike the balanced
    // SysTick path, there is no guaranteed periodic interrupt to wake an
    // unconditional WFI here after loop() returns. Sleeping in yield() would
    // therefore deadlock ordinary sketches after their first iteration unless
    // they happened to have another wake source armed already.
    if ((__get_PRIMASK() & 1U) != 0U) {
        __asm volatile("nop");
    }
    return;
#else
    // The default balanced profile must not block here. Ordinary sketches and
    // peripheral examples rely on loop() progressing even when they have not
    // armed an explicit wake source.
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

extern "C" void __attribute__((weak)) suspendLoop(void) {
    g_loop_suspended = true;
}

extern "C" void __attribute__((weak)) resumeLoop(void) {
    g_loop_suspended = false;
}

extern "C" uint32_t __attribute__((weak)) getFreeHeap(void) {
    return static_cast<uint32_t>(nrf54l15_heap_free_bytes());
}

extern "C" uint32_t __attribute__((weak)) getFreeHeapSize(void) {
    return getFreeHeap();
}

extern "C" int __attribute__((weak)) dbgHeapTotal(void) {
    return static_cast<int>(nrf54l15_heap_total_bytes());
}

extern "C" int __attribute__((weak)) dbgHeapUsed(void) {
    return static_cast<int>(nrf54l15_heap_used_bytes());
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
        if (!g_loop_suspended && loop != nullptr) {
            loop();
        }
        Scheduler.run();
        yield();
    }

    return 0;
}
