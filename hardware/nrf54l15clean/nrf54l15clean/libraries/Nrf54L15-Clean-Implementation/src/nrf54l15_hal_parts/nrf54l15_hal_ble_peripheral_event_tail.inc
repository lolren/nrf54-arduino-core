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
                bleConnectionTxEndBudgetUsOnPhy(txFollowLength,
                                                connectionCurrentTxPhy_);
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
