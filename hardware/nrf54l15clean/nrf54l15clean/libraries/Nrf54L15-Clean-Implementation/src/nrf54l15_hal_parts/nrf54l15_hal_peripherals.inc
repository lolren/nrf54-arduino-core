namespace xiao_nrf54l15 {

size_t formatBleAddressString(const uint8_t address[kBleLegacyAddressLength],
                              char* out, size_t outSize) {
  if (out == nullptr || outSize == 0U) {
    return 0U;
  }
  out[0] = '\0';
  if (address == nullptr || outSize < kBleAddressStringLength) {
    return 0U;
  }

  static constexpr char kHexDigits[] = "0123456789ABCDEF";
  size_t outIndex = 0U;
  for (int i = static_cast<int>(kBleLegacyAddressLength) - 1; i >= 0; --i) {
    const uint8_t value = address[i];
    out[outIndex++] = kHexDigits[(value >> 4U) & 0x0FU];
    out[outIndex++] = kHexDigits[value & 0x0FU];
    if (i > 0) {
      out[outIndex++] = ':';
    }
  }
  out[outIndex] = '\0';
  return outIndex;
}

bool parseBleAddressString(const char* text,
                           uint8_t addressOut[kBleLegacyAddressLength]) {
  if (text == nullptr || addressOut == nullptr) {
    return false;
  }

  auto isSpace = [](char c) -> bool {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  auto hexNibble = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
      return 10 + (c - 'A');
    }
    if (c >= 'a' && c <= 'f') {
      return 10 + (c - 'a');
    }
    return -1;
  };

  while (isSpace(*text)) {
    ++text;
  }

  char separator = '\0';
  for (int i = static_cast<int>(kBleLegacyAddressLength) - 1; i >= 0; --i) {
    const int hi = hexNibble(*text++);
    const int lo = hexNibble(*text++);
    if (hi < 0 || lo < 0) {
      return false;
    }
    addressOut[i] = static_cast<uint8_t>((hi << 4U) | lo);

    if (i == 0) {
      break;
    }

    if (*text == ':' || *text == '-') {
      if (separator == '\0') {
        separator = *text;
      } else if (*text != separator) {
        return false;
      }
      ++text;
      continue;
    }

    if (separator != '\0') {
      return false;
    }
  }

  while (isSpace(*text)) {
    ++text;
  }

  return *text == '\0';
}

bool Gpio::configure(const Pin& pin, GpioDirection direction, GpioPull pull) {
  if (!isConnected(pin)) {
    return false;
  }

  const uint32_t base = gpioBaseForPort(pin.port);
  if (base == 0U) {
    return false;
  }

  const uint32_t bit = (1UL << pin.pin);
  const uint32_t cnfAddr = base + gpio::PIN_CNF +
                           (static_cast<uint32_t>(pin.pin) * sizeof(uint32_t));

  uint32_t cnf = reg32(cnfAddr);
  cnf &= ~(gpio::PIN_CNF_DIR_Msk | gpio::PIN_CNF_INPUT_Msk |
           gpio::PIN_CNF_PULL_Msk);

  if (direction == GpioDirection::kOutput) {
    cnf |= gpio::PIN_CNF_DIR_Msk;
    // Disconnect input buffer while pin is output.
    cnf |= gpio::PIN_CNF_INPUT_Msk;
    reg32(base + gpio::DIRSET) = bit;
  } else {
    // Input buffer connected for reads.
    cnf &= ~gpio::PIN_CNF_INPUT_Msk;
    reg32(base + gpio::DIRCLR) = bit;
  }

  cnf |= ((static_cast<uint32_t>(pull) << gpio::PIN_CNF_PULL_Pos) &
          gpio::PIN_CNF_PULL_Msk);

  reg32(cnfAddr) = cnf;
  return true;
}

bool Gpio::write(const Pin& pin, bool high) {
  if (!isConnected(pin)) {
    return false;
  }

  const uint32_t base = gpioBaseForPort(pin.port);
  if (base == 0U) {
    return false;
  }

  const uint32_t bit = (1UL << pin.pin);
  reg32(base + (high ? gpio::OUTSET : gpio::OUTCLR)) = bit;
  return true;
}

bool Gpio::read(const Pin& pin, bool* high) {
  if (!isConnected(pin) || high == nullptr) {
    return false;
  }

  const uint32_t base = gpioBaseForPort(pin.port);
  if (base == 0U) {
    return false;
  }

  const uint32_t bit = (1UL << pin.pin);
  *high = (reg32(base + gpio::IN) & bit) != 0U;
  return true;
}

bool Gpio::toggle(const Pin& pin) {
  bool state = false;
  if (!read(pin, &state)) {
    return false;
  }
  return write(pin, !state);
}

bool Gpio::setDriveS0D1(const Pin& pin) {
  if (!isConnected(pin)) {
    return false;
  }

  const uint32_t base = gpioBaseForPort(pin.port);
  if (base == 0U) {
    return false;
  }

  const uint32_t cnfAddr = base + gpio::PIN_CNF +
                           (static_cast<uint32_t>(pin.pin) * sizeof(uint32_t));
  uint32_t cnf = reg32(cnfAddr);

  cnf &= ~(gpio::PIN_CNF_DRIVE0_Msk | gpio::PIN_CNF_DRIVE1_Msk);
  cnf |= (gpio::DRIVE0_S0 << gpio::PIN_CNF_DRIVE0_Pos);
  cnf |= (gpio::DRIVE1_D1 << gpio::PIN_CNF_DRIVE1_Pos);

  reg32(cnfAddr) = cnf;
  return true;
}

Spim::Spim(uint32_t base, uint32_t coreClockHz)
    : base_(base), coreClockHz_(coreClockHz), cs_(kPinDisconnected) {}

bool Spim::begin(const Pin& sck, const Pin& mosi, const Pin& miso,
                 const Pin& cs, uint32_t hz, SpiMode mode, bool lsbFirst) {
  if (!isConnected(sck) || !isConnected(mosi) || !isConnected(miso)) {
    return false;
  }

  cs_ = cs;

  if (!Gpio::configure(sck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(mosi, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(miso, GpioDirection::kInput, GpioPull::kDisabled)) {
    return false;
  }

  if (isConnected(cs_) &&
      (!Gpio::configure(cs_, GpioDirection::kOutput, GpioPull::kDisabled) ||
       !Gpio::write(cs_, true))) {
    return false;
  }

  reg32(base_ + spim::ENABLE) = spim::ENABLE_DISABLED;

  reg32(base_ + spim::PSEL_SCK) = make_psel(sck.port, sck.pin);
  reg32(base_ + spim::PSEL_MOSI) = make_psel(mosi.port, mosi.pin);
  reg32(base_ + spim::PSEL_MISO) = make_psel(miso.port, miso.pin);
  reg32(base_ + spim::PSEL_DCX) = PSEL_DISCONNECTED;
  reg32(base_ + spim::PSEL_CSN) = PSEL_DISCONNECTED;

  if (!setFrequency(hz)) {
    return false;
  }

  uint32_t config = 0;
  if (lsbFirst) {
    config |= spim::CONFIG_ORDER_LSB_FIRST;
  }

  if (mode == SpiMode::kMode1 || mode == SpiMode::kMode3) {
    config |= spim::CONFIG_CPHA_TRAILING;
  }
  if (mode == SpiMode::kMode2 || mode == SpiMode::kMode3) {
    config |= spim::CONFIG_CPOL_ACTIVE_LOW;
  }

  reg32(base_ + spim::CONFIG) = config;
  reg32(base_ + spim::ORC) = 0xFF;
  reg32(base_ + spim::ENABLE) = spim::ENABLE_ENABLED;

  return true;
}

bool Spim::setFrequency(uint32_t hz) {
  if (base_ == 0U) {
    return false;
  }
  const uint32_t minDivisor = (base_ == SPIM00_BASE) ? 4U : 2U;
  const uint32_t prescaler = spimPrescaler(coreClockHz_, hz, minDivisor);
  if (prescaler == 0U) {
    return false;
  }
  reg32(base_ + spim::PRESCALER) = prescaler;
  return true;
}

void Spim::end() {
  reg32(base_ + spim::ENABLE) = spim::ENABLE_DISABLED;
  reg32(base_ + spim::PSEL_SCK) = PSEL_DISCONNECTED;
  reg32(base_ + spim::PSEL_MOSI) = PSEL_DISCONNECTED;
  reg32(base_ + spim::PSEL_MISO) = PSEL_DISCONNECTED;
  reg32(base_ + spim::PSEL_CSN) = PSEL_DISCONNECTED;
}

bool Spim::transfer(const uint8_t* tx, uint8_t* rx, size_t len,
                    uint32_t spinLimit) {
  if (len == 0U) {
    return true;
  }
  if (len > 0xFFFFU) {
    return false;
  }

  uint8_t txFallback = 0xFF;
  uint8_t rxFallback = 0;

  const uint8_t* txPtr = (tx != nullptr) ? tx : &txFallback;
  uint8_t* rxPtr = (rx != nullptr) ? rx : &rxFallback;

  const uint32_t txLen = (tx != nullptr) ? static_cast<uint32_t>(len) : 0U;
  const uint32_t rxLen = (rx != nullptr) ? static_cast<uint32_t>(len) : 0U;

  clearEvent(base_, spim::EVENTS_STARTED);
  clearEvent(base_, spim::EVENTS_END);
  clearEvent(base_, spim::EVENTS_STOPPED);

  reg32(base_ + spim::DMA_TX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(txPtr));
  reg32(base_ + spim::DMA_TX_MAXCNT) = txLen;

  reg32(base_ + spim::DMA_RX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rxPtr));
  reg32(base_ + spim::DMA_RX_MAXCNT) = rxLen;

  if (isConnected(cs_)) {
    Gpio::write(cs_, false);
  }

  reg32(base_ + spim::TASKS_START) = 1;
  const bool endOk = waitForEvent(base_, spim::EVENTS_END, spinLimit);

  reg32(base_ + spim::TASKS_STOP) = 1;
  const bool stopOk = waitForEvent(base_, spim::EVENTS_STOPPED, spinLimit);

  if (isConnected(cs_)) {
    Gpio::write(cs_, true);
  }

  return endOk && stopOk;
}

Spis::Spis(uint32_t base)
    : spis_(reinterpret_cast<NRF_SPIS_Type*>(static_cast<uintptr_t>(base))),
      active_(false) {}

bool Spis::begin(const Pin& sck, const Pin& mosi, const Pin& miso,
                 const Pin& csn, SpiMode mode, bool lsbFirst,
                 uint8_t defaultChar, uint8_t overReadChar,
                 bool autoAcquireAfterEnd) {
  if (!isConnected(sck) || !isConnected(mosi) || !isConnected(miso) ||
      !isConnected(csn)) {
    return false;
  }

  if (!Gpio::configure(sck, GpioDirection::kInput, GpioPull::kDisabled) ||
      !Gpio::configure(mosi, GpioDirection::kInput, GpioPull::kDisabled) ||
      !Gpio::configure(miso, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(csn, GpioDirection::kInput, GpioPull::kPullUp) ||
      !Gpio::write(miso, true)) {
    return false;
  }

  spis_->ENABLE = (SPIS_ENABLE_ENABLE_Disabled << SPIS_ENABLE_ENABLE_Pos);
  spis_->PSEL.SCK = make_psel(sck.port, sck.pin);
  spis_->PSEL.MOSI = make_psel(mosi.port, mosi.pin);
  spis_->PSEL.MISO = make_psel(miso.port, miso.pin);
  spis_->PSEL.CSN = make_psel(csn.port, csn.pin);

  uint32_t config = 0U;
  if (lsbFirst) {
    config |= (SPIS_CONFIG_ORDER_LsbFirst << SPIS_CONFIG_ORDER_Pos) &
              SPIS_CONFIG_ORDER_Msk;
  }
  if (mode == SpiMode::kMode1 || mode == SpiMode::kMode3) {
    config |= (SPIS_CONFIG_CPHA_Trailing << SPIS_CONFIG_CPHA_Pos) &
              SPIS_CONFIG_CPHA_Msk;
  }
  if (mode == SpiMode::kMode2 || mode == SpiMode::kMode3) {
    config |= (SPIS_CONFIG_CPOL_ActiveLow << SPIS_CONFIG_CPOL_Pos) &
              SPIS_CONFIG_CPOL_Msk;
  }

  spis_->CONFIG = config;
  spis_->DEF = ((static_cast<uint32_t>(defaultChar) << SPIS_DEF_DEF_Pos) &
                SPIS_DEF_DEF_Msk);
  spis_->ORC = ((static_cast<uint32_t>(overReadChar) << SPIS_ORC_ORC_Pos) &
                SPIS_ORC_ORC_Msk);
  spis_->SHORTS =
      autoAcquireAfterEnd
          ? ((SPIS_SHORTS_END_ACQUIRE_Enabled << SPIS_SHORTS_END_ACQUIRE_Pos) &
             SPIS_SHORTS_END_ACQUIRE_Msk)
          : 0U;
  spis_->INTENCLR = 0xFFFFFFFFUL;
  spis_->EVENTS_END = 0U;
  spis_->EVENTS_ACQUIRED = 0U;
  spis_->EVENTS_DMA.RX.END = 0U;
  spis_->EVENTS_DMA.TX.END = 0U;

  spis_->ENABLE = ((SPIS_ENABLE_ENABLE_Enabled << SPIS_ENABLE_ENABLE_Pos) &
                   SPIS_ENABLE_ENABLE_Msk);
  active_ = true;
  clearStatus();
  return true;
}

bool Spis::acquire(uint32_t spinLimit) {
  if (!active_) {
    return false;
  }
  if ((spis_->SEMSTAT & SPIS_SEMSTAT_SEMSTAT_Msk) ==
      (SPIS_SEMSTAT_SEMSTAT_CPU << SPIS_SEMSTAT_SEMSTAT_Pos)) {
    return true;
  }

  spis_->EVENTS_ACQUIRED = 0U;
  spis_->TASKS_ACQUIRE = SPIS_TASKS_ACQUIRE_TASKS_ACQUIRE_Trigger;
  while (spinLimit-- > 0U) {
    if ((spis_->SEMSTAT & SPIS_SEMSTAT_SEMSTAT_Msk) ==
        (SPIS_SEMSTAT_SEMSTAT_CPU << SPIS_SEMSTAT_SEMSTAT_Pos)) {
      return true;
    }
    if (spis_->EVENTS_ACQUIRED != 0U) {
      spis_->EVENTS_ACQUIRED = 0U;
      return true;
    }
  }

  return (spis_->SEMSTAT & SPIS_SEMSTAT_SEMSTAT_Msk) ==
         (SPIS_SEMSTAT_SEMSTAT_CPU << SPIS_SEMSTAT_SEMSTAT_Pos);
}

bool Spis::setBuffers(uint8_t* rx, size_t rxLen, const uint8_t* tx, size_t txLen,
                      uint32_t spinLimit) {
  if (!active_ || rx == nullptr || tx == nullptr || rxLen == 0U || txLen == 0U ||
      rxLen > 0xFFFFU || txLen > 0xFFFFU) {
    return false;
  }
  if (!acquire(spinLimit)) {
    return false;
  }

  spis_->DMA.RX.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rx));
  spis_->DMA.RX.MAXCNT =
      (static_cast<uint32_t>(rxLen) << SPIS_DMA_RX_MAXCNT_MAXCNT_Pos) &
      SPIS_DMA_RX_MAXCNT_MAXCNT_Msk;
  spis_->DMA.TX.PTR = static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(const_cast<uint8_t*>(tx)));
  spis_->DMA.TX.MAXCNT =
      (static_cast<uint32_t>(txLen) << SPIS_DMA_TX_MAXCNT_MAXCNT_Pos) &
      SPIS_DMA_TX_MAXCNT_MAXCNT_Msk;

  spis_->EVENTS_END = 0U;
  spis_->EVENTS_DMA.RX.END = 0U;
  spis_->EVENTS_DMA.TX.END = 0U;
  clearStatus();
  return true;
}

bool Spis::releaseTransaction() {
  if (!active_) {
    return false;
  }
  spis_->TASKS_RELEASE = SPIS_TASKS_RELEASE_TASKS_RELEASE_Trigger;
  return true;
}

bool Spis::pollAcquired(bool clearEventFlag) {
  const bool fired = active_ && (spis_->EVENTS_ACQUIRED != 0U);
  if (fired && clearEventFlag) {
    spis_->EVENTS_ACQUIRED = 0U;
  }
  return fired;
}

bool Spis::pollEnd(bool clearEventFlag) {
  const bool fired = active_ && (spis_->EVENTS_END != 0U);
  if (fired && clearEventFlag) {
    spis_->EVENTS_END = 0U;
  }
  return fired;
}

size_t Spis::receivedBytes() const {
  if (!active_) {
    return 0U;
  }
  return static_cast<size_t>((spis_->DMA.RX.AMOUNT & SPIS_DMA_RX_AMOUNT_AMOUNT_Msk) >>
                             SPIS_DMA_RX_AMOUNT_AMOUNT_Pos);
}

size_t Spis::transmittedBytes() const {
  if (!active_) {
    return 0U;
  }
  return static_cast<size_t>((spis_->DMA.TX.AMOUNT & SPIS_DMA_TX_AMOUNT_AMOUNT_Msk) >>
                             SPIS_DMA_TX_AMOUNT_AMOUNT_Pos);
}

bool Spis::overflowed() const {
  return active_ && ((spis_->STATUS & SPIS_STATUS_OVERFLOW_Msk) != 0U);
}

bool Spis::overread() const {
  return active_ && ((spis_->STATUS & SPIS_STATUS_OVERREAD_Msk) != 0U);
}

void Spis::clearStatus() {
  if (spis_ == nullptr) {
    return;
  }
  spis_->STATUS = SPIS_STATUS_OVERREAD_Clear | SPIS_STATUS_OVERFLOW_Clear;
}

void Spis::end() {
  if (spis_ == nullptr) {
    return;
  }

  spis_->SHORTS = 0U;
  spis_->ENABLE = (SPIS_ENABLE_ENABLE_Disabled << SPIS_ENABLE_ENABLE_Pos);
  spis_->PSEL.SCK = PSEL_DISCONNECTED;
  spis_->PSEL.MOSI = PSEL_DISCONNECTED;
  spis_->PSEL.MISO = PSEL_DISCONNECTED;
  spis_->PSEL.CSN = PSEL_DISCONNECTED;
  active_ = false;
}

Twim::Twim(uint32_t base) : base_(base) {}

bool Twim::begin(const Pin& scl, const Pin& sda, TwimFrequency frequency) {
  if (!isConnected(scl) || !isConnected(sda)) {
    return false;
  }

  // Per datasheet, configure SCL/SDA for input and S0D1 drive before enabling TWIM.
  if (!Gpio::configure(scl, GpioDirection::kInput, GpioPull::kDisabled) ||
      !Gpio::configure(sda, GpioDirection::kInput, GpioPull::kDisabled) ||
      !Gpio::setDriveS0D1(scl) || !Gpio::setDriveS0D1(sda)) {
    return false;
  }

  reg32(base_ + twim::ENABLE) = twim::ENABLE_DISABLED;

  reg32(base_ + twim::PSEL_SCL) = make_psel(scl.port, scl.pin);
  reg32(base_ + twim::PSEL_SDA) = make_psel(sda.port, sda.pin);
  if (!setFrequency(frequency)) {
    return false;
  }

  reg32(base_ + twim::ENABLE) = twim::ENABLE_ENABLED;
  return true;
}

bool Twim::setFrequency(TwimFrequency frequency) {
  if (base_ == 0U) {
    return false;
  }
  reg32(base_ + twim::FREQUENCY) = static_cast<uint32_t>(frequency);
  return true;
}

void Twim::end() {
  reg32(base_ + twim::ENABLE) = twim::ENABLE_DISABLED;
  reg32(base_ + twim::PSEL_SCL) = PSEL_DISCONNECTED;
  reg32(base_ + twim::PSEL_SDA) = PSEL_DISCONNECTED;
}

bool Twim::write(uint8_t address7, const uint8_t* data, size_t len,
                 uint32_t spinLimit) {
  if (len > 0xFFFFU) {
    return false;
  }

  uint8_t dummy = 0;
  const uint8_t* tx = (data != nullptr) ? data : &dummy;

  clearTwimState(base_);
  reg32(base_ + twim::ADDRESS) = (address7 & 0x7FU);

  reg32(base_ + twim::DMA_TX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tx));
  reg32(base_ + twim::DMA_TX_MAXCNT) = static_cast<uint32_t>(len);

  reg32(base_ + twim::TASKS_DMA_TX_START) = 1;
  const uint32_t doneEvent = (len > 0U) ? twim::EVENTS_LASTTX
                                        : twim::EVENTS_DMA_TX_END;
  const bool writeOk = waitForEventOrError(base_, doneEvent, twim::EVENTS_ERROR,
                                           spinLimit);

  reg32(base_ + twim::TASKS_STOP) = 1;
  const bool stopOk = waitForEvent(base_, twim::EVENTS_STOPPED, spinLimit);

  const bool hadError = (reg32(base_ + twim::EVENTS_ERROR) != 0U);
  reg32(base_ + twim::ERRORSRC) = twim::ERRORSRC_ALL;

  return writeOk && stopOk && !hadError;
}

bool Twim::read(uint8_t address7, uint8_t* data, size_t len,
                uint32_t spinLimit) {
  if (data == nullptr || len == 0U || len > 0xFFFFU) {
    return false;
  }

  clearTwimState(base_);
  reg32(base_ + twim::ADDRESS) = (address7 & 0x7FU);

  reg32(base_ + twim::DMA_RX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
  reg32(base_ + twim::DMA_RX_MAXCNT) = static_cast<uint32_t>(len);

  reg32(base_ + twim::TASKS_DMA_RX_START) = 1;
  const bool readOk = waitForEventOrError(base_, twim::EVENTS_LASTRX,
                                          twim::EVENTS_ERROR, spinLimit);

  reg32(base_ + twim::TASKS_STOP) = 1;
  const bool stopOk = waitForEvent(base_, twim::EVENTS_STOPPED, spinLimit);

  const bool hadError = (reg32(base_ + twim::EVENTS_ERROR) != 0U);
  reg32(base_ + twim::ERRORSRC) = twim::ERRORSRC_ALL;

  return readOk && stopOk && !hadError;
}

bool Twim::writeRead(uint8_t address7, const uint8_t* tx, size_t txLen,
                     uint8_t* rx, size_t rxLen, uint32_t spinLimit) {
  if (txLen == 0U) {
    return read(address7, rx, rxLen, spinLimit);
  }
  if (rxLen == 0U) {
    return write(address7, tx, txLen, spinLimit);
  }
  if (tx == nullptr || rx == nullptr || txLen > 0xFFFFU || rxLen > 0xFFFFU) {
    return false;
  }

  clearTwimState(base_);
  reg32(base_ + twim::ADDRESS) = (address7 & 0x7FU);

  reg32(base_ + twim::DMA_TX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tx));
  reg32(base_ + twim::DMA_TX_MAXCNT) = static_cast<uint32_t>(txLen);

  reg32(base_ + twim::DMA_RX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rx));
  reg32(base_ + twim::DMA_RX_MAXCNT) = static_cast<uint32_t>(rxLen);

  reg32(base_ + twim::TASKS_DMA_TX_START) = 1;
  const bool writePhaseOk = waitForEventOrError(base_, twim::EVENTS_LASTTX,
                                                 twim::EVENTS_ERROR, spinLimit);

  if (!writePhaseOk) {
    reg32(base_ + twim::TASKS_STOP) = 1;
    waitForEvent(base_, twim::EVENTS_STOPPED, spinLimit);
    reg32(base_ + twim::ERRORSRC) = twim::ERRORSRC_ALL;
    return false;
  }

  reg32(base_ + twim::TASKS_DMA_RX_START) = 1;
  const bool readPhaseOk = waitForEventOrError(base_, twim::EVENTS_LASTRX,
                                                twim::EVENTS_ERROR, spinLimit);

  reg32(base_ + twim::TASKS_STOP) = 1;
  const bool stopOk = waitForEvent(base_, twim::EVENTS_STOPPED, spinLimit);

  const bool hadError = (reg32(base_ + twim::EVENTS_ERROR) != 0U);
  reg32(base_ + twim::ERRORSRC) = twim::ERRORSRC_ALL;

  return writePhaseOk && readPhaseOk && stopOk && !hadError;
}

Uarte::Uarte(uint32_t base) : base_(base) {}

bool Uarte::begin(const Pin& txd, const Pin& rxd, UarteBaud baud,
                  bool hwFlowControl, const Pin& cts, const Pin& rts) {
  if (!isConnected(txd) || !isConnected(rxd)) {
    return false;
  }
  if (hwFlowControl && (!isConnected(cts) || !isConnected(rts))) {
    return false;
  }

  if (!Gpio::configure(txd, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(rxd, GpioDirection::kInput, GpioPull::kDisabled)) {
    return false;
  }
  Gpio::write(txd, true);

  if (hwFlowControl) {
    if (!Gpio::configure(cts, GpioDirection::kInput, GpioPull::kDisabled) ||
        !Gpio::configure(rts, GpioDirection::kOutput, GpioPull::kDisabled)) {
      return false;
    }
    Gpio::write(rts, true);
  }

  reg32(base_ + uarte::ENABLE) = uarte::ENABLE_DISABLED;

  reg32(base_ + uarte::PSEL_TXD) = make_psel(txd.port, txd.pin);
  reg32(base_ + uarte::PSEL_RXD) = make_psel(rxd.port, rxd.pin);
  reg32(base_ + uarte::PSEL_CTS) =
      hwFlowControl ? make_psel(cts.port, cts.pin) : PSEL_DISCONNECTED;
  reg32(base_ + uarte::PSEL_RTS) =
      hwFlowControl ? make_psel(rts.port, rts.pin) : PSEL_DISCONNECTED;

  uint32_t config = reg32(base_ + uarte::CONFIG);
  if (hwFlowControl) {
    config |= uarte::CONFIG_HWFC_Msk;
  } else {
    config &= ~uarte::CONFIG_HWFC_Msk;
  }
  reg32(base_ + uarte::CONFIG) = config;

  reg32(base_ + uarte::BAUDRATE) = static_cast<uint32_t>(baud);
  reg32(base_ + uarte::ENABLE) = uarte::ENABLE_ENABLED;

  return true;
}

void Uarte::end() {
  reg32(base_ + uarte::ENABLE) = uarte::ENABLE_DISABLED;
  reg32(base_ + uarte::PSEL_TXD) = PSEL_DISCONNECTED;
  reg32(base_ + uarte::PSEL_RXD) = PSEL_DISCONNECTED;
  reg32(base_ + uarte::PSEL_CTS) = PSEL_DISCONNECTED;
  reg32(base_ + uarte::PSEL_RTS) = PSEL_DISCONNECTED;
}

bool Uarte::write(const uint8_t* data, size_t len, uint32_t spinLimit) {
  if (data == nullptr) {
    return false;
  }
  if (len == 0U) {
    return true;
  }
  if (len > 0xFFFFU) {
    return false;
  }

  clearEvent(base_, uarte::EVENTS_DMA_TX_END);
  clearEvent(base_, uarte::EVENTS_TXSTOPPED);
  clearEvent(base_, uarte::EVENTS_ERROR);
  reg32(base_ + uarte::ERRORSRC) = uarte::ERRORSRC_ALL;

  reg32(base_ + uarte::DMA_TX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
  reg32(base_ + uarte::DMA_TX_MAXCNT) = static_cast<uint32_t>(len);

  reg32(base_ + uarte::TASKS_DMA_TX_START) = 1;
  const bool txOk = waitForEvent(base_, uarte::EVENTS_DMA_TX_END, spinLimit);

  reg32(base_ + uarte::TASKS_DMA_TX_STOP) = 1;
  const bool stopped =
      waitForEvent(base_, uarte::EVENTS_TXSTOPPED, spinLimit);

  return txOk && stopped;
}

size_t Uarte::read(uint8_t* data, size_t len, uint32_t spinLimit) {
  if (data == nullptr || len == 0U || len > 0xFFFFU) {
    return 0;
  }

  clearEvent(base_, uarte::EVENTS_DMA_RX_END);
  clearEvent(base_, uarte::EVENTS_RXTO);
  clearEvent(base_, uarte::EVENTS_ERROR);
  reg32(base_ + uarte::ERRORSRC) = uarte::ERRORSRC_ALL;

  reg32(base_ + uarte::DMA_RX_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
  reg32(base_ + uarte::DMA_RX_MAXCNT) = static_cast<uint32_t>(len);

  reg32(base_ + uarte::TASKS_DMA_RX_START) = 1;

  bool rxDone = false;
  while (spinLimit-- > 0U) {
    if (reg32(base_ + uarte::EVENTS_DMA_RX_END) != 0U) {
      rxDone = true;
      break;
    }
    if (reg32(base_ + uarte::EVENTS_ERROR) != 0U) {
      break;
    }
  }

  reg32(base_ + uarte::TASKS_DMA_RX_STOP) = 1;
  waitForEvent(base_, uarte::EVENTS_RXTO, 2000000UL);

  const size_t amount = static_cast<size_t>(reg32(base_ + uarte::DMA_RX_AMOUNT));
  if (!rxDone && reg32(base_ + uarte::EVENTS_ERROR) != 0U) {
    reg32(base_ + uarte::ERRORSRC) = uarte::ERRORSRC_ALL;
  }

  return amount;
}

Timer::Timer(uint32_t base, uint32_t pclkHz, uint8_t channelCount)
    : base_(base),
      pclkHz_(pclkHz),
      channelCount_(channelCount),
      prescaler_(4),
      callbacks_{},
      callbackContext_{} {
  if (channelCount_ > 8U) {
    channelCount_ = 8U;
  }
}

bool Timer::begin(TimerBitWidth bitWidth, uint8_t prescaler, bool counterMode) {
  if (channelCount_ == 0U || prescaler > 9U) {
    return false;
  }

  stop();
  clear();

  reg32(base_ + timer::MODE) = counterMode ? timer::MODE_COUNTER : timer::MODE_TIMER;
  reg32(base_ + timer::BITMODE) = static_cast<uint32_t>(bitWidth);
  reg32(base_ + timer::PRESCALER) = static_cast<uint32_t>(prescaler);
  prescaler_ = prescaler;

  reg32(base_ + timer::SHORTS) = 0;
  reg32(base_ + timer::INTENCLR) = 0xFFFFFFFFUL;

  for (uint8_t ch = 0; ch < channelCount_; ++ch) {
    clearEvent(base_, timerCompareEventOffset(ch));
    reg32(base_ + timerOneShotOffset(ch)) = 0;
  }

  return true;
}

bool Timer::setFrequency(uint32_t targetHz) {
  if (targetHz == 0U || pclkHz_ == 0U) {
    return false;
  }

  uint8_t bestPrescaler = 0;
  uint32_t bestError = 0xFFFFFFFFUL;
  bool found = false;

  for (uint8_t prescaler = 0; prescaler <= 9U; ++prescaler) {
    const uint32_t hz = pclkHz_ >> prescaler;
    if (hz == 0U) {
      continue;
    }
    const uint32_t error = absDiffU32(hz, targetHz);
    if (!found || error < bestError) {
      found = true;
      bestError = error;
      bestPrescaler = prescaler;
      if (error == 0U) {
        break;
      }
    }
  }

  if (!found) {
    return false;
  }

  reg32(base_ + timer::PRESCALER) = bestPrescaler;
  prescaler_ = bestPrescaler;
  return true;
}

uint32_t Timer::timerHz() const {
  if (prescaler_ > 31U) {
    return 0;
  }
  return pclkHz_ >> prescaler_;
}

uint32_t Timer::ticksFromMicros(uint32_t us) const {
  const uint32_t hz = timerHz();
  if (hz == 0U || us == 0U) {
    return 0U;
  }

  const uint64_t ticks =
      (static_cast<uint64_t>(hz) * static_cast<uint64_t>(us) + 999999ULL) /
      1000000ULL;
  return static_cast<uint32_t>(ticks);
}

void Timer::start() { reg32(base_ + timer::TASKS_START) = 1; }

void Timer::stop() { reg32(base_ + timer::TASKS_STOP) = 1; }

void Timer::clear() { reg32(base_ + timer::TASKS_CLEAR) = 1; }

bool Timer::setCompare(uint8_t channel, uint32_t ccValue, bool autoClear,
                       bool autoStop, bool oneShot, bool enableInterruptFlag) {
  if (channel >= channelCount_) {
    return false;
  }

  reg32(base_ + timerCcOffset(channel)) = ccValue;
  reg32(base_ + timerOneShotOffset(channel)) = oneShot ? 1U : 0U;

  uint32_t shorts = reg32(base_ + timer::SHORTS);
  const uint32_t clearMask = (1UL << channel);
  const uint32_t stopMask = (1UL << (16U + channel));

  if (autoClear) {
    shorts |= clearMask;
  } else {
    shorts &= ~clearMask;
  }

  if (autoStop) {
    shorts |= stopMask;
  } else {
    shorts &= ~stopMask;
  }

  reg32(base_ + timer::SHORTS) = shorts;
  clearEvent(base_, timerCompareEventOffset(channel));
  enableInterrupt(channel, enableInterruptFlag);

  return true;
}

uint32_t Timer::capture(uint8_t channel) {
  if (channel >= channelCount_) {
    return 0U;
  }
  reg32(base_ + timerCaptureTaskOffset(channel)) = 1;
  return reg32(base_ + timerCcOffset(channel));
}

uint32_t Timer::ccValue(uint8_t channel) const {
  if (channel >= channelCount_) {
    return 0U;
  }
  return reg32(base_ + timerCcOffset(channel));
}

bool Timer::pollCompare(uint8_t channel, bool clearEventFlag) {
  if (channel >= channelCount_) {
    return false;
  }

  const uint32_t offset = timerCompareEventOffset(channel);
  const bool fired = (reg32(base_ + offset) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, offset);
  }
  return fired;
}

volatile uint32_t* Timer::publishCompareConfigRegister(uint8_t channel) const {
  if (channel >= channelCount_) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + timerPublishCompareOffset(channel)));
}

volatile uint32_t* Timer::subscribeStartConfigRegister() const {
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + timer::SUBSCRIBE_START));
}

volatile uint32_t* Timer::subscribeStopConfigRegister() const {
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + timer::SUBSCRIBE_STOP));
}

volatile uint32_t* Timer::subscribeCountConfigRegister() const {
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + timer::SUBSCRIBE_COUNT));
}

volatile uint32_t* Timer::subscribeClearConfigRegister() const {
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + timer::SUBSCRIBE_CLEAR));
}

volatile uint32_t* Timer::subscribeCaptureConfigRegister(uint8_t channel) const {
  if (channel >= channelCount_) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + timerSubscribeCaptureOffset(channel)));
}

void Timer::enableInterrupt(uint8_t channel, bool enable) {
  if (channel >= channelCount_) {
    return;
  }

  const uint32_t mask = timerCompareIntMask(channel);
  reg32(base_ + (enable ? timer::INTENSET : timer::INTENCLR)) = mask;
}

bool Timer::attachCompareCallback(uint8_t channel, CompareCallback callback,
                                  void* context) {
  if (channel >= channelCount_) {
    return false;
  }
  callbacks_[channel] = callback;
  callbackContext_[channel] = context;
  return true;
}

void Timer::service() {
  for (uint8_t ch = 0; ch < channelCount_; ++ch) {
    const uint32_t eventOffset = timerCompareEventOffset(ch);
    if (reg32(base_ + eventOffset) == 0U) {
      continue;
    }

    clearEvent(base_, eventOffset);
    if (callbacks_[ch] != nullptr) {
      callbacks_[ch](ch, callbackContext_[ch]);
    }
  }
}

uint16_t Pwm::clampCountertop(uint16_t countertop) {
  if (countertop < 3U) {
    return 3U;
  }
  if (countertop > 0x7FFFU) {
    return 0x7FFFU;
  }
  return countertop;
}

uint16_t Pwm::encodeSequenceWordTicks(uint16_t pulseTicks, bool activeHigh) {
  if (pulseTicks > 0x7FFFU) {
    pulseTicks = 0x7FFFU;
  }

  uint16_t word = static_cast<uint16_t>(pulseTicks & 0x7FFFU);
  if (!activeHigh) {
    word |= 0x8000U;
  }
  return word;
}

uint16_t Pwm::encodeSequenceWordPermille(uint16_t dutyPermille,
                                         uint16_t countertop,
                                         bool activeHigh) {
  countertop = clampCountertop(countertop);
  if (dutyPermille > 1000U) {
    dutyPermille = 1000U;
  }

  uint32_t pulse =
      (static_cast<uint32_t>(countertop) * dutyPermille + 500U) / 1000U;
  if (pulse > countertop) {
    pulse = countertop;
  }
  return encodeSequenceWordTicks(static_cast<uint16_t>(pulse), activeHigh);
}

Pwm::Pwm(uint32_t base)
    : base_(base),
      outPins_{kPinDisconnected, kPinDisconnected, kPinDisconnected,
               kPinDisconnected},
      dutyPermille_{500U, 500U, 500U, 500U},
      configuredMask_(0U),
      activeHighMask_(0x0FU),
      countertop_(1000),
      prescaler_(0),
      sequenceConfiguredMask_(0U),
      loadMode_(DecoderLoad::kIndividual),
      mode_(DecoderMode::kRefreshCount),
      counterMode_(CounterMode::kUp),
      highLevelManaged_(false),
      configured_(false),
      irqCallback_(nullptr),
      irqContext_(nullptr),
      sequence_{0, 0, 0, 0} {}

bool Pwm::beginRaw(const Pin* outPins, uint8_t channelCount, uint32_t frequencyHz,
                   DecoderLoad load, DecoderMode mode, uint8_t idleOutMask,
                   CounterMode counterMode) {
  if (outPins == nullptr || channelCount == 0U || channelCount > maxChannelCount()) {
    return false;
  }

  if (configured_) {
    end();
  }

  for (uint8_t ch = 0U; ch < maxChannelCount(); ++ch) {
    dutyPermille_[ch] = 500U;
    sequence_[ch] = 0U;
  }

  reg32(base_ + pwm::ENABLE) = pwm::ENABLE_DISABLED;
  reg32(base_ + pwm::SHORTS) = 0;
  counterMode_ = counterMode;
  reg32(base_ + pwm::MODE) = static_cast<uint32_t>(counterMode);

  if (!applyOutputRouting(outPins, channelCount, idleOutMask)) {
    configuredMask_ = 0U;
    return false;
  }

  if (!configureClockAndTop(frequencyHz)) {
    disconnectAllOutputs();
    configuredMask_ = 0U;
    return false;
  }

  reg32(base_ + pwm::DECODER) =
      (static_cast<uint32_t>(load) << 0U) |
      (static_cast<uint32_t>(mode) << 8U);
  reg32(base_ + pwm::LOOP) = 0U;

  for (uint8_t seq = 0U; seq < 2U; ++seq) {
    reg32(base_ + pwmDmaSeqPtrOffset(seq)) =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&sequence_[0]));
    reg32(base_ + pwmDmaSeqMaxCntOffset(seq)) = 0U;
    reg32(base_ + pwm::SEQ_REFRESH + (static_cast<uint32_t>(seq) * sizeof(uint32_t))) = 0U;
    reg32(base_ + pwm::SEQ_ENDDELAY + (static_cast<uint32_t>(seq) * sizeof(uint32_t))) =
        0U;
  }

  loadMode_ = load;
  mode_ = mode;
  sequenceConfiguredMask_ = 0U;
  highLevelManaged_ = false;
  reg32(base_ + pwm::ENABLE) = pwm::ENABLE_ENABLED;
  configured_ = true;
  return true;
}

bool Pwm::beginChannels(const ChannelConfig* channels, uint8_t channelCount,
                        uint32_t frequencyHz) {
  if (channels == nullptr || channelCount == 0U || channelCount > maxChannelCount()) {
    return false;
  }

  Pin outPins[4] = {kPinDisconnected, kPinDisconnected, kPinDisconnected,
                    kPinDisconnected};
  uint8_t newActiveHighMask = 0U;
  uint8_t idleOutMask = 0U;
  for (uint8_t ch = 0U; ch < maxChannelCount(); ++ch) {
    dutyPermille_[ch] = 500U;
  }

  for (uint8_t ch = 0U; ch < channelCount; ++ch) {
    const ChannelConfig& channel = channels[ch];
    if (!isConnected(channel.outPin)) {
      return false;
    }
    for (uint8_t prev = 0U; prev < ch; ++prev) {
      if (channels[prev].outPin.port == channel.outPin.port &&
          channels[prev].outPin.pin == channel.outPin.pin) {
        return false;
      }
    }

    outPins[ch] = channel.outPin;
    dutyPermille_[ch] = (channel.dutyPermille > 1000U) ? 1000U : channel.dutyPermille;
    if (channel.activeHigh) {
      newActiveHighMask |= static_cast<uint8_t>(1U << ch);
    } else {
      idleOutMask |= static_cast<uint8_t>(1U << ch);
    }
  }

  if (!beginRaw(outPins, channelCount, frequencyHz, DecoderLoad::kIndividual,
                DecoderMode::kRefreshCount, idleOutMask, CounterMode::kUp)) {
    return false;
  }

  activeHighMask_ = newActiveHighMask;
  highLevelManaged_ = true;
  updateSequenceWords();
  if (!setSequence(0U, sequence_, 4U) || !setSequence(1U, sequence_, 4U)) {
    end();
    return false;
  }
  return true;
}

bool Pwm::beginSingle(const Pin& outPin, uint32_t frequencyHz, uint16_t dutyPermille,
                      bool activeHigh) {
  const ChannelConfig channel = {outPin, dutyPermille, activeHigh};
  return beginChannels(&channel, 1U, frequencyHz);
}

bool Pwm::setDutyPermille(uint16_t dutyPermille) {
  if (!highLevelManaged_) {
    return false;
  }

  const uint16_t clampedDuty = (dutyPermille > 1000U) ? 1000U : dutyPermille;
  for (uint8_t ch = 0U; ch < maxChannelCount(); ++ch) {
    if (configuredMask_ == 0U || channelConfigured(ch)) {
      dutyPermille_[ch] = clampedDuty;
    }
  }
  if (!configured_) {
    return false;
  }
  updateSequenceWords();
  return true;
}

bool Pwm::setDutyPermille(uint8_t channel, uint16_t dutyPermille) {
  if (!highLevelManaged_ || !channelConfigured(channel)) {
    return false;
  }

  dutyPermille_[channel] = (dutyPermille > 1000U) ? 1000U : dutyPermille;
  if (!configured_) {
    return false;
  }
  updateSequenceWords();
  return true;
}

bool Pwm::setActiveHigh(uint8_t channel, bool activeHigh) {
  if (!highLevelManaged_ || !channelConfigured(channel)) {
    return false;
  }

  const uint8_t bit = static_cast<uint8_t>(1U << channel);
  if (activeHigh) {
    activeHighMask_ |= bit;
  } else {
    activeHighMask_ &= static_cast<uint8_t>(~bit);
  }

  if (!configured_) {
    return false;
  }

  uint32_t idleOut = reg32(base_ + pwm::IDLEOUT);
  if (activeHigh) {
    idleOut &= ~(1UL << channel);
  } else {
    idleOut |= (1UL << channel);
  }
  reg32(base_ + pwm::IDLEOUT) = idleOut;
  updateSequenceWords();
  return true;
}

bool Pwm::setSequence(uint8_t sequence, const uint16_t* words, uint16_t wordCount,
                      uint32_t refreshCount, uint32_t endDelay) {
  if (!configured_ || sequence > 1U || words == nullptr || wordCount == 0U) {
    return false;
  }

  reg32(base_ + pwmDmaSeqPtrOffset(sequence)) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(words));
  reg32(base_ + pwmDmaSeqMaxCntOffset(sequence)) =
      static_cast<uint32_t>(wordCount * sizeof(uint16_t));
  reg32(base_ + pwm::SEQ_REFRESH + (static_cast<uint32_t>(sequence) * sizeof(uint32_t))) =
      refreshCount;
  reg32(base_ + pwm::SEQ_ENDDELAY + (static_cast<uint32_t>(sequence) * sizeof(uint32_t))) =
      endDelay;
  sequenceConfiguredMask_ |= static_cast<uint8_t>(1U << sequence);
  return true;
}

bool Pwm::setLoopCount(uint16_t loopCount) {
  if (!configured_) {
    return false;
  }

  reg32(base_ + pwm::LOOP) = loopCount;
  return true;
}

bool Pwm::setCounterMode(CounterMode mode) {
  if (!configured_) {
    return false;
  }

  reg32(base_ + pwm::MODE) = static_cast<uint32_t>(mode);
  counterMode_ = mode;
  return true;
}

bool Pwm::setFrequency(uint32_t frequencyHz) {
  if (!configured_) {
    return false;
  }

  if (!configureClockAndTop(frequencyHz)) {
    return false;
  }

  updateSequenceWords();
  return true;
}

bool Pwm::triggerNextStep() {
  if (!configured_ || mode_ != DecoderMode::kNextStep) {
    return false;
  }

  reg32(base_ + pwm::TASKS_NEXTSTEP) = 1U;
  return true;
}

bool Pwm::channelConfigured(uint8_t channel) const {
  if (channel >= maxChannelCount()) {
    return false;
  }
  return (configuredMask_ & static_cast<uint8_t>(1U << channel)) != 0U;
}

uint8_t Pwm::configuredChannelMask() const { return configuredMask_; }
uint16_t Pwm::countertop() const { return countertop_; }
uint8_t Pwm::prescaler() const { return prescaler_; }
Pwm::DecoderLoad Pwm::decoderLoad() const { return loadMode_; }
Pwm::DecoderMode Pwm::decoderMode() const { return mode_; }
Pwm::CounterMode Pwm::counterMode() const { return counterMode_; }

volatile uint32_t* Pwm::publishStoppedConfigRegister() const {
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + pwm::PUBLISH_STOPPED));
}

volatile uint32_t* Pwm::publishSequenceStartedConfigRegister(uint8_t sequence) const {
  if (sequence > 1U) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(
      base_ + pwm::PUBLISH_SEQSTARTED +
      (static_cast<uint32_t>(sequence) * pwm::REGISTER_STRIDE)));
}

volatile uint32_t* Pwm::publishSequenceEndConfigRegister(uint8_t sequence) const {
  if (sequence > 1U) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(
      base_ + pwm::PUBLISH_SEQEND +
      (static_cast<uint32_t>(sequence) * pwm::REGISTER_STRIDE)));
}

volatile uint32_t* Pwm::publishPeriodEndConfigRegister() const {
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + pwm::PUBLISH_PWMPERIODEND));
}

volatile uint32_t* Pwm::publishLoopsDoneConfigRegister() const {
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + pwm::PUBLISH_LOOPSDONE));
}

volatile uint32_t* Pwm::publishRamUnderflowConfigRegister() const {
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + pwm::PUBLISH_RAMUNDERFLOW));
}

volatile uint32_t* Pwm::publishCompareMatchConfigRegister(uint8_t channel) const {
  if (channel >= maxChannelCount()) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(
      base_ + pwm::PUBLISH_COMPAREMATCH +
      (static_cast<uint32_t>(channel) * pwm::REGISTER_STRIDE)));
}

volatile uint32_t* Pwm::subscribeStopConfigRegister() const {
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + pwm::SUBSCRIBE_STOP));
}

volatile uint32_t* Pwm::subscribeNextStepConfigRegister() const {
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + pwm::SUBSCRIBE_NEXTSTEP));
}

volatile uint32_t* Pwm::subscribeSequenceStartConfigRegister(uint8_t sequence) const {
  if (sequence > 1U) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(
      base_ + pwm::SUBSCRIBE_DMA_SEQ_START +
      (pwmTaskSeqStartOffset(sequence) - pwm::TASKS_DMA_SEQ_START)));
}

uint32_t Pwm::pendingInterruptMask() const {
  if (!configured_) {
    return 0U;
  }
  return reg32(base_ + pwm::INTPEND) & irqSupportedMask();
}

void Pwm::clearInterruptEvents(uint32_t mask) {
  if (!configured_) {
    return;
  }

  mask &= irqSupportedMask();
  if ((mask & kIrqStopped) != 0U) {
    clearEvent(base_, pwm::EVENTS_STOPPED);
  }
  if ((mask & irqSequenceStartedMask(0U)) != 0U) {
    clearEvent(base_, pwmEventSeqStartedOffset(0U));
  }
  if ((mask & irqSequenceStartedMask(1U)) != 0U) {
    clearEvent(base_, pwmEventSeqStartedOffset(1U));
  }
  if ((mask & irqSequenceEndMask(0U)) != 0U) {
    clearEvent(base_, pwmEventSeqEndOffset(0U));
  }
  if ((mask & irqSequenceEndMask(1U)) != 0U) {
    clearEvent(base_, pwmEventSeqEndOffset(1U));
  }
  if ((mask & kIrqPeriodEnd) != 0U) {
    clearEvent(base_, pwm::EVENTS_PWMPERIODEND);
  }
  if ((mask & kIrqLoopsDone) != 0U) {
    clearEvent(base_, pwm::EVENTS_LOOPSDONE);
  }
  if ((mask & kIrqRamUnderflow) != 0U) {
    clearEvent(base_, pwm::EVENTS_RAMUNDERFLOW);
  }
  if ((mask & irqDmaSequenceEndMask(0U)) != 0U) {
    clearEvent(base_, pwmEventDmaSeqEndOffset(0U));
  }
  if ((mask & irqDmaSequenceReadyMask(0U)) != 0U) {
    clearEvent(base_, pwmEventDmaSeqReadyOffset(0U));
  }
  if ((mask & irqDmaSequenceBusErrorMask(0U)) != 0U) {
    clearEvent(base_, pwmEventDmaSeqBusErrorOffset(0U));
  }
  if ((mask & irqDmaSequenceEndMask(1U)) != 0U) {
    clearEvent(base_, pwmEventDmaSeqEndOffset(1U));
  }
  if ((mask & irqDmaSequenceReadyMask(1U)) != 0U) {
    clearEvent(base_, pwmEventDmaSeqReadyOffset(1U));
  }
  if ((mask & irqDmaSequenceBusErrorMask(1U)) != 0U) {
    clearEvent(base_, pwmEventDmaSeqBusErrorOffset(1U));
  }
  for (uint8_t ch = 0U; ch < maxChannelCount(); ++ch) {
    if ((mask & irqCompareMatchMask(ch)) == 0U) {
      continue;
    }
    clearEvent(base_, pwm::EVENTS_COMPAREMATCH +
                          (static_cast<uint32_t>(ch) * pwm::REGISTER_STRIDE));
  }
}

void Pwm::enableInterruptMask(uint32_t mask, bool enable) {
  if (!configured_) {
    return;
  }

  mask &= irqSupportedMask();
  if (mask == 0U) {
    return;
  }

  if (enable) {
    clearInterruptEvents(mask);
  }
  reg32(base_ + (enable ? pwm::INTENSET : pwm::INTENCLR)) = mask;
}

void Pwm::setIrqCallback(IrqCallback callback, void* context) {
  irqCallback_ = callback;
  irqContext_ = context;
}

bool Pwm::makeActive(uint8_t irqPriority) {
  if (!configured_) {
    return false;
  }

  Pwm** slot = pwmActiveSlotForBase(base_);
  const int32_t irqNumber = pwmIrqNumberForBase(base_);
  if (slot == nullptr || irqNumber < 0) {
    return false;
  }

  *slot = this;
  NVIC_ClearPendingIRQ(static_cast<IRQn_Type>(irqNumber));
  NVIC_SetPriority(static_cast<IRQn_Type>(irqNumber), irqPriority);
  NVIC_EnableIRQ(static_cast<IRQn_Type>(irqNumber));
  return true;
}

void Pwm::onIrq() {
  if (!configured_) {
    return;
  }

  const uint32_t pending = pendingInterruptMask();
  if (pending == 0U) {
    return;
  }

  clearInterruptEvents(pending);
  if (irqCallback_ != nullptr) {
    irqCallback_(pending, irqContext_);
  }
}

bool Pwm::start(uint8_t sequence, uint32_t spinLimit) {
  if (!configured_ || sequence > 1U ||
      (sequenceConfiguredMask_ & static_cast<uint8_t>(1U << sequence)) == 0U) {
    return false;
  }

  uint32_t shorts = 0;
  if (sequence == 0U) {
    shorts |= (1UL << 2U);  // LOOPSDONE -> TASKS_DMA.SEQ[0].START
    shorts |= (1UL << 6U);  // DMA_SEQ0 bus error -> STOP
  } else {
    shorts |= (1UL << 3U);  // LOOPSDONE -> TASKS_DMA.SEQ[1].START
    shorts |= (1UL << 7U);  // DMA_SEQ1 bus error -> STOP
  }
  reg32(base_ + pwm::SHORTS) = shorts;

  clearEvent(base_, pwm::EVENTS_STOPPED);
  clearEvent(base_, pwm::EVENTS_LOOPSDONE);
  clearEvent(base_, pwm::EVENTS_PWMPERIODEND);
  clearEvent(base_, pwm::EVENTS_RAMUNDERFLOW);
  for (uint8_t ch = 0U; ch < maxChannelCount(); ++ch) {
    clearEvent(base_, pwm::EVENTS_COMPAREMATCH +
                          (static_cast<uint32_t>(ch) * pwm::REGISTER_STRIDE));
  }
  for (uint8_t seq = 0U; seq < 2U; ++seq) {
    clearEvent(base_, pwmEventSeqStartedOffset(seq));
    clearEvent(base_, pwmEventSeqEndOffset(seq));
    clearEvent(base_, pwmEventDmaSeqEndOffset(seq));
    clearEvent(base_, pwmEventDmaSeqReadyOffset(seq));
    clearEvent(base_, pwmEventDmaSeqBusErrorOffset(seq));
  }

  reg32(base_ + pwmTaskSeqStartOffset(sequence)) = 1;
  return waitForEvent(base_, pwmEventSeqStartedOffset(sequence), spinLimit);
}

bool Pwm::stop(uint32_t spinLimit) {
  if (!configured_) {
    return false;
  }

  clearEvent(base_, pwm::EVENTS_STOPPED);
  reg32(base_ + pwm::TASKS_STOP) = 1;
  reg32(base_ + pwm::SHORTS) = 0;

  return waitForEvent(base_, pwm::EVENTS_STOPPED, spinLimit);
}

void Pwm::end() {
  if (configured_) {
    enableInterruptMask(irqSupportedMask(), false);
    clearInterruptEvents(irqSupportedMask());
    stop(200000UL);
  }

  Pwm** slot = pwmActiveSlotForBase(base_);
  const int32_t irqNumber = pwmIrqNumberForBase(base_);
  if (slot != nullptr && *slot == this) {
    *slot = nullptr;
    if (irqNumber >= 0) {
      NVIC_DisableIRQ(static_cast<IRQn_Type>(irqNumber));
      NVIC_ClearPendingIRQ(static_cast<IRQn_Type>(irqNumber));
    }
  }

  reg32(base_ + pwm::ENABLE) = pwm::ENABLE_DISABLED;
  reg32(base_ + pwm::SHORTS) = 0;
  disconnectAllOutputs();

  configured_ = false;
  configuredMask_ = 0U;
  activeHighMask_ = 0x0FU;
  sequenceConfiguredMask_ = 0U;
  loadMode_ = DecoderLoad::kIndividual;
  mode_ = DecoderMode::kRefreshCount;
  counterMode_ = CounterMode::kUp;
  highLevelManaged_ = false;
  for (uint8_t ch = 0U; ch < maxChannelCount(); ++ch) {
    outPins_[ch] = kPinDisconnected;
    dutyPermille_[ch] = 500U;
    sequence_[ch] = 0U;
  }
}

bool Pwm::pollPeriodEnd(bool clearEventFlag) {
  const bool fired = (reg32(base_ + pwm::EVENTS_PWMPERIODEND) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, pwm::EVENTS_PWMPERIODEND);
  }
  return fired;
}

bool Pwm::pollCompareMatch(uint8_t channel, bool clearEventFlag) {
  if (channel >= maxChannelCount()) {
    return false;
  }

  const uint32_t offset =
      pwm::EVENTS_COMPAREMATCH +
      (static_cast<uint32_t>(channel) * pwm::REGISTER_STRIDE);
  const bool fired = (reg32(base_ + offset) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, offset);
  }
  return fired;
}

bool Pwm::pollSequenceStarted(uint8_t sequence, bool clearEventFlag) {
  if (sequence > 1U) {
    return false;
  }

  const bool fired = (reg32(base_ + pwmEventSeqStartedOffset(sequence)) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, pwmEventSeqStartedOffset(sequence));
  }
  return fired;
}

bool Pwm::pollSequenceEnd(uint8_t sequence, bool clearEventFlag) {
  if (sequence > 1U) {
    return false;
  }

  const bool fired = (reg32(base_ + pwmEventSeqEndOffset(sequence)) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, pwmEventSeqEndOffset(sequence));
  }
  return fired;
}

bool Pwm::pollDmaSequenceEnd(uint8_t sequence, bool clearEventFlag) {
  if (sequence > 1U) {
    return false;
  }

  const bool fired = (reg32(base_ + pwmEventDmaSeqEndOffset(sequence)) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, pwmEventDmaSeqEndOffset(sequence));
  }
  return fired;
}

bool Pwm::pollDmaSequenceReady(uint8_t sequence, bool clearEventFlag) {
  if (sequence > 1U) {
    return false;
  }

  const bool fired = (reg32(base_ + pwmEventDmaSeqReadyOffset(sequence)) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, pwmEventDmaSeqReadyOffset(sequence));
  }
  return fired;
}

bool Pwm::pollDmaSequenceBusError(uint8_t sequence, bool clearEventFlag) {
  if (sequence > 1U) {
    return false;
  }

  const bool fired = (reg32(base_ + pwmEventDmaSeqBusErrorOffset(sequence)) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, pwmEventDmaSeqBusErrorOffset(sequence));
  }
  return fired;
}

bool Pwm::pollLoopsDone(bool clearEventFlag) {
  const bool fired = (reg32(base_ + pwm::EVENTS_LOOPSDONE) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, pwm::EVENTS_LOOPSDONE);
  }
  return fired;
}

bool Pwm::pollRamUnderflow(bool clearEventFlag) {
  const bool fired = (reg32(base_ + pwm::EVENTS_RAMUNDERFLOW) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, pwm::EVENTS_RAMUNDERFLOW);
  }
  return fired;
}

uint32_t Pwm::sequenceConfiguredBytes(uint8_t sequence) const {
  if (sequence > 1U) {
    return 0U;
  }
  return reg32(base_ + pwmDmaSeqMaxCntOffset(sequence));
}

uint32_t Pwm::sequenceTransferredBytes(uint8_t sequence) const {
  if (sequence > 1U) {
    return 0U;
  }
  return reg32(base_ + pwmDmaSeqAmountOffset(sequence));
}

uint32_t Pwm::sequenceCurrentTransferredBytes(uint8_t sequence) const {
  if (sequence > 1U) {
    return 0U;
  }
  return reg32(base_ + pwmDmaSeqCurrentAmountOffset(sequence));
}

uint32_t Pwm::sequenceBusErrorAddress(uint8_t sequence) const {
  if (sequence > 1U) {
    return 0U;
  }
  return reg32(base_ + pwmDmaSeqBusErrorAddressOffset(sequence));
}

bool Pwm::applyOutputRouting(const Pin* outPins, uint8_t channelCount,
                             uint8_t idleOutMask) {
  uint8_t newConfiguredMask = 0U;
  for (uint8_t ch = 0U; ch < maxChannelCount(); ++ch) {
    outPins_[ch] = kPinDisconnected;
  }

  for (uint8_t ch = 0U; ch < channelCount; ++ch) {
    const Pin& pin = outPins[ch];
    if (!isConnected(pin)) {
      return false;
    }
    for (uint8_t prev = 0U; prev < ch; ++prev) {
      if (outPins[prev].port == pin.port && outPins[prev].pin == pin.pin) {
        return false;
      }
    }
    outPins_[ch] = pin;
    newConfiguredMask |= static_cast<uint8_t>(1U << ch);
  }

  configuredMask_ = newConfiguredMask;
  activeHighMask_ =
      static_cast<uint8_t>(configuredMask_ &
                           static_cast<uint8_t>(~(idleOutMask & configuredMask_)));

  for (uint8_t ch = 0U; ch < maxChannelCount(); ++ch) {
    if (!channelConfigured(ch)) {
      continue;
    }
    if (!Gpio::configure(outPins_[ch], GpioDirection::kOutput, GpioPull::kDisabled)) {
      configuredMask_ = 0U;
      return false;
    }
    Gpio::write(outPins_[ch],
                (idleOutMask & static_cast<uint8_t>(1U << ch)) != 0U);
  }

  disconnectAllOutputs();
  for (uint8_t ch = 0U; ch < maxChannelCount(); ++ch) {
    if (!channelConfigured(ch)) {
      continue;
    }
    reg32(base_ + pwm::PSEL_OUT + (static_cast<uint32_t>(ch) * sizeof(uint32_t))) =
        make_psel(outPins_[ch].port, outPins_[ch].pin);
  }
  reg32(base_ + pwm::IDLEOUT) = static_cast<uint32_t>(idleOutMask & configuredMask_);
  return true;
}

bool Pwm::configureClockAndTop(uint32_t frequencyHz) {
  uint32_t effectiveFrequencyHz = frequencyHz;
  if (counterMode_ == CounterMode::kUpDown) {
    if (effectiveFrequencyHz > (0xFFFFFFFFUL / 2UL)) {
      return false;
    }
    effectiveFrequencyHz *= 2UL;
  }

  PwmTiming timing{};
  if (!computePwmTiming(effectiveFrequencyHz, &timing)) {
    return false;
  }

  prescaler_ = timing.prescaler;
  countertop_ = timing.countertop;

  reg32(base_ + pwm::PRESCALER) = prescaler_;
  reg32(base_ + pwm::COUNTERTOP) = countertop_;
  return true;
}

void Pwm::disconnectAllOutputs() {
  for (uint8_t ch = 0U; ch < maxChannelCount(); ++ch) {
    reg32(base_ + pwm::PSEL_OUT + (static_cast<uint32_t>(ch) * sizeof(uint32_t))) =
        PSEL_DISCONNECTED;
  }
}

void Pwm::updateSequenceWords() {
  if (!highLevelManaged_) {
    return;
  }

  countertop_ = clampCountertop(countertop_);

  for (uint8_t ch = 0U; ch < maxChannelCount(); ++ch) {
    if (!channelConfigured(ch)) {
      sequence_[ch] = 0U;
      continue;
    }

    uint32_t pulse =
        (static_cast<uint32_t>(countertop_) * dutyPermille_[ch] + 500U) / 1000U;
    if (pulse > countertop_) {
      pulse = countertop_;
    }

    uint16_t word = static_cast<uint16_t>(pulse & 0x7FFFU);
    if ((activeHighMask_ & static_cast<uint8_t>(1U << ch)) == 0U) {
      word |= 0x8000U;
    }
    sequence_[ch] = word;
  }
}

Gpiote::Gpiote(uint32_t base, uint8_t channelCount)
    : base_(base), channelCount_(channelCount), callbacks_{}, callbackContext_{} {
  if (channelCount_ > 8U) {
    channelCount_ = 8U;
  }
}

bool Gpiote::configureEvent(uint8_t channel, const Pin& pin, GpiotePolarity polarity,
                            bool enableInterruptFlag) {
  if (channel >= channelCount_ || !isConnected(pin)) {
    return false;
  }

  if (!Gpio::configure(pin, GpioDirection::kInput, GpioPull::kDisabled)) {
    return false;
  }

  uint32_t config = 0;
  config |= (gpiote::MODE_EVENT << gpiote::CONFIG_MODE_Pos);
  config |= (static_cast<uint32_t>(pin.pin) << gpiote::CONFIG_PSEL_Pos);
  config |= (static_cast<uint32_t>(pin.port) << gpiote::CONFIG_PORT_Pos);
  config |= (static_cast<uint32_t>(polarity) << gpiote::CONFIG_POLARITY_Pos);

  reg32(base_ + gpioteConfigOffset(channel)) = config;
  clearEvent(base_, gpioteInEventOffset(channel));

  if (enableInterruptFlag) {
    enableInterrupt(channel, true);
  }
  return true;
}

bool Gpiote::configureTask(uint8_t channel, const Pin& pin, GpiotePolarity polarity,
                           bool initialHigh) {
  if (channel >= channelCount_ || !isConnected(pin)) {
    return false;
  }

  if (!Gpio::configure(pin, GpioDirection::kOutput, GpioPull::kDisabled)) {
    return false;
  }
  Gpio::write(pin, initialHigh);

  uint32_t config = 0;
  config |= (gpiote::MODE_TASK << gpiote::CONFIG_MODE_Pos);
  config |= (static_cast<uint32_t>(pin.pin) << gpiote::CONFIG_PSEL_Pos);
  config |= (static_cast<uint32_t>(pin.port) << gpiote::CONFIG_PORT_Pos);
  config |= (static_cast<uint32_t>(polarity) << gpiote::CONFIG_POLARITY_Pos);
  if (initialHigh) {
    config |= (1UL << gpiote::CONFIG_OUTINIT_Pos);
  }

  reg32(base_ + gpioteConfigOffset(channel)) = config;
  return true;
}

void Gpiote::disableChannel(uint8_t channel) {
  if (channel >= channelCount_) {
    return;
  }
  reg32(base_ + gpioteConfigOffset(channel)) = 0;
  enableInterrupt(channel, false);
  clearEvent(base_, gpioteInEventOffset(channel));
}

bool Gpiote::triggerTaskOut(uint8_t channel) {
  if (channel >= channelCount_) {
    return false;
  }
  reg32(base_ + gpioteTaskOutOffset(channel)) = 1;
  return true;
}

bool Gpiote::triggerTaskSet(uint8_t channel) {
  if (channel >= channelCount_) {
    return false;
  }
  reg32(base_ + gpioteTaskSetOffset(channel)) = 1;
  return true;
}

bool Gpiote::triggerTaskClr(uint8_t channel) {
  if (channel >= channelCount_) {
    return false;
  }
  reg32(base_ + gpioteTaskClrOffset(channel)) = 1;
  return true;
}

bool Gpiote::pollInEvent(uint8_t channel, bool clearEventFlag) {
  if (channel >= channelCount_) {
    return false;
  }

  const uint32_t offset = gpioteInEventOffset(channel);
  const bool fired = (reg32(base_ + offset) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, offset);
  }
  return fired;
}

bool Gpiote::pollPortEvent(bool clearEventFlag) {
  const bool fired = (reg32(base_ + gpiote::EVENTS_PORT_NONSECURE) != 0U);
  if (fired && clearEventFlag) {
    clearEvent(base_, gpiote::EVENTS_PORT_NONSECURE);
  }
  return fired;
}

volatile uint32_t* Gpiote::subscribeTaskOutConfigRegister(uint8_t channel) const {
  if (channel >= channelCount_) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + gpioteSubscribeOutOffset(channel)));
}

volatile uint32_t* Gpiote::subscribeTaskSetConfigRegister(uint8_t channel) const {
  if (channel >= channelCount_) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + gpioteSubscribeSetOffset(channel)));
}

volatile uint32_t* Gpiote::subscribeTaskClrConfigRegister(uint8_t channel) const {
  if (channel >= channelCount_) {
    return nullptr;
  }
  return reinterpret_cast<volatile uint32_t*>(
      static_cast<uintptr_t>(base_ + gpioteSubscribeClrOffset(channel)));
}

void Gpiote::enableInterrupt(uint8_t channel, bool enable) {
  if (channel >= channelCount_) {
    return;
  }

  const uint32_t mask = (1UL << channel);
  reg32(base_ + (enable ? gpiote::INTENSET0 : gpiote::INTENCLR0)) = mask;
}

bool Gpiote::attachInCallback(uint8_t channel, InCallback callback, void* context) {
  if (channel >= channelCount_) {
    return false;
  }
  callbacks_[channel] = callback;
  callbackContext_[channel] = context;
  return true;
}

void Gpiote::service() {
  for (uint8_t ch = 0; ch < channelCount_; ++ch) {
    const uint32_t offset = gpioteInEventOffset(ch);
    if (reg32(base_ + offset) == 0U) {
      continue;
    }

    clearEvent(base_, offset);
    if (callbacks_[ch] != nullptr) {
      callbacks_[ch](ch, callbackContext_[ch]);
    }
  }
}

Dppic::Dppic(uint32_t base)
    : dppic_(reinterpret_cast<NRF_DPPIC_Type*>(static_cast<uintptr_t>(base))) {}

bool Dppic::enableChannel(uint8_t channel, bool enable) {
  if (channel >= 24U || dppic_ == nullptr) {
    return false;
  }

  const uint32_t mask = (1UL << channel);
  if (enable) {
    dppic_->CHENSET = mask;
  } else {
    dppic_->CHENCLR = mask;
  }
  return true;
}

bool Dppic::channelEnabled(uint8_t channel) const {
  if (channel >= 24U || dppic_ == nullptr) {
    return false;
  }
  return (dppic_->CHEN & (1UL << channel)) != 0U;
}

bool Dppic::configureChannelGroup(uint8_t group, uint32_t channelMask) const {
  if (group >= DPPIC_CHG_MaxCount || dppic_ == nullptr) {
    return false;
  }
  dppic_->CHG[group] = channelMask;
  return true;
}

bool Dppic::configurePublish(volatile uint32_t* publishRegister, uint8_t channel,
                             bool enable) const {
  if (publishRegister == nullptr || channel >= 24U) {
    return false;
  }
  *publishRegister = enable ? nrf54l15::dppic::configValue(channel) : 0U;
  return true;
}

bool Dppic::configureSubscribe(volatile uint32_t* subscribeRegister, uint8_t channel,
                               bool enable) const {
  if (subscribeRegister == nullptr || channel >= 24U) {
    return false;
  }
  *subscribeRegister = enable ? nrf54l15::dppic::configValue(channel) : 0U;
  return true;
}

bool Dppic::connect(volatile uint32_t* publishRegister,
                    volatile uint32_t* subscribeRegister,
                    uint8_t channel,
                    bool enableChannelFlag) const {
  if (!configurePublish(publishRegister, channel, true) ||
      !configureSubscribe(subscribeRegister, channel, true)) {
    return false;
  }

  if (enableChannelFlag && dppic_ != nullptr) {
    dppic_->CHENSET = (1UL << channel);
  }
  return true;
}

bool Dppic::disconnectPublish(volatile uint32_t* publishRegister) const {
  if (publishRegister == nullptr) {
    return false;
  }
  *publishRegister = 0U;
  return true;
}

bool Dppic::disconnectSubscribe(volatile uint32_t* subscribeRegister) const {
  if (subscribeRegister == nullptr) {
    return false;
  }
  *subscribeRegister = 0U;
  return true;
}
