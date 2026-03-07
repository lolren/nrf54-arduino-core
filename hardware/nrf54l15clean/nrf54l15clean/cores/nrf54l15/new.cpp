#include <cstddef>
#include <cstdlib>

void* operator new(std::size_t size) {
    if (size == 0U) {
        size = 1U;
    }
    return std::malloc(size);
}

void* operator new[](std::size_t size) {
    if (size == 0U) {
        size = 1U;
    }
    return std::malloc(size);
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

extern "C" void __cxa_pure_virtual(void) {
    while (true) {
        __asm volatile("wfi");
    }
}
