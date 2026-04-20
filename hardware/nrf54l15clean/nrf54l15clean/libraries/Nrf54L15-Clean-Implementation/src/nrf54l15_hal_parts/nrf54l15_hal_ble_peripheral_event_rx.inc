
bool BleRadio::pollConnectionEventInternal(BleConnectionEvent* event,
                                           uint32_t spinLimit) {
  resetConnectionEventStruct(event);
  if (!initialized_ || radio_ == nullptr || !connected_) {
    return false;
  }
  if (connectionRole_ == BleConnectionRole::kCentral) {
    return pollCentralConnectionEvent(event, spinLimit);
  }

  const uint32_t intervalUsForListen =
      (connectionIntervalUnits_ > 0U)
          ? (static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL)
          : 1250UL;

  uint32_t nowUs = bleTimingUs();
  // If application code stalled long enough that the current event window is
  // already behind us, align channel selection to the next event to avoid
  // staying one event behind indefinitely.
  uint32_t lateAllowanceUs = 0U;
  if (connectionSyncAttemptsRemaining_ > 0U) {
    // During initial sync after CONNECT_IND, the central may transmit anywhere
    // within the transmit window; allow a wider late window before declaring
    // the event missed.
    lateAllowanceUs = connectionFirstEventListenUs_ + 400U;
  } else {
    // In steady-state, use the post-anchor RX listen window to decide whether
    // we've missed the current event. A small late threshold (e.g. a few
    // hundred microseconds) can cause channel selection to advance even while
    // we're still within the valid RX window, leading to cascaded timeouts.
    const uint32_t listenBudgetUs =
        bleConnectionRxListenUs(connectionIntervalUnits_, connectionSca_);
    lateAllowanceUs =
        (listenBudgetUs > kBleConnRxStartLeadUs)
            ? static_cast<uint32_t>(listenBudgetUs - kBleConnRxStartLeadUs)
            : 0U;
  }
  const bool useCurrentEventCounterForChannel =
      timeReachedUs(nowUs, connectionNextEventUs_ + lateAllowanceUs);
  if (useCurrentEventCounterForChannel) {
    ++encDebug_.connLatePollCount;
    if (connectionSyncAttemptsRemaining_ > 0U) {
      --connectionSyncAttemptsRemaining_;
      if (connectionSyncAttemptsRemaining_ == 0U) {
        connectionFirstEventListenUs_ = 0U;
      }
    }
    if (connectionMissedEventCount_ < 0xFFFFU) {
      ++connectionMissedEventCount_;
    }
    noteConnectionMissedEvent(&encDebug_, connectionMissedEventCount_);
    armConnectionResyncWindow(kBleConnResyncExtraListenUs,
                              kBleConnGenericMissResyncEvents);
    {
      const uint32_t intervalUs =
          (connectionIntervalUnits_ > 0U)
              ? (static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL)
              : 1250UL;
      const uint32_t timeoutUs =
          static_cast<uint32_t>(connectionTimeoutUnits_) * 10000UL;
      uint32_t supervisionEvents = timeoutUs / intervalUs;
      if (supervisionEvents < 1U) {
        supervisionEvents = 1U;
      }
      if (connectionMissedEventCount_ >= supervisionEvents) {
        if (event != nullptr) {
          event->terminateInd = true;
          event->disconnectReasonValid = true;
          event->disconnectReasonRemote = false;
          event->disconnectReason = kBleLlErrorConnectionTimeout;
        }
        rememberDisconnectReason(kBleLlErrorConnectionTimeout, false);
        emitBleTrace("SUPERVISION_TIMEOUT");
        disconnectWithReason(BleDisconnectReason::kSupervisionTimeout, 0U,
                             spinLimit / 2U + 1U);
        return true;
      }
    }
    updateNextConnectionEventTime();
    emitBleTrace("EVT_LATE_SKIP");
    return false;
  }

  const uint32_t rxStartUs = connectionNextEventUs_ - kBleConnRxStartLeadUs;
  if (!timeReachedUs(nowUs, rxStartUs)) {
    const uint32_t prewaitStartUs =
        (rxStartUs > kBleConnAnchorPrewaitUs)
            ? (rxStartUs - kBleConnAnchorPrewaitUs)
            : rxStartUs;
    if (!timeReachedUs(nowUs, prewaitStartUs)) {
      uint32_t waitGuard = spinLimit;
      while (waitGuard-- > 0U && !timeReachedUs(nowUs, prewaitStartUs)) {
        nowUs = bleTimingUs();
      }
      if (!timeReachedUs(nowUs, prewaitStartUs)) {
        return false;
      }
    }
    // Short, bounded pre-wait near anchor reduces event jitter while
    // allowing RX ramp-up before the nominal event anchor.
    while (!timeReachedUs(nowUs, rxStartUs)) {
      nowUs = bleTimingUs();
    }
  }

  const uint32_t currentEventAnchorUs = connectionNextEventUs_;
  updateNextConnectionEventTime();

  // `updateNextConnectionEventTime()` advances `connectionEventCounter_`
  // before servicing this event. Use the same "current event" counter basis
  // as channel selection so LL instants are applied on the intended event.
  const uint16_t currentEventCounter =
      useCurrentEventCounterForChannel
          ? connectionEventCounter_
          : ((connectionEventCounter_ > 0U)
                 ? static_cast<uint16_t>(connectionEventCounter_ - 1U)
                 : 0U);

  applyPendingConnectionUpdateAtInstant(currentEventCounter, currentEventAnchorUs);
  bool channelMapAppliedThisEvent = false;
  if (connectionChannelMapPending_ &&
      llEventInstantReached(currentEventCounter, connectionChannelMapInstant_)) {
    if (connectionPendingChannelCount_ >= 2U) {
      memcpy(connectionChannelMap_, connectionPendingChannelMap_,
             sizeof(connectionChannelMap_));
      connectionChannelCount_ = connectionPendingChannelCount_;
      ++encDebug_.connChannelMapAppliedCount;
      encDebug_.connLastChannelMapAppliedEventCounter = currentEventCounter;
      channelMapAppliedThisEvent = true;
      // Channel-map instants are a common re-sync boundary in this custom LL.
      // Keep RX widened for a few post-apply events so an isolated miss does
      // not immediately cascade into repeated supervision-timeout misses.
      armConnectionResyncWindow(kBleConnResyncExtraListenUs,
                                kBleConnChannelMapApplyResyncEvents);
      emitBleTrace("LL_CHANNEL_MAP_APPLIED");
    }
    connectionChannelMapPending_ = false;
  }

  const uint8_t dataChannel =
      selectNextDataChannel(useCurrentEventCounterForChannel);
  if (channelMapAppliedThisEvent) {
    encDebug_.connLastChannelMapAppliedDataChannel = dataChannel;
  }
  const bool usePreparedHardwareRx =
      backgroundConnectionServiceInProgress_ && backgroundConnectionRxPrepared_ &&
      !useCurrentEventCounterForChannel &&
      (backgroundConnectionPreparedDataChannel_ == dataChannel);
  backgroundConnectionRxPrepared_ = false;
  backgroundConnectionPreparedDataChannel_ = 0U;
  bleDisableCompare(kBleBackgroundConnRxCompareChannel, false);

  if (!usePreparedHardwareRx && !setDataChannel(dataChannel)) {
    return false;
  }

  if (event != nullptr) {
    event->eventStarted = true;
    event->eventCounter = currentEventCounter;
    event->dataChannel = dataChannel;
  }
  uint8_t eventLead10Us = 0U;
  const uint32_t eventArmUs = bleTimingUs();
  if (!timeReachedUs(eventArmUs, currentEventAnchorUs)) {
    const uint32_t leadUs = currentEventAnchorUs - eventArmUs;
    eventLead10Us =
        static_cast<uint8_t>((leadUs >= 2550U) ? 255U : (leadUs / 10U));
  }
  blePeriphConnTracePush(currentEventCounter, dataChannel, 2U, eventLead10Us);

  if (!usePreparedHardwareRx) {
    clearRadioCoreEvents(radio_);
    memset(rxPacket_, 0, sizeof(rxPacket_));
    radio_->PACKETPTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
    radio_->SHORTS = bleRadioShortsRxListen();
    radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
  } else {
    uint32_t kickFallbackUs = rxStartUs + kBleBackgroundConnRxFallbackLeadUs;
    uint32_t kickNowUs = bleTimingUs();
    while (!timeReachedUs(kickNowUs, kickFallbackUs)) {
      const uint32_t state =
          (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
      if ((state != RADIO_STATE_STATE_Disabled) ||
          (radio_->EVENTS_RXREADY != 0U) ||
          (radio_->EVENTS_ADDRESS != 0U) ||
          (radio_->EVENTS_END != 0U)) {
        break;
      }
      kickNowUs = bleTimingUs();
    }
    const uint32_t state =
        (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if ((state == RADIO_STATE_STATE_Disabled) &&
        (radio_->EVENTS_RXREADY == 0U) &&
        (radio_->EVENTS_ADDRESS == 0U) &&
        (radio_->EVENTS_END == 0U)) {
      radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
      ++encDebug_.connBgRxHardwareFallbackCount;
    }
  }

  const uint16_t unsyncedEventCount =
      (connectionMissedEventCount_ < 0xFFFFU)
          ? static_cast<uint16_t>(connectionMissedEventCount_ + 1U)
          : 0xFFFFU;
  uint32_t rxListenUs = bleConnectionRxListenUs(connectionIntervalUnits_,
                                                connectionSca_,
                                                unsyncedEventCount);
  if (rxListenUs == 0U) {
    rxListenUs = intervalUsForListen;
  }
  if (connectionSyncAttemptsRemaining_ > 0U) {
    // During initial sync after CONNECT_IND, keep RX active from the early
    // pre-anchor lead through the full transmit window (plus margin).
    const uint32_t syncBudgetUs =
        kBleConnRxStartLeadUs + connectionFirstEventListenUs_ + 800U;
    if (syncBudgetUs > rxListenUs) {
      rxListenUs = syncBudgetUs;
    }
    if (rxListenUs > 50000U) {
      rxListenUs = 50000U;
    }
  }
  encDebug_.connLastRxListenBudgetUs = rxListenUs;
  uint32_t rxEndTimestampUs = 0U;
  const bool endSeen =
      waitRadioEndBudgeted(radio_, rxListenUs, spinLimit, &rxEndTimestampUs);
  if (!endSeen) {
    uint8_t timeoutFlags = 0U;
    if (radio_->EVENTS_RXREADY != 0U) {
      timeoutFlags |= 0x01U;
    }
    if (radio_->EVENTS_ADDRESS != 0U) {
      timeoutFlags |= 0x02U;
    }
    if (radio_->EVENTS_CRCOK != 0U) {
      timeoutFlags |= 0x04U;
    }
    if (radio_->EVENTS_CRCERROR != 0U) {
      timeoutFlags |= 0x08U;
    }
    blePeriphConnTracePush(currentEventCounter, dataChannel, 3U, timeoutFlags);
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs, spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    if (connectionSyncAttemptsRemaining_ > 0U) {
      --connectionSyncAttemptsRemaining_;
      if (connectionSyncAttemptsRemaining_ == 0U) {
        connectionFirstEventListenUs_ = 0U;
      }
    }
    ++connectionMissedEventCount_;
    ++encDebug_.connRxTimeoutCount;
    const uint16_t sinceChannelMapApply =
        static_cast<uint16_t>(currentEventCounter -
                              static_cast<uint16_t>(encDebug_.connLastChannelMapAppliedEventCounter));
    if ((encDebug_.connLastChannelMapAppliedEventCounter != 0U) &&
        (sinceChannelMapApply < 4U)) {
      ++encDebug_.connChannelMapTimeoutAfterApplyCount;
      encDebug_.connLastChannelMapTimeoutEventCounter = currentEventCounter;
    }
    armConnectionResyncWindow(kBleConnResyncExtraListenUs,
                              kBleConnGenericMissResyncEvents);
    if (connectionChannelMapPending_ ||
        ((encDebug_.connLastChannelMapAppliedEventCounter != 0U) &&
         (sinceChannelMapApply < 4U))) {
      armConnectionResyncWindow(kBleConnResyncExtraListenUs,
                                kBleConnChannelMapApplyResyncEvents);
    }
    noteConnectionMissedEvent(&encDebug_, connectionMissedEventCount_);
    {
      const uint32_t intervalUs =
          (connectionIntervalUnits_ > 0U)
              ? (static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL)
              : 1250UL;
      const uint32_t timeoutUs =
          static_cast<uint32_t>(connectionTimeoutUnits_) * 10000UL;
      uint32_t supervisionEvents = timeoutUs / intervalUs;
      if (supervisionEvents < 1U) {
        supervisionEvents = 1U;
      }
      if (connectionMissedEventCount_ >= supervisionEvents) {
        blePeriphConnTracePush(currentEventCounter, dataChannel, 6U,
                               static_cast<uint8_t>(connectionMissedEventCount_ & 0xFFU));
        if (event != nullptr) {
          event->terminateInd = true;
          event->disconnectReasonValid = true;
          event->disconnectReasonRemote = false;
          event->disconnectReason = kBleLlErrorConnectionTimeout;
        }
        rememberDisconnectReason(kBleLlErrorConnectionTimeout, false);
        emitBleTrace("SUPERVISION_TIMEOUT");
        disconnectWithReason(BleDisconnectReason::kSupervisionTimeout, 0U,
                             spinLimit / 2U + 1U);
      }
    }
    emitBleTrace("EVT_RX_TIMEOUT");
    return true;
  }
  if (rxEndTimestampUs == 0U) {
    rxEndTimestampUs = bleTimingUs();
  }

  const uint8_t hdr0Early = rxPacket_[0];
  const uint8_t llidEarly = hdr0Early & 0x03U;
  const uint8_t nesnEarly = (hdr0Early >> 2U) & 0x01U;
  const uint8_t snEarly = (hdr0Early >> 3U) & 0x01U;
  const uint8_t rxLengthEarly = rxPacket_[1];
  auto tryFastAttWriteTurnaround = [&]() -> bool {
    if ((radio_->EVENTS_END == 0U) || (radio_->EVENTS_PHYEND != 0U)) {
      return false;
    }
    if (connectionEncSessionValid_ ||
        connectionEncRxEnabled_ ||
        connectionEncTxEnabled_ ||
        connectionEncAwaitingStartRsp_ ||
        connectionEncStartReqPending_ ||
        connectionEncStartReqTxPending_ ||
        connectionEncKeyDerivationPending_) {
      return false;
    }
    if ((llidEarly != kBlePduDataStartOrComplete) ||
        (rxLengthEarly < (kBleL2capHeaderLen + 5U))) {
      return false;
    }

    const bool peerAckedLastTxEarly =
        connectionTxHistoryValid_ && (nesnEarly != connectionLastTxSn_);
    const bool packetIsNewEarly = (snEarly == connectionExpectedRxSn_);
    const bool canSendNewPayloadEarly =
        !connectionTxHistoryValid_ || peerAckedLastTxEarly;
    if (!packetIsNewEarly || !canSendNewPayloadEarly) {
      return false;
    }

    const uint16_t l2capLenEarly = readLe16(&rxPacket_[2]);
    const uint16_t cidEarly = readLe16(&rxPacket_[4]);
    if ((cidEarly != kBleL2capCidAtt) ||
        (l2capLenEarly < 3U) ||
        (static_cast<uint16_t>(rxLengthEarly) !=
         static_cast<uint16_t>(l2capLenEarly + kBleL2capHeaderLen)) ||
        (rxPacket_[6] != kAttOpWriteReq)) {
      return false;
    }

    const uint16_t handle = readLe16(&rxPacket_[7]);
    const uint16_t valueLen = static_cast<uint16_t>(l2capLenEarly - 3U);
    const uint8_t* const valueEarly = (valueLen > 0U) ? &rxPacket_[9] : nullptr;
    const bool isCccdHandle =
        (handle == kHandleGattServiceChangedCccd) ||
        (handle == kHandleBatteryLevelCccd) ||
        (findCustomCharacteristicByCccdHandle(handle) != nullptr);
    const bool isCustomValueHandle =
        (findCustomCharacteristicByValueHandle(handle) != nullptr);
    if (!isCccdHandle && !isCustomValueHandle) {
      return false;
    }

    const uint16_t cccd = readLe16(&rxPacket_[9]);
    const uint32_t armStartUs = bleTimingUs();
    uint8_t divider = kBleConnMicrosPollDivider;
    while ((radio_->EVENTS_CRCOK == 0U) &&
           (radio_->EVENTS_CRCERROR == 0U) &&
           (radio_->EVENTS_PHYEND == 0U)) {
      if (--divider == 0U) {
        divider = kBleConnMicrosPollDivider;
        if (static_cast<uint32_t>(bleTimingUs() - armStartUs) >=
            kBleConnFastCccdArmBudgetUs) {
          break;
        }
      }
    }
    if ((radio_->EVENTS_CRCOK == 0U) ||
        (radio_->EVENTS_CRCERROR != 0U) ||
        (radio_->EVENTS_PHYEND != 0U)) {
      return false;
    }

    bool writeAccepted = false;
    if (isCccdHandle) {
      if (valueLen != 2U) {
        return false;
      }
      const uint16_t cccd = readLe16(&rxPacket_[9]);
      writeAccepted = applyCccdState(handle, cccd);
    } else {
      uint8_t ignoredError = kAttErrWriteNotPermitted;
      writeAccepted = writeCustomGattCharacteristic(handle, valueEarly, valueLen,
                                                    true, &ignoredError);
    }
    if (!writeAccepted) {
      return false;
    }
    if (connectionPreparedWriteActive_ &&
        (connectionPreparedWriteHandle_ == handle)) {
      clearPreparedWriteState();
    }

    writeLe16(&connectionTxPayload_[0], 1U);
    writeLe16(&connectionTxPayload_[2], kBleL2capCidAtt);
    connectionTxPayload_[4] = kAttOpWriteRsp;

    const uint8_t expectedRxSnAfter =
        static_cast<uint8_t>(connectionExpectedRxSn_ ^ 0x01U);
    const uint8_t txSnAfter =
        peerAckedLastTxEarly
            ? static_cast<uint8_t>(connectionTxSn_ ^ 0x01U)
            : connectionTxSn_;
    const uint8_t txLlidFast = kBlePduDataStartOrComplete;
    const uint8_t txLengthFast = 5U;
    txPacket_[0] = static_cast<uint8_t>((txLlidFast & 0x03U) |
                                        ((expectedRxSnAfter & 0x01U) << 2U) |
                                        ((txSnAfter & 0x01U) << 3U));
    txPacket_[1] = txLengthFast;
    memcpy(&txPacket_[2], connectionTxPayload_, txLengthFast);

    clearRadioCoreEvents(radio_);
    radio_->PACKETPTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&txPacket_[0]));
    radio_->SHORTS = bleRadioShortsRxThenTxAtTifs();

    const uint32_t txEndBudgetUs = bleConnectionTxEndBudgetUs(txLengthFast);
    const bool txEndSeen =
        waitRadioEndBudgeted(radio_, txEndBudgetUs, spinLimit / 2U + 1U);
    bool txOkFast = false;
    if (txEndSeen) {
      txOkFast = waitRadioDisabledBudgeted(
          radio_, kBleConnTxDisableWaitUs, spinLimit / 2U + 1U);
    }
    if (!txOkFast) {
      radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
      (void)waitRadioDisabledBudgeted(
          radio_, kBleConnTxDisableWaitUs, spinLimit / 2U + 1U);
      radio_->SHORTS = 0U;
      ++encDebug_.connTxTimeoutCount;
      if (event != nullptr) {
        event->packetReceived = true;
        event->crcOk = true;
        event->rssiDbm = radioRssiDbm(radio_);
        event->payload = &rxPacket_[2];
        event->packetIsNew = true;
        event->peerAckedLastTx = peerAckedLastTxEarly;
        event->freshTxAllowed = true;
        event->implicitEmptyAck = false;
        event->terminateInd = true;
        event->llid = llidEarly;
        event->rxNesn = nesnEarly;
        event->rxSn = snEarly;
        event->payloadLength = rxLengthEarly;
        event->attPacket = true;
        event->attOpcode = kAttOpWriteReq;
        event->txPacketSent = false;
        event->txLlid = txLlidFast;
        event->txNesn = expectedRxSnAfter;
        event->txSn = txSnAfter;
        event->txPayloadLength = txLengthFast;
        event->txPayload = &connectionTxPayload_[0];
      }
      disconnect(spinLimit / 2U + 1U);
      clearRadioCoreEvents(radio_);
      return true;
    }

    connectionExpectedRxSn_ = expectedRxSnAfter;
    connectionTxSn_ = txSnAfter;
    connectionMissedEventCount_ = 0U;
    encDebug_.connMissedEventCountLast = 0U;
    {
      const uint32_t intervalUsFast =
          (connectionIntervalUnits_ > 0U)
              ? (static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL)
              : 1250UL;
      const uint32_t rxPacketStartUs =
          rxEndTimestampUs - bleDataPduAirTimeUs(rxLengthEarly);
      // Peripheral anchor tracks the actual central packet start on every
      // successful event so steady-state drift does not accumulate after the
      // initial sync window closes.
      connectionNextEventUs_ = rxPacketStartUs + intervalUsFast;
    }
    if (connectionSyncAttemptsRemaining_ > 0U) {
      --connectionSyncAttemptsRemaining_;
      if (connectionSyncAttemptsRemaining_ == 0U) {
        connectionFirstEventListenUs_ = 0U;
      }
    }

    connectionLastTxLlid_ = txLlidFast;
    connectionLastTxLength_ = txLengthFast;
    connectionLastTxPlainLlid_ = txLlidFast;
    connectionLastTxPlainLength_ = txLengthFast;
    memcpy(connectionLastTxPlainPayload_, connectionTxPayload_, txLengthFast);
    connectionLastTxWasEncrypted_ = false;
    connectionLastTxEncryptedLength_ = 0U;
    memset(connectionLastTxEncryptedPayload_, 0,
           sizeof(connectionLastTxEncryptedPayload_));
    connectionTxHistoryValid_ = true;
    connectionLastTxSn_ = static_cast<uint8_t>((txPacket_[0] >> 3U) & 0x01U);
    blePeriphTxTracePushPacket(txLlidFast, txLengthFast, &connectionTxPayload_[0]);

    if (event != nullptr) {
      event->packetReceived = true;
      event->crcOk = true;
      event->rssiDbm = radioRssiDbm(radio_);
      event->payload = &rxPacket_[2];
      event->packetIsNew = true;
      event->peerAckedLastTx = peerAckedLastTxEarly;
      event->freshTxAllowed = true;
      event->implicitEmptyAck = false;
      event->terminateInd = false;
      event->llid = llidEarly;
      event->rxNesn = nesnEarly;
      event->rxSn = snEarly;
      event->payloadLength = rxLengthEarly;
      event->attPacket = true;
      event->attOpcode = kAttOpWriteReq;
      event->txPacketSent = true;
      event->txLlid = txLlidFast;
      event->txNesn = expectedRxSnAfter;
      event->txSn = txSnAfter;
      event->txPayloadLength = txLengthFast;
      event->txPayload = &connectionLastTxPlainPayload_[0];
      event->emptyAckTransmitted = false;
    }

    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return true;
  };
  if (tryFastAttWriteTurnaround()) {
    return true;
  }
  auto tryFastLlControlOrSignalingTurnaround = [&]() -> bool {
    if ((radio_->EVENTS_END == 0U) || (radio_->EVENTS_PHYEND != 0U)) {
      return false;
    }
    if (connectionEncSessionValid_ ||
        connectionEncRxEnabled_ ||
        connectionEncTxEnabled_ ||
        connectionEncAwaitingStartRsp_ ||
        connectionEncStartReqPending_ ||
        connectionEncStartReqTxPending_ ||
        connectionEncKeyDerivationPending_) {
      return false;
    }

    const bool peerAckedLastTxEarly =
        connectionTxHistoryValid_ && (nesnEarly != connectionLastTxSn_);
    const bool packetIsNewEarly = (snEarly == connectionExpectedRxSn_);
    const bool canSendNewPayloadEarly =
        !connectionTxHistoryValid_ || peerAckedLastTxEarly;
    if (!packetIsNewEarly || !canSendNewPayloadEarly) {
      return false;
    }

    bool candidateLlControl = false;
    bool candidateSignaling = false;
    bool candidateAttImmediate = false;
    uint8_t rxLlOpcodeFast = 0U;
    uint8_t rxSignalCodeFast = 0U;
    uint8_t rxAttOpcodeFast = 0U;
    if ((llidEarly == kBlePduLlControl) && (rxLengthEarly >= 1U)) {
      rxLlOpcodeFast = rxPacket_[2];
      switch (rxLlOpcodeFast) {
        case kBleLlCtrlEncReq:
        case kBleLlCtrlStartEncReq:
        case kBleLlCtrlPauseEncReq:
        case kBleLlCtrlEncRsp:
        case kBleLlCtrlStartEncRsp:
        case kBleLlCtrlPauseEncRsp:
          return false;
        default:
          candidateLlControl = true;
          break;
      }
    } else if ((llidEarly == kBlePduDataStartOrComplete) &&
               (rxLengthEarly >= (kBleL2capHeaderLen + 1U)) &&
               (readLe16(&rxPacket_[4]) == kBleL2capCidAtt)) {
      rxAttOpcodeFast = rxPacket_[2 + kBleL2capHeaderLen];
      switch (rxAttOpcodeFast) {
        case kAttOpExchangeMtuReq:
        case kAttOpFindInfoReq:
        case kAttOpFindByTypeValueReq:
        case kAttOpReadByTypeReq:
        case kAttOpReadReq:
        case kAttOpReadBlobReq:
        case kAttOpReadMultipleReq:
        case kAttOpReadByGroupTypeReq:
          candidateAttImmediate = true;
          break;
        default:
          candidateAttImmediate = false;
          break;
      }
    } else if ((llidEarly == kBlePduDataStartOrComplete) &&
               (rxLengthEarly >= (kBleL2capHeaderLen + 4U)) &&
               (readLe16(&rxPacket_[4]) == kBleL2capCidLeSignaling)) {
      rxSignalCodeFast = rxPacket_[6];
      candidateSignaling = true;
    } else {
      return false;
    }

    const uint32_t armStartUs = bleTimingUs();
    uint8_t divider = kBleConnMicrosPollDivider;
    while ((radio_->EVENTS_CRCOK == 0U) &&
           (radio_->EVENTS_CRCERROR == 0U) &&
           (radio_->EVENTS_PHYEND == 0U)) {
      if (--divider == 0U) {
        divider = kBleConnMicrosPollDivider;
        if (static_cast<uint32_t>(bleTimingUs() - armStartUs) >=
            kBleConnFastCccdArmBudgetUs) {
          break;
        }
      }
    }
    if ((radio_->EVENTS_CRCOK == 0U) ||
        (radio_->EVENTS_CRCERROR != 0U) ||
        (radio_->EVENTS_PHYEND != 0U)) {
      return false;
    }

    uint8_t txLlidFast = 0x01U;
    uint8_t txLengthFast = 0U;
    if (candidateLlControl) {
      bool terminateFast = false;
      if (!buildLlControlResponse(&rxPacket_[2], rxLengthEarly, currentEventCounter,
                                  connectionTxPayload_, &txLengthFast,
                                  &terminateFast) ||
          terminateFast || (txLengthFast == 0U)) {
        return false;
      }
      const uint8_t rspOpcode = connectionTxPayload_[0];
      if ((rspOpcode == kBleLlCtrlEncRsp) ||
          (rspOpcode == kBleLlCtrlStartEncRsp) ||
          (rspOpcode == kBleLlCtrlPauseEncRsp)) {
        return false;
      }
      txLlidFast = kBlePduLlControl;
    } else {
      if (!buildL2capResponse(&rxPacket_[2], rxLengthEarly, connectionTxPayload_,
                              &txLengthFast) ||
          (txLengthFast == 0U)) {
        return false;
      }
      txLlidFast = kBlePduDataStartOrComplete;
    }

    const uint8_t expectedRxSnAfter =
        static_cast<uint8_t>(connectionExpectedRxSn_ ^ 0x01U);
    const uint8_t txSnAfter =
        peerAckedLastTxEarly
            ? static_cast<uint8_t>(connectionTxSn_ ^ 0x01U)
            : connectionTxSn_;
    txPacket_[0] = static_cast<uint8_t>((txLlidFast & 0x03U) |
                                        ((expectedRxSnAfter & 0x01U) << 2U) |
                                        ((txSnAfter & 0x01U) << 3U));
    txPacket_[1] = txLengthFast;
    memcpy(&txPacket_[2], connectionTxPayload_, txLengthFast);

    clearRadioCoreEvents(radio_);
    radio_->PACKETPTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&txPacket_[0]));
    radio_->SHORTS = bleRadioShortsRxThenTxAtTifs();

    const uint32_t txEndBudgetUs = bleConnectionTxEndBudgetUs(txLengthFast);
    const bool txEndSeen =
        waitRadioEndBudgeted(radio_, txEndBudgetUs, spinLimit / 2U + 1U);
    bool txOkFast = false;
    if (txEndSeen) {
      txOkFast = waitRadioDisabledBudgeted(
          radio_, kBleConnTxDisableWaitUs, spinLimit / 2U + 1U);
    }
    if (!txOkFast) {
      radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
      (void)waitRadioDisabledBudgeted(
          radio_, kBleConnTxDisableWaitUs, spinLimit / 2U + 1U);
      radio_->SHORTS = 0U;
      ++encDebug_.connTxTimeoutCount;
      if (event != nullptr) {
        event->packetReceived = true;
        event->crcOk = true;
        event->rssiDbm = radioRssiDbm(radio_);
        event->payload = &rxPacket_[2];
        event->packetIsNew = true;
        event->peerAckedLastTx = peerAckedLastTxEarly;
        event->freshTxAllowed = true;
        event->implicitEmptyAck = false;
        event->terminateInd = true;
        event->llid = llidEarly;
        event->rxNesn = nesnEarly;
        event->rxSn = snEarly;
        event->payloadLength = rxLengthEarly;
        event->llControlPacket = candidateLlControl;
        event->llControlOpcode = candidateLlControl ? rxLlOpcodeFast : 0U;
        event->attPacket = candidateAttImmediate;
        event->attOpcode = candidateAttImmediate ? rxAttOpcodeFast : 0U;
        event->txPacketSent = false;
        event->txLlid = txLlidFast;
        event->txNesn = expectedRxSnAfter;
        event->txSn = txSnAfter;
        event->txPayloadLength = txLengthFast;
        event->txPayload = &connectionTxPayload_[0];
      }
      disconnect(spinLimit / 2U + 1U);
      clearRadioCoreEvents(radio_);
      return true;
    }

    connectionExpectedRxSn_ = expectedRxSnAfter;
    connectionTxSn_ = txSnAfter;
    connectionMissedEventCount_ = 0U;
    encDebug_.connMissedEventCountLast = 0U;
    {
      const uint32_t intervalUsFast =
          (connectionIntervalUnits_ > 0U)
              ? (static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL)
              : 1250UL;
      const uint32_t rxPacketStartUs =
          rxEndTimestampUs - bleDataPduAirTimeUs(rxLengthEarly);
      // Peripheral anchor tracks the actual central packet start on every
      // successful event so steady-state drift does not accumulate after the
      // initial sync window closes.
      connectionNextEventUs_ = rxPacketStartUs + intervalUsFast;
    }
    if (connectionSyncAttemptsRemaining_ > 0U) {
      --connectionSyncAttemptsRemaining_;
      if (connectionSyncAttemptsRemaining_ == 0U) {
        connectionFirstEventListenUs_ = 0U;
      }
    }

    connectionLastTxLlid_ = txLlidFast;
    connectionLastTxLength_ = txLengthFast;
    connectionLastTxPlainLlid_ = txLlidFast;
    connectionLastTxPlainLength_ = txLengthFast;
    if (txLengthFast > 0U) {
      memcpy(connectionLastTxPlainPayload_, connectionTxPayload_, txLengthFast);
    }
    connectionLastTxWasEncrypted_ = false;
    connectionLastTxEncryptedLength_ = 0U;
    memset(connectionLastTxEncryptedPayload_, 0,
           sizeof(connectionLastTxEncryptedPayload_));
    connectionTxHistoryValid_ = true;
    connectionLastTxSn_ = static_cast<uint8_t>((txPacket_[0] >> 3U) & 0x01U);
    blePeriphTxTracePushPacket(txLlidFast, txLengthFast, &connectionTxPayload_[0]);
    if (candidateAttImmediate) {
      ++encDebug_.connFastAttReadTurnaroundCount;
    } else if (candidateLlControl) {
      ++encDebug_.connFastLlControlTurnaroundCount;
    } else if (candidateSignaling) {
      ++encDebug_.connFastSignalingTurnaroundCount;
    }

    if (event != nullptr) {
      event->packetReceived = true;
      event->crcOk = true;
      event->rssiDbm = radioRssiDbm(radio_);
      event->payload = &rxPacket_[2];
      event->packetIsNew = true;
      event->peerAckedLastTx = peerAckedLastTxEarly;
      event->freshTxAllowed = true;
      event->implicitEmptyAck = false;
      event->terminateInd = false;
      event->llid = llidEarly;
      event->rxNesn = nesnEarly;
      event->rxSn = snEarly;
      event->payloadLength = rxLengthEarly;
      event->llControlPacket = candidateLlControl;
      event->llControlOpcode = candidateLlControl ? rxLlOpcodeFast : 0U;
      event->attPacket = candidateAttImmediate;
      event->attOpcode = candidateAttImmediate ? rxAttOpcodeFast : 0U;
      event->txPacketSent = true;
      event->txLlid = txLlidFast;
      event->txNesn = expectedRxSnAfter;
      event->txSn = txSnAfter;
      event->txPayloadLength = txLengthFast;
      event->txPayload = &connectionLastTxPlainPayload_[0];
      event->emptyAckTransmitted = false;
    }

    (void)rxSignalCodeFast;
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return true;
  };
  if (tryFastLlControlOrSignalingTurnaround()) {
    return true;
  }
  bool disabled =
      waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs, spinLimit / 2U + 1U);
  if (!disabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    disabled = waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs,
                                         spinLimit / 2U + 1U);
  }
  radio_->SHORTS = 0U;
  if (!disabled) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOk = (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);

  if (event != nullptr) {
    event->packetReceived = true;
    event->crcOk = crcOk;
    event->rssiDbm = radioRssiDbm(radio_);
    event->payload = &rxPacket_[2];
  }

  if (!crcOk) {
    blePeriphConnTracePush(currentEventCounter, dataChannel, 4U, rxPacket_[1]);
    if (connectionSyncAttemptsRemaining_ > 0U) {
      --connectionSyncAttemptsRemaining_;
      if (connectionSyncAttemptsRemaining_ == 0U) {
        connectionFirstEventListenUs_ = 0U;
      }
    }
    ++connectionMissedEventCount_;
    ++encDebug_.connRxCrcErrorCount;
    armConnectionResyncWindow(kBleConnResyncExtraListenUs,
                              kBleConnGenericMissResyncEvents);
    noteConnectionMissedEvent(&encDebug_, connectionMissedEventCount_);
    {
      const uint32_t intervalUs =
          (connectionIntervalUnits_ > 0U)
              ? (static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL)
              : 1250UL;
      const uint32_t timeoutUs =
          static_cast<uint32_t>(connectionTimeoutUnits_) * 10000UL;
      uint32_t supervisionEvents = timeoutUs / intervalUs;
      if (supervisionEvents < 1U) {
        supervisionEvents = 1U;
      }
      if (connectionMissedEventCount_ >= supervisionEvents) {
        if (event != nullptr) {
          event->terminateInd = true;
          event->disconnectReasonValid = true;
          event->disconnectReasonRemote = false;
          event->disconnectReason = kBleLlErrorConnectionTimeout;
        }
        rememberDisconnectReason(kBleLlErrorConnectionTimeout, false);
        emitBleTrace("SUPERVISION_TIMEOUT");
        disconnectWithReason(BleDisconnectReason::kSupervisionTimeout, 0U,
                             spinLimit / 2U + 1U);
      }
    }
    clearRadioCoreEvents(radio_);
    return true;
  }
  blePeriphConnTracePush(currentEventCounter, dataChannel, 5U, rxPacket_[1]);
  connectionMissedEventCount_ = 0U;

  const uint8_t hdr0 = rxPacket_[0];
  const uint8_t llid = hdr0 & 0x03U;
  const uint8_t nesn = (hdr0 >> 2U) & 0x01U;
  const uint8_t sn = (hdr0 >> 3U) & 0x01U;
  const uint8_t rxLengthRaw = rxPacket_[1];
  {
    const uint32_t intervalUsRx =
        (connectionIntervalUnits_ > 0U)
            ? (static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL)
            : 1250UL;
    const uint32_t rxPacketStartUs =
        rxEndTimestampUs - bleDataPduAirTimeUs(rxLengthRaw);
    // Keep the peripheral anchored to the actual central packet timing in
    // every successful event, not only during the initial sync budget.
    connectionNextEventUs_ = rxPacketStartUs + intervalUsRx;
  }
  if (connectionSyncAttemptsRemaining_ > 0U) {
    --connectionSyncAttemptsRemaining_;
    if (connectionSyncAttemptsRemaining_ == 0U) {
      connectionFirstEventListenUs_ = 0U;
    }
  }

  bool peerAckedLastTx =
      connectionTxHistoryValid_ && (nesn != connectionLastTxSn_);
  const bool snMatchesExpected = (sn == connectionExpectedRxSn_);
  // RX payload freshness is keyed only by SN. NESN/ACK applies to our TX path
  // independently and must not block consuming valid peer packets.
  const bool canConsumeNewPayload = snMatchesExpected;

  bool packetIsNew = false;
  if (canConsumeNewPayload) {
    connectionExpectedRxSn_ ^= 0x01U;
    packetIsNew = true;
  }
  g_ble_periph_last_rx_llid = llid;
  g_ble_periph_last_rx_len = rxLengthRaw;
  g_ble_periph_last_rx_packet_is_new = packetIsNew ? 1U : 0U;
  g_ble_periph_last_rx_peer_acked = peerAckedLastTx ? 1U : 0U;
  if (!packetIsNew &&
      (llid == kBlePduLlControl) &&
      (rxLengthRaw >= 1U) &&
      (rxPacket_[2] == kBleLlCtrlChannelMapInd)) {
    ++encDebug_.connChannelMapDuplicateCount;
  }
  bool implicitEmptyAck = false;
  const bool lastTxWasEarlyLlControlResponse =
      !connectionEncSessionValid_ &&
      connectionTxHistoryValid_ &&
      (connectionLastTxLlid_ == kBlePduLlControl) &&
      (connectionLastTxPlainLength_ >= 1U) &&
      ((connectionLastTxPlainPayload_[0] == kBleLlCtrlFeatureRsp) ||
       (connectionLastTxPlainPayload_[0] == kBleLlCtrlVersionInd) ||
       (connectionLastTxPlainPayload_[0] == kBleLlCtrlLengthRsp) ||
       (connectionLastTxPlainPayload_[0] == kBleLlCtrlPhyRsp) ||
       (connectionLastTxPlainPayload_[0] == kBleLlCtrlPingRsp) ||
       (connectionLastTxPlainPayload_[0] == kBleLlCtrlConnectionParamRsp));
  const bool lastTxWasAttServerResponse =
      !connectionEncSessionValid_ &&
      connectionTxHistoryValid_ &&
      (connectionLastTxLlid_ == kBlePduDataStartOrComplete) &&
      (connectionLastTxPlainLength_ >= (kBleL2capHeaderLen + 1U)) &&
      (readLe16(&connectionLastTxPlainPayload_[2]) == kBleL2capCidAtt) &&
      attOpcodeIsServerResponsePdu(connectionLastTxPlainPayload_[4]);
  const bool lastTxHadAttPayload =
      !connectionEncSessionValid_ &&
      connectionTxHistoryValid_ &&
      (connectionLastTxLlid_ == kBlePduDataStartOrComplete) &&
      (connectionLastTxPlainLength_ >= (kBleL2capHeaderLen + 1U)) &&
      (readLe16(&connectionLastTxPlainPayload_[2]) == kBleL2capCidAtt);
  const bool peerAdvancedToNewUnencryptedAttProcedure =
      !connectionEncSessionValid_ &&
      packetIsNew &&
      (llid == kBlePduDataStartOrComplete) &&
      (rxLengthRaw >= (kBleL2capHeaderLen + 1U)) &&
      (readLe16(&rxPacket_[4]) == kBleL2capCidAtt) &&
      attOpcodeIsClientProgressPdu(rxPacket_[2 + kBleL2capHeaderLen]);
  const bool pendingTxIsRetryableL2capResponse =
      connectionPendingTxValid_ &&
      (connectionPendingTxLlid_ == kBlePduDataStartOrComplete) &&
      (connectionPendingTxLength_ >= kBleL2capHeaderLen) &&
      ((readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidAtt) ||
       (readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidSmp) ||
       (readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidLeSignaling));
  if (!peerAckedLastTx &&
      connectionTxHistoryValid_ &&
      packetIsNew &&
       (lastTxWasEarlyLlControlResponse ||
       ((lastTxWasAttServerResponse || lastTxHadAttPayload) &&
        peerAdvancedToNewUnencryptedAttProcedure) ||
       pendingTxIsRetryableL2capResponse)) {
    // Interop guard: some centrals advance SN while holding NESN stable after
    // immediate early LL control responses or queued L2CAP replies. Some
    // centrals also issue the next unencrypted ATT procedure before updating
    // NESN for the prior ATT response. Treat that higher-layer progress as an
    // implicit ACK so ATT discovery and early SMP pairing replies do not
    // deadlock on a stale NESN bit.
    peerAckedLastTx = true;
    implicitEmptyAck = true;
    if ((lastTxWasAttServerResponse || lastTxHadAttPayload) &&
        peerAdvancedToNewUnencryptedAttProcedure) {
      ++encDebug_.connImplicitAttProgressAckCount;
    }
  }
  if (peerAckedLastTx) {
    connectionTxSn_ ^= 0x01U;
  }

  bool terminateInd = false;
  bool peerTerminateIndReceived = false;
  uint8_t peerTerminateErrorCode = 0U;
  bool terminateMicFailure = false;
  uint8_t rxLength = rxLengthRaw;
  auto recordMicFailPacket = [&](uint8_t rawLen) {
    const uint8_t copyLen =
        (rawLen < sizeof(encDebug_.encRxLastMicFailPacket))
            ? rawLen
            : static_cast<uint8_t>(sizeof(encDebug_.encRxLastMicFailPacket));
    encDebug_.encRxLastMicFailPacketLen = copyLen;
    memset(encDebug_.encRxLastMicFailPacket, 0,
           sizeof(encDebug_.encRxLastMicFailPacket));
    if (copyLen > 0U) {
      memcpy(encDebug_.encRxLastMicFailPacket, &rxPacket_[2], copyLen);
    }
  };

  bool rxWasDecrypted = false;
  auto derivePendingEncSessionKey = [&]() {
    if (terminateInd || !connectionEncKeyDerivationPending_) {
      return;
    }

    if (!aesEncryptLe(smpStk_, connectionEncSkd_, connectionEncSessionKey_)) {
      connectionEncKeyDerivationPending_ = false;
      encDebug_.encLastSessionKeyValid = 0U;
      encDebug_.encLastSessionAltKeyValid = 0U;
      terminateInd = true;
      emitBleTrace("ENC_SESSION_KEY_FAIL");
      return;
    }

    uint8_t skdAlt[16] = {0};
    memcpy(&skdAlt[0], &connectionEncSkd_[8], 8U);
    memcpy(&skdAlt[8], &connectionEncSkd_[0], 8U);
    connectionEncAltKeyValid_ = aesEncryptLe(smpStk_, skdAlt, connectionEncSessionKeyAlt_);
    connectionEncKeyDerivationPending_ = false;
    memcpy(encDebug_.encLastSessionKey, connectionEncSessionKey_,
           sizeof(encDebug_.encLastSessionKey));
    memcpy(encDebug_.encLastSessionAltKey, connectionEncSessionKeyAlt_,
           sizeof(encDebug_.encLastSessionAltKey));
    encDebug_.encLastSessionKeyValid = 1U;
    encDebug_.encLastSessionAltKeyValid = connectionEncAltKeyValid_ ? 1U : 0U;
    encDebug_.encLastRxDir = connectionEncRxDirection_;
    encDebug_.encLastTxDir = connectionEncTxDirection_;
  };

  const bool deferEncryptedDataDecrypt =
      connectionEncSessionValid_ &&
      connectionEncRxEnabled_ &&
      !connectionEncStartReqPending_ &&
      !connectionEncStartReqTxPending_ &&
      !connectionEncAwaitingStartRsp_ &&
      !connectionEncKeyDerivationPending_ &&
      packetIsNew &&
      (llid == kBlePduDataStartOrComplete) &&
      (rxLengthRaw >= kBleMicLen);

  const bool sessionEncryptionPossible =
      connectionEncSessionValid_ &&
      (connectionEncRxEnabled_ || connectionEncTxEnabled_ || connectionEncAwaitingStartRsp_);
  const bool attemptDecryptStartEncReq =
      connectionEncSessionValid_ &&
      connectionEncStartReqPending_ &&
      (llid == kBlePduLlControl) &&
      (rxLengthRaw == static_cast<uint8_t>(1U + kBleMicLen)) &&
      !connectionEncKeyDerivationPending_;
  const bool deferAwaitingStartRspDecrypt =
      connectionEncAwaitingStartRsp_ &&
      (llid == kBlePduLlControl) &&
      (rxLengthRaw >= kBleMicLen);
  const bool shouldAttemptRxDecrypt =
      packetIsNew || attemptDecryptStartEncReq || deferAwaitingStartRspDecrypt;
  bool fastStartEncRspMatched = false;
  if (deferAwaitingStartRspDecrypt &&
      packetIsNew &&
      connectionEncPrecomputedStartRspValid_ &&
      (rxLengthRaw == (1U + kBleMicLen)) &&
      (memcmp(&rxPacket_[2], connectionEncPrecomputedStartRsp_, (1U + kBleMicLen)) == 0)) {
    rxWasDecrypted = true;
    rxLength = 1U;
    rxPacket_[2] = kBleLlCtrlStartEncRsp;
    connectionEncRxCounter_ =
        (connectionEncRxCounter_ + 1ULL) & kBleEncPacketCounterMask;
    fastStartEncRspMatched = true;
    emitBleTrace("LL_START_ENC_RSP_FAST");
  }
  if ((sessionEncryptionPossible || attemptDecryptStartEncReq) &&
      shouldAttemptRxDecrypt &&
      !deferEncryptedDataDecrypt &&
      !deferAwaitingStartRspDecrypt &&
      !fastStartEncRspMatched) {
    const bool allowPlainStartEncRsp =
        connectionEncAwaitingStartRsp_ &&
        (llid == kBlePduLlControl) &&
        (rxLengthRaw == 1U) &&
        (rxPacket_[2] == kBleLlCtrlStartEncRsp);

    if (!allowPlainStartEncRsp) {
      if (rxLengthRaw < kBleMicLen) {
        const bool finalStartRspQueued =
            connectionPendingTxValid_ &&
            (connectionPendingTxLlid_ == kBlePduLlControl) &&
            (connectionPendingTxLength_ >= 1U) &&
            (connectionPendingTxPayload_[0] == kBleLlCtrlStartEncRsp);
        const bool allowStartProcedurePlainZeroLen =
            connectionEncRxEnabled_ &&
            (rxLengthRaw == 0U) &&
            (connectionEncAwaitingStartRsp_ ||
             connectionEncStartReqPending_ ||
             connectionEncStartReqTxPending_ ||
             finalStartRspQueued) &&
            (connectionEncRxCounter_ <= 1ULL);
        // Some centrals emit a small burst of plaintext empty data PDUs right
        // after encryption transitions to ON. Keep this window bounded so we
        // tolerate interop behavior without accepting plaintext payload data.
        const bool allowPostStartPlainZeroLen =
            connectionEncRxEnabled_ &&
            connectionEncTxEnabled_ &&
            (rxLengthRaw == 0U) &&
            ((llid == kBlePduDataStartOrComplete) || (llid == 0x01U)) &&
            (connectionEncRxCounter_ <= 3ULL);
        if (connectionEncRxEnabled_ &&
            !allowStartProcedurePlainZeroLen &&
            !allowPostStartPlainZeroLen) {
          terminateInd = true;
          terminateMicFailure = true;
          ++encDebug_.encRxShortPduCount;
          encDebug_.encRxLastMicFailHdr = hdr0;
          encDebug_.encRxLastMicFailLenRaw = rxLengthRaw;
          encDebug_.encRxLastMicFailDir = connectionEncRxDirection_;
          encDebug_.encRxLastMicFailCounterLo =
              static_cast<uint32_t>(connectionEncRxCounter_ & 0xFFFFFFFFULL);
          encDebug_.encRxLastMicFailState =
              static_cast<uint8_t>((connectionEncSessionValid_ ? 0x01U : 0U) |
                                   (connectionEncRxEnabled_ ? 0x02U : 0U) |
                                   (connectionEncTxEnabled_ ? 0x04U : 0U) |
                                   (connectionEncStartReqPending_ ? 0x08U : 0U) |
                                   (connectionEncStartReqTxPending_ ? 0x10U : 0U) |
                                   (connectionEncAwaitingStartRsp_ ? 0x20U : 0U));
          encDebug_.encRxLastMicFailData0 = 0U;
          encDebug_.encRxLastMicFailData1 = 0U;
          encDebug_.encRxLastMicFailData2 = 0U;
          encDebug_.encRxLastMicFailData3 = 0U;
          encDebug_.encRxLastMicFailData4 = 0U;
          recordMicFailPacket(rxLengthRaw);
          emitBleTrace("ENC_RX_SHORT_PDU");
        }
      } else {
        uint64_t rxCounter = connectionEncRxCounter_;
        if (!packetIsNew && (rxCounter > 0ULL)) {
          rxCounter -= 1ULL;
        }
        uint64_t decryptCounterUsed = rxCounter;

        uint8_t plaintext[kBleDataPduMaxPayload] = {0};
        uint8_t plaintextLen = 0U;
        bool decryptOk = bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                              rxCounter, connectionEncRxDirection_, hdr0,
                                              &rxPacket_[2], rxLengthRaw,
                                              plaintext, &plaintextLen);
        if (!decryptOk &&
            (connectionEncRxEnabled_ || attemptDecryptStartEncReq) &&
            (rxCounter == 0ULL) &&
            (connectionEncAwaitingStartRsp_ || connectionEncStartReqPending_)) {
          const uint8_t swappedRxDirection =
              static_cast<uint8_t>(connectionEncRxDirection_ ^ 0x01U);
          if (bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                   rxCounter, swappedRxDirection, hdr0,
                                   &rxPacket_[2], rxLengthRaw,
                                   plaintext, &plaintextLen)) {
            decryptOk = true;
            connectionEncRxDirection_ = swappedRxDirection;
            connectionEncTxDirection_ = static_cast<uint8_t>(swappedRxDirection ^ 0x01U);
            connectionEncPrecomputedStartRspValid_ = false;
            connectionEncPrecomputedStartRspTxValid_ = false;
            encDebug_.encLastRxDir = connectionEncRxDirection_;
            encDebug_.encLastTxDir = connectionEncTxDirection_;
            emitBleTrace("ENC_DIR_SWAP");
          } else if (connectionEncAltKeyValid_ &&
                     bleCcmDecryptPayload(connectionEncSessionKeyAlt_, connectionEncIv_,
                                          rxCounter, connectionEncRxDirection_, hdr0,
                                          &rxPacket_[2], rxLengthRaw,
                                          plaintext, &plaintextLen)) {
            decryptOk = true;
            memcpy(connectionEncSessionKey_, connectionEncSessionKeyAlt_,
                   sizeof(connectionEncSessionKey_));
            connectionEncAltKeyValid_ = false;
            connectionEncPrecomputedStartRspValid_ = false;
            connectionEncPrecomputedStartRspTxValid_ = false;
            memcpy(encDebug_.encLastSessionKey, connectionEncSessionKey_,
                   sizeof(encDebug_.encLastSessionKey));
            encDebug_.encLastSessionKeyValid = 1U;
            emitBleTrace("ENC_KEY_SWAP");
          } else if (connectionEncAltKeyValid_ &&
                     bleCcmDecryptPayload(connectionEncSessionKeyAlt_, connectionEncIv_,
                                          rxCounter, swappedRxDirection, hdr0,
                                          &rxPacket_[2], rxLengthRaw,
                                          plaintext, &plaintextLen)) {
            decryptOk = true;
            memcpy(connectionEncSessionKey_, connectionEncSessionKeyAlt_,
                   sizeof(connectionEncSessionKey_));
            connectionEncAltKeyValid_ = false;
            connectionEncRxDirection_ = swappedRxDirection;
            connectionEncTxDirection_ = static_cast<uint8_t>(swappedRxDirection ^ 0x01U);
            connectionEncPrecomputedStartRspValid_ = false;
            connectionEncPrecomputedStartRspTxValid_ = false;
            memcpy(encDebug_.encLastSessionKey, connectionEncSessionKey_,
                   sizeof(encDebug_.encLastSessionKey));
            encDebug_.encLastSessionKeyValid = 1U;
            encDebug_.encLastRxDir = connectionEncRxDirection_;
            encDebug_.encLastTxDir = connectionEncTxDirection_;
            emitBleTrace("ENC_KEY_DIR_SWAP");
          }
        }
        if (!decryptOk &&
            (connectionEncRxEnabled_ || attemptDecryptStartEncReq) &&
            packetIsNew &&
            (connectionEncRxCounter_ < 4ULL)) {
          for (uint8_t delta = 1U; delta <= 2U; ++delta) {
            const uint64_t trialCounter =
                (rxCounter + static_cast<uint64_t>(delta)) & kBleEncPacketCounterMask;
            if (bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                     trialCounter, connectionEncRxDirection_, hdr0,
                                     &rxPacket_[2], rxLengthRaw,
                                     plaintext, &plaintextLen)) {
              decryptOk = true;
              decryptCounterUsed = trialCounter;
              emitBleTrace("ENC_RX_COUNTER_RESYNC");
              break;
            }
          }
        }
        if (!decryptOk) {
          if (connectionEncRxEnabled_) {
            terminateInd = true;
            terminateMicFailure = true;
            ++encDebug_.encRxMicFailCount;
            encDebug_.encRxLastMicFailHdr = hdr0;
            encDebug_.encRxLastMicFailLenRaw = rxLengthRaw;
            encDebug_.encRxLastMicFailDir = connectionEncRxDirection_;
            encDebug_.encRxLastMicFailCounterLo =
                static_cast<uint32_t>(rxCounter & 0xFFFFFFFFULL);
            encDebug_.encRxLastMicFailState =
                static_cast<uint8_t>((connectionEncSessionValid_ ? 0x01U : 0U) |
                                     (connectionEncRxEnabled_ ? 0x02U : 0U) |
                                     (connectionEncTxEnabled_ ? 0x04U : 0U) |
                                     (connectionEncStartReqPending_ ? 0x08U : 0U) |
                                     (connectionEncStartReqTxPending_ ? 0x10U : 0U) |
                                     (connectionEncAwaitingStartRsp_ ? 0x20U : 0U));
            encDebug_.encRxLastMicFailData0 = rxPacket_[2];
            encDebug_.encRxLastMicFailData1 =
                (rxLengthRaw > 1U) ? rxPacket_[3] : 0U;
            encDebug_.encRxLastMicFailData2 =
                (rxLengthRaw > 2U) ? rxPacket_[4] : 0U;
            encDebug_.encRxLastMicFailData3 =
                (rxLengthRaw > 3U) ? rxPacket_[5] : 0U;
            encDebug_.encRxLastMicFailData4 =
                (rxLengthRaw > 4U) ? rxPacket_[6] : 0U;
            recordMicFailPacket(rxLengthRaw);
            emitBleTrace("ENC_RX_MIC_FAIL");
          }
        } else {
          rxWasDecrypted = true;
          rxLength = plaintextLen;
          if (plaintextLen > 0U) {
            memcpy(&rxPacket_[2], plaintext, plaintextLen);
          }
          if (packetIsNew) {
            connectionEncRxCounter_ =
                (decryptCounterUsed + 1ULL) & kBleEncPacketCounterMask;
          }
        }
      }
    }
  }
