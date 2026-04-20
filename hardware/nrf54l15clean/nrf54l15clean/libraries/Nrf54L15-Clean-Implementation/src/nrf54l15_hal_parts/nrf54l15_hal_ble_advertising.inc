bool BleRadio::transmitPreparedPacketOnCurrentChannel(const uint8_t* packet,
                                                      uint32_t spinLimit,
                                                      uint32_t* txReadyUs) {
  if (!initialized_ || radio_ == nullptr || connected_ || packet == nullptr) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  radio_->PACKETPTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(packet));
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;

  bool readySeen = false;
  bool disabledSeen = false;
  uint32_t remaining = spinLimit;
  while (remaining-- > 0U) {
    if (!readySeen && (radio_->EVENTS_READY != 0U)) {
      readySeen = true;
      if (txReadyUs != nullptr) {
        *txReadyUs = bleTimingUs();
      }
    }

    if (radio_->EVENTS_DISABLED != 0U) {
      disabledSeen = true;
      break;
    }

    const uint32_t state =
        (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (state == RADIO_STATE_STATE_Disabled) {
      disabledSeen = true;
      break;
    }
  }

  if (!disabledSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
  }

  radio_->SHORTS = 0U;
  clearRadioCoreEvents(radio_);
  return disabledSeen;
}

bool BleRadio::kickPreparedPacketOnCurrentChannel(const uint8_t* packet) {
  if (!initialized_ || radio_ == nullptr || connected_ || packet == nullptr) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  radio_->PACKETPTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(packet));
  radio_->SHORTS = bleRadioShortsTxOnly();
  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  return true;
}

bool BleRadio::advertiseOncePrepared(BleAdvertisingChannel channel,
                                     uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || connected_) {
    return false;
  }
  if (pduType_ != BleAdvPduType::kAdvNonConnInd &&
      pduType_ != BleAdvPduType::kAdvInd &&
      pduType_ != BleAdvPduType::kAdvScanInd) {
    return false;
  }
  if (!setAdvertisingChannel(channel)) {
    return false;
  }
  return transmitPreparedPacketOnCurrentChannel(&txPacket_[0], spinLimit);
}

bool BleRadio::advertiseOnce(BleAdvertisingChannel channel, uint32_t spinLimit) {
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  const bool ok = advertiseOncePrepared(channel, spinLimit);
  endUnconnectedRadioActivity();
  return ok;
}

bool BleRadio::advertiseEvent(uint32_t interChannelDelayUs, uint32_t spinLimit) {
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  // Add spec-compliant random delay (0-10ms) to prevent persistent collisions.
  {
    static uint32_t seed = 0U;
    if (seed == 0U) {
      seed = micros() ^ (static_cast<uint32_t>(address_[0]) << 24U);
    }
    seed = seed * 1103515245U + 12345U;
    const uint32_t advDelayUs = (seed % 10001U);
    if (advDelayUs > 0U) {
      delayMicroseconds(advDelayUs);
    }
  }

  if (!advertiseOncePrepared(BleAdvertisingChannel::k37, spinLimit)) {
    endUnconnectedRadioActivity();
    return false;
  }
  if (interChannelDelayUs > 0U) {
    delayMicroseconds(interChannelDelayUs);
  }

  if (!advertiseOncePrepared(BleAdvertisingChannel::k38, spinLimit)) {
    endUnconnectedRadioActivity();
    return false;
  }
  if (interChannelDelayUs > 0U) {
    delayMicroseconds(interChannelDelayUs);
  }

  const bool ok = advertiseOncePrepared(BleAdvertisingChannel::k39, spinLimit);
  endUnconnectedRadioActivity();
  return ok;
}

bool BleRadio::prepareBackgroundAdvertisingEvent() {
  if (!initialized_ || radio_ == nullptr || connected_ ||
      !backgroundAdvertisingEnabled_) {
    return false;
  }
  if (pduType_ != BleAdvPduType::kAdvNonConnInd) {
    return false;
  }

  BleAdvertisingChannel channel = backgroundAdvertisingChannel_;
  if (backgroundAdvertisingThreeChannel_) {
    channel = bleBackgroundAdvertisingStageChannel(backgroundAdvertisingPrimaryStage_);
    backgroundAdvertisingChannel_ = channel;
    backgroundAdvertisingDebug_.lastChannel = static_cast<uint8_t>(channel);
  }

  if (!setAdvertisingChannel(channel)) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&txPacket_[0]));
  radio_->SHORTS = backgroundAdvertisingUseIrqTxKick_
                       ? bleRadioShortsTxOnly()
                       : bleRadioShortsBackgroundAdvDirectTx();
  backgroundAdvertisingTxKickRetryCountCurrent_ = 0U;
  return true;
}

bool BleRadio::configureBackgroundAdvertisingHardware(bool enable) {
  if (radio_ == nullptr) {
    return false;
  }

  Dppic radioDomain(nrf54l15::DPPIC10_BASE);
  Dppic periDomain(nrf54l15::DPPIC20_BASE);
  Dppic lpDomain(nrf54l15::DPPIC30_BASE);
  NRF_PPIB_Type* const ppib11 = blePpib11();
  NRF_PPIB_Type* const ppib21 = blePpib21();
  NRF_PPIB_Type* const ppib22 = blePpib22();
  NRF_PPIB_Type* const ppib30 = blePpib30();
  if (ppib11 == nullptr || ppib21 == nullptr || ppib22 == nullptr ||
      ppib30 == nullptr) {
    return false;
  }

  ppib30->EVENTS_RECEIVE[kBleBackgroundAdvLpToPeriBridgeIndex] = 0U;
  ppib22->EVENTS_RECEIVE[kBleBackgroundAdvLpToPeriBridgeIndex] = 0U;
  ppib21->EVENTS_RECEIVE[kBleBackgroundAdvLpToPeriBridgeIndex] = 0U;
  ppib11->EVENTS_RECEIVE[kBleBackgroundAdvLpToPeriBridgeIndex] = 0U;
  *reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(ppib30) + 0x400U) =
      0U;
  *reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(ppib21) + 0x400U) =
      0U;

  if (!enable) {
    (void)lpDomain.disconnectPublish(&NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundAdvPrewarmCompareChannel]);
    (void)lpDomain.disconnectPublish(&NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundAdvTxCompareChannel]);
    (void)lpDomain.disconnectPublish(
        &NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundAdvStage1TxCompareChannel]);
    (void)lpDomain.disconnectPublish(
        &NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundAdvStage2TxCompareChannel]);
    (void)lpDomain.disconnectPublish(
        &NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundAdvCleanupCompareChannel]);
    (void)lpDomain.disconnectPublish(
        &NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundAdvFinalCleanupCompareChannel]);
    (void)lpDomain.disconnectSubscribe(&NRF_CLOCK->SUBSCRIBE_XOSTART);
    (void)lpDomain.disconnectSubscribe(&NRF_CLOCK->SUBSCRIBE_XOSTOP);
    (void)lpDomain.disconnectSubscribe(
        &ppib30->SUBSCRIBE_SEND[kBleBackgroundAdvLpToPeriBridgeIndex]);
    (void)lpDomain.disconnectSubscribe(
        &ppib30->SUBSCRIBE_SEND[kBleBackgroundAdvLatencyBridgeIndex]);
    (void)lpDomain.enableChannel(kBleBackgroundAdvLpPrewarmDppiChannel, false);
    (void)lpDomain.enableChannel(kBleBackgroundAdvLpTxDppiChannel, false);
    (void)lpDomain.enableChannel(kBleBackgroundAdvLpStopDppiChannel, false);

    (void)periDomain.disconnectPublish(
        &ppib22->PUBLISH_RECEIVE[kBleBackgroundAdvLpToPeriBridgeIndex]);
    (void)periDomain.disconnectPublish(
        &ppib22->PUBLISH_RECEIVE[kBleBackgroundAdvLatencyBridgeIndex]);
    (void)periDomain.disconnectSubscribe(&NRF_POWER->SUBSCRIBE_CONSTLAT);
    (void)periDomain.disconnectSubscribe(
        &ppib21->SUBSCRIBE_SEND[kBleBackgroundAdvLpToPeriBridgeIndex]);
    (void)periDomain.enableChannel(kBleBackgroundAdvPeriTxDppiChannel, false);
    (void)periDomain.enableChannel(kBleBackgroundAdvPeriLatencyDppiChannel, false);

    (void)radioDomain.disconnectPublish(
        &ppib11->PUBLISH_RECEIVE[kBleBackgroundAdvLpToPeriBridgeIndex]);
    (void)radioDomain.disconnectPublish(&radio_->PUBLISH_PLLREADY);
    (void)radioDomain.disconnectSubscribe(&radio_->SUBSCRIBE_PLLEN);
    (void)radioDomain.disconnectSubscribe(&radio_->SUBSCRIBE_TXEN);
    (void)radioDomain.enableChannel(kBleBackgroundAdvRadioPllenDppiChannel, false);
    (void)radioDomain.enableChannel(kBleBackgroundAdvRadioTxDppiChannel, false);
    return true;
  }

  if (!lpDomain.connect(&NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundAdvPrewarmCompareChannel],
                        &NRF_CLOCK->SUBSCRIBE_XOSTART,
                        kBleBackgroundAdvLpPrewarmDppiChannel)) {
    return false;
  }
  if (backgroundAdvertisingManageLatency_ &&
      (!lpDomain.configureSubscribe(
           &ppib30->SUBSCRIBE_SEND[kBleBackgroundAdvLatencyBridgeIndex],
           kBleBackgroundAdvLpPrewarmDppiChannel, true) ||
       !periDomain.configurePublish(
           &ppib22->PUBLISH_RECEIVE[kBleBackgroundAdvLatencyBridgeIndex],
           kBleBackgroundAdvPeriLatencyDppiChannel, true) ||
       !periDomain.configureSubscribe(&NRF_POWER->SUBSCRIBE_CONSTLAT,
                                      kBleBackgroundAdvPeriLatencyDppiChannel,
                                      true) ||
       !periDomain.enableChannel(kBleBackgroundAdvPeriLatencyDppiChannel,
                                 true))) {
    return false;
  }
  if (!backgroundAdvertisingUseIrqTxKick_) {
    if (!lpDomain.configurePublish(
            &NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundAdvTxCompareChannel],
            kBleBackgroundAdvLpTxDppiChannel, true) ||
        !lpDomain.configurePublish(
            &NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundAdvStage1TxCompareChannel],
            kBleBackgroundAdvLpTxDppiChannel, true) ||
        !lpDomain.configurePublish(
            &NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundAdvStage2TxCompareChannel],
            kBleBackgroundAdvLpTxDppiChannel, true) ||
        !lpDomain.configureSubscribe(
            &ppib30->SUBSCRIBE_SEND[kBleBackgroundAdvLpToPeriBridgeIndex],
            kBleBackgroundAdvLpTxDppiChannel, true) ||
        !lpDomain.enableChannel(kBleBackgroundAdvLpTxDppiChannel, true)) {
      return false;
    }
  }
  if (!lpDomain.configurePublish(
          &NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundAdvCleanupCompareChannel],
          kBleBackgroundAdvLpStopDppiChannel, true) ||
      !lpDomain.configurePublish(
          &NRF_GRTC->PUBLISH_COMPARE[kBleBackgroundAdvFinalCleanupCompareChannel],
          kBleBackgroundAdvLpStopDppiChannel, true) ||
      !lpDomain.configureSubscribe(&NRF_CLOCK->SUBSCRIBE_XOSTOP,
                                   kBleBackgroundAdvLpStopDppiChannel, true) ||
      !lpDomain.enableChannel(kBleBackgroundAdvLpStopDppiChannel, true)) {
    return false;
  }

  if (!backgroundAdvertisingUseIrqTxKick_) {
    if (!periDomain.configurePublish(
            &ppib22->PUBLISH_RECEIVE[kBleBackgroundAdvLpToPeriBridgeIndex],
            kBleBackgroundAdvPeriTxDppiChannel, true) ||
        !periDomain.configureSubscribe(
            &ppib21->SUBSCRIBE_SEND[kBleBackgroundAdvLpToPeriBridgeIndex],
            kBleBackgroundAdvPeriTxDppiChannel, true) ||
        !periDomain.enableChannel(kBleBackgroundAdvPeriTxDppiChannel, true)) {
      return false;
    }
  }
  if (!backgroundAdvertisingUseIrqTxKick_) {
    radio_->EVENTS_PLLREADY = 0U;
    if (!radioDomain.configurePublish(
            &ppib11->PUBLISH_RECEIVE[kBleBackgroundAdvLpToPeriBridgeIndex],
            kBleBackgroundAdvRadioPllenDppiChannel, true) ||
        !radioDomain.configureSubscribe(&radio_->SUBSCRIBE_PLLEN,
                                        kBleBackgroundAdvRadioPllenDppiChannel,
                                        true) ||
        !radioDomain.enableChannel(kBleBackgroundAdvRadioPllenDppiChannel, true) ||
        !radioDomain.configurePublish(&radio_->PUBLISH_PLLREADY,
                                      kBleBackgroundAdvRadioTxDppiChannel, true) ||
        !radioDomain.configureSubscribe(&radio_->SUBSCRIBE_TXEN,
                                        kBleBackgroundAdvRadioTxDppiChannel,
                                        true) ||
        !radioDomain.enableChannel(kBleBackgroundAdvRadioTxDppiChannel, true)) {
      return false;
    }
  }
  return true;
}

bool BleRadio::armBackgroundAdvertising(uint32_t targetTxUs, bool prewarmHfxo,
                                        bool stopHfxoAfterTx) {
  if (!initialized_ || radio_ == nullptr || connected_ ||
      !backgroundAdvertisingEnabled_) {
    return false;
  }

  uint8_t payloadLength = static_cast<uint8_t>(txPacket_[1] & 0x3FU);
  if (payloadLength > kBleLegacyRawPayloadMaxLength) {
    payloadLength = kBleLegacyRawPayloadMaxLength;
  }
  const uint32_t txWindowUs = bleBackgroundAdvertisingPacketWindowUs(payloadLength);
  const bool prewarmInterrupt =
      (backgroundAdvertisingManageLatency_ &&
       !backgroundAdvertisingConstlatHardwareVerified_) ||
      backgroundAdvertisingManageRfPath_ ||
      backgroundAdvertisingUseIrqTxKick_;
  const bool txInterrupt = backgroundAdvertisingUseIrqTxKick_;

  ensureGrtcReady(NRF_GRTC);
  bleBackgroundEnableIrq();
  bleBackgroundDisableClockIrq();

  if (backgroundAdvertisingThreeChannel_) {
    if (!prewarmHfxo || (targetTxUs <= backgroundAdvertisingHfxoLeadUs_)) {
      return false;
    }

    const uint32_t prewarmUs = targetTxUs - backgroundAdvertisingHfxoLeadUs_;
    const uint32_t serviceLeadUs = bleBackgroundAdvertisingServiceLeadUs(
        backgroundAdvertisingInterChannelDelayUs_);
    const uint32_t stage1TxUs =
        targetTxUs + txWindowUs + backgroundAdvertisingInterChannelDelayUs_;
    const uint32_t stage1ServiceUs = stage1TxUs - serviceLeadUs;
    const uint32_t stage2TxUs =
        stage1TxUs + txWindowUs + backgroundAdvertisingInterChannelDelayUs_;
    const uint32_t stage2ServiceUs = stage2TxUs - serviceLeadUs;
    const uint32_t finalCleanupUs = stage2TxUs + txWindowUs;

    NRF_CLOCK->EVENTS_XOTUNED = 0U;
    NRF_CLOCK->EVENTS_XOTUNEFAILED = 0U;
    bleProgramCompare(kBleBackgroundAdvPrewarmCompareChannel, prewarmUs,
                      prewarmInterrupt);
    bleProgramCompare(kBleBackgroundAdvTxCompareChannel, targetTxUs, txInterrupt);
    bleProgramCompare(kBleBackgroundAdvStage1ServiceCompareChannel,
                      stage1ServiceUs, true);
    bleProgramCompare(kBleBackgroundAdvStage1TxCompareChannel, stage1TxUs,
                      txInterrupt);
    bleProgramCompare(kBleBackgroundAdvStage2ServiceCompareChannel,
                      stage2ServiceUs, true);
    bleProgramCompare(kBleBackgroundAdvStage2TxCompareChannel, stage2TxUs,
                      txInterrupt);
    bleProgramCompare(kBleBackgroundAdvFinalCleanupCompareChannel,
                      finalCleanupUs, true);
    bleDisableCompare(kBleBackgroundAdvCleanupCompareChannel, true);

    const uint32_t irqState = bleEnterCritical();
    backgroundAdvertisingDue_ = false;
    backgroundAdvertisingArmed_ = true;
    backgroundAdvertisingNextTxUs_ = targetTxUs;
    backgroundAdvertisingServiceUs_ = stage1ServiceUs;
    backgroundAdvertisingAwaitingHfxoTuned_ = !bleHfxoRunning();
    backgroundAdvertisingPendingTxCompareChannel_ = kBleBackgroundInvalidCompareChannel;
    backgroundAdvertisingPendingServiceCompareChannel_ =
        kBleBackgroundInvalidCompareChannel;
    ++backgroundAdvertisingDebug_.eventArmCount;
    backgroundAdvertisingDebug_.lastEventStartUs = targetTxUs;
    backgroundAdvertisingDebug_.lastChannel =
        static_cast<uint8_t>(backgroundAdvertisingChannel_);
    backgroundAdvertisingDebug_.currentStage = 0U;
    g_bleBackgroundRadio = this;
    bleExitCritical(irqState);
    return true;
  }

  uint32_t serviceUs = targetTxUs + txWindowUs;
  if (!bleBackgroundAdvertisingSetStopPathEnabled(stopHfxoAfterTx)) {
    return false;
  }
  if (prewarmHfxo) {
    if (targetTxUs <= backgroundAdvertisingHfxoLeadUs_) {
      return false;
    }
    const uint32_t prewarmUs = targetTxUs - backgroundAdvertisingHfxoLeadUs_;
    NRF_CLOCK->EVENTS_XOTUNED = 0U;
    NRF_CLOCK->EVENTS_XOTUNEFAILED = 0U;
    bleProgramCompare(kBleBackgroundAdvPrewarmCompareChannel, prewarmUs,
                      prewarmInterrupt);
  } else {
    bleDisableCompare(kBleBackgroundAdvPrewarmCompareChannel, true);
  }
  bleProgramCompare(kBleBackgroundAdvTxCompareChannel, targetTxUs, txInterrupt);
  bleDisableCompare(kBleBackgroundAdvStage1ServiceCompareChannel, true);
  bleDisableCompare(kBleBackgroundAdvStage1TxCompareChannel, false);
  bleDisableCompare(kBleBackgroundAdvStage2ServiceCompareChannel, true);
  bleDisableCompare(kBleBackgroundAdvStage2TxCompareChannel, false);
  bleDisableCompare(kBleBackgroundAdvFinalCleanupCompareChannel, true);
  bleProgramCompare(kBleBackgroundAdvCleanupCompareChannel, serviceUs, true);

  const uint32_t irqState = bleEnterCritical();
  backgroundAdvertisingDue_ = false;
  backgroundAdvertisingArmed_ = true;
  backgroundAdvertisingNextTxUs_ = targetTxUs;
  backgroundAdvertisingServiceUs_ = serviceUs;
  backgroundAdvertisingAwaitingHfxoTuned_ = prewarmHfxo && !bleHfxoRunning();
  backgroundAdvertisingPendingTxCompareChannel_ = kBleBackgroundInvalidCompareChannel;
  backgroundAdvertisingPendingServiceCompareChannel_ =
      kBleBackgroundInvalidCompareChannel;
  ++backgroundAdvertisingDebug_.eventArmCount;
  backgroundAdvertisingDebug_.lastEventStartUs = targetTxUs;
  backgroundAdvertisingDebug_.lastChannel =
      static_cast<uint8_t>(backgroundAdvertisingChannel_);
  backgroundAdvertisingDebug_.currentStage = 0U;
  g_bleBackgroundRadio = this;
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::handleBackgroundAdvertisingPrewarmEvent() {
  if (!ClockControl::startHfxo(false, 0U)) {
    return false;
  }

  if (backgroundAdvertisingManageRfPath_ && backgroundAdvertisingRfPathCollapsed_) {
    if (!ensureRfPathActiveForBle()) {
      return false;
    }
    backgroundAdvertisingRfPathCollapsed_ = false;
    ++backgroundAdvertisingDebug_.rfPathPrewarmRestoreCount;
  }

  bool alreadyActive = false;
  if (bleBackgroundAdvertisingAssertManagedLatency(
          backgroundAdvertisingManageLatency_, &alreadyActive)) {
    backgroundAdvertisingDebug_.constlatActive = 1U;
    if (alreadyActive) {
      backgroundAdvertisingConstlatHardwareVerified_ = true;
      ++backgroundAdvertisingDebug_.constlatPrewarmHardwareCount;
    } else {
      ++backgroundAdvertisingDebug_.constlatPrewarmFallbackCount;
    }
  }

  backgroundAdvertisingDebug_.rfPathActive =
      BoardControl::rfSwitchPowerEnabled() ? 1U : 0U;
  const bool awaitingTune = !bleHfxoRunning();
  const uint32_t irqState = bleEnterCritical();
  backgroundAdvertisingAwaitingHfxoTuned_ = awaitingTune;
  bleExitCritical(irqState);
  if (awaitingTune) {
    bleBackgroundEnableClockTuneInterrupts();
  } else {
    bleBackgroundDisableClockIrq();
  }
  return true;
}

bool BleRadio::rescheduleBackgroundAdvertisingTxCompare(uint8_t compareChannel) {
  if (!initialized_ || radio_ == nullptr || connected_ ||
      !backgroundAdvertisingEnabled_) {
    return false;
  }

  if (backgroundAdvertisingTxKickRetryCountCurrent_ >=
      kBleBackgroundAdvTxKickRetryLimit) {
    return false;
  }

  const uint32_t retryTxUs = bleTimingUs() + kBleBackgroundAdvTxKickRetryUs;
  bleProgramCompare(compareChannel, retryTxUs, true);

  const uint32_t irqState = bleEnterCritical();
  ++backgroundAdvertisingTxKickRetryCountCurrent_;
  ++backgroundAdvertisingDebug_.txKickRetryCount;
  backgroundAdvertisingNextTxUs_ = retryTxUs;
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::rescheduleBackgroundAdvertisingService(uint8_t compareChannel) {
  if (!initialized_ || radio_ == nullptr || connected_ ||
      !backgroundAdvertisingEnabled_) {
    return false;
  }

  uint8_t payloadLength = static_cast<uint8_t>(txPacket_[1] & 0x3FU);
  if (payloadLength > kBleLegacyRawPayloadMaxLength) {
    payloadLength = kBleLegacyRawPayloadMaxLength;
  }

  const uint32_t serviceUs =
      bleTimingUs() + bleBackgroundAdvertisingPacketWindowUs(payloadLength);
  bleProgramCompare(compareChannel, serviceUs, true);

  const uint32_t irqState = bleEnterCritical();
  backgroundAdvertisingDue_ = false;
  backgroundAdvertisingArmed_ = true;
  backgroundAdvertisingServiceUs_ = serviceUs;
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::reconfigureBackgroundAdvertisingTxKick(bool useIrqKick) {
  if (!initialized_ || radio_ == nullptr || connected_ ||
      !backgroundAdvertisingEnabled_) {
    return false;
  }

  if (backgroundAdvertisingUseIrqTxKick_ == useIrqKick) {
    return true;
  }

  backgroundAdvertisingUseIrqTxKick_ = useIrqKick;
  backgroundAdvertisingTxKickRetryCountCurrent_ = 0U;
  if (!configureBackgroundAdvertisingHardware(false) ||
      !configureBackgroundAdvertisingHardware(true)) {
    return false;
  }
  return true;
}

bool BleRadio::settleBackgroundAdvertisingRadioBeforeService(uint32_t waitBudgetUs) {
  if (radio_ == nullptr) {
    return false;
  }

  const uint32_t initialState =
      (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  if ((radio_->EVENTS_DISABLED != 0U) ||
      (initialState == RADIO_STATE_STATE_Disabled)) {
    return true;
  }

  if ((initialState == RADIO_STATE_STATE_TxIdle) &&
      (radio_->EVENTS_READY != 0U || radio_->EVENTS_TXREADY != 0U) &&
      (radio_->EVENTS_PHYEND == 0U) && (radio_->EVENTS_END == 0U)) {
    radio_->TASKS_START = RADIO_TASKS_START_TASKS_START_Trigger;
    ++backgroundAdvertisingDebug_.txStartKickCount;
  }

  bool endSeen = ((radio_->EVENTS_PHYEND != 0U) || (radio_->EVENTS_END != 0U));
  if (!endSeen) {
    endSeen = waitRadioEndBudgeted(radio_, waitBudgetUs, 240000UL);
  }
  if (!endSeen) {
    ++backgroundAdvertisingDebug_.txSettleTimeoutCount;
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    (void)waitRadioDisabledBudgeted(radio_, waitBudgetUs, 240000UL);
    return false;
  }

  bool disabledSeen = ((radio_->EVENTS_DISABLED != 0U) ||
                       (((radio_->STATE & RADIO_STATE_STATE_Msk) >>
                         RADIO_STATE_STATE_Pos) == RADIO_STATE_STATE_Disabled));
  if (!disabledSeen) {
    disabledSeen = waitRadioDisabledBudgeted(radio_, waitBudgetUs, 240000UL);
  }
  if (!disabledSeen) {
    ++backgroundAdvertisingDebug_.txSettleTimeoutCount;
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    disabledSeen = waitRadioDisabledBudgeted(radio_, waitBudgetUs, 240000UL);
  }

  return disabledSeen;
}

void BleRadio::collapseBackgroundAdvertisingRfPathIfManaged() {
  if (!backgroundAdvertisingManageRfPath_ || backgroundAdvertisingRfPathCollapsed_) {
    backgroundAdvertisingDebug_.rfPathActive =
        BoardControl::rfSwitchPowerEnabled() ? 1U : 0U;
    return;
  }

  if (BoardControl::collapseRfPathIdle()) {
    rfPathOwnedByBle_ = true;
    backgroundAdvertisingRfPathCollapsed_ = true;
    ++backgroundAdvertisingDebug_.rfPathIdleCollapseCount;
  }

  backgroundAdvertisingDebug_.rfPathActive =
      BoardControl::rfSwitchPowerEnabled() ? 1U : 0U;
}

bool BleRadio::beginBackgroundAdvertising(uint32_t intervalMs,
                                          BleAdvertisingChannel channel,
                                          uint32_t hfxoLeadUs,
                                          bool addRandomDelay) {
  if (!initialized_ || radio_ == nullptr || connected_) {
    return false;
  }
  if ((channel != BleAdvertisingChannel::k37) &&
      (channel != BleAdvertisingChannel::k38) &&
      (channel != BleAdvertisingChannel::k39)) {
    return false;
  }
  if (pduType_ != BleAdvPduType::kAdvNonConnInd) {
    return false;
  }
  if ((intervalMs == 0U) || (intervalMs > (0xFFFFFFFFUL / 1000UL))) {
    return false;
  }

  if (hfxoLeadUs == 0U) {
    hfxoLeadUs = kBleBackgroundAdvDefaultHfxoLeadUs;
  }

  const uint32_t intervalUs = intervalMs * 1000UL;
  if ((intervalUs < kBleBackgroundAdvMinIntervalUs) ||
      (intervalUs <= (hfxoLeadUs + kBleBackgroundAdvEventWindowUs))) {
    return false;
  }

  stopBackgroundConnectionService();
  stopBackgroundAdvertising();
  snapshotBackgroundAdvertisingRfPathRestoreState();
  if (!ensureRfPathActiveForBle()) {
    backgroundAdvertisingRestoreRfPathValid_ = false;
    return false;
  }
  if (!buildAdvertisingPacket()) {
    releaseRfPathForBle();
    restoreBackgroundAdvertisingRfPathRestoreState();
    return false;
  }

  const uint32_t irqState = bleEnterCritical();
  backgroundAdvertisingEnabled_ = true;
  backgroundAdvertisingArmed_ = false;
  backgroundAdvertisingInProgress_ = false;
  backgroundAdvertisingDue_ = false;
  backgroundAdvertisingThreeChannel_ = false;
  backgroundAdvertisingRandomDelay_ = addRandomDelay;
  backgroundAdvertisingManageRfPath_ = bleBackgroundAdvertisingShouldManageRfPath();
  backgroundAdvertisingRfPathCollapsed_ = false;
  backgroundAdvertisingUseIrqTxKick_ = bleBackgroundAdvertisingInitialUseIrqTxKick();
  backgroundAdvertisingManageLatency_ =
      bleBackgroundAdvertisingShouldManageLatency();
  backgroundAdvertisingConstlatHardwareVerified_ = false;
  backgroundAdvertisingAwaitingHfxoTuned_ = false;
  backgroundAdvertisingChannel_ = channel;
  backgroundAdvertisingIntervalUs_ = intervalUs;
  backgroundAdvertisingInterChannelDelayUs_ = 0U;
  backgroundAdvertisingHfxoLeadUs_ = hfxoLeadUs;
  backgroundAdvertisingNextTxUs_ = 0U;
  backgroundAdvertisingServiceUs_ = 0U;
  backgroundAdvertisingEventStartTxUs_ = 0U;
  backgroundAdvertisingRandomState_ = 0U;
  backgroundAdvertisingPrimaryStage_ = 0U;
  backgroundAdvertisingLastStopReason_ = kBleBackgroundAdvStopReasonNone;
  backgroundAdvertisingTxKickRetryCountCurrent_ = 0U;
  backgroundAdvertisingPendingTxCompareChannel_ = kBleBackgroundInvalidCompareChannel;
  backgroundAdvertisingPendingServiceCompareChannel_ =
      kBleBackgroundInvalidCompareChannel;
  memset(&backgroundAdvertisingDebug_, 0, sizeof(backgroundAdvertisingDebug_));
  backgroundAdvertisingDebug_.enabled = 1U;
  backgroundAdvertisingDebug_.threeChannel = 0U;
  backgroundAdvertisingDebug_.rfPathManaged =
      backgroundAdvertisingManageRfPath_ ? 1U : 0U;
  backgroundAdvertisingDebug_.rfPathActive =
      BoardControl::rfSwitchPowerEnabled() ? 1U : 0U;
  backgroundAdvertisingDebug_.latencyManaged =
      backgroundAdvertisingManageLatency_ ? 1U : 0U;
  backgroundAdvertisingDebug_.constlatActive =
      bleBackgroundAdvertisingConstlatActive() ? 1U : 0U;
  backgroundAdvertisingDebug_.lastChannel = static_cast<uint8_t>(channel);
  bleExitCritical(irqState);

  bleBackgroundAdvertisingConfigureRandomSource(addRandomDelay);

  if (!prepareBackgroundAdvertisingEvent() ||
      !configureBackgroundAdvertisingHardware(true)) {
    stopBackgroundAdvertising();
    return false;
  }

  const uint32_t firstTxUs =
      bleTimingUs() + hfxoLeadUs + kBleBackgroundAdvFirstEventGuardUs;
  backgroundAdvertisingEventStartTxUs_ = firstTxUs;
  if (!armBackgroundAdvertising(firstTxUs, true, true)) {
    stopBackgroundAdvertising();
    return false;
  }
  collapseBackgroundAdvertisingRfPathIfManaged();

  return true;
}

bool BleRadio::beginBackgroundAdvertising3Channel(uint32_t intervalMs,
                                                  uint32_t interChannelDelayUs,
                                                  uint32_t hfxoLeadUs,
                                                  bool addRandomDelay) {
  if (!initialized_ || radio_ == nullptr || connected_) {
    return false;
  }
  if (pduType_ != BleAdvPduType::kAdvNonConnInd) {
    return false;
  }
  if ((intervalMs == 0U) || (intervalMs > (0xFFFFFFFFUL / 1000UL))) {
    return false;
  }
  if (interChannelDelayUs < kBleBackgroundAdvMinInterChannelDelayUs) {
    return false;
  }

  if (hfxoLeadUs == 0U) {
    hfxoLeadUs = kBleBackgroundAdvDefaultHfxoLeadUs;
  }

  const uint32_t intervalUs = intervalMs * 1000UL;
  const uint32_t maxPacketWindowUs =
      kBleBackgroundAdvTxRampUpUs +
      bleDataPduAirTimeUs(kBleLegacyRawPayloadMaxLength) +
      kBleBackgroundAdvDisableGuardUs;
  const uint32_t eventDurationUs =
      3U * maxPacketWindowUs + 2U * interChannelDelayUs;
  if ((intervalUs < kBleBackgroundAdvMinIntervalUs) ||
      (intervalUs <= eventDurationUs)) {
    return false;
  }

  stopBackgroundConnectionService();
  stopBackgroundAdvertising();
  snapshotBackgroundAdvertisingRfPathRestoreState();
  if (!ensureRfPathActiveForBle()) {
    backgroundAdvertisingRestoreRfPathValid_ = false;
    return false;
  }
  if (!buildAdvertisingPacket()) {
    releaseRfPathForBle();
    restoreBackgroundAdvertisingRfPathRestoreState();
    return false;
  }

  const uint32_t irqState = bleEnterCritical();
  backgroundAdvertisingEnabled_ = true;
  backgroundAdvertisingArmed_ = false;
  backgroundAdvertisingInProgress_ = false;
  backgroundAdvertisingDue_ = false;
  backgroundAdvertisingThreeChannel_ = true;
  backgroundAdvertisingRandomDelay_ = addRandomDelay;
  backgroundAdvertisingManageRfPath_ = bleBackgroundAdvertisingShouldManageRfPath();
  backgroundAdvertisingRfPathCollapsed_ = false;
  backgroundAdvertisingUseIrqTxKick_ = bleBackgroundAdvertisingInitialUseIrqTxKick();
  backgroundAdvertisingManageLatency_ =
      bleBackgroundAdvertisingShouldManageLatency();
  backgroundAdvertisingConstlatHardwareVerified_ = false;
  backgroundAdvertisingAwaitingHfxoTuned_ = false;
  backgroundAdvertisingChannel_ = BleAdvertisingChannel::k37;
  backgroundAdvertisingIntervalUs_ = intervalUs;
  backgroundAdvertisingInterChannelDelayUs_ = interChannelDelayUs;
  backgroundAdvertisingHfxoLeadUs_ = hfxoLeadUs;
  backgroundAdvertisingNextTxUs_ = 0U;
  backgroundAdvertisingServiceUs_ = 0U;
  backgroundAdvertisingEventStartTxUs_ = 0U;
  backgroundAdvertisingPrimaryStage_ = 0U;
  backgroundAdvertisingLastStopReason_ = kBleBackgroundAdvStopReasonNone;
  backgroundAdvertisingTxKickRetryCountCurrent_ = 0U;
  backgroundAdvertisingPendingTxCompareChannel_ = kBleBackgroundInvalidCompareChannel;
  backgroundAdvertisingPendingServiceCompareChannel_ =
      kBleBackgroundInvalidCompareChannel;
  memset(&backgroundAdvertisingDebug_, 0, sizeof(backgroundAdvertisingDebug_));
  backgroundAdvertisingDebug_.enabled = 1U;
  backgroundAdvertisingDebug_.threeChannel = 1U;
  backgroundAdvertisingDebug_.rfPathManaged =
      backgroundAdvertisingManageRfPath_ ? 1U : 0U;
  backgroundAdvertisingDebug_.rfPathActive =
      BoardControl::rfSwitchPowerEnabled() ? 1U : 0U;
  backgroundAdvertisingDebug_.latencyManaged =
      backgroundAdvertisingManageLatency_ ? 1U : 0U;
  backgroundAdvertisingDebug_.constlatActive =
      bleBackgroundAdvertisingConstlatActive() ? 1U : 0U;
  backgroundAdvertisingDebug_.lastChannel =
      static_cast<uint8_t>(BleAdvertisingChannel::k37);
  bleExitCritical(irqState);

  bleBackgroundAdvertisingConfigureRandomSource(addRandomDelay);

  if (!prepareBackgroundAdvertisingEvent() ||
      !configureBackgroundAdvertisingHardware(true)) {
    stopBackgroundAdvertising();
    return false;
  }

  const uint32_t firstTxUs =
      bleTimingUs() + hfxoLeadUs + kBleBackgroundAdvFirstEventGuardUs;
  backgroundAdvertisingEventStartTxUs_ = firstTxUs;
  if (!armBackgroundAdvertising(firstTxUs, true, false)) {
    stopBackgroundAdvertising();
    return false;
  }
  collapseBackgroundAdvertisingRfPathIfManaged();

  return true;
}

void BleRadio::stopBackgroundAdvertising() {
  if (backgroundAdvertisingEnabled_ &&
      backgroundAdvertisingLastStopReason_ == kBleBackgroundAdvStopReasonNone) {
    backgroundAdvertisingLastStopReason_ = kBleBackgroundAdvStopReasonExplicit;
  }
  if (backgroundAdvertisingManageLatency_) {
    (void)bleBackgroundAdvertisingReleaseManagedLatency(true);
  }
  bleBackgroundDisableClockIrq();
  NRF_CLOCK->EVENTS_XOTUNED = 0U;
  NRF_CLOCK->EVENTS_XOTUNEERROR = 0U;
  NRF_CLOCK->EVENTS_XOTUNEFAILED = 0U;
  bleBackgroundAdvertisingConfigureRandomSource(false);
  bleDisableCompare(kBleBackgroundAdvPrewarmCompareChannel, true);
  bleDisableCompare(kBleBackgroundAdvTxCompareChannel, false);
  bleDisableCompare(kBleBackgroundAdvCleanupCompareChannel, true);
  bleDisableCompare(kBleBackgroundAdvStage1ServiceCompareChannel, true);
  bleDisableCompare(kBleBackgroundAdvStage1TxCompareChannel, false);
  bleDisableCompare(kBleBackgroundAdvStage2ServiceCompareChannel, true);
  bleDisableCompare(kBleBackgroundAdvStage2TxCompareChannel, false);
  bleDisableCompare(kBleBackgroundAdvFinalCleanupCompareChannel, true);
  (void)configureBackgroundAdvertisingHardware(false);

  if (radio_ != nullptr) {
    radio_->SHORTS = 0U;
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(120000UL);
    clearRadioCoreEvents(radio_);
  }
  NRF_CLOCK->TASKS_XOSTOP = CLOCK_TASKS_XOSTOP_TASKS_XOSTOP_Trigger;

  const uint32_t irqState = bleEnterCritical();
  backgroundAdvertisingEnabled_ = false;
  backgroundAdvertisingArmed_ = false;
  backgroundAdvertisingInProgress_ = false;
  backgroundAdvertisingDue_ = false;
  backgroundAdvertisingThreeChannel_ = false;
  backgroundAdvertisingRandomDelay_ = false;
  backgroundAdvertisingManageRfPath_ = false;
  backgroundAdvertisingRfPathCollapsed_ = false;
  backgroundAdvertisingUseIrqTxKick_ = bleBackgroundAdvertisingInitialUseIrqTxKick();
  backgroundAdvertisingManageLatency_ = false;
  backgroundAdvertisingConstlatHardwareVerified_ = false;
  backgroundAdvertisingAwaitingHfxoTuned_ = false;
  backgroundAdvertisingChannel_ = BleAdvertisingChannel::k37;
  backgroundAdvertisingIntervalUs_ = 0U;
  backgroundAdvertisingInterChannelDelayUs_ = 0U;
  backgroundAdvertisingHfxoLeadUs_ = kBleBackgroundAdvDefaultHfxoLeadUs;
  backgroundAdvertisingNextTxUs_ = 0U;
  backgroundAdvertisingServiceUs_ = 0U;
  backgroundAdvertisingEventStartTxUs_ = 0U;
  backgroundAdvertisingRandomState_ = 0U;
  backgroundAdvertisingPrimaryStage_ = 0U;
  backgroundAdvertisingTxKickRetryCountCurrent_ = 0U;
  backgroundAdvertisingPendingTxCompareChannel_ = kBleBackgroundInvalidCompareChannel;
  backgroundAdvertisingPendingServiceCompareChannel_ =
      kBleBackgroundInvalidCompareChannel;
  backgroundAdvertisingDebug_.enabled = 0U;
  backgroundAdvertisingDebug_.threeChannel = 0U;
  backgroundAdvertisingDebug_.rfPathManaged = 0U;
  backgroundAdvertisingDebug_.rfPathActive =
      BoardControl::rfSwitchPowerEnabled() ? 1U : 0U;
  backgroundAdvertisingDebug_.latencyManaged = 0U;
  backgroundAdvertisingDebug_.constlatActive =
      bleBackgroundAdvertisingConstlatActive() ? 1U : 0U;
  backgroundAdvertisingDebug_.currentStage = 0U;
  if (!backgroundConnectionServiceEnabled_) {
    g_bleBackgroundPendSvPending = 0U;
    blePendSvClearPending();
  }
  if (g_bleBackgroundRadio == this && !backgroundServicesActive() &&
      deferredConnectionEventCount_ == 0U &&
      deferredGattWriteCount_ == 0U && deferredTraceCount_ == 0U) {
    g_bleBackgroundRadio = nullptr;
  }
  bleExitCritical(irqState);

  restoreBackgroundAdvertisingRfPathRestoreState();
  releaseRfPathForBle();
}

bool BleRadio::isBackgroundAdvertisingEnabled() const {
  return backgroundAdvertisingEnabled_;
}

uint8_t BleRadio::getBackgroundAdvertisingLastStopReason() const {
  return backgroundAdvertisingLastStopReason_;
}

void BleRadio::getBackgroundAdvertisingDebugCounters(
    BleBackgroundAdvertisingDebugCounters* out) const {
  if (out == nullptr) {
    return;
  }
  const uint32_t irqState = bleEnterCritical();
  *out = backgroundAdvertisingDebug_;
  bleExitCritical(irqState);
}

void BleRadio::clearBackgroundAdvertisingDebugCounters() {
  const uint32_t irqState = bleEnterCritical();
  const uint8_t enabled = backgroundAdvertisingEnabled_ ? 1U : 0U;
  const uint8_t threeChannel = backgroundAdvertisingThreeChannel_ ? 1U : 0U;
  const uint8_t rfPathManaged = backgroundAdvertisingManageRfPath_ ? 1U : 0U;
  const uint8_t rfPathActive =
      BoardControl::rfSwitchPowerEnabled() ? 1U : 0U;
  const uint8_t latencyManaged = backgroundAdvertisingManageLatency_ ? 1U : 0U;
  const uint8_t constlatActive =
      bleBackgroundAdvertisingConstlatActive() ? 1U : 0U;
  const uint8_t lastChannel = static_cast<uint8_t>(backgroundAdvertisingChannel_);
  const uint8_t currentStage = backgroundAdvertisingPrimaryStage_;
  memset(&backgroundAdvertisingDebug_, 0, sizeof(backgroundAdvertisingDebug_));
  backgroundAdvertisingDebug_.enabled = enabled;
  backgroundAdvertisingDebug_.threeChannel = threeChannel;
  backgroundAdvertisingDebug_.rfPathManaged = rfPathManaged;
  backgroundAdvertisingDebug_.rfPathActive = rfPathActive;
  backgroundAdvertisingDebug_.latencyManaged = latencyManaged;
  backgroundAdvertisingDebug_.constlatActive = constlatActive;
  backgroundAdvertisingDebug_.lastChannel = lastChannel;
  backgroundAdvertisingDebug_.currentStage = currentStage;
  bleExitCritical(irqState);
}

bool BleRadio::backgroundAdvertisingConsumePendingPrewarmFromFallback() {
  if (!backgroundAdvertisingEnabled_ || radio_ == nullptr || connected_ ||
      backgroundAdvertisingInProgress_) {
    return false;
  }
  if ((__get_PRIMASK() & 1U) == 0U &&
      NRF54L15_CLEAN_BLE_BACKGROUND_GRTC_IRQ_RUN != 0) {
    return false;
  }

  const uint32_t irqState = bleEnterCritical();
  const bool pending = bleCompareEventPending(kBleBackgroundAdvPrewarmCompareChannel);
  if (pending) {
    bleDisableCompare(kBleBackgroundAdvPrewarmCompareChannel, true);
  }
  bleExitCritical(irqState);
  if (!pending) {
    return false;
  }
  if (!handleBackgroundAdvertisingPrewarmEvent()) {
    backgroundAdvertisingLastStopReason_ = kBleBackgroundAdvStopReasonPrewarmFailed;
    stopBackgroundAdvertising();
  }
  return true;
}

bool BleRadio::backgroundAdvertisingNeedsIdleService() const {
  if (!backgroundAdvertisingEnabled_ || radio_ == nullptr || connected_ ||
      backgroundAdvertisingInProgress_) {
    return false;
  }
  if (backgroundAdvertisingDue_) {
    return true;
  }
  if ((__get_PRIMASK() & 1U) == 0U &&
      NRF54L15_CLEAN_BLE_BACKGROUND_GRTC_IRQ_RUN != 0) {
    return false;
  }
  if (bleCompareEventPending(kBleBackgroundAdvCleanupCompareChannel) ||
      bleCompareEventPending(kBleBackgroundAdvStage1ServiceCompareChannel) ||
      bleCompareEventPending(kBleBackgroundAdvStage2ServiceCompareChannel) ||
      bleCompareEventPending(kBleBackgroundAdvFinalCleanupCompareChannel)) {
    return true;
  }
  return false;
}

bool BleRadio::backgroundAdvertisingConsumePendingCompareFromFallback() {
  if (!backgroundAdvertisingEnabled_ || radio_ == nullptr || connected_ ||
      backgroundAdvertisingInProgress_) {
    return false;
  }
  if ((__get_PRIMASK() & 1U) == 0U &&
      NRF54L15_CLEAN_BLE_BACKGROUND_GRTC_IRQ_RUN != 0) {
    return false;
  }

  const uint32_t irqState = bleEnterCritical();
  bool consumed = false;
  if (bleCompareEventPending(kBleBackgroundAdvCleanupCompareChannel)) {
    bleDisableCompare(kBleBackgroundAdvCleanupCompareChannel, true);
    backgroundAdvertisingArmed_ = false;
    backgroundAdvertisingDue_ = true;
    consumed = true;
  } else if (bleCompareEventPending(kBleBackgroundAdvStage1ServiceCompareChannel)) {
    bleDisableCompare(kBleBackgroundAdvStage1ServiceCompareChannel, true);
    backgroundAdvertisingArmed_ = false;
    backgroundAdvertisingDue_ = true;
    consumed = true;
  } else if (bleCompareEventPending(kBleBackgroundAdvStage2ServiceCompareChannel)) {
    bleDisableCompare(kBleBackgroundAdvStage2ServiceCompareChannel, true);
    backgroundAdvertisingArmed_ = false;
    backgroundAdvertisingDue_ = true;
    consumed = true;
  } else if (bleCompareEventPending(kBleBackgroundAdvFinalCleanupCompareChannel)) {
    bleDisableCompare(kBleBackgroundAdvFinalCleanupCompareChannel, true);
    backgroundAdvertisingArmed_ = false;
    backgroundAdvertisingDue_ = true;
    consumed = true;
  }
  bleExitCritical(irqState);
  return consumed;
}

bool BleRadio::serviceBackgroundAdvertising() {
  if (NRF54L15_CLEAN_BLE_BACKGROUND_SERVICE == 0) {
    return false;
  }

  const uint32_t irqState = bleEnterCritical();
  if (!backgroundAdvertisingEnabled_ || radio_ == nullptr || connected_) {
    bleExitCritical(irqState);
    backgroundAdvertisingLastStopReason_ = kBleBackgroundAdvStopReasonInvalidState;
    stopBackgroundAdvertising();
    return false;
  }
  if (backgroundAdvertisingInProgress_) {
    bleExitCritical(irqState);
    return false;
  }

  const bool cleanupDue = backgroundAdvertisingDue_;
  if (!cleanupDue) {
    if (!backgroundAdvertisingArmed_ ||
        static_cast<int32_t>(bleTimingUs() - backgroundAdvertisingServiceUs_) < 0) {
      bleExitCritical(irqState);
      return false;
    }
  }

  backgroundAdvertisingInProgress_ = true;
  backgroundAdvertisingDue_ = false;
  backgroundAdvertisingArmed_ = false;
  ++backgroundAdvertisingDebug_.serviceRunCount;
  backgroundAdvertisingDebug_.latencyManaged =
      backgroundAdvertisingManageLatency_ ? 1U : 0U;
  backgroundAdvertisingDebug_.constlatActive =
      bleBackgroundAdvertisingConstlatActive() ? 1U : 0U;
  const uint8_t radioState = static_cast<uint8_t>(
      (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos);
  const bool txReadySeen = (radio_->EVENTS_READY != 0U);
  const bool txPhyendSeen = (radio_->EVENTS_PHYEND != 0U) ||
                            (radio_->EVENTS_END != 0U);
  const bool txDisabledSeen = (radio_->EVENTS_DISABLED != 0U);
  if (backgroundAdvertisingManageLatency_ && bleBackgroundAdvertisingConstlatActive()) {
    ++backgroundAdvertisingDebug_.constlatServiceObservedCount;
  }
  backgroundAdvertisingDebug_.lastServiceUs = backgroundAdvertisingServiceUs_;
  bleExitCritical(irqState);

  const uint32_t settleBudgetUs =
      (backgroundAdvertisingThreeChannel_ && backgroundAdvertisingPrimaryStage_ < 2U)
          ? kBleBackgroundAdvStageSettleBudgetUs
          : kBleBackgroundAdvEventSettleBudgetUs;
  if (!settleBackgroundAdvertisingRadioBeforeService(settleBudgetUs)) {
    const uint32_t exitIrqState = bleEnterCritical();
    backgroundAdvertisingInProgress_ = false;
    bleExitCritical(exitIrqState);
    backgroundAdvertisingLastStopReason_ = kBleBackgroundAdvStopReasonSettleTimeout;
    stopBackgroundAdvertising();
    return false;
  }

  {
    const uint32_t settleIrqState = bleEnterCritical();
    const uint8_t finalRadioState = static_cast<uint8_t>(
        (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos);
    const bool finalTxReadySeen =
        txReadySeen || (radio_->EVENTS_READY != 0U) || (radio_->EVENTS_TXREADY != 0U);
    const bool finalTxPhyendSeen =
        txPhyendSeen || (radio_->EVENTS_PHYEND != 0U) || (radio_->EVENTS_END != 0U);
    const bool finalTxDisabledSeen =
        txDisabledSeen || (radio_->EVENTS_DISABLED != 0U) ||
        (finalRadioState == RADIO_STATE_STATE_Disabled);
    if (finalTxReadySeen) {
      ++backgroundAdvertisingDebug_.txReadyCount;
    }
    if (finalTxPhyendSeen) {
      ++backgroundAdvertisingDebug_.txPhyendCount;
    }
    if (finalTxDisabledSeen) {
      ++backgroundAdvertisingDebug_.txDisabledCount;
    }
    backgroundAdvertisingDebug_.lastRadioState = finalRadioState;
    backgroundAdvertisingDebug_.lastTxReadySeen = finalTxReadySeen ? 1U : 0U;
    backgroundAdvertisingDebug_.lastTxPhyendSeen = finalTxPhyendSeen ? 1U : 0U;
    backgroundAdvertisingDebug_.lastTxDisabledSeen = finalTxDisabledSeen ? 1U : 0U;
    bleExitCritical(settleIrqState);

    if (!backgroundAdvertisingUseIrqTxKick_ &&
        !finalTxReadySeen && !finalTxPhyendSeen) {
      g_bleBackgroundAdvDirectTxKickBlacklisted = 1U;
      if (!reconfigureBackgroundAdvertisingTxKick(true)) {
        const uint32_t exitIrqState = bleEnterCritical();
        backgroundAdvertisingInProgress_ = false;
        bleExitCritical(exitIrqState);
        backgroundAdvertisingLastStopReason_ =
            kBleBackgroundAdvStopReasonTxCompareFailed;
        stopBackgroundAdvertising();
        return false;
      }
      ++backgroundAdvertisingDebug_.txKickFallbackCount;
      radio_->SHORTS = bleRadioShortsTxOnly();
    }
  }

  uint8_t payloadLength = static_cast<uint8_t>(txPacket_[1] & 0x3FU);
  if (payloadLength > kBleLegacyRawPayloadMaxLength) {
    payloadLength = kBleLegacyRawPayloadMaxLength;
  }
  const uint32_t txWindowUs = bleBackgroundAdvertisingPacketWindowUs(payloadLength);

  bool prepared = false;
  bool armed = false;
  if (backgroundAdvertisingThreeChannel_ &&
      backgroundAdvertisingPrimaryStage_ < 2U) {
    ++backgroundAdvertisingPrimaryStage_;
    ++backgroundAdvertisingDebug_.stageAdvanceCount;
    backgroundAdvertisingDebug_.currentStage = backgroundAdvertisingPrimaryStage_;
    prepared = prepareBackgroundAdvertisingEvent();
    if (prepared) {
      const uint32_t serviceLeadUs = bleBackgroundAdvertisingServiceLeadUs(
          backgroundAdvertisingInterChannelDelayUs_);
      if (backgroundAdvertisingPrimaryStage_ >= 2U) {
        const uint32_t stage2TxUs =
            backgroundAdvertisingEventStartTxUs_ +
            2U * (txWindowUs + backgroundAdvertisingInterChannelDelayUs_);
        backgroundAdvertisingServiceUs_ = stage2TxUs + txWindowUs;
      } else {
        const uint32_t stage2TxUs =
            backgroundAdvertisingEventStartTxUs_ +
            2U * (txWindowUs + backgroundAdvertisingInterChannelDelayUs_);
        backgroundAdvertisingServiceUs_ = stage2TxUs - serviceLeadUs;
      }
      armed = prepared;
    }
  } else {
    ++backgroundAdvertisingDebug_.eventCompleteCount;
    backgroundAdvertisingDebug_.lastCompletedEventStartUs =
        backgroundAdvertisingEventStartTxUs_;
    if (bleHfxoRunning()) {
      ClockControl::stopHfxo();
    }
    uint32_t nextEventStartUs =
        backgroundAdvertisingEventStartTxUs_ + backgroundAdvertisingIntervalUs_;
    if (backgroundAdvertisingRandomDelay_) {
      bool usedHardware = false;
      const uint32_t randomDelayUs = bleBackgroundAdvertisingRandomDelayUs(
          &backgroundAdvertisingRandomState_, address_[0], &usedHardware);
      backgroundAdvertisingDebug_.lastRandomDelayUs = randomDelayUs;
      if (usedHardware) {
        ++backgroundAdvertisingDebug_.randomHardwareCount;
      } else {
        ++backgroundAdvertisingDebug_.randomFallbackCount;
      }
      nextEventStartUs += randomDelayUs;
    } else {
      backgroundAdvertisingDebug_.lastRandomDelayUs = 0U;
    }
    backgroundAdvertisingPrimaryStage_ = 0U;
    backgroundAdvertisingDebug_.currentStage = 0U;
    backgroundAdvertisingEventStartTxUs_ = nextEventStartUs;
    prepared = prepareBackgroundAdvertisingEvent();
    if (prepared) {
      armed = armBackgroundAdvertising(nextEventStartUs, true,
                                       !backgroundAdvertisingThreeChannel_);
      if (armed && bleBackgroundAdvertisingReleaseManagedLatency(
                       backgroundAdvertisingManageLatency_)) {
        ++backgroundAdvertisingDebug_.lowPowerReleaseCount;
      }
      if (armed) {
        collapseBackgroundAdvertisingRfPathIfManaged();
      }
    }
  }

  const uint32_t exitIrqState = bleEnterCritical();
  backgroundAdvertisingInProgress_ = false;
  bleExitCritical(exitIrqState);

  if (!prepared || !armed) {
    backgroundAdvertisingLastStopReason_ =
        prepared ? kBleBackgroundAdvStopReasonArmFailed
                 : kBleBackgroundAdvStopReasonPrepareFailed;
    stopBackgroundAdvertising();
    return false;
  }

  return true;
}

bool BleRadio::advertiseExtendedEvent(uint32_t auxOffsetUs,
                                      uint32_t interPrimaryDelayUs,
                                      uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || connected_) {
    return false;
  }
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  uint32_t actualAuxOffsetUs = 0U;
  if (!buildExtendedAdvertisingPackets(auxOffsetUs, &actualAuxOffsetUs)) {
    endUnconnectedRadioActivity();
    return false;
  }

  {
    static uint32_t seed = 0U;
    if (seed == 0U) {
      seed = micros() ^ (static_cast<uint32_t>(address_[0]) << 24U);
    }
    seed = seed * 1103515245U + 12345U;
    const uint32_t advDelayUs = (seed % 10001U);
    if (advDelayUs > 0U) {
      delayMicroseconds(advDelayUs);
    }
  }

  const BleAdvertisingChannel channels[] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };
  const uint8_t startIndex = static_cast<uint8_t>(advCycleStartIndex_ % 3U);

  for (size_t i = 0U; i < 3U; ++i) {
    const uint8_t idx = static_cast<uint8_t>((startIndex + i) % 3U);
    if (!setAdvertisingChannel(channels[idx])) {
      endUnconnectedRadioActivity();
      return false;
    }

    uint32_t primaryReadyUs = 0U;
    if (!transmitPreparedPacketOnCurrentChannel(&extendedPrimaryPacket_[0], spinLimit,
                                                &primaryReadyUs)) {
      endUnconnectedRadioActivity();
      return false;
    }

    if (!setDataChannel(extendedAdvAuxChannel_)) {
      endUnconnectedRadioActivity();
      return false;
    }

    const uint32_t auxTargetUs = primaryReadyUs + actualAuxOffsetUs;
    uint32_t nowUs = bleTimingUs();
    while (!timeReachedUs(nowUs, auxTargetUs)) {
      nowUs = bleTimingUs();
    }

    uint32_t secondaryReadyUs = 0U;
    for (uint8_t secondaryIndex = 0U;
         secondaryIndex < extendedSecondaryPacketCount_;
         ++secondaryIndex) {
      if (!transmitPreparedPacketOnCurrentChannel(
              &extendedSecondaryPackets_[secondaryIndex][0],
              spinLimit,
              ((secondaryIndex + 1U) < extendedSecondaryPacketCount_) ? &secondaryReadyUs
                                                                      : nullptr)) {
        endUnconnectedRadioActivity();
        return false;
      }

      if ((secondaryIndex + 1U) < extendedSecondaryPacketCount_) {
        const uint32_t nextTargetUs = secondaryReadyUs + actualAuxOffsetUs;
        nowUs = bleTimingUs();
        while (!timeReachedUs(nowUs, nextTargetUs)) {
          nowUs = bleTimingUs();
        }
      }
    }

    if (((i + 1U) < 3U) && (interPrimaryDelayUs > 0U)) {
      delayMicroseconds(interPrimaryDelayUs);
    }
  }

  advCycleStartIndex_ = static_cast<uint8_t>((startIndex + 1U) % 3U);
  endUnconnectedRadioActivity();
  return true;
}

bool BleRadio::advertiseExtendedScannableEvent(uint32_t auxOffsetUs,
                                               uint32_t interPrimaryDelayUs,
                                               uint32_t requestListenSpinLimit,
                                               uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || connected_) {
    return false;
  }
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  uint32_t actualAuxOffsetUs = 0U;
  uint32_t actualScanRspChainOffsetUs = 0U;
  if (!buildExtendedScannableAdvertisingPackets(auxOffsetUs, &actualAuxOffsetUs,
                                                &actualScanRspChainOffsetUs)) {
    endUnconnectedRadioActivity();
    return false;
  }

  {
    static uint32_t seed = 0U;
    if (seed == 0U) {
      seed = micros() ^ (static_cast<uint32_t>(address_[0]) << 24U);
    }
    seed = seed * 1103515245U + 12345U;
    const uint32_t advDelayUs = (seed % 10001U);
    if (advDelayUs > 0U) {
      delayMicroseconds(advDelayUs);
    }
  }

  const BleAdvertisingChannel channels[] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };
  const uint8_t startIndex = static_cast<uint8_t>(advCycleStartIndex_ % 3U);

  for (size_t i = 0U; i < 3U; ++i) {
    const uint8_t idx = static_cast<uint8_t>((startIndex + i) % 3U);
    if (!setAdvertisingChannel(channels[idx])) {
      endUnconnectedRadioActivity();
      return false;
    }

    uint32_t primaryReadyUs = 0U;
    if (!transmitPreparedPacketOnCurrentChannel(&extendedPrimaryPacket_[0], spinLimit,
                                                &primaryReadyUs)) {
      endUnconnectedRadioActivity();
      return false;
    }

    if (!setDataChannel(extendedAdvAuxChannel_)) {
      endUnconnectedRadioActivity();
      return false;
    }

    const uint32_t auxTargetUs = primaryReadyUs + actualAuxOffsetUs;
    uint32_t nowUs = bleTimingUs();
    while (!timeReachedUs(nowUs, auxTargetUs)) {
      nowUs = bleTimingUs();
    }

    if (!handleExtendedScanRequestAndMaybeRespond(requestListenSpinLimit,
                                                  actualScanRspChainOffsetUs,
                                                  spinLimit)) {
      endUnconnectedRadioActivity();
      return false;
    }

    if (((i + 1U) < 3U) && (interPrimaryDelayUs > 0U)) {
      delayMicroseconds(interPrimaryDelayUs);
    }
  }

  advCycleStartIndex_ = static_cast<uint8_t>((startIndex + 1U) % 3U);
  endUnconnectedRadioActivity();
  return true;
}

bool BleRadio::handleExtendedScanRequestAndMaybeRespond(
    uint32_t requestListenSpinLimit,
    uint32_t scanRspChainOffsetUs,
    uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || connected_) {
    return false;
  }

  clearRadioCoreEvents(radio_);
  memset(rxPacket_, 0, sizeof(rxPacket_));
  memcpy(rxPacket_, extendedAuxAdvPacket_, sizeof(extendedAuxAdvPacket_));
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk) |
      ((RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos) &
       RADIO_SHORTS_DISABLED_RXEN_Msk) |
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool txEndSeen = waitForEnd(spinLimit);
  if (!txEndSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return false;
  }

  bool txDisabled = waitDisabled(spinLimit / 2U + 1U);
  if (!txDisabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    txDisabled = waitDisabled(spinLimit / 2U + 1U);
  }
  if (!txDisabled) {
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return false;
  }

  radio_->EVENTS_DISABLED = 0U;
  radio_->EVENTS_END = 0U;
  radio_->EVENTS_PHYEND = 0U;
  radio_->EVENTS_CRCOK = 0U;
  radio_->EVENTS_CRCERROR = 0U;
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  const uint32_t state =
      (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  if (state == RADIO_STATE_STATE_Disabled) {
    radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  }

  uint32_t rxEndUs = 0U;
  const bool endSeen = waitRadioEndBudgeted(
      radio_, kBleAdvRequestListenMaxUs, requestListenSpinLimit, &rxEndUs);
  if (!endSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return true;
  }
  if (rxEndUs == 0U) {
    rxEndUs = bleTimingUs();
  }

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOk = (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  const uint8_t hdr = rxPacket_[0];
  const uint8_t pduType = static_cast<uint8_t>(hdr & 0x0FU);
  const uint8_t length = static_cast<uint8_t>(rxPacket_[1] & 0x3FU);
  const bool rxAddrRandom = ((hdr >> 7U) & 0x1U) != 0U;
  const uint8_t* payload = &rxPacket_[2];

  bool disabled = waitDisabled(spinLimit / 2U + 1U);
  if (!disabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    disabled = waitDisabled(spinLimit / 2U + 1U);
  }
  radio_->SHORTS = 0U;
  if (!disabled) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  bool hasScanReq = false;
  if (crcOk && (length >= 12U) && (pduType == kBlePduScanReq)) {
    const uint8_t* advertiserAddress = &payload[6];
    hasScanReq = bleAddressEqual(advertiserAddress, &address_[0]) &&
                 (rxAddrRandom ==
                  (addressType_ == BleAddressType::kRandomStatic));
  }

  if (!hasScanReq || (extendedScanRspPacketCount_ == 0U)) {
    clearRadioCoreEvents(radio_);
    return true;
  }

  clearRadioCoreEvents(radio_);
  radio_->PACKETPTR = static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(&extendedScanRspPackets_[0][0]));
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  const uint32_t txTriggerTargetUs = rxEndUs + kBleConnTxenAfterRxUs;
  bleTriggerTxAtTargetUsCritical(radio_, txTriggerTargetUs);
  uint32_t scanRspReadyUs = 0U;
  bool readySeen = false;
  bool responseDisabled = false;
  uint32_t remaining = spinLimit;
  while (remaining-- > 0U) {
    if (!readySeen && (radio_->EVENTS_READY != 0U)) {
      readySeen = true;
      scanRspReadyUs = bleTimingUs();
    }
    if (radio_->EVENTS_DISABLED != 0U) {
      responseDisabled = true;
      break;
    }

    const uint32_t currentState =
        (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (currentState == RADIO_STATE_STATE_Disabled) {
      responseDisabled = true;
      break;
    }
  }
  if (!responseDisabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    responseDisabled = waitDisabled(spinLimit / 2U + 1U);
  }
  radio_->SHORTS = 0U;
  if (!responseDisabled || !readySeen) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  uint32_t previousPacketStartUs = scanRspReadyUs;
  for (uint8_t packetIndex = 1U; packetIndex < extendedScanRspPacketCount_;
       ++packetIndex) {
    const uint32_t nextTargetUs = previousPacketStartUs + scanRspChainOffsetUs;
    uint32_t nowUs = bleTimingUs();
    while (!timeReachedUs(nowUs, nextTargetUs)) {
      nowUs = bleTimingUs();
    }

    uint32_t nextReadyUs = 0U;
    if (!transmitPreparedPacketOnCurrentChannel(
            &extendedScanRspPackets_[packetIndex][0], spinLimit, &nextReadyUs)) {
      clearRadioCoreEvents(radio_);
      return false;
    }
    previousPacketStartUs = nextReadyUs;
  }

  clearRadioCoreEvents(radio_);
  return true;
}

bool BleRadio::advertiseInteractOncePrepared(BleAdvertisingChannel channel,
                                             BleAdvInteraction* interaction,
                                             uint32_t requestListenSpinLimit,
                                             uint32_t spinLimit) {
  if (interaction != nullptr) {
    interaction->channel = channel;
    interaction->receivedScanRequest = false;
    interaction->scanResponseTransmitted = false;
    interaction->receivedConnectInd = false;
    interaction->connectIndChSel2 = false;
    interaction->peerAddressRandom = false;
    interaction->rssiDbm = 0;
    memset(interaction->peerAddress, 0, sizeof(interaction->peerAddress));
  }

  if (!initialized_ || radio_ == nullptr || connected_) {
    return false;
  }
  if (pduType_ != BleAdvPduType::kAdvInd &&
      pduType_ != BleAdvPduType::kAdvScanInd) {
    return false;
  }
  if (!setAdvertisingChannel(channel)) {
    return false;
  }

  // Keep TX->RX turnaround in hardware to catch requests sent at T_IFS timing.
  clearRadioCoreEvents(radio_);
  memset(rxPacket_, 0, sizeof(rxPacket_));
  {
    const size_t copyLen =
        (sizeof(rxPacket_) < sizeof(txPacket_)) ? sizeof(rxPacket_) : sizeof(txPacket_);
    memcpy(rxPacket_, txPacket_, copyLen);
  }
  radio_->PACKETPTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
  radio_->SHORTS =
      ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
       RADIO_SHORTS_TXREADY_START_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk) |
      ((RADIO_SHORTS_DISABLED_RXEN_Enabled << RADIO_SHORTS_DISABLED_RXEN_Pos) &
       RADIO_SHORTS_DISABLED_RXEN_Msk) |
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk);

  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;
  const bool txEndSeen = waitForEnd(spinLimit);
  if (!txEndSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return false;
  }

  bool txDisabled = waitDisabled(spinLimit / 2U + 1U);
  if (!txDisabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    txDisabled = waitDisabled(spinLimit / 2U + 1U);
  }
  if (!txDisabled) {
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return false;
  }

  // Clear TX completion events and remove DISABLED->RX shortcut so RX disable
  // completes cleanly once the listen window ends.
  radio_->EVENTS_DISABLED = 0U;
  radio_->EVENTS_END = 0U;
  radio_->EVENTS_PHYEND = 0U;
  radio_->EVENTS_CRCOK = 0U;
  radio_->EVENTS_CRCERROR = 0U;
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  // Defensive fallback: if shortcut path did not enter RX, arm RX manually.
  const uint32_t state =
      (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  if (state == RADIO_STATE_STATE_Disabled) {
    radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  }

  uint32_t rxEndUs = 0U;
  const bool endSeen = waitRadioEndBudgeted(
      radio_, kBleAdvRequestListenMaxUs, requestListenSpinLimit, &rxEndUs);
  if (!endSeen) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitDisabled(spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return true;
  }
  if (rxEndUs == 0U) {
    rxEndUs = bleTimingUs();
  }

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOk = (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  const uint8_t hdr = rxPacket_[0];
  const uint8_t pduType = static_cast<uint8_t>(hdr & 0x0FU);
  const bool pduChSel2 = ((hdr >> 5U) & 0x1U) != 0U;
  const uint8_t length = static_cast<uint8_t>(rxPacket_[1] & 0x3FU);
  const bool txAddrRandom = ((hdr >> 6U) & 0x1U) != 0U;
  const bool rxAddrRandom = ((hdr >> 7U) & 0x1U) != 0U;
  const uint8_t* payload = &rxPacket_[2];

  bool disabled = waitDisabled(spinLimit / 2U + 1U);
  if (!disabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    disabled = waitDisabled(spinLimit / 2U + 1U);
  }
  radio_->SHORTS = 0U;
  if (!disabled) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  bool hasScanReq = false;
  bool hasConnectInd = false;
  bool connectAccepted = false;
  bool addressMatch = false;
  const bool connectable = (pduType_ == BleAdvPduType::kAdvInd);
  if (crcOk && length >= 12U) {
    const uint8_t* scannerOrInitiator = payload;
    const uint8_t* advertiserAddress = &payload[6];
    const bool advertiserAddressMatch =
        bleAddressEqual(advertiserAddress, &address_[0]);
    const bool advertiserAddressTypeMatch =
        (rxAddrRandom == (addressType_ == BleAddressType::kRandomStatic));
    addressMatch = advertiserAddressMatch && advertiserAddressTypeMatch;
    // Some Android/Qualcomm centrals appear to reuse the correct advertiser
    // address bytes while misreporting the RxAdd bit in SCAN_REQ/CONNECT_IND.
    // Accept those legacy advertising-channel requests as long as the address
    // bytes match exactly.
    if (!addressMatch && advertiserAddressMatch &&
        (pduType == kBlePduScanReq || pduType == kBlePduConnectInd)) {
      addressMatch = true;
    }

    if (addressMatch && pduType == kBlePduScanReq) {
      hasScanReq = true;
      if (interaction != nullptr) {
        memcpy(&interaction->peerAddress[0], scannerOrInitiator, 6U);
      }
    } else if (addressMatch && pduType == kBlePduConnectInd && connectable &&
               (length >= 34U)) {
      hasConnectInd = true;
      if (interaction != nullptr) {
        memcpy(&interaction->peerAddress[0], scannerOrInitiator, 6U);
      }
      connectAccepted = startConnectionFromConnectInd(payload, length, txAddrRandom,
                                                      pduChSel2, rxEndUs);
    }
  }

  if (interaction != nullptr) {
    interaction->channel = channel;
    interaction->rssiDbm = radioRssiDbm(radio_);
    interaction->peerAddressRandom = txAddrRandom;
    interaction->receivedScanRequest = hasScanReq;
    interaction->receivedConnectInd = hasConnectInd;
    interaction->connectIndChSel2 = hasConnectInd && useChSel2_ && pduChSel2;
  }

  bool txScanRspOk = false;
  const bool scannable = (pduType_ == BleAdvPduType::kAdvInd) ||
                         (pduType_ == BleAdvPduType::kAdvScanInd);
  if (hasScanReq && scannable && !connectAccepted) {
    clearRadioCoreEvents(radio_);
    radio_->PACKETPTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&scanRspPacket_[0]));
    radio_->SHORTS =
        ((RADIO_SHORTS_TXREADY_START_Enabled << RADIO_SHORTS_TXREADY_START_Pos) &
         RADIO_SHORTS_TXREADY_START_Msk) |
        ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
          << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
         RADIO_SHORTS_PHYEND_DISABLE_Msk);

    // Wait for T_IFS turnaround (150us nominal).
    const uint32_t txTriggerTargetUs = rxEndUs + kBleScanRspTxenAfterRxUs;
    bleTriggerTxAtTargetUsCritical(radio_, txTriggerTargetUs);
    txScanRspOk = waitDisabled(spinLimit);
    radio_->SHORTS = 0U;
  }

  if (interaction != nullptr) {
    interaction->scanResponseTransmitted = txScanRspOk;
    if (hasConnectInd && !connectAccepted) {
      interaction->receivedConnectInd = false;
      interaction->connectIndChSel2 = false;
    }
  }

  clearRadioCoreEvents(radio_);
  return true;
}

bool BleRadio::advertiseInteractOnce(BleAdvertisingChannel channel,
                                     BleAdvInteraction* interaction,
                                     uint32_t requestListenSpinLimit,
                                     uint32_t spinLimit) {
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  const bool ok = advertiseInteractOncePrepared(channel, interaction,
                                                requestListenSpinLimit, spinLimit);
  endUnconnectedRadioActivity();
  return ok;
}

bool BleRadio::advertiseInteractEvent(BleAdvInteraction* interaction,
                                      uint32_t interChannelDelayUs,
                                      uint32_t requestListenSpinLimit,
                                      uint32_t spinLimit) {
  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  // Add spec-compliant random delay (0-10ms) to prevent persistent collisions.
  {
    static uint32_t seed = 0U;
    if (seed == 0U) {
      seed = micros() ^ (static_cast<uint32_t>(address_[0]) << 24U);
    }
    seed = seed * 1103515245U + 12345U;
    const uint32_t advDelayUs = (seed % 10001U);
    if (advDelayUs > 0U) {
      delayMicroseconds(advDelayUs);
    }
  }

  BleAdvInteraction tmp{};
  bool allOk = true;
  const BleAdvertisingChannel channels[] = {
      BleAdvertisingChannel::k37,
      BleAdvertisingChannel::k38,
      BleAdvertisingChannel::k39,
  };
  const uint8_t startIndex = static_cast<uint8_t>(advCycleStartIndex_ % 3U);
  if (interaction != nullptr) {
    interaction->channel = channels[startIndex];
    interaction->receivedScanRequest = false;
    interaction->scanResponseTransmitted = false;
    interaction->receivedConnectInd = false;
    interaction->connectIndChSel2 = false;
    interaction->peerAddressRandom = false;
    interaction->rssiDbm = 0;
    memset(interaction->peerAddress, 0, sizeof(interaction->peerAddress));
  }

  for (size_t i = 0; i < 3U; ++i) {
    const uint8_t idx = static_cast<uint8_t>((startIndex + i) % 3U);
    tmp = {};
    const bool ok = advertiseInteractOncePrepared(channels[idx], &tmp,
                                                  requestListenSpinLimit, spinLimit);
    allOk = allOk && ok;

    if (interaction != nullptr) {
      if (tmp.receivedConnectInd || tmp.receivedScanRequest) {
        *interaction = tmp;
      }
    }

    if (tmp.receivedConnectInd) {
      // Stop advertising this event immediately if a connection indication
      // was observed.
      break;
    }

    if ((i < 2U) && (interChannelDelayUs > 0U)) {
      delayMicroseconds(interChannelDelayUs);
    }
  }

  advCycleStartIndex_ = static_cast<uint8_t>((startIndex + 1U) % 3U);
  endUnconnectedRadioActivity();
  return allOk;
}
