#include "HardwareSerial.h"

#include "Arduino.h"

namespace {

static constexpr uint32_t kPselDisconnected = 0xFFFFFFFFUL;

// UARTE register offsets.
static constexpr uint32_t U_TASKS_DMA_RX_START = 0x028UL;
static constexpr uint32_t U_TASKS_DMA_RX_STOP  = 0x02CUL;
static constexpr uint32_t U_TASKS_DMA_TX_START = 0x050UL;
static constexpr uint32_t U_TASKS_DMA_TX_STOP  = 0x054UL;

static constexpr uint32_t U_EVENTS_ERROR       = 0x114UL;
static constexpr uint32_t U_EVENTS_RXTO        = 0x124UL;
static constexpr uint32_t U_EVENTS_TXSTOPPED   = 0x130UL;
static constexpr uint32_t U_EVENTS_DMA_RX_END  = 0x14CUL;
static constexpr uint32_t U_EVENTS_DMA_TX_END  = 0x168UL;

static constexpr uint32_t U_ERRORSRC           = 0x480UL;
static constexpr uint32_t U_ENABLE             = 0x500UL;
static constexpr uint32_t U_BAUDRATE           = 0x524UL;
static constexpr uint32_t U_CONFIG             = 0x56CUL;

static constexpr uint32_t U_PSEL_TXD           = 0x604UL;
static constexpr uint32_t U_PSEL_CTS           = 0x608UL;
static constexpr uint32_t U_PSEL_RXD           = 0x60CUL;
static constexpr uint32_t U_PSEL_RTS           = 0x610UL;

static constexpr uint32_t U_DMA_RX_PTR         = 0x704UL;
static constexpr uint32_t U_DMA_RX_MAXCNT      = 0x708UL;
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
        case PIN_LED_BUILTIN: *port = 2; *p = 0; return true;
        case PIN_BUTTON: *port = 0; *p = 0; return true;
        case PIN_SAMD11_TX: *port = 1; *p = 8; return true;
        case PIN_SAMD11_RX: *port = 1; *p = 9; return true;
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
    fmt.configReg = (UARTE_CONFIG_HWFC_Disabled << UARTE_CONFIG_HWFC_Pos);
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
    const uint32_t margin = 2000UL;
    return (per_byte * bytes) + margin;
}

}  // namespace

HardwareSerial::HardwareSerial(NRF_UARTE_Type* uart, uint8_t txPin, uint8_t rxPin)
    : _uart(uart),
      _txPin(txPin),
      _rxPin(rxPin),
      _peek(-1),
      _configured(false),
      _baud(9600UL),
      _config(0U),
      _rxByte(0),
      _txByte(0),
      _dataMask(0xFFU) {}

void HardwareSerial::begin(unsigned long baud) {
    begin(baud, SERIAL_8N1);
}

void HardwareSerial::begin(unsigned long baud, uint16_t config) {
    if (_uart == nullptr) {
        return;
    }

    uint8_t txPort = 0, tx = 0, rxPort = 0, rx = 0;
    if (!decode_pin(_txPin, &txPort, &tx) || !decode_pin(_rxPin, &rxPort, &rx)) {
        return;
    }

    configure_pin_output(txPort, tx, true);
    configure_pin_input(rxPort, rx, true);

    const uintptr_t base = reinterpret_cast<uintptr_t>(_uart);

    reg32(base + U_ENABLE) = UARTE_ENABLE_ENABLE_Disabled;
    reg32(base + U_PSEL_TXD) = make_psel(txPort, tx);
    reg32(base + U_PSEL_RXD) = make_psel(rxPort, rx);
    reg32(base + U_PSEL_CTS) = kPselDisconnected;
    reg32(base + U_PSEL_RTS) = kPselDisconnected;
    const UarteFormat fmt = decode_serial_format(config);
    _dataMask = fmt.dataMask;
    reg32(base + U_CONFIG) = fmt.configReg;
    reg32(base + U_BAUDRATE) = baud_to_reg(baud);
    reg32(base + U_ENABLE) = UARTE_ENABLE_ENABLE_Enabled;

    _baud = baud;
    _config = config;
    _peek = -1;
    _configured = true;
}

void HardwareSerial::end() {
    if (_uart == nullptr) {
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_uart);
    reg32(base + U_TASKS_DMA_TX_STOP) = UARTE_TASKS_DMA_TX_STOP_STOP_Trigger;
    reg32(base + U_TASKS_DMA_RX_STOP) = UARTE_TASKS_DMA_RX_STOP_STOP_Trigger;
    reg32(base + U_ENABLE) = UARTE_ENABLE_ENABLE_Disabled;

    reg32(base + U_PSEL_TXD) = kPselDisconnected;
    reg32(base + U_PSEL_RXD) = kPselDisconnected;
    reg32(base + U_PSEL_CTS) = kPselDisconnected;
    reg32(base + U_PSEL_RTS) = kPselDisconnected;

    _configured = false;
    _peek = -1;
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

bool HardwareSerial::beginRxByte() {
    if (!_configured || _uart == nullptr) {
        return false;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_uart);

    reg32(base + U_EVENTS_DMA_RX_END) = 0U;
    reg32(base + U_EVENTS_ERROR) = 0U;
    reg32(base + U_DMA_RX_PTR) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&_rxByte));
    reg32(base + U_DMA_RX_MAXCNT) = 1U;
    reg32(base + U_TASKS_DMA_RX_START) = UARTE_TASKS_DMA_RX_START_START_Trigger;

    const bool ok =
        wait_event_timeout_us(base, U_EVENTS_DMA_RX_END, serial_byte_timeout_us(_baud, 1U));

    reg32(base + U_TASKS_DMA_RX_STOP) = UARTE_TASKS_DMA_RX_STOP_STOP_Trigger;
    wait_event_timeout_us(base, U_EVENTS_RXTO, 2000UL);

    if (!ok) {
        reg32(base + U_ERRORSRC) = 0xFFFFFFFFUL;
    }

    return ok;
}

int HardwareSerial::available() {
    if (_peek >= 0) {
        return 1;
    }
    if (beginRxByte()) {
        _peek = static_cast<int>(_rxByte & _dataMask);
        return 1;
    }
    return 0;
}

int HardwareSerial::read() {
    if (_peek >= 0) {
        const int v = _peek;
        _peek = -1;
        return v;
    }
    if (beginRxByte()) {
        return static_cast<int>(_rxByte & _dataMask);
    }
    return -1;
}

int HardwareSerial::peek() {
    (void)available();
    return _peek;
}

void HardwareSerial::flush() {
    // TX writes are synchronous in this implementation.
}

size_t HardwareSerial::write(uint8_t value) {
    if (!_configured || _uart == nullptr) {
        return 0;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(_uart);
    _txByte = static_cast<uint8_t>(value & _dataMask);

    reg32(base + U_EVENTS_DMA_TX_END) = 0U;
    reg32(base + U_DMA_TX_PTR) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&_txByte));
    reg32(base + U_DMA_TX_MAXCNT) = 1U;

    reg32(base + U_TASKS_DMA_TX_START) = UARTE_TASKS_DMA_TX_START_START_Trigger;
    if (!wait_event_timeout_us(base, U_EVENTS_DMA_TX_END, serial_byte_timeout_us(_baud, 1U))) {
        // Recover on timeout to prevent partial-frame stream corruption.
        reg32(base + U_EVENTS_TXSTOPPED) = 0U;
        reg32(base + U_TASKS_DMA_TX_STOP) = UARTE_TASKS_DMA_TX_STOP_STOP_Trigger;
        wait_event_timeout_us(base, U_EVENTS_TXSTOPPED, 2000UL);
        return 0;
    }

    return 1;
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

#if defined(NRF54L15_CLEAN_SERIAL_ROUTE_HEADER)
HardwareSerial Serial(NRF_UARTE21, PIN_SERIAL_TX, PIN_SERIAL_RX);
HardwareSerial Serial1(NRF_UARTE20, PIN_SAMD11_RX, PIN_SAMD11_TX);
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
