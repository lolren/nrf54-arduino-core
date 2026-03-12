#include "SPI.h"

#include <cstring>

namespace {

#if !defined(NRF54L15_CLEAN_AUTO_GATE_IDLE_US)
#define NRF54L15_CLEAN_AUTO_GATE_IDLE_US 2000UL
#endif

static constexpr uint32_t SPIM_TASKS_START      = 0x000UL;
static constexpr uint32_t SPIM_TASKS_STOP       = 0x004UL;
static constexpr uint32_t SPIM_EVENTS_STARTED   = 0x100UL;
static constexpr uint32_t SPIM_EVENTS_STOPPED   = 0x104UL;
static constexpr uint32_t SPIM_EVENTS_END       = 0x108UL;
static constexpr uint32_t SPIM_EVENTS_DMA_RX_BUSERROR = 0x154UL;
static constexpr uint32_t SPIM_EVENTS_DMA_TX_BUSERROR = 0x170UL;

static constexpr uint32_t SPIM_ENABLE           = 0x500UL;
static constexpr uint32_t SPIM_PRESCALER        = 0x52CUL;
static constexpr uint32_t SPIM_CONFIG           = 0x554UL;
static constexpr uint32_t SPIM_ORC              = 0x5C0UL;

static constexpr uint32_t SPIM_PSEL_SCK         = 0x600UL;
static constexpr uint32_t SPIM_PSEL_MOSI        = 0x604UL;
static constexpr uint32_t SPIM_PSEL_MISO        = 0x608UL;
static constexpr uint32_t SPIM_PSEL_CSN         = 0x610UL;

static constexpr uint32_t SPIM_DMA_RX_PTR       = 0x704UL;
static constexpr uint32_t SPIM_DMA_RX_MAXCNT    = 0x708UL;
static constexpr uint32_t SPIM_DMA_TX_PTR       = 0x73CUL;
static constexpr uint32_t SPIM_DMA_TX_MAXCNT    = 0x740UL;

static constexpr uint32_t SPIM_ENABLE_DISABLED  = 0UL;
static constexpr uint32_t SPIM_ENABLE_ENABLED   = 7UL;

static constexpr uint32_t SPIM_CONFIG_ORDER_LSB_FIRST = 1UL << 0;
static constexpr uint32_t SPIM_CONFIG_CPHA_TRAILING   = 1UL << 1;
static constexpr uint32_t SPIM_CONFIG_CPOL_ACTIVE_LOW = 1UL << 2;

static constexpr uint32_t PSEL_DISCONNECTED = 0xFFFFFFFFUL;
static constexpr size_t SPI_DMA_CHUNK_BYTES = 64U;

static inline volatile uint32_t& reg32(uintptr_t addr) {
    return *reinterpret_cast<volatile uint32_t*>(addr);
}

static bool wait_event(uintptr_t base, uint32_t event_off, uint32_t spin) {
    while (spin-- > 0U) {
        if (reg32(base + event_off) != 0U) {
            return true;
        }
    }
    return false;
}

static inline uint32_t make_psel(uint8_t port, uint8_t pin) {
    return (static_cast<uint32_t>(pin) & 0x1FU) |
           ((static_cast<uint32_t>(port) & 0x7UL) << 5U);
}

static bool decode_pin(uint8_t pin, uint8_t* port, uint8_t* p) {
    if (port == nullptr || p == nullptr) {
        return false;
    }
    switch (pin) {
        case PIN_D0: *port = 1; *p = 4; return true;
        case PIN_D1: *port = 1; *p = 5; return true;
        case PIN_D2: *port = 1; *p = 6; return true;
        case PIN_D3: *port = 1; *p = 7; return true;
        case PIN_D4: *port = 1; *p = 10; return true;
        case PIN_D5: *port = 1; *p = 11; return true;
        case PIN_D6: *port = 2; *p = 8; return true;
        case PIN_D7: *port = 2; *p = 7; return true;
        case PIN_D8: *port = 2; *p = 1; return true;
        case PIN_D9: *port = 2; *p = 4; return true;
        case PIN_D10: *port = 2; *p = 2; return true;
        case PIN_D11: *port = 0; *p = 3; return true;
        case PIN_D12: *port = 0; *p = 4; return true;
        case PIN_D13: *port = 2; *p = 10; return true;
        case PIN_D14: *port = 2; *p = 9; return true;
        case PIN_D15: *port = 2; *p = 6; return true;
        default: return false;
    }
}

static uint32_t compute_prescaler(uint32_t target_hz) {
    const uint32_t core_hz = F_CPU;
    if (target_hz == 0U) {
        target_hz = 1000000U;
    }

    uint32_t divisor = core_hz / target_hz;
    if ((core_hz % target_hz) != 0U) {
        ++divisor;
    }
    if (divisor < 2U) {
        divisor = 2U;
    }
    if ((divisor & 1U) != 0U) {
        ++divisor;
    }
    if (divisor > 126U) {
        divisor = 126U;
    }
    return divisor;
}

}  // namespace

SPIClass SPI(NRF_SPIM00, PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCK, PIN_SPI_SS);

SPIClass::SPIClass(NRF_SPIM_Type* spim, uint8_t mosi, uint8_t miso, uint8_t sck, uint8_t cs)
    : _spim(spim), _mosi(mosi), _miso(miso), _sck(sck), _cs(cs), _settings(),
      _initialized(false), _inTransaction(false), _lastActivityUs(0U) {}

void SPIClass::begin() {
    if (_spim == nullptr) {
        return;
    }

    configurePins();

    uint8_t sckPort = 0, sckPin = 0, mosiPort = 0, mosiPin = 0, misoPort = 0, misoPin = 0;
    if (!decode_pin(_sck, &sckPort, &sckPin) ||
        !decode_pin(_mosi, &mosiPort, &mosiPin) ||
        !decode_pin(_miso, &misoPort, &misoPin)) {
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_spim);

    reg32(base + SPIM_ENABLE) = SPIM_ENABLE_DISABLED;
    reg32(base + SPIM_PSEL_SCK) = make_psel(sckPort, sckPin);
    reg32(base + SPIM_PSEL_MOSI) = make_psel(mosiPort, mosiPin);
    reg32(base + SPIM_PSEL_MISO) = make_psel(misoPort, misoPin);
    reg32(base + SPIM_PSEL_CSN) = PSEL_DISCONNECTED;
    reg32(base + SPIM_ORC) = 0xFFU;

    applySettings();

    reg32(base + SPIM_ENABLE) = SPIM_ENABLE_ENABLED;

    _initialized = true;
    _inTransaction = false;
    _lastActivityUs = micros();
}

void SPIClass::begin(uint8_t csPin) {
    _cs = csPin;
    begin();
}

bool SPIClass::setPins(int8_t sck, int8_t miso, int8_t mosi, int8_t ss) {
    if (_spim == nullptr) {
        return false;
    }

    const uint8_t nextSck = (sck >= 0) ? static_cast<uint8_t>(sck) : _sck;
    const uint8_t nextMiso = (miso >= 0) ? static_cast<uint8_t>(miso) : _miso;
    const uint8_t nextMosi = (mosi >= 0) ? static_cast<uint8_t>(mosi) : _mosi;
    const uint8_t nextSs = (ss >= 0) ? static_cast<uint8_t>(ss) : _cs;

    uint8_t sckPort = 0;
    uint8_t sckPin = 0;
    uint8_t misoPort = 0;
    uint8_t misoPin = 0;
    uint8_t mosiPort = 0;
    uint8_t mosiPin = 0;
    if (!decode_pin(nextSck, &sckPort, &sckPin) ||
        !decode_pin(nextMiso, &misoPort, &misoPin) ||
        !decode_pin(nextMosi, &mosiPort, &mosiPin)) {
        return false;
    }

    const bool wasInitialized = _initialized;
    if (wasInitialized) {
        end();
    }

    _sck = nextSck;
    _miso = nextMiso;
    _mosi = nextMosi;
    _cs = nextSs;

    if (!wasInitialized) {
        return true;
    }

    begin();
    return _initialized;
}

void SPIClass::end() {
    if (_spim == nullptr) {
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_spim);
    reg32(base + SPIM_ENABLE) = SPIM_ENABLE_DISABLED;
    reg32(base + SPIM_PSEL_SCK) = PSEL_DISCONNECTED;
    reg32(base + SPIM_PSEL_MOSI) = PSEL_DISCONNECTED;
    reg32(base + SPIM_PSEL_MISO) = PSEL_DISCONNECTED;
    reg32(base + SPIM_PSEL_CSN) = PSEL_DISCONNECTED;

    _inTransaction = false;
    _initialized = false;
    _lastActivityUs = micros();
}

void SPIClass::beginTransaction(SPISettings settings) {
    if (!_initialized) {
        begin();
    }
    _settings = settings;
    applySettings();
    _inTransaction = true;
    _lastActivityUs = micros();
}

void SPIClass::endTransaction(void) {
    _inTransaction = false;
    _lastActivityUs = micros();
}

uint8_t SPIClass::transfer(uint8_t data) {
    uint8_t rx = 0U;
    transfer(&data, &rx, 1U);
    return rx;
}

uint16_t SPIClass::transfer16(uint16_t data) {
    uint8_t tx[2] = {
        static_cast<uint8_t>((data >> 8U) & 0xFFU),
        static_cast<uint8_t>(data & 0xFFU),
    };
    uint8_t rx[2] = {0U, 0U};

    if (_settings.bitOrder() == LSBFIRST) {
        tx[0] = static_cast<uint8_t>(data & 0xFFU);
        tx[1] = static_cast<uint8_t>((data >> 8U) & 0xFFU);
    }

    transfer(tx, rx, sizeof(tx));

    if (_settings.bitOrder() == LSBFIRST) {
        return static_cast<uint16_t>((static_cast<uint16_t>(rx[1]) << 8U) | rx[0]);
    }
    return static_cast<uint16_t>((static_cast<uint16_t>(rx[0]) << 8U) | rx[1]);
}

void SPIClass::transfer(void* buf, size_t count) {
    transfer(buf, buf, count);
}

void SPIClass::transfer(const void* tx_buf, void* rx_buf, size_t count) {
    if (!_initialized || _spim == nullptr || count == 0U) {
        return;
    }

    const bool autoTransaction = !_inTransaction;
    if (autoTransaction) {
        beginTransaction(_settings);
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_spim);
    const uint8_t* txSrc = static_cast<const uint8_t*>(tx_buf);
    uint8_t* rxDst = static_cast<uint8_t*>(rx_buf);

    // nRF54 EasyDMA requires RAM/word-aligned pointers. Stage every transfer
    // through aligned RAM so single-byte and const/flash-backed buffers work.
    alignas(4) uint8_t txScratch[SPI_DMA_CHUNK_BYTES];
    alignas(4) uint8_t rxScratch[SPI_DMA_CHUNK_BYTES];

    size_t transferred = 0U;
    while (transferred < count) {
        const size_t remaining = count - transferred;
        const uint32_t chunk = static_cast<uint32_t>(
            (remaining > SPI_DMA_CHUNK_BYTES) ? SPI_DMA_CHUNK_BYTES : remaining);
        const bool hasRx = (rxDst != nullptr);

        if (txSrc != nullptr) {
            std::memcpy(txScratch, txSrc + transferred, chunk);
        } else {
            std::memset(txScratch, 0xFF, chunk);
        }

        reg32(base + SPIM_EVENTS_END) = 0U;
        reg32(base + SPIM_EVENTS_STOPPED) = 0U;
        reg32(base + SPIM_EVENTS_DMA_RX_BUSERROR) = 0U;
        reg32(base + SPIM_EVENTS_DMA_TX_BUSERROR) = 0U;

        reg32(base + SPIM_DMA_TX_PTR) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(txScratch));
        reg32(base + SPIM_DMA_TX_MAXCNT) = chunk;
        reg32(base + SPIM_DMA_RX_PTR) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rxScratch));
        reg32(base + SPIM_DMA_RX_MAXCNT) = hasRx ? chunk : 0U;

        reg32(base + SPIM_TASKS_START) = 1U;
        const bool endOk = wait_event(base, SPIM_EVENTS_END, 2000000UL);

        reg32(base + SPIM_TASKS_STOP) = 1U;
        const bool stopOk = wait_event(base, SPIM_EVENTS_STOPPED, 2000000UL);

        if (!endOk ||
            !stopOk ||
            reg32(base + SPIM_EVENTS_DMA_RX_BUSERROR) != 0U ||
            reg32(base + SPIM_EVENTS_DMA_TX_BUSERROR) != 0U) {
            break;
        }

        if (hasRx) {
            std::memcpy(rxDst + transferred, rxScratch, chunk);
        }

        transferred += chunk;
    }

    if (autoTransaction) {
        endTransaction();
    }
    _lastActivityUs = micros();
}

void SPIClass::setBitOrder(uint8_t order) {
    _settings = SPISettings(_settings.clock(), order, _settings.dataMode());
    if (_initialized) {
        applySettings();
    }
}

void SPIClass::setDataMode(uint8_t mode) {
    _settings = SPISettings(_settings.clock(), _settings.bitOrder(), mode);
    if (_initialized) {
        applySettings();
    }
}

void SPIClass::setClockDivider(uint32_t div) {
    uint32_t clock = 1000000UL;
    if (div == 0U) {
        clock = _settings.clock();
    } else if (div >= 100000UL) {
        clock = div;
    } else {
        clock = static_cast<uint32_t>(F_CPU / div);
    }

    _settings = SPISettings(clock, _settings.bitOrder(), _settings.dataMode());
    if (_initialized) {
        applySettings();
    }
}

void SPIClass::usingInterrupt(int interruptNumber) {
    (void)interruptNumber;
}

void SPIClass::notUsingInterrupt(int interruptNumber) {
    (void)interruptNumber;
}

void SPIClass::attachInterrupt() {}
void SPIClass::detachInterrupt() {}

void SPIClass::serviceAutoGate() {
#if defined(NRF54L15_CLEAN_AUTO_GATE) && (NRF54L15_CLEAN_AUTO_GATE != 0)
    if (!_initialized || _inTransaction) {
        return;
    }

    const uint32_t idleUs = static_cast<uint32_t>(NRF54L15_CLEAN_AUTO_GATE_IDLE_US);
    const uint32_t nowUs = micros();
    if ((nowUs - _lastActivityUs) >= idleUs) {
        end();
    }
#endif
}

void SPIClass::configurePins() {
    pinMode(_sck, OUTPUT);
    pinMode(_mosi, OUTPUT);
    pinMode(_miso, INPUT);
}

void SPIClass::applySettings() {
    if (_spim == nullptr) {
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_spim);

    reg32(base + SPIM_PRESCALER) = compute_prescaler(_settings.clock());

    uint32_t cfg = 0U;
    if (_settings.bitOrder() == LSBFIRST) {
        cfg |= SPIM_CONFIG_ORDER_LSB_FIRST;
    }

    const uint8_t mode = _settings.dataMode();
    if (mode == SPI_MODE1 || mode == SPI_MODE3) {
        cfg |= SPIM_CONFIG_CPHA_TRAILING;
    }
    if (mode == SPI_MODE2 || mode == SPI_MODE3) {
        cfg |= SPIM_CONFIG_CPOL_ACTIVE_LOW;
    }

    reg32(base + SPIM_CONFIG) = cfg;
}

uint32_t SPIClass::getFrequencyValue(uint32_t clockHz) {
    return clockHz;
}
