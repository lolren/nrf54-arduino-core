bool BleRadio::pollCentralConnectionEvent(BleConnectionEvent* event,
                                          uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || !connected_ ||
      connectionRole_ != BleConnectionRole::kCentral) {
    return false;
  }

  uint32_t nowUs = bleTimingUs();
  const uint32_t txAnchorUs = connectionNextEventUs_;
  {
    const uint16_t unsyncedEventCount =
        (connectionMissedEventCount_ < 0xFFFFU)
            ? static_cast<uint16_t>(connectionMissedEventCount_ + 1U)
            : 0xFFFFU;
    uint32_t lateAllowanceUs = bleConnectionRxListenUs(connectionIntervalUnits_,
                                                       connectionSca_,
                                                       unsyncedEventCount);
    lateAllowanceUs =
        (lateAllowanceUs > kBleConnRxStartLeadUs)
            ? static_cast<uint32_t>(lateAllowanceUs - kBleConnRxStartLeadUs)
            : 0U;
    // If we are already beyond the peer's receive window for this event,
    // skip it entirely. Advancing to the next event/channel and transmitting
    // or listening immediately in the same call can lock both sides onto the
    // right channel at the wrong time until supervision timeout.
    if (timeReachedUs(nowUs, txAnchorUs + lateAllowanceUs)) {
      ++encDebug_.connLatePollCount;
      if (connectionMissedEventCount_ < 0xFFFFU) {
        ++connectionMissedEventCount_;
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
          if (event != nullptr) {
            event->terminateInd = true;
          }
          emitBleTrace("SUPERVISION_TIMEOUT");
          disconnectWithReason(BleDisconnectReason::kSupervisionTimeout, 0U,
                               spinLimit / 2U + 1U);
          return true;
        }
      }
      updateNextConnectionEventTime();
      emitBleTrace("CENTRAL_LATE_SKIP");
      return false;
    }
  }
  if (!timeReachedUs(nowUs, txAnchorUs)) {
    const uint32_t prewaitStartUs =
        (txAnchorUs > kBleConnAnchorPrewaitUs)
            ? (txAnchorUs - kBleConnAnchorPrewaitUs)
            : txAnchorUs;
    if (!timeReachedUs(nowUs, prewaitStartUs)) {
      uint32_t waitGuard = spinLimit;
      while (waitGuard-- > 0U && !timeReachedUs(nowUs, prewaitStartUs)) {
        nowUs = bleTimingUs();
      }
      if (!timeReachedUs(nowUs, prewaitStartUs)) {
        return false;
      }
    }
    while (!timeReachedUs(nowUs, txAnchorUs)) {
      nowUs = bleTimingUs();
    }
  }

  const uint32_t currentEventAnchorUs = connectionNextEventUs_;
  updateNextConnectionEventTime();

  const uint16_t currentEventCounter =
      (connectionEventCounter_ > 0U)
          ? static_cast<uint16_t>(connectionEventCounter_ - 1U)
          : 0U;
  applyPendingConnectionUpdateAtInstant(currentEventCounter, currentEventAnchorUs);
  const uint8_t dataChannel = selectNextDataChannel(false);
  if (!setDataChannel(dataChannel)) {
    return false;
  }

  if (event != nullptr) {
    event->eventStarted = true;
    event->eventCounter = currentEventCounter;
    event->dataChannel = dataChannel;
  }

  const bool canSendNewPayloadThisEvent =
      !connectionTxHistoryValid_ || connectionFreshTxAllowed_;
  const bool pendingTxIsRetryableL2capRequest =
      connectionPendingTxValid_ &&
      (connectionPendingTxLlid_ == kBlePduDataStartOrComplete) &&
      (connectionPendingTxLength_ >= kBleL2capHeaderLen) &&
      ((readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidAtt) ||
       (readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidSmp) ||
       (readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidLeSignaling));
  const bool promotePendingTxAfterEmptyAck =
      !canSendNewPayloadThisEvent &&
      pendingTxIsRetryableL2capRequest &&
      (connectionLastTxLlid_ == 0x01U) &&
      (connectionLastTxLength_ == 0U);
  uint8_t txLlid = 0x01U;
  uint8_t txLength = 0U;
  if (canSendNewPayloadThisEvent || promotePendingTxAfterEmptyAck) {
    if (connectionPendingTxValid_) {
      txLlid = connectionPendingTxLlid_;
      txLength = connectionPendingTxLength_;
      if (txLength > 0U) {
        memcpy(connectionTxPayload_, connectionPendingTxPayload_, txLength);
      }
      connectionPendingTxLlid_ = 0x01U;
      connectionPendingTxLength_ = 0U;
      connectionPendingTxValid_ = false;
    }
    connectionLastTxLlid_ = txLlid;
    connectionLastTxLength_ = txLength;
    connectionLastTxPlainLlid_ = txLlid;
    connectionLastTxPlainLength_ = txLength;
    memset(connectionLastTxPlainPayload_, 0, sizeof(connectionLastTxPlainPayload_));
    if (txLength > 0U) {
      memcpy(connectionLastTxPlainPayload_, connectionTxPayload_, txLength);
    }
  } else {
    txLlid = connectionLastTxLlid_;
    txLength = connectionLastTxLength_;
    if (txLength > 0U) {
      memcpy(connectionTxPayload_, connectionLastTxPlainPayload_, txLength);
    }
  }

  const bool txHasFollowingPayload =
      (canSendNewPayloadThisEvent || promotePendingTxAfterEmptyAck) &&
      (connectionPendingTxValid_ ||
       (connectionCustomNotificationQueueCount_ > 0U));
  txPacket_[0] = static_cast<uint8_t>((txLlid & 0x03U) |
                                      ((connectionExpectedRxSn_ & 0x01U) << 2U) |
                                      ((connectionTxSn_ & 0x01U) << 3U) |
                                      (txHasFollowingPayload ? 0x10U : 0U));
  txPacket_[1] = txLength;
  if (txLength > 0U) {
    memcpy(&txPacket_[2], connectionTxPayload_, txLength);
  }

  const uint8_t txNesnBit = static_cast<uint8_t>((txPacket_[0] >> 2U) & 0x01U);
  const uint8_t txSnBit = static_cast<uint8_t>((txPacket_[0] >> 3U) & 0x01U);

  clearRadioCoreEvents(radio_);
  const size_t txCopyLen = 2U + static_cast<size_t>(txLength);
  const size_t copyLen = (txCopyLen <= sizeof(rxPacket_)) ? txCopyLen : sizeof(rxPacket_);
  memcpy(rxPacket_, txPacket_, copyLen);
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

  uint32_t txTriggerNowUs = bleTimingUs();
  while (!timeReachedUs(txTriggerNowUs, txAnchorUs)) {
    txTriggerNowUs = bleTimingUs();
  }
  radio_->TASKS_TXEN = RADIO_TASKS_TXEN_TASKS_TXEN_Trigger;

  const uint32_t txEndBudgetUs = bleConnectionTxEndBudgetUs(txLength);
  const bool txEndSeen =
      waitRadioEndBudgeted(radio_, txEndBudgetUs, spinLimit / 2U + 1U);
  bool txOk = false;
  if (txEndSeen) {
    radio_->EVENTS_END = 0U;
    radio_->EVENTS_PHYEND = 0U;
    txOk = waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs,
                                     spinLimit / 2U + 1U);
  }
  if (!txOk) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs,
                              spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    return false;
  }
  connectionTxHistoryValid_ = true;

  // With DISABLED->RXEN armed for the central turnaround, a fast peer response
  // can complete before we finish TX cleanup. Preserve that real RX completion
  // instead of clearing it as stale TX state.
  const uint32_t postTxState =
      (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  const bool followRxAlreadyDone =
      (radio_->EVENTS_CRCOK != 0U) || (radio_->EVENTS_CRCERROR != 0U);
  {
    uint8_t observeFlags = 0U;
    if (radio_->EVENTS_RXREADY != 0U) {
      observeFlags |= 0x01U;
    }
    if (radio_->EVENTS_ADDRESS != 0U) {
      observeFlags |= 0x02U;
    }
    if (radio_->EVENTS_CRCOK != 0U) {
      observeFlags |= 0x04U;
    }
    if (radio_->EVENTS_CRCERROR != 0U) {
      observeFlags |= 0x08U;
    }
    if (followRxAlreadyDone) {
      observeFlags |= 0x10U;
    }
    g_ble_central_posttx_observe_flags = observeFlags;
    g_ble_central_posttx_observe_state = static_cast<uint8_t>(postTxState);
    ++g_ble_central_posttx_observe_count;
  }
  if (followRxAlreadyDone) {
    ++g_ble_central_follow_rx_preserved_count;
  }

  // Clear TX completion state before waiting on follow-up RX; otherwise the
  // stale TX END/PHYEND can be mistaken for a received packet.
  radio_->EVENTS_END = 0U;
  radio_->EVENTS_PHYEND = 0U;
  radio_->EVENTS_DISABLED = 0U;
  if (!followRxAlreadyDone) {
    radio_->EVENTS_CRCOK = 0U;
    radio_->EVENTS_CRCERROR = 0U;
  }
  radio_->SHORTS =
      ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
       RADIO_SHORTS_RXREADY_START_Msk) |
      ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
        << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
       RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
      ((RADIO_SHORTS_PHYEND_DISABLE_Enabled << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
       RADIO_SHORTS_PHYEND_DISABLE_Msk);

  if (!followRxAlreadyDone) {
    const uint32_t state =
        (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (state == RADIO_STATE_STATE_Disabled) {
      radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
    }
  }

  uint32_t followListenUs = kBleConnFollowupRxListenMaxUs;
  const uint32_t intervalUs =
      (connectionIntervalUnits_ > 0U)
          ? (static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL)
          : 1250UL;
  if (intervalUs > 3000U) {
    const uint32_t capUs = intervalUs - 3000U;
    if (followListenUs > capUs) {
      followListenUs = capUs;
    }
  }

  const bool rxDoneSeen =
      followRxAlreadyDone ||
      waitRadioRxDoneBudgeted(radio_, followListenUs, spinLimit / 2U + 1U);
  if (!rxDoneSeen) {
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
    g_ble_central_rxtimeout_observe_flags = timeoutFlags;
    g_ble_central_rxtimeout_observe_state = static_cast<uint8_t>(
        (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos);
    g_ble_central_rxtimeout_hdr0 = rxPacket_[0];
    g_ble_central_rxtimeout_hdr1 = rxPacket_[1];
    g_ble_central_rxtimeout_hdr2 = rxPacket_[2];
    g_ble_central_rxtimeout_hdr3 = rxPacket_[3];
    ++g_ble_central_rxtimeout_observe_count;
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs, spinLimit / 2U + 1U);
    radio_->SHORTS = 0U;
    clearRadioCoreEvents(radio_);
    connectionFreshTxAllowed_ = false;
    ++connectionMissedEventCount_;

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
      }
      emitBleTrace("SUPERVISION_TIMEOUT");
      disconnectWithReason(BleDisconnectReason::kSupervisionTimeout, 0U,
                           spinLimit / 2U + 1U);
    }

    if (event != nullptr) {
      event->txPacketSent = true;
      event->txLlid = txLlid;
      event->txNesn = txNesnBit;
      event->txSn = txSnBit;
      event->txPayloadLength = txLength;
      event->txPayload =
          (txLength > 0U) ? &connectionLastTxPlainPayload_[0] : nullptr;
      event->emptyAckTransmitted = (txLlid == 0x01U) && (txLength == 0U);
      event->freshTxAllowed = canSendNewPayloadThisEvent;
    }
    emitBleTrace("CENTRAL_EVT_RX_TIMEOUT");
    return true;
  }

  bool rxDisabled = waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs,
                                              spinLimit / 2U + 1U);
  if (!rxDisabled) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    rxDisabled = waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs,
                                           spinLimit / 2U + 1U);
  }
  const uint32_t crcStatus =
      (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
      RADIO_CRCSTATUS_CRCSTATUS_Pos;
  const bool crcOk = (crcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
  radio_->SHORTS = 0U;
  if (!rxDisabled) {
    clearRadioCoreEvents(radio_);
    return false;
  }

  if (event != nullptr) {
    event->packetReceived = true;
    event->crcOk = crcOk;
    event->rssiDbm = radioRssiDbm(radio_);
    event->payload = &rxPacket_[2];
  }

  if (!crcOk) {
    connectionFreshTxAllowed_ = false;
    ++connectionMissedEventCount_;
    clearRadioCoreEvents(radio_);
    return true;
  }
  connectionMissedEventCount_ = 0U;

  const uint8_t hdr0 = rxPacket_[0];
  const uint8_t llid = hdr0 & 0x03U;
  const uint8_t nesn = (hdr0 >> 2U) & 0x01U;
  const uint8_t sn = (hdr0 >> 3U) & 0x01U;
  const uint8_t rxLength = rxPacket_[1];

  bool peerAckedLastTx =
      connectionTxHistoryValid_ && (nesn != connectionTxSn_);
  const bool packetIsNew = (sn == connectionExpectedRxSn_);
  if (packetIsNew) {
    connectionExpectedRxSn_ ^= 0x01U;
  }
  bool implicitEmptyAck = false;
  const bool lastTxHadAttPayload =
      !connectionEncSessionValid_ &&
      connectionTxHistoryValid_ &&
      (connectionLastTxLlid_ == kBlePduDataStartOrComplete) &&
      (connectionLastTxPlainLength_ >= (kBleL2capHeaderLen + 1U)) &&
      (readLe16(&connectionLastTxPlainPayload_[2]) == kBleL2capCidAtt);
  const bool peerAdvancedToNewAttServerResponse =
      !connectionEncSessionValid_ &&
      packetIsNew &&
      (llid == kBlePduDataStartOrComplete) &&
      (rxLength >= (kBleL2capHeaderLen + 1U)) &&
      (readLe16(&rxPacket_[4]) == kBleL2capCidAtt) &&
      attOpcodeIsServerResponsePdu(rxPacket_[2 + kBleL2capHeaderLen]);
  if (!peerAckedLastTx &&
      connectionTxHistoryValid_ &&
      packetIsNew &&
      (lastTxHadAttPayload && peerAdvancedToNewAttServerResponse)) {
    peerAckedLastTx = true;
    implicitEmptyAck = true;
    if (lastTxHadAttPayload && peerAdvancedToNewAttServerResponse) {
      ++encDebug_.connImplicitAttProgressAckCount;
    }
  }
  if (peerAckedLastTx) {
    connectionTxSn_ ^= 0x01U;
    connectionFreshTxAllowed_ = true;
  } else {
    connectionFreshTxAllowed_ = false;
  }

  bool terminateInd = false;
  bool peerTerminateIndReceived = false;
  uint8_t peerTerminateErrorCode = 0U;
  if ((llid == kBlePduLlControl) &&
      (rxLength >= 2U) &&
      (rxPacket_[2] == kBleLlCtrlTerminateInd)) {
    terminateInd = true;
    peerTerminateIndReceived = true;
    peerTerminateErrorCode = rxPacket_[3];
  }

  rememberConnectionRxDebug(
      llid, rxLength,
      bleConnectionPayloadProtocolOpcode(llid, &rxPacket_[2], rxLength), nesn, sn,
      packetIsNew, peerAckedLastTx);

  if (event != nullptr) {
    event->packetIsNew = packetIsNew;
    event->peerAckedLastTx = peerAckedLastTx;
    event->freshTxAllowed = (!connectionTxHistoryValid_ || peerAckedLastTx);
    event->implicitEmptyAck = implicitEmptyAck;
    event->llid = llid;
    event->rxNesn = nesn;
    event->rxSn = sn;
    event->payloadLength = rxLength;
    if (llid == kBlePduLlControl && rxLength >= 1U) {
      event->llControlPacket = true;
      event->llControlOpcode = rxPacket_[2];
    }
    if (llid == kBlePduDataStartOrComplete && rxLength >= 5U) {
      const uint16_t l2capLen = readLe16(&rxPacket_[2]);
      const uint16_t cid = readLe16(&rxPacket_[4]);
      const uint8_t availableAttLen = (rxLength > kBleL2capHeaderLen)
                                          ? static_cast<uint8_t>(rxLength - kBleL2capHeaderLen)
                                          : 0U;
      if (cid == kBleL2capCidAtt && l2capLen > 0U && availableAttLen > 0U) {
        event->attPacket = true;
        event->attOpcode = rxPacket_[2 + kBleL2capHeaderLen];
      }
    }
  }

  const bool rxHasL2cap =
      packetIsNew &&
      (llid == kBlePduDataStartOrComplete) &&
      (rxLength >= kBleL2capHeaderLen);
  const uint16_t rxL2capLen = rxHasL2cap ? readLe16(&rxPacket_[2]) : 0U;
  const uint16_t rxL2capCid = rxHasL2cap ? readLe16(&rxPacket_[4]) : 0U;
  const uint8_t rxAttOpcode =
      (rxHasL2cap && rxLength > kBleL2capHeaderLen)
          ? rxPacket_[2 + kBleL2capHeaderLen]
          : 0U;

  if (packetIsNew &&
      (llid == kBlePduLlControl) &&
      (rxLength >= 1U)) {
    const uint8_t opcode = rxPacket_[2];
    if (opcode == kBleLlCtrlRejectInd && rxLength >= 2U &&
        rxPacket_[3] == kBleLlCtrlLengthReq) {
      connectionDataLengthUpdatePending_ = false;
      connectionDataLengthUpdateInFlight_ = false;
    } else if (opcode == kBleLlCtrlRejectExtInd && rxLength >= 3U &&
               rxPacket_[3] == kBleLlCtrlLengthReq) {
      connectionDataLengthUpdatePending_ = false;
      connectionDataLengthUpdateInFlight_ = false;
    }

    if (!terminateInd && !connectionPendingTxValid_) {
      uint8_t responseLength = 0U;
      bool responseTerminate = false;
      if (buildLlControlResponse(&rxPacket_[2], rxLength, currentEventCounter,
                                 connectionPendingTxPayload_, &responseLength,
                                 &responseTerminate) &&
          responseLength > 0U) {
        connectionPendingTxLlid_ = kBlePduLlControl;
        connectionPendingTxLength_ = responseLength;
        connectionPendingTxValid_ = true;
      }
      if (responseTerminate) {
        terminateInd = true;
      }
    }
  }

  if (rxHasL2cap && (rxL2capCid == kBleL2capCidAtt) &&
      (rxL2capLen >= 3U) &&
      (rxAttOpcode == kAttOpExchangeMtuRsp)) {
    const uint16_t serverRxMtu = readLe16(&rxPacket_[2 + kBleL2capHeaderLen + 1U]);
    const uint16_t negotiatedMtu = minU16(serverRxMtu, localPreferredAttMtu());
    connectionAttMtu_ = (negotiatedMtu >= kBleDefaultAttMtu)
                            ? negotiatedMtu
                            : kBleDefaultAttMtu;
    connectionAttMtuExchangePending_ = false;
    connectionAttMtuExchangeInFlight_ = false;
    connectionAttMtuExchangeComplete_ = true;
    emitBleTrace("ATT_MTU_RSP_RX");
  }

  if (rxHasL2cap && !terminateInd &&
      !connectionPendingTxValid_) {
    uint8_t responseLength = 0U;
    if (buildL2capResponse(&rxPacket_[2], rxLength, connectionPendingTxPayload_,
                           &responseLength) &&
        responseLength > 0U) {
      connectionPendingTxLlid_ = kBlePduDataStartOrComplete;
      connectionPendingTxLength_ = responseLength;
      connectionPendingTxValid_ = true;
    }
  }

  if (rxHasL2cap &&
      (rxL2capCid == kBleL2capCidAtt) &&
      (rxL2capLen > 0U) &&
      (rxAttOpcode == kAttOpHandleValueInd) &&
      !connectionPendingTxValid_) {
      writeLe16(&connectionPendingTxPayload_[0], 1U);
      writeLe16(&connectionPendingTxPayload_[2], kBleL2capCidAtt);
      connectionPendingTxPayload_[4] = kAttOpHandleValueCfm;
      connectionPendingTxLlid_ = kBlePduDataStartOrComplete;
      connectionPendingTxLength_ = 5U;
      connectionPendingTxValid_ = true;
  }

  maybeQueueCentralLinkSetupRequest();

  if (event != nullptr) {
    event->txPacketSent = true;
    event->txLlid = txLlid;
    event->txNesn = txNesnBit;
    event->txSn = txSnBit;
    event->txPayloadLength = txLength;
    event->txPayload =
        (txLength > 0U) ? &connectionLastTxPlainPayload_[0] : nullptr;
    event->emptyAckTransmitted = (txLlid == 0x01U) && (txLength == 0U);
    event->terminateInd = terminateInd;
  }

  if (terminateInd) {
    emitBleTrace("CENTRAL_TERMINATE");
    disconnectWithReason(BleDisconnectReason::kPeerTerminate,
                         peerTerminateErrorCode, spinLimit / 2U + 1U);
  }

  clearRadioCoreEvents(radio_);
  return true;
}

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

  if (deferEncryptedDataDecrypt) {
    // Data PDUs in encrypted mode are decrypted after TX so we can keep ACK
    // timing deterministic at T_IFS.
    rxLength = 0U;
  }

  if ((llid == kBlePduLlControl) && (rxLength >= 2U) &&
      (rxPacket_[2] == kBleLlCtrlTerminateInd)) {
    rememberDisconnectReason(rxPacket_[3], true);
    if (event != nullptr) {
      event->disconnectReasonValid = true;
      event->disconnectReasonRemote = true;
      event->disconnectReason = rxPacket_[3];
    }
    terminateInd = true;
    peerTerminateIndReceived = true;
    peerTerminateErrorCode = rxPacket_[3];
  }

  if ((llid == kBlePduLlControl) &&
      (rxLength >= 1U)) {
    const uint8_t opcode = rxPacket_[2];
    if (connectionEncStartReqPending_) {
      ++encDebug_.startPendingControlRxSeen;
      encDebug_.startPendingLastHdr = hdr0;
      encDebug_.startPendingLastLenRaw = rxLengthRaw;
      encDebug_.startPendingLastByte0 = opcode;
      encDebug_.startPendingLastDecrypted = rxWasDecrypted ? 1U : 0U;
    }

    if (packetIsNew) {
      if (opcode == kBleLlCtrlEncReq) {
        ++encDebug_.mainEncReqSeen;
      } else if (opcode == kBleLlCtrlStartEncReq) {
        ++encDebug_.mainStartEncReqSeen;
        if (rxWasDecrypted) {
          ++encDebug_.mainStartEncReqSeenDecrypted;
        }
      } else if (opcode == kBleLlCtrlStartEncRsp) {
        encDebug_.encStartRspLastRawLen = rxLengthRaw;
        encDebug_.encStartRspLastDecrypted = rxWasDecrypted ? 1U : 0U;
        encDebug_.encStartRspLastHdr = hdr0;
      }
    }
  }

  if (event != nullptr) {
    event->packetIsNew = packetIsNew;
    event->peerAckedLastTx = peerAckedLastTx;
    event->freshTxAllowed = (!connectionTxHistoryValid_ || peerAckedLastTx);
    event->implicitEmptyAck = implicitEmptyAck;
    event->terminateInd = terminateInd;
    event->llid = llid;
    event->rxNesn = nesn;
    event->rxSn = sn;
    event->payloadLength = rxLength;
    if (llid == kBlePduLlControl && rxLength >= 1U) {
      event->llControlPacket = true;
      event->llControlOpcode = rxPacket_[2];
    }
    if (llid == kBlePduDataStartOrComplete && rxLength >= 5U) {
      const uint16_t l2capLen = readLe16(&rxPacket_[2]);
      const uint16_t cid = readLe16(&rxPacket_[4]);
      const uint8_t availableAttLen = (rxLength > kBleL2capHeaderLen)
                                          ? static_cast<uint8_t>(rxLength - kBleL2capHeaderLen)
                                          : 0U;
      if (cid == kBleL2capCidAtt && l2capLen > 0U && availableAttLen > 0U) {
        event->attPacket = true;
        event->attOpcode = rxPacket_[2 + kBleL2capHeaderLen];
      }
    }
  }

  bool llControlHandledImmediate = false;
  bool immediateLlControlResponseValid = false;
  uint8_t immediateLlControlResponseLength = 0U;
  bool l2capHandledImmediate = false;
  bool immediateL2capResponseValid = false;
  uint8_t immediateL2capResponseLength = 0U;
  const bool encryptionProcedureBusy =
      connectionEncSessionValid_ &&
      (connectionEncKeyDerivationPending_ ||
       connectionEncStartReqTxPending_ ||
       connectionEncAwaitingStartRsp_ ||
       connectionEncEnableTxOnNextEvent_ ||
       connectionEncStartReqPending_);
  // If the peer has not ACKed our last TX PDU, keep retransmitting that exact
  // packet/SN until ACKed. This is required for encrypted empty PDUs as well,
  // since each fresh encrypted transmission consumes a new CCM packet counter.
  const bool canSendNewPayloadThisEvent =
      !connectionTxHistoryValid_ || peerAckedLastTx;
  // Several controllers retransmit the same LL control request until they see
  // a matching control response (not just an empty ACK). Build and transmit LL
  // control responses in this event whenever SN/NESN allows a fresh TX payload.
  if (packetIsNew && !terminateInd &&
      (llid == kBlePduLlControl) &&
      !deferAwaitingStartRspDecrypt &&
      canSendNewPayloadThisEvent) {
    llControlHandledImmediate = true;
    if (buildLlControlResponse(&rxPacket_[2], rxLength, currentEventCounter,
                               connectionTxPayload_,
                               &immediateLlControlResponseLength, &terminateInd) &&
        immediateLlControlResponseLength > 0U) {
      immediateLlControlResponseValid = true;
    }
  }
  if (packetIsNew && !terminateInd &&
      (llid == kBlePduDataStartOrComplete) &&
      (rxLength >= kBleL2capHeaderLen) &&
      canSendNewPayloadThisEvent &&
      !encryptionProcedureBusy &&
      !deferEncryptedDataDecrypt) {
    l2capHandledImmediate = true;
    if (buildL2capResponse(&rxPacket_[2], rxLength, connectionTxPayload_,
                           &immediateL2capResponseLength) &&
        immediateL2capResponseLength > 0U) {
      immediateL2capResponseValid = true;
    }
  }

  auto attRequestCanUseImmediateResponse = [&](uint8_t attOpcode) {
    switch (attOpcode) {
      case kAttOpExchangeMtuReq:
      case kAttOpFindInfoReq:
      case kAttOpFindByTypeValueReq:
      case kAttOpReadByTypeReq:
      case kAttOpReadReq:
      case kAttOpReadBlobReq:
      case kAttOpReadMultipleReq:
      case kAttOpReadByGroupTypeReq:
        return true;
      default:
        return false;
    }
  };
  const uint16_t rxL2capCid =
      (rxLength >= kBleL2capHeaderLen) ? readLe16(&rxPacket_[4]) : 0U;
  const uint8_t rxAttOpcode =
      (rxLength >= (kBleL2capHeaderLen + 1U)) ? rxPacket_[2 + kBleL2capHeaderLen] : 0U;
  g_ble_periph_last_rx_l2cap_cid = rxL2capCid;
  g_ble_periph_last_rx_att_opcode = rxAttOpcode;
  g_ble_periph_last_rx_ll_opcode =
      ((llid == kBlePduLlControl) && (rxLength >= 1U)) ? rxPacket_[2] : 0U;
  if (rxLengthRaw > 0U) {
    const uint8_t traceOpcode =
        ((llid == kBlePduLlControl) && (rxLength >= 1U))
            ? rxPacket_[2]
            : rxAttOpcode;
    blePeriphRxTracePush(llid, rxLengthRaw, packetIsNew, peerAckedLastTx,
                         rxL2capCid, traceOpcode);
  }
  if (packetIsNew &&
      (llid == kBlePduDataStartOrComplete) &&
      (rxL2capCid == kBleL2capCidAtt)) {
    if ((rxLength >= (kBleL2capHeaderLen + 3U)) &&
        (rxAttOpcode == kAttOpExchangeMtuRsp)) {
      const uint16_t peerRxMtu =
          readLe16(&rxPacket_[2 + kBleL2capHeaderLen + 1U]);
      const uint16_t negotiatedMtu =
          minU16(peerRxMtu, localPreferredAttMtu());
      connectionAttMtu_ =
          (negotiatedMtu >= kBleDefaultAttMtu) ? negotiatedMtu
                                               : kBleDefaultAttMtu;
      connectionAttMtuExchangePending_ = false;
      connectionAttMtuExchangeInFlight_ = false;
      connectionAttMtuExchangeComplete_ = true;
      emitBleTrace("ATT_MTU_RSP_RX");
    } else if ((rxLength >= (kBleL2capHeaderLen + 5U)) &&
               (rxAttOpcode == kAttOpErrorRsp) &&
               (rxPacket_[2 + kBleL2capHeaderLen + 1U] == kAttOpExchangeMtuReq)) {
      connectionAttMtuExchangePending_ = false;
      connectionAttMtuExchangeInFlight_ = false;
    }
  }
  if (packetIsNew &&
      (llid == kBlePduDataStartOrComplete) &&
      (rxL2capCid == kBleL2capCidLeSignaling) &&
      connectionConnParamUpdateInFlight_) {
    const uint8_t sigCode = rxPacket_[2 + kBleL2capHeaderLen];
    if ((rxLength >= (kBleL2capHeaderLen + 6U)) &&
        (sigCode == kL2capSigCodeConnParamUpdateRsp)) {
      connectionConnParamUpdatePending_ = false;
      connectionConnParamUpdateInFlight_ = false;
      emitBleTrace("CONN_PARAM_RSP_RX");
    } else if ((rxLength >= (kBleL2capHeaderLen + 6U)) &&
               (sigCode == kL2capSigCodeCommandRejectRsp)) {
      connectionConnParamUpdatePending_ = false;
      connectionConnParamUpdateInFlight_ = false;
      emitBleTrace("CONN_PARAM_REJECT_RX");
    }
  }
  // Same-event controller-owned L2CAP responses reduce dependence on a later
  // connection event. Keep ATT limited to read-only procedures and CCCD writes
  // so sketch callbacks stay out of the timing-critical TX path, but allow SMP
  // through here because it is fully controller-owned and Android/BlueZ often
  // pipeline ATT immediately after the first Pairing Request.
  bool rxL2capCanUseImmediateResponse =
      packetIsNew &&
      !terminateInd &&
      (llid == kBlePduDataStartOrComplete) &&
      !deferAwaitingStartRspDecrypt &&
      canSendNewPayloadThisEvent &&
      (rxLength >= (kBleL2capHeaderLen + 1U)) &&
      (rxL2capCid == kBleL2capCidSmp);
  if (!rxL2capCanUseImmediateResponse &&
      packetIsNew &&
      !terminateInd &&
      (llid == kBlePduDataStartOrComplete) &&
      !deferAwaitingStartRspDecrypt &&
      canSendNewPayloadThisEvent &&
      (rxLength >= (kBleL2capHeaderLen + 1U)) &&
      (rxL2capCid == kBleL2capCidAtt) &&
      attRequestCanUseImmediateResponse(rxAttOpcode)) {
    rxL2capCanUseImmediateResponse = true;
  }
  if (!rxL2capCanUseImmediateResponse &&
      packetIsNew &&
      !terminateInd &&
      (llid == kBlePduDataStartOrComplete) &&
      !deferAwaitingStartRspDecrypt &&
      canSendNewPayloadThisEvent &&
      (rxLength >= (kBleL2capHeaderLen + 3U)) &&
      (rxL2capCid == kBleL2capCidAtt) &&
      (rxAttOpcode == kAttOpWriteReq)) {
    const uint16_t handle = readLe16(&rxPacket_[2 + kBleL2capHeaderLen + 1U]);
    const bool isCccdHandle =
        (handle == kHandleGattServiceChangedCccd) ||
        (handle == kHandleBatteryLevelCccd) ||
        (findCustomCharacteristicByCccdHandle(handle) != nullptr);
    rxL2capCanUseImmediateResponse = isCccdHandle;
  }
  if (rxL2capCanUseImmediateResponse) {
    l2capHandledImmediate = true;
    if (buildL2capResponse(&rxPacket_[2], rxLength, connectionTxPayload_,
                           &immediateL2capResponseLength) &&
        immediateL2capResponseLength > 0U) {
      immediateL2capResponseValid = true;
    } else {
      l2capHandledImmediate = false;
    }
  }

  // If we decrypted LL_START_ENC_REQ (it arrived encrypted), some controllers
  // expect LL_START_ENC_RSP to be encrypted as well. Enable TX encryption
  // immediately for this response in that case.
  if (immediateLlControlResponseValid &&
      (llid == kBlePduLlControl) &&
      (rxLength >= 1U) &&
      (rxPacket_[2] == kBleLlCtrlStartEncReq) &&
      rxWasDecrypted) {
    connectionEncTxEnabled_ = true;
    connectionEncEnableTxOnNextEvent_ = false;
  }

  uint8_t txLlid = 0x01U;
  uint8_t txLength = 0U;
  const bool txCanUseFreshPayload =
      !terminateInd && canSendNewPayloadThisEvent;
  const bool pendingTxIsRetryableL2capResponseForRetry =
      connectionPendingTxValid_ &&
      (connectionPendingTxLlid_ == kBlePduDataStartOrComplete) &&
      (connectionPendingTxLength_ >= kBleL2capHeaderLen) &&
      ((readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidAtt) ||
       (readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidSmp) ||
       (readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidLeSignaling));
  const bool promotePendingTxAfterEmptyAck =
      !txCanUseFreshPayload &&
      !terminateInd &&
      pendingTxIsRetryableL2capResponseForRetry &&
      (connectionLastTxLlid_ == 0x01U) &&
      (connectionLastTxLength_ == 0U);
  if (txCanUseFreshPayload || promotePendingTxAfterEmptyAck) {
    // Time-critical path: transmit either a queued response from a previous
    // event or an empty ACK in this event, then build next responses after TX.
    // Some centrals retransmit the same ATT or SMP request if they saw only our
    // empty PDU. In that case, promote the queued L2CAP response instead of
    // deadlocking on repeated empty-PDU retransmits.
    if (immediateLlControlResponseValid) {
      txLlid = kBlePduLlControl;
      txLength = immediateLlControlResponseLength;
    } else if (immediateL2capResponseValid) {
      txLlid = kBlePduDataStartOrComplete;
      txLength = immediateL2capResponseLength;
    } else if (connectionPendingTxValid_) {
      txLlid = connectionPendingTxLlid_;
      txLength = connectionPendingTxLength_;
      if (txLength > 0U) {
        memcpy(connectionTxPayload_, connectionPendingTxPayload_, txLength);
      }
      connectionPendingTxValid_ = false;
      connectionPendingTxLlid_ = 0x01U;
      connectionPendingTxLength_ = 0U;
    } else {
      txLlid = 0x01U;
      txLength = 0U;
    }

    connectionLastTxLlid_ = txLlid;
    connectionLastTxLength_ = txLength;
  } else {
    // Retransmit the last payload verbatim until it is ACKed.
    txLlid = connectionLastTxLlid_;
    txLength = connectionLastTxLength_;
  }

  if (terminateInd && terminateMicFailure) {
    txLlid = kBlePduLlControl;
    txLength = 2U;
    connectionTxPayload_[0] = kBleLlCtrlTerminateInd;
    connectionTxPayload_[1] = kBleLlErrorMicFailure;
    ++encDebug_.encClearCount;
    encDebug_.encLastClearReason = 0x05U;  // MIC failure terminate.
    connectionEncRxEnabled_ = false;
    connectionEncTxEnabled_ = false;
    connectionLastTxLlid_ = txLlid;
    connectionLastTxLength_ = txLength;
    connectionLastTxWasEncrypted_ = false;
    connectionLastTxEncryptedLength_ = 0U;
  }

  // Preserve a snapshot of the plaintext payload for retransmissions and for
  // `BleConnectionEvent::txPayload` visibility (the working buffer is reused later).
  const bool txPayloadIsNewPlain =
      txCanUseFreshPayload || promotePendingTxAfterEmptyAck ||
      (terminateInd && terminateMicFailure);
  if (txPayloadIsNewPlain) {
    connectionLastTxPlainLlid_ = txLlid;
    connectionLastTxPlainLength_ = txLength;
    if (txLength > 0U) {
      memcpy(connectionLastTxPlainPayload_, connectionTxPayload_, txLength);
    }
  }

  const bool txHasFollowingPayload =
      txPayloadIsNewPlain &&
      (connectionPendingTxValid_ ||
       (connectionCustomNotificationQueueCount_ > 0U));
  txPacket_[0] = static_cast<uint8_t>((txLlid & 0x03U) |
                                      ((connectionExpectedRxSn_ & 0x01U) << 2U) |
                                      ((connectionTxSn_ & 0x01U) << 3U) |
                                      (txHasFollowingPayload ? 0x10U : 0U));
  const uint8_t txNesnBit = static_cast<uint8_t>((txPacket_[0] >> 2U) & 0x01U);
  const uint8_t txSnBit = static_cast<uint8_t>((txPacket_[0] >> 3U) & 0x01U);

  const uint8_t* const txPlainPayloadForCurrentTx =
      txPayloadIsNewPlain ? &connectionTxPayload_[0] : &connectionLastTxPlainPayload_[0];
  const bool txIsEncRspPlain =
      (txLlid == kBlePduLlControl) &&
      (txLength >= 1U) &&
      (txPlainPayloadForCurrentTx[0] == kBleLlCtrlEncRsp);
  const bool txIsStartEncReqPlain =
      (txLlid == kBlePduLlControl) &&
      (txLength >= 1U) &&
      (txPlainPayloadForCurrentTx[0] == kBleLlCtrlStartEncReq);
  const bool txIsStartEncRspPlain =
      (txLlid == kBlePduLlControl) &&
      (txLength >= 1U) &&
      (txPlainPayloadForCurrentTx[0] == kBleLlCtrlStartEncRsp);
  const bool txIsEmptyAckPlain = (txLlid == 0x01U) && (txLength == 0U);

  const bool sessionEncryptionActive = connectionEncTxEnabled_ && connectionEncSessionValid_;
  const bool encryptCurrentTx = sessionEncryptionActive &&
                                (txCanUseFreshPayload || connectionLastTxWasEncrypted_);
  uint8_t txLengthOnAir = txLength;
  uint64_t txCounterUsedForCurrentTx = 0ULL;
  bool txCounterUsedValid = false;
  if (!txCanUseFreshPayload && sessionEncryptionActive && connectionLastTxWasEncrypted_) {
    txLengthOnAir = connectionLastTxEncryptedLength_;
    txPacket_[1] = txLengthOnAir;
    if (txLengthOnAir > 0U) {
      memcpy(&txPacket_[2], connectionLastTxEncryptedPayload_, txLengthOnAir);
    }
  } else {
    const uint8_t* const txPlainPayload = txPlainPayloadForCurrentTx;
    txPacket_[1] = txLength;
    if (txLength > 0U) {
      memcpy(&txPacket_[2], txPlainPayload, txLength);
    }

    if (encryptCurrentTx) {
      uint8_t encryptedLength = 0U;
      bool encryptedOk = false;
      // `connectionEncTxCounter_` tracks the next TX packet counter to use.
      // Every fresh encrypted data-channel PDU (including zero-length/MIC-only)
      // consumes one counter value. Retransmissions reuse the cached ciphertext.
      uint64_t txCounterToUse = connectionEncTxCounter_;
      if (txCanUseFreshPayload &&
          (txLlid == 0x01U) &&
          (txLength == 0U) &&
          connectionEncPrecomputedEmptyValid_ &&
          (connectionEncPrecomputedCounter_ == txCounterToUse)) {
        encryptedLength = kBleMicLen;
        memcpy(connectionLastTxEncryptedPayload_, connectionEncPrecomputedPayload_,
               kBleMicLen);
        encryptedOk = true;
      } else if (txCanUseFreshPayload &&
                 txIsStartEncRspPlain &&
                 connectionEncPrecomputedStartRspTxValid_ &&
                 (connectionEncPrecomputedStartRspTxCounter_ == txCounterToUse)) {
        encryptedLength = static_cast<uint8_t>(1U + kBleMicLen);
        memcpy(connectionLastTxEncryptedPayload_, connectionEncPrecomputedStartRspTx_,
               encryptedLength);
        encryptedOk = true;
      } else {
        encryptedOk = bleCcmEncryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                           txCounterToUse, connectionEncTxDirection_,
                                           txPacket_[0],
                                           txPlainPayload, txLength,
                                           connectionLastTxEncryptedPayload_, &encryptedLength);
      }

      if (!encryptedOk) {
        txLengthOnAir = 0U;
        txPacket_[1] = 0U;
        connectionLastTxWasEncrypted_ = false;
        connectionLastTxEncryptedLength_ = 0U;
        connectionEncPrecomputedEmptyValid_ = false;
        terminateInd = true;
      } else {
        txLengthOnAir = encryptedLength;
        txPacket_[1] = encryptedLength;
        memcpy(&txPacket_[2], connectionLastTxEncryptedPayload_, encryptedLength);
        connectionLastTxWasEncrypted_ = true;
        connectionLastTxEncryptedLength_ = encryptedLength;
        txCounterUsedForCurrentTx = txCounterToUse;
        txCounterUsedValid = true;
        if (txCanUseFreshPayload) {
          connectionEncTxCounter_ =
              (txCounterToUse + 1ULL) & kBleEncPacketCounterMask;
          if (connectionEncPrecomputedCounter_ != connectionEncTxCounter_) {
            connectionEncPrecomputedEmptyValid_ = false;
          }
          if (txIsStartEncRspPlain) {
            connectionEncPrecomputedStartRspTxValid_ = false;
          }
        }
      }
    } else if (txCanUseFreshPayload) {
      connectionLastTxWasEncrypted_ = false;
      connectionLastTxEncryptedLength_ = 0U;
      memset(connectionLastTxEncryptedPayload_, 0, sizeof(connectionLastTxEncryptedPayload_));
    }
  }

  encDebug_.encLastTxHdr = txPacket_[0];
  encDebug_.encLastTxPlainLen = txLength;
  encDebug_.encLastTxAirLen = txLengthOnAir;
  encDebug_.encLastTxWasFresh = txCanUseFreshPayload ? 1U : 0U;
  encDebug_.encLastTxWasEncrypted =
      (sessionEncryptionActive && connectionLastTxWasEncrypted_) ? 1U : 0U;
  if (encDebug_.encLastTxWasEncrypted != 0U) {
    uint64_t txCounterObserved = 0ULL;
    if (txCounterUsedValid) {
      txCounterObserved = txCounterUsedForCurrentTx;
    } else {
      txCounterObserved = (connectionEncTxCounter_ - 1ULL) & kBleEncPacketCounterMask;
    }
    encDebug_.encLastTxCounterLo =
        static_cast<uint32_t>(txCounterObserved & 0xFFFFFFFFULL);
  }

  // During start encryption, some controllers send LL_START_ENC_REQ as a second
  // packet in a later connection event. Keep TX->RX turnaround in hardware while
  // awaiting LL_START_ENC_REQ so we can catch it without CPU latency.
  const bool allowPlainFollowupTurnaround =
      !terminateInd &&
      !connectionEncSessionValid_ &&
      !connectionEncRxEnabled_ &&
      !connectionEncTxEnabled_ &&
      !connectionEncStartReqPending_ &&
      !connectionEncStartReqTxPending_ &&
      !connectionEncAwaitingStartRsp_ &&
      !connectionEncKeyDerivationPending_ &&
      packetIsNew &&
      ((rxLength > 0U) || txHasFollowingPayload);
  const bool allowQueuedDataBurstFollowup =
      allowPlainFollowupTurnaround && txHasFollowingPayload;
  const bool doPostTxRxTurnaround =
      !terminateInd &&
      (allowPlainFollowupTurnaround ||
       (connectionEncStartReqPending_ && txIsEncRspPlain) ||
       txIsStartEncReqPlain);

  // Preserve the just-received payload before the T_IFS turnaround path
  // reuses `rxPacket_` as the TX->RX buffer for same-event follow-up traffic.
  uint8_t postTxOriginalPayload[kBleDataPduMaxPayload] = {0};
  const bool restoreOriginalPacketAfterFollowup =
      packetIsNew && !terminateInd && (rxLengthRaw > 0U);
  if (restoreOriginalPacketAfterFollowup) {
    memcpy(postTxOriginalPayload, &rxPacket_[2], rxLengthRaw);
  }

  clearRadioCoreEvents(radio_);
  if (doPostTxRxTurnaround) {
    // Use a single buffer for TX and immediate follow-up RX so DISABLED->RXEN
    // shortcut can arm the receiver exactly at T_IFS timing.
    const size_t txCopyLen = 2U + static_cast<size_t>(txLengthOnAir);
    const size_t copyLen = (txCopyLen <= sizeof(rxPacket_)) ? txCopyLen : sizeof(rxPacket_);
    memcpy(rxPacket_, txPacket_, copyLen);
    radio_->PACKETPTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
    radio_->SHORTS = bleRadioShortsTxThenRxAtTifs();
  } else {
    radio_->PACKETPTR =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&txPacket_[0]));
    radio_->SHORTS = bleRadioShortsTxOnly();
  }
  const uint32_t txTriggerTargetUs = rxEndTimestampUs + kBleConnTxenAfterRxUs;
  const uint32_t txTriggerArmUs = bleTimingUs();
  const uint32_t txLagUs =
      timeReachedUs(txTriggerArmUs, txTriggerTargetUs)
          ? static_cast<uint32_t>(txTriggerArmUs - txTriggerTargetUs)
          : 0U;
  noteTxenLag(&encDebug_, txLagUs);
  if (encryptCurrentTx) {
    ++encDebug_.encTxPacketCount;
    encDebug_.encTxenLagLastUs = txLagUs;
    if (txLagUs > encDebug_.encTxenLagMaxUs) {
      encDebug_.encTxenLagMaxUs = txLagUs;
    }
  }
  if (txIsEncRspPlain) {
    encDebug_.encRspTxenLagLastUs = txLagUs;
    if (txLagUs > encDebug_.encRspTxenLagMaxUs) {
      encDebug_.encRspTxenLagMaxUs = txLagUs;
    }
  }
  bleTriggerTxAtTargetUsCritical(radio_, txTriggerTargetUs);
  // Keep ENC_RSP timing deterministic while ensuring encrypted follow-up PDUs
  // (e.g. LL_START_ENC_REQ) can be decrypted in the same event window.
  derivePendingEncSessionKey();
  const uint32_t txEndBudgetUs = bleConnectionTxEndBudgetUs(txLengthOnAir);
  const bool txEndSeen =
      waitRadioEndBudgeted(radio_, txEndBudgetUs, spinLimit / 2U + 1U);
  bool txOk = false;
  if (txEndSeen) {
    if (doPostTxRxTurnaround) {
      // Clear TX END/PHYEND immediately after TX completion so the follow-up
      // listener observes only RX completion events.
      radio_->EVENTS_END = 0U;
      radio_->EVENTS_PHYEND = 0U;
    }
    txOk = waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs,
                                     spinLimit / 2U + 1U);
  }
  if (!txOk) {
    radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs, spinLimit / 2U + 1U);
  }
  if (txOk) {
    connectionTxHistoryValid_ = true;
    connectionLastTxSn_ = txSnBit;
    blePeriphTxTracePushPacket(txLlid, txLength,
                               txPayloadIsNewPlain ? &connectionTxPayload_[0]
                                                   : &connectionLastTxPlainPayload_[0]);
  }
  if (!doPostTxRxTurnaround || !txOk) {
    radio_->SHORTS = 0U;
  } else {
    // Clear TX completion events and remove DISABLED->RX shortcut so RX disable
    // completes cleanly once the follow-up listen window ends.
    radio_->EVENTS_DISABLED = 0U;
    radio_->EVENTS_CRCOK = 0U;
    radio_->EVENTS_CRCERROR = 0U;
    radio_->SHORTS = bleRadioShortsRxListen();

    // Defensive fallback: if shortcut path did not enter RX, arm RX manually.
    const uint32_t state =
        (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (state == RADIO_STATE_STATE_Disabled) {
      radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
    }
  }
  if (!txOk) {
    ++encDebug_.connTxTimeoutCount;
    emitBleTrace("EVT_TX_TIMEOUT");
  }

  if (txOk &&
      (connectionLastTxPlainLlid_ == kBlePduLlControl) &&
      (connectionLastTxPlainLength_ >= 1U)) {
    const uint8_t opcode = connectionLastTxPlainPayload_[0];
    if (opcode == kBleLlCtrlEncRsp) {
      ++encDebug_.mainEncRspTxOk;
      // Some centrals do not send LL_START_ENC_REQ in the same event as
      // LL_ENC_RSP. Arm a next-event fallback so the procedure still advances
      // when the immediate follow-up listener sees nothing.
      if (connectionEncStartReqPending_) {
        connectionEncStartReqTxPending_ = true;
      }
    } else if (opcode == kBleLlCtrlStartEncReq) {
      connectionEncStartReqTxPending_ = false;
      connectionEncAwaitingStartRsp_ = true;
      connectionEncRxEnabled_ = true;
      // Tx encryption is enabled only after final LL_START_ENC_RSP is sent.
      connectionEncTxEnabled_ = false;
      connectionEncEnableTxOnNextEvent_ = false;
      uint8_t preLen = 0U;
      connectionEncPrecomputedCounter_ = connectionEncTxCounter_;
      connectionEncPrecomputedEmptyValid_ =
          bleCcmEncryptPayload(connectionEncSessionKey_, connectionEncIv_,
                               connectionEncPrecomputedCounter_,
                               connectionEncTxDirection_,
                               0x01U,  // LLID=1, SN/NESN/MD are masked in MIC.
                               nullptr, 0U,
                               connectionEncPrecomputedPayload_, &preLen) &&
          (preLen == kBleMicLen);
      const uint8_t startRspPlain[1] = {kBleLlCtrlStartEncRsp};
      uint8_t startRspLen = 0U;
      connectionEncPrecomputedStartRspValid_ =
          bleCcmEncryptPayload(connectionEncSessionKey_, connectionEncIv_,
                               connectionEncRxCounter_,
                               connectionEncRxDirection_,
                               0x03U,  // LLID=3, SN/NESN/MD are masked in MIC.
                               startRspPlain, sizeof(startRspPlain),
                               connectionEncPrecomputedStartRsp_, &startRspLen) &&
          (startRspLen == (1U + kBleMicLen));
      connectionEncPrecomputedStartRspTxCounter_ = connectionEncTxCounter_;
      uint8_t startRspTxLen = 0U;
      connectionEncPrecomputedStartRspTxValid_ =
          bleCcmEncryptPayload(connectionEncSessionKey_, connectionEncIv_,
                               connectionEncPrecomputedStartRspTxCounter_,
                               connectionEncTxDirection_,
                               0x03U,  // LLID=3, SN/NESN/MD are masked in MIC.
                               startRspPlain, sizeof(startRspPlain),
                               connectionEncPrecomputedStartRspTx_, &startRspTxLen) &&
          (startRspTxLen == (1U + kBleMicLen));
      emitBleTrace("LL_START_ENC_REQ_TX");
    } else if (opcode == kBleLlCtrlStartEncRsp) {
      ++encDebug_.mainStartEncRspTxOk;
      // Peripheral just sent LL_START_ENC_RSP — encryption handshake is complete
      // from our side. Clear the fallback flag set after LL_ENC_RSP so that
      // encryptionProcedureBusy unblocks and L2CAP/ATT can proceed.
      connectionEncStartReqTxPending_ = false;
      connectionEncStartReqPending_ = false;
      connectionEncPrecomputedCounter_ = connectionEncTxCounter_;
      uint8_t preLen = 0U;
      connectionEncPrecomputedEmptyValid_ =
          bleCcmEncryptPayload(connectionEncSessionKey_, connectionEncIv_,
                               connectionEncPrecomputedCounter_,
                               connectionEncTxDirection_,
                               0x01U,  // LLID=1, SN/NESN/MD are masked in MIC.
                               nullptr, 0U,
                               connectionEncPrecomputedPayload_, &preLen) &&
          (preLen == kBleMicLen);
    }
  }

  bool lastTxOkForEncEnable = txOk;

  // While awaiting LL_START_ENC_REQ, listen briefly for a post-TX follow-up packet
  // and respond immediately (T_IFS). When doPostTxRxTurnaround is true, RX is already
  // armed in hardware at the TX->RX boundary.
  if (doPostTxRxTurnaround && txOk && !terminateInd) {
    ++encDebug_.followupArmed;
    uint32_t followListenUs = kBleConnFollowupRxListenMaxUs;
    const uint32_t followIntervalUs =
        static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL;
    if (followIntervalUs > 3000U) {
      const uint32_t capUs = followIntervalUs - 3000U;
      if (followListenUs > capUs) {
        followListenUs = capUs;
      }
    }
    encDebug_.connLastFollowupListenBudgetUs = followListenUs;
    const uint32_t followStartUs = bleTimingUs();
    uint8_t followPackets = 0U;
    while (!terminateInd) {
      const uint32_t followNowUs = bleTimingUs();
      const uint32_t elapsedUs =
          static_cast<uint32_t>(followNowUs - followStartUs);
      if ((elapsedUs >= followListenUs) || (followPackets >= 8U)) {
        break;
      }
      const uint32_t remainingUs = followListenUs - elapsedUs;

      uint32_t followRxEndTimestampUs = 0U;
      const bool followEndSeen = waitRadioRxDoneBudgeted(
          radio_, remainingUs, spinLimit / 2U + 1U, &followRxEndTimestampUs);
      if (!followEndSeen) {
        ++encDebug_.connFollowupRxTimeoutCount;
        radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
        waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs, spinLimit / 2U + 1U);
        radio_->SHORTS = 0U;
        break;
      }

      ++followPackets;
      ++encDebug_.followupEndSeen;
      auto transmitImmediateFollowupResponse = [&](uint8_t txFollowLlid,
                                                   uint8_t txFollowLength) -> bool {
        connectionLastTxLlid_ = txFollowLlid;
        connectionLastTxLength_ = txFollowLength;
        connectionLastTxWasEncrypted_ = false;
        connectionLastTxEncryptedLength_ = 0U;
        memset(connectionLastTxEncryptedPayload_, 0,
               sizeof(connectionLastTxEncryptedPayload_));
        connectionLastTxPlainLlid_ = txFollowLlid;
        connectionLastTxPlainLength_ = txFollowLength;
        if (txFollowLength > 0U) {
          memcpy(connectionLastTxPlainPayload_, connectionTxPayload_, txFollowLength);
        }

        txPacket_[0] = static_cast<uint8_t>((txFollowLlid & 0x03U) |
                                            ((connectionExpectedRxSn_ & 0x01U) << 2U) |
                                            ((connectionTxSn_ & 0x01U) << 3U));
        bool encryptFollowTx = false;
        uint64_t followTxCounter = 0ULL;
        uint8_t txFollowLengthOnAir = txFollowLength;
        const bool txFollowIsStartEncRspPlain =
            (txFollowLlid == kBlePduLlControl) &&
            (txFollowLength >= 1U) &&
            (connectionTxPayload_[0] == kBleLlCtrlStartEncRsp);
        const bool txFollowIsEmptyAck =
            (txFollowLlid == 0x01U) &&
            (txFollowLength == 0U);
        const bool encryptFollowStartEncRsp =
            connectionEncSessionValid_ &&
            txFollowIsStartEncRspPlain &&
            connectionEncEnableTxOnNextEvent_;
        const bool encryptFollowEmptyAck =
            connectionEncSessionValid_ &&
            connectionEncTxEnabled_ &&
            txFollowIsEmptyAck;
        if (encryptFollowStartEncRsp || encryptFollowEmptyAck) {
          uint8_t encryptedLength = 0U;
          followTxCounter = connectionEncTxCounter_;
          if (encryptFollowStartEncRsp) {
            connectionEncTxEnabled_ = true;
            connectionEncEnableTxOnNextEvent_ = false;
          }
          if (encryptFollowEmptyAck &&
              connectionEncPrecomputedEmptyValid_ &&
              (connectionEncPrecomputedCounter_ == followTxCounter)) {
            encryptedLength = kBleMicLen;
            memcpy(connectionLastTxEncryptedPayload_, connectionEncPrecomputedPayload_,
                   kBleMicLen);
            encryptFollowTx = true;
          } else {
            encryptFollowTx =
                bleCcmEncryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                     followTxCounter, connectionEncTxDirection_,
                                     txPacket_[0], connectionTxPayload_,
                                     txFollowLength,
                                     connectionLastTxEncryptedPayload_,
                                     &encryptedLength);
          }
          if (!encryptFollowTx) {
            if (encryptFollowStartEncRsp) {
              connectionEncTxEnabled_ = false;
            }
            terminateInd = true;
            return false;
          }
          connectionLastTxWasEncrypted_ = true;
          connectionLastTxEncryptedLength_ = encryptedLength;
          txFollowLengthOnAir = encryptedLength;
          txPacket_[1] = encryptedLength;
          memcpy(&txPacket_[2], connectionLastTxEncryptedPayload_, encryptedLength);
        } else {
          txPacket_[1] = txFollowLength;
          memcpy(&txPacket_[2], connectionTxPayload_, txFollowLength);
        }

        clearRadioCoreEvents(radio_);
        radio_->PACKETPTR =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&txPacket_[0]));
        radio_->SHORTS = bleRadioShortsTxOnly();

        const uint32_t txFollowTriggerTargetUs =
            followRxEndTimestampUs + kBleConnTxenAfterRxUs;
        const uint32_t txFollowArmUs = bleTimingUs();
        noteTxenLag(&encDebug_,
                    timeReachedUs(txFollowArmUs, txFollowTriggerTargetUs)
                        ? static_cast<uint32_t>(txFollowArmUs -
                                                txFollowTriggerTargetUs)
                        : 0U);
        bleTriggerTxAtTargetUsCritical(radio_, txFollowTriggerTargetUs);

        const uint32_t txFollowEndBudgetUs =
            bleConnectionTxEndBudgetUs(txFollowLengthOnAir);
        const bool txFollowEndSeen =
            waitRadioEndBudgeted(radio_, txFollowEndBudgetUs, spinLimit / 2U + 1U);
        bool txFollowOk = false;
        if (txFollowEndSeen) {
          txFollowOk =
              waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs,
                                        spinLimit / 2U + 1U);
        }
        if (!txFollowOk) {
          radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
          waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs,
                                    spinLimit / 2U + 1U);
        }
        radio_->SHORTS = 0U;
        if (txFollowOk) {
          connectionTxHistoryValid_ = true;
          connectionLastTxSn_ =
              static_cast<uint8_t>((txPacket_[0] >> 3U) & 0x01U);
          blePeriphTxTracePushPacket(txFollowLlid, txFollowLength,
                                     &connectionLastTxPlainPayload_[0]);
          if (encryptFollowTx) {
            connectionEncTxCounter_ =
                (followTxCounter + 1ULL) & kBleEncPacketCounterMask;
            ++encDebug_.encTxPacketCount;
            ++encDebug_.followupStartEncRspTxOk;
          }
        }
        lastTxOkForEncEnable = txFollowOk;
        return txFollowOk;
      };

      bool followDisabled =
          waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs, spinLimit / 2U + 1U);
      if (!followDisabled) {
        radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
        followDisabled = waitRadioDisabledBudgeted(radio_, kBleConnDisableWaitUs,
                                                   spinLimit / 2U + 1U);
      }
      radio_->SHORTS = 0U;
      if (!followDisabled) {
        break;
      }

      const uint32_t followCrcStatus =
          (radio_->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) >>
          RADIO_CRCSTATUS_CRCSTATUS_Pos;
      const bool followCrcOk =
          (followCrcStatus == RADIO_CRCSTATUS_CRCSTATUS_CRCOk);
      if (!followCrcOk) {
        ++encDebug_.connFollowupRxCrcErrorCount;
      }
      if (followCrcOk) {
        ++encDebug_.followupCrcOk;
        const uint8_t hdrFollow = rxPacket_[0];
        const uint8_t llidFollow = hdrFollow & 0x03U;
        const uint8_t nesnFollow = (hdrFollow >> 2U) & 0x01U;
        const uint8_t snFollow = (hdrFollow >> 3U) & 0x01U;
        const uint8_t rxLengthFollowRaw = rxPacket_[1];

        const bool peerAckedLastTxFollow =
            connectionTxHistoryValid_ && (nesnFollow != connectionLastTxSn_);
        const bool snMatchesExpectedFollow = (snFollow == connectionExpectedRxSn_);
        const bool canConsumeNewPayloadFollow = snMatchesExpectedFollow;

        bool peerAckedLastTxFollowMutable = peerAckedLastTxFollow;
        bool packetIsNewFollow = false;
        if (canConsumeNewPayloadFollow) {
          connectionExpectedRxSn_ ^= 0x01U;
          packetIsNewFollow = true;
        }
        const bool pendingTxIsRetryableL2capResponseFollow =
            connectionPendingTxValid_ &&
            (connectionPendingTxLlid_ == kBlePduDataStartOrComplete) &&
            (connectionPendingTxLength_ >= kBleL2capHeaderLen) &&
            ((readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidAtt) ||
             (readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidSmp) ||
             (readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidLeSignaling));
        if (!peerAckedLastTxFollowMutable &&
            connectionTxHistoryValid_ &&
            packetIsNewFollow &&
            pendingTxIsRetryableL2capResponseFollow) {
          peerAckedLastTxFollowMutable = true;
        }
        if (peerAckedLastTxFollowMutable) {
          connectionTxSn_ ^= 0x01U;
        }

        uint8_t rxLengthFollow = rxLengthFollowRaw;
        bool followWasDecrypted = false;
        uint64_t followCounterUsed = connectionEncRxCounter_;
        if (connectionEncSessionValid_ &&
            (connectionEncRxEnabled_ ||
             connectionEncAwaitingStartRsp_ ||
             connectionEncStartReqPending_)) {
          const bool allowPlainStartEncRspFollow =
              connectionEncAwaitingStartRsp_ &&
              (llidFollow == kBlePduLlControl) &&
              (rxLengthFollowRaw == 1U) &&
              (rxPacket_[2] == kBleLlCtrlStartEncRsp);
          const bool allowPlainZeroLenFollow =
              connectionEncRxEnabled_ &&
              (rxLengthFollowRaw == 0U) &&
              ((llidFollow == kBlePduDataStartOrComplete) || (llidFollow == 0x01U)) &&
              (connectionEncRxCounter_ <= 3ULL);

          if (!allowPlainStartEncRspFollow && !allowPlainZeroLenFollow) {
            if (rxLengthFollowRaw < kBleMicLen) {
              if (connectionEncRxEnabled_) {
                terminateInd = true;
                terminateMicFailure = true;
                ++encDebug_.encRxShortPduCount;
                encDebug_.encRxLastMicFailHdr = hdrFollow;
                encDebug_.encRxLastMicFailLenRaw = rxLengthFollowRaw;
                encDebug_.encRxLastMicFailDir = connectionEncRxDirection_;
                encDebug_.encRxLastMicFailCounterLo =
                    static_cast<uint32_t>(connectionEncRxCounter_ & 0xFFFFFFFFULL);
                recordMicFailPacket(rxLengthFollowRaw);
                emitBleTrace("ENC_RX_SHORT_PDU_FOLLOW");
              }
            } else {
              uint64_t rxCounter = connectionEncRxCounter_;
              if (!packetIsNewFollow && (rxCounter > 0ULL)) {
                rxCounter -= 1ULL;
              }
              uint64_t decryptCounterUsed = rxCounter;

              uint8_t plaintext[kBleDataPduMaxPayload] = {0};
              uint8_t plaintextLen = 0U;
              bool followDecryptOk =
                  bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                       rxCounter, connectionEncRxDirection_, hdrFollow,
                                       &rxPacket_[2], rxLengthFollowRaw,
                                       plaintext, &plaintextLen);
              if (!followDecryptOk &&
                  (rxCounter == 0ULL) &&
                  (connectionEncAwaitingStartRsp_ || connectionEncStartReqPending_)) {
                const uint8_t swappedRxDirection =
                    static_cast<uint8_t>(connectionEncRxDirection_ ^ 0x01U);
                if (bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                         rxCounter, swappedRxDirection, hdrFollow,
                                         &rxPacket_[2], rxLengthFollowRaw,
                                         plaintext, &plaintextLen)) {
                  followDecryptOk = true;
                  connectionEncRxDirection_ = swappedRxDirection;
                  connectionEncTxDirection_ =
                      static_cast<uint8_t>(swappedRxDirection ^ 0x01U);
                  connectionEncPrecomputedStartRspValid_ = false;
                  connectionEncPrecomputedStartRspTxValid_ = false;
                  encDebug_.encLastRxDir = connectionEncRxDirection_;
                  encDebug_.encLastTxDir = connectionEncTxDirection_;
                  emitBleTrace("ENC_DIR_SWAP");
                } else if (connectionEncAltKeyValid_ &&
                           bleCcmDecryptPayload(connectionEncSessionKeyAlt_,
                                                connectionEncIv_, rxCounter,
                                                connectionEncRxDirection_, hdrFollow,
                                                &rxPacket_[2], rxLengthFollowRaw,
                                                plaintext, &plaintextLen)) {
                  followDecryptOk = true;
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
                           bleCcmDecryptPayload(connectionEncSessionKeyAlt_,
                                                connectionEncIv_, rxCounter,
                                                swappedRxDirection, hdrFollow,
                                                &rxPacket_[2], rxLengthFollowRaw,
                                                plaintext, &plaintextLen)) {
                  followDecryptOk = true;
                  memcpy(connectionEncSessionKey_, connectionEncSessionKeyAlt_,
                         sizeof(connectionEncSessionKey_));
                  connectionEncAltKeyValid_ = false;
                  connectionEncRxDirection_ = swappedRxDirection;
                  connectionEncTxDirection_ =
                      static_cast<uint8_t>(swappedRxDirection ^ 0x01U);
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
              if (!followDecryptOk &&
                  packetIsNewFollow &&
                  (connectionEncRxCounter_ < 4ULL)) {
                for (uint8_t delta = 1U; delta <= 2U; ++delta) {
                  const uint64_t trialCounter =
                      (rxCounter + static_cast<uint64_t>(delta)) &
                      kBleEncPacketCounterMask;
                  if (bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                           trialCounter, connectionEncRxDirection_,
                                           hdrFollow, &rxPacket_[2], rxLengthFollowRaw,
                                           plaintext, &plaintextLen)) {
                    followDecryptOk = true;
                    decryptCounterUsed = trialCounter;
                    emitBleTrace("ENC_RX_COUNTER_RESYNC");
                    break;
                  }
                }
              }
              if (!followDecryptOk) {
                if (connectionEncRxEnabled_) {
                  terminateInd = true;
                  terminateMicFailure = true;
                  ++encDebug_.encRxMicFailCount;
                  encDebug_.encRxLastMicFailHdr = hdrFollow;
                  encDebug_.encRxLastMicFailLenRaw = rxLengthFollowRaw;
                  encDebug_.encRxLastMicFailDir = connectionEncRxDirection_;
                  encDebug_.encRxLastMicFailCounterLo =
                      static_cast<uint32_t>(rxCounter & 0xFFFFFFFFULL);
                  recordMicFailPacket(rxLengthFollowRaw);
                  emitBleTrace("ENC_RX_MIC_FAIL_FOLLOW");
                }
              } else {
                rxLengthFollow = plaintextLen;
                followWasDecrypted = true;
                followCounterUsed = decryptCounterUsed;
                if (plaintextLen > 0U) {
                  memcpy(&rxPacket_[2], plaintext, plaintextLen);
                }
                if (packetIsNewFollow) {
                  connectionEncRxCounter_ =
                      (decryptCounterUsed + 1ULL) & kBleEncPacketCounterMask;
                }
              }
            }
          }
        }

        if (llidFollow == 0x01U) {
          ++encDebug_.followupRxLlid1;
        } else if (llidFollow == 0x02U) {
          ++encDebug_.followupRxLlid2;
        } else if (llidFollow == 0x03U) {
          ++encDebug_.followupRxLlid3;
        }
        encDebug_.lastFollowHdr = hdrFollow;
        encDebug_.lastFollowLlid = llidFollow;
        encDebug_.lastFollowLen = rxLengthFollow;
        encDebug_.lastFollowByte0 =
            (rxLengthFollow >= 1U) ? rxPacket_[2] : 0U;

        if ((llidFollow == kBlePduLlControl) &&
            (rxLengthFollow >= 2U) &&
            (rxPacket_[2] == kBleLlCtrlTerminateInd)) {
          peerTerminateIndReceived = true;
          peerTerminateErrorCode = rxPacket_[3];
        }

        if (packetIsNewFollow && !terminateInd &&
            (llidFollow == kBlePduLlControl) &&
            (rxLengthFollow >= 1U) &&
            (rxPacket_[2] == kBleLlCtrlStartEncReq)) {
          ++encDebug_.followupStartEncReqSeen;
          uint8_t responseLength = 0U;
          bool followTerminate = false;
          if (buildLlControlResponse(&rxPacket_[2], rxLengthFollow,
                                     currentEventCounter,
                                     connectionTxPayload_, &responseLength,
                                     &followTerminate) &&
              responseLength > 0U) {
            transmitImmediateFollowupResponse(kBlePduLlControl,
                                              responseLength);

            if (followTerminate) {
              terminateInd = true;
            }
          }

          // We handled LL_START_ENC_REQ; don't keep listening in this event.
          break;
        }

        if (allowPlainFollowupTurnaround && !terminateInd) {
          if (packetIsNewFollow && !connectionPendingTxValid_) {
            if ((llidFollow == kBlePduLlControl) && (rxLengthFollow >= 1U)) {
              uint8_t responseLength = 0U;
              bool followTerminate = false;
              if (buildLlControlResponse(&rxPacket_[2], rxLengthFollow,
                                         currentEventCounter,
                                         connectionPendingTxPayload_,
                                         &responseLength, &followTerminate) &&
                  responseLength > 0U) {
                connectionPendingTxLlid_ = kBlePduLlControl;
                connectionPendingTxLength_ = responseLength;
                connectionPendingTxValid_ = true;
              }
              if (followTerminate) {
                terminateInd = true;
              }
            } else if ((llidFollow == kBlePduDataStartOrComplete) &&
                       (rxLengthFollow >= kBleL2capHeaderLen)) {
              uint8_t responseLength = 0U;
              if (buildL2capResponse(&rxPacket_[2], rxLengthFollow,
                                     connectionPendingTxPayload_,
                                     &responseLength) &&
                  responseLength > 0U) {
                connectionPendingTxLlid_ = kBlePduDataStartOrComplete;
                connectionPendingTxLength_ = responseLength;
                connectionPendingTxValid_ = true;
              }
            }
          }

          if (!terminateInd) {
            auto queueCustomFollowupPayload = [&]() {
              if (connectionPendingTxValid_ ||
                  connectionCustomNotificationQueueCount_ == 0U) {
                return;
              }

              BleQueuedCustomNotificationState queuedCustom{};
              uint8_t queueGuard = connectionCustomNotificationQueueCount_;
              while ((queueGuard-- > 0U) &&
                     peekCustomGattNotification(&queuedCustom)) {
                if (queuedCustom.characteristicIndex >=
                    customGattCharacteristicCount_) {
                  popCustomGattNotification();
                  continue;
                }

                BleCustomCharacteristicState& custom =
                    customGattCharacteristics_[queuedCustom.characteristicIndex];
                const bool modeEnabled = queuedCustom.indication
                                             ? (((custom.properties &
                                                  kBleGattPropIndicate) != 0U) &&
                                                ((custom.cccdValue & 0x0002U) !=
                                                 0U))
                                             : (((custom.properties &
                                                  kBleGattPropNotify) != 0U) &&
                                                ((custom.cccdValue & 0x0001U) !=
                                                 0U));
                if (!modeEnabled) {
                  popCustomGattNotification();
                  continue;
                }

                const bool indicationFlowReady =
                    !connectionServiceChangedIndicationAwaitingConfirm_ &&
                    (connectionCustomIndicationAwaitingHandle_ == 0U);
                if (queuedCustom.indication && !indicationFlowReady) {
                  return;
                }

                writeLe16(&connectionPendingTxPayload_[0],
                          static_cast<uint16_t>(3U + queuedCustom.valueLength));
                writeLe16(&connectionPendingTxPayload_[2], kBleL2capCidAtt);
                connectionPendingTxPayload_[4] = queuedCustom.indication
                                                     ? kAttOpHandleValueInd
                                                     : kAttOpHandleValueNtf;
                writeLe16(&connectionPendingTxPayload_[5], custom.valueHandle);
                if (queuedCustom.valueLength > 0U) {
                  memcpy(&connectionPendingTxPayload_[7], queuedCustom.value,
                         queuedCustom.valueLength);
                }
                connectionPendingTxLlid_ = kBlePduDataStartOrComplete;
                connectionPendingTxLength_ =
                    static_cast<uint8_t>(7U + queuedCustom.valueLength);
                connectionPendingTxValid_ = true;
                if (queuedCustom.indication) {
                  connectionCustomIndicationAwaitingHandle_ =
                      custom.valueHandle;
                }
                popCustomGattNotification();
                return;
              }
            };

            const bool txFollowCanUseFreshPayload =
                !connectionTxHistoryValid_ || peerAckedLastTxFollowMutable;
            uint8_t txFollowLlid = 0x01U;
            uint8_t txFollowLength = 0U;
            if (txFollowCanUseFreshPayload) {
              if (allowQueuedDataBurstFollowup) {
                queueCustomFollowupPayload();
              }
              if (connectionPendingTxValid_) {
                txFollowLlid = connectionPendingTxLlid_;
                txFollowLength = connectionPendingTxLength_;
                if (txFollowLength > 0U) {
                  memcpy(connectionTxPayload_, connectionPendingTxPayload_,
                         txFollowLength);
                }
                connectionPendingTxValid_ = false;
                connectionPendingTxLlid_ = 0x01U;
                connectionPendingTxLength_ = 0U;
              }
              connectionLastTxLlid_ = txFollowLlid;
              connectionLastTxLength_ = txFollowLength;
              connectionLastTxPlainLlid_ = txFollowLlid;
              connectionLastTxPlainLength_ = txFollowLength;
              memset(connectionLastTxPlainPayload_, 0,
                     sizeof(connectionLastTxPlainPayload_));
              if (txFollowLength > 0U) {
                memcpy(connectionLastTxPlainPayload_, connectionTxPayload_,
                       txFollowLength);
              }
              connectionLastTxWasEncrypted_ = false;
              connectionLastTxEncryptedLength_ = 0U;
              memset(connectionLastTxEncryptedPayload_, 0,
                     sizeof(connectionLastTxEncryptedPayload_));
            } else {
              txFollowLlid = connectionLastTxLlid_;
              txFollowLength = connectionLastTxLength_;
            }

            const bool txFollowHasMoreData =
                txFollowCanUseFreshPayload &&
                (connectionPendingTxValid_ ||
                 (connectionCustomNotificationQueueCount_ > 0U));
            txPacket_[0] = static_cast<uint8_t>((txFollowLlid & 0x03U) |
                                                ((connectionExpectedRxSn_ & 0x01U) << 2U) |
                                                ((connectionTxSn_ & 0x01U) << 3U) |
                                                (txFollowHasMoreData ? 0x10U : 0U));
            txPacket_[1] = txFollowLength;
            if (txFollowLength > 0U) {
              memcpy(&txPacket_[2], connectionLastTxPlainPayload_, txFollowLength);
            }

            clearRadioCoreEvents(radio_);
            {
              const size_t txCopyLen = 2U + static_cast<size_t>(txFollowLength);
              const size_t copyLen =
                  (txCopyLen <= sizeof(rxPacket_)) ? txCopyLen : sizeof(rxPacket_);
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

            const uint32_t txFollowTriggerTargetUs =
                followRxEndTimestampUs + kBleConnTxenAfterRxUs;
            uint32_t txFollowTriggerNowUs = bleTimingUs();
            while (!timeReachedUs(txFollowTriggerNowUs, txFollowTriggerTargetUs)) {
              txFollowTriggerNowUs = bleTimingUs();
            }
            bleTriggerTxAtTargetUsCritical(radio_, txFollowTriggerTargetUs);

            const uint32_t txFollowEndBudgetUs =
                bleConnectionTxEndBudgetUs(txFollowLength);
            const bool txFollowEndSeen =
                waitRadioEndBudgeted(radio_, txFollowEndBudgetUs, spinLimit / 2U + 1U);
            bool txFollowOk = false;
            if (txFollowEndSeen) {
              radio_->EVENTS_END = 0U;
              radio_->EVENTS_PHYEND = 0U;
              txFollowOk =
                  waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs,
                                            spinLimit / 2U + 1U);
            }
            if (!txFollowOk) {
              radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
              waitRadioDisabledBudgeted(radio_, kBleConnTxDisableWaitUs,
                                        spinLimit / 2U + 1U);
              radio_->SHORTS = 0U;
              break;
            }

            connectionTxHistoryValid_ = true;
            radio_->EVENTS_DISABLED = 0U;
            radio_->EVENTS_CRCOK = 0U;
            radio_->EVENTS_CRCERROR = 0U;
            radio_->SHORTS =
                ((RADIO_SHORTS_RXREADY_START_Enabled << RADIO_SHORTS_RXREADY_START_Pos) &
                 RADIO_SHORTS_RXREADY_START_Msk) |
                ((RADIO_SHORTS_ADDRESS_RSSISTART_Enabled
                  << RADIO_SHORTS_ADDRESS_RSSISTART_Pos) &
                 RADIO_SHORTS_ADDRESS_RSSISTART_Msk) |
                ((RADIO_SHORTS_PHYEND_DISABLE_Enabled
                  << RADIO_SHORTS_PHYEND_DISABLE_Pos) &
                 RADIO_SHORTS_PHYEND_DISABLE_Msk);
            {
              const uint32_t state =
                  (radio_->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
              if (state == RADIO_STATE_STATE_Disabled) {
                radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
              }
            }
            continue;
          }
        }
      }

      // Re-arm RX for another follow-up packet in the same event.
      clearRadioCoreEvents(radio_);
      radio_->PACKETPTR =
          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&rxPacket_[0]));
      radio_->SHORTS = bleRadioShortsRxListen();
      radio_->TASKS_RXEN = RADIO_TASKS_RXEN_TASKS_RXEN_Trigger;
    }

    clearRadioCoreEvents(radio_);
  }
  if (restoreOriginalPacketAfterFollowup) {
    rxPacket_[0] = hdr0;
    rxPacket_[1] = rxLengthRaw;
    memcpy(&rxPacket_[2], postTxOriginalPayload, rxLengthRaw);
  }

  // Fallback in case TX path did not run to completion in this event.
  derivePendingEncSessionKey();

  // Awaiting LL_START_ENC_RSP is latency-critical for the ACK path: transmit the
  // ACK first, then validate/decode the encrypted control PDU.
  if (!terminateInd &&
      deferAwaitingStartRspDecrypt &&
      !fastStartEncRspMatched &&
      packetIsNew &&
      connectionEncSessionValid_ &&
      connectionEncAwaitingStartRsp_) {
    uint8_t plaintext[kBleDataPduMaxPayload] = {0};
    uint8_t plaintextLen = 0U;
    const uint64_t rxCounter = connectionEncRxCounter_;
    bool decryptOk = bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                                          rxCounter, connectionEncRxDirection_, hdr0,
                                          &rxPacket_[2], rxLengthRaw,
                                          plaintext, &plaintextLen);
    if (!decryptOk) {
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

    if (!decryptOk) {
      terminateInd = true;
      terminateMicFailure = true;
      ++encDebug_.encRxMicFailCount;
      encDebug_.encRxLastMicFailHdr = hdr0;
      encDebug_.encRxLastMicFailLenRaw = rxLengthRaw;
      encDebug_.encRxLastMicFailDir = connectionEncRxDirection_;
      encDebug_.encRxLastMicFailCounterLo =
          static_cast<uint32_t>(rxCounter & 0xFFFFFFFFULL);
      recordMicFailPacket(rxLengthRaw);
      emitBleTrace("ENC_RX_MIC_FAIL_POSTTX");
    } else {
      rxWasDecrypted = true;
      rxLength = plaintextLen;
      if (plaintextLen > 0U) {
        memcpy(&rxPacket_[2], plaintext, plaintextLen);
      }
      if (packetIsNew) {
        connectionEncRxCounter_ = (rxCounter + 1ULL) & kBleEncPacketCounterMask;
      }

      if ((llid == kBlePduLlControl) &&
          (rxLength >= 1U) &&
          (rxPacket_[2] == kBleLlCtrlStartEncRsp)) {
        // Keep procedure state unchanged here so the normal LL control
        // response path can queue/transmit final LL_START_ENC_RSP.
        emitBleTrace("LL_START_ENC_RSP_RX_POSTTX");
      }
    }
  }

  if (deferEncryptedDataDecrypt && !terminateInd && packetIsNew) {
    const uint64_t rxCounter = connectionEncRxCounter_;
    uint8_t plaintext[kBleDataPduMaxPayload] = {0};
    uint8_t plaintextLen = 0U;
    const bool decryptOk =
        bleCcmDecryptPayload(connectionEncSessionKey_, connectionEncIv_,
                            rxCounter, connectionEncRxDirection_, hdr0,
                            &rxPacket_[2], rxLengthRaw,
                            plaintext, &plaintextLen);

    if (!decryptOk) {
      terminateInd = true;
      terminateMicFailure = true;
      ++encDebug_.encRxMicFailCount;
      encDebug_.encRxLastMicFailHdr = hdr0;
      encDebug_.encRxLastMicFailLenRaw = rxLengthRaw;
      encDebug_.encRxLastMicFailDir = connectionEncRxDirection_;
      encDebug_.encRxLastMicFailCounterLo =
          static_cast<uint32_t>(rxCounter & 0xFFFFFFFFULL);
      recordMicFailPacket(rxLengthRaw);
      emitBleTrace("ENC_RX_MIC_FAIL_POSTTX_DATA");
    } else {
      rxWasDecrypted = true;
      rxLength = plaintextLen;
      if (plaintextLen > 0U) {
        memcpy(&rxPacket_[2], plaintext, plaintextLen);
      }
      connectionEncRxCounter_ = (rxCounter + 1ULL) & kBleEncPacketCounterMask;
    }
  }

  encDebug_.encLastRxHdr = hdr0;
  encDebug_.encLastRxLenRaw = rxLengthRaw;
  encDebug_.encLastRxWasNew = packetIsNew ? 1U : 0U;
  encDebug_.encLastRxWasDecrypted = rxWasDecrypted ? 1U : 0U;
  encDebug_.encLastRxCounterLo =
      static_cast<uint32_t>(connectionEncRxCounter_ & 0xFFFFFFFFULL);

  rememberConnectionRxDebug(
      llid, rxLength,
      bleConnectionPayloadProtocolOpcode(llid, &rxPacket_[2], rxLength), nesn, sn,
      packetIsNew, peerAckedLastTx);

  uint8_t deferredLlid = 0x01U;
  uint8_t deferredLength = 0U;

  if (!terminateInd) {
    // Interop fallback: some hosts stall after LL_ENC_RSP instead of issuing
    // the initiator-side LL_START_ENC_REQ. Queue a single next-event
    // LL_START_ENC_REQ so the host can complete the procedure with
    // LL_START_ENC_RSP, but do not emit any extra terminal response after that.
    if (connectionEncSessionValid_ &&
        connectionEncStartReqPending_ &&
        connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionPendingTxValid_) {
      connectionTxPayload_[0] = kBleLlCtrlStartEncReq;
      deferredLlid = kBlePduLlControl;
      deferredLength = 1U;
    } else if (packetIsNew &&
               !llControlHandledImmediate &&
               llid == kBlePduLlControl &&
               rxLength >= 1U) {
      uint8_t responseLength = 0U;
      if (buildLlControlResponse(&rxPacket_[2], rxLength, currentEventCounter,
                                 connectionTxPayload_, &responseLength,
                                 &terminateInd) &&
          responseLength > 0U) {
        deferredLlid = kBlePduLlControl;
        deferredLength = responseLength;
      }
    } else if (packetIsNew &&
               !l2capHandledImmediate &&
               !encryptionProcedureBusy &&
               llid == kBlePduDataStartOrComplete &&
               rxLength >= kBleL2capHeaderLen) {
      uint8_t responseLength = 0U;
      if (buildL2capResponse(&rxPacket_[2], rxLength, connectionTxPayload_,
                             &responseLength) &&
          responseLength > 0U) {
        deferredLlid = kBlePduDataStartOrComplete;
          deferredLength = responseLength;
      }
    }

    if (deferredLength == 0U) {
      maybeQueueCentralLinkSetupRequest();
    }

    // Peripheral-initiated SMP Security Request: ask the central to start
    // LE legacy pairing so that an STK is established before it sends
    // LL_ENC_REQ.  Only sent when no key is already available and no SMP
    // handshake is in progress.
    if ((deferredLength == 0U) &&
        !connectionPendingTxValid_ &&
        !encryptionProcedureBusy &&
        !connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionEncStartReqPending_ &&
        !smpStkValid_ &&
        (smpPairingState_ == kSmpPairingStateIdle) &&
        !connectionEncSessionValid_ &&
        connectionSmpSecurityRequestPending_) {
      writeLe16(&connectionTxPayload_[0], 2U);           // L2CAP length = 2
      writeLe16(&connectionTxPayload_[2], kBleL2capCidSmp);
      connectionTxPayload_[4] = kSmpCodeSecurityRequest; // opcode 0x0B
      connectionTxPayload_[5] = 0x01U;                   // AuthReq: Bonding, no MITM
      deferredLlid = kBlePduDataStartOrComplete;
      deferredLength = 6U;
      connectionSmpSecurityRequestPending_ = false;
      emitBleTrace("SMP_SEC_REQ_TX");
    }

    if ((deferredLength == 0U) &&
        !connectionPendingTxValid_ &&
        !encryptionProcedureBusy &&
        !connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionEncStartReqPending_ &&
        connectionServiceChangedIndicationsEnabled_ &&
        connectionServiceChangedIndicationPending_ &&
        !connectionServiceChangedIndicationAwaitingConfirm_) {
      writeLe16(&connectionTxPayload_[0], 7U);
      writeLe16(&connectionTxPayload_[2], kBleL2capCidAtt);
      connectionTxPayload_[4] = kAttOpHandleValueInd;
      writeLe16(&connectionTxPayload_[5], kHandleGattServiceChangedValue);
      writeLe16(&connectionTxPayload_[7], 0x0001U);
      writeLe16(&connectionTxPayload_[9], 0xFFFFU);
      deferredLlid = kBlePduDataStartOrComplete;
      deferredLength = 11U;
      connectionServiceChangedIndicationPending_ = false;
      connectionServiceChangedIndicationAwaitingConfirm_ = true;
    }

    if ((deferredLength == 0U) &&
        !connectionPendingTxValid_ &&
        !encryptionProcedureBusy &&
        !connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionEncStartReqPending_) {
      BleQueuedCustomNotificationState queuedCustom{};
      uint8_t queueGuard = connectionCustomNotificationQueueCount_;
      while ((deferredLength == 0U) && (queueGuard-- > 0U) &&
             peekCustomGattNotification(&queuedCustom)) {
        if (queuedCustom.characteristicIndex >= customGattCharacteristicCount_) {
          popCustomGattNotification();
          continue;
        }

        BleCustomCharacteristicState& custom =
            customGattCharacteristics_[queuedCustom.characteristicIndex];
        const bool modeEnabled = queuedCustom.indication
                                     ? (((custom.properties & kBleGattPropIndicate) != 0U) &&
                                        ((custom.cccdValue & 0x0002U) != 0U))
                                     : (((custom.properties & kBleGattPropNotify) != 0U) &&
                                        ((custom.cccdValue & 0x0001U) != 0U));
        const bool indicationFlowReady =
            !connectionServiceChangedIndicationAwaitingConfirm_ &&
            (connectionCustomIndicationAwaitingHandle_ == 0U);
        const bool canTransmit =
            modeEnabled && (!queuedCustom.indication || indicationFlowReady);
        if (!modeEnabled) {
          popCustomGattNotification();
          continue;
        }
        if (!canTransmit) {
          break;
        }

        writeLe16(&connectionTxPayload_[0],
                  static_cast<uint16_t>(3U + queuedCustom.valueLength));
        writeLe16(&connectionTxPayload_[2], kBleL2capCidAtt);
        connectionTxPayload_[4] = queuedCustom.indication
                                      ? kAttOpHandleValueInd
                                      : kAttOpHandleValueNtf;
        writeLe16(&connectionTxPayload_[5], custom.valueHandle);
        if (queuedCustom.valueLength > 0U) {
          memcpy(&connectionTxPayload_[7], queuedCustom.value,
                 queuedCustom.valueLength);
        }
        deferredLlid = kBlePduDataStartOrComplete;
        deferredLength = static_cast<uint8_t>(7U + queuedCustom.valueLength);
        if (queuedCustom.indication) {
          connectionCustomIndicationAwaitingHandle_ = custom.valueHandle;
        }
        popCustomGattNotification();
      }
    }

    if ((deferredLength == 0U) &&
        !connectionPendingTxValid_ &&
        !encryptionProcedureBusy &&
        !connectionEncStartReqTxPending_ &&
        !connectionEncAwaitingStartRsp_ &&
        !connectionEncStartReqPending_ &&
        connectionBatteryNotificationsEnabled_ &&
        connectionBatteryNotificationPending_) {
      writeLe16(&connectionTxPayload_[0], 4U);
      writeLe16(&connectionTxPayload_[2], kBleL2capCidAtt);
      connectionTxPayload_[4] = kAttOpHandleValueNtf;
      writeLe16(&connectionTxPayload_[5], kHandleBatteryLevelValue);
      connectionTxPayload_[7] = gapBatteryLevel_;
      deferredLlid = kBlePduDataStartOrComplete;
      deferredLength = 8U;
      connectionBatteryNotificationPending_ = false;
    }

    if (deferredLength > 0U) {
      connectionPendingTxLlid_ = deferredLlid;
      connectionPendingTxLength_ = deferredLength;
      memcpy(connectionPendingTxPayload_, connectionTxPayload_, deferredLength);
      connectionPendingTxValid_ = true;
    }
  }

  if (event != nullptr) {
    event->txPacketSent = txOk;
    event->txLlid = txLlid;
    event->txNesn = txNesnBit;
    event->txSn = txSnBit;
    event->txPayloadLength = txLength;
    event->txPayload =
        (txLength > 0U) ? &connectionLastTxPlainPayload_[0] : nullptr;
    event->emptyAckTransmitted = txOk && (txLlid == 0x01U) && (txLength == 0U);
    event->terminateInd = terminateInd;
  }

  if (connectionEncEnableTxOnNextEvent_ && lastTxOkForEncEnable && !terminateInd) {
    connectionEncTxEnabled_ = true;
    connectionEncEnableTxOnNextEvent_ = false;
  }

  if (terminateInd) {
    const BleDisconnectReason disconnectReason =
        terminateMicFailure
            ? BleDisconnectReason::kMicFailure
            : (peerTerminateIndReceived ? BleDisconnectReason::kPeerTerminate
                                        : BleDisconnectReason::kInternalTerminate);
    const uint8_t disconnectErrorCode =
        terminateMicFailure ? kBleLlErrorMicFailure : peerTerminateErrorCode;
    if (terminateMicFailure) {
      emitBleTrace("TERMINATE_MIC_FAILURE");
    } else if (peerTerminateIndReceived) {
      emitBleTrace("PEER_TERMINATE");
    } else {
      emitBleTrace("TERMINATE_INTERNAL");
    }
    recordDisconnectDebug(disconnectReason, disconnectErrorCode);
    connectionPendingTxLlid_ = 0x01U;
    connectionPendingTxLength_ = 0U;
    connectionPendingTxValid_ = false;
    connected_ = false;
    restoreAdvertisingLinkDefaults();
  }

  clearRadioCoreEvents(radio_);
  return true;
}

bool BleRadio::pollConnectionEvent(BleConnectionEvent* event,
                                   uint32_t spinLimit) {
  if (event != nullptr) {
    resetConnectionEventStruct(event);
    if (dequeueConnectionEvent(event)) {
      rememberLatestConnectionRssi(*event);
      return true;
    }
  }

  const bool ran = pollConnectionEventInternal(event, spinLimit);
  if (ran && event != nullptr) {
    rememberLatestConnectionRssi(*event);
  }
  return ran;
}
