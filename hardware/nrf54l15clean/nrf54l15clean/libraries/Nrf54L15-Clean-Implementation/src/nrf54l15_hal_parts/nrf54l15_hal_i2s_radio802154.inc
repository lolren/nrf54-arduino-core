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

ZigbeeRadio::ZigbeeRadio(uint32_t radioBase)
    : radio_(reinterpret_cast<NRF_RADIO_Type*>(static_cast<uintptr_t>(radioBase))),
      initialized_(false),
      rfPathOwnedByZigbee_(false),
      channel_(15U),
      txPacket_{0},
      rxPacket_{0} {}

bool ZigbeeRadio::configureIeee802154() {
  if (radio_ == nullptr) {
    return false;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitRadioDisabledBudgeted(radio_, 0U, 200000UL);
  radio_->TASKS_SOFTRESET = RADIO_TASKS_SOFTRESET_TASKS_SOFTRESET_Trigger;

  radio_->MODE = ((RADIO_MODE_MODE_Ieee802154_250Kbit << RADIO_MODE_MODE_Pos) &
                  RADIO_MODE_MODE_Msk);
  // Match Zephyr's nRF54LX controller setup for hardware-assisted TIFS:
  // legacy ramp-up is more tolerant on connection-event direction switches.
  radio_->TIMING = ((RADIO_TIMING_RU_Legacy << RADIO_TIMING_RU_Pos) &
                    RADIO_TIMING_RU_Msk);

  uint32_t pcnf0 = 0U;
  pcnf0 |= (8UL << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk;
  pcnf0 |= (RADIO_PCNF0_S1INCL_Automatic << RADIO_PCNF0_S1INCL_Pos) &
           RADIO_PCNF0_S1INCL_Msk;
  pcnf0 |= (RADIO_PCNF0_PLEN_32bitZero << RADIO_PCNF0_PLEN_Pos) &
           RADIO_PCNF0_PLEN_Msk;
  // External Zigbee coordinators are only decoded reliably when the hardware
  // treats the PHR length as including the two-byte FCS, matching 802.15.4.
  pcnf0 |= (RADIO_PCNF0_CRCINC_Include << RADIO_PCNF0_CRCINC_Pos) &
           RADIO_PCNF0_CRCINC_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_TERMLEN_Pos) & RADIO_PCNF0_TERMLEN_Msk;
  radio_->PCNF0 = pcnf0;

  uint32_t pcnf1 = 0U;
  pcnf1 |= (127UL << RADIO_PCNF1_MAXLEN_Pos) & RADIO_PCNF1_MAXLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk;
  pcnf1 |= (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) &
           RADIO_PCNF1_ENDIAN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos) &
           RADIO_PCNF1_WHITEEN_Msk;
  pcnf1 |= (RADIO_PCNF1_WHITEOFFSET_Include << RADIO_PCNF1_WHITEOFFSET_Pos) &
           RADIO_PCNF1_WHITEOFFSET_Msk;
  radio_->PCNF1 = pcnf1;

  uint32_t crccnf = 0U;
  crccnf |= (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) &
            RADIO_CRCCNF_LEN_Msk;
  crccnf |= (RADIO_CRCCNF_SKIPADDR_Ieee802154 << RADIO_CRCCNF_SKIPADDR_Pos) &
            RADIO_CRCCNF_SKIPADDR_Msk;
  radio_->CRCCNF = crccnf;
  radio_->CRCPOLY = (0x11021UL & RADIO_CRCPOLY_CRCPOLY_Msk);
  radio_->CRCINIT = 0U;

  // Disable hardware address/header matching so active scan can observe any
  // nearby IEEE 802.15.4 traffic, not only frames matching a programmed table.
  for (uint8_t i = 0U; i < 8U; ++i) {
    radio_->DAB[i] = 0U;
    radio_->DAP[i] = 0U;
  }
  radio_->DACNF = 0U;
  radio_->MHRMATCHCONF = 0U;
  radio_->MHRMATCHMASK = 0U;

  // IEEE 802.15.4 timing, SFD, CCA and ED defaults.
  radio_->TIFS = 40U;
  radio_->SFD = 0xA7U;
  radio_->EDCTRL = RADIO_EDCTRL_ResetValue;
  radio_->CCACTRL =
      ((RADIO_CCACTRL_CCAMODE_CarrierMode << RADIO_CCACTRL_CCAMODE_Pos) &
       RADIO_CCACTRL_CCAMODE_Msk) |
      (RADIO_CCACTRL_ResetValue & ~RADIO_CCACTRL_CCAMODE_Msk);

  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->EVENTS_MHRMATCH = 0U;
  radio_->EVENTS_DEVMATCH = 0U;
  radio_->EVENTS_DEVMISS = 0U;
  radio_->EVENTS_SYNC = 0U;
  radio_->EVENTS_EDEND = 0U;
  radio_->EVENTS_CCAIDLE = 0U;
  radio_->EVENTS_CCABUSY = 0U;
  radio_->EVENTS_CCASTOPPED = 0U;
  return true;
}

bool ZigbeeRadio::begin(uint8_t channel, int8_t txPowerDbm) {
  if (radio_ == nullptr) {
    initialized_ = false;
    rfPathOwnedByZigbee_ = false;
    return false;
  }

  if (!ClockControl::startHfxo(true, 1500000UL)) {
    initialized_ = false;
    rfPathOwnedByZigbee_ = false;
    return false;
  }
  const bool rfPathWasPowered = BoardControl::rfSwitchPowerEnabled();
  if (!BoardControl::enableRfPath(boardDesiredAntennaPath())) {
    initialized_ = false;
    rfPathOwnedByZigbee_ = false;
    return false;
  }
  rfPathOwnedByZigbee_ = !rfPathWasPowered;
  if (!configureIeee802154()) {
    initialized_ = false;
    if (rfPathOwnedByZigbee_) {
      (void)BoardControl::collapseRfPathIdle();
      rfPathOwnedByZigbee_ = false;
    }
    return false;
  }
  if (!setChannel(channel)) {
    initialized_ = false;
    if (rfPathOwnedByZigbee_) {
      (void)BoardControl::collapseRfPathIdle();
      rfPathOwnedByZigbee_ = false;
    }
    return false;
  }
  if (!setTxPowerDbm(txPowerDbm)) {
    initialized_ = false;
    if (rfPathOwnedByZigbee_) {
      (void)BoardControl::collapseRfPathIdle();
      rfPathOwnedByZigbee_ = false;
    }
    return false;
  }

  initialized_ = true;
  return true;
}

void ZigbeeRadio::end() {
  if (radio_ == nullptr) {
    initialized_ = false;
    rfPathOwnedByZigbee_ = false;
    return;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitRadioDisabledBudgeted(radio_, 0U, 200000UL);
  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->EVENTS_MHRMATCH = 0U;
  radio_->EVENTS_EDEND = 0U;
  radio_->EVENTS_CCAIDLE = 0U;
  radio_->EVENTS_CCABUSY = 0U;
  radio_->EVENTS_CCASTOPPED = 0U;
  initialized_ = false;
  if (rfPathOwnedByZigbee_) {
    (void)BoardControl::collapseRfPathIdle();
    rfPathOwnedByZigbee_ = false;
  }
}

bool ZigbeeRadio::setChannel(uint8_t channel) {
  if (radio_ == nullptr || !ieee802154ChannelValid(channel)) {
    return false;
  }

  const uint8_t freq = ieee802154ChannelToFrequency(channel);
  radio_->FREQUENCY =
      ((static_cast<uint32_t>(freq) << RADIO_FREQUENCY_FREQUENCY_Pos) &
       RADIO_FREQUENCY_FREQUENCY_Msk) |
      (0UL << RADIO_FREQUENCY_MAP_Pos);

  channel_ = channel;
  return true;
}

uint8_t ZigbeeRadio::channel() const { return channel_; }

bool ZigbeeRadio::setTxPowerDbm(int8_t dbm) {
  if (radio_ == nullptr) {
    return false;
  }
  const uint32_t regValue = radioTxPowerRegFromDbm(dbm);
  radio_->TXPOWER = ((regValue << RADIO_TXPOWER_TXPOWER_Pos) &
                     RADIO_TXPOWER_TXPOWER_Msk);
  return true;
}

bool ZigbeeRadio::performCcaCheck(uint32_t spinLimit) {
  if (radio_ == nullptr) {
    return false;
  }
  if (spinLimit == 0U) {
    spinLimit = 1U;
  }

  clearRadioCoreEvents(radio_);
  radio_->EVENTS_CCAIDLE = 0U;
  radio_->EVENTS_CCABUSY = 0U;
  radio_->EVENTS_CCASTOPPED = 0U;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk);
  radio_->EVENTS_READY = 0U;
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  if (!waitForNonZero(&radio_->EVENTS_READY, spinLimit)) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitRadioDisabledBudgeted(radio_, 0U, spinLimit);
    radio_->SHORTS = 0U;
    return false;
  }

  radio_->TASKS_CCASTART = RADIO_TASKS_CCASTART_TASKS_CCASTART_Trigger;
  bool idle = false;
  uint32_t spins = spinLimit;
  while (spins-- > 0U) {
    if (radio_->EVENTS_CCAIDLE != 0U) {
      idle = true;
      break;
    }
    if (radio_->EVENTS_CCABUSY != 0U || radio_->EVENTS_CCASTOPPED != 0U) {
      idle = false;
      break;
    }
  }

  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitRadioDisabledBudgeted(radio_, 0U, spinLimit);
  radio_->SHORTS = 0U;
  return idle;
}

bool ZigbeeRadio::frameRequestsMacAcknowledgement(const uint8_t* psdu,
                                                  uint8_t length,
                                                  uint8_t* outSequence) {
  if (outSequence != nullptr) {
    *outSequence = 0U;
  }
  if (psdu == nullptr || length < 3U) {
    return false;
  }

  const uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  const bool ackRequested = ((frameControl >> 5U) & 0x1U) != 0U;
  const uint8_t destinationMode =
      static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  if (!ackRequested || (frameType != 0x01U && frameType != 0x03U) ||
      destinationMode == 0U) {
    return false;
  }

  if (destinationMode == 0x02U) {
    if (length < 7U) {
      return false;
    }
    if (readLe16(&psdu[5]) == 0xFFFFU) {
      return false;
    }
  }

  if (outSequence != nullptr) {
    *outSequence = psdu[2];
  }
  return true;
}

bool ZigbeeRadio::enableMacAcknowledgementRequest(uint8_t* psdu, uint8_t length,
                                                  uint8_t* outSequence) {
  if (outSequence != nullptr) {
    *outSequence = 0U;
  }
  if (psdu == nullptr || length < 3U) {
    return false;
  }

  uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  const uint8_t destinationMode =
      static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  if ((frameType != 0x01U && frameType != 0x03U) || destinationMode == 0U) {
    return false;
  }

  if (destinationMode == 0x02U) {
    if (length < 7U) {
      return false;
    }
    if (readLe16(&psdu[5]) == 0xFFFFU) {
      return false;
    }
  }

  frameControl |= (1U << 5U);
  writeLe16(&psdu[0], frameControl);
  if (outSequence != nullptr) {
    *outSequence = psdu[2];
  }
  return true;
}

bool ZigbeeRadio::waitForMacAcknowledgement(uint8_t sequence,
                                            uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return false;
  }

  setIeee802154CrcLengthMode(radio_, true);
  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk);

  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  const bool rxDone = waitRadioRxDoneBudgeted(radio_, 2500U, spinLimit);
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  if (!disabled || !rxDone) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOkEvent = (radio_->EVENTS_CRCOK != 0U);
  const bool crcOk =
      crcOkEvent || (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  if (!crcOk) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  const uint8_t length = ieee802154MacPayloadLengthFromPhr(rxPacket_[0]);
  ZigbeeMacAcknowledgementView acknowledgement{};
  const bool match =
      parseMacAcknowledgement(&rxPacket_[1], length, &acknowledgement) &&
      acknowledgement.valid && acknowledgement.sequence == sequence;
  clearRadioCoreEvents(radio_);
  return match;
}

bool ZigbeeRadio::sendMacAcknowledgement(uint8_t sequence, bool framePending,
                                         uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return false;
  }

  uint8_t ackLength = 0U;
  if (!buildMacAcknowledgement(sequence, &txPacket_[1], &ackLength,
                               framePending)) {
    return false;
  }
  txPacket_[0] = ieee802154PhrLengthFromMacPayloadLength(ackLength);
  if (txPacket_[0] == 0U) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(txPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool endSeen = waitRadioEndBudgeted(radio_, 3000U, spinLimit);
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  clearRadioCoreEvents(radio_);
  return endSeen && disabled;
}

bool ZigbeeRadio::transmit(const uint8_t* psdu, uint8_t length, bool performCca,
                           uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || psdu == nullptr) {
    return false;
  }
  if (length == 0U || length > 125U) {
    return false;
  }

  forceRadioDisabled(radio_, spinLimit);

  if (performCca && !performCcaCheck(spinLimit / 2U + 1U)) {
    return false;
  }

  txPacket_[0] = ieee802154PhrLengthFromMacPayloadLength(length);
  memcpy(&txPacket_[1], psdu, length);
  uint8_t acknowledgementSequence = 0U;
  const bool waitForAck = enableMacAcknowledgementRequest(
      &txPacket_[1], length, &acknowledgementSequence);
  lastTransmitDebug_ = {};
  lastTransmitDebug_.txLength = length;
  lastTransmitDebug_.ackRequested = waitForAck;
  lastTransmitDebug_.ackSequence = acknowledgementSequence;

  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(txPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool endSeen = waitRadioEndBudgeted(radio_, 12000U, spinLimit);
  lastTransmitDebug_.endSeen = endSeen;

  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
  lastTransmitDebug_.disabledSeen = disabled;
  radio_->SHORTS = 0U;
  const bool acknowledged =
      (!waitForAck || !(endSeen && disabled))
          ? (endSeen && disabled)
          : waitForMacAcknowledgement(acknowledgementSequence, spinLimit);
  lastTransmitDebug_.ackReceived = acknowledged;
  if (waitForAck && endSeen && disabled) {
    lastTransmitDebug_.rxLength = ieee802154MacPayloadLengthFromPhr(rxPacket_[0]);
    if (lastTransmitDebug_.rxLength >= 3U) {
      lastTransmitDebug_.rxSequence = rxPacket_[3];
    }
  }
  clearRadioCoreEvents(radio_);
  return endSeen && disabled && acknowledged;
}

bool ZigbeeRadio::transmitThenReceive(const uint8_t* psdu, uint8_t length,
                                      ZigbeeFrame* frame,
                                      uint32_t listenWindowUs,
                                      bool performCca, uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || psdu == nullptr || frame == nullptr) {
    return false;
  }
  if (length == 0U || length > 125U) {
    return false;
  }

  forceRadioDisabled(radio_, spinLimit);

  if (performCca && !performCcaCheck(spinLimit / 2U + 1U)) {
    return false;
  }

  txPacket_[0] = ieee802154PhrLengthFromMacPayloadLength(length);
  memcpy(&txPacket_[1], psdu, length);
  memset(rxPacket_, 0, sizeof(rxPacket_));
  uint8_t acknowledgementSequence = 0U;
  bool awaitingMacAcknowledgement = enableMacAcknowledgementRequest(
      &txPacket_[1], length, &acknowledgementSequence);
  lastTransmitDebug_ = {};
  lastTransmitDebug_.txLength = length;
  lastTransmitDebug_.ackRequested = awaitingMacAcknowledgement;
  lastTransmitDebug_.ackSequence = acknowledgementSequence;

  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(txPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  uint32_t txEndTimestampUs = 0U;
  const bool txEndSeen =
      waitRadioEndBudgeted(radio_, 12000U, spinLimit, &txEndTimestampUs);
  lastTransmitDebug_.endSeen = txEndSeen;
  lastTransmitDebug_.txEndTimestampUs = txEndTimestampUs;
  if (!txEndSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    lastTransmitDebug_.disabledSeen =
        waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return false;
  }

  radio_->EVENTS_READY = 0U;
  radio_->EVENTS_RXREADY = 0U;
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->EVENTS_ADDRESS = 0U;
  radio_->EVENTS_PAYLOAD = 0U;
  radio_->EVENTS_CRCOK = 0U;
  radio_->EVENTS_CRCERROR = 0U;
  radio_->EVENTS_RXADDRESS = 0U;
  radio_->EVENTS_DEVMATCH = 0U;
  radio_->EVENTS_DEVMISS = 0U;
  radio_->EVENTS_MHRMATCH = 0U;
  radio_->EVENTS_SYNC = 0U;
  radio_->EVENTS_DISABLED = 0U;
  setIeee802154CrcLengthMode(radio_, true);
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_DISABLED_RXEN_Enabled
        << RADIO_SHORTS_DISABLED_RXEN_Pos) &
       RADIO_SHORTS_DISABLED_RXEN_Msk) |
      ((RADIO_SHORTS_RXREADY_START_Enabled
        << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
        << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  bool txDisabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
  if (!txDisabled) {
    radio_->SHORTS =
        ((RADIO_SHORTS_RXREADY_START_Enabled
          << RADIO_SHORTS_RXREADY_START_Pos) &
         RADIO_SHORTS_RXREADY_START_Msk) |
        ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
          << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
         RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
        ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
          << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
         RADIO_SHORTS_PHYEND_DISABLE_Msk);
    radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
    txDisabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
  }
  if (!txDisabled) {
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return false;
  }
  lastTransmitDebug_.disabledSeen = txDisabled;
  lastTransmitDebug_.disabledTimestampUs = bleTimingUs();

  const uint32_t receiveWindowStartUs = micros();
  bool rxPrimed = true;
  while (static_cast<uint32_t>(micros() - receiveWindowStartUs) <
         listenWindowUs) {
    const uint32_t elapsedUs =
        static_cast<uint32_t>(micros() - receiveWindowStartUs);
    const uint32_t remainingUs =
        (elapsedUs < listenWindowUs) ? (listenWindowUs - elapsedUs) : 0U;
    if (remainingUs == 0U) {
      break;
    }

    if (!rxPrimed) {
      memset(rxPacket_, 0, sizeof(rxPacket_));
      clearRadioCoreEvents(radio_);
      radio_->EVENTS_FRAMESTART = 0U;
      radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
      radio_->SHORTS =
          ((RADIO_SHORTS_RXREADY_START_Enabled
            << RADIO_SHORTS_RXREADY_START_Pos) &
           RADIO_SHORTS_RXREADY_START_Msk) |
          ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
            << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
           RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
          ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
            << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
           RADIO_SHORTS_PHYEND_DISABLE_Msk);

      radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
      lastTransmitDebug_.rxAttempted = true;
    } else {
      lastTransmitDebug_.rxAttempted = true;
      rxPrimed = false;
    }
    uint32_t rxDoneTimestampUs = 0U;
    const bool rxDone =
        waitRadioRxDoneBudgeted(radio_, remainingUs, spinLimit,
                                &rxDoneTimestampUs);
    lastTransmitDebug_.rxDone = rxDone;
    lastTransmitDebug_.rxDoneTimestampUs = rxDoneTimestampUs;
    const int8_t rssiDbm = radioRssiDbm(radio_);

    const uint32_t crcStatus =
        (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
        RADIO_CRCSTATUS_CRCSTATUS_Pos;
    const bool crcOkEvent = (radio_->EVENTS_CRCOK != 0U);
    const bool crcOk =
        rxDone &&
        (crcOkEvent || (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk));
    lastTransmitDebug_.crcOk = crcOk;
    lastTransmitDebug_.crcError = rxDone && !crcOk;

    uint8_t rxLength = 0U;
    bool validLength = false;
    if (crcOk) {
      rxLength = ieee802154MacPayloadLengthFromPhr(rxPacket_[0]);
      validLength = (rxLength != 0U && rxLength <= 127U);
    }

    ZigbeeMacAcknowledgementView macAcknowledgement{};
    const bool matchedMacAcknowledgement =
        awaitingMacAcknowledgement && validLength &&
        parseMacAcknowledgement(&rxPacket_[1], rxLength, &macAcknowledgement) &&
        macAcknowledgement.valid &&
        macAcknowledgement.sequence == acknowledgementSequence;
    if (matchedMacAcknowledgement && macAcknowledgement.framePending) {
      awaitingMacAcknowledgement = false;
      lastTransmitDebug_.ackReceived = true;
      lastTransmitDebug_.rxLength = rxLength;
      if (rxLength >= 3U) {
        lastTransmitDebug_.rxSequence = rxPacket_[3];
      }
      // Preserve the hardware DISABLED->RXEN turnaround after a frame-pending
      // MAC ACK. Clearing DISABLED/READY/RXREADY here can race the start of
      // the pending indirect data frame and make sleepy-child Trust Center
      // traffic disappear on the joiner side.
      radio_->EVENTS_FRAMESTART = 0U;
      radio_->EVENTS_ADDRESS = 0U;
      radio_->EVENTS_PAYLOAD = 0U;
      radio_->EVENTS_CRCOK = 0U;
      radio_->EVENTS_CRCERROR = 0U;
      radio_->EVENTS_RXADDRESS = 0U;
      radio_->EVENTS_DEVMATCH = 0U;
      radio_->EVENTS_DEVMISS = 0U;
      radio_->EVENTS_MHRMATCH = 0U;
      radio_->EVENTS_SYNC = 0U;
      const uint32_t radioState =
          (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
      if (radioState == RADIO_STATE_STATE_Disabled) {
        radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
      }

      rxPrimed = true;
      continue;
    }

    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    bool disabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
    if (!disabled) {
      radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
      disabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
    }
    snapshotZigbeeReceiveEvents(radio_, &lastTransmitDebug_);
    radio_->SHORTS = 0U;
    if (!disabled || !rxDone) {
      continue;
    }
    if (!crcOk) {
      continue;
    }
    if (!validLength) {
      continue;
    }

    if (awaitingMacAcknowledgement &&
        parseMacAcknowledgement(&rxPacket_[1], rxLength, &macAcknowledgement) &&
        macAcknowledgement.valid &&
        macAcknowledgement.sequence == acknowledgementSequence) {
      awaitingMacAcknowledgement = false;
      lastTransmitDebug_.ackReceived = true;
      lastTransmitDebug_.rxLength = rxLength;
      if (rxLength >= 3U) {
        lastTransmitDebug_.rxSequence = rxPacket_[3];
      }
      continue;
    }

    uint8_t responseAckSequence = 0U;
    const bool sendAcknowledgement = frameRequestsMacAcknowledgement(
        &rxPacket_[1], rxLength, &responseAckSequence);

    frame->channel = channel_;
    frame->rssiDbm = rssiDbm;
    frame->length = rxLength;
    memcpy(frame->psdu, &rxPacket_[1], rxLength);
    lastTransmitDebug_.ackReceived =
        (!lastTransmitDebug_.ackRequested || !awaitingMacAcknowledgement);
    lastTransmitDebug_.rxLength = rxLength;
    if (rxLength >= 3U) {
      lastTransmitDebug_.rxSequence = rxPacket_[3];
    }
    if (sendAcknowledgement) {
      const bool framePending =
          ((readLe16(&rxPacket_[1]) & 0x0007U) == 0x03U);
      (void)sendMacAcknowledgement(responseAckSequence, framePending, spinLimit);
    }
    clearRadioCoreEvents(radio_);
    return true;
  }

  lastTransmitDebug_.ackReceived =
      (!lastTransmitDebug_.ackRequested || !awaitingMacAcknowledgement);
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  (void)waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  clearRadioCoreEvents(radio_);
  return false;
}

bool ZigbeeRadio::receive(ZigbeeFrame* frame, uint32_t listenWindowUs,
                          uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || frame == nullptr) {
    return false;
  }

  forceRadioDisabled(radio_, spinLimit);

  setIeee802154CrcLengthMode(radio_, true);
  clearRadioCoreEvents(radio_);
  radio_->EVENTS_FRAMESTART = 0U;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk);

  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  const bool rxDone = waitRadioRxDoneBudgeted(radio_, listenWindowUs, spinLimit);
  const int8_t rssiDbm = radioRssiDbm(radio_);

  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  if (!disabled || !rxDone) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOkEvent = (radio_->EVENTS_CRCOK != 0U);
  const bool crcOk = crcOkEvent || (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  if (!crcOk) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  const uint8_t length = ieee802154MacPayloadLengthFromPhr(rxPacket_[0]);
  if (length == 0U || length > 127U) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  uint8_t acknowledgementSequence = 0U;
  const bool sendAcknowledgement = frameRequestsMacAcknowledgement(
      &rxPacket_[1], length, &acknowledgementSequence);

  frame->channel = channel_;
  frame->rssiDbm = rssiDbm;
  frame->length = length;
  memcpy(frame->psdu, &rxPacket_[1], length);
  if (sendAcknowledgement) {
    const bool framePending =
        ((readLe16(&rxPacket_[1]) & 0x0007U) == 0x03U);
    (void)sendMacAcknowledgement(acknowledgementSequence, framePending, spinLimit);
  }
  clearRadioCoreEvents(radio_);
  return true;
}

bool ZigbeeRadio::sampleEnergyDetect(uint8_t* outEdLevel, uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || outEdLevel == nullptr) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  radio_->EVENTS_EDEND = 0U;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk);
  radio_->EVENTS_READY = 0U;
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  if (!waitForNonZero(&radio_->EVENTS_READY, spinLimit)) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitRadioDisabledBudgeted(radio_, 0U, spinLimit);
    radio_->SHORTS = 0U;
    return false;
  }

  radio_->TASKS_EDSTART = RADIO_TASKS_EDSTART_TASKS_EDSTART_Trigger;
  if (!waitForNonZero(&radio_->EVENTS_EDEND, spinLimit)) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitRadioDisabledBudgeted(radio_, 0U, spinLimit);
    radio_->SHORTS = 0U;
    return false;
  }

  const uint8_t edLevel =
      static_cast<uint8_t>((radio_->EDSAMPLE & RADIO_EDSAMPLE_EDLVL_Msk) >>
                           RADIO_EDSAMPLE_EDLVL_Pos);
  *outEdLevel = edLevel;

  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitRadioDisabledBudgeted(radio_, 0U, spinLimit);
  radio_->SHORTS = 0U;
  return true;
}

ZigbeeTransmitDebug ZigbeeRadio::lastTransmitDebug() const {
  return lastTransmitDebug_;
}

RawRadioLink::RawRadioLink(uint32_t radioBase)
    : radio_(reinterpret_cast<NRF_RADIO_Type*>(static_cast<uintptr_t>(radioBase))),
      power_(),
      config_(),
      initialized_(false),
      receiverArmed_(false),
      txPacket_{0},
      rxPacket_{0} {}

bool RawRadioLink::configureProprietary1M() {
  if (radio_ == nullptr) {
    return false;
  }
  if (config_.maxPayloadLength == 0U) {
    return false;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitRadioDisabledBudgeted(radio_, 0U, 200000UL);
  radio_->TASKS_SOFTRESET = RADIO_TASKS_SOFTRESET_TASKS_SOFTRESET_Trigger;

  radio_->MODE = ((RADIO_MODE_MODE_Nrf_1Mbit << RADIO_MODE_MODE_Pos) &
                  RADIO_MODE_MODE_Msk);
  radio_->TIMING = ((RADIO_TIMING_RU_Fast << RADIO_TIMING_RU_Pos) &
                    RADIO_TIMING_RU_Msk);

  uint32_t pcnf0 = 0U;
  pcnf0 |= (8UL << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk;
  pcnf0 |= (0UL << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk;
  pcnf0 |= (RADIO_PCNF0_S1INCL_Automatic << RADIO_PCNF0_S1INCL_Pos) &
           RADIO_PCNF0_S1INCL_Msk;
  pcnf0 |= (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos) &
           RADIO_PCNF0_PLEN_Msk;
  pcnf0 |= (RADIO_PCNF0_CRCINC_Exclude << RADIO_PCNF0_CRCINC_Pos) &
           RADIO_PCNF0_CRCINC_Msk;
  radio_->PCNF0 = pcnf0;

  uint32_t pcnf1 = 0U;
  pcnf1 |= (static_cast<uint32_t>(config_.maxPayloadLength)
            << RADIO_PCNF1_MAXLEN_Pos) & RADIO_PCNF1_MAXLEN_Msk;
  pcnf1 |= (0UL << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk;
  pcnf1 |= (4UL << RADIO_PCNF1_BALEN_Pos) & RADIO_PCNF1_BALEN_Msk;
  pcnf1 |= ((config_.bigEndian ? RADIO_PCNF1_ENDIAN_Big
                               : RADIO_PCNF1_ENDIAN_Little)
            << RADIO_PCNF1_ENDIAN_Pos) & RADIO_PCNF1_ENDIAN_Msk;
  pcnf1 |= ((config_.whiteningEnabled ? RADIO_PCNF1_WHITEEN_Enabled
                                      : RADIO_PCNF1_WHITEEN_Disabled)
            << RADIO_PCNF1_WHITEEN_Pos) & RADIO_PCNF1_WHITEEN_Msk;
  radio_->PCNF1 = pcnf1;

  radio_->BASE0 = (config_.addressBase0 & RADIO_BASE0_BASE0_Msk);
  radio_->PREFIX0 =
      ((static_cast<uint32_t>(config_.addressPrefix0) << RADIO_PREFIX0_AP0_Pos) &
       RADIO_PREFIX0_AP0_Msk);
  radio_->TXADDRESS =
      (0UL << RADIO_TXADDRESS_TXADDRESS_Pos) & RADIO_TXADDRESS_TXADDRESS_Msk;
  radio_->RXADDRESSES =
      (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos) &
      RADIO_RXADDRESSES_ADDR0_Msk;

  uint32_t crccnf = 0U;
  crccnf |= (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) &
            RADIO_CRCCNF_LEN_Msk;
  crccnf |= (RADIO_CRCCNF_SKIPADDR_Include << RADIO_CRCCNF_SKIPADDR_Pos) &
            RADIO_CRCCNF_SKIPADDR_Msk;
  radio_->CRCCNF = crccnf;
  radio_->CRCPOLY = (config_.crcPolynomial & RADIO_CRCPOLY_CRCPOLY_Msk);
  radio_->CRCINIT = (config_.crcInit & RADIO_CRCINIT_CRCINIT_Msk);

  return setFrequencyOffsetMhz(config_.frequencyOffsetMhz) &&
         setTxPowerDbm(config_.txPowerDbm);
}

bool RawRadioLink::begin(const RawRadioConfig& config) {
  if (radio_ == nullptr || config.maxPayloadLength == 0U) {
    initialized_ = false;
    receiverArmed_ = false;
    return false;
  }

  if (!ClockControl::startHfxo(true, 1500000UL)) {
    initialized_ = false;
    receiverArmed_ = false;
    return false;
  }

  config_ = config;
  power_.setLatencyMode(PowerLatencyMode::kConstantLatency);

  if (!configureProprietary1M()) {
    initialized_ = false;
    receiverArmed_ = false;
    return false;
  }

  clearRadioCoreEvents(radio_);
  memset(txPacket_, 0, sizeof(txPacket_));
  memset(rxPacket_, 0, sizeof(rxPacket_));
  initialized_ = true;
  receiverArmed_ = false;
  return true;
}

void RawRadioLink::end() {
  if (radio_ == nullptr) {
    initialized_ = false;
    receiverArmed_ = false;
    return;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitRadioDisabledBudgeted(radio_, 0U, 200000UL);
  clearRadioCoreEvents(radio_);
  initialized_ = false;
  receiverArmed_ = false;
}

bool RawRadioLink::setFrequencyOffsetMhz(uint8_t frequencyOffsetMhz) {
  if (radio_ == nullptr) {
    return false;
  }

  config_.frequencyOffsetMhz = frequencyOffsetMhz;
  radio_->FREQUENCY =
      ((static_cast<uint32_t>(config_.frequencyOffsetMhz)
        << RADIO_FREQUENCY_FREQUENCY_Pos) &
       RADIO_FREQUENCY_FREQUENCY_Msk) |
      (0UL << RADIO_FREQUENCY_MAP_Pos);
  return true;
}

bool RawRadioLink::setPipe(uint32_t addressBase0, uint8_t addressPrefix0) {
  if (radio_ == nullptr) {
    return false;
  }

  config_.addressBase0 = addressBase0;
  config_.addressPrefix0 = addressPrefix0;
  radio_->BASE0 = (config_.addressBase0 & RADIO_BASE0_BASE0_Msk);
  radio_->PREFIX0 =
      ((static_cast<uint32_t>(config_.addressPrefix0) << RADIO_PREFIX0_AP0_Pos) &
       RADIO_PREFIX0_AP0_Msk);
  return true;
}

bool RawRadioLink::setTxPowerDbm(int8_t dbm) {
  if (radio_ == nullptr) {
    return false;
  }

  config_.txPowerDbm = dbm;
  const uint32_t regValue = radioTxPowerRegFromDbm(dbm);
  radio_->TXPOWER = ((regValue << RADIO_TXPOWER_TXPOWER_Pos) &
                     RADIO_TXPOWER_TXPOWER_Msk);
  return true;
}

bool RawRadioLink::transmit(const uint8_t* payload, uint8_t length,
                            uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || payload == nullptr) {
    return false;
  }
  if (length == 0U || length > config_.maxPayloadLength) {
    return false;
  }

  txPacket_[0] = length;
  memcpy(&txPacket_[1], payload, length);

  clearRadioCoreEvents(radio_);
  receiverArmed_ = false;
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(txPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool endSeen = waitRadioEndBudgeted(radio_, 12000U, spinLimit);
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  const bool disabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  clearRadioCoreEvents(radio_);
  return endSeen && disabled;
}

bool RawRadioLink::armReceive() {
  if (!initialized_ || radio_ == nullptr) {
    return false;
  }

  memset(rxPacket_, 0, sizeof(rxPacket_));
  clearRadioCoreEvents(radio_);
  radio_->PACKETPTR = reinterpret_cast<uint32_t>(rxPacket_);
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk);
  radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  receiverArmed_ = true;
  return true;
}

RawRadioReceiveStatus RawRadioLink::finishReceive(RawRadioPacket* outPacket,
                                                  uint32_t spinLimit,
                                                  bool timedOut) {
  if (radio_ == nullptr || !receiverArmed_) {
    return RawRadioReceiveStatus::kError;
  }

  const int8_t rssiDbm = radioRssiDbm(radio_);
  const bool disabled = waitRadioDisabledBudgeted(radio_, 3000U, spinLimit);
  radio_->SHORTS = 0U;
  receiverArmed_ = false;
  if (!disabled) {
    clearRadioCoreEvents(radio_);
    return RawRadioReceiveStatus::kError;
  }

  if (timedOut ||
      ((radio_->EVENTS_CRCOK == 0U) && (radio_->EVENTS_CRCERROR == 0U))) {
    clearRadioCoreEvents(radio_);
    return RawRadioReceiveStatus::kIdle;
  }

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOkEvent = (radio_->EVENTS_CRCOK != 0U);
  const bool crcOk = crcOkEvent || (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  if (!crcOk) {
    clearRadioCoreEvents(radio_);
    return RawRadioReceiveStatus::kCrcError;
  }

  const uint8_t length = minU8(rxPacket_[0], config_.maxPayloadLength);
  if (length == 0U) {
    clearRadioCoreEvents(radio_);
    return RawRadioReceiveStatus::kError;
  }

  if (outPacket != nullptr) {
    outPacket->length = length;
    outPacket->rssiDbm = rssiDbm;
    memcpy(outPacket->payload, &rxPacket_[1], length);
  }

  clearRadioCoreEvents(radio_);
  return RawRadioReceiveStatus::kPacket;
}

RawRadioReceiveStatus RawRadioLink::pollReceive(RawRadioPacket* outPacket,
                                                uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return RawRadioReceiveStatus::kError;
  }
  if (!receiverArmed_) {
    return RawRadioReceiveStatus::kIdle;
  }

  if ((radio_->EVENTS_CRCOK == 0U) && (radio_->EVENTS_CRCERROR == 0U) &&
      (radio_->EVENTS_END == 0U)) {
    return RawRadioReceiveStatus::kIdle;
  }

  return finishReceive(outPacket, spinLimit, false);
}

RawRadioReceiveStatus RawRadioLink::waitForReceive(RawRadioPacket* outPacket,
                                                   uint32_t listenWindowUs,
                                                   uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr) {
    return RawRadioReceiveStatus::kError;
  }
  if (!receiverArmed_ && !armReceive()) {
    return RawRadioReceiveStatus::kError;
  }

  const bool rxDone = waitRadioRxDoneBudgeted(radio_, listenWindowUs, spinLimit);
  if (!rxDone) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  }

  return finishReceive(outPacket, spinLimit, !rxDone);
}

bool RawRadioLink::initialized() const { return initialized_; }

bool RawRadioLink::receiverArmed() const { return receiverArmed_; }

uint8_t RawRadioLink::maxPayloadLength() const { return config_.maxPayloadLength; }

const RawRadioConfig& RawRadioLink::config() const { return config_; }

bool ZigbeeRadio::buildDataFrameShort(uint8_t sequence, uint16_t panId,
                                      uint16_t destinationShort,
                                      uint16_t sourceShort, const uint8_t* payload,
                                      uint8_t payloadLength, uint8_t* outPsdu,
                                      uint8_t* outLength, bool requestAck) {
  if (outPsdu == nullptr || outLength == nullptr) {
    return false;
  }
  if (payloadLength > 0U && payload == nullptr) {
    return false;
  }

  // Data frame, PAN compression, destination short, source short.
  // Header layout: FCF(2), Seq(1), DstPAN(2), DstShort(2), SrcShort(2).
  constexpr uint8_t kHeaderLen = 9U;
  if (payloadLength > static_cast<uint8_t>(127U - kHeaderLen)) {
    return false;
  }

  uint16_t frameControl = 0x9841U;
  if (requestAck) {
    frameControl |= (1U << 5U);
  }

  writeLe16(&outPsdu[0], frameControl);
  outPsdu[2] = sequence;
  writeLe16(&outPsdu[3], panId);
  writeLe16(&outPsdu[5], destinationShort);
  writeLe16(&outPsdu[7], sourceShort);
  if (payloadLength > 0U) {
    memcpy(&outPsdu[kHeaderLen], payload, payloadLength);
  }
  *outLength = static_cast<uint8_t>(kHeaderLen + payloadLength);
  return true;
}

bool ZigbeeRadio::buildMacCommandFrameShort(
    uint8_t sequence, uint16_t panId, uint16_t destinationShort,
    uint16_t sourceShort, uint8_t commandId, const uint8_t* payload,
    uint8_t payloadLength, uint8_t* outPsdu, uint8_t* outLength,
    bool requestAck) {
  if (outPsdu == nullptr || outLength == nullptr) {
    return false;
  }
  if (payloadLength > 0U && payload == nullptr) {
    return false;
  }

  // MAC command frame with PAN compression, destination short, source short.
  // Header layout: FCF(2), Seq(1), DstPAN(2), DstShort(2), SrcShort(2),
  // CommandId(1).
  constexpr uint8_t kHeaderLen = 10U;
  if (payloadLength > static_cast<uint8_t>(127U - kHeaderLen)) {
    return false;
  }

  uint16_t frameControl = 0x9843U;
  if (requestAck) {
    frameControl |= (1U << 5U);
  }

  writeLe16(&outPsdu[0], frameControl);
  outPsdu[2] = sequence;
  writeLe16(&outPsdu[3], panId);
  writeLe16(&outPsdu[5], destinationShort);
  writeLe16(&outPsdu[7], sourceShort);
  outPsdu[9] = commandId;
  if (payloadLength > 0U) {
    memcpy(&outPsdu[kHeaderLen], payload, payloadLength);
  }
  *outLength = static_cast<uint8_t>(kHeaderLen + payloadLength);
  return true;
}

bool ZigbeeRadio::parseDataFrameShort(const uint8_t* psdu, uint8_t length,
                                      ZigbeeDataFrameView* outView) {
  if (outView != nullptr) {
    memset(outView, 0, sizeof(*outView));
  }
  if (psdu == nullptr || outView == nullptr || length < 9U) {
    return false;
  }

  const uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  const bool panCompression = ((frameControl >> 6U) & 0x1U) != 0U;
  const uint8_t dstAddrMode = static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  const uint8_t srcAddrMode = static_cast<uint8_t>((frameControl >> 14U) & 0x03U);

  if (frameType != 0x01U || dstAddrMode != 0x02U || srcAddrMode != 0x02U) {
    return false;
  }

  uint8_t offset = 3U;
  if (length < static_cast<uint8_t>(offset + 2U + 2U)) {
    return false;
  }
  const uint16_t destinationPan = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);
  const uint16_t destinationShort = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);

  uint16_t sourcePan = destinationPan;
  if (!panCompression) {
    if (length < static_cast<uint8_t>(offset + 2U)) {
      return false;
    }
    sourcePan = readLe16(&psdu[offset]);
    offset = static_cast<uint8_t>(offset + 2U);
  }

  if (length < static_cast<uint8_t>(offset + 2U)) {
    return false;
  }
  const uint16_t sourceShort = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);
  if (length < offset) {
    return false;
  }

  outView->valid = true;
  outView->ackRequested = ((frameControl >> 5U) & 0x1U) != 0U;
  outView->sequence = psdu[2];
  outView->panId = sourcePan;
  outView->destinationShort = destinationShort;
  outView->sourceShort = sourceShort;
  outView->payloadLength = static_cast<uint8_t>(length - offset);
  outView->payload = &psdu[offset];
  return true;
}

bool ZigbeeRadio::parseMacCommandFrameShort(const uint8_t* psdu, uint8_t length,
                                            ZigbeeMacCommandView* outView) {
  if (outView != nullptr) {
    memset(outView, 0, sizeof(*outView));
  }
  if (psdu == nullptr || outView == nullptr || length < 10U) {
    return false;
  }

  const uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  const bool panCompression = ((frameControl >> 6U) & 0x1U) != 0U;
  const uint8_t dstAddrMode = static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  const uint8_t srcAddrMode = static_cast<uint8_t>((frameControl >> 14U) & 0x03U);

  if (frameType != 0x03U || dstAddrMode != 0x02U || srcAddrMode != 0x02U) {
    return false;
  }

  uint8_t offset = 3U;
  if (length < static_cast<uint8_t>(offset + 2U + 2U)) {
    return false;
  }
  const uint16_t destinationPan = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);
  const uint16_t destinationShort = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);

  uint16_t sourcePan = destinationPan;
  if (!panCompression) {
    if (length < static_cast<uint8_t>(offset + 2U)) {
      return false;
    }
    sourcePan = readLe16(&psdu[offset]);
    offset = static_cast<uint8_t>(offset + 2U);
  }

  if (length < static_cast<uint8_t>(offset + 2U + 1U)) {
    return false;
  }
  const uint16_t sourceShort = readLe16(&psdu[offset]);
  offset = static_cast<uint8_t>(offset + 2U);
  const uint8_t commandId = psdu[offset];
  offset = static_cast<uint8_t>(offset + 1U);

  outView->valid = true;
  outView->ackRequested = ((frameControl >> 5U) & 0x1U) != 0U;
  outView->sequence = psdu[2];
  outView->panId = sourcePan;
  outView->destinationShort = destinationShort;
  outView->sourceShort = sourceShort;
  outView->commandId = commandId;
  outView->payloadLength = static_cast<uint8_t>(length - offset);
  outView->payload = &psdu[offset];
  return true;
}

bool ZigbeeRadio::buildMacAcknowledgement(uint8_t sequence, uint8_t* outPsdu,
                                          uint8_t* outLength,
                                          bool framePending) {
  if (outPsdu == nullptr || outLength == nullptr) {
    return false;
  }

  uint16_t frameControl = 0x0002U;
  if (framePending) {
    frameControl |= (1U << 4U);
  }
  writeLe16(&outPsdu[0], frameControl);
  outPsdu[2] = sequence;
  *outLength = 3U;
  return true;
}

bool ZigbeeRadio::parseMacAcknowledgement(
    const uint8_t* psdu, uint8_t length, ZigbeeMacAcknowledgementView* outView) {
  if (outView != nullptr) {
    memset(outView, 0, sizeof(*outView));
  }
  if (psdu == nullptr || outView == nullptr || length != 3U) {
    return false;
  }

  const uint16_t frameControl = readLe16(&psdu[0]);
  const uint8_t frameType = static_cast<uint8_t>(frameControl & 0x0007U);
  const uint8_t destinationMode =
      static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  const uint8_t sourceMode =
      static_cast<uint8_t>((frameControl >> 14U) & 0x03U);
  if (frameType != 0x02U || destinationMode != 0U || sourceMode != 0U) {
    return false;
  }

  outView->valid = true;
  outView->framePending = ((frameControl >> 4U) & 0x1U) != 0U;
  outView->sequence = psdu[2];
  return true;
}
