I2sTx::I2sTx(uint32_t base)
    : i2s_(reinterpret_cast<NRF_I2S_Type*>(static_cast<uintptr_t>(base))),
      config_(),
      buffers_{nullptr, nullptr},
      wordCount_(0U),
      refillCallback_(nullptr),
      refillContext_(nullptr),
      nextBufferIndex_(1U),
      configured_(false),
      running_(false),
      restartPending_(false),
      txPtrUpdCount_(0U),
      stoppedCount_(0U),
      restartCount_(0U),
      manualStopCount_(0U) {}

bool I2sTx::setBuffers(uint32_t* buffer0, uint32_t* buffer1, uint32_t wordCount) {
  if (buffer0 == nullptr || buffer1 == nullptr || wordCount == 0U) {
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(buffer0) & 0x3U) != 0U ||
      (reinterpret_cast<uintptr_t>(buffer1) & 0x3U) != 0U) {
    return false;
  }
  if (wordCount >
      (I2S_RXTXD_MAXCNT_MAXCNT_Msk >> I2S_RXTXD_MAXCNT_MAXCNT_Pos)) {
    return false;
  }

  buffers_[0] = buffer0;
  buffers_[1] = buffer1;
  wordCount_ = wordCount;
  return true;
}

void I2sTx::setRefillCallback(RefillCallback callback, void* context) {
  refillCallback_ = callback;
  refillContext_ = context;
}

bool I2sTx::begin(const I2sTxConfig& config, uint32_t* buffer0, uint32_t* buffer1,
                  uint32_t wordCount) {
  if (i2s_ == nullptr || !setBuffers(buffer0, buffer1, wordCount)) {
    return false;
  }
  if (!isConnected(config.mck) || !isConnected(config.sck) ||
      !isConnected(config.lrck) || !isConnected(config.sdout)) {
    return false;
  }
  if (!Gpio::configure(config.mck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.lrck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sdout, GpioDirection::kOutput, GpioPull::kDisabled)) {
    return false;
  }
  if (isConnected(config.sdin) &&
      !Gpio::configure(config.sdin, GpioDirection::kInput, GpioPull::kDisabled)) {
    return false;
  }

  config_ = config;

  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Disabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;

  i2s_->PSEL.MCK = make_psel(config_.mck.port, config_.mck.pin);
  i2s_->PSEL.SCK = make_psel(config_.sck.port, config_.sck.pin);
  i2s_->PSEL.LRCK = make_psel(config_.lrck.port, config_.lrck.pin);
  i2s_->PSEL.SDIN =
      isConnected(config_.sdin) ? make_psel(config_.sdin.port, config_.sdin.pin)
                                : PSEL_DISCONNECTED;
  i2s_->PSEL.SDOUT = make_psel(config_.sdout.port, config_.sdout.pin);

  i2s_->CONFIG.MODE = (I2S_CONFIG_MODE_MODE_Master << I2S_CONFIG_MODE_MODE_Pos) &
                      I2S_CONFIG_MODE_MODE_Msk;
  i2s_->CONFIG.RXEN =
      (I2S_CONFIG_RXEN_RXEN_Disabled << I2S_CONFIG_RXEN_RXEN_Pos) &
      I2S_CONFIG_RXEN_RXEN_Msk;
  i2s_->CONFIG.TXEN =
      (I2S_CONFIG_TXEN_TXEN_Enabled << I2S_CONFIG_TXEN_TXEN_Pos) &
      I2S_CONFIG_TXEN_TXEN_Msk;
  i2s_->CONFIG.MCKEN =
      ((config_.enableMasterClock ? I2S_CONFIG_MCKEN_MCKEN_Enabled
                                  : I2S_CONFIG_MCKEN_MCKEN_Disabled)
       << I2S_CONFIG_MCKEN_MCKEN_Pos) &
      I2S_CONFIG_MCKEN_MCKEN_Msk;
  i2s_->CONFIG.MCKFREQ = config_.mckFreq;
  i2s_->CONFIG.RATIO =
      (config_.ratio << I2S_CONFIG_RATIO_RATIO_Pos) & I2S_CONFIG_RATIO_RATIO_Msk;
  i2s_->CONFIG.SWIDTH =
      (config_.sampleWidth << I2S_CONFIG_SWIDTH_SWIDTH_Pos) &
      I2S_CONFIG_SWIDTH_SWIDTH_Msk;
  i2s_->CONFIG.ALIGN =
      (config_.align << I2S_CONFIG_ALIGN_ALIGN_Pos) & I2S_CONFIG_ALIGN_ALIGN_Msk;
  i2s_->CONFIG.FORMAT =
      (config_.format << I2S_CONFIG_FORMAT_FORMAT_Pos) & I2S_CONFIG_FORMAT_FORMAT_Msk;
  i2s_->CONFIG.CHANNELS =
      (config_.channels << I2S_CONFIG_CHANNELS_CHANNELS_Pos) &
      I2S_CONFIG_CHANNELS_CHANNELS_Msk;

  i2s_->RXTXD.MAXCNT = (wordCount_ << I2S_RXTXD_MAXCNT_MAXCNT_Pos) &
                       I2S_RXTXD_MAXCNT_MAXCNT_Msk;
  i2s_->INTENCLR = I2S_INTENCLR_TXPTRUPD_Msk | I2S_INTENCLR_STOPPED_Msk;
  clearEvents();
  if (refillCallback_ != nullptr) {
    refillCallback_(buffers_[0], wordCount_, refillContext_);
    refillCallback_(buffers_[1], wordCount_, refillContext_);
  }
  armBuffer(0U);

  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Enabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;

  NVIC_SetPriority(I2S20_IRQn, config_.irqPriority);
  NVIC_EnableIRQ(I2S20_IRQn);

  nextBufferIndex_ = 1U;
  configured_ = true;
  running_ = false;
  restartPending_ = false;
  txPtrUpdCount_ = 0U;
  stoppedCount_ = 0U;
  restartCount_ = 0U;
  manualStopCount_ = 0U;
  return true;
}

void I2sTx::end() {
  if (!configured_ || i2s_ == nullptr) {
    return;
  }

  i2s_->INTENCLR = I2S_INTENCLR_TXPTRUPD_Msk | I2S_INTENCLR_STOPPED_Msk;
  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  waitForNonZero(&i2s_->EVENTS_STOPPED, 300000UL);
  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Disabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;
  i2s_->PSEL.MCK = PSEL_DISCONNECTED;
  i2s_->PSEL.SCK = PSEL_DISCONNECTED;
  i2s_->PSEL.LRCK = PSEL_DISCONNECTED;
  i2s_->PSEL.SDIN = PSEL_DISCONNECTED;
  i2s_->PSEL.SDOUT = PSEL_DISCONNECTED;
  if (g_activeI2sTx == this) {
    g_activeI2sTx = nullptr;
  }
  configured_ = false;
  running_ = false;
  restartPending_ = false;
}

bool I2sTx::start() {
  if (!configured_ || i2s_ == nullptr) {
    return false;
  }

  clearEvents();
  nextBufferIndex_ = 1U;
  armBuffer(0U);
  i2s_->INTENSET = I2S_INTENSET_TXPTRUPD_Msk | I2S_INTENSET_STOPPED_Msk;
  running_ = true;
  restartPending_ = false;
  i2s_->TASKS_START = I2S_TASKS_START_TASKS_START_Trigger;
  return true;
}

bool I2sTx::stop() {
  if (!configured_ || !running_ || i2s_ == nullptr) {
    return false;
  }

  ++manualStopCount_;
  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  return true;
}

void I2sTx::service() {
  if (restartPending_ && config_.autoRestart) {
    restartPending_ = false;
    ++restartCount_;
    (void)start();
  }
}

void I2sTx::onIrq() {
  if (!configured_ || i2s_ == nullptr) {
    return;
  }

  if (i2s_->EVENTS_TXPTRUPD != 0U) {
    i2s_->EVENTS_TXPTRUPD = 0U;
    const uint8_t queuedIndex = nextBufferIndex_;
    armBuffer(queuedIndex);
    nextBufferIndex_ ^= 1U;
    if (refillCallback_ != nullptr) {
      refillCallback_(buffers_[nextBufferIndex_], wordCount_, refillContext_);
    }
    ++txPtrUpdCount_;
  }

  if (i2s_->EVENTS_STOPPED != 0U) {
    i2s_->EVENTS_STOPPED = 0U;
    running_ = false;
    restartPending_ = true;
    ++stoppedCount_;
  }
}

bool I2sTx::makeActive() {
  if (!configured_) {
    return false;
  }
  g_activeI2sDuplex = nullptr;
  g_activeI2sRx = nullptr;
  g_activeI2sTx = this;
  return true;
}

void I2sTx::irqHandler() {
  if (g_activeI2sTx != nullptr) {
    g_activeI2sTx->onIrq();
  }
}

bool I2sTx::configured() const { return configured_; }

bool I2sTx::running() const { return running_; }

bool I2sTx::restartPending() const { return restartPending_; }

uint32_t I2sTx::txPtrUpdCount() const { return txPtrUpdCount_; }

uint32_t I2sTx::stoppedCount() const { return stoppedCount_; }

uint32_t I2sTx::restartCount() const { return restartCount_; }

uint32_t I2sTx::manualStopCount() const { return manualStopCount_; }

void I2sTx::clearEvents() {
  i2s_->EVENTS_TXPTRUPD = 0U;
  i2s_->EVENTS_STOPPED = 0U;
}

void I2sTx::armBuffer(uint8_t bufferIndex) {
  const uint8_t index = bufferIndex & 1U;
  i2s_->TXD.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffers_[index])) &
      I2S_TXD_PTR_PTR_Msk;
}

I2sRx::I2sRx(uint32_t base)
    : i2s_(reinterpret_cast<NRF_I2S_Type*>(static_cast<uintptr_t>(base))),
      config_(),
      buffers_{nullptr, nullptr},
      wordCount_(0U),
      receiveCallback_(nullptr),
      receiveContext_(nullptr),
      nextBufferIndex_(1U),
      configured_(false),
      running_(false),
      restartPending_(false),
      rxPtrUpdCount_(0U),
      stoppedCount_(0U),
      restartCount_(0U),
      manualStopCount_(0U) {}

bool I2sRx::setBuffers(uint32_t* buffer0, uint32_t* buffer1, uint32_t wordCount) {
  if (buffer0 == nullptr || buffer1 == nullptr || wordCount == 0U) {
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(buffer0) & 0x3U) != 0U ||
      (reinterpret_cast<uintptr_t>(buffer1) & 0x3U) != 0U) {
    return false;
  }
  if (wordCount >
      (I2S_RXTXD_MAXCNT_MAXCNT_Msk >> I2S_RXTXD_MAXCNT_MAXCNT_Pos)) {
    return false;
  }

  buffers_[0] = buffer0;
  buffers_[1] = buffer1;
  wordCount_ = wordCount;
  return true;
}

void I2sRx::setReceiveCallback(ReceiveCallback callback, void* context) {
  receiveCallback_ = callback;
  receiveContext_ = context;
}

bool I2sRx::begin(const I2sRxConfig& config, uint32_t* buffer0, uint32_t* buffer1,
                  uint32_t wordCount) {
  if (i2s_ == nullptr || !setBuffers(buffer0, buffer1, wordCount)) {
    return false;
  }
  if (!isConnected(config.mck) || !isConnected(config.sck) ||
      !isConnected(config.lrck) || !isConnected(config.sdin)) {
    return false;
  }
  if (!Gpio::configure(config.mck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.lrck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sdin, GpioDirection::kInput, GpioPull::kDisabled)) {
    return false;
  }
  if (isConnected(config.sdout) &&
      !Gpio::configure(config.sdout, GpioDirection::kOutput, GpioPull::kDisabled)) {
    return false;
  }

  config_ = config;

  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Disabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;

  i2s_->PSEL.MCK = make_psel(config_.mck.port, config_.mck.pin);
  i2s_->PSEL.SCK = make_psel(config_.sck.port, config_.sck.pin);
  i2s_->PSEL.LRCK = make_psel(config_.lrck.port, config_.lrck.pin);
  i2s_->PSEL.SDIN = make_psel(config_.sdin.port, config_.sdin.pin);
  i2s_->PSEL.SDOUT =
      isConnected(config_.sdout) ? make_psel(config_.sdout.port, config_.sdout.pin)
                                 : PSEL_DISCONNECTED;

  i2s_->CONFIG.MODE = (I2S_CONFIG_MODE_MODE_Master << I2S_CONFIG_MODE_MODE_Pos) &
                      I2S_CONFIG_MODE_MODE_Msk;
  i2s_->CONFIG.RXEN =
      (I2S_CONFIG_RXEN_RXEN_Enabled << I2S_CONFIG_RXEN_RXEN_Pos) &
      I2S_CONFIG_RXEN_RXEN_Msk;
  i2s_->CONFIG.TXEN =
      (I2S_CONFIG_TXEN_TXEN_Disabled << I2S_CONFIG_TXEN_TXEN_Pos) &
      I2S_CONFIG_TXEN_TXEN_Msk;
  i2s_->CONFIG.MCKEN =
      ((config_.enableMasterClock ? I2S_CONFIG_MCKEN_MCKEN_Enabled
                                  : I2S_CONFIG_MCKEN_MCKEN_Disabled)
       << I2S_CONFIG_MCKEN_MCKEN_Pos) &
      I2S_CONFIG_MCKEN_MCKEN_Msk;
  i2s_->CONFIG.MCKFREQ = config_.mckFreq;
  i2s_->CONFIG.RATIO =
      (config_.ratio << I2S_CONFIG_RATIO_RATIO_Pos) & I2S_CONFIG_RATIO_RATIO_Msk;
  i2s_->CONFIG.SWIDTH =
      (config_.sampleWidth << I2S_CONFIG_SWIDTH_SWIDTH_Pos) &
      I2S_CONFIG_SWIDTH_SWIDTH_Msk;
  i2s_->CONFIG.ALIGN =
      (config_.align << I2S_CONFIG_ALIGN_ALIGN_Pos) & I2S_CONFIG_ALIGN_ALIGN_Msk;
  i2s_->CONFIG.FORMAT =
      (config_.format << I2S_CONFIG_FORMAT_FORMAT_Pos) & I2S_CONFIG_FORMAT_FORMAT_Msk;
  i2s_->CONFIG.CHANNELS =
      (config_.channels << I2S_CONFIG_CHANNELS_CHANNELS_Pos) &
      I2S_CONFIG_CHANNELS_CHANNELS_Msk;

  i2s_->RXTXD.MAXCNT = (wordCount_ << I2S_RXTXD_MAXCNT_MAXCNT_Pos) &
                       I2S_RXTXD_MAXCNT_MAXCNT_Msk;
  i2s_->INTENCLR = I2S_INTENCLR_RXPTRUPD_Msk | I2S_INTENCLR_STOPPED_Msk;
  clearEvents();
  armBuffer(0U);

  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Enabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;

  NVIC_SetPriority(I2S20_IRQn, config_.irqPriority);
  NVIC_EnableIRQ(I2S20_IRQn);

  nextBufferIndex_ = 1U;
  configured_ = true;
  running_ = false;
  restartPending_ = false;
  rxPtrUpdCount_ = 0U;
  stoppedCount_ = 0U;
  restartCount_ = 0U;
  manualStopCount_ = 0U;
  return true;
}

void I2sRx::end() {
  if (!configured_ || i2s_ == nullptr) {
    return;
  }

  i2s_->INTENCLR = I2S_INTENCLR_RXPTRUPD_Msk | I2S_INTENCLR_STOPPED_Msk;
  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  waitForNonZero(&i2s_->EVENTS_STOPPED, 300000UL);
  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Disabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;
  i2s_->PSEL.MCK = PSEL_DISCONNECTED;
  i2s_->PSEL.SCK = PSEL_DISCONNECTED;
  i2s_->PSEL.LRCK = PSEL_DISCONNECTED;
  i2s_->PSEL.SDIN = PSEL_DISCONNECTED;
  i2s_->PSEL.SDOUT = PSEL_DISCONNECTED;
  if (g_activeI2sRx == this) {
    g_activeI2sRx = nullptr;
  }
  configured_ = false;
  running_ = false;
  restartPending_ = false;
}

bool I2sRx::start() {
  if (!configured_ || i2s_ == nullptr) {
    return false;
  }

  clearEvents();
  nextBufferIndex_ = 1U;
  armBuffer(0U);
  i2s_->INTENSET = I2S_INTENSET_RXPTRUPD_Msk | I2S_INTENSET_STOPPED_Msk;
  running_ = true;
  restartPending_ = false;
  i2s_->TASKS_START = I2S_TASKS_START_TASKS_START_Trigger;
  return true;
}

bool I2sRx::stop() {
  if (!configured_ || !running_ || i2s_ == nullptr) {
    return false;
  }

  ++manualStopCount_;
  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  return true;
}

void I2sRx::service() {
  if (restartPending_ && config_.autoRestart) {
    restartPending_ = false;
    ++restartCount_;
    (void)start();
  }
}

void I2sRx::onIrq() {
  if (!configured_ || i2s_ == nullptr) {
    return;
  }

  if (i2s_->EVENTS_RXPTRUPD != 0U) {
    i2s_->EVENTS_RXPTRUPD = 0U;
    const uint8_t queuedIndex = nextBufferIndex_;
    armBuffer(queuedIndex);
    nextBufferIndex_ ^= 1U;
    if (receiveCallback_ != nullptr) {
      receiveCallback_(buffers_[nextBufferIndex_], wordCount_, receiveContext_);
    }
    ++rxPtrUpdCount_;
  }

  if (i2s_->EVENTS_STOPPED != 0U) {
    i2s_->EVENTS_STOPPED = 0U;
    running_ = false;
    restartPending_ = true;
    ++stoppedCount_;
  }
}

bool I2sRx::makeActive() {
  if (!configured_) {
    return false;
  }
  g_activeI2sDuplex = nullptr;
  g_activeI2sTx = nullptr;
  g_activeI2sRx = this;
  return true;
}

void I2sRx::irqHandler() {
  if (g_activeI2sRx != nullptr) {
    g_activeI2sRx->onIrq();
  }
}

bool I2sRx::configured() const { return configured_; }

bool I2sRx::running() const { return running_; }

bool I2sRx::restartPending() const { return restartPending_; }

uint32_t I2sRx::rxPtrUpdCount() const { return rxPtrUpdCount_; }

uint32_t I2sRx::stoppedCount() const { return stoppedCount_; }

uint32_t I2sRx::restartCount() const { return restartCount_; }

uint32_t I2sRx::manualStopCount() const { return manualStopCount_; }

void I2sRx::clearEvents() {
  i2s_->EVENTS_RXPTRUPD = 0U;
  i2s_->EVENTS_STOPPED = 0U;
}

void I2sRx::armBuffer(uint8_t bufferIndex) {
  const uint8_t index = bufferIndex & 1U;
  i2s_->RXD.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffers_[index])) &
      I2S_RXD_PTR_PTR_Msk;
}

I2sDuplex::I2sDuplex(uint32_t base)
    : i2s_(reinterpret_cast<NRF_I2S_Type*>(static_cast<uintptr_t>(base))),
      config_(),
      txBuffers_{nullptr, nullptr},
      rxBuffers_{nullptr, nullptr},
      wordCount_(0U),
      txRefillCallback_(nullptr),
      txRefillContext_(nullptr),
      rxReceiveCallback_(nullptr),
      rxReceiveContext_(nullptr),
      nextTxBufferIndex_(1U),
      nextRxBufferIndex_(1U),
      configured_(false),
      running_(false),
      restartPending_(false),
      txPtrUpdCount_(0U),
      rxPtrUpdCount_(0U),
      stoppedCount_(0U),
      restartCount_(0U),
      manualStopCount_(0U) {}

bool I2sDuplex::setTxBuffers(uint32_t* buffer0, uint32_t* buffer1,
                             uint32_t wordCount) {
  if (buffer0 == nullptr || buffer1 == nullptr || wordCount == 0U) {
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(buffer0) & 0x3U) != 0U ||
      (reinterpret_cast<uintptr_t>(buffer1) & 0x3U) != 0U) {
    return false;
  }
  if (wordCount >
      (I2S_RXTXD_MAXCNT_MAXCNT_Msk >> I2S_RXTXD_MAXCNT_MAXCNT_Pos)) {
    return false;
  }

  txBuffers_[0] = buffer0;
  txBuffers_[1] = buffer1;
  wordCount_ = wordCount;
  return true;
}

bool I2sDuplex::setRxBuffers(uint32_t* buffer0, uint32_t* buffer1,
                             uint32_t wordCount) {
  if (buffer0 == nullptr || buffer1 == nullptr || wordCount == 0U) {
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(buffer0) & 0x3U) != 0U ||
      (reinterpret_cast<uintptr_t>(buffer1) & 0x3U) != 0U) {
    return false;
  }
  if (wordCount >
      (I2S_RXTXD_MAXCNT_MAXCNT_Msk >> I2S_RXTXD_MAXCNT_MAXCNT_Pos)) {
    return false;
  }
  if (wordCount_ != 0U && wordCount_ != wordCount) {
    return false;
  }

  rxBuffers_[0] = buffer0;
  rxBuffers_[1] = buffer1;
  wordCount_ = wordCount;
  return true;
}

void I2sDuplex::setTxRefillCallback(TxRefillCallback callback, void* context) {
  txRefillCallback_ = callback;
  txRefillContext_ = context;
}

void I2sDuplex::setRxReceiveCallback(RxReceiveCallback callback, void* context) {
  rxReceiveCallback_ = callback;
  rxReceiveContext_ = context;
}

bool I2sDuplex::begin(const I2sDuplexConfig& config, uint32_t* txBuffer0,
                      uint32_t* txBuffer1, uint32_t* rxBuffer0,
                      uint32_t* rxBuffer1, uint32_t wordCount) {
  if (i2s_ == nullptr ||
      !setTxBuffers(txBuffer0, txBuffer1, wordCount) ||
      !setRxBuffers(rxBuffer0, rxBuffer1, wordCount)) {
    return false;
  }
  if (!isConnected(config.mck) || !isConnected(config.sck) ||
      !isConnected(config.lrck) || !isConnected(config.sdin) ||
      !isConnected(config.sdout)) {
    return false;
  }
  if (!Gpio::configure(config.mck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.lrck, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sdin, GpioDirection::kInput, GpioPull::kDisabled) ||
      !Gpio::configure(config.sdout, GpioDirection::kOutput, GpioPull::kDisabled)) {
    return false;
  }

  config_ = config;

  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Disabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;

  i2s_->PSEL.MCK = make_psel(config_.mck.port, config_.mck.pin);
  i2s_->PSEL.SCK = make_psel(config_.sck.port, config_.sck.pin);
  i2s_->PSEL.LRCK = make_psel(config_.lrck.port, config_.lrck.pin);
  i2s_->PSEL.SDIN = make_psel(config_.sdin.port, config_.sdin.pin);
  i2s_->PSEL.SDOUT = make_psel(config_.sdout.port, config_.sdout.pin);

  i2s_->CONFIG.MODE = (I2S_CONFIG_MODE_MODE_Master << I2S_CONFIG_MODE_MODE_Pos) &
                      I2S_CONFIG_MODE_MODE_Msk;
  i2s_->CONFIG.RXEN =
      (I2S_CONFIG_RXEN_RXEN_Enabled << I2S_CONFIG_RXEN_RXEN_Pos) &
      I2S_CONFIG_RXEN_RXEN_Msk;
  i2s_->CONFIG.TXEN =
      (I2S_CONFIG_TXEN_TXEN_Enabled << I2S_CONFIG_TXEN_TXEN_Pos) &
      I2S_CONFIG_TXEN_TXEN_Msk;
  i2s_->CONFIG.MCKEN =
      ((config_.enableMasterClock ? I2S_CONFIG_MCKEN_MCKEN_Enabled
                                  : I2S_CONFIG_MCKEN_MCKEN_Disabled)
       << I2S_CONFIG_MCKEN_MCKEN_Pos) &
      I2S_CONFIG_MCKEN_MCKEN_Msk;
  i2s_->CONFIG.MCKFREQ = config_.mckFreq;
  i2s_->CONFIG.RATIO =
      (config_.ratio << I2S_CONFIG_RATIO_RATIO_Pos) & I2S_CONFIG_RATIO_RATIO_Msk;
  i2s_->CONFIG.SWIDTH =
      (config_.sampleWidth << I2S_CONFIG_SWIDTH_SWIDTH_Pos) &
      I2S_CONFIG_SWIDTH_SWIDTH_Msk;
  i2s_->CONFIG.ALIGN =
      (config_.align << I2S_CONFIG_ALIGN_ALIGN_Pos) & I2S_CONFIG_ALIGN_ALIGN_Msk;
  i2s_->CONFIG.FORMAT =
      (config_.format << I2S_CONFIG_FORMAT_FORMAT_Pos) & I2S_CONFIG_FORMAT_FORMAT_Msk;
  i2s_->CONFIG.CHANNELS =
      (config_.channels << I2S_CONFIG_CHANNELS_CHANNELS_Pos) &
      I2S_CONFIG_CHANNELS_CHANNELS_Msk;

  i2s_->RXTXD.MAXCNT = (wordCount_ << I2S_RXTXD_MAXCNT_MAXCNT_Pos) &
                       I2S_RXTXD_MAXCNT_MAXCNT_Msk;
  i2s_->INTENCLR = I2S_INTENCLR_TXPTRUPD_Msk | I2S_INTENCLR_RXPTRUPD_Msk |
                   I2S_INTENCLR_STOPPED_Msk;
  clearEvents();
  armTxBuffer(0U);
  armRxBuffer(0U);

  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Enabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;

  NVIC_SetPriority(I2S20_IRQn, config_.irqPriority);
  NVIC_EnableIRQ(I2S20_IRQn);

  nextTxBufferIndex_ = 1U;
  nextRxBufferIndex_ = 1U;
  configured_ = true;
  running_ = false;
  restartPending_ = false;
  txPtrUpdCount_ = 0U;
  rxPtrUpdCount_ = 0U;
  stoppedCount_ = 0U;
  restartCount_ = 0U;
  manualStopCount_ = 0U;
  return true;
}

void I2sDuplex::end() {
  if (!configured_ || i2s_ == nullptr) {
    return;
  }

  i2s_->INTENCLR = I2S_INTENCLR_TXPTRUPD_Msk | I2S_INTENCLR_RXPTRUPD_Msk |
                   I2S_INTENCLR_STOPPED_Msk;
  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  waitForNonZero(&i2s_->EVENTS_STOPPED, 300000UL);
  i2s_->ENABLE = (I2S_ENABLE_ENABLE_Disabled << I2S_ENABLE_ENABLE_Pos) &
                 I2S_ENABLE_ENABLE_Msk;
  i2s_->PSEL.MCK = PSEL_DISCONNECTED;
  i2s_->PSEL.SCK = PSEL_DISCONNECTED;
  i2s_->PSEL.LRCK = PSEL_DISCONNECTED;
  i2s_->PSEL.SDIN = PSEL_DISCONNECTED;
  i2s_->PSEL.SDOUT = PSEL_DISCONNECTED;
  if (g_activeI2sDuplex == this) {
    g_activeI2sDuplex = nullptr;
  }
  configured_ = false;
  running_ = false;
  restartPending_ = false;
}

bool I2sDuplex::start() {
  if (!configured_ || i2s_ == nullptr) {
    return false;
  }

  clearEvents();
  nextTxBufferIndex_ = 1U;
  nextRxBufferIndex_ = 1U;
  armTxBuffer(0U);
  armRxBuffer(0U);
  i2s_->INTENSET = I2S_INTENSET_TXPTRUPD_Msk | I2S_INTENSET_RXPTRUPD_Msk |
                   I2S_INTENSET_STOPPED_Msk;
  running_ = true;
  restartPending_ = false;
  i2s_->TASKS_START = I2S_TASKS_START_TASKS_START_Trigger;
  return true;
}

bool I2sDuplex::stop() {
  if (!configured_ || !running_ || i2s_ == nullptr) {
    return false;
  }

  ++manualStopCount_;
  i2s_->TASKS_STOP = I2S_TASKS_STOP_TASKS_STOP_Trigger;
  return true;
}

void I2sDuplex::service() {
  if (restartPending_ && config_.autoRestart) {
    restartPending_ = false;
    ++restartCount_;
    (void)start();
  }
}

void I2sDuplex::onIrq() {
  if (!configured_ || i2s_ == nullptr) {
    return;
  }

  if (i2s_->EVENTS_TXPTRUPD != 0U) {
    i2s_->EVENTS_TXPTRUPD = 0U;
    const uint8_t queuedIndex = nextTxBufferIndex_;
    armTxBuffer(queuedIndex);
    nextTxBufferIndex_ ^= 1U;
    if (txRefillCallback_ != nullptr) {
      txRefillCallback_(txBuffers_[nextTxBufferIndex_], wordCount_,
                        txRefillContext_);
    }
    ++txPtrUpdCount_;
  }

  if (i2s_->EVENTS_RXPTRUPD != 0U) {
    i2s_->EVENTS_RXPTRUPD = 0U;
    const uint8_t queuedIndex = nextRxBufferIndex_;
    armRxBuffer(queuedIndex);
    nextRxBufferIndex_ ^= 1U;
    if (rxReceiveCallback_ != nullptr) {
      rxReceiveCallback_(rxBuffers_[nextRxBufferIndex_], wordCount_,
                         rxReceiveContext_);
    }
    ++rxPtrUpdCount_;
  }

  if (i2s_->EVENTS_STOPPED != 0U) {
    i2s_->EVENTS_STOPPED = 0U;
    running_ = false;
    restartPending_ = true;
    ++stoppedCount_;
  }
}

bool I2sDuplex::makeActive() {
  if (!configured_) {
    return false;
  }
  g_activeI2sTx = nullptr;
  g_activeI2sRx = nullptr;
  g_activeI2sDuplex = this;
  return true;
}

void I2sDuplex::irqHandler() {
  if (g_activeI2sDuplex != nullptr) {
    g_activeI2sDuplex->onIrq();
  }
}

bool I2sDuplex::configured() const { return configured_; }

bool I2sDuplex::running() const { return running_; }

bool I2sDuplex::restartPending() const { return restartPending_; }

uint32_t I2sDuplex::txPtrUpdCount() const { return txPtrUpdCount_; }

uint32_t I2sDuplex::rxPtrUpdCount() const { return rxPtrUpdCount_; }

uint32_t I2sDuplex::stoppedCount() const { return stoppedCount_; }

uint32_t I2sDuplex::restartCount() const { return restartCount_; }

uint32_t I2sDuplex::manualStopCount() const { return manualStopCount_; }

void I2sDuplex::clearEvents() {
  i2s_->EVENTS_TXPTRUPD = 0U;
  i2s_->EVENTS_RXPTRUPD = 0U;
  i2s_->EVENTS_STOPPED = 0U;
}

void I2sDuplex::armTxBuffer(uint8_t bufferIndex) {
  const uint8_t index = bufferIndex & 1U;
  i2s_->TXD.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(txBuffers_[index])) &
      I2S_TXD_PTR_PTR_Msk;
}

void I2sDuplex::armRxBuffer(uint8_t bufferIndex) {
  const uint8_t index = bufferIndex & 1U;
  i2s_->RXD.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rxBuffers_[index])) &
      I2S_RXD_PTR_PTR_Msk;
}
