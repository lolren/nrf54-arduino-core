#include "Wire.h"

#include "cmsis.h"

#include <string.h>

namespace {

#if !defined(NRF54L15_CLEAN_AUTO_GATE_IDLE_US)
#define NRF54L15_CLEAN_AUTO_GATE_IDLE_US 2000UL
#endif

static constexpr uint32_t PSEL_DISCONNECTED = 0xFFFFFFFFUL;

// Common task/event offsets shared by TWIM/TWIS at this peripheral block.
static constexpr uint32_t T_TASKS_STOP         = 0x004UL;
static constexpr uint32_t T_TASKS_RESUME       = 0x010UL;
static constexpr uint32_t T_TASKS_PREPARERX    = 0x020UL;
static constexpr uint32_t T_TASKS_PREPARETX    = 0x024UL;
static constexpr uint32_t T_TASKS_DMA_RX_START = 0x028UL;
static constexpr uint32_t T_TASKS_DMA_TX_START = 0x050UL;

static constexpr uint32_t T_EVENTS_STOPPED     = 0x104UL;
static constexpr uint32_t T_EVENTS_ERROR       = 0x114UL;
static constexpr uint32_t T_EVENTS_LASTRX      = 0x134UL;
static constexpr uint32_t T_EVENTS_LASTTX      = 0x138UL;
static constexpr uint32_t T_EVENTS_WRITE       = 0x13CUL;
static constexpr uint32_t T_EVENTS_READ        = 0x140UL;
static constexpr uint32_t T_EVENTS_DMA_RX_END  = 0x14CUL;
static constexpr uint32_t T_EVENTS_DMA_TX_END  = 0x168UL;

// TWIM register offsets.
static constexpr uint32_t T_TWIM_ERRORSRC      = 0x4C4UL;
static constexpr uint32_t T_FREQUENCY          = 0x524UL;
static constexpr uint32_t T_ADDRESS            = 0x588UL;

// TWIS register offsets.
static constexpr uint32_t T_SHORTS             = 0x200UL;
static constexpr uint32_t T_INTENSET           = 0x304UL;
static constexpr uint32_t T_INTENCLR           = 0x308UL;
static constexpr uint32_t T_TWIS_ERRORSRC      = 0x4D0UL;
static constexpr uint32_t T_ADDRESS0           = 0x588UL;
static constexpr uint32_t T_ADDRESS1           = 0x58CUL;
static constexpr uint32_t T_CONFIG             = 0x594UL;
static constexpr uint32_t T_ORC                = 0x5C0UL;

static constexpr uint32_t T_ENABLE             = 0x500UL;

static constexpr uint32_t T_PSEL_SCL           = 0x600UL;
static constexpr uint32_t T_PSEL_SDA           = 0x604UL;

// DMA layout is shared for TWIM/TWIS at these offsets.
static constexpr uint32_t T_DMA_RX_PTR         = 0x704UL;
static constexpr uint32_t T_DMA_RX_MAXCNT      = 0x708UL;
static constexpr uint32_t T_DMA_RX_AMOUNT      = 0x70CUL;
static constexpr uint32_t T_DMA_TX_PTR         = 0x73CUL;
static constexpr uint32_t T_DMA_TX_MAXCNT      = 0x740UL;
static constexpr uint32_t T_DMA_TX_AMOUNT      = 0x744UL;

static constexpr uint32_t T_ENABLE_DISABLED    = 0UL;
static constexpr uint32_t T_ENABLE_TWIM        = 6UL;
static constexpr uint32_t T_ENABLE_TWIS        = 9UL;

static constexpr uint32_t T_TWIM_ERRORSRC_ALL  = 0x7UL;
static constexpr uint32_t T_TWIM_ERRORSRC_ANACK = 0x1UL;
static constexpr uint32_t T_TWIM_ERRORSRC_DNACK = 0x2UL;

static constexpr uint32_t T_TWIS_ERRORSRC_OVERFLOW = (1UL << 0U);
static constexpr uint32_t T_TWIS_ERRORSRC_DNACK    = (1UL << 2U);
static constexpr uint32_t T_TWIS_ERRORSRC_OVERREAD = (1UL << 3U);
static constexpr uint32_t T_TWIS_ERRORSRC_ALL =
    T_TWIS_ERRORSRC_OVERFLOW | T_TWIS_ERRORSRC_DNACK | T_TWIS_ERRORSRC_OVERREAD;

static constexpr uint32_t T_TWIS_CONFIG_ADDRESS0_ENABLE = (1UL << 0U);

static constexpr uint32_t T_TWIS_SHORT_WRITE_SUSPEND = (1UL << 13U);
static constexpr uint32_t T_TWIS_SHORT_READ_SUSPEND  = (1UL << 14U);

static constexpr uint32_t T_TWIS_INT_STOPPED = (1UL << 1U);
static constexpr uint32_t T_TWIS_INT_ERROR   = (1UL << 5U);
static constexpr uint32_t T_TWIS_INT_WRITE   = (1UL << 15U);
static constexpr uint32_t T_TWIS_INT_READ    = (1UL << 16U);
static constexpr uint32_t T_TWIS_INT_DMARXEND = (1UL << 19U);
static constexpr uint32_t T_TWIS_INT_DMATXEND = (1UL << 26U);
static constexpr uint32_t T_TWIS_INT_MASK =
    T_TWIS_INT_STOPPED |
    T_TWIS_INT_ERROR |
    T_TWIS_INT_WRITE |
    T_TWIS_INT_READ |
    T_TWIS_INT_DMARXEND |
    T_TWIS_INT_DMATXEND;

static constexpr uint8_t TWIS_DEFAULT_ORC = 0xFFU;

static inline volatile uint32_t& reg32(uintptr_t addr) {
    return *reinterpret_cast<volatile uint32_t*>(addr);
}

static inline uint32_t make_psel(uint8_t port, uint8_t pin) {
    return (static_cast<uint32_t>(pin) & 0x1FU) |
           ((static_cast<uint32_t>(port) & 0x7UL) << 5U);
}

static bool wait_event(uintptr_t base, uint32_t event_off, uint32_t spin) {
    while (spin-- > 0U) {
        if (reg32(base + event_off) != 0U) {
            return true;
        }
    }
    return false;
}

static bool wait_event_or_error(uintptr_t base, uint32_t event_off, uint32_t spin) {
    while (spin-- > 0U) {
        if (reg32(base + event_off) != 0U) {
            return true;
        }
        if (reg32(base + T_EVENTS_ERROR) != 0U) {
            return false;
        }
    }
    return false;
}

static bool wait_tx_done_or_error(uintptr_t base, uint32_t spin) {
    while (spin-- > 0U) {
        if (reg32(base + T_EVENTS_LASTTX) != 0U || reg32(base + T_EVENTS_DMA_TX_END) != 0U) {
            return true;
        }
        if (reg32(base + T_EVENTS_ERROR) != 0U) {
            return false;
        }
    }
    return false;
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

static NRF_GPIO_Type* gpio_for_port(uint8_t port) {
    switch (port) {
        case 0: return NRF_P0;
        case 1: return NRF_P1;
        case 2: return NRF_P2;
        default: return nullptr;
    }
}

static void configure_i2c_pin(uint8_t port, uint8_t pin) {
    NRF_GPIO_Type* gpio = gpio_for_port(port);
    if (gpio == nullptr) {
        return;
    }

    gpio->DIRCLR = (1UL << pin);

    uint32_t cnf = gpio->PIN_CNF[pin];
    cnf &= ~(GPIO_PIN_CNF_DIR_Msk | GPIO_PIN_CNF_INPUT_Msk |
             GPIO_PIN_CNF_PULL_Msk | GPIO_PIN_CNF_DRIVE0_Msk |
             GPIO_PIN_CNF_DRIVE1_Msk);
    cnf |= (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos);
    cnf |= GPIO_PIN_CNF_INPUT_Connect;
    cnf |= GPIO_PIN_CNF_PULL_Disabled;
    cnf |= (GPIO_PIN_CNF_DRIVE0_S0 << GPIO_PIN_CNF_DRIVE0_Pos);
    cnf |= (GPIO_PIN_CNF_DRIVE1_D1 << GPIO_PIN_CNF_DRIVE1_Pos);
    gpio->PIN_CNF[pin] = cnf;
}

static uint32_t twim_frequency_reg(uint32_t hz) {
    if (hz >= 1000000UL) {
        return TWIM_FREQUENCY_FREQUENCY_K1000;
    }
    if (hz >= 400000UL) {
        return TWIM_FREQUENCY_FREQUENCY_K400;
    }
    if (hz >= 250000UL) {
        return TWIM_FREQUENCY_FREQUENCY_K250;
    }
    return TWIM_FREQUENCY_FREQUENCY_K100;
}

static uint8_t end_tx_error_code(bool transactionOk, bool stopOk, uint32_t errorsrc) {
    if (transactionOk && stopOk && errorsrc == 0U) {
        return 0U;
    }
    if ((errorsrc & T_TWIM_ERRORSRC_ANACK) != 0U) {
        return 2U;  // NACK on address transmit.
    }
    if ((errorsrc & T_TWIM_ERRORSRC_DNACK) != 0U) {
        return 3U;  // NACK on data transmit.
    }
    return 4U;
}

static bool twi_try_irqn_for_instance(const NRF_TWIM_Type* twim, IRQn_Type* irqn) {
    if (irqn == nullptr) {
        return false;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(twim);
    if (base == reinterpret_cast<uintptr_t>(NRF_TWIM21)) {
        *irqn = TWIM21_IRQn;
        return true;
    }
    if (base == reinterpret_cast<uintptr_t>(NRF_TWIM20)) {
        *irqn = TWIM20_IRQn;
        return true;
    }
    return false;
}

static TwoWire* g_twim20Owner = nullptr;
static TwoWire* g_twim21Owner = nullptr;
static TwoWire* g_twim22Owner = nullptr;
static TwoWire* g_twim30Owner = nullptr;
static TwoWire* g_unknownOwner = nullptr;

static TwoWire*& twi_owner_slot(const NRF_TWIM_Type* twim) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(twim);
    if (base == reinterpret_cast<uintptr_t>(NRF_TWIM20)) {
        return g_twim20Owner;
    }
    if (base == reinterpret_cast<uintptr_t>(NRF_TWIM21)) {
        return g_twim21Owner;
    }
    if (base == reinterpret_cast<uintptr_t>(NRF_TWIM22)) {
        return g_twim22Owner;
    }
    if (base == reinterpret_cast<uintptr_t>(NRF_TWIM30)) {
        return g_twim30Owner;
    }
    return g_unknownOwner;
}

static void twi_clear_owner_if_target_active(const NRF_TWIM_Type* twim, TwoWire* ownerToClear) {
    TwoWire*& owner = twi_owner_slot(twim);
    if (owner != ownerToClear) {
        return;
    }

    IRQn_Type irqn = Reset_IRQn;
    if (twi_try_irqn_for_instance(twim, &irqn)) {
        NVIC_DisableIRQ(irqn);
    }
    owner = nullptr;
}

static bool twi_supports_target_mode(const NRF_TWIM_Type* twim) {
    IRQn_Type irqn = Reset_IRQn;
    return twi_try_irqn_for_instance(twim, &irqn);
}

}  // namespace

// The XIAO nRF54L15 board routes its two hardware I2C buses onto dedicated
// controllers (D4/D5 -> TWIM22, D12/D11 -> TWIM30), not the serial-fabric
// instances used by Serial/Serial1.
TwoWire Wire(NRF_TWIM22, PIN_WIRE_SDA, PIN_WIRE_SCL);
TwoWire Wire1(NRF_TWIM30, PIN_WIRE1_SDA, PIN_WIRE1_SCL);

extern "C" void TWIM20_IRQHandler(void) {
    if (g_twim20Owner != nullptr) {
        g_twim20Owner->handleTargetIrq();
    }
}

extern "C" void TWIM21_IRQHandler(void) {
    if (g_twim21Owner != nullptr) {
        g_twim21Owner->handleTargetIrq();
    }
}

TwoWire::TwoWire(NRF_TWIM_Type* twim, uint8_t sda, uint8_t scl)
    : _i2c(nullptr),
      _twim(twim),
      _sda(sda),
      _scl(scl),
      _frequency(400000UL),
      _initialized(false),
      _txBuffer{0},
      _txBufferLength(0),
      _txAddress(0),
      _rxBuffer{0},
      _rxBufferIndex(0),
      _rxBufferLength(0),
      _peek(-1),
      _targetTxBuffer{0},
      _targetTxLength(0),
      _targetTxIndex(0),
      _targetAddress(0),
      _targetRegistered(false),
      _inOnRequestCallback(false),
      _targetDirection(TARGET_DIR_NONE),
      _onReceive(nullptr),
      _onRequest(nullptr),
      _pendingRepeatedStart(false),
      _lastActivityUs(0U) {}

void TwoWire::begin() {
    if (_twim == nullptr) {
        return;
    }

    uint8_t sclPort = 0, sclPin = 0, sdaPort = 0, sdaPin = 0;
    if (!decode_pin(_scl, &sclPort, &sclPin) || !decode_pin(_sda, &sdaPort, &sdaPin)) {
        return;
    }

    configure_i2c_pin(sclPort, sclPin);
    configure_i2c_pin(sdaPort, sdaPin);

    const uintptr_t base = reinterpret_cast<uintptr_t>(_twim);

    twi_clear_owner_if_target_active(_twim, this);

    reg32(base + T_INTENCLR) = T_TWIS_INT_MASK;
    reg32(base + T_SHORTS) = 0U;

    reg32(base + T_ENABLE) = T_ENABLE_DISABLED;
    reg32(base + T_PSEL_SCL) = make_psel(sclPort, sclPin);
    reg32(base + T_PSEL_SDA) = make_psel(sdaPort, sdaPin);
    reg32(base + T_FREQUENCY) = twim_frequency_reg(_frequency);
    reg32(base + T_ENABLE) = T_ENABLE_TWIM;

    _initialized = true;
    _targetRegistered = false;
    _targetAddress = 0U;
    _targetDirection = TARGET_DIR_NONE;
    _inOnRequestCallback = false;
    _pendingRepeatedStart = false;
    clearControllerTxState();
    clearReceiveState();
    clearTargetTxState();
    _lastActivityUs = micros();
}

void TwoWire::begin(uint8_t address) {
    if (_twim == nullptr) {
        return;
    }
    if (!twi_supports_target_mode(_twim)) {
        end();
        return;
    }

    uint8_t sclPort = 0, sclPin = 0, sdaPort = 0, sdaPin = 0;
    if (!decode_pin(_scl, &sclPort, &sclPin) || !decode_pin(_sda, &sdaPort, &sdaPin)) {
        return;
    }

    configure_i2c_pin(sclPort, sclPin);
    configure_i2c_pin(sdaPort, sdaPin);

    const uintptr_t base = reinterpret_cast<uintptr_t>(_twim);
    IRQn_Type irqn = Reset_IRQn;
    const bool hasIqn = twi_try_irqn_for_instance(_twim, &irqn);
    TwoWire*& owner = twi_owner_slot(_twim);
    if (!hasIqn) {
        end();
        return;
    }

    twi_clear_owner_if_target_active(_twim, this);

    reg32(base + T_ENABLE) = T_ENABLE_DISABLED;
    reg32(base + T_INTENCLR) = T_TWIS_INT_MASK;
    reg32(base + T_SHORTS) = 0U;

    reg32(base + T_PSEL_SCL) = make_psel(sclPort, sclPin);
    reg32(base + T_PSEL_SDA) = make_psel(sdaPort, sdaPin);

    _targetAddress = static_cast<uint8_t>(address & 0x7FU);
    reg32(base + T_ADDRESS0) = _targetAddress;
    reg32(base + T_ADDRESS1) = 0U;
    reg32(base + T_CONFIG) = T_TWIS_CONFIG_ADDRESS0_ENABLE;
    reg32(base + T_ORC) = TWIS_DEFAULT_ORC;

    reg32(base + T_EVENTS_STOPPED) = 0U;
    reg32(base + T_EVENTS_ERROR) = 0U;
    reg32(base + T_EVENTS_WRITE) = 0U;
    reg32(base + T_EVENTS_READ) = 0U;
    reg32(base + T_EVENTS_DMA_RX_END) = 0U;
    reg32(base + T_EVENTS_DMA_TX_END) = 0U;
    reg32(base + T_TWIS_ERRORSRC) = T_TWIS_ERRORSRC_ALL;

    clearControllerTxState();
    clearReceiveState();
    clearTargetTxState();
    _targetDirection = TARGET_DIR_NONE;
    _inOnRequestCallback = false;
    _pendingRepeatedStart = false;

    reg32(base + T_ENABLE) = T_ENABLE_TWIS;
    armTargetRx();
    armTargetTx();

    reg32(base + T_SHORTS) = T_TWIS_SHORT_WRITE_SUSPEND | T_TWIS_SHORT_READ_SUSPEND;
    reg32(base + T_INTENSET) = T_TWIS_INT_MASK;

    owner = this;
    NVIC_SetPriority(irqn, 2U);
    NVIC_EnableIRQ(irqn);

    _initialized = true;
    _targetRegistered = true;
    _lastActivityUs = micros();
}

void TwoWire::begin(int address) {
    begin(static_cast<uint8_t>(address));
}

void TwoWire::end() {
    if (_twim == nullptr) {
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_twim);
    twi_clear_owner_if_target_active(_twim, this);

    reg32(base + T_INTENCLR) = T_TWIS_INT_MASK;
    reg32(base + T_SHORTS) = 0U;

    reg32(base + T_ENABLE) = T_ENABLE_DISABLED;
    reg32(base + T_PSEL_SCL) = PSEL_DISCONNECTED;
    reg32(base + T_PSEL_SDA) = PSEL_DISCONNECTED;

    _initialized = false;
    _txBufferLength = 0;
    _pendingRepeatedStart = false;
    _targetRegistered = false;
    _targetAddress = 0U;
    _targetDirection = TARGET_DIR_NONE;
    _inOnRequestCallback = false;
    clearReceiveState();
    clearTargetTxState();
    _lastActivityUs = micros();
}

bool TwoWire::setPins(uint8_t sda, uint8_t scl) {
    if (_twim == nullptr) {
        return false;
    }

    uint8_t sclPort = 0, sclPin = 0, sdaPort = 0, sdaPin = 0;
    if (!decode_pin(scl, &sclPort, &sclPin) || !decode_pin(sda, &sdaPort, &sdaPin)) {
        return false;
    }

    const bool wasInitialized = _initialized;
    const bool wasTarget = _targetRegistered;
    const uint8_t targetAddress = _targetAddress;

    if (wasInitialized) {
        end();
    }

    _sda = sda;
    _scl = scl;

    if (!wasInitialized) {
        return true;
    }

    if (wasTarget) {
        begin(targetAddress);
    } else {
        begin();
    }

    return _initialized;
}

void TwoWire::setClock(uint32_t freq) {
    _frequency = freq;
    if (_initialized && !_targetRegistered && _twim != nullptr) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(_twim);
        reg32(base + T_FREQUENCY) = twim_frequency_reg(_frequency);
    }
    _lastActivityUs = micros();
}

void TwoWire::beginTransmission(uint8_t address) {
    if (!_initialized && !_targetRegistered) {
        begin();
    }
    if (_targetRegistered) {
        return;
    }
    _txAddress = static_cast<uint8_t>(address & 0x7FU);
    _txBufferLength = 0;
    _lastActivityUs = micros();
}

void TwoWire::beginTransmission(int address) {
    beginTransmission(static_cast<uint8_t>(address));
}

uint8_t TwoWire::endTransmission(bool sendStop) {
    if (!_initialized && !_targetRegistered) {
        begin();
    }
    if (!_initialized || _targetRegistered || _twim == nullptr) {
        return 4;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_twim);

    if (_txBufferLength == 0U && sendStop) {
        // Force an address phase in write mode so write-only targets
        // (for example SSD1306) can be detected by begin()/scanner code.
        uint8_t dummy = 0x00U;

        reg32(base + T_EVENTS_STOPPED) = 0U;
        reg32(base + T_EVENTS_ERROR) = 0U;
        reg32(base + T_EVENTS_LASTTX) = 0U;
        reg32(base + T_EVENTS_DMA_TX_END) = 0U;
        reg32(base + T_TWIM_ERRORSRC) = T_TWIM_ERRORSRC_ALL;

        reg32(base + T_ADDRESS) = _txAddress;
        reg32(base + T_DMA_TX_PTR) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&dummy));
        reg32(base + T_DMA_TX_MAXCNT) = 1U;
        reg32(base + T_TASKS_DMA_TX_START) = 1U;

        const bool writeOk = wait_tx_done_or_error(base, 300000UL);
        const uint32_t errorsrc = reg32(base + T_TWIM_ERRORSRC) & T_TWIM_ERRORSRC_ALL;

        reg32(base + T_TASKS_STOP) = 1U;
        const bool stopOk = wait_event(base, T_EVENTS_STOPPED, 300000UL);
        reg32(base + T_TWIM_ERRORSRC) = T_TWIM_ERRORSRC_ALL;

        _pendingRepeatedStart = false;
        _txBufferLength = 0U;
        _lastActivityUs = micros();
        return end_tx_error_code(writeOk, stopOk, errorsrc);
    }

    reg32(base + T_EVENTS_STOPPED) = 0U;
    reg32(base + T_EVENTS_ERROR) = 0U;
    reg32(base + T_EVENTS_LASTTX) = 0U;
    reg32(base + T_EVENTS_DMA_TX_END) = 0U;
    reg32(base + T_TWIM_ERRORSRC) = T_TWIM_ERRORSRC_ALL;

    reg32(base + T_ADDRESS) = _txAddress;
    reg32(base + T_DMA_TX_PTR) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_txBuffer));
    reg32(base + T_DMA_TX_MAXCNT) = _txBufferLength;

    reg32(base + T_TASKS_DMA_TX_START) = 1U;

    const uint32_t doneEvent = (_txBufferLength > 0U) ? T_EVENTS_LASTTX : T_EVENTS_DMA_TX_END;
    const bool writeOk = wait_event_or_error(base, doneEvent, 300000UL);
    const uint32_t errorsrc = reg32(base + T_TWIM_ERRORSRC) & T_TWIM_ERRORSRC_ALL;
    const bool hadError = (reg32(base + T_EVENTS_ERROR) != 0U) || (errorsrc != 0U);

    bool stopOk = true;
    if (sendStop || !writeOk || hadError) {
        reg32(base + T_TASKS_STOP) = 1U;
        stopOk = wait_event(base, T_EVENTS_STOPPED, 300000UL);
        _pendingRepeatedStart = false;
    } else {
        _pendingRepeatedStart = true;
    }

    reg32(base + T_TWIM_ERRORSRC) = T_TWIM_ERRORSRC_ALL;

    _txBufferLength = 0;
    _lastActivityUs = micros();
    return end_tx_error_code(writeOk, stopOk, errorsrc);
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity, uint8_t sendStop) {
    return requestFrom(address, static_cast<size_t>(quantity), sendStop != 0U);
}

uint8_t TwoWire::requestFrom(uint8_t address, size_t quantity, bool sendStop) {
    if (!_initialized && !_targetRegistered) {
        begin();
    }
    if (!_initialized || _targetRegistered || _twim == nullptr || quantity == 0U) {
        return 0;
    }
    if (quantity > BUFFER_LENGTH) {
        quantity = BUFFER_LENGTH;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_twim);

    clearReceiveState();

    reg32(base + T_EVENTS_STOPPED) = 0U;
    reg32(base + T_EVENTS_ERROR) = 0U;
    reg32(base + T_EVENTS_LASTRX) = 0U;
    reg32(base + T_TWIM_ERRORSRC) = T_TWIM_ERRORSRC_ALL;

    reg32(base + T_ADDRESS) = (address & 0x7FU);
    reg32(base + T_DMA_RX_PTR) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_rxBuffer));
    reg32(base + T_DMA_RX_MAXCNT) = static_cast<uint32_t>(quantity);

    reg32(base + T_TASKS_DMA_RX_START) = 1U;
    const bool readOk = wait_event_or_error(base, T_EVENTS_LASTRX, 300000UL);
    const uint32_t errorsrc = reg32(base + T_TWIM_ERRORSRC) & T_TWIM_ERRORSRC_ALL;
    const bool hadError = (reg32(base + T_EVENTS_ERROR) != 0U) || (errorsrc != 0U);

    bool stopOk = true;
    if (sendStop || !readOk || hadError) {
        reg32(base + T_TASKS_STOP) = 1U;
        stopOk = wait_event(base, T_EVENTS_STOPPED, 300000UL);
        _pendingRepeatedStart = false;
    } else {
        _pendingRepeatedStart = true;
    }

    reg32(base + T_TWIM_ERRORSRC) = T_TWIM_ERRORSRC_ALL;

    if (!readOk || !stopOk || hadError) {
        _pendingRepeatedStart = false;
        clearReceiveState();
        _lastActivityUs = micros();
        return 0;
    }

    _rxBufferLength = static_cast<uint8_t>(quantity);
    _rxBufferIndex = 0;
    _peek = -1;
    _lastActivityUs = micros();
    return _rxBufferLength;
}

uint8_t TwoWire::requestFrom(int address, int quantity) {
    return requestFrom(static_cast<uint8_t>(address), static_cast<size_t>(quantity), true);
}

uint8_t TwoWire::requestFrom(int address, int quantity, uint8_t sendStop) {
    return requestFrom(static_cast<uint8_t>(address), static_cast<size_t>(quantity), sendStop != 0U);
}

int TwoWire::available(void) {
    int avail = static_cast<int>(_rxBufferLength) - static_cast<int>(_rxBufferIndex);
    if (_peek >= 0) {
        ++avail;
    }
    return (avail > 0) ? avail : 0;
}

int TwoWire::read(void) {
    if (_peek >= 0) {
        const int v = _peek;
        _peek = -1;
        return v;
    }
    if (_rxBufferIndex >= _rxBufferLength) {
        return -1;
    }
    return _rxBuffer[_rxBufferIndex++];
}

int TwoWire::peek(void) {
    if (_peek >= 0) {
        return _peek;
    }
    if (_rxBufferIndex >= _rxBufferLength) {
        return -1;
    }
    _peek = _rxBuffer[_rxBufferIndex++];
    return _peek;
}

size_t TwoWire::write(uint8_t data) {
    if (isTargetWriteContext()) {
        if (_targetTxLength >= BUFFER_LENGTH) {
            return 0;
        }
        _targetTxBuffer[_targetTxLength++] = data;
        return 1;
    }

    if (_txBufferLength >= BUFFER_LENGTH) {
        return 0;
    }
    _txBuffer[_txBufferLength++] = data;
    return 1;
}

size_t TwoWire::write(const uint8_t* data, size_t quantity) {
    if (data == nullptr || quantity == 0U) {
        return 0;
    }

    size_t written = 0;
    if (isTargetWriteContext()) {
        while (written < quantity && _targetTxLength < BUFFER_LENGTH) {
            _targetTxBuffer[_targetTxLength++] = data[written++];
        }
        return written;
    }

    while (written < quantity && _txBufferLength < BUFFER_LENGTH) {
        _txBuffer[_txBufferLength++] = data[written++];
    }
    return written;
}

void TwoWire::flush(void) {
}

void TwoWire::onReceive(void (*callback)(int)) {
    _onReceive = callback;
}

void TwoWire::onRequest(void (*callback)(void)) {
    _onRequest = callback;
}

void TwoWire::serviceAutoGate() {
#if defined(NRF54L15_CLEAN_AUTO_GATE) && (NRF54L15_CLEAN_AUTO_GATE != 0)
    if (!_initialized || _targetRegistered || _pendingRepeatedStart) {
        return;
    }

    const uint32_t idleUs = static_cast<uint32_t>(NRF54L15_CLEAN_AUTO_GATE_IDLE_US);
    const uint32_t nowUs = micros();
    if ((nowUs - _lastActivityUs) >= idleUs) {
        end();
    }
#endif
}

int TwoWire::targetWriteRequestedAdapter(struct i2c_target_config* config) {
    (void)config;
    return Wire.handleTargetWriteRequested();
}

int TwoWire::targetWriteReceivedAdapter(struct i2c_target_config* config, uint8_t value) {
    (void)config;
    return Wire.handleTargetWriteReceived(value);
}

int TwoWire::targetReadRequestedAdapter(struct i2c_target_config* config, uint8_t* value) {
    (void)config;
    return Wire.handleTargetReadRequested(value);
}

int TwoWire::targetReadProcessedAdapter(struct i2c_target_config* config, uint8_t* value) {
    (void)config;
    return Wire.handleTargetReadProcessed(value);
}

int TwoWire::targetStopAdapter(struct i2c_target_config* config) {
    (void)config;
    return Wire.handleTargetStop();
}

bool TwoWire::isTargetWriteContext() const {
    if (!_targetRegistered) {
        return false;
    }
    if (_inOnRequestCallback) {
        return true;
    }
    return _targetDirection != TARGET_DIR_WRITE;
}

void TwoWire::clearControllerTxState() {
    _txBufferLength = 0;
}

void TwoWire::clearReceiveState() {
    _rxBufferIndex = 0;
    _rxBufferLength = 0;
    _peek = -1;
    memset(_rxBuffer, 0, sizeof(_rxBuffer));
}

void TwoWire::clearTargetTxState() {
    _targetTxLength = 0;
    _targetTxIndex = 0;
}

int TwoWire::provideTargetByte(uint8_t* value) {
    if (value == nullptr) {
        return -1;
    }

    if (_targetTxIndex < _targetTxLength) {
        *value = _targetTxBuffer[_targetTxIndex++];
    } else {
        *value = TWIS_DEFAULT_ORC;
    }
    return 0;
}

int TwoWire::handleTargetWriteRequested() {
    _targetDirection = TARGET_DIR_WRITE;
    clearReceiveState();
    return 0;
}

int TwoWire::handleTargetWriteReceived(uint8_t value) {
    if (_rxBufferLength >= BUFFER_LENGTH) {
        return -1;
    }
    _rxBuffer[_rxBufferLength++] = value;
    _rxBufferIndex = 0;
    _peek = -1;
    return 0;
}

int TwoWire::handleTargetReadRequested(uint8_t* value) {
    _targetDirection = TARGET_DIR_READ;

    if (_onRequest != nullptr) {
        clearTargetTxState();
        _inOnRequestCallback = true;
        _onRequest();
        _inOnRequestCallback = false;
    }

    _targetTxIndex = 0;

    if (value != nullptr) {
        if (_targetTxLength > 0U) {
            *value = _targetTxBuffer[0];
        } else {
            *value = TWIS_DEFAULT_ORC;
        }
    }

    return 0;
}

int TwoWire::handleTargetReadProcessed(uint8_t* value) {
    return provideTargetByte(value);
}

int TwoWire::handleTargetStop() {
    if (_twim == nullptr) {
        _targetDirection = TARGET_DIR_NONE;
        _inOnRequestCallback = false;
        return 0;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_twim);

    if (_targetDirection == TARGET_DIR_WRITE) {
        uint32_t rxAmount = reg32(base + T_DMA_RX_AMOUNT) & 0xFFFFUL;
        if (rxAmount > BUFFER_LENGTH) {
            rxAmount = BUFFER_LENGTH;
        }
        _rxBufferLength = static_cast<uint8_t>(rxAmount);
        _rxBufferIndex = 0;
        _peek = -1;

        if (_onReceive != nullptr && _rxBufferLength > 0U) {
            _onReceive(static_cast<int>(_rxBufferLength));
        }
    } else if (_targetDirection == TARGET_DIR_READ) {
        uint32_t txAmount = reg32(base + T_DMA_TX_AMOUNT) & 0xFFFFUL;
        if (txAmount > BUFFER_LENGTH) {
            txAmount = BUFFER_LENGTH;
        }
        _targetTxIndex = static_cast<uint8_t>(txAmount);
    }

    _targetDirection = TARGET_DIR_NONE;
    _inOnRequestCallback = false;
    return 0;
}

void TwoWire::armTargetRx() {
    if (!_targetRegistered || _twim == nullptr) {
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_twim);
    reg32(base + T_EVENTS_DMA_RX_END) = 0U;
    reg32(base + T_DMA_RX_PTR) =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_rxBuffer));
    reg32(base + T_DMA_RX_MAXCNT) = BUFFER_LENGTH;
    reg32(base + T_TASKS_PREPARERX) = 1U;
}

void TwoWire::armTargetTx() {
    if (!_targetRegistered || _twim == nullptr) {
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_twim);
    const uint32_t txCount = (_targetTxLength > 0U) ? _targetTxLength : 1U;

    if (_targetTxLength == 0U) {
        _targetTxBuffer[0] = TWIS_DEFAULT_ORC;
    }

    _targetTxIndex = 0;
    reg32(base + T_EVENTS_DMA_TX_END) = 0U;
    reg32(base + T_DMA_TX_PTR) =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_targetTxBuffer));
    reg32(base + T_DMA_TX_MAXCNT) = txCount;
    reg32(base + T_TASKS_PREPARETX) = 1U;
}

void TwoWire::handleTargetIrq() {
    if (!_targetRegistered || _twim == nullptr) {
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_twim);

    if (reg32(base + T_EVENTS_ERROR) != 0U) {
        reg32(base + T_EVENTS_ERROR) = 0U;
        reg32(base + T_TWIS_ERRORSRC) = T_TWIS_ERRORSRC_ALL;
    }

    if (reg32(base + T_EVENTS_WRITE) != 0U) {
        reg32(base + T_EVENTS_WRITE) = 0U;
        (void)handleTargetWriteRequested();
        armTargetRx();
        reg32(base + T_TASKS_RESUME) = 1U;
    }

    if (reg32(base + T_EVENTS_READ) != 0U) {
        reg32(base + T_EVENTS_READ) = 0U;
        uint8_t first = TWIS_DEFAULT_ORC;
        (void)handleTargetReadRequested(&first);
        armTargetTx();
        reg32(base + T_TASKS_RESUME) = 1U;
    }

    if (reg32(base + T_EVENTS_DMA_RX_END) != 0U) {
        reg32(base + T_EVENTS_DMA_RX_END) = 0U;
        uint32_t rxAmount = reg32(base + T_DMA_RX_AMOUNT) & 0xFFFFUL;
        if (rxAmount > BUFFER_LENGTH) {
            rxAmount = BUFFER_LENGTH;
        }
        _rxBufferLength = static_cast<uint8_t>(rxAmount);
        _rxBufferIndex = 0;
        _peek = -1;
    }

    if (reg32(base + T_EVENTS_DMA_TX_END) != 0U) {
        reg32(base + T_EVENTS_DMA_TX_END) = 0U;
        uint32_t txAmount = reg32(base + T_DMA_TX_AMOUNT) & 0xFFFFUL;
        if (txAmount > BUFFER_LENGTH) {
            txAmount = BUFFER_LENGTH;
        }
        _targetTxIndex = static_cast<uint8_t>(txAmount);
    }

    if (reg32(base + T_EVENTS_STOPPED) != 0U) {
        reg32(base + T_EVENTS_STOPPED) = 0U;
        (void)handleTargetStop();
        armTargetRx();
    }
}
