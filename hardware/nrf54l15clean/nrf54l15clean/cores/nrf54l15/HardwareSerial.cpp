#include "HardwareSerial.h"

#include "Arduino.h"

#include <string.h>

namespace {

static constexpr uint32_t kPselDisconnected = 0xFFFFFFFFUL;
static constexpr uint32_t kRxFrameTimeoutBits = 32UL;

// UARTE register offsets.
static constexpr uint32_t U_TASKS_FLUSHRX      = 0x01CUL;
static constexpr uint32_t U_TASKS_DMA_RX_START = 0x028UL;
static constexpr uint32_t U_TASKS_DMA_RX_STOP  = 0x02CUL;
static constexpr uint32_t U_TASKS_DMA_TX_START = 0x050UL;
static constexpr uint32_t U_TASKS_DMA_TX_STOP  = 0x054UL;

static constexpr uint32_t U_EVENTS_ERROR       = 0x114UL;
static constexpr uint32_t U_EVENTS_RXTO        = 0x124UL;
static constexpr uint32_t U_EVENTS_TXSTOPPED   = 0x130UL;
static constexpr uint32_t U_EVENTS_DMA_RX_END  = 0x14CUL;
static constexpr uint32_t U_EVENTS_DMA_RX_READY = 0x150UL;
static constexpr uint32_t U_EVENTS_DMA_TX_END  = 0x168UL;
static constexpr uint32_t U_EVENTS_DMA_TX_BUSERROR = 0x170UL;
static constexpr uint32_t U_EVENTS_FRAMETIMEOUT = 0x174UL;

static constexpr uint32_t U_SHORTS             = 0x200UL;
static constexpr uint32_t U_INTENSET           = 0x304UL;
static constexpr uint32_t U_INTENCLR           = 0x308UL;

static constexpr uint32_t U_ERRORSRC           = 0x480UL;
static constexpr uint32_t U_ENABLE             = 0x500UL;
static constexpr uint32_t U_BAUDRATE           = 0x524UL;
static constexpr uint32_t U_CONFIG             = 0x56CUL;
static constexpr uint32_t U_ADDRESS            = 0x574UL;
static constexpr uint32_t U_FRAMETIMEOUT       = 0x578UL;

static constexpr uint32_t U_PSEL_TXD           = 0x604UL;
static constexpr uint32_t U_PSEL_CTS           = 0x608UL;
static constexpr uint32_t U_PSEL_RXD           = 0x60CUL;
static constexpr uint32_t U_PSEL_RTS           = 0x610UL;

static constexpr uint32_t U_DMA_RX_PTR         = 0x704UL;
static constexpr uint32_t U_DMA_RX_MAXCNT      = 0x708UL;
static constexpr uint32_t U_DMA_RX_AMOUNT      = 0x70CUL;
static constexpr uint32_t U_DMA_TX_PTR         = 0x73CUL;
static constexpr uint32_t U_DMA_TX_MAXCNT      = 0x740UL;

static inline volatile uint32_t& reg32(uintptr_t addr) {
    return *reinterpret_cast<volatile uint32_t*>(addr);
}

static inline uint32_t make_psel(uint8_t port, uint8_t pin) {
    return (static_cast<uint32_t>(pin) & 0x1FU) |
           ((static_cast<uint32_t>(port) & 0x7UL) << 5U);
}

static bool decode_pin(uint8_t pin, uint8_t* port, uint8_t* p) {
    return pinToPortPin(pin, port, p);
}

static NRF_GPIO_Type* gpio_for_port(uint8_t port) {
    switch (port) {
        case 0: return NRF_P0;
        case 1: return NRF_P1;
        case 2: return NRF_P2;
        default: return nullptr;
    }
}

static void configure_pin_output(uint8_t port, uint8_t pin, bool high) {
    NRF_GPIO_Type* gpio = gpio_for_port(port);
    if (gpio == nullptr) {
        return;
    }
    const uint32_t bit = (1UL << pin);
    gpio->DIRSET = bit;
    if (high) {
        gpio->OUTSET = bit;
    } else {
        gpio->OUTCLR = bit;
    }
}

static void configure_pin_input(uint8_t port, uint8_t pin, bool pullup) {
    NRF_GPIO_Type* gpio = gpio_for_port(port);
    if (gpio == nullptr) {
        return;
    }
    const uint32_t bit = (1UL << pin);
    gpio->DIRCLR = bit;

    uint32_t cnf = gpio->PIN_CNF[pin];
    cnf &= ~(GPIO_PIN_CNF_DIR_Msk | GPIO_PIN_CNF_INPUT_Msk | GPIO_PIN_CNF_PULL_Msk);
    cnf |= (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos);
    cnf |= GPIO_PIN_CNF_INPUT_Connect;
    cnf |= (pullup ? GPIO_PIN_CNF_PULL_Pullup : GPIO_PIN_CNF_PULL_Disabled);
    gpio->PIN_CNF[pin] = cnf;
}

static uint32_t baud_to_reg(unsigned long baud) {
    if (baud >= 1000000UL) {
        return UARTE_BAUDRATE_BAUDRATE_Baud1M;
    }
    if (baud >= 115200UL) {
        return UARTE_BAUDRATE_BAUDRATE_Baud115200;
    }
    if (baud >= 57600UL) {
        return UARTE_BAUDRATE_BAUDRATE_Baud57600;
    }
    if (baud >= 38400UL) {
        return UARTE_BAUDRATE_BAUDRATE_Baud38400;
    }
    if (baud >= 19200UL) {
        return UARTE_BAUDRATE_BAUDRATE_Baud19200;
    }
    return UARTE_BAUDRATE_BAUDRATE_Baud9600;
}

struct UarteFormat {
    uint32_t configReg;
    uint8_t dataMask;
};

static UarteFormat decode_serial_format(uint16_t config) {
    UarteFormat fmt{};
    // Preserve a valid baseline. On nRF54, CONFIG also contains FRAMESIZE and
    // other fields; leaving FRAMESIZE at 0 produces undefined behavior.
    fmt.configReg = (UARTE_CONFIG_HWFC_Disabled << UARTE_CONFIG_HWFC_Pos) |
                    (UARTE_CONFIG_FRAMESIZE_8bit << UARTE_CONFIG_FRAMESIZE_Pos);
    fmt.dataMask = 0xFFU;

    const uint8_t dataBits = static_cast<uint8_t>(5U + ((config >> 1U) & 0x3U));
    if (dataBits < 8U) {
        fmt.dataMask = static_cast<uint8_t>((1U << dataBits) - 1U);
    }

    const uint8_t paritySel = static_cast<uint8_t>(config & 0x30U);
    if (paritySel == 0x20U || paritySel == 0x30U) {
        fmt.configReg |= (UARTE_CONFIG_PARITY_Included << UARTE_CONFIG_PARITY_Pos);
        fmt.configReg |= ((paritySel == 0x30U ? UARTE_CONFIG_PARITYTYPE_Odd
                                              : UARTE_CONFIG_PARITYTYPE_Even)
                          << UARTE_CONFIG_PARITYTYPE_Pos);
    } else {
        fmt.configReg |= (UARTE_CONFIG_PARITY_Excluded << UARTE_CONFIG_PARITY_Pos);
    }

    fmt.configReg |= (((config & 0x08U) != 0U ? UARTE_CONFIG_STOP_Two
                                               : UARTE_CONFIG_STOP_One)
                      << UARTE_CONFIG_STOP_Pos);

    return fmt;
}

static bool wait_event_timeout_us(uintptr_t base, uint32_t event_off, uint32_t timeout_us) {
    const unsigned long start = micros();
    while ((unsigned long)(micros() - start) < timeout_us) {
        if (reg32(base + event_off) != 0U) {
            return true;
        }
    }
    return reg32(base + event_off) != 0U;
}

static uint32_t serial_byte_timeout_us(unsigned long baud, uint32_t bytes) {
    if (baud == 0UL) {
        baud = 9600UL;
    }
    if (bytes == 0U) {
        bytes = 1U;
    }
    // 12 bits/byte includes start/data/parity/stop margin.
    const uint32_t per_byte = static_cast<uint32_t>((12000000ULL + baud - 1ULL) / baud);
    // Keep this small: Serial.available()/read() should not block for milliseconds.
    // Long blocking delays will starve the cooperative BLE stack and cause 0x08
    // supervision timeouts under load.
    // Tuned compromise: keep polling responsive while giving RX enough dwell time
    // to catch the first byte of a burst when called periodically.
    const uint32_t margin = 500UL;
    return (per_byte * bytes) + margin;
}

static constexpr uint32_t kUarteRxInterruptMask =
    UARTE_INTENCLR_ERROR_Msk |
    UARTE_INTENCLR_RXTO_Msk |
    UARTE_INTENCLR_FRAMETIMEOUT_Msk |
    UARTE_INTENCLR_DMARXEND_Msk |
    UARTE_INTENCLR_DMARXREADY_Msk;
static constexpr uint32_t kUarteTxInterruptMask =
    UARTE_INTENCLR_TXSTOPPED_Msk |
    UARTE_INTENCLR_DMATXBUSERROR_Msk;
static uint8_t g_ownedConstlatUsers = 0U;

extern "C" void nrf54l15_wire_handle_shared_irq(const NRF_TWIM_Type* twim);

static bool uart_try_irqn_for_instance(const NRF_UARTE_Type* uart, IRQn_Type* irqn) {
    if (irqn == nullptr || uart == nullptr) {
        return false;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(uart);
    if (base == reinterpret_cast<uintptr_t>(NRF_UARTE00)) {
        *irqn = SPIM00_IRQn;
        return true;
    }
    if (base == reinterpret_cast<uintptr_t>(NRF_UARTE20)) {
        *irqn = SPIM20_IRQn;
        return true;
    }
    if (base == reinterpret_cast<uintptr_t>(NRF_UARTE21)) {
        *irqn = SPIM21_IRQn;
        return true;
    }
    if (base == reinterpret_cast<uintptr_t>(NRF_UARTE22)) {
        *irqn = SPIM22_IRQn;
        return true;
    }
    if (base == reinterpret_cast<uintptr_t>(NRF_UARTE30)) {
        *irqn = SPIM30_IRQn;
        return true;
    }
    return false;
}

static HardwareSerial* g_uarte00Owner = nullptr;
static HardwareSerial* g_uarte20Owner = nullptr;
static HardwareSerial* g_uarte21Owner = nullptr;
static HardwareSerial* g_uarte22Owner = nullptr;
static HardwareSerial* g_uarte30Owner = nullptr;
static HardwareSerial* g_unknownOwner = nullptr;

static HardwareSerial*& uart_owner_slot(const NRF_UARTE_Type* uart) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(uart);
    if (base == reinterpret_cast<uintptr_t>(NRF_UARTE00)) {
        return g_uarte00Owner;
    }
    if (base == reinterpret_cast<uintptr_t>(NRF_UARTE20)) {
        return g_uarte20Owner;
    }
    if (base == reinterpret_cast<uintptr_t>(NRF_UARTE21)) {
        return g_uarte21Owner;
    }
    if (base == reinterpret_cast<uintptr_t>(NRF_UARTE22)) {
        return g_uarte22Owner;
    }
    if (base == reinterpret_cast<uintptr_t>(NRF_UARTE30)) {
        return g_uarte30Owner;
    }
    return g_unknownOwner;
}

}  // namespace

extern "C" void SPIM00_IRQHandler(void) {
    if (g_uarte00Owner != nullptr) {
        g_uarte00Owner->handleIrq();
    }
}

extern "C" void SPIM20_IRQHandler(void) {
    if (g_uarte20Owner != nullptr) {
        g_uarte20Owner->handleIrq();
    }
}

extern "C" void SPIM21_IRQHandler(void) {
    if (g_uarte21Owner != nullptr) {
        g_uarte21Owner->handleIrq();
    }
}

extern "C" void SPIM22_IRQHandler(void) {
    if (g_uarte22Owner != nullptr) {
        g_uarte22Owner->handleIrq();
    }
    nrf54l15_wire_handle_shared_irq(NRF_TWIM22);
}

extern "C" void SPIM30_IRQHandler(void) {
    if (g_uarte30Owner != nullptr) {
        g_uarte30Owner->handleIrq();
    }
    nrf54l15_wire_handle_shared_irq(NRF_TWIM30);
}

HardwareSerial::HardwareSerial(NRF_UARTE_Type* uart, uint8_t txPin, uint8_t rxPin)
    : _uart(uart),
      _txPin(txPin),
      _rxPin(rxPin),
      _configured(false),
      _constlatOwned(false),
      _baud(9600UL),
      _config(0U),
      _rxHead(0U),
      _rxTail(0U),
      _rxCount(0U),
      _rxDropped(0U),
      _rxDmaBuffer{{0U}, {0U}},
      _rxDmaActive(0U),
      _rxDmaPrepared(1U),
      _rxDmaRunning(false),
      _rxDmaObservedAmount(0U),
      _rxDmaLastActivityUs(0U),
      _txHead(0U),
      _txTail(0U),
      _txCount(0U),
      _txDmaCount(0U),
      _txDmaRunning(false),
      _txBlockingActive(false),
      _txBlockingDone(false),
      _txBlockingFailed(false),
      _txBuffer{0},
      _dataMask(0xFFU),
      _txRing{0},
      _rxRing{0} {}

void HardwareSerial::commitRxBytes(const uint8_t* data, uint32_t amount) {
    if (data == nullptr || amount == 0U) {
        return;
    }
    if (amount > kRxDmaChunkSize) {
        amount = kRxDmaChunkSize;
    }

    for (uint32_t i = 0; i < amount; ++i) {
        const uint8_t value = data[i];
        if (_rxCount >= kRxRingSize) {
            ++_rxDropped;
        } else {
            _rxRing[_rxHead] = value;
            _rxHead = static_cast<uint16_t>(
                (_rxHead + 1U) & static_cast<uint16_t>(kRxRingSize - 1U));
            ++_rxCount;
        }
    }
}

void HardwareSerial::begin(unsigned long baud) {
    begin(baud, SERIAL_8N1);
}

void HardwareSerial::begin(unsigned long baud, uint16_t config) {
    if (_uart == nullptr) {
        return;
    }
    if (_configured) {
        end();
    }

    uint8_t txPort = 0, tx = 0, rxPort = 0, rx = 0;
    if (!decode_pin(_txPin, &txPort, &tx) || !decode_pin(_rxPin, &rxPort, &rx)) {
        return;
    }

    configure_pin_output(txPort, tx, true);
    configure_pin_input(rxPort, rx, true);
    requestConstlatIfNeeded();

    const uintptr_t base = reinterpret_cast<uintptr_t>(_uart);

    reg32(base + U_ENABLE) = UARTE_ENABLE_ENABLE_Disabled;
    reg32(base + U_PSEL_TXD) = make_psel(txPort, tx);
    reg32(base + U_PSEL_RXD) = make_psel(rxPort, rx);
    reg32(base + U_PSEL_CTS) = kPselDisconnected;
    reg32(base + U_PSEL_RTS) = kPselDisconnected;
    const UarteFormat fmt = decode_serial_format(config);
    _dataMask = fmt.dataMask;
    reg32(base + U_CONFIG) = fmt.configReg |
                             (UARTE_CONFIG_FRAMETIMEOUT_Enabled << UARTE_CONFIG_FRAMETIMEOUT_Pos);
    reg32(base + U_BAUDRATE) = baud_to_reg(baud);
    reg32(base + U_ADDRESS) = 0U;
    reg32(base + U_FRAMETIMEOUT) = kRxFrameTimeoutBits;
    reg32(base + U_SHORTS) |= UARTE_SHORTS_DMA_TX_END_DMA_TX_STOP_Msk;
    reg32(base + U_ENABLE) = UARTE_ENABLE_ENABLE_Enabled;

    _baud = baud;
    _config = config;
    _rxHead = 0U;
    _rxTail = 0U;
    _rxCount = 0U;
    _rxDropped = 0U;
    _rxDmaActive = 0U;
    _rxDmaPrepared = 1U;
    _rxDmaRunning = false;
    _rxDmaObservedAmount = 0U;
    _rxDmaLastActivityUs = 0U;
    _txHead = 0U;
    _txTail = 0U;
    _txCount = 0U;
    _txDmaCount = 0U;
    _txDmaRunning = false;
    _txBlockingActive = false;
    _txBlockingDone = false;
    _txBlockingFailed = false;
    _configured = true;

    IRQn_Type irqn = Reset_IRQn;
    if (uart_try_irqn_for_instance(_uart, &irqn)) {
        HardwareSerial*& owner = uart_owner_slot(_uart);
        owner = this;
        NVIC_DisableIRQ(irqn);
        NVIC_ClearPendingIRQ(irqn);
        NVIC_SetPriority(irqn, 2U);
        NVIC_EnableIRQ(irqn);
    }
    reg32(base + U_EVENTS_DMA_TX_END) = 0U;
    reg32(base + U_EVENTS_DMA_TX_BUSERROR) = 0U;
    reg32(base + U_EVENTS_TXSTOPPED) = 0U;
    reg32(base + U_INTENCLR) = kUarteTxInterruptMask;
    if (!usesPins(PIN_SAMD11_RX, PIN_SAMD11_TX)) {
        reg32(base + U_INTENSET) = kUarteTxInterruptMask;
    }
    startRxDma();
}

void HardwareSerial::end() {
    if (_uart == nullptr) {
        return;
    }

    stopRxDma();

    const uintptr_t base = reinterpret_cast<uintptr_t>(_uart);
    reg32(base + U_INTENCLR) = kUarteRxInterruptMask | kUarteTxInterruptMask;
    reg32(base + U_SHORTS) &= ~UARTE_SHORTS_DMA_TX_END_DMA_TX_STOP_Msk;
    reg32(base + U_TASKS_DMA_TX_STOP) = UARTE_TASKS_DMA_TX_STOP_STOP_Trigger;
    reg32(base + U_ENABLE) = UARTE_ENABLE_ENABLE_Disabled;

    reg32(base + U_PSEL_TXD) = kPselDisconnected;
    reg32(base + U_PSEL_RXD) = kPselDisconnected;
    reg32(base + U_PSEL_CTS) = kPselDisconnected;
    reg32(base + U_PSEL_RTS) = kPselDisconnected;

    IRQn_Type irqn = Reset_IRQn;
    if (uart_try_irqn_for_instance(_uart, &irqn)) {
        HardwareSerial*& owner = uart_owner_slot(_uart);
        if (owner == this) {
            NVIC_DisableIRQ(irqn);
            NVIC_ClearPendingIRQ(irqn);
            owner = nullptr;
        }
    }

    _configured = false;
    _rxHead = 0U;
    _rxTail = 0U;
    _rxCount = 0U;
    _txHead = 0U;
    _txTail = 0U;
    _txCount = 0U;
    _txDmaCount = 0U;
    _txDmaRunning = false;
    _txBlockingActive = false;
    _txBlockingDone = false;
    _txBlockingFailed = false;
    releaseConstlatIfNeeded();
}

bool HardwareSerial::setPins(int8_t rxPin, int8_t txPin) {
    if (_uart == nullptr) {
        return false;
    }

    const uint8_t nextRxPin = (rxPin >= 0) ? static_cast<uint8_t>(rxPin) : _rxPin;
    const uint8_t nextTxPin = (txPin >= 0) ? static_cast<uint8_t>(txPin) : _txPin;

    uint8_t txPort = 0;
    uint8_t tx = 0;
    uint8_t rxPort = 0;
    uint8_t rx = 0;
    if (!decode_pin(nextTxPin, &txPort, &tx) || !decode_pin(nextRxPin, &rxPort, &rx)) {
        return false;
    }

    const bool wasConfigured = _configured;
    const unsigned long baud = _baud;
    const uint16_t config = _config;

    if (wasConfigured) {
        end();
    }

    _txPin = nextTxPin;
    _rxPin = nextRxPin;

    if (!wasConfigured) {
        return true;
    }

    begin(baud, config);
    return _configured;
}

void HardwareSerial::startRxDma() {
    if (!_configured || _uart == nullptr) {
        return;
    }
    if (_constlatOwned) {
        NRF_POWER->TASKS_CONSTLAT = POWER_TASKS_CONSTLAT_TASKS_CONSTLAT_Trigger;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_uart);

    reg32(base + U_INTENCLR) = kUarteRxInterruptMask;
    reg32(base + U_EVENTS_DMA_RX_END) = 0U;
    reg32(base + U_EVENTS_DMA_RX_READY) = 0U;
    reg32(base + U_EVENTS_ERROR) = 0U;
    reg32(base + U_EVENTS_RXTO) = 0U;
    reg32(base + U_EVENTS_FRAMETIMEOUT) = 0U;
    reg32(base + U_ERRORSRC) = 0xFFFFFFFFUL;

    // Enable FRAMETIMEOUT→DMA_RX_STOP shortcut so the idle-line timeout causes a
    // proper DMA stop that fires DMA_RX_END with the correct byte count.
    // DMA_RX_AMOUNT is only valid after a DMA completion event; without this
    // shortcut, flushPartialRxDma would read a stale count from the previous
    // transfer and corrupt the ring buffer.
    reg32(base + U_SHORTS) |= UARTE_SHORTS_FRAMETIMEOUT_DMA_RX_STOP_Msk;

    memset(_rxDmaBuffer[_rxDmaActive], 0, HardwareSerial::kRxDmaChunkSize);
    reg32(base + U_DMA_RX_PTR) =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&_rxDmaBuffer[_rxDmaActive][0]));
    reg32(base + U_DMA_RX_MAXCNT) = HardwareSerial::kRxDmaChunkSize;
    reg32(base + U_TASKS_DMA_RX_START) = UARTE_TASKS_DMA_RX_START_START_Trigger;
    _rxDmaRunning = true;
    _rxDmaObservedAmount = 0U;
    _rxDmaLastActivityUs = micros();
    reg32(base + U_INTENSET) = kUarteRxInterruptMask;
}

void HardwareSerial::stopRxDma() {
    if (_uart == nullptr) {
        return;
    }
    if (!_rxDmaRunning) {
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_uart);
    reg32(base + U_INTENCLR) = kUarteRxInterruptMask;
    reg32(base + U_SHORTS) &= ~(UARTE_SHORTS_DMA_RX_END_DMA_RX_START_Msk |
                                UARTE_SHORTS_FRAMETIMEOUT_DMA_RX_STOP_Msk);
    reg32(base + U_EVENTS_RXTO) = 0U;
    reg32(base + U_EVENTS_FRAMETIMEOUT) = 0U;
    reg32(base + U_TASKS_DMA_RX_STOP) = UARTE_TASKS_DMA_RX_STOP_STOP_Trigger;
    wait_event_timeout_us(base, U_EVENTS_RXTO, serial_byte_timeout_us(_baud, 2U));
    reg32(base + U_ERRORSRC) = 0xFFFFFFFFUL;
    _rxDmaRunning = false;
}

void HardwareSerial::flushPartialRxDma(uintptr_t base) {
    if (!_rxDmaRunning) {
        return;
    }

    uint32_t amount = reg32(base + U_DMA_RX_AMOUNT);
    if (amount > kRxDmaChunkSize) {
        amount = kRxDmaChunkSize;
    }
    if (amount <= _rxDmaObservedAmount) {
        return;
    }

    const uint32_t delta = amount - _rxDmaObservedAmount;
    commitRxBytes(&_rxDmaBuffer[_rxDmaActive][_rxDmaObservedAmount], delta);
    _rxDmaObservedAmount = static_cast<uint8_t>(amount);
    _rxDmaLastActivityUs = micros();
}

void HardwareSerial::processRxDmaEvents() {
    if (!_configured || _uart == nullptr) {
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_uart);
    if ((reg32(base + U_EVENTS_ERROR) != 0U) || (reg32(base + U_ERRORSRC) != 0U)) {
        reg32(base + U_EVENTS_ERROR) = 0U;
        reg32(base + U_ERRORSRC) = 0xFFFFFFFFUL;
        // Per nRF54L15 PS §8.25.7, OVERRUN does not stop the UART receiver —
        // clearing the error flags is sufficient recovery.  Do NOT call
        // stopRxDma() here: its 708 µs blocking wait for RXTO lets the FIFO
        // overflow again immediately under a continuous data burst (large paste),
        // producing another OVERRUN on every restart and permanently breaking RX
        // until reboot.  Fall through: if MAXCNT was also reached simultaneously,
        // DMA_RX_END is set below and will flush the valid bytes and restart DMA.
        // If DMA is still running (< MAXCNT), it continues normally.
    }

    if (reg32(base + U_EVENTS_DMA_RX_READY) != 0U) {
        reg32(base + U_EVENTS_DMA_RX_READY) = 0U;
        // DMA_RX_AMOUNT is stale until DMA_RX_END fires; skip flush here.
    }

    if (reg32(base + U_EVENTS_FRAMETIMEOUT) != 0U) {
        reg32(base + U_EVENTS_FRAMETIMEOUT) = 0U;
        // The FRAMETIMEOUT→DMA_RX_STOP shortcut has already triggered the stop
        // task.  DMA_RX_END will fire with the correct byte count once the DMA
        // has stopped; commit bytes there, not here, to avoid reading a stale
        // DMA_RX_AMOUNT from the previous transfer.
    }

    if (reg32(base + U_EVENTS_RXTO) != 0U) {
        reg32(base + U_EVENTS_RXTO) = 0U;
        // Receiver stopped (follows DMA_RX_STOP).  DMA_RX_END carries the
        // valid count; do not flush with a potentially stale AMOUNT here.
    }

    if (reg32(base + U_EVENTS_DMA_RX_END) == 0U) {
        return;
    }

    reg32(base + U_EVENTS_DMA_RX_END) = 0U;
    flushPartialRxDma(base);
    memset(_rxDmaBuffer[_rxDmaActive], 0, kRxDmaChunkSize);
    _rxDmaRunning = false;
    _rxDmaObservedAmount = 0U;
    _rxDmaLastActivityUs = micros();
    startRxDma();
}

void HardwareSerial::serviceRxDma() {
    if (!_configured || _uart == nullptr) {
        return;
    }

    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (!_rxDmaRunning) {
        startRxDma();
        __set_PRIMASK(primask);
        return;
    }
    processRxDmaEvents();
    // Do NOT call flushPartialRxDma unconditionally here.  On nRF54L15,
    // DMA_RX_AMOUNT only reflects the last *completed* DMA transfer, not the
    // in-progress byte count.  Calling it with a stale AMOUNT would commit
    // zeros from the freshly memset buffer and pin _rxDmaObservedAmount at
    // the stale value, preventing future partial flushes.  Byte delivery is
    // handled by the DMA_RX_END path in processRxDmaEvents, which is reached
    // after the FRAMETIMEOUT→DMA_RX_STOP shortcut fires.
    __set_PRIMASK(primask);
}

void HardwareSerial::startNextTxDmaLocked(uintptr_t base) {
    if (!_configured || _uart == nullptr || _txDmaRunning || _txCount == 0U) {
        return;
    }
    if (_constlatOwned) {
        NRF_POWER->TASKS_CONSTLAT = POWER_TASKS_CONSTLAT_TASKS_CONSTLAT_Trigger;
    }

    uint16_t chunk = _txCount;
    if (chunk > kTxDmaChunkSize) {
        chunk = kTxDmaChunkSize;
    }
    if (chunk == 0U) {
        return;
    }

    uint16_t tail = _txTail;
    for (uint16_t i = 0U; i < chunk; ++i) {
        _txBuffer[i] = _txRing[tail];
        tail = static_cast<uint16_t>(
            (tail + 1U) & static_cast<uint16_t>(kTxRingSize - 1U));
    }

    reg32(base + U_EVENTS_DMA_TX_END) = 0U;
    reg32(base + U_EVENTS_DMA_TX_BUSERROR) = 0U;
    reg32(base + U_EVENTS_TXSTOPPED) = 0U;
    reg32(base + U_DMA_TX_PTR) =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&_txBuffer[0]));
    reg32(base + U_DMA_TX_MAXCNT) = chunk;
    _txDmaCount = static_cast<uint8_t>(chunk);
    _txDmaRunning = true;
    reg32(base + U_TASKS_DMA_TX_START) = UARTE_TASKS_DMA_TX_START_START_Trigger;
}

void HardwareSerial::processTxDmaEvents(uintptr_t base) {
    if (!_configured || _uart == nullptr) {
        return;
    }

    if (reg32(base + U_EVENTS_DMA_TX_END) != 0U) {
        reg32(base + U_EVENTS_DMA_TX_END) = 0U;
    }

    if (reg32(base + U_EVENTS_DMA_TX_BUSERROR) != 0U) {
        reg32(base + U_EVENTS_DMA_TX_BUSERROR) = 0U;
        reg32(base + U_TASKS_DMA_TX_STOP) = UARTE_TASKS_DMA_TX_STOP_STOP_Trigger;
        if (_txBlockingActive) {
            _txBlockingFailed = true;
            _txBlockingDone = true;
            _txBlockingActive = false;
        }
        _txDmaCount = 0U;
        _txDmaRunning = false;
    }

    if (reg32(base + U_EVENTS_TXSTOPPED) != 0U) {
        reg32(base + U_EVENTS_TXSTOPPED) = 0U;
        if (_txBlockingActive) {
            // Bridge Serial uses polling writes. RX-side IRQ activity can still
            // observe TXSTOPPED on the shared UARTE instance, so wake the
            // blocking writer instead of consuming the event as ring-TX state.
            _txBlockingDone = true;
            _txBlockingActive = false;
            return;
        }
        if (_txDmaRunning && _txDmaCount != 0U) {
            _txTail = static_cast<uint16_t>(
                (_txTail + _txDmaCount) & static_cast<uint16_t>(kTxRingSize - 1U));
            _txCount = static_cast<uint16_t>(_txCount - _txDmaCount);
            _txDmaCount = 0U;
            _txDmaRunning = false;
        }
    }

    if (!_txDmaRunning && _txCount != 0U) {
        startNextTxDmaLocked(base);
    }
}

void HardwareSerial::serviceTxDma() {
    if (!_configured || _uart == nullptr) {
        return;
    }

    if (usesPins(PIN_SAMD11_RX, PIN_SAMD11_TX)) {
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_uart);
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    processTxDmaEvents(base);
    __set_PRIMASK(primask);
}

size_t HardwareSerial::writeBlocking(const uint8_t* buffer, size_t size) {
    if (!_configured || _uart == nullptr) {
        return 0U;
    }
    if (buffer == nullptr || size == 0U) {
        return 0U;
    }
    if (_constlatOwned) {
        NRF_POWER->TASKS_CONSTLAT = POWER_TASKS_CONSTLAT_TASKS_CONSTLAT_Trigger;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_uart);
    _txBlockingActive = true;
    _txBlockingDone = false;
    _txBlockingFailed = false;
    reg32(base + U_EVENTS_DMA_TX_END) = 0U;
    reg32(base + U_EVENTS_DMA_TX_BUSERROR) = 0U;
    reg32(base + U_EVENTS_TXSTOPPED) = 0U;
    reg32(base + U_DMA_TX_PTR) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer));
    reg32(base + U_DMA_TX_MAXCNT) = static_cast<uint32_t>(size);
    reg32(base + U_TASKS_DMA_TX_START) = UARTE_TASKS_DMA_TX_START_START_Trigger;

    const uint32_t timeoutUs =
        serial_byte_timeout_us(_baud, static_cast<uint32_t>(size)) + 1000UL;
    const unsigned long start = micros();
    while (!_txBlockingDone && !_txBlockingFailed &&
           reg32(base + U_EVENTS_DMA_TX_BUSERROR) == 0U &&
           reg32(base + U_EVENTS_TXSTOPPED) == 0U &&
           static_cast<unsigned long>(micros() - start) < timeoutUs) {
    }

    if (!_txBlockingDone && !_txBlockingFailed &&
        reg32(base + U_EVENTS_DMA_TX_BUSERROR) == 0U &&
        reg32(base + U_EVENTS_TXSTOPPED) == 0U) {
        _txBlockingActive = false;
        reg32(base + U_TASKS_DMA_TX_STOP) = UARTE_TASKS_DMA_TX_STOP_STOP_Trigger;
        wait_event_timeout_us(base, U_EVENTS_TXSTOPPED, 2000UL);
        return 0U;
    }

    _txBlockingActive = false;
    if (reg32(base + U_EVENTS_DMA_TX_BUSERROR) != 0U) {
        reg32(base + U_EVENTS_DMA_TX_BUSERROR) = 0U;
        return 0U;
    }
    if (_txBlockingFailed) {
        return 0U;
    }
    return size;
}

void HardwareSerial::handleIrq() {
    if (_uart != nullptr) {
        processTxDmaEvents(reinterpret_cast<uintptr_t>(_uart));
    }
    processRxDmaEvents();
}

bool HardwareSerial::usesP2Pins() const {
    uint8_t txPort = 0U;
    uint8_t tx = 0U;
    uint8_t rxPort = 0U;
    uint8_t rx = 0U;
    if (!decode_pin(_txPin, &txPort, &tx) || !decode_pin(_rxPin, &rxPort, &rx)) {
        return false;
    }
    return (txPort == 2U) || (rxPort == 2U);
}

void HardwareSerial::requestConstlatIfNeeded() {
    if (!usesP2Pins() || _constlatOwned) {
        return;
    }

    if (g_ownedConstlatUsers != 0U) {
        ++g_ownedConstlatUsers;
        _constlatOwned = true;
        return;
    }

    if ((NRF_POWER->CONSTLATSTAT & POWER_CONSTLATSTAT_STATUS_Msk) != 0U) {
        return;
    }

    NRF_POWER->TASKS_CONSTLAT = POWER_TASKS_CONSTLAT_TASKS_CONSTLAT_Trigger;
    g_ownedConstlatUsers = 1U;
    _constlatOwned = true;
}

void HardwareSerial::releaseConstlatIfNeeded() {
    if (!_constlatOwned) {
        return;
    }

    if (g_ownedConstlatUsers > 0U) {
        --g_ownedConstlatUsers;
    }
    if (g_ownedConstlatUsers == 0U) {
        NRF_POWER->TASKS_LOWPWR = POWER_TASKS_LOWPWR_TASKS_LOWPWR_Trigger;
    }
    _constlatOwned = false;
}

int HardwareSerial::available() {
    serviceRxDma();
    return static_cast<int>(_rxCount);
}

int HardwareSerial::availableForWrite() {
    serviceTxDma();
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const int free = static_cast<int>(kTxRingSize - _txCount);
    __set_PRIMASK(primask);
    return free;
}

int HardwareSerial::read() {
    serviceRxDma();
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (_rxCount == 0U) {
        __set_PRIMASK(primask);
        return -1;
    }

    const uint8_t value = _rxRing[_rxTail];
    _rxTail = static_cast<uint16_t>(
        (_rxTail + 1U) & static_cast<uint16_t>(kRxRingSize - 1U));
    --_rxCount;
    __set_PRIMASK(primask);
    return static_cast<int>(value & _dataMask);
}

int HardwareSerial::peek() {
    serviceRxDma();
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (_rxCount == 0U) {
        __set_PRIMASK(primask);
        return -1;
    }
    const int value = static_cast<int>(_rxRing[_rxTail] & _dataMask);
    __set_PRIMASK(primask);
    return value;
}

void HardwareSerial::flush() {
    if (!_configured || _uart == nullptr) {
        return;
    }

    while (true) {
        serviceTxDma();

        const uint32_t primask = __get_PRIMASK();
        __disable_irq();
        const bool idle = (_txCount == 0U) && !_txDmaRunning;
        __set_PRIMASK(primask);
        if (idle) {
            break;
        }

        if (primask == 0U) {
            yield();
        }
    }
}

size_t HardwareSerial::write(uint8_t value) {
    const uint8_t masked = static_cast<uint8_t>(value & _dataMask);
    return write(&masked, 1U);
}

size_t HardwareSerial::write(const uint8_t* buffer, size_t size) {
    if (!_configured || _uart == nullptr) {
        return 0U;
    }
    if (buffer == nullptr || size == 0U) {
        return 0U;
    }

    if (usesPins(PIN_SAMD11_RX, PIN_SAMD11_TX)) {
        size_t written = 0U;
        while (written < size) {
            size_t chunk = size - written;
            if (chunk > kTxDmaChunkSize) {
                chunk = kTxDmaChunkSize;
            }

            if (_dataMask == 0xFFU) {
                memcpy(_txBuffer, buffer + written, chunk);
            } else {
                for (size_t i = 0U; i < chunk; ++i) {
                    _txBuffer[i] = static_cast<uint8_t>(buffer[written + i] & _dataMask);
                }
            }

            const size_t sent = writeBlocking(_txBuffer, chunk);
            written += sent;
            if (sent != chunk) {
                break;
            }

            if (written < size) {
                yield();
            }
        }

        return written;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_uart);
    size_t written = 0U;
    while (written < size) {
        serviceTxDma();

        const uint32_t primask = __get_PRIMASK();
        __disable_irq();
        processTxDmaEvents(base);

        uint16_t free = static_cast<uint16_t>(kTxRingSize - _txCount);
        if (free == 0U) {
            __set_PRIMASK(primask);
            if (primask == 0U) {
                yield();
            }
            continue;
        }

        size_t chunk = size - written;
        if (chunk > free) {
            chunk = free;
        }

        if (_dataMask == 0xFFU) {
            for (size_t i = 0U; i < chunk; ++i) {
                _txRing[_txHead] = buffer[written + i];
                _txHead = static_cast<uint16_t>(
                    (_txHead + 1U) & static_cast<uint16_t>(kTxRingSize - 1U));
            }
        } else {
            for (size_t i = 0U; i < chunk; ++i) {
                _txRing[_txHead] = static_cast<uint8_t>(buffer[written + i] & _dataMask);
                _txHead = static_cast<uint16_t>(
                    (_txHead + 1U) & static_cast<uint16_t>(kTxRingSize - 1U));
            }
        }
        _txCount = static_cast<uint16_t>(_txCount + chunk);
        written += chunk;

        startNextTxDmaLocked(base);
        __set_PRIMASK(primask);

        if (written < size && primask == 0U && chunk == free) {
            yield();
        }
    }

    return written;
}

HardwareSerial::operator bool() const {
    return _configured && (_uart != nullptr);
}

bool HardwareSerial::isConfigured() const {
    return _configured && (_uart != nullptr);
}

bool HardwareSerial::usesPins(uint8_t txPin, uint8_t rxPin) const {
    return (_txPin == txPin) && (_rxPin == rxPin);
}

extern "C" void nrf54l15_serial_prepare_idle_sleep(void) {
    if (g_ownedConstlatUsers != 0U) {
        NRF_POWER->TASKS_CONSTLAT = POWER_TASKS_CONSTLAT_TASKS_CONSTLAT_Trigger;
    }
}

extern "C" uint8_t nrf54l15_constlat_users_active(void) {
    return (g_ownedConstlatUsers != 0U) ? 1U : 0U;
}

static constexpr uint8_t kSerialPinDisconnected = 0xFFU;

#if defined(NRF54L15_CLEAN_SERIAL_DISABLED)
HardwareSerial Serial(NRF_UARTE21, kSerialPinDisconnected, kSerialPinDisconnected);
HardwareSerial Serial1(NRF_UARTE20, kSerialPinDisconnected, kSerialPinDisconnected);
#elif defined(NRF54L15_CLEAN_SERIAL_ROUTE_HEADER)
#if defined(NRF54L15_CLEAN_SERIAL_ROUTE_HEADER_SWAP_INSTANCES)
HardwareSerial Serial(NRF_UARTE20, PIN_SERIAL_TX, PIN_SERIAL_RX);
HardwareSerial Serial1(NRF_UARTE21, PIN_SAMD11_RX, PIN_SAMD11_TX);
#else
HardwareSerial Serial(NRF_UARTE21, PIN_SERIAL_TX, PIN_SERIAL_RX);
HardwareSerial Serial1(NRF_UARTE20, PIN_SAMD11_RX, PIN_SAMD11_TX);
#endif
#else
HardwareSerial Serial(NRF_UARTE20, PIN_SAMD11_RX, PIN_SAMD11_TX);
HardwareSerial Serial1(NRF_UARTE21, PIN_SERIAL1_TX, PIN_SERIAL1_RX);
#endif

// Compatibility alias for sketches/libraries that expect Serial2.
HardwareSerial& Serial2 = Serial1;

extern "C" uint8_t nrf54l15_bridge_serial_active(void) {
    const bool serialBridgeActive =
        Serial.isConfigured() && Serial.usesPins(PIN_SAMD11_RX, PIN_SAMD11_TX);
    const bool serial1BridgeActive =
        Serial1.isConfigured() && Serial1.usesPins(PIN_SAMD11_RX, PIN_SAMD11_TX);
    return (serialBridgeActive || serial1BridgeActive) ? 1U : 0U;
}

extern "C" void nrf54l15_serial_idle_service(void) {
    Serial.serviceTxDma();
    Serial1.serviceTxDma();
}
