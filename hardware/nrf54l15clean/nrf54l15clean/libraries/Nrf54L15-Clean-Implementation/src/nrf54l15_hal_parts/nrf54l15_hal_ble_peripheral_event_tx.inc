
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
  const uint16_t rxL2capCid =
      (rxLength >= kBleL2capHeaderLen) ? readLe16(&rxPacket_[4]) : 0U;
  const uint8_t rxAttOpcode =
      (rxLength >= (kBleL2capHeaderLen + 1U))
          ? rxPacket_[2 + kBleL2capHeaderLen]
          : 0U;
  auto pendingTxIsAttServerPushPdu = [&]() {
    if (!connectionPendingTxValid_ ||
        (connectionPendingTxLlid_ != kBlePduDataStartOrComplete) ||
        (connectionPendingTxLength_ < 7U) ||
        (readLe16(&connectionPendingTxPayload_[2]) != kBleL2capCidAtt)) {
      return false;
    }
    const uint8_t opcode = connectionPendingTxPayload_[4];
    return (opcode == kAttOpHandleValueNtf) ||
           (opcode == kAttOpHandleValueInd);
  };
  auto codedCustomAttWriteNeedsDeferredResponse = [&]() {
    if ((connectionCurrentRxPhy_ & kBlePhyCoded) == 0U) {
      return false;
    }
    if ((llid != kBlePduDataStartOrComplete) ||
        (rxL2capCid != kBleL2capCidAtt) ||
        (rxAttOpcode != kAttOpWriteReq) ||
        (rxLength < (kBleL2capHeaderLen + 3U))) {
      return false;
    }

    const uint16_t handle =
        readLe16(&rxPacket_[2 + kBleL2capHeaderLen + 1U]);
    const bool isCccdHandle =
        (handle == kHandleGattServiceChangedCccd) ||
        (handle == kHandleBatteryLevelCccd) ||
        (findCustomCharacteristicByCccdHandle(handle) != nullptr);
    const bool isCustomValueHandle =
        (findCustomCharacteristicByValueHandle(handle) != nullptr);
    return !isCccdHandle && isCustomValueHandle;
  };
  const bool currentRxNeedsDeferredCustomWriteResponse =
      codedCustomAttWriteNeedsDeferredResponse();
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
      !currentRxNeedsDeferredCustomWriteResponse &&
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
      uint16_t localRxMtu = connectionRequestedAttMtu_;
      if (localRxMtu < kBleDefaultAttMtu) {
        localRxMtu = kBleDefaultAttMtu;
      } else if (localRxMtu > localPreferredAttMtu()) {
        localRxMtu = localPreferredAttMtu();
      }
      const uint16_t negotiatedMtu = minU16(peerRxMtu, localRxMtu);
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
  const bool suppressPendingServerPushForDeferredWrite =
      currentRxNeedsDeferredCustomWriteResponse &&
      pendingTxIsAttServerPushPdu();
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
    } else if (connectionPendingTxValid_ &&
               !suppressPendingServerPushForDeferredWrite) {
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
  const uint32_t codedRxEndAdjustUs =
      ((connectionCurrentRxPhy_ & kBlePhyCoded) != 0U) ? 256U : 0U;
  const uint32_t txTriggerTargetUs =
      rxEndTimestampUs + codedRxEndAdjustUs + kBleConnTxenAfterRxUs;
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
  const uint32_t txEndBudgetUs =
      bleConnectionTxEndBudgetUsOnPhy(txLengthOnAir, connectionCurrentTxPhy_);
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
    const uint16_t rxPayloadLength = currentRxDataPduPayloadLength();
    const uint16_t txPayloadLength = currentTxDataPduPayloadLength();
    const uint32_t dynamicFollowListenUs =
        bleDataPduAirTimeUsOnPhy(static_cast<uint8_t>(rxPayloadLength),
                                 connectionCurrentRxPhy_) +
        bleConnectionTxEndBudgetUsOnPhy(static_cast<uint8_t>(txPayloadLength),
                                        connectionCurrentTxPhy_) +
        1200U;
    if (followListenUs < dynamicFollowListenUs) {
      followListenUs = dynamicFollowListenUs;
    }
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

        const uint32_t codedFollowRxEndAdjustUs =
            ((connectionCurrentRxPhy_ & kBlePhyCoded) != 0U) ? 256U : 0U;
        const uint32_t txFollowTriggerTargetUs =
            followRxEndTimestampUs + codedFollowRxEndAdjustUs +
            kBleConnTxenAfterRxUs;
        const uint32_t txFollowArmUs = bleTimingUs();
        noteTxenLag(&encDebug_,
                    timeReachedUs(txFollowArmUs, txFollowTriggerTargetUs)
                        ? static_cast<uint32_t>(txFollowArmUs -
                                                txFollowTriggerTargetUs)
                        : 0U);
        bleTriggerTxAtTargetUsCritical(radio_, txFollowTriggerTargetUs);

        const uint32_t txFollowEndBudgetUs =
            bleConnectionTxEndBudgetUsOnPhy(txFollowLengthOnAir,
                                            connectionCurrentTxPhy_);
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
          auto followNeedsDeferredCustomWriteResponse = [&]() {
            if ((connectionCurrentRxPhy_ & kBlePhyCoded) == 0U) {
              return false;
            }
            if ((llidFollow != kBlePduDataStartOrComplete) ||
                (rxLengthFollow < (kBleL2capHeaderLen + 3U))) {
              return false;
            }
            const uint16_t followCid = readLe16(&rxPacket_[4]);
            if ((followCid != kBleL2capCidAtt) ||
                (rxPacket_[2 + kBleL2capHeaderLen] != kAttOpWriteReq)) {
              return false;
            }
            const uint16_t handle =
                readLe16(&rxPacket_[2 + kBleL2capHeaderLen + 1U]);
            const bool isCccdHandle =
                (handle == kHandleGattServiceChangedCccd) ||
                (handle == kHandleBatteryLevelCccd) ||
                (findCustomCharacteristicByCccdHandle(handle) != nullptr);
            const bool isCustomValueHandle =
                (findCustomCharacteristicByValueHandle(handle) != nullptr);
            return !isCccdHandle && isCustomValueHandle;
          };
          const bool followCanDisplacePendingServerPush =
              connectionPendingTxValid_ &&
              pendingTxIsAttServerPushPdu();
          if (packetIsNewFollow &&
              (!connectionPendingTxValid_ ||
               followCanDisplacePendingServerPush)) {
            if ((llidFollow == kBlePduLlControl) && (rxLengthFollow >= 1U)) {
              uint8_t responseLength = 0U;
              bool followTerminate = false;
              uint8_t followResponsePayload[kBleDataPduMaxPayload] = {0};
              uint8_t* responsePayload =
                  followCanDisplacePendingServerPush
                      ? followResponsePayload
                      : connectionPendingTxPayload_;
              if (buildLlControlResponse(&rxPacket_[2], rxLengthFollow,
                                         currentEventCounter,
                                         responsePayload,
                                         &responseLength, &followTerminate) &&
                  responseLength > 0U) {
                if (followCanDisplacePendingServerPush) {
                  memcpy(connectionPendingTxPayload_, followResponsePayload,
                         responseLength);
                }
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
              uint8_t followResponsePayload[kBleDataPduMaxPayload] = {0};
              uint8_t* responsePayload =
                  (followCanDisplacePendingServerPush ||
                   followNeedsDeferredCustomWriteResponse())
                      ? followResponsePayload
                      : connectionPendingTxPayload_;
              if (buildL2capResponse(&rxPacket_[2], rxLengthFollow,
                                     responsePayload,
                                     &responseLength) &&
                  responseLength > 0U) {
                if (responsePayload == followResponsePayload) {
                  memcpy(connectionPendingTxPayload_, followResponsePayload,
                         responseLength);
                }
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
