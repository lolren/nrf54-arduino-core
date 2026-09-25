
bool BleRadio::buildLlControlResponse(const uint8_t* payload, uint8_t length,
                                      uint16_t currentEventCounter,
                                      uint8_t* outPayload, uint8_t* outLength,
                                      bool* terminateInd) {
  if (terminateInd != nullptr) {
    *terminateInd = false;
  }
  if (payload == nullptr || outPayload == nullptr || outLength == nullptr || length == 0U) {
    return false;
  }

  const uint8_t opcode = payload[0];
  *outLength = 0U;
  auto rejectMalformedRequest = [&]() -> bool {
    outPayload[0] = kBleLlCtrlRejectExtInd;
    outPayload[1] = opcode;
    outPayload[2] = kBleLlErrorUnsupportedLlParamValue;
    *outLength = 3U;
    emitBleTrace("LL_REJECT_MALFORMED");
    return true;
  };
  auto rejectUnsupportedFeatureRequest = [&]() -> bool {
    outPayload[0] = kBleLlCtrlRejectExtInd;
    outPayload[1] = opcode;
    outPayload[2] = kBleLlErrorUnsupportedRemoteFeature;
    *outLength = 3U;
    emitBleTrace("LL_REJECT_UNSUPPORTED_FEATURE");
    return true;
  };
  auto rejectPinOrKeyMissing = [&]() -> bool {
    outPayload[0] = kBleLlCtrlRejectExtInd;
    outPayload[1] = opcode;
    outPayload[2] = kBleLlErrorPinOrKeyMissing;
    *outLength = 3U;
    if (connectionRole_ == BleConnectionRole::kPeripheral &&
        opcode == kBleLlCtrlEncReq) {
      connectionMissingKeyTerminatePending_ = true;
    }
    emitBleTrace("LL_REJECT_PIN_OR_KEY_MISSING");
    return true;
  };
  auto rejectCommandDisallowed = [&]() -> bool {
    outPayload[0] = kBleLlCtrlRejectExtInd;
    outPayload[1] = opcode;
    outPayload[2] = kBleLlErrorCommandDisallowed;
    *outLength = 3U;
    emitBleTrace("LL_REJECT_COMMAND_DISALLOWED");
    return true;
  };
  auto rejectProcedureCollision = [&]() -> bool {
    outPayload[0] = kBleLlCtrlRejectExtInd;
    outPayload[1] = opcode;
    outPayload[2] = kBleLlErrorLlProcedureCollision;
    *outLength = 3U;
    emitBleTrace("LL_REJECT_PROCEDURE_COLLISION");
    return true;
  };

  switch (opcode) {
    case kBleLlCtrlTerminateInd:
      if (length != 2U) {
        return rejectMalformedRequest();
      }
      if (terminateInd != nullptr) {
        *terminateInd = true;
      }
      return false;

    case kBleLlCtrlConnectionUpdateInd:
      if (length != 12U) {
        return rejectMalformedRequest();
      }
      if (connectionRole_ != BleConnectionRole::kPeripheral) {
        return rejectCommandDisallowed();
      }
      {
        const uint8_t winSize = payload[1];
        const uint16_t winOffset = readLe16(&payload[2]);
        const uint16_t interval = readLe16(&payload[4]);
        const uint16_t latency = readLe16(&payload[6]);
        const uint16_t timeout = readLe16(&payload[8]);
        const uint16_t instant = readLe16(&payload[10]);
        bool valid = true;
        const uint8_t maxWinSize = static_cast<uint8_t>(
            minU16(8U, static_cast<uint16_t>(interval > 0U
                                                ? interval - 1U
                                                : 0U)));
        if (winSize < 1U || winSize > maxWinSize ||
            winOffset > interval) {
          valid = false;
        }
        if (interval < 6U || interval > 3200U) {
          valid = false;
        }
        if (latency > 499U) {
          valid = false;
        }
        if (timeout < 10U || timeout > 3200U) {
          valid = false;
        }
        const uint32_t timeoutMs = static_cast<uint32_t>(timeout) * 10UL;
        const uint32_t requiredMs =
            ((1UL + static_cast<uint32_t>(latency)) *
             static_cast<uint32_t>(interval) * 5UL) /
            2UL;
        if (timeoutMs <= requiredMs) {
          valid = false;
        }
        const uint16_t instantDelta =
            static_cast<uint16_t>(instant - currentEventCounter);
        const bool instantInFuture = (instantDelta != 0U) && (instantDelta < 0x8000U);
        if (!instantInFuture) {
          valid = false;
        }
        if (valid) {
          connectionPendingWinSize_ = (winSize > 0U) ? winSize : 1U;
          connectionPendingWinOffset_ = winOffset;
          connectionPendingIntervalUnits_ = interval;
          connectionPendingLatency_ = latency;
          connectionPendingTimeoutUnits_ = timeout;
          connectionUpdateInstant_ = instant;
          connectionUpdatePending_ = true;
          emitBleTrace("LL_CONN_UPDATE_PENDING");
        } else {
          return rejectMalformedRequest();
        }
      }
      return false;

    case kBleLlCtrlChannelMapInd:
      if (length != 8U) {
        return rejectMalformedRequest();
      }
      if (connectionRole_ != BleConnectionRole::kPeripheral) {
        return rejectCommandDisallowed();
      }
      {
        uint8_t map[5] = {payload[1], payload[2], payload[3], payload[4], payload[5]};
        const uint16_t instant = readLe16(&payload[6]);
        const bool reservedBitsClear = (map[4] & 0xE0U) == 0U;
        const uint8_t count = bitCount37(map);
        const uint16_t instantDelta =
            static_cast<uint16_t>(instant - currentEventCounter);
        const bool instantInFuture = (instantDelta != 0U) && (instantDelta < 0x8000U);
        const bool duplicatePendingMap =
            connectionChannelMapPending_ &&
            (connectionChannelMapInstant_ == instant) &&
            (memcmp(connectionPendingChannelMap_, map,
                    sizeof(connectionPendingChannelMap_)) == 0);
        const bool duplicateAppliedMap =
            !connectionChannelMapPending_ &&
            (static_cast<uint16_t>(encDebug_.connLastChannelMapAppliedEventCounter) ==
             instant) &&
            (memcmp(connectionChannelMap_, map,
                    sizeof(connectionChannelMap_)) == 0);
        if (reservedBitsClear && count >= 2U && instantInFuture) {
          memcpy(connectionPendingChannelMap_, map, sizeof(connectionPendingChannelMap_));
          connectionPendingChannelCount_ = count;
          connectionChannelMapInstant_ = instant;
          connectionChannelMapPending_ = true;
          uint8_t resyncEvents = static_cast<uint8_t>(instantDelta);
          if (resyncEvents > kBleConnChannelMapPendingResyncEventsMax) {
            resyncEvents = kBleConnChannelMapPendingResyncEventsMax;
          }
          armConnectionResyncWindow(kBleConnResyncExtraListenUs, resyncEvents);
          ++encDebug_.connChannelMapPendingCount;
          encDebug_.connLastChannelMapRxEventCounter = currentEventCounter;
          encDebug_.connLastChannelMapInstant = instant;
          emitBleTrace("LL_CHANNEL_MAP_PENDING");
        } else if (reservedBitsClear && count >= 2U && duplicatePendingMap) {
          ++encDebug_.connChannelMapDuplicateCount;
          armConnectionResyncWindow(kBleConnResyncExtraListenUs,
                                    kBleConnChannelMapPendingResyncEventsMax);
          emitBleTrace("LL_CHANNEL_MAP_DUP_PENDING");
        } else if (reservedBitsClear && count >= 2U && duplicateAppliedMap) {
          ++encDebug_.connChannelMapDuplicateCount;
          armConnectionResyncWindow(kBleConnResyncExtraListenUs,
                                    kBleConnChannelMapApplyResyncEvents);
          emitBleTrace("LL_CHANNEL_MAP_DUP_APPLIED");
        } else {
          return rejectMalformedRequest();
        }
      }
      return false;

    case kBleLlCtrlEncReq:
      if (length != 23U) {
        return rejectMalformedRequest();
      }
      if (connectionEncSessionValid_ || connectionEncStartReqPending_ ||
          connectionEncStartReqTxPending_ ||
          connectionEncAwaitingStartRsp_ || connectionEncRxEnabled_ ||
          connectionEncTxEnabled_) {
        return rejectProcedureCollision();
      }
      if (!smpStkValid_) {
        return rejectPinOrKeyMissing();
      }
      if (bondKeyPrimedForConnection_) {
        activeBondMeetsSecurityPolicy_ =
            bondMeetsSecurityPolicy(bondRecord_, securityPolicy_);
        if (!activeBondMeetsSecurityPolicy_) {
          connectionSmpSecurityRequestPending_ = true;
          emitBleTrace("LL_ENC_REQ_BOND_POLICY_REJECT");
          return rejectPinOrKeyMissing();
        }
        const uint16_t ediv = readLe16(&payload[9]);
        if ((ediv != bondRecord_.ediv) ||
            (memcmp(&payload[1], bondRecord_.rand, sizeof(bondRecord_.rand)) != 0)) {
          bondKeyPrimedForConnection_ = false;
          smpStkValid_ = false;
          smpPeerLtkValid_ = false;
          return rejectPinOrKeyMissing();
        }
      }
      {
        // Controller-compatible layout:
        //   SKD[0..7]  = SKDm (from LL_ENC_REQ)
        //   SKD[8..15] = SKDs (from LL_ENC_RSP)
        //   IV[0..3]   = IVm  (from LL_ENC_REQ)
        //   IV[4..7]   = IVs  (from LL_ENC_RSP)
        memset(connectionEncSkd_, 0, sizeof(connectionEncSkd_));
        memcpy(&connectionEncSkd_[0], &payload[11], 8U);  // SKDm
        memcpy(&connectionEncIv_[0], &payload[19], 4U);   // IVm
        memcpy(encDebug_.encLastSkdm, &payload[11], sizeof(encDebug_.encLastSkdm));
        memcpy(encDebug_.encLastIvm, &payload[19], sizeof(encDebug_.encLastIvm));
        smpEncReqEdiv_ = readLe16(&payload[9]);
        memcpy(smpEncReqRand_, &payload[1], sizeof(smpEncReqRand_));

        const uint32_t seed =
            micros() ^ connectionAccessAddress_ ^
            static_cast<uint32_t>(connectionEventCounter_ << 16U) ^
            readLe32(&payload[11]) ^ readLe32(&payload[19]);
        if (connectionPrefetchedEncMaterialValid_) {
          memcpy(&connectionEncSkd_[8], connectionPrefetchedSkds_,
                 sizeof(connectionPrefetchedSkds_));
          memcpy(&connectionEncIv_[4], connectionPrefetchedIvs_,
                 sizeof(connectionPrefetchedIvs_));
          connectionPrefetchedEncMaterialValid_ = false;
          ++encDebug_.encRandomPrefetchUseCount;
        } else {
          const bool skdsOk = fillBleSecurityRandomBytes(
              &connectionEncSkd_[8], 8U, seed,
              kBleSecurityRandomSpinLimit);
          if (skdsOk) {
            ++encDebug_.encRandomHardwareCount;
          } else {
            ++encDebug_.encRandomFallbackCount;
          }
          const bool ivsOk = fillBleSecurityRandomBytes(
              &connectionEncIv_[4], 4U, seed ^ 0x6BA38B4FUL,
              kBleSecurityRandomSpinLimit);
          if (ivsOk) {
            ++encDebug_.encRandomHardwareCount;
          } else {
            ++encDebug_.encRandomFallbackCount;
          }
          if (!skdsOk || !ivsOk) {
            memset(&connectionEncSkd_[8], 0, 8U);
            memset(&connectionEncIv_[4], 0, 4U);
            emitBleTrace("ENC_RANDOM_UNAVAILABLE");
            return rejectPinOrKeyMissing();
          }
        }
        memcpy(encDebug_.encLastSkds, &connectionEncSkd_[8], sizeof(encDebug_.encLastSkds));
        memcpy(encDebug_.encLastIvs, &connectionEncIv_[4], sizeof(encDebug_.encLastIvs));
        // Derive the session key after the TX phase so we meet T_IFS timing.
        connectionEncKeyDerivationPending_ = true;
        memset(connectionEncSessionKey_, 0, sizeof(connectionEncSessionKey_));
        memset(connectionEncSessionKeyAlt_, 0, sizeof(connectionEncSessionKeyAlt_));
        connectionEncAltKeyValid_ = false;
        // Per BLE spec Core 5.3 Vol 6 Part B §5.1.3.2:
        // directionBit=1 for PDUs sent by the master (central→peripheral),
        // directionBit=0 for PDUs sent by the slave (peripheral→central).
        // As the peripheral: RX (from master) uses direction=1, TX (to master) uses direction=0.
        connectionEncRxDirection_ = 1U;
        connectionEncTxDirection_ = 0U;
        encDebug_.encLastRxDir = connectionEncRxDirection_;
        encDebug_.encLastTxDir = connectionEncTxDirection_;

        connectionEncSessionValid_ = true;
        connectionEncRxEnabled_ = false;
        connectionEncTxEnabled_ = false;
        // Session-key derivation runs after LL_ENC_RSP is transmitted. The
        // peripheral schedules LL_START_ENC_REQ as soon as derivation finishes.
        (void)bleLlSetEncryptionProcedurePhase(
            BleLlEncryptionPhase::kAwaitStartRequest,
            &connectionEncStartReqPending_,
            &connectionEncStartReqTxPending_,
            &connectionEncAwaitingStartRsp_,
            &connectionEncEnableTxOnNextEvent_);
        connectionEncRxCounter_ = 0ULL;
        connectionEncTxCounter_ = 0ULL;
        connectionPendingTxValid_ = false;
        connectionPendingTxLlid_ = 0x01U;
        connectionPendingTxLength_ = 0U;
        clearPendingL2capTxFragment();
        connectionLastTxWasEncrypted_ = false;
        connectionLastTxEncryptedLength_ = 0U;
        memset(connectionLastTxEncryptedPayload_, 0, sizeof(connectionLastTxEncryptedPayload_));
        connectionEncPrecomputedStartRspValid_ = false;
        memset(connectionEncPrecomputedStartRsp_, 0, sizeof(connectionEncPrecomputedStartRsp_));
        connectionEncPrecomputedStartRspTxValid_ = false;
        connectionEncPrecomputedStartRspTxCounter_ = 0ULL;
        memset(connectionEncPrecomputedStartRspTx_, 0,
               sizeof(connectionEncPrecomputedStartRspTx_));

        outPayload[0] = kBleLlCtrlEncRsp;
        memcpy(&outPayload[1], &connectionEncSkd_[8], 8U);
        memcpy(&outPayload[9], &connectionEncIv_[4], 4U);
        *outLength = 13U;
        emitBleTrace("LL_ENC_REQ_ACCEPTED");
        return true;
      }

    case kBleLlCtrlStartEncReq:
      if (length != 1U) {
        return rejectMalformedRequest();
      }
      if (!connectionEncSessionValid_) {
        return rejectPinOrKeyMissing();
      }
      if (connectionEncTxEnabled_) {
        return rejectProcedureCollision();
      }
      if (!connectionEncStartReqPending_ &&
          !connectionEncStartReqTxPending_ &&
          !connectionEncAwaitingStartRsp_) {
        return rejectCommandDisallowed();
      }
      connectionEncRxEnabled_ = true;
      (void)bleLlSetEncryptionProcedurePhase(
          BleLlEncryptionPhase::kEnableTxOnNextEvent,
          &connectionEncStartReqPending_,
          &connectionEncStartReqTxPending_,
          &connectionEncAwaitingStartRsp_,
          &connectionEncEnableTxOnNextEvent_);
      outPayload[0] = kBleLlCtrlStartEncRsp;
      *outLength = 1U;
      emitBleTrace("LL_START_ENC_REQ_ACCEPTED");
      return true;

    case kBleLlCtrlPauseEncReq:
      if (length != 1U) {
        return rejectMalformedRequest();
      }
      if (!connectionEncSessionValid_) {
        return rejectPinOrKeyMissing();
      }
      if (connectionEncStartReqPending_ || connectionEncStartReqTxPending_ ||
          connectionEncAwaitingStartRsp_) {
        return rejectProcedureCollision();
      }
      if (!connectionEncRxEnabled_ && !connectionEncTxEnabled_) {
        return rejectCommandDisallowed();
      }
      ++encDebug_.encPauseReqAcceptedCount;
      ++encDebug_.encClearCount;
      encDebug_.encLastClearReason = 0x01U;  // PauseEncReq accepted.
      clearEncryptionState();
      outPayload[0] = kBleLlCtrlPauseEncRsp;
      *outLength = 1U;
      emitBleTrace("LL_PAUSE_ENC_REQ_ACCEPTED");
      return true;

    case kBleLlCtrlCteReq:
    case kBleLlCtrlClockAccuracyReq:
      if (length != 2U) {
        return rejectMalformedRequest();
      }
      return rejectUnsupportedFeatureRequest();

    case kBleLlCtrlCisReq:
      if (length != 36U) {
        return rejectMalformedRequest();
      }
      return rejectUnsupportedFeatureRequest();

    case kBleLlCtrlConnectionParamReq:
      if (length != 24U) {
        return rejectMalformedRequest();
      }
      {
        const uint16_t intervalMin = readLe16(&payload[1]);
        const uint16_t intervalMax = readLe16(&payload[3]);
        const uint16_t latency = readLe16(&payload[5]);
        const uint16_t timeout = readLe16(&payload[7]);
        if (!connParamsAreValid(intervalMin, intervalMax, latency, timeout)) {
          return rejectMalformedRequest();
        }
        if (connectionRole_ == BleConnectionRole::kCentral) {
          if (connectionCentralConnParamIndPending_ ||
              connectionUpdatePending_) {
            return rejectProcedureCollision();
          }
          connectionCentralConnParamIntervalUnits_ =
              chooseAcceptedConnIntervalUnits(intervalMin, intervalMax);
          connectionCentralConnParamLatency_ = latency;
          connectionCentralConnParamTimeoutUnits_ = timeout;
          connectionCentralConnParamIndPending_ = true;
          emitBleTrace("LL_CONN_PARAM_UPDATE_PENDING");
        } else if (connectionRole_ != BleConnectionRole::kPeripheral) {
          return rejectCommandDisallowed();
        }

        outPayload[0] = kBleLlCtrlConnectionParamRsp;
        memcpy(&outPayload[1], &payload[1], 23U);
        *outLength = 24U;
        emitBleTrace("LL_CONN_PARAM_RSP_TX");
      }
      return true;

    case kBleLlCtrlFeatureReq:
    case kBleLlCtrlSlaveFeatureReq:
      if (length != 9U) {
        return rejectMalformedRequest();
      }
      outPayload[0] = kBleLlCtrlFeatureRsp;
      outPayload[1] = kBleLlFeatureMaskOctet0;
      outPayload[2] = kBleLlFeatureMaskOctet1;
      memset(&outPayload[3], 0, 6U);
      *outLength = 9U;
      return true;

    case kBleLlCtrlVersionInd:
      if (length != 6U) {
        return rejectMalformedRequest();
      }
      outPayload[0] = kBleLlCtrlVersionInd;
      outPayload[1] = 0x0DU;  // Core spec version 5.4.
      writeLe16(&outPayload[2], 0x0059U);  // Nordic Semiconductor company ID.
      writeLe16(&outPayload[4], 0x0001U);  // Implementation subversion.
      *outLength = 6U;
      return true;

    case kBleLlCtrlLengthReq:
      if (length != 9U) {
        return rejectMalformedRequest();
      }
      updateConnectionDataLengthFromPeer(readLe16(&payload[1]),
                                         readLe16(&payload[3]),
                                         readLe16(&payload[5]),
                                         readLe16(&payload[7]));
      connectionDataLengthUpdatePending_ = false;
      connectionDataLengthUpdateInFlight_ = false;
      connectionDataLengthUpdateComplete_ = true;
      // LL_LENGTH_RSP: MaxRxOctets, MaxRxTime, MaxTxOctets, MaxTxTime.
      outPayload[0] = kBleLlCtrlLengthRsp;
      writeLe16(&outPayload[1], localMaxDataPduPayloadLength());
      writeLe16(&outPayload[3], currentDataLengthMaxTimeUs());
      writeLe16(&outPayload[5], localMaxDataPduPayloadLength());
      writeLe16(&outPayload[7], currentDataLengthMaxTimeUs());
      *outLength = 9U;
      return true;

    case kBleLlCtrlPhyReq:
      if (length != 3U) {
        return rejectMalformedRequest();
      }
      if (connectionPhyRequestInFlight_ || connectionPhyUpdatePending_) {
        return rejectProcedureCollision();
      }
      if (supportedBlePhys(payload[1]) == kBlePhyNone ||
          supportedBlePhys(payload[2]) == kBlePhyNone) {
        return rejectMalformedRequest();
      }
      {
        const bool pairingInProgress =
            (smpPairingState_ != kSmpPairingStateIdle) &&
            !connectionEncSessionValid_;
        const uint8_t symmetricPhys =
            pairingInProgress ? static_cast<uint8_t>(kBlePhy1M)
                              : preferredSymmetricBlePhys();
        outPayload[0] = kBleLlCtrlPhyRsp;
        outPayload[1] = symmetricPhys;
        outPayload[2] = symmetricPhys;
        *outLength = 3U;
        emitBleTrace("LL_PHY_REQ_RX");
        if (pairingInProgress) {
          emitBleTrace("LL_PHY_HOLD_1M_PAIRING");
        }
        emitBleTrace("LL_PHY_RSP_TX");
        return true;
      }

    case kBleLlCtrlPingReq:
      if (length != 1U) {
        return rejectMalformedRequest();
      }
      outPayload[0] = kBleLlCtrlPingRsp;
      *outLength = 1U;
      return true;

    case kBleLlCtrlConnectionParamRsp:
      if (length != 24U) {
        return false;
      }
      // This controller currently initiates parameter changes with L2CAP or
      // LL_CONNECTION_UPDATE_IND, never LL_CONNECTION_PARAM_REQ. A response
      // therefore has no local procedure state to advance.
      emitBleTrace("LL_CONN_PARAM_RSP_UNEXPECTED");
      return false;

    case kBleLlCtrlEncRsp:
      if (length != 13U) {
        return false;
      }
      if (!connectionEncSessionValid_ ||
          connectionRole_ != BleConnectionRole::kCentral) {
        return false;
      }
      memcpy(&connectionEncSkd_[8], &payload[1], 8U);
      memcpy(&connectionEncIv_[4], &payload[9], 4U);
      memcpy(encDebug_.encLastSkds, &connectionEncSkd_[8],
             sizeof(encDebug_.encLastSkds));
      memcpy(encDebug_.encLastIvs, &connectionEncIv_[4],
             sizeof(encDebug_.encLastIvs));
      if (!aesEncryptLe(smpStk_, connectionEncSkd_, connectionEncSessionKey_)) {
        connectionEncSessionValid_ = false;
        connectionEncStartReqTxPending_ = false;
        connectionEncAwaitingStartRsp_ = false;
        encDebug_.encLastSessionKeyValid = 0U;
        encDebug_.encLastSessionAltKeyValid = 0U;
        emitBleTrace("ENC_SESSION_KEY_FAIL");
        return false;
      }
      {
        uint8_t skdAlt[16] = {0};
        memcpy(&skdAlt[0], &connectionEncSkd_[8], 8U);
        memcpy(&skdAlt[8], &connectionEncSkd_[0], 8U);
        connectionEncAltKeyValid_ =
            aesEncryptLe(smpStk_, skdAlt, connectionEncSessionKeyAlt_);
      }
      memcpy(encDebug_.encLastSessionKey, connectionEncSessionKey_,
             sizeof(encDebug_.encLastSessionKey));
      memcpy(encDebug_.encLastSessionAltKey, connectionEncSessionKeyAlt_,
             sizeof(encDebug_.encLastSessionAltKey));
      encDebug_.encLastSessionKeyValid = 1U;
      encDebug_.encLastSessionAltKeyValid = connectionEncAltKeyValid_ ? 1U : 0U;
      primeStartEncryptionResponseCiphertext();
      connectionEncKeyDerivationPending_ = false;
      (void)bleLlSetEncryptionProcedurePhase(
          BleLlEncryptionPhase::kAwaitStartRequest,
          &connectionEncStartReqPending_,
          &connectionEncStartReqTxPending_,
          &connectionEncAwaitingStartRsp_,
          &connectionEncEnableTxOnNextEvent_);
      emitBleTrace("LL_ENC_RSP_RX");
      return false;

    case kBleLlCtrlStartEncRsp:
      if (length != 1U) {
        return false;
      }
      if (connectionEncAwaitingStartRsp_ && connectionEncSessionValid_) {
        ++encDebug_.encStartRspRxCount;
        connectionEncAwaitingStartRsp_ = false;
        connectionEncTxEnabled_ = true;
        connectionEncEnableTxOnNextEvent_ = false;
        connectionEncPrecomputedStartRspValid_ = false;
        connectionEncPrecomputedStartRspTxValid_ = false;
        emitBleTrace("LL_START_ENC_RSP_RX");
        if (connectionRole_ == BleConnectionRole::kPeripheral) {
          outPayload[0] = kBleLlCtrlStartEncRsp;
          *outLength = 1U;
          return true;
        }
      }
      return false;

    case kBleLlCtrlPauseEncRsp:
      if (length != 1U) {
        return false;
      }
      ++encDebug_.encPauseRspRxCount;
      ++encDebug_.encClearCount;
      encDebug_.encLastClearReason = 0x02U;  // PauseEncRsp received.
      clearEncryptionState();
      emitBleTrace("LL_PAUSE_ENC_RSP_RX");
      return false;

    case kBleLlCtrlPingRsp:
      if (length != 1U) {
        return false;
      }
      return false;

    case kBleLlCtrlLengthRsp:
      if (length != 9U) {
        return false;
      }
      updateConnectionDataLengthFromPeer(readLe16(&payload[1]),
                                         readLe16(&payload[3]),
                                         readLe16(&payload[5]),
                                         readLe16(&payload[7]));
      connectionDataLengthUpdatePending_ = false;
      connectionDataLengthUpdateInFlight_ = false;
      connectionDataLengthUpdateComplete_ = true;
      emitBleTrace("LL_LENGTH_RSP_RX");
      return false;

    case kBleLlCtrlRejectInd:
      if (length != 2U) {
        return false;
      }
      if (payload[1] == kBleLlCtrlLengthReq) {
        connectionDataLengthUpdatePending_ = false;
        connectionDataLengthUpdateInFlight_ = false;
      } else if (payload[1] == kBleLlCtrlPhyReq) {
        connectionPhyRequestPending_ = false;
        connectionPhyRequestInFlight_ = false;
      }
      if (connectionEncAwaitingStartRsp_ || connectionEncStartReqPending_ ||
          connectionEncStartReqTxPending_) {
        ++encDebug_.encClearCount;
        encDebug_.encLastClearReason = 0x03U;  // RejectInd during enc procedure.
        clearEncryptionState();
      }
      return false;

    case kBleLlCtrlRejectExtInd:
      if (length != 3U) {
        return false;
      }
      if (payload[1] == kBleLlCtrlLengthReq) {
        connectionDataLengthUpdatePending_ = false;
        connectionDataLengthUpdateInFlight_ = false;
      } else if (payload[1] == kBleLlCtrlPhyReq) {
        connectionPhyRequestPending_ = false;
        connectionPhyRequestInFlight_ = false;
      }
      if ((payload[1] == kBleLlCtrlEncReq) ||
          (payload[1] == kBleLlCtrlStartEncReq) ||
          (payload[1] == kBleLlCtrlPauseEncReq)) {
        ++encDebug_.encClearCount;
        encDebug_.encLastClearReason = 0x04U;  // RejectExtInd during enc procedure.
        clearEncryptionState();
      }
      return false;

    case kBleLlCtrlPhyUpdateInd:
      if (length != 5U) {
        emitBleTrace("LL_PHY_UPDATE_BADLEN");
        return false;
      }
      if (connectionRole_ != BleConnectionRole::kPeripheral) {
        return rejectCommandDisallowed();
      }
      {
        // LL_PHY_UPDATE_IND carries the selected PHY for each direction, not
        // a request bitmask. A zero field means "leave that direction
        // unchanged", so do not reject it as unsupported.
        const uint8_t centralToPeripheralRaw = payload[1];
        const uint8_t peripheralToCentralRaw = payload[2];
        const uint8_t centralToPeripheralPhy =
            supportedBlePhys(centralToPeripheralRaw);
        const uint8_t peripheralToCentralPhy =
            supportedBlePhys(peripheralToCentralRaw);
        // A zero PHY in both directions completes the LLCP procedure without
        // an instant. The instant bytes are reserved in that form.
        if (centralToPeripheralRaw == 0U && peripheralToCentralRaw == 0U) {
          connectionPhyUpdatePending_ = false;
          connectionPhyUpdateInstant_ = 0U;
          connectionPendingTxPhy_ = connectionCurrentTxPhy_;
          connectionPendingRxPhy_ = connectionCurrentRxPhy_;
          connectionPhyRequestPending_ = false;
          connectionPhyRequestInFlight_ = false;
          emitBleTrace("LL_PHY_UPDATE_NO_CHANGE");
          return false;
        }
        const uint16_t instant = readLe16(&payload[3]);
        const uint16_t instantDelta =
            static_cast<uint16_t>(instant - currentEventCounter);
        const bool instantInFuture =
            (instantDelta != 0U) && (instantDelta < 0x8000U);
        if (((centralToPeripheralRaw != 0U) &&
             (centralToPeripheralPhy == kBlePhyNone)) ||
            ((peripheralToCentralRaw != 0U) &&
             (peripheralToCentralPhy == kBlePhyNone))) {
          emitBleTrace("LL_PHY_UPDATE_BADPHY");
          return false;
        }
        // Peripheral view:
        // - central->peripheral PHY is our RX PHY
        // - peripheral->central PHY is our TX PHY
        const uint8_t nextRxPhy =
            (centralToPeripheralRaw != 0U)
                ? selectPreferredBlePhy(static_cast<uint8_t>(
                      centralToPeripheralPhy & connectionPreferredRxPhyMask_))
                : connectionCurrentRxPhy_;
        const uint8_t nextTxPhy =
            (peripheralToCentralRaw != 0U)
                ? selectPreferredBlePhy(static_cast<uint8_t>(
                      peripheralToCentralPhy & connectionPreferredTxPhyMask_))
                : connectionCurrentTxPhy_;
        if (nextTxPhy == kBlePhyNone || nextRxPhy == kBlePhyNone) {
          emitBleTrace("LL_PHY_UPDATE_BADPHY");
          return false;
        }
        if (!instantInFuture) {
          if (nextTxPhy == connectionCurrentTxPhy_ &&
              nextRxPhy == connectionCurrentRxPhy_) {
            connectionPhyRequestPending_ = false;
            connectionPhyRequestInFlight_ = false;
            armConnectionResyncWindow(kBleConnResyncExtraListenUs, 8U);
            emitBleTrace("LL_PHY_UPDATE_LATE_NOOP");
            return false;
          }
          emitBleTrace("LL_PHY_UPDATE_PAST");
          return false;
        }

        connectionPendingTxPhy_ = nextTxPhy;
        connectionPendingRxPhy_ = nextRxPhy;
        connectionPhyUpdateInstant_ = instant;
        connectionPhyUpdatePending_ = true;
        connectionPhyRequestPending_ = false;
        connectionPhyRequestInFlight_ = false;
        emitBleTrace("LL_PHY_UPDATE_PENDING");
      }
      return false;

    case kBleLlCtrlMinUsedChannelsInd:
      if (length != 3U) {
        return false;
      }
      return false;

    case kBleLlCtrlCteRsp:
      if (length != 1U) {
        return false;
      }
      return false;

    case kBleLlCtrlPeriodicSyncInd:
      if (length != 35U) {
        return false;
      }
      return false;

    case kBleLlCtrlClockAccuracyRsp:
      if (length != 2U) {
        return false;
      }
      return false;

    case kBleLlCtrlCisRsp:
      if (length != 9U) {
        return false;
      }
      return false;

    case kBleLlCtrlCisInd:
      if (length != 16U) {
        return false;
      }
      return false;

    case kBleLlCtrlCisTerminateInd:
      if (length != 4U) {
        return false;
      }
      return false;

    case kBleLlCtrlCsReq:
    case kBleLlCtrlCsRsp:
    case kBleLlCtrlCsCfg:
    case kBleLlCtrlCsProcReq:
    case kBleLlCtrlCsProcRsp:
    case kBleLlCtrlCsSecReq:
    case kBleLlCtrlCsSecRsp:
    case kBleLlCtrlCsStart:
    case kBleLlCtrlCsTerminate:
    case kBleLlCtrlCsAbort:
      // Channel Sounding LL control PDUs are consumed by the CS transport
      // layer. Do not reject them as unknown LLCP opcodes.
      return false;

    case kBleLlCtrlUnknownRsp:
      if (length != 2U) {
        return false;
      }
      return false;

    case kBleLlCtrlFeatureRsp:
      if (length != 9U) {
        return false;
      }
      return false;

    case kBleLlCtrlPhyRsp:
      if (length != 3U) {
        return false;
      }
      if (!connectionPhyRequestInFlight_) {
        return false;
      }
      {
        const uint8_t remoteTxPhys = supportedBlePhys(payload[1]);
        const uint8_t remoteRxPhys = supportedBlePhys(payload[2]);
        const uint8_t localSymmetricPhys = preferredSymmetricBlePhys();
        const uint8_t commonPhys = static_cast<uint8_t>(
            localSymmetricPhys & remoteTxPhys & remoteRxPhys);
        const uint8_t selectedPhy = selectPreferredBlePhy(commonPhys);
        if (selectedPhy == kBlePhyNone) {
          connectionPhyRequestPending_ = false;
          connectionPhyRequestInFlight_ = false;
          emitBleTrace("LL_PHY_RSP_NO_COMMON");
          return false;
        }
        const uint16_t instant =
            static_cast<uint16_t>(currentEventCounter + 6U);
        connectionPendingTxPhy_ = selectedPhy;
        connectionPendingRxPhy_ = selectedPhy;
        connectionPhyUpdateInstant_ = instant;
        connectionPhyUpdatePending_ = true;
        outPayload[0] = kBleLlCtrlPhyUpdateInd;
        outPayload[1] = selectedPhy;
        outPayload[2] = selectedPhy;
        writeLe16(&outPayload[3], instant);
        *outLength = 5U;
        emitBleTrace((selectedPhy == kBlePhyCoded) ? "LL_PHY_UPDATE_CODED_TX"
                     : ((selectedPhy == kBlePhy2M) ? "LL_PHY_UPDATE_2M_TX"
                                                   : "LL_PHY_UPDATE_1M_TX"));
        return true;
      }

    default:
      outPayload[0] = kBleLlCtrlUnknownRsp;
      outPayload[1] = opcode;
      *outLength = 2U;
      return true;
  }
}

void BleRadio::primeStartEncryptionResponseCiphertext() {
  connectionEncPrecomputedStartRspValid_ = false;
  connectionEncPrecomputedStartRspTxValid_ = false;
  if (!connectionEncSessionValid_) {
    return;
  }

  static constexpr uint8_t kStartEncRsp[] = {kBleLlCtrlStartEncRsp};
  uint8_t encryptedLength = 0U;
  if (connectionRole_ == BleConnectionRole::kCentral) {
    connectionEncPrecomputedStartRspValid_ =
        bleCcmEncryptPayload(connectionEncSessionKey_, connectionEncIv_,
                             connectionEncRxCounter_, connectionEncRxDirection_,
                             0x03U, kStartEncRsp, sizeof(kStartEncRsp),
                             connectionEncPrecomputedStartRsp_, &encryptedLength) &&
        (encryptedLength == sizeof(connectionEncPrecomputedStartRsp_));
    return;
  }

  connectionEncPrecomputedStartRspTxCounter_ = connectionEncTxCounter_;
  connectionEncPrecomputedStartRspTxValid_ =
      bleCcmEncryptPayload(connectionEncSessionKey_, connectionEncIv_,
                           connectionEncPrecomputedStartRspTxCounter_,
                           connectionEncTxDirection_, 0x03U, kStartEncRsp,
                           sizeof(kStartEncRsp), connectionEncPrecomputedStartRspTx_,
                           &encryptedLength) &&
      (encryptedLength == sizeof(connectionEncPrecomputedStartRspTx_));
}

bool BleRadio::ensureSecureConnectionsLocalKeypair() {
  if (smpSecureConnectionsLocalKeyReady_) {
    return true;
  }
  if (smpSecureConnectionsLocalKeyInProgress_) {
    return false;
  }

  smpSecureConnectionsLocalKeyInProgress_ = true;
  const uint32_t startUs = micros();
  Secp256r1Scalar priv{};
  Secp256r1Point pub{};
  if (!Secp256r1::generateKeyPair(&priv, &pub)) {
    memset(smpSecureConnectionsPrivateKey_, 0,
           sizeof(smpSecureConnectionsPrivateKey_));
    memset(smpSecureConnectionsPublicKey_, 0,
           sizeof(smpSecureConnectionsPublicKey_));
    smpSecureConnectionsLocalKeypairTimeUs_ = micros() - startUs;
    smpSecureConnectionsLocalKeyInProgress_ = false;
    return false;
  }
  smpSecureConnectionsLocalKeypairTimeUs_ = micros() - startUs;
  memcpy(smpSecureConnectionsPrivateKey_, priv.bytes,
         sizeof(smpSecureConnectionsPrivateKey_));
  // Secp256r1::encodeUncompressed() already emits little-endian coordinates,
  // which matches the SMP Pairing Public Key wire format used by standard BLE
  // stacks.
  Secp256r1::encodeUncompressed(pub, smpSecureConnectionsPublicKey_);
  smpSecureConnectionsLocalKeyReady_ = true;
  smpSecureConnectionsLocalKeyInProgress_ = false;
  return true;
}

// OOB (Out-of-Band) pairing support for LE Secure Connections
// Per Bluetooth Core Spec Vol 3 Part H §2.3.5.6.6

void BleRadio::setSecurityOobEnabled(bool enable) {
  const uint32_t irqState = bleEnterCritical();
  smpOobEnabled_ = enable;
  if (!enable) {
    smpOobDataLocalValid_ = false;
    smpOobDataRemoteValid_ = false;
    memset(&smpOobLocal_, 0, sizeof(smpOobLocal_));
    memset(&smpOobRemote_, 0, sizeof(smpOobRemote_));
  }
  bleExitCritical(irqState);
}

void BleRadio::retireSecurityOobData() {
  const uint32_t irqState = bleEnterCritical();
  invalidateSecureConnectionsKeypairAndOob(
      smpSecureConnectionsLocalKeyReady_,
      smpSecureConnectionsLocalKeyInProgress_,
      smpSecureConnectionsPrivateKey_, smpSecureConnectionsPublicKey_,
      smpOobDataLocalValid_, smpOobDataRemoteValid_, smpOobLocal_,
      smpOobRemote_);
  bleExitCritical(irqState);
}

bool BleRadio::setSecurityOobLocalData(const uint8_t oob_r[16],
                                           const uint8_t oob_c[16]) {
  if (oob_r == nullptr || oob_c == nullptr ||
      !ensureSecureConnectionsLocalKeypair()) {
    return false;
  }
  uint8_t expectedConfirm[16] = {0};
  if (!bleSecureConnectionsF4(&smpSecureConnectionsPublicKey_[1],
                              &smpSecureConnectionsPublicKey_[1], oob_r,
                              0x00U, expectedConfirm) ||
      memcmp(expectedConfirm, oob_c, sizeof(expectedConfirm)) != 0) {
    return false;
  }
  const uint32_t irqState = bleEnterCritical();
  memcpy(smpOobLocal_.r, oob_r, 16);
  memcpy(smpOobLocal_.c, oob_c, 16);
  smpOobDataLocalValid_ = true;
  smpOobEnabled_ = true;
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::setSecurityOobRemoteData(const uint8_t oob_r[16],
                                        const uint8_t oob_c[16]) {
  if (oob_r == nullptr || oob_c == nullptr) {
    return false;
  }
  const uint32_t irqState = bleEnterCritical();
  memcpy(smpOobRemote_.r, oob_r, 16);
  memcpy(smpOobRemote_.c, oob_c, 16);
  smpOobDataRemoteValid_ = true;
  smpOobEnabled_ = true;
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::generateSecurityOobData(uint8_t oob_r[16],
                                       uint8_t oob_c[16]) {
  if (oob_r == nullptr || oob_c == nullptr) {
    return false;
  }
  // Ensure local keypair exists (needed for F4 computation)
  if (!ensureSecureConnectionsLocalKeypair()) return false;

  // Generate random r from CRACEN RNG
  const uint32_t seed = micros() ^ connectionAccessAddress_ ^
                        static_cast<uint32_t>(connectionEventCounter_ << 16U);
  if (!fillBleSecurityRandomBytes(oob_r, 16, seed, kBleSecurityRandomSpinLimit)) {
    memset(oob_r, 0, 16U);
    memset(oob_c, 0, 16U);
    return false;
  }

  // LE SC OOB confirm value: c = F4(PKx, PKx, r, 0).
  if (!bleSecureConnectionsF4(&smpSecureConnectionsPublicKey_[1],
                              &smpSecureConnectionsPublicKey_[1],
                              oob_r,
                              0x00U,
                              oob_c)) {
    return false;
  }

  // Store locally
  memcpy(smpOobLocal_.r, oob_r, 16);
  memcpy(smpOobLocal_.c, oob_c, 16);
  smpOobDataLocalValid_ = true;
  smpOobEnabled_ = true;

  return true;
}

bool BleRadio::getSecurityOobData(uint8_t oob_r[16], uint8_t oob_c[16]) const {
  if (oob_r == nullptr || oob_c == nullptr || !smpOobDataLocalValid_) {
    return false;
  }
  memcpy(oob_r, smpOobLocal_.r, 16);
  memcpy(oob_c, smpOobLocal_.c, 16);
  return true;
}

bool BleRadio::getSecurityOobRemoteData(uint8_t oob_r[16], uint8_t oob_c[16]) const {
  if (oob_r == nullptr || oob_c == nullptr || !smpOobDataRemoteValid_) {
    return false;
  }
  memcpy(oob_r, smpOobRemote_.r, 16);
  memcpy(oob_c, smpOobRemote_.c, 16);
  return true;
}

static bool decodeSecureConnectionsPublicKeyToInternalLe(
    const uint8_t wirePublicKey[65], uint8_t outInternalLe[65],
    bool* outWireBigEndian, Secp256r1Point* outPoint) {
  if (wirePublicKey == nullptr || outInternalLe == nullptr ||
      outWireBigEndian == nullptr || outPoint == nullptr ||
      wirePublicKey[0] != 0x04U) {
    return false;
  }

  // SMP Pairing Public Key uses little-endian X and Y fields on the wire.
  // Try the standards-compliant LE decode first and only fall back to a BE
  // interpretation for interoperability with older non-compliant builds.
  if (Secp256r1::decodeUncompressed(wirePublicKey, outPoint)) {
    memcpy(outInternalLe, wirePublicKey, 65U);
    *outWireBigEndian = false;
    return true;
  }

  uint8_t beWireKeyAsLe[65] = {0};
  beWireKeyAsLe[0] = 0x04U;
  reverseCopy(&wirePublicKey[1], 32U, &beWireKeyAsLe[1]);
  reverseCopy(&wirePublicKey[33], 32U, &beWireKeyAsLe[33]);
  if (!Secp256r1::decodeUncompressed(beWireKeyAsLe, outPoint)) {
    return false;
  }

  memcpy(outInternalLe, beWireKeyAsLe, sizeof(beWireKeyAsLe));
  *outWireBigEndian = true;
  return true;
}

static void buildSecureConnectionsWirePublicKeyFromInternalLe(
    const uint8_t internalLe[65], bool wireBigEndian, uint8_t outWireKey[65]) {
  if (internalLe == nullptr || outWireKey == nullptr) {
    return;
  }

  outWireKey[0] = 0x04U;
  if (wireBigEndian) {
    reverseCopy(&internalLe[1], 32U, &outWireKey[1]);
    reverseCopy(&internalLe[33], 32U, &outWireKey[33]);
    return;
  }

  memcpy(&outWireKey[1], &internalLe[1], 64U);
}

bool BleRadio::computeSecureConnectionsDhKey() {
  if (!smpSecureConnectionsActive_ || !smpSecureConnectionsLocalKeyReady_ ||
      !smpSecureConnectionsPeerPublicKeyValid_) {
    return false;
  }
  if (smpSecureConnectionsDhKeyReady_) {
    return true;
  }

  Secp256r1Scalar priv{};
  Secp256r1Point peer{};
  Secp256r1Point shared{};
  const uint32_t startUs = micros();
  memcpy(priv.bytes, smpSecureConnectionsPrivateKey_, sizeof(priv.bytes));
  if (!Secp256r1::decodeUncompressed(smpSecureConnectionsPeerPublicKey_, &peer) ||
      !Secp256r1::scalarMultiply(priv, peer, &shared)) {
    return false;
  }

  smpSecureConnectionsDhKeyTimeUs_ = micros() - startUs;
  memcpy(smpSecureConnectionsDhKey_, shared.x, sizeof(shared.x));
  smpSecureConnectionsDhKeyReady_ = true;
  return true;
}

void BleRadio::buildLegacyTemporaryKey(uint8_t outTk[16]) const {
  if (outTk == nullptr) {
    return;
  }
  memset(outTk, 0, 16U);
  if (resolveLegacyPairingIoAction() != kSmpScIoActionNone &&
      smpPairingPasskeyValid_) {
    writeLe32(&outTk[0], smpPairingPasskeyValue_);
  }
}

bool BleRadio::computeSecureConnectionsLocalConfirm() {
  if (!smpSecureConnectionsActive_ || !smpSecureConnectionsLocalKeyReady_ ||
      !smpSecureConnectionsPeerPublicKeyValid_) {
    return false;
  }
  if (smpSecureConnectionsLocalConfirmReady_) {
    return true;
  }

  uint8_t z = 0U;
  if (resolveCurrentSecureConnectionsPairAlgorithm() == kSmpScPairAlgPasskey) {
    if (prepareSecureConnectionsPasskey() !=
        BleSmpPasskeyPreparationResult::Ready) {
      return false;
    }
    z = secureConnectionsPasskeyRoundZ();
  }

  if (!bleSecureConnectionsF4(&smpSecureConnectionsPublicKey_[1],
                              &smpSecureConnectionsPeerPublicKey_[1],
                              smpLocalRandom_, z, smpLocalConfirm_)) {
    return false;
  }

  smpSecureConnectionsLocalConfirmReady_ = true;
  return true;
}

namespace {

// SMP pairing is short-lived but latency-sensitive on some Android stacks.
// When an SC response is queued from deferred work, actively service the next
// event instead of only arming a later background wake. This avoids leaving
// DHKey/key-distribution responses parked until a tighter central times out.
constexpr uint32_t kBleDeferredSmpResponseSpinLimit = 120000UL;

constexpr uint8_t kSmpScInitIoActions[5][5] = {
    {kSmpScIoActionNone, kSmpScIoActionNone, kSmpScIoActionInput,
     kSmpScIoActionNone, kSmpScIoActionInput},
    {kSmpScIoActionNone, kSmpScIoActionNumericComparison,
     kSmpScIoActionInput, kSmpScIoActionNone,
     kSmpScIoActionNumericComparison},
    {kSmpScIoActionDisplay, kSmpScIoActionDisplay,
     kSmpScIoActionInput, kSmpScIoActionNone,
     kSmpScIoActionDisplay},
    {kSmpScIoActionNone, kSmpScIoActionNone, kSmpScIoActionNone,
     kSmpScIoActionNone, kSmpScIoActionNone},
    {kSmpScIoActionDisplay, kSmpScIoActionNumericComparison,
     kSmpScIoActionInput, kSmpScIoActionNone,
     kSmpScIoActionNumericComparison},
};

constexpr uint8_t kSmpScRespIoActions[5][5] = {
    {kSmpScIoActionNone, kSmpScIoActionNone, kSmpScIoActionDisplay,
     kSmpScIoActionNone, kSmpScIoActionDisplay},
    {kSmpScIoActionNone, kSmpScIoActionNumericComparison,
     kSmpScIoActionDisplay, kSmpScIoActionNone,
     kSmpScIoActionNumericComparison},
    {kSmpScIoActionInput, kSmpScIoActionInput, kSmpScIoActionInput,
     kSmpScIoActionNone, kSmpScIoActionInput},
    {kSmpScIoActionNone, kSmpScIoActionNone, kSmpScIoActionNone,
     kSmpScIoActionNone, kSmpScIoActionNone},
    {kSmpScIoActionInput, kSmpScIoActionNumericComparison,
     kSmpScIoActionDisplay, kSmpScIoActionNone,
     kSmpScIoActionNumericComparison},
};

constexpr uint8_t kSmpLegacyIoActionRoleDependent = 0x05U;
constexpr uint8_t kSmpLegacyIoActions[5][5] = {
    {kSmpScIoActionNone, kSmpScIoActionNone, kSmpScIoActionInput,
     kSmpScIoActionNone, kSmpScIoActionInput},
    {kSmpScIoActionNone, kSmpScIoActionNone, kSmpScIoActionInput,
     kSmpScIoActionNone, kSmpScIoActionInput},
    {kSmpScIoActionDisplay, kSmpScIoActionDisplay, kSmpScIoActionInput,
     kSmpScIoActionNone, kSmpScIoActionDisplay},
    {kSmpScIoActionNone, kSmpScIoActionNone, kSmpScIoActionNone,
     kSmpScIoActionNone, kSmpScIoActionNone},
    {kSmpScIoActionDisplay, kSmpScIoActionDisplay, kSmpScIoActionInput,
     kSmpScIoActionNone, kSmpLegacyIoActionRoleDependent},
};

uint8_t resolveSecureConnectionsIoAction(bool localInitiator,
                                         uint8_t initiatorIoCap,
                                         uint8_t responderIoCap,
                                         uint8_t initiatorAuthReq,
                                         uint8_t responderAuthReq,
                                         uint8_t initiatorOobFlag,
                                         uint8_t responderOobFlag) {
  if (initiatorOobFlag != 0U || responderOobFlag != 0U) {
    return kSmpScIoActionOob;
  }
  if ((initiatorAuthReq & kSmpAuthReqMitmMask) == 0U &&
      (responderAuthReq & kSmpAuthReqMitmMask) == 0U) {
    return kSmpScIoActionNone;
  }
  if (initiatorIoCap > kSmpIoCapKeyboardDisplay ||
      responderIoCap > kSmpIoCapKeyboardDisplay) {
    return kSmpScIoActionNone;
  }
  if (localInitiator) {
    return kSmpScInitIoActions[responderIoCap][initiatorIoCap];
  }
  return kSmpScRespIoActions[responderIoCap][initiatorIoCap];
}

uint8_t resolveSecureConnectionsPairAlgorithm(bool localInitiator,
                                              const uint8_t pairingReq[7],
                                              const uint8_t pairingRsp[7]) {
  if (pairingReq == nullptr || pairingRsp == nullptr) {
    return kSmpScPairAlgJustWorks;
  }

  const uint8_t ioAction = resolveSecureConnectionsIoAction(
      localInitiator, pairingReq[1], pairingRsp[1], pairingReq[3],
      pairingRsp[3], pairingReq[2], pairingRsp[2]);
  switch (ioAction) {
    case kSmpScIoActionOob:
      return kSmpScPairAlgOob;
    case kSmpScIoActionInput:
    case kSmpScIoActionDisplay:
      return kSmpScPairAlgPasskey;
    case kSmpScIoActionNumericComparison:
      return kSmpScPairAlgNumericComparison;
    case kSmpScIoActionNone:
    default:
      return kSmpScPairAlgJustWorks;
  }
}

void formatPairingPasskey(uint32_t passkey, uint8_t outPasskey[6]) {
  if (outPasskey == nullptr) {
    return;
  }
  if (passkey > 999999UL) {
    passkey %= 1000000UL;
  }
  outPasskey[0] = static_cast<uint8_t>('0' + ((passkey / 100000UL) % 10UL));
  outPasskey[1] = static_cast<uint8_t>('0' + ((passkey / 10000UL) % 10UL));
  outPasskey[2] = static_cast<uint8_t>('0' + ((passkey / 1000UL) % 10UL));
  outPasskey[3] = static_cast<uint8_t>('0' + ((passkey / 100UL) % 10UL));
  outPasskey[4] = static_cast<uint8_t>('0' + ((passkey / 10UL) % 10UL));
  outPasskey[5] = static_cast<uint8_t>('0' + (passkey % 10UL));
}

bool parseFixedPasskeyString(const char* pin, uint32_t* outPasskey) {
  if (pin == nullptr || outPasskey == nullptr || strlen(pin) != 6U) {
    return false;
  }

  uint32_t value = 0UL;
  for (size_t i = 0U; i < 6U; ++i) {
    if (pin[i] < '0' || pin[i] > '9') {
      return false;
    }
    value = (value * 10UL) + static_cast<uint32_t>(pin[i] - '0');
  }

  *outPasskey = value;
  return true;
}

}  // namespace

void BleRadio::setSecurityIoCapabilities(uint8_t ioCaps) {
  if (ioCaps > kSmpIoCapKeyboardDisplay) {
    ioCaps = kSmpIoCapNoInputNoOutput;
  }
  const uint32_t irqState = bleEnterCritical();
  smpLocalIoCapabilities_ = ioCaps;
  bleExitCritical(irqState);
  // Bluefruit.begin() sets this even for advertising-only applications.
  // Generate the key on SC negotiation or explicit OOB preparation instead.
}

void BleRadio::setSecurityFixedPasskey(const char* pin) {
  uint32_t passkey = 0UL;
  const bool valid = parseFixedPasskeyString(pin, &passkey);
  const uint32_t irqState = bleEnterCritical();
  smpFixedPasskeyValid_ = valid;
  smpFixedPasskeyValue_ = valid ? passkey : 0UL;
  bleExitCritical(irqState);
}

bool BleRadio::setSecurityPolicy(const BleSecurityPolicy& policy) {
  if (policy.minEncryptionKeySize < 7U ||
      policy.maxEncryptionKeySize > 16U ||
      policy.minEncryptionKeySize > policy.maxEncryptionKeySize ||
      (policy.secureConnectionsRequired &&
       !policy.secureConnectionsEnabled)) {
    return false;
  }
  const uint32_t irqState = bleEnterCritical();
  securityPolicy_ = policy;
  bleExitCritical(irqState);
  return true;
}

BleSecurityPolicy BleRadio::getSecurityPolicy() const {
  BleSecurityPolicy policy{};
  getSecurityPolicy(&policy);
  return policy;
}

void BleRadio::getSecurityPolicy(BleSecurityPolicy* outPolicy) const {
  if (outPolicy == nullptr) {
    return;
  }
  const uint32_t irqState = bleEnterCritical();
  *outPolicy = securityPolicy_;
  bleExitCritical(irqState);
}

void BleRadio::latchSecurityPolicyForPairing() {
  smpSecurityPolicy_ = securityPolicy_;
  smpPairingLocalIoCapabilities_ = smpLocalIoCapabilities_;
  smpPairingPasskeyValid_ = smpFixedPasskeyValid_;
  smpPairingPasskeyValue_ =
      smpFixedPasskeyValid_ ? smpFixedPasskeyValue_ : 0UL;
  smpSecurityPolicyLatched_ = true;
}

uint8_t BleRadio::buildLocalPairingAuthReq(bool bondingAllowed) const {
  const BleSecurityPolicy& policy =
      smpSecurityPolicyLatched_ ? smpSecurityPolicy_ : securityPolicy_;
  uint8_t authReq =
      (bondingAllowed && policy.bondingEnabled) ? 0x01U : 0x00U;
  if (policy.mitmRequired ||
      (smpOobEnabled_ &&
       (smpOobDataLocalValid_ || smpOobDataRemoteValid_))) {
    authReq |= kSmpAuthReqMitmMask;
  }
  if (policy.secureConnectionsEnabled) {
    authReq |= kSmpAuthReqSecureConnectionsMask;
  }
  return authReq;
}

bool BleRadio::bondMeetsSecurityPolicy(
    const BleBondRecord& record, const BleSecurityPolicy& policy) const {
  const bool authenticated =
      (record.reserved[0] & kBleBondRecordFlagAuthenticated) != 0U;
  const bool secureConnections =
      (record.reserved[0] & kBleBondRecordFlagSecureConnections) != 0U;
  const uint8_t keySize =
      (record.keySize >= 7U && record.keySize <= 16U) ? record.keySize : 16U;
  return (!policy.mitmRequired || authenticated) &&
         (!policy.secureConnectionsRequired || secureConnections) &&
         keySize >= policy.minEncryptionKeySize &&
         keySize <= policy.maxEncryptionKeySize;
}

uint8_t BleRadio::resolveLegacyPairingIoAction() const {
  if (((smpPairingReq_[3] | smpPairingRsp_[3]) &
       kSmpAuthReqMitmMask) == 0U) {
    return kSmpScIoActionNone;
  }
  const uint8_t localIo = smpLocalInitiator_ ? smpPairingReq_[1]
                                             : smpPairingRsp_[1];
  const uint8_t remoteIo = smpLocalInitiator_ ? smpPairingRsp_[1]
                                              : smpPairingReq_[1];
  if (localIo > kSmpIoCapKeyboardDisplay ||
      remoteIo > kSmpIoCapKeyboardDisplay) {
    return kSmpScIoActionNone;
  }
  const uint8_t action = kSmpLegacyIoActions[remoteIo][localIo];
  if (action != kSmpLegacyIoActionRoleDependent) {
    return action;
  }
  return connectionRole_ == BleConnectionRole::kCentral
             ? kSmpScIoActionDisplay
             : kSmpScIoActionInput;
}

bool BleRadio::requestPairingPasskeyInput() {
  if (!connected_ || smpPairingState_ == kSmpPairingStateIdle ||
      smpTimedOutForConnection_) {
    return false;
  }
  if (smpPendingUserAction_ == BleSmpUserAction::PasskeyInput &&
      smpPendingUserPasskeyRequestId_ != 0UL) {
    return true;
  }
  ++smpNextUserPasskeyRequestId_;
  if (smpNextUserPasskeyRequestId_ == 0UL) {
    smpNextUserPasskeyRequestId_ = 1UL;
  }
  smpPendingUserPasskeyRequestId_ = smpNextUserPasskeyRequestId_;
  smpPendingUserAction_ = BleSmpUserAction::PasskeyInput;
  smpPendingUserPasskey_ = true;
  smpPendingUserPasskeyMatchRequest_ = false;
  smpPendingUserPasskeyCallbackDispatched_ = false;
  smpPendingUserPasskeyValue_ = 0UL;
  emitBleTrace("SMP_PASSKEY_INPUT_PENDING");
  return true;
}

BleSmpPasskeyPreparationResult BleRadio::preparePairingPasskeyForAction(
    uint8_t ioAction) {
  if (ioAction == kSmpScIoActionNone) {
    return BleSmpPasskeyPreparationResult::Ready;
  }
  if (ioAction != kSmpScIoActionInput &&
      ioAction != kSmpScIoActionDisplay) {
    return BleSmpPasskeyPreparationResult::Fatal;
  }
  if (!smpPairingPasskeyValid_) {
    if (ioAction == kSmpScIoActionInput) {
      return requestPairingPasskeyInput()
                 ? BleSmpPasskeyPreparationResult::Pending
                 : BleSmpPasskeyPreparationResult::Fatal;
    }

    uint8_t rawPasskey[4] = {0};
    const uint32_t seed =
        micros() ^ connectionAccessAddress_ ^
        static_cast<uint32_t>(connectionEventCounter_ << 16U) ^
        0x50415353UL;
    if (!fillBleSecurityRandomBytes(rawPasskey, sizeof(rawPasskey), seed,
                                    kBleSecurityRandomSpinLimit)) {
      ++encDebug_.encRandomFallbackCount;
      return BleSmpPasskeyPreparationResult::Fatal;
    }
    ++encDebug_.encRandomHardwareCount;
    smpPairingPasskeyValue_ = readLe32(rawPasskey) % 1000000UL;
    smpPairingPasskeyValid_ = true;
  }

  if (ioAction == kSmpScIoActionDisplay &&
      smpPendingUserAction_ == BleSmpUserAction::None) {
    smpPendingUserPasskeyValue_ = smpPairingPasskeyValue_;
    smpPendingUserPasskeyMatchRequest_ = false;
    smpPendingUserPasskey_ = true;
    smpPendingUserAction_ = BleSmpUserAction::Display;
    emitBleTrace("SMP_PASSKEY_DISPLAY_PENDING");
  }
  return BleSmpPasskeyPreparationResult::Ready;
}

bool BleRadio::prepareLocalPairingRandom() {
  // Na/Nb are fresh per-pairing nonces. LE SC OOB r is separate input to F6.
  const uint32_t seed =
      micros() ^ connectionAccessAddress_ ^
      static_cast<uint32_t>(connectionEventCounter_ << 16U);
  if (smpPrefetchedLocalRandomValid_) {
    memcpy(smpLocalRandom_, smpPrefetchedLocalRandom_, sizeof(smpLocalRandom_));
    smpPrefetchedLocalRandomValid_ = false;
    ++encDebug_.encRandomPrefetchUseCount;
    return true;
  }
  if (fillBleSecurityRandomBytes(smpLocalRandom_, sizeof(smpLocalRandom_),
                                 seed, kBleSecurityRandomSpinLimit)) {
    ++encDebug_.encRandomHardwareCount;
    return true;
  }
  ++encDebug_.encRandomFallbackCount;
  memset(smpLocalRandom_, 0, sizeof(smpLocalRandom_));
  emitBleTrace("SMP_RANDOM_UNAVAILABLE");
  return false;
}

bool BleRadio::prepareLocalLegacyEncryptionKey() {
  if (smpLocalLtkValid_) {
    reduceSmpEncryptionKey(smpLocalLtk_);
    return true;
  }
  if (!connected_) {
    return false;
  }

  uint8_t material[26] = {};
  if (smpPrefetchedLegacyKeyValid_) {
    memcpy(material, smpPrefetchedLegacyKeyMaterial_, sizeof(material));
    memset(smpPrefetchedLegacyKeyMaterial_, 0,
           sizeof(smpPrefetchedLegacyKeyMaterial_));
    smpPrefetchedLegacyKeyValid_ = false;
  } else {
    if (bleRunningInIsr()) {
      return false;
    }
    const uint32_t seed =
        micros() ^ connectionAccessAddress_ ^
        static_cast<uint32_t>(connectionEventCounter_ << 16U) ^
        0x4C544B31UL;
    if (!fillBleSecurityRandomBytes(material, sizeof(material), seed,
                                    kBleSecurityRandomSpinLimit)) {
      return false;
    }
  }

  if (isAllBytes(material, 16U, 0x00U)) {
    memset(material, 0, sizeof(material));
    return false;
  }
  memcpy(smpLocalLtk_, material, sizeof(smpLocalLtk_));
  memcpy(smpLocalRand_, &material[16], sizeof(smpLocalRand_));
  smpLocalEdiv_ = readLe16(&material[24]);
  memset(material, 0, sizeof(material));
  reduceSmpEncryptionKey(smpLocalLtk_);
  smpLocalLtkValid_ = true;
  emitBleTrace("SMP_LOCAL_LEGACY_KEY_READY");
  return true;
}

bool BleRadio::localIdentityDistributionAvailable() const {
  if (!localIdentityAddressValid_) {
    return false;
  }
  uint8_t identityRoot[16] = {};
  Ficr::identityRoot(identityRoot);
  return !isAllBytes(identityRoot, sizeof(identityRoot), 0x00U) &&
         !isAllBytes(identityRoot, sizeof(identityRoot), 0xFFU);
}

void BleRadio::restartSmpTimer() {
  if (!connected_ || smpTimedOutForConnection_) {
    return;
  }
  smpTimerStartedMs_ = millis();
  smpTimerActive_ = true;
}

void BleRadio::stopSmpTimer() {
  smpTimerActive_ = false;
  smpTimerStartedMs_ = 0UL;
}

void BleRadio::currentSmpPeerKey(uint8_t address[6], bool* random) const {
  if (address == nullptr || random == nullptr) {
    return;
  }
  if (bondIdentityMatchedForConnection_ && bondRecordValid_ &&
      bondRecord_.peerIrkValid != 0U) {
    memcpy(address, bondRecord_.peerIdentityAddress, 6U);
    *random = bondRecord_.peerIdentityAddressRandom != 0U;
    return;
  }
  memcpy(address, connectionPeerAddress_, 6U);
  *random = connectionPeerAddressRandom_;
}

void BleRadio::decaySmpBackoff(uint32_t nowMs) {
  if (!smpBackoffPeerValid_) {
    return;
  }
  const uint32_t elapsed = nowMs - smpBackoffLastFailureMs_;
  if (elapsed < kSmpBackoffDecayMs) {
    return;
  }
  const uint32_t steps = elapsed / kSmpBackoffDecayMs;
  if (steps > static_cast<uint32_t>(smpBackoffLevel_)) {
    smpBackoffPeerValid_ = false;
    smpBackoffPeerAddressRandom_ = false;
    memset(smpBackoffPeerAddress_, 0, sizeof(smpBackoffPeerAddress_));
    smpBackoffLevel_ = 0U;
    smpBackoffStartedMs_ = 0UL;
    smpBackoffDurationMs_ = 0UL;
    smpBackoffLastFailureMs_ = 0UL;
    return;
  }
  smpBackoffLevel_ = static_cast<uint8_t>(
      smpBackoffLevel_ - static_cast<uint8_t>(steps));
  smpBackoffLastFailureMs_ += steps * kSmpBackoffDecayMs;
}

bool BleRadio::smpBackoffActiveForCurrentPeer() {
  const uint32_t nowMs = millis();
  decaySmpBackoff(nowMs);
  if (!smpBackoffPeerValid_) {
    return false;
  }
  uint8_t peerAddress[6] = {};
  bool peerRandom = false;
  currentSmpPeerKey(peerAddress, &peerRandom);
  if (peerRandom != smpBackoffPeerAddressRandom_ ||
      memcmp(peerAddress, smpBackoffPeerAddress_, sizeof(peerAddress)) != 0) {
    return false;
  }
  return (nowMs - smpBackoffStartedMs_) < smpBackoffDurationMs_;
}

void BleRadio::noteSmpPairingFailure(uint8_t reason) {
  if (reason == kSmpReasonRepeatedAttempts) {
    return;
  }
  const uint32_t nowMs = millis();
  decaySmpBackoff(nowMs);
  uint8_t peerAddress[6] = {};
  bool peerRandom = false;
  currentSmpPeerKey(peerAddress, &peerRandom);
  const bool samePeer =
      smpBackoffPeerValid_ &&
      peerRandom == smpBackoffPeerAddressRandom_ &&
      memcmp(peerAddress, smpBackoffPeerAddress_, sizeof(peerAddress)) == 0;
  if (!samePeer) {
    smpBackoffLevel_ = 0U;
  } else if (smpBackoffLevel_ < kSmpBackoffMaximumLevel) {
    ++smpBackoffLevel_;
  }
  uint32_t duration =
      kSmpBackoffMinimumMs << static_cast<uint32_t>(smpBackoffLevel_);
  if (duration > kSmpBackoffMaximumMs) {
    duration = kSmpBackoffMaximumMs;
  }
  smpBackoffPeerValid_ = true;
  smpBackoffPeerAddressRandom_ = peerRandom;
  memcpy(smpBackoffPeerAddress_, peerAddress, sizeof(smpBackoffPeerAddress_));
  smpBackoffStartedMs_ = nowMs;
  smpBackoffDurationMs_ = duration;
  smpBackoffLastFailureMs_ = nowMs;
}

void BleRadio::clearSmpBackoffForCurrentPeer() {
  if (!smpBackoffPeerValid_) {
    return;
  }
  uint8_t peerAddress[6] = {};
  bool peerRandom = false;
  currentSmpPeerKey(peerAddress, &peerRandom);
  if (peerRandom != smpBackoffPeerAddressRandom_ ||
      memcmp(peerAddress, smpBackoffPeerAddress_, sizeof(peerAddress)) != 0) {
    return;
  }
  smpBackoffPeerValid_ = false;
  smpBackoffPeerAddressRandom_ = false;
  memset(smpBackoffPeerAddress_, 0, sizeof(smpBackoffPeerAddress_));
  smpBackoffLevel_ = 0U;
  smpBackoffStartedMs_ = 0UL;
  smpBackoffDurationMs_ = 0UL;
  smpBackoffLastFailureMs_ = 0UL;
}

void BleRadio::noteLastSmpTxAcknowledged() {
  if (connectionLastTxPlainLlid_ != kBlePduDataStartOrComplete ||
      connectionLastTxPlainLength_ < (kBleL2capHeaderLen + 1U) ||
      readLe16(&connectionLastTxPlainPayload_[2]) != kBleL2capCidSmp) {
    return;
  }
  const uint8_t code = connectionLastTxPlainPayload_[kBleL2capHeaderLen];
  if (smpDistributeLocalEncKey_ && smpLocalEncInfoSent_ &&
      !smpLocalEncInfoAcked_ && code == kSmpCodeEncryptionInformation) {
    smpLocalEncInfoAcked_ = true;
    emitBleTrace("SMP_LOCAL_ENC_INFO_ACK");
  }
  if (smpDistributeLocalEncKey_ && smpLocalMasterIdSent_ &&
      !smpLocalMasterIdAcked_ && code == kSmpCodeMasterIdentification) {
    smpLocalMasterIdAcked_ = true;
    emitBleTrace("SMP_LOCAL_MASTER_ID_ACK");
  }
  if (smpDistributeLocalIdKey_ && smpLocalIdInfoSent_ &&
      !smpLocalIdInfoAcked_ && code == kSmpCodeIdentityInformation) {
    smpLocalIdInfoAcked_ = true;
    emitBleTrace("SMP_LOCAL_ID_INFO_ACK");
  }
  if (smpDistributeLocalIdKey_ && smpLocalIdAddressSent_ &&
      !smpLocalIdAddressAcked_ &&
      code == kSmpCodeIdentityAddressInformation) {
    smpLocalIdAddressAcked_ = true;
    emitBleTrace("SMP_LOCAL_ID_ADDR_ACK");
  }
  if (smpDistributeLocalSignKey_ && smpLocalSignInfoSent_ &&
      !smpLocalSignInfoAcked_ && code == kSmpCodeSigningInformation) {
    smpLocalSignInfoAcked_ = true;
    emitBleTrace("SMP_LOCAL_SIGN_INFO_ACK");
  }
}

void BleRadio::serviceSmpTimer() {
  if (!smpTimerActive_ || smpTimedOutForConnection_ || !connected_ ||
      (millis() - smpTimerStartedMs_) < kSmpTimeoutMs) {
    return;
  }
  stopSmpTimer();
  smpTimedOutForConnection_ = true;
  smpPairingFailureReasonPending_ = true;
  smpLastPairingFailureReason_ = kSmpReasonUnspecified;
  noteSmpPairingFailure(kSmpReasonUnspecified);
  const bool pendingSmpStart =
      connectionPendingTxValid_ &&
      connectionPendingTxLlid_ == kBlePduDataStartOrComplete &&
      connectionPendingTxLength_ >= kBleL2capHeaderLen &&
      readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidSmp;
  const bool pendingSmpContinuation =
      connectionPendingTxValid_ && connectionPendingTxSmpFragment_ &&
      connectionPendingTxLlid_ == kBlePduDataContinuation;
  if (pendingSmpStart || pendingSmpContinuation) {
    connectionPendingTxLlid_ = kBlePduDataContinuation;
    connectionPendingTxLength_ = 0U;
    connectionPendingTxValid_ = false;
    connectionPendingTxSmpFragment_ = false;
    memset(connectionPendingTxPayload_, 0, sizeof(connectionPendingTxPayload_));
  }
  connectionSmpSecurityRequestPending_ = false;
  invalidateSecureConnectionsKeypairAndOob(
      smpSecureConnectionsLocalKeyReady_,
      smpSecureConnectionsLocalKeyInProgress_,
      smpSecureConnectionsPrivateKey_, smpSecureConnectionsPublicKey_,
      smpOobDataLocalValid_, smpOobDataRemoteValid_, smpOobLocal_,
      smpOobRemote_);
  if (connectionPendingL2capTxFragmentActive_ &&
      connectionPendingL2capTxFragmentLength_ >= kBleL2capHeaderLen &&
      readLe16(&connectionPendingL2capTxFragmentPayload_[2]) ==
          kBleL2capCidSmp) {
    clearPendingL2capTxFragment();
  }
  clearSmpPairingState();
  emitBleTrace("SMP_TIMEOUT");
}

bool BleRadio::requestPairing() {
  serviceSmpTimer();
  if (connected_ && (!smpPrefetchedLocalRandomValid_ ||
       !connectionPrefetchedEncMaterialValid_ ||
       !smpPrefetchedLegacyKeyValid_)) {
    prefetchConnectionSecurityMaterial(kBleSecurityRandomSpinLimit);
  }
  const BleSecurityPolicy configuredPolicy = getSecurityPolicy();
  const uint32_t irqState = bleEnterCritical();
  if (!connected_ || connectionEncSessionValid_ || connectionEncRxEnabled_ ||
      connectionEncTxEnabled_ || connectionEncKeyDerivationPending_ ||
      connectionEncStartReqPending_ || connectionEncStartReqTxPending_ ||
      connectionEncAwaitingStartRsp_ ||
      smpPairingState_ != kSmpPairingStateIdle ||
      smpPendingUserPasskey_ || smpDeferredPairingFailed_) {
    bleExitCritical(irqState);
    return false;
  }
  if (smpTimedOutForConnection_) {
    bleExitCritical(irqState);
    return false;
  }
  if (smpBackoffActiveForCurrentPeer()) {
    smpPairingFailureReasonPending_ = true;
    smpLastPairingFailureReason_ = kSmpReasonRepeatedAttempts;
    bleExitCritical(irqState);
    return false;
  }
  const bool storedBondMeetsLocalPolicy =
      bondKeyPrimedForConnection_ && smpStkValid_ &&
      bondMeetsSecurityPolicy(bondRecord_, configuredPolicy);
  if (!storedBondMeetsLocalPolicy && !smpPrefetchedLocalRandomValid_) {
    emitBleTrace("SMP_RANDOM_UNAVAILABLE");
    bleExitCritical(irqState);
    return false;
  }

  // A completed or failed SC attempt retires its keypair and therefore its OOB
  // payload. Keep OOB mode fail-closed until the sketch generates and/or sets
  // fresh data, or explicitly disables OOB mode.
  if (smpOobEnabled_ && !smpOobDataLocalValid_ &&
      !smpOobDataRemoteValid_ && !storedBondMeetsLocalPolicy) {
    bleExitCritical(irqState);
    return false;
  }

  smpPairingFailureReasonPending_ = false;
  smpLastPairingFailureReason_ = 0U;

  if (connectionRole_ == BleConnectionRole::kPeripheral) {
    latchSecurityPolicyForPairing();
    if (storedBondMeetsLocalPolicy) {
      connectionSmpSecurityRequestPending_ = true;
      emitBleTrace("SMP_PAIR_REQ_USE_BOND");
      bleExitCritical(irqState);
      return true;
    }
    connectionSmpSecurityRequestPending_ = true;
    if (bondKeyPrimedForConnection_) {
      emitBleTrace("SMP_PAIR_REQ_BOND_UPGRADE");
    }
    bleExitCritical(irqState);
    return true;
  }

  if (connectionRole_ != BleConnectionRole::kCentral) {
    bleExitCritical(irqState);
    return false;
  }
  if (storedBondMeetsLocalPolicy) {
    connectionBondedEncryptionRequested_ = true;
    emitBleTrace("SMP_PAIR_REQ_USE_BOND");
    bleExitCritical(irqState);
    return true;
  }
  if (bondKeyPrimedForConnection_) {
    emitBleTrace("SMP_PAIR_REQ_BOND_UPGRADE");
  }

  clearSmpPairingState();
  latchSecurityPolicyForPairing();
  if (smpSecurityPolicy_.bondingEnabled &&
      !smpSecurityPolicy_.secureConnectionsRequired) {
    (void)prepareLocalLegacyEncryptionKey();
  }

  uint8_t pairingReq[kSmpPairingRequestLen] = {0};
  pairingReq[0] = kSmpCodePairingRequest;
  pairingReq[1] = smpPairingLocalIoCapabilities_;
  pairingReq[2] =
      (smpOobEnabled_ && smpOobDataRemoteValid_) ? 0x01U : 0x00U;
  pairingReq[3] = buildLocalPairingAuthReq(true);
  pairingReq[4] = smpSecurityPolicy_.maxEncryptionKeySize;
  if ((pairingReq[3] & kSmpAuthReqBondingMask) != 0U) {
    pairingReq[5] = kSmpKeyDistSignKeyMask;
    if (smpLocalLtkValid_) {
      pairingReq[5] |= kSmpKeyDistEncKeyMask;
    }
    if (localIdentityDistributionAvailable()) {
      pairingReq[5] |= kSmpKeyDistIdKeyMask;
    }
    pairingReq[6] = static_cast<uint8_t>(
        kSmpKeyDistEncKeyMask | kSmpKeyDistIdKeyMask |
        kSmpKeyDistSignKeyMask);
  }

  if (!queuePendingSmpL2capResponse(pairingReq, kSmpPairingRequestLen)) {
    bleExitCritical(irqState);
    return false;
  }

  memcpy(smpPairingReq_, pairingReq, sizeof(smpPairingReq_));
  smpLocalInitiator_ = true;
  smpBondingRequested_ =
      (pairingReq[3] & kSmpAuthReqBondingMask) != 0U;
  smpSecureConnectionsActive_ =
      (pairingReq[3] & kSmpAuthReqSecureConnectionsMask) != 0U;
  smpKeySize_ = pairingReq[4];
  smpPairingState_ = kSmpPairingStateReqSent;
  emitBleTrace("SMP_PAIRING_REQUEST_TX");
  bleExitCritical(irqState);
  return true;
}

uint8_t BleRadio::resolveCurrentSecureConnectionsIoAction() const {
  return resolveSecureConnectionsIoAction(
      smpLocalInitiator_, smpPairingReq_[1], smpPairingRsp_[1],
      smpPairingReq_[3], smpPairingRsp_[3], smpPairingReq_[2],
      smpPairingRsp_[2]);
}

uint8_t BleRadio::resolveCurrentSecureConnectionsPairAlgorithm() const {
  return resolveSecureConnectionsPairAlgorithm(smpLocalInitiator_,
                                               smpPairingReq_,
                                               smpPairingRsp_);
}

bool BleRadio::secureConnectionsPairAlgorithmSupported(uint8_t pairAlg) const {
  if (pairAlg == kSmpScPairAlgJustWorks ||
      pairAlg == kSmpScPairAlgNumericComparison) {
    return true;
  }
  if (pairAlg == kSmpScPairAlgPasskey) {
    const uint8_t ioAction = resolveCurrentSecureConnectionsIoAction();
    return (ioAction == kSmpScIoActionDisplay) ||
           (ioAction == kSmpScIoActionInput);
  }
  if (pairAlg == kSmpScPairAlgOob) {
    const bool localHasPeerData = localHasPeerSecureConnectionsOobData(
        smpLocalInitiator_, smpPairingReq_, smpPairingRsp_);
    const bool peerHasLocalData = peerHasLocalSecureConnectionsOobData(
        smpLocalInitiator_, smpPairingReq_, smpPairingRsp_);
    return smpOobEnabled_ && (localHasPeerData || peerHasLocalData) &&
           (!localHasPeerData || smpOobDataRemoteValid_) &&
           (!peerHasLocalData || smpOobDataLocalValid_);
  }
  return false;
}

bool BleRadio::currentPairingIsAuthenticated() const {
  if (smpSecureConnectionsActive_) {
    const uint8_t pairAlg = resolveCurrentSecureConnectionsPairAlgorithm();
    if (pairAlg == kSmpScPairAlgOob) {
      // The API does not describe whether a one-way r was transported over a
      // confidential OOB channel. Conservatively expose mutual authentication
      // only when both peers advertised possession of the other's commitment.
      return localHasPeerSecureConnectionsOobData(
                 smpLocalInitiator_, smpPairingReq_, smpPairingRsp_) &&
             peerHasLocalSecureConnectionsOobData(
                 smpLocalInitiator_, smpPairingReq_, smpPairingRsp_);
    }
    return pairAlg == kSmpScPairAlgPasskey ||
           pairAlg == kSmpScPairAlgNumericComparison;
  }

  return (((smpPairingReq_[3] | smpPairingRsp_[3]) &
           kSmpAuthReqMitmMask) != 0U) &&
         resolveLegacyPairingIoAction() != kSmpScIoActionNone &&
         smpPairingPasskeyValid_;
}

BleSmpPasskeyPreparationResult BleRadio::prepareSecureConnectionsPasskey() {
  if (!smpSecureConnectionsActive_) {
    return BleSmpPasskeyPreparationResult::Fatal;
  }
  if (resolveCurrentSecureConnectionsPairAlgorithm() != kSmpScPairAlgPasskey) {
    return BleSmpPasskeyPreparationResult::Ready;
  }
  if (smpSecureConnectionsPasskeyValid_) {
    return BleSmpPasskeyPreparationResult::Ready;
  }

  const uint8_t ioAction = resolveCurrentSecureConnectionsIoAction();
  const BleSmpPasskeyPreparationResult result =
      preparePairingPasskeyForAction(ioAction);
  if (result == BleSmpPasskeyPreparationResult::Pending &&
      smpPendingUserAction_ == BleSmpUserAction::PasskeyInput) {
    return BleSmpPasskeyPreparationResult::Pending;
  }
  if (result != BleSmpPasskeyPreparationResult::Ready) {
    return result;
  }
  if (!smpPairingPasskeyValid_) {
    return BleSmpPasskeyPreparationResult::Fatal;
  }
  smpSecureConnectionsPasskeyValue_ = smpPairingPasskeyValue_;
  smpSecureConnectionsPasskeyRound_ = 0U;
  smpSecureConnectionsPasskeyValid_ = true;
  return BleSmpPasskeyPreparationResult::Ready;
}

uint8_t BleRadio::secureConnectionsPasskeyRoundZ() const {
  if (!smpSecureConnectionsPasskeyValid_) {
    return 0U;
  }
  const uint8_t bit =
      static_cast<uint8_t>((smpSecureConnectionsPasskeyValue_ >>
                            smpSecureConnectionsPasskeyRound_) &
                           0x01UL);
  return static_cast<uint8_t>(0x80U | bit);
}

bool BleRadio::advanceSecureConnectionsPasskeyRound() {
  ++smpSecureConnectionsPasskeyRound_;
  if (smpSecureConnectionsPasskeyRound_ >= 20U) {
    return false;
  }
  if (!prepareLocalPairingRandom()) {
    return false;
  }
  memset(smpPeerConfirm_, 0, sizeof(smpPeerConfirm_));
  memset(smpPeerRandom_, 0, sizeof(smpPeerRandom_));
  memset(smpLocalConfirm_, 0, sizeof(smpLocalConfirm_));
  smpSecureConnectionsLocalConfirmReady_ = false;
  smpSecureConnectionsConfirmSent_ = false;
  smpSecureConnectionsRandomSent_ = false;
  return true;
}

bool BleRadio::isPairingInProgress() const {
  const uint32_t irqState = bleEnterCritical();
  const bool active =
      (smpPairingState_ != kSmpPairingStateIdle) ||
      smpPendingUserPasskey_ ||
      smpDeferredPairingFailed_ ||
      smpSecureConnectionsDeferredPublicKey_ ||
      smpSecureConnectionsDeferredConfirm_ ||
      smpSecureConnectionsDeferredRandom_ ||
      smpSecureConnectionsDeferredDhKeyCheck_;
  bleExitCritical(irqState);
  return active;
}

bool BleRadio::isSecurityProcedureActive() const {
  const uint32_t irqState = bleEnterCritical();
  const bool active =
      (smpPairingState_ != kSmpPairingStateIdle) ||
      smpPendingUserPasskey_ ||
      smpDeferredPairingFailed_ ||
      smpSecureConnectionsDeferredPublicKey_ ||
      smpSecureConnectionsDeferredConfirm_ ||
      smpSecureConnectionsDeferredRandom_ ||
      smpSecureConnectionsDeferredDhKeyCheck_ ||
      connectionBondedEncryptionRequested_ ||
      connectionEncKeyDerivationPending_ ||
      connectionEncStartReqPending_ ||
      connectionEncStartReqTxPending_ ||
      connectionEncAwaitingStartRsp_;
  bleExitCritical(irqState);
  return active;
}

bool BleRadio::getPendingPairingPasskey(uint8_t outPasskey[6],
                                        bool* outMatchRequest) const {
  if (outPasskey == nullptr || outMatchRequest == nullptr) {
    return false;
  }
  const uint32_t irqState = bleEnterCritical();
  if (!smpPendingUserPasskey_ ||
      smpPendingUserAction_ == BleSmpUserAction::PasskeyInput) {
    bleExitCritical(irqState);
    return false;
  }
  formatPairingPasskey(smpPendingUserPasskeyValue_, outPasskey);
  *outMatchRequest = smpPendingUserPasskeyMatchRequest_;
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::replyPendingPairingPasskey(bool accept) {
  const uint32_t irqState = bleEnterCritical();
  if (!smpPendingUserPasskey_ ||
      smpPendingUserAction_ == BleSmpUserAction::PasskeyInput) {
    bleExitCritical(irqState);
    return false;
  }

  const bool matchRequest = smpPendingUserPasskeyMatchRequest_;
  smpPendingUserPasskey_ = false;
  smpPendingUserPasskeyMatchRequest_ = false;
  smpPendingUserPasskeyValue_ = 0UL;
  smpPendingUserAction_ = BleSmpUserAction::None;
  if (!matchRequest) {
    bleExitCritical(irqState);
    return true;
  }
  if (accept) {
    smpSecureConnectionsDeferredDhKeyCheck_ = true;
  } else {
    smpDeferredPairingFailed_ = true;
    smpDeferredPairingFailedReason_ =
        matchRequest ? kSmpReasonNumericComparisonFailed
                     : kSmpReasonPasskeyEntryFailed;
  }
  bleExitCritical(irqState);
  serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
  return true;
}

bool BleRadio::getPendingPairingPasskeyRequest(
    uint32_t* outRequestId) const {
  if (outRequestId == nullptr) {
    return false;
  }
  const uint32_t irqState = bleEnterCritical();
  const bool pending =
      connected_ && smpTimerActive_ && !smpTimedOutForConnection_ &&
      smpPairingState_ != kSmpPairingStateIdle &&
      smpPendingUserPasskey_ &&
      smpPendingUserAction_ == BleSmpUserAction::PasskeyInput &&
      smpPendingUserPasskeyRequestId_ != 0UL;
  if (pending) {
    *outRequestId = smpPendingUserPasskeyRequestId_;
  }
  bleExitCritical(irqState);
  return pending;
}

bool BleRadio::replyPendingPairingPasskey(uint32_t requestId,
                                          const uint8_t passkey[6]) {
  if (passkey == nullptr) {
    return false;
  }
  uint32_t value = 0UL;
  for (size_t i = 0U; i < 6U; ++i) {
    if (passkey[i] < static_cast<uint8_t>('0') ||
        passkey[i] > static_cast<uint8_t>('9')) {
      return false;
    }
    value = (value * 10UL) +
            static_cast<uint32_t>(passkey[i] - static_cast<uint8_t>('0'));
  }

  const uint32_t irqState = bleEnterCritical();
  if (!connected_ || !smpTimerActive_ || smpTimedOutForConnection_ ||
      smpPairingState_ == kSmpPairingStateIdle ||
      !smpPendingUserPasskey_ ||
      smpPendingUserAction_ != BleSmpUserAction::PasskeyInput ||
      requestId == 0UL || requestId != smpPendingUserPasskeyRequestId_) {
    bleExitCritical(irqState);
    return false;
  }

  smpPairingPasskeyValue_ = value;
  smpPairingPasskeyValid_ = true;
  smpPendingUserPasskey_ = false;
  smpPendingUserPasskeyMatchRequest_ = false;
  smpPendingUserAction_ = BleSmpUserAction::None;
  smpPendingUserPasskeyCallbackDispatched_ = false;
  smpPendingUserPasskeyRequestId_ = 0UL;
  smpPendingUserPasskeyValue_ = 0UL;
  if (smpSecureConnectionsActive_) {
    smpSecureConnectionsPasskeyValue_ = value;
    smpSecureConnectionsPasskeyRound_ = 0U;
    smpSecureConnectionsPasskeyValid_ = true;
    if (smpSecureConnectionsPeerPublicKeyValid_ &&
        smpSecureConnectionsPublicKeySent_ &&
        (smpLocalInitiator_ || smpPeerConfirmReceived_)) {
      smpSecureConnectionsDeferredConfirm_ = true;
    }
  } else {
    smpLegacyConfirmDeferred_ = true;
  }
  bleExitCritical(irqState);

  emitBleTrace("SMP_PASSKEY_INPUT_ACCEPTED");
  serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
  return true;
}

bool BleRadio::serviceDeferredPairingConfirm() {
  if (!connected_ || smpSecureConnectionsActive_ ||
      !smpLegacyConfirmDeferred_ ||
      smpPairingState_ == kSmpPairingStateIdle ||
      smpTimedOutForConnection_ || connectionPendingTxValid_) {
    return false;
  }
  const uint8_t action = resolveLegacyPairingIoAction();
  if ((action == kSmpScIoActionInput || action == kSmpScIoActionDisplay) &&
      !smpPairingPasskeyValid_) {
    return false;
  }
  if (!smpLocalInitiator_ && !smpPeerConfirmReceived_) {
    return false;
  }

  uint8_t tk[16] = {0};
  buildLegacyTemporaryKey(tk);
  const uint8_t initiatorType =
      smpLocalInitiator_ ? (bleAddressTypeIsRandom(addressType_) ? 0x01U : 0x00U)
                         : (connectionPeerAddressRandom_ ? 0x01U : 0x00U);
  const uint8_t responderType =
      smpLocalInitiator_ ? (connectionPeerAddressRandom_ ? 0x01U : 0x00U)
                         : (bleAddressTypeIsRandom(addressType_) ? 0x01U : 0x00U);
  const uint8_t* initiatorAddress =
      smpLocalInitiator_ ? address_ : connectionPeerAddress_;
  const uint8_t* responderAddress =
      smpLocalInitiator_ ? connectionPeerAddress_ : address_;
  if (!smpC1(tk, smpLocalRandom_, smpPairingReq_, smpPairingRsp_,
             initiatorType, initiatorAddress, responderType, responderAddress,
             smpLocalConfirm_)) {
    smpDeferredPairingFailed_ = true;
    smpDeferredPairingFailedReason_ = kSmpReasonUnspecified;
    return false;
  }

  uint8_t localConfirm[kSmpPairingConfirmLen] = {0};
  localConfirm[0] = kSmpCodePairingConfirm;
  memcpy(&localConfirm[1], smpLocalConfirm_, sizeof(smpLocalConfirm_));
  if (!queuePendingSmpL2capResponse(localConfirm, sizeof(localConfirm))) {
    return false;
  }
  smpLegacyConfirmDeferred_ = false;
  smpPairingState_ = kSmpPairingStateConfirmSent;
  emitBleTrace("SMP_CONFIRM_TX_DEFERRED");
  serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
  return true;
}

bool BleRadio::consumePairingFailureReason(uint8_t* outReason) {
  if (outReason == nullptr) {
    return false;
  }
  const uint32_t irqState = bleEnterCritical();
  if (!smpPairingFailureReasonPending_) {
    bleExitCritical(irqState);
    return false;
  }
  *outReason = smpLastPairingFailureReason_;
  smpPairingFailureReasonPending_ = false;
  smpLastPairingFailureReason_ = 0U;
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::secureConnectionsInitiatorTransmitsConfirm() const {
  if (!smpSecureConnectionsActive_) {
    return false;
  }
  const uint8_t pairAlg = resolveCurrentSecureConnectionsPairAlgorithm();
  return pairAlg == kSmpScPairAlgPasskey;
}

bool BleRadio::secureConnectionsResponderVerifiesRandom() const {
  if (!smpSecureConnectionsActive_) {
    return false;
  }
  const uint8_t pairAlg = resolveCurrentSecureConnectionsPairAlgorithm();
  return pairAlg == kSmpScPairAlgPasskey;
}

void BleRadio::reduceSmpEncryptionKey(uint8_t key[16]) const {
  if (key == nullptr) {
    return;
  }
  const uint8_t keySize =
      (smpKeySize_ >= 7U && smpKeySize_ <= 16U) ? smpKeySize_ : 16U;
  if (keySize < 16U) {
    memset(&key[keySize], 0, static_cast<size_t>(16U - keySize));
  }
}

bool BleRadio::computeSecureConnectionsCheckValues() {
  if (!smpSecureConnectionsActive_) {
    return false;
  }
  if (!smpSecureConnectionsDhKeyReady_ &&
      !computeSecureConnectionsDhKey()) {
    return false;
  }
  if (smpSecureConnectionsCheckValuesReady_) {
    return true;
  }

  uint8_t localCheckR[16] = {0};
  uint8_t peerCheckR[16] = {0};
  const uint8_t pairAlg = resolveCurrentSecureConnectionsPairAlgorithm();
  if (pairAlg == kSmpScPairAlgPasskey) {
    if (prepareSecureConnectionsPasskey() !=
        BleSmpPasskeyPreparationResult::Ready) {
      return false;
    }
    writeLe32(&localCheckR[0], smpSecureConnectionsPasskeyValue_);
    writeLe32(&peerCheckR[0], smpSecureConnectionsPasskeyValue_);
  } else if (pairAlg == kSmpScPairAlgOob) {
    if (localHasPeerSecureConnectionsOobData(
            smpLocalInitiator_, smpPairingReq_, smpPairingRsp_)) {
      if (!smpOobDataRemoteValid_) {
        return false;
      }
      memcpy(localCheckR, smpOobRemote_.r, sizeof(localCheckR));
    }
    if (peerHasLocalSecureConnectionsOobData(
            smpLocalInitiator_, smpPairingReq_, smpPairingRsp_)) {
      if (!smpOobDataLocalValid_) {
        return false;
      }
      memcpy(peerCheckR, smpOobLocal_.r, sizeof(peerCheckR));
    }
  }
  const uint8_t localType =
      bleAddressTypeIsRandom(addressType_) ? 0x01U : 0x00U;
  const uint8_t peerType = connectionPeerAddressRandom_ ? 0x01U : 0x00U;
  const uint8_t initiatorType = smpLocalInitiator_ ? localType : peerType;
  const uint8_t responderType = smpLocalInitiator_ ? peerType : localType;
  const uint8_t* initiatorAddress =
      smpLocalInitiator_ ? address_ : connectionPeerAddress_;
  const uint8_t* responderAddress =
      smpLocalInitiator_ ? connectionPeerAddress_ : address_;
  const uint8_t* initiatorRandom =
      smpLocalInitiator_ ? smpLocalRandom_ : smpPeerRandom_;
  const uint8_t* responderRandom =
      smpLocalInitiator_ ? smpPeerRandom_ : smpLocalRandom_;
  const uint8_t* localIoCaps =
      smpLocalInitiator_ ? &smpPairingReq_[1] : &smpPairingRsp_[1];
  const uint8_t* peerIoCaps =
      smpLocalInitiator_ ? &smpPairingRsp_[1] : &smpPairingReq_[1];
  const uint32_t startUs = micros();

  if (!bleSecureConnectionsF5(smpSecureConnectionsDhKey_, initiatorRandom,
                              responderRandom, initiatorType,
                              initiatorAddress, responderType,
                              responderAddress, smpSecureConnectionsMacKey_,
                              smpStk_)) {
    return false;
  }
  if (!bleSecureConnectionsF6(smpSecureConnectionsMacKey_, smpLocalRandom_,
                              smpPeerRandom_, localCheckR, localIoCaps, localType,
                              address_, peerType, connectionPeerAddress_,
                              smpSecureConnectionsLocalDhKeyCheck_)) {
    return false;
  }
  if (!bleSecureConnectionsF6(smpSecureConnectionsMacKey_, smpPeerRandom_,
                              smpLocalRandom_, peerCheckR, peerIoCaps, peerType,
                              connectionPeerAddress_, localType, address_,
                              smpSecureConnectionsPeerDhKeyCheck_)) {
    return false;
  }

  reduceSmpEncryptionKey(smpStk_);
  smpStkValid_ = true;
  connectionEncAuthenticated_ = currentPairingIsAuthenticated();
  connectionEncSecureConnections_ = true;
  memcpy(encDebug_.encLastStk, smpStk_, sizeof(encDebug_.encLastStk));
  if (smpBondingRequested_) {
    memcpy(smpPeerLtk_, smpStk_, sizeof(smpPeerLtk_));
    smpPeerLtkValid_ = true;
    memset(smpEncReqRand_, 0, sizeof(smpEncReqRand_));
    smpEncReqEdiv_ = 0U;
  }
  smpSecureConnectionsCheckValuesTimeUs_ = micros() - startUs;
  smpSecureConnectionsCheckValuesReady_ = true;
  return true;
}

void BleRadio::clearPendingL2capTxFragment() {
  connectionPendingL2capTxFragmentActive_ = false;
  connectionPendingL2capTxFragmentLength_ = 0U;
  connectionPendingL2capTxFragmentOffset_ = 0U;
  memset(connectionPendingL2capTxFragmentPayload_, 0,
         sizeof(connectionPendingL2capTxFragmentPayload_));
}

bool BleRadio::deferL2capTxPayload(const uint8_t* l2capPayload,
                                   uint16_t l2capPayloadLength) {
  if (l2capPayload == nullptr || l2capPayloadLength == 0U ||
      connectionPendingL2capTxFragmentActive_ ||
      l2capPayloadLength > sizeof(connectionPendingL2capTxFragmentPayload_)) {
    return false;
  }

  memcpy(connectionPendingL2capTxFragmentPayload_, l2capPayload,
         l2capPayloadLength);
  connectionPendingL2capTxFragmentLength_ = l2capPayloadLength;
  connectionPendingL2capTxFragmentOffset_ = 0U;
  connectionPendingL2capTxFragmentActive_ = true;
  emitBleTrace("L2CAP_TX_DEFER_Q");
  return true;
}

bool BleRadio::rewindPendingL2capTxFragment() {
  if (!connectionPendingL2capTxFragmentActive_) {
    return false;
  }
  connectionPendingL2capTxFragmentOffset_ = 0U;
  emitBleTrace("L2CAP_TX_FRAG_REWIND_Q");
  return true;
}

bool BleRadio::queueNextPendingL2capTxFragment() {
  if (!connected_ || connectionPendingTxValid_ ||
      !connectionPendingL2capTxFragmentActive_) {
    return false;
  }

  if (connectionPendingL2capTxFragmentOffset_ >=
      connectionPendingL2capTxFragmentLength_) {
    clearPendingL2capTxFragment();
    return false;
  }

  uint16_t maxPayload = currentTxDataPduPayloadLength();
  if (maxPayload == 0U) {
    return false;
  }
  if (maxPayload > kBleDataPduMaxPayload) {
    maxPayload = kBleDataPduMaxPayload;
  }

  const bool startsL2capSdu =
      connectionPendingL2capTxFragmentOffset_ == 0U;
  const uint16_t remaining = static_cast<uint16_t>(
      connectionPendingL2capTxFragmentLength_ -
      connectionPendingL2capTxFragmentOffset_);
  const uint8_t fragmentLength =
      static_cast<uint8_t>(minU16(maxPayload, remaining));
  if (fragmentLength == 0U) {
    return false;
  }

  memcpy(connectionPendingTxPayload_,
         &connectionPendingL2capTxFragmentPayload_[
             connectionPendingL2capTxFragmentOffset_],
         fragmentLength);
  connectionPendingL2capTxFragmentOffset_ = static_cast<uint16_t>(
      connectionPendingL2capTxFragmentOffset_ + fragmentLength);
  connectionPendingTxLlid_ = startsL2capSdu
                                 ? kBlePduDataStartOrComplete
                                 : kBlePduDataContinuation;
  connectionPendingTxLength_ = fragmentLength;
  connectionPendingTxSmpFragment_ =
      connectionPendingL2capTxFragmentLength_ >= kBleL2capHeaderLen &&
      readLe16(&connectionPendingL2capTxFragmentPayload_[2]) ==
          kBleL2capCidSmp;
  connectionPendingTxValid_ = true;

  if (connectionPendingL2capTxFragmentOffset_ >=
      connectionPendingL2capTxFragmentLength_) {
    clearPendingL2capTxFragment();
    emitBleTrace("L2CAP_TX_FRAG_DONE_Q");
  } else {
    emitBleTrace("L2CAP_TX_FRAG_CONT_Q");
  }
  return true;
}

bool BleRadio::prepareL2capTxPayload(const uint8_t* l2capPayload,
                                     uint16_t l2capPayloadLength,
                                     uint8_t* outPayload,
                                     uint8_t* outPayloadLength) {
  if (l2capPayload == nullptr || outPayload == nullptr ||
      outPayloadLength == nullptr || l2capPayloadLength == 0U) {
    return false;
  }

  uint16_t maxPayload = currentTxDataPduPayloadLength();
  if (maxPayload == 0U) {
    return false;
  }
  if (maxPayload > kBleDataPduMaxPayload) {
    maxPayload = kBleDataPduMaxPayload;
  }

  if (l2capPayloadLength <= maxPayload) {
    memcpy(outPayload, l2capPayload, l2capPayloadLength);
    *outPayloadLength = static_cast<uint8_t>(l2capPayloadLength);
    return true;
  }

  if (connectionPendingL2capTxFragmentActive_ ||
      (l2capPayloadLength > sizeof(connectionPendingL2capTxFragmentPayload_))) {
    return false;
  }

  const uint8_t firstFragmentLength = static_cast<uint8_t>(maxPayload);
  memcpy(outPayload, l2capPayload, firstFragmentLength);
  *outPayloadLength = firstFragmentLength;

  memcpy(connectionPendingL2capTxFragmentPayload_, l2capPayload,
         l2capPayloadLength);
  connectionPendingL2capTxFragmentLength_ = l2capPayloadLength;
  connectionPendingL2capTxFragmentOffset_ = firstFragmentLength;
  connectionPendingL2capTxFragmentActive_ = true;
  emitBleTrace("L2CAP_TX_FRAG_START_Q");
  return true;
}

bool BleRadio::pendingTxIsAttServerPushPdu() const {
  if (!connectionPendingTxValid_ ||
      connectionPendingTxLlid_ != kBlePduDataStartOrComplete ||
      connectionPendingTxLength_ < 7U ||
      readLe16(&connectionPendingTxPayload_[2]) != kBleL2capCidAtt) {
    return false;
  }
  const uint8_t opcode = connectionPendingTxPayload_[4];
  return opcode == kAttOpHandleValueNtf || opcode == kAttOpHandleValueInd;
}

bool BleRadio::canPreservePendingServerPushPdu() const {
  if (!pendingTxIsAttServerPushPdu()) {
    return false;
  }

  const uint8_t opcode = connectionPendingTxPayload_[4];
  const uint16_t handle = readLe16(&connectionPendingTxPayload_[5]);
  if (opcode == kAttOpHandleValueInd &&
      handle == kHandleGattServiceChangedValue) {
    return true;
  }
  if (opcode == kAttOpHandleValueNtf && handle == kHandleBatteryLevelValue) {
    return true;
  }

  const BleCustomCharacteristicState* characteristic =
      findCustomCharacteristicByValueHandle(handle);
  if (characteristic == nullptr) {
    return false;
  }
  const ptrdiff_t idx = characteristic - &customGattCharacteristics_[0];
  return idx >= 0 &&
         static_cast<uint8_t>(idx) < customGattCharacteristicCount_ &&
         connectionCustomNotificationQueueCount_ <
             kCustomGattNotificationQueueDepth;
}

bool BleRadio::preservePendingServerPushPdu() {
  if (!canPreservePendingServerPushPdu()) {
    return false;
  }

  const uint8_t opcode = connectionPendingTxPayload_[4];
  const uint16_t handle = readLe16(&connectionPendingTxPayload_[5]);
  const bool indicate = opcode == kAttOpHandleValueInd;
  const uint8_t valueLength =
      static_cast<uint8_t>(connectionPendingTxLength_ - 7U);
  const uint8_t* value =
      valueLength > 0U ? &connectionPendingTxPayload_[7] : nullptr;

  if (indicate && handle == kHandleGattServiceChangedValue) {
    connectionServiceChangedIndicationAwaitingConfirm_ = false;
    connectionServiceChangedIndicationPending_ = true;
    return true;
  }
  if (!indicate && handle == kHandleBatteryLevelValue) {
    connectionBatteryNotificationPending_ = true;
    return true;
  }

  BleCustomCharacteristicState* characteristic =
      findCustomCharacteristicByValueHandle(handle);
  if (characteristic == nullptr) {
    return false;
  }
  const ptrdiff_t idx = characteristic - &customGattCharacteristics_[0];
  if (idx < 0 || static_cast<uint8_t>(idx) >= customGattCharacteristicCount_ ||
      connectionCustomNotificationQueueCount_ >=
          kCustomGattNotificationQueueDepth) {
    return false;
  }

  const uint8_t newTail =
      connectionCustomNotificationQueueTail_ == 0U
          ? static_cast<uint8_t>(kCustomGattNotificationQueueDepth - 1U)
          : static_cast<uint8_t>(connectionCustomNotificationQueueTail_ - 1U);
  BleQueuedCustomNotificationState& slot =
      connectionCustomNotificationQueue_[newTail];
  slot.valid = true;
  slot.characteristicIndex = static_cast<uint8_t>(idx);
  slot.indication = indicate;
  slot.valueLength = valueLength;
  memset(slot.value, 0, sizeof(slot.value));
  if (valueLength > 0U && value != nullptr) {
    memcpy(slot.value, value, valueLength);
  }
  connectionCustomNotificationQueueTail_ = newTail;
  ++connectionCustomNotificationQueueCount_;
  if (indicate && connectionCustomIndicationAwaitingHandle_ == handle) {
    connectionCustomIndicationAwaitingHandle_ = 0U;
  }
  return true;
}

bool BleRadio::pendingTxIsOptionalLinkSetupPdu() const {
  if (!connectionPendingTxValid_ || connectionPendingTxLength_ == 0U) {
    return false;
  }
  if (connectionPendingTxLlid_ == kBlePduLlControl) {
    const uint8_t opcode = connectionPendingTxPayload_[0];
    return opcode == kBleLlCtrlLengthReq || opcode == kBleLlCtrlPhyReq;
  }
  if (connectionPendingTxLlid_ != kBlePduDataStartOrComplete ||
      connectionPendingTxLength_ < kBleL2capHeaderLen + 1U) {
    return false;
  }
  const uint16_t cid = readLe16(&connectionPendingTxPayload_[2]);
  return (cid == kBleL2capCidAtt &&
          connectionPendingTxPayload_[4] == kAttOpExchangeMtuReq) ||
         (cid == kBleL2capCidLeSignaling &&
          connectionPendingTxPayload_[4] ==
              kL2capSigCodeConnParamUpdateReq);
}

bool BleRadio::requeuePendingOptionalLinkSetupPdu() {
  if (!pendingTxIsOptionalLinkSetupPdu()) {
    return false;
  }
  if (connectionPendingTxLlid_ == kBlePduLlControl) {
    if (connectionPendingTxPayload_[0] == kBleLlCtrlLengthReq) {
      connectionDataLengthUpdateInFlight_ = false;
      connectionDataLengthUpdatePending_ = true;
    } else {
      connectionPhyRequestInFlight_ = false;
      connectionPhyRequestPending_ = true;
    }
    return true;
  }

  const uint16_t cid = readLe16(&connectionPendingTxPayload_[2]);
  if (cid == kBleL2capCidAtt) {
    connectionAttMtuExchangeInFlight_ = false;
    connectionAttMtuExchangePending_ = true;
  } else {
    connectionConnParamUpdateInFlight_ = false;
    connectionConnParamUpdatePending_ = true;
  }
  return true;
}

bool BleRadio::buildSmpL2capResponse(const uint8_t* smpPayload, uint8_t smpLength,
                                     uint8_t* outPayload,
                                     uint8_t* outPayloadLength) {
  if (smpPayload == nullptr || outPayload == nullptr ||
      outPayloadLength == nullptr || smpLength == 0U ||
      smpTimedOutForConnection_) {
    return false;
  }

  const uint16_t l2capLength = smpLength;
  const uint16_t totalLength =
      static_cast<uint16_t>(kBleL2capHeaderLen + l2capLength);
  if (totalLength > kBleDataPduMaxPayload) {
    return false;
  }

  uint8_t l2capPayload[kBleDataPduMaxPayload] = {0};
  writeLe16(&l2capPayload[0], l2capLength);
  writeLe16(&l2capPayload[2], kBleL2capCidSmp);
  memcpy(&l2capPayload[4], smpPayload, smpLength);
  const bool prepared =
      prepareL2capTxPayload(l2capPayload, totalLength, outPayload,
                            outPayloadLength);
  if (!prepared) {
    return false;
  }
  if (smpPayload[0] == kSmpCodePairingFailed) {
    noteSmpPairingFailure(
        (smpLength >= 2U) ? smpPayload[1] : kSmpReasonUnspecified);
    stopSmpTimer();
  } else {
    restartSmpTimer();
  }
  return true;
}

bool BleRadio::queuePendingSmpL2capResponse(const uint8_t* smpPayload,
                                            uint8_t smpLength) {
  if (!connected_) {
    return false;
  }

  const bool preserveServerPush = connectionPendingTxValid_ &&
                                  pendingTxIsAttServerPushPdu();
  const bool requeueLinkSetup = connectionPendingTxValid_ &&
                                pendingTxIsOptionalLinkSetupPdu();
  if (connectionPendingTxValid_ && !preserveServerPush && !requeueLinkSetup) {
    return false;
  }
  if (preserveServerPush && !canPreservePendingServerPushPdu()) {
    return false;
  }

  uint8_t pendingSmpPayload[kBleDataPduMaxPayload] = {0};
  uint8_t payloadLength = 0U;
  if (!buildSmpL2capResponse(smpPayload, smpLength, pendingSmpPayload,
                             &payloadLength)) {
    return false;
  }

  if ((preserveServerPush && !preservePendingServerPushPdu()) ||
      (requeueLinkSetup && !requeuePendingOptionalLinkSetupPdu())) {
    return false;
  }

  memcpy(connectionPendingTxPayload_, pendingSmpPayload, payloadLength);
  connectionPendingTxLlid_ = kBlePduDataStartOrComplete;
  connectionPendingTxLength_ = payloadLength;
  connectionPendingTxValid_ = true;
  return true;
}

void BleRadio::serviceSecureConnectionsWork() {
  serviceSmpTimer();
  if (!connected_) {
    return;
  }
  if (!smpSecureConnectionsActive_) {
    (void)serviceDeferredPairingConfirm();
    return;
  }

  if (!smpSecureConnectionsLocalKeyReady_ &&
      !smpSecureConnectionsLocalKeyInProgress_ &&
      ((smpLocalInitiator_ &&
        (smpPairingState_ == kSmpPairingStateReqSent)) ||
       (!smpLocalInitiator_ &&
        (smpPairingState_ == kSmpPairingStateRspSent)) ||
       smpSecureConnectionsDeferredPublicKey_)) {
    (void)ensureSecureConnectionsLocalKeypair();
  }

  if (smpDeferredPairingFailed_) {
    const uint8_t fail[2] = {kSmpCodePairingFailed,
                             smpDeferredPairingFailedReason_};
    if (queuePendingSmpL2capResponse(fail, sizeof(fail))) {
      const uint8_t reason = smpDeferredPairingFailedReason_;
      latchSecureConnectionsDebugState();
      smpPairingFailureReasonPending_ = true;
      smpLastPairingFailureReason_ = reason;
      invalidateSecureConnectionsKeypairAndOob(
          smpSecureConnectionsLocalKeyReady_,
          smpSecureConnectionsLocalKeyInProgress_,
          smpSecureConnectionsPrivateKey_, smpSecureConnectionsPublicKey_,
          smpOobDataLocalValid_, smpOobDataRemoteValid_, smpOobLocal_,
          smpOobRemote_);
      clearSmpSecurityState();
      emitBleTrace("SMP_PAIRING_FAILED");
      serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
    }
    return;
  }

  if (smpSecureConnectionsDeferredPublicKey_) {
    if (ensureSecureConnectionsLocalKeypair()) {
      if (smpSecureConnectionsPeerPublicKeyValid_) {
        if (memcmp(&smpSecureConnectionsPeerPublicKey_[1],
                   &smpSecureConnectionsPublicKey_[1], 32U) == 0) {
          const uint8_t fail[2] = {
              kSmpCodePairingFailed, kSmpReasonUnspecified};
          if (queuePendingSmpL2capResponse(fail, sizeof(fail))) {
            latchSecureConnectionsDebugState();
            smpPairingFailureReasonPending_ = true;
            smpLastPairingFailureReason_ = kSmpReasonUnspecified;
            invalidateSecureConnectionsKeypairAndOob(
                smpSecureConnectionsLocalKeyReady_,
                smpSecureConnectionsLocalKeyInProgress_,
                smpSecureConnectionsPrivateKey_,
                smpSecureConnectionsPublicKey_, smpOobDataLocalValid_,
                smpOobDataRemoteValid_, smpOobLocal_, smpOobRemote_);
            clearSmpSecurityState();
            emitBleTrace("SMP_SC_PUBKEY_REJECTED");
            serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
          }
          return;
        }
        const BleSmpPasskeyPreparationResult passkeyResult =
            prepareSecureConnectionsPasskey();
        if (passkeyResult == BleSmpPasskeyPreparationResult::Fatal) {
          const uint8_t fail[2] = {
              kSmpCodePairingFailed, kSmpReasonAuthRequirements};
          if (queuePendingSmpL2capResponse(fail, sizeof(fail))) {
            latchSecureConnectionsDebugState();
            smpPairingFailureReasonPending_ = true;
            smpLastPairingFailureReason_ = kSmpReasonAuthRequirements;
            invalidateSecureConnectionsKeypairAndOob(
                smpSecureConnectionsLocalKeyReady_,
                smpSecureConnectionsLocalKeyInProgress_,
                smpSecureConnectionsPrivateKey_,
                smpSecureConnectionsPublicKey_, smpOobDataLocalValid_,
                smpOobDataRemoteValid_, smpOobLocal_, smpOobRemote_);
            clearSmpSecurityState();
            emitBleTrace("SMP_SC_ALG_UNSUPPORTED");
            serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
          }
          return;
        }
        const uint8_t pairAlg =
            resolveCurrentSecureConnectionsPairAlgorithm();
        if (pairAlg == kSmpScPairAlgOob) {
          // OOB commitments travel out of band. Once both public keys are
          // known, the initiator starts the in-band phase with fresh Na.
          smpSecureConnectionsDeferredConfirm_ = false;
          smpSecureConnectionsDeferredRandom_ = smpLocalInitiator_;
        } else {
          smpSecureConnectionsDeferredConfirm_ =
              smpLocalInitiator_ ? secureConnectionsInitiatorTransmitsConfirm()
                                 : !secureConnectionsInitiatorTransmitsConfirm();
        }
      }
      uint8_t localPublicKey[kSmpPairingPublicKeyLen] = {0};
      buildSecureConnectionsWirePublicKeyFromInternalLe(
          smpSecureConnectionsPublicKey_,
          smpSecureConnectionsWireFormatBigEndian_, localPublicKey);
      localPublicKey[0] = kSmpCodePairingPublicKey;
      if (queuePendingSmpL2capResponse(localPublicKey, sizeof(localPublicKey))) {
        smpSecureConnectionsDeferredPublicKey_ = false;
        smpSecureConnectionsPublicKeySent_ = true;
        emitBleTrace("SMP_SC_PUBKEY_TX");
        serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
      }
    }
  }

  if (smpSecureConnectionsDeferredConfirm_) {
    const BleSmpPasskeyPreparationResult passkeyResult =
        prepareSecureConnectionsPasskey();
    if (passkeyResult == BleSmpPasskeyPreparationResult::Pending) {
      return;
    }
    if (passkeyResult == BleSmpPasskeyPreparationResult::Fatal) {
      smpSecureConnectionsDeferredConfirm_ = false;
      smpDeferredPairingFailed_ = true;
      smpDeferredPairingFailedReason_ = kSmpReasonAuthRequirements;
      return;
    }
    if (computeSecureConnectionsLocalConfirm()) {
      uint8_t localConfirm[kSmpPairingConfirmLen] = {0};
      localConfirm[0] = kSmpCodePairingConfirm;
      memcpy(&localConfirm[1], smpLocalConfirm_, sizeof(smpLocalConfirm_));
      if (queuePendingSmpL2capResponse(localConfirm, sizeof(localConfirm))) {
        smpSecureConnectionsDeferredConfirm_ = false;
        smpSecureConnectionsConfirmSent_ = true;
        smpPairingState_ = kSmpPairingStateConfirmSent;
        emitBleTrace("SMP_SC_CONFIRM_TX");
        serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
      }
    }
  }

  if (smpSecureConnectionsDeferredRandom_) {
    uint8_t localRandom[kSmpPairingRandomLen] = {0};
    localRandom[0] = kSmpCodePairingRandom;
    memcpy(&localRandom[1], smpLocalRandom_, sizeof(smpLocalRandom_));
    if (queuePendingSmpL2capResponse(localRandom, sizeof(localRandom))) {
      smpSecureConnectionsDeferredRandom_ = false;
      smpSecureConnectionsRandomSent_ = true;
      if (!smpLocalInitiator_ &&
          resolveCurrentSecureConnectionsPairAlgorithm() !=
              kSmpScPairAlgPasskey) {
        smpSecureConnectionsDeferredDhKeyCheck_ = true;
      }
      smpPairingState_ = kSmpPairingStateRandomSent;
      emitBleTrace("SMP_SC_RANDOM_TX");
      serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
    }
  }

  // Avoid long synchronous ECC work here. During SC pairing the peer may
  // pipeline Confirm/Random quickly after the public key exchange, and eager
  // DhKey/check-value precompute can starve those inbound packets. Compute the
  // expensive material only when the state machine actually needs it.

  if (smpSecureConnectionsDeferredDhKeyCheck_) {
    if (smpPendingUserPasskey_ && smpPendingUserPasskeyMatchRequest_ &&
        resolveCurrentSecureConnectionsPairAlgorithm() ==
            kSmpScPairAlgNumericComparison) {
      return;
    }
    if (computeSecureConnectionsCheckValues()) {
      if (smpLocalInitiator_ && !smpSecureConnectionsDhKeyCheckSent_) {
        uint8_t localDhKeyCheck[kSmpDhKeyCheckLen] = {0};
        localDhKeyCheck[0] = kSmpCodeDhKeyCheck;
        memcpy(&localDhKeyCheck[1], smpSecureConnectionsLocalDhKeyCheck_,
               sizeof(smpSecureConnectionsLocalDhKeyCheck_));
        if (queuePendingSmpL2capResponse(localDhKeyCheck,
                                         sizeof(localDhKeyCheck))) {
          smpSecureConnectionsDhKeyCheckSent_ = true;
          emitBleTrace("SMP_SC_DHKEY_CHECK_TX");
          serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
        }
        return;
      }
      if (!smpSecureConnectionsReceivedDhKeyCheckValid_) {
        return;
      }
      if (memcmp(smpSecureConnectionsReceivedDhKeyCheck_,
                 smpSecureConnectionsPeerDhKeyCheck_,
                 sizeof(smpSecureConnectionsPeerDhKeyCheck_)) != 0) {
        const uint8_t fail[2] = {
            kSmpCodePairingFailed, kSmpReasonDhKeyCheckFailed};
        if (queuePendingSmpL2capResponse(fail, sizeof(fail))) {
          latchSecureConnectionsDebugState();
          smpPairingFailureReasonPending_ = true;
          smpLastPairingFailureReason_ = kSmpReasonDhKeyCheckFailed;
          clearSmpSecurityState();
          emitBleTrace("SMP_SC_DHKEY_CHECK_FAIL_TX");
          serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
        }
        return;
      }

      uint8_t localDhKeyCheck[kSmpDhKeyCheckLen] = {0};
      localDhKeyCheck[0] = kSmpCodeDhKeyCheck;
      memcpy(&localDhKeyCheck[1], smpSecureConnectionsLocalDhKeyCheck_,
             sizeof(smpSecureConnectionsLocalDhKeyCheck_));
      if (queuePendingSmpL2capResponse(localDhKeyCheck,
                                       sizeof(localDhKeyCheck))) {
        smpSecureConnectionsDeferredDhKeyCheck_ = false;
        smpSecureConnectionsReceivedDhKeyCheckValid_ = false;
        smpSecureConnectionsDhKeyCheckSent_ = true;
        invalidateSecureConnectionsKeypairAndOob(
            smpSecureConnectionsLocalKeyReady_,
            smpSecureConnectionsLocalKeyInProgress_,
            smpSecureConnectionsPrivateKey_, smpSecureConnectionsPublicKey_,
            smpOobDataLocalValid_, smpOobDataRemoteValid_, smpOobLocal_,
            smpOobRemote_);
        emitBleTrace("SMP_SC_DHKEY_CHECK_TX");
        serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
      }
    }
  }

  serviceSmpKeyDistribution();
  (void)completeSmpKeyDistributionIfDone();
}

bool BleRadio::prepareLocalSigningKey() {
  if (smpLocalCsrkValid_) {
    return true;
  }
  if (!connected_ || !smpBondingRequested_ ||
      !smpDistributeLocalSignKey_ || bleRunningInIsr()) {
    return false;
  }

  uint8_t csrk[16] = {0};
  const uint32_t seed =
      micros() ^ connectionAccessAddress_ ^
      static_cast<uint32_t>(connectionEventCounter_ << 16U) ^ 0x4353524BUL;
  if (!fillBleSecurityRandomBytes(csrk, sizeof(csrk), seed,
                                  kBleSecurityRandomSpinLimit)) {
    return false;
  }
  uint8_t combined = 0U;
  for (uint8_t i = 0U; i < sizeof(csrk); ++i) {
    combined = static_cast<uint8_t>(combined | csrk[i]);
  }
  if (combined == 0U) {
    return false;
  }

  memcpy(smpLocalCsrk_, csrk, sizeof(smpLocalCsrk_));
  smpLocalCsrkValid_ = true;
  smpLocalSignCounter_ = 0U;
  smpLocalSignCounterReservedUntil_ = 0U;
  emitBleTrace("SMP_LOCAL_CSRK_READY");
  return true;
}

bool BleRadio::persistActiveSigningCounters(bool requireDurable) {
  if (bleRunningInIsr() || !bondRecordValid_ ||
      (!smpPeerCsrkValid_ && !smpLocalCsrkValid_)) {
    return false;
  }

  if (smpPeerCsrkValid_) {
    memcpy(bondRecord_.peerCsrk, smpPeerCsrk_,
           sizeof(bondRecord_.peerCsrk));
    bondRecord_.peerCsrkValid = 1U;
  }
  if (smpLocalCsrkValid_) {
    memcpy(bondRecord_.localCsrk, smpLocalCsrk_,
           sizeof(bondRecord_.localCsrk));
    bondRecord_.localCsrkValid = 1U;
  }
  if (!isBondRecordUsable(bondRecord_) ||
      !writeRetainedBondRecord(bondRecord_)) {
    return false;
  }

  if (requireDurable) {
    const bool callbackMode =
        bondLoadCallback_ != nullptr || bondSaveCallback_ != nullptr;
    const bool flashOk = callbackMode
                             ? (bondFlashPersistPending_
                                    ? writeFlashBondRecord(bondRecord_)
                                    : writeFlashBondSigningState(bondRecord_))
                             : persistActiveBondDatabaseRecord(false);
    if (!flashOk) {
      bondFlashPersistPending_ = true;
      return false;
    }
    bondFlashPersistPending_ = false;
  } else {
    bondFlashPersistPending_ = true;
  }
  // Legacy callbacks remain capacity-1 and use the frozen signing extension;
  // the built-in eight-bond store commits counters in the selected replica.
  return true;
}

bool BleRadio::verifyAttSignedWrite(const uint8_t* attRequest,
                                    uint16_t requestLength,
                                    uint32_t* outSignCounter) {
  if (attRequest == nullptr || requestLength < 15U ||
      attRequest[0] != kAttOpSignedWriteCmd || !bondRecordValid_ ||
      !smpPeerCsrkValid_ || connectionEncSessionValid_ ||
      connectionEncRxEnabled_ || connectionEncTxEnabled_) {
    return false;
  }

  const uint16_t messageLength = static_cast<uint16_t>(requestLength - 12U);
  const uint32_t receivedCounter = readLe32(&attRequest[messageLength]);
  if (smpPeerSignCounterValid_ &&
      receivedCounter <= smpPeerSignCounter_) {
    ++signedWriteReplayRejectCount_;
    emitBleTrace("ATT_SIGNED_WRITE_REPLAY");
    return false;
  }

  uint8_t expectedMac[8] = {0};
  if (!bleComputeDataSignature(smpPeerCsrk_, attRequest, messageLength,
                               receivedCounter, expectedMac)) {
    return false;
  }
  uint8_t difference = 0U;
  for (uint8_t i = 0U; i < sizeof(expectedMac); ++i) {
    difference = static_cast<uint8_t>(
        difference | (expectedMac[i] ^ attRequest[messageLength + 4U + i]));
  }
  if (difference != 0U) {
    emitBleTrace("ATT_SIGNED_WRITE_BAD_MAC");
    return false;
  }

  smpPeerSignCounter_ = receivedCounter;
  smpPeerSignCounterValid_ = true;
  if (outSignCounter != nullptr) {
    *outSignCounter = receivedCounter;
  }
  emitBleTrace("ATT_SIGNED_WRITE_VERIFIED");
  return true;
}

bool BleRadio::persistDeferredSignedWriteCounter(uint32_t signCounter) {
  if (!bondRecordValid_ || !smpPeerCsrkValid_ ||
      !smpPeerSignCounterValid_ || signCounter > smpPeerSignCounter_) {
    return false;
  }
  if (bondRecord_.peerSignCounterValid != 0U &&
      signCounter == smpPeerSignCounterReservedUntil_) {
    return true;
  }

  // A receiver cannot reserve counters on behalf of its peer: after this
  // device reboots, the peer may legitimately send signCounter + 1. Persist
  // the exact accepted counter before making the command visible to GATT.
  bondRecord_.peerSignCounter = signCounter;
  bondRecord_.peerSignCounterValid = 1U;
  if (!persistActiveSigningCounters(true)) {
    emitBleTrace("ATT_SIGNED_WRITE_COUNTER_PERSIST_FAILED");
    return false;
  }
  smpPeerSignCounterReservedUntil_ = signCounter;
  emitBleTrace("ATT_SIGNED_WRITE_COUNTER_PERSISTED");
  return true;
}

void BleRadio::applyDeferredSignedGattWrite(uint16_t valueHandle,
                                            const uint8_t* value,
                                            uint16_t valueLength,
                                            uint32_t signCounter) {
  if (!persistDeferredSignedWriteCounter(signCounter)) {
    return;
  }
  if (!connected_ ||
      findCustomCharacteristicByValueHandle(valueHandle) == nullptr) {
    emitBleTrace("ATT_SIGNED_WRITE_IGNORED");
    return;
  }
  if (connectionPreparedWriteActive_ &&
      connectionPreparedWriteHandle_ == valueHandle) {
    clearPreparedWriteState();
  }
  uint8_t ignoredError = kAttErrWriteNotPermitted;
  if (writeCustomGattCharacteristic(valueHandle, value, valueLength, false,
                                    &ignoredError, true)) {
    emitBleTrace("ATT_SIGNED_WRITE_APPLIED");
  } else {
    emitBleTrace("ATT_SIGNED_WRITE_IGNORED");
  }
}

void BleRadio::serviceSmpKeyDistribution() {
  if (!connected_ || smpPairingState_ == kSmpPairingStateIdle ||
      !smpBondingRequested_ ||
      (!smpDistributeLocalEncKey_ && !smpDistributeLocalIdKey_ &&
       !smpDistributeLocalSignKey_) ||
      !connectionEncSessionValid_ || !connectionEncRxEnabled_ ||
      !connectionEncTxEnabled_) {
    return;
  }
  if (smpSecureConnectionsActive_ && !smpSecureConnectionsDhKeyCheckSent_) {
    return;
  }
  if (smpDistributeLocalSignKey_ && !smpLocalCsrkValid_ &&
      !prepareLocalSigningKey()) {
    emitBleTrace("SMP_LOCAL_CSRK_UNAVAILABLE");
    return;
  }
  if (smpDistributeLocalEncKey_ &&
      (!smpLocalLtkValid_ || !prepareLocalLegacyEncryptionKey())) {
    emitBleTrace("SMP_LOCAL_LTK_UNAVAILABLE");
    return;
  }

  const bool peerEncDone =
      !smpExpectPeerEncKey_ ||
      (smpPeerLtkValid_ && !smpPeerLtkAwaitMasterId_);
  const bool peerIdDone =
      !smpExpectPeerIdKey_ ||
      (smpPeerIrkValid_ && smpPeerIdentityAddressValid_);
  const bool peerSignDone = !smpExpectPeerSignKey_ || smpPeerCsrkValid_;
  // Preserve the controller's established over-the-air Phase 3 ordering: the
  // local initiator waits for the peer distribution before sending its keys.
  if (smpLocalInitiator_ &&
      (!peerEncDone || !peerIdDone || !peerSignDone)) {
    return;
  }

  if (smpDistributeLocalEncKey_ && !smpLocalEncInfoSent_) {
    uint8_t encryptionInfo[kSmpEncryptionInformationLen] = {0};
    encryptionInfo[0] = kSmpCodeEncryptionInformation;
    reduceSmpEncryptionKey(smpLocalLtk_);
    memcpy(&encryptionInfo[1], smpLocalLtk_, sizeof(smpLocalLtk_));
    if (queuePendingSmpL2capResponse(encryptionInfo,
                                    sizeof(encryptionInfo))) {
      smpLocalEncInfoSent_ = true;
      smpLocalEncInfoAcked_ = false;
      emitBleTrace("SMP_LOCAL_ENC_INFO_TX");
      serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
    }
    return;
  }
  if (smpDistributeLocalEncKey_ && !smpLocalEncInfoAcked_) {
    return;
  }

  if (smpDistributeLocalEncKey_ && !smpLocalMasterIdSent_) {
    uint8_t masterId[kSmpMasterIdentificationLen] = {0};
    masterId[0] = kSmpCodeMasterIdentification;
    writeLe16(&masterId[1], smpLocalEdiv_);
    memcpy(&masterId[3], smpLocalRand_, sizeof(smpLocalRand_));
    if (queuePendingSmpL2capResponse(masterId, sizeof(masterId))) {
      smpLocalMasterIdSent_ = true;
      smpLocalMasterIdAcked_ = false;
      emitBleTrace("SMP_LOCAL_MASTER_ID_TX");
      serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
    }
    return;
  }
  if (smpDistributeLocalEncKey_ && !smpLocalMasterIdAcked_) {
    return;
  }

  if (smpDistributeLocalIdKey_ && !smpLocalIdInfoSent_) {
    uint8_t identityInfo[kSmpIdentityInformationLen] = {0};
    identityInfo[0] = kSmpCodeIdentityInformation;
    if (!getLocalIdentityIrk(&identityInfo[1])) {
      emitBleTrace("SMP_LOCAL_IRK_UNAVAILABLE");
      return;
    }
    if (queuePendingSmpL2capResponse(identityInfo, sizeof(identityInfo))) {
      smpLocalIdInfoSent_ = true;
      smpLocalIdInfoAcked_ = false;
      emitBleTrace("SMP_LOCAL_ID_INFO_TX");
      serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
    }
    return;
  }
  if (smpDistributeLocalIdKey_ && !smpLocalIdInfoAcked_) {
    return;
  }

  if (smpDistributeLocalIdKey_ && !smpLocalIdAddressSent_) {
    uint8_t localIdentityAddress[6] = {0};
    BleAddressType localIdentityType = BleAddressType::kPublic;
    if (!getLocalIdentityAddress(localIdentityAddress, &localIdentityType)) {
      emitBleTrace("SMP_LOCAL_ID_ADDR_UNAVAILABLE");
      return;
    }

    uint8_t identityAddress[kSmpIdentityAddressInformationLen] = {0};
    identityAddress[0] = kSmpCodeIdentityAddressInformation;
    identityAddress[1] =
        (localIdentityType == BleAddressType::kRandomStatic) ? 0x01U : 0x00U;
    memcpy(&identityAddress[2], localIdentityAddress,
           sizeof(localIdentityAddress));
    if (queuePendingSmpL2capResponse(identityAddress,
                                     sizeof(identityAddress))) {
      smpLocalIdAddressSent_ = true;
      smpLocalIdAddressAcked_ = false;
      emitBleTrace("SMP_LOCAL_ID_ADDR_TX");
      serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
    }
    return;
  }

  if (smpDistributeLocalIdKey_ && !smpLocalIdAddressAcked_) {
    return;
  }

  if (smpDistributeLocalSignKey_ && !smpLocalSignInfoSent_) {
    uint8_t signingInfo[kSmpSigningInformationLen] = {0};
    signingInfo[0] = kSmpCodeSigningInformation;
    memcpy(&signingInfo[1], smpLocalCsrk_, sizeof(smpLocalCsrk_));
    if (queuePendingSmpL2capResponse(signingInfo, sizeof(signingInfo))) {
      smpLocalSignInfoSent_ = true;
      smpLocalSignInfoAcked_ = false;
      emitBleTrace("SMP_LOCAL_SIGN_INFO_TX");
      serviceBackgroundConnection(kBleDeferredSmpResponseSpinLimit);
    }
  }
}

bool BleRadio::completeSmpKeyDistributionIfDone() {
  if (smpPairingState_ == kSmpPairingStateIdle ||
      !connectionEncSessionValid_ || !connectionEncRxEnabled_ ||
      !connectionEncTxEnabled_) {
    return false;
  }

  const bool peerEncDone =
      !smpExpectPeerEncKey_ ||
      (smpPeerLtkValid_ && !smpPeerLtkAwaitMasterId_);
  const bool peerIdDone =
      !smpExpectPeerIdKey_ ||
      (smpPeerIrkValid_ && smpPeerIdentityAddressValid_);
  const bool peerSignDone = !smpExpectPeerSignKey_ || smpPeerCsrkValid_;
  const bool localIdDone =
      !smpDistributeLocalIdKey_ ||
      (smpLocalIdInfoSent_ && smpLocalIdInfoAcked_ &&
       smpLocalIdAddressSent_ &&
       smpLocalIdAddressAcked_);
  const bool localEncDone =
      !smpDistributeLocalEncKey_ ||
      (smpLocalEncInfoSent_ && smpLocalEncInfoAcked_ &&
       smpLocalMasterIdSent_ && smpLocalMasterIdAcked_);
  const bool localSignDone =
      !smpDistributeLocalSignKey_ ||
      (smpLocalSignInfoSent_ && smpLocalSignInfoAcked_);
  if (!peerEncDone || !peerIdDone || !peerSignDone || !localEncDone ||
      !localIdDone || !localSignDone) {
    return false;
  }

  if (smpBondingRequested_) {
    BleBondRecord record{};
    if (!buildCurrentBondRecord(&record) || !persistBondRecord(record)) {
      emitBleTrace("SMP_BOND_PERSIST_FAILED");
    }
  }

  smpExpectPeerEncKey_ = false;
  smpExpectPeerIdKey_ = false;
  smpExpectPeerSignKey_ = false;
  smpDistributeLocalEncKey_ = false;
  smpDistributeLocalIdKey_ = false;
  smpDistributeLocalSignKey_ = false;
  smpLocalEncInfoSent_ = false;
  smpLocalEncInfoAcked_ = false;
  smpLocalMasterIdSent_ = false;
  smpLocalMasterIdAcked_ = false;
  smpLocalIdInfoSent_ = false;
  smpLocalIdInfoAcked_ = false;
  smpLocalIdAddressSent_ = false;
  smpLocalIdAddressAcked_ = false;
  smpLocalSignInfoSent_ = false;
  smpLocalSignInfoAcked_ = false;
  smpPairingState_ = kSmpPairingStateIdle;
  stopSmpTimer();
  clearSmpBackoffForCurrentPeer();
  armConnectionResyncWindow(kBleConnResyncExtraListenUs, 16U);
  emitBleTrace("SMP_KEY_DIST_DONE");
  return true;
}

void BleRadio::clearSmpPairingState() {
  smpPairingState_ = kSmpPairingStateIdle;
  memset(smpPairingReq_, 0, sizeof(smpPairingReq_));
  memset(smpPairingRsp_, 0, sizeof(smpPairingRsp_));
  memset(smpPeerConfirm_, 0, sizeof(smpPeerConfirm_));
  memset(smpPeerRandom_, 0, sizeof(smpPeerRandom_));
  memset(smpLocalRandom_, 0, sizeof(smpLocalRandom_));
  memset(smpLocalConfirm_, 0, sizeof(smpLocalConfirm_));
  memset(smpStk_, 0, sizeof(smpStk_));
  smpStkValid_ = false;
  smpBondingRequested_ = false;
  smpLocalInitiator_ = false;
  smpSecurityPolicyLatched_ = false;
  smpExpectPeerEncKey_ = false;
  smpExpectPeerIdKey_ = false;
  smpExpectPeerSignKey_ = false;
  smpDistributeLocalEncKey_ = false;
  smpDistributeLocalIdKey_ = false;
  smpDistributeLocalSignKey_ = false;
  smpLocalLtkValid_ = false;
  smpLocalEncInfoSent_ = false;
  smpLocalEncInfoAcked_ = false;
  smpLocalMasterIdSent_ = false;
  smpLocalMasterIdAcked_ = false;
  smpLocalIdInfoSent_ = false;
  smpLocalIdInfoAcked_ = false;
  smpLocalIdAddressSent_ = false;
  smpLocalIdAddressAcked_ = false;
  smpLocalSignInfoSent_ = false;
  smpLocalSignInfoAcked_ = false;
  if (smpPendingUserAction_ == BleSmpUserAction::PasskeyInput) {
    smpPendingUserPasskeyRequestId_ = 0UL;
  }
  smpPendingUserPasskey_ = false;
  smpPendingUserPasskeyMatchRequest_ = false;
  smpPendingUserAction_ = BleSmpUserAction::None;
  smpPendingUserPasskeyCallbackDispatched_ = false;
  smpPeerConfirmReceived_ = false;
  smpLegacyConfirmDeferred_ = false;
  smpDeferredPairingFailed_ = false;
  smpDeferredPairingFailedReason_ = 0U;
  smpPendingUserPasskeyValue_ = 0UL;
  smpPairingLocalIoCapabilities_ = kSmpIoCapNoInputNoOutput;
  smpPeerLtkValid_ = false;
  smpPeerLtkAwaitMasterId_ = false;
  smpPeerIrkValid_ = false;
  smpPeerIdentityAddressValid_ = false;
  smpPeerIdentityAddressRandom_ = false;
  smpPeerCsrkValid_ = false;
  smpLocalCsrkValid_ = false;
  smpPeerSignCounterValid_ = false;
  memset(smpLocalLtk_, 0, sizeof(smpLocalLtk_));
  memset(smpLocalRand_, 0, sizeof(smpLocalRand_));
  smpLocalEdiv_ = 0U;
  memset(smpPeerLtk_, 0, sizeof(smpPeerLtk_));
  memset(smpPeerIrk_, 0, sizeof(smpPeerIrk_));
  memset(smpPeerIdentityAddress_, 0, sizeof(smpPeerIdentityAddress_));
  memset(smpPeerCsrk_, 0, sizeof(smpPeerCsrk_));
  memset(smpLocalCsrk_, 0, sizeof(smpLocalCsrk_));
  smpPeerSignCounter_ = 0U;
  smpLocalSignCounter_ = 0U;
  smpPeerSignCounterReservedUntil_ = 0U;
  smpLocalSignCounterReservedUntil_ = 0U;
  memset(smpEncReqRand_, 0, sizeof(smpEncReqRand_));
  smpEncReqEdiv_ = 0U;
  smpKeySize_ = 16U;
  if (!smpFixedPasskeyValid_) {
    smpFixedPasskeyValue_ = 0UL;
  }
  smpPendingUserPasskeyRequestId_ = 0UL;
  smpPairingPasskeyValid_ = false;
  smpPairingPasskeyValue_ = 0UL;
  smpSecureConnectionsActive_ = false;
  smpSecureConnectionsPeerPublicKeyValid_ = false;
  smpSecureConnectionsDhKeyReady_ = false;
  smpSecureConnectionsLocalConfirmReady_ = false;
  smpSecureConnectionsCheckValuesReady_ = false;
  smpSecureConnectionsPublicKeySent_ = false;
  smpSecureConnectionsConfirmSent_ = false;
  smpSecureConnectionsRandomSent_ = false;
  smpSecureConnectionsDhKeyCheckSent_ = false;
  smpSecureConnectionsReceivedDhKeyCheckValid_ = false;
  smpSecureConnectionsWireFormatBigEndian_ = false;
  smpSecureConnectionsDeferredPublicKey_ = false;
  smpSecureConnectionsDeferredConfirm_ = false;
  smpSecureConnectionsDeferredRandom_ = false;
  smpSecureConnectionsDeferredDhKeyCheck_ = false;
  smpSecureConnectionsPasskeyValid_ = false;
  smpSecureConnectionsPasskeyRound_ = 0U;
  smpSecureConnectionsLocalKeypairTimeUs_ = 0U;
  smpSecureConnectionsDhKeyTimeUs_ = 0U;
  smpSecureConnectionsCheckValuesTimeUs_ = 0U;
  smpSecureConnectionsPasskeyValue_ = 0UL;
  smpSecureConnectionsCooperateHookCount_ = 0U;
  smpSecureConnectionsBackgroundServiceCount_ = 0U;
  memset(smpSecureConnectionsPeerPublicKey_, 0,
         sizeof(smpSecureConnectionsPeerPublicKey_));
  smpSecureConnectionsLastPublicKeyDecodeFailed_ = false;
  smpSecureConnectionsLastPublicKeyDecodeLength_ = 0U;
  memset(smpSecureConnectionsLastPublicKeyWire_, 0,
         sizeof(smpSecureConnectionsLastPublicKeyWire_));
  memset(smpSecureConnectionsDhKey_, 0, sizeof(smpSecureConnectionsDhKey_));
  memset(smpSecureConnectionsMacKey_, 0,
         sizeof(smpSecureConnectionsMacKey_));
  memset(smpSecureConnectionsLocalDhKeyCheck_, 0,
         sizeof(smpSecureConnectionsLocalDhKeyCheck_));
  memset(smpSecureConnectionsPeerDhKeyCheck_, 0,
         sizeof(smpSecureConnectionsPeerDhKeyCheck_));
  memset(smpSecureConnectionsReceivedDhKeyCheck_, 0,
         sizeof(smpSecureConnectionsReceivedDhKeyCheck_));
  bondKeyPrimedForConnection_ = false;
  connectionBondedEncryptionRequested_ = false;

  // OOB data is a user-supplied pairing configuration, like a fixed PIN. Keep
  // it across transient SMP state resets so requestPairing() can advertise it.
}

void BleRadio::clearEncryptionState() {
  connectionEncSessionValid_ = false;
  connectionEncRxEnabled_ = false;
  connectionEncTxEnabled_ = false;
  connectionEncAuthenticated_ = false;
  connectionEncSecureConnections_ = false;
  connectionEncStartReqPending_ = false;
  connectionEncStartReqTxPending_ = false;
  connectionEncAwaitingStartRsp_ = false;
  connectionEncEnableTxOnNextEvent_ = false;
  connectionEncRxCounter_ = 0ULL;
  connectionEncTxCounter_ = 0ULL;
  connectionEncKeyDerivationPending_ = false;
  memset(connectionEncSkd_, 0, sizeof(connectionEncSkd_));
  memset(connectionEncSessionKey_, 0, sizeof(connectionEncSessionKey_));
  memset(connectionEncSessionKeyAlt_, 0, sizeof(connectionEncSessionKeyAlt_));
  connectionEncAltKeyValid_ = false;
  connectionEncRxDirection_ = 1U;
  connectionEncTxDirection_ = 0U;
  memset(connectionEncIv_, 0, sizeof(connectionEncIv_));
  connectionLastTxWasEncrypted_ = false;
  connectionLastTxEncryptedLength_ = 0U;
  memset(connectionLastTxEncryptedPayload_, 0, sizeof(connectionLastTxEncryptedPayload_));
  connectionEncPrecomputedStartRspValid_ = false;
  memset(connectionEncPrecomputedStartRsp_, 0, sizeof(connectionEncPrecomputedStartRsp_));
  connectionEncPrecomputedStartRspTxValid_ = false;
  connectionEncPrecomputedStartRspTxCounter_ = 0ULL;
  memset(connectionEncPrecomputedStartRspTx_, 0, sizeof(connectionEncPrecomputedStartRspTx_));
}

void BleRadio::clearSmpSecurityState() {
  stopSmpTimer();
  const bool retireSecureConnectionsKeypair = smpSecureConnectionsActive_;
  const bool captureScState =
      smpSecureConnectionsActive_ ||
      smpSecureConnectionsPeerPublicKeyValid_ ||
      smpSecureConnectionsPublicKeySent_ ||
      smpSecureConnectionsConfirmSent_ ||
      smpSecureConnectionsRandomSent_ ||
      smpSecureConnectionsDhKeyReady_ ||
      smpSecureConnectionsCheckValuesReady_ ||
      smpSecureConnectionsDeferredPublicKey_ ||
      smpSecureConnectionsDeferredConfirm_ ||
      smpSecureConnectionsDeferredRandom_ ||
      smpSecureConnectionsDeferredDhKeyCheck_ ||
      smpSecureConnectionsLastPublicKeyDecodeFailed_ ||
      (smpPairingState_ != kSmpPairingStateIdle);
  if (captureScState) {
    latchSecureConnectionsDebugState();
  }
  if (connectionPendingL2capTxFragmentActive_ &&
      connectionPendingL2capTxFragmentLength_ >= kBleL2capHeaderLen &&
      readLe16(&connectionPendingL2capTxFragmentPayload_[2]) ==
          kBleL2capCidSmp) {
    clearPendingL2capTxFragment();
  }
  clearSmpPairingState();
  if (retireSecureConnectionsKeypair) {
    invalidateSecureConnectionsKeypairAndOob(
        smpSecureConnectionsLocalKeyReady_,
        smpSecureConnectionsLocalKeyInProgress_,
        smpSecureConnectionsPrivateKey_, smpSecureConnectionsPublicKey_,
        smpOobDataLocalValid_, smpOobDataRemoteValid_, smpOobLocal_,
        smpOobRemote_);
  }
}

void BleRadio::clearConnectionSecurityState() {
  connectionMissingKeyTerminatePending_ = false;
  clearSmpSecurityState();
  clearPendingL2capTxFragment();
  clearEncryptionState();
}

bool BleRadio::buildCurrentBondRecord(BleBondRecord* outRecord) const {
  if (outRecord == nullptr) {
    return false;
  }

  const uint8_t* projectedLtk = nullptr;
  const uint8_t* projectedRand = nullptr;
  uint16_t projectedEdiv = 0U;
  uint8_t zeroRand[8] = {0};
  if (smpSecureConnectionsActive_) {
    if (!smpStkValid_) {
      return false;
    }
    projectedLtk = smpStk_;
    projectedRand = zeroRand;
  } else if (connectionRole_ == BleConnectionRole::kPeripheral) {
    if (!smpLocalLtkValid_) {
      return false;
    }
    projectedLtk = smpLocalLtk_;
    projectedRand = smpLocalRand_;
    projectedEdiv = smpLocalEdiv_;
  } else if (connectionRole_ == BleConnectionRole::kCentral) {
    if (!smpPeerLtkValid_ || smpPeerLtkAwaitMasterId_) {
      return false;
    }
    projectedLtk = smpPeerLtk_;
    projectedRand = smpEncReqRand_;
    projectedEdiv = smpEncReqEdiv_;
  } else {
    return false;
  }

  BleBondRecord record{};
  memcpy(record.peerAddress, connectionPeerAddress_, sizeof(record.peerAddress));
  record.peerAddressRandom = connectionPeerAddressRandom_ ? 1U : 0U;
  if (smpPeerIdentityAddressValid_) {
    memcpy(record.peerIdentityAddress, smpPeerIdentityAddress_,
           sizeof(record.peerIdentityAddress));
    record.peerIdentityAddressRandom = smpPeerIdentityAddressRandom_ ? 1U : 0U;
  }
  BleAddressType localIdentityType = BleAddressType::kPublic;
  if (!getLocalIdentityAddress(record.localAddress, &localIdentityType)) {
    return false;
  }
  record.localAddressRandom =
      (localIdentityType == BleAddressType::kRandomStatic) ? 1U : 0U;
  memcpy(record.ltk, projectedLtk, sizeof(record.ltk));
  reduceSmpEncryptionKey(record.ltk);
  memcpy(record.rand, projectedRand, sizeof(record.rand));
  if (smpPeerIrkValid_) {
    memcpy(record.peerIrk, smpPeerIrk_, sizeof(record.peerIrk));
    record.peerIrkValid = 1U;
  }
  if (smpPeerCsrkValid_) {
    memcpy(record.peerCsrk, smpPeerCsrk_, sizeof(record.peerCsrk));
    record.peerCsrkValid = 1U;
    record.peerSignCounter = smpPeerSignCounterReservedUntil_;
    record.peerSignCounterValid = smpPeerSignCounterValid_ ? 1U : 0U;
  }
  if (smpLocalCsrkValid_) {
    memcpy(record.localCsrk, smpLocalCsrk_, sizeof(record.localCsrk));
    record.localCsrkValid = 1U;
    record.localSignCounter = smpLocalSignCounterReservedUntil_;
  }
  record.ediv = projectedEdiv;
  record.keySize = (smpKeySize_ >= 7U && smpKeySize_ <= 16U) ? smpKeySize_ : 16U;
  memset(record.reserved, 0, sizeof(record.reserved));
  record.signingReserved = 0U;
  if (currentPairingIsAuthenticated()) {
    record.reserved[0] = kBleBondRecordFlagAuthenticated;
  }
  if (smpSecureConnectionsActive_) {
    record.reserved[0] |= kBleBondRecordFlagSecureConnections;
  }
  if (!isBondRecordUsable(record)) {
    return false;
  }
  *outRecord = record;
  return true;
}

void BleRadio::prefetchConnectionSecurityMaterial(uint32_t spinLimit) {
  if (!initialized_ || radio_ == nullptr || !connected_) {
    return;
  }

  const uint32_t seedBase =
      micros() ^ connectionAccessAddress_ ^
      static_cast<uint32_t>(connectionEventCounter_ << 16U) ^ connectionCrcInit_;

  if (smpPrefetchedLocalRandomValid_ &&
      connectionPrefetchedEncMaterialValid_ &&
      smpPrefetchedLegacyKeyValid_) {
    return;
  }

  uint8_t material[54] = {};
  if (!fillBleSecurityRandomBytes(material, sizeof(material),
                                  seedBase ^ 0x13579BDFUL, spinLimit)) {
    ++encDebug_.encRandomFallbackCount;
    return;
  }

  ++encDebug_.encRandomHardwareCount;
  if (!smpPrefetchedLocalRandomValid_) {
    memcpy(smpPrefetchedLocalRandom_, &material[0],
           sizeof(smpPrefetchedLocalRandom_));
    smpPrefetchedLocalRandomValid_ = true;
  }
  if (!connectionPrefetchedEncMaterialValid_) {
    memcpy(connectionPrefetchedSkds_, &material[16],
           sizeof(connectionPrefetchedSkds_));
    memcpy(connectionPrefetchedIvs_, &material[24],
           sizeof(connectionPrefetchedIvs_));
    connectionPrefetchedEncMaterialValid_ = true;
  }
  if (!smpPrefetchedLegacyKeyValid_) {
    memcpy(smpPrefetchedLegacyKeyMaterial_, &material[28],
           sizeof(smpPrefetchedLegacyKeyMaterial_));
    smpPrefetchedLegacyKeyValid_ = true;
  }
  ++encDebug_.encRandomPrefetchFillCount;
}

bool BleRadio::isBondRecordUsable(const BleBondRecord& record) const {
  return bleBondRecordLooksSane(record);
}

bool BleRadio::loadBondRecordFromPersistence() {
  if (bondStorageLoaded_) {
    return bondRecordValid_;
  }

  bondLocalIdentityMigrationPending_ = false;
  activeBondId_ = kBleInvalidBondId;
  activeBondGeneration_ = 0U;
  memset(&bondRecord_, 0, sizeof(bondRecord_));
  bondRecordValid_ = false;

  // The legacy callback API remains an explicitly single-bond backend.
  if (bondLoadCallback_ != nullptr || bondSaveCallback_ != nullptr) {
    BleBondRecord loaded{};
    const bool callbackRecordLoaded =
        bondLoadCallback_ != nullptr &&
        bondLoadCallback_(&loaded, bondCallbackContext_) &&
        isBondRecordUsable(loaded);
    bool loadedFromStore = callbackRecordLoaded;
    if (callbackRecordLoaded) {
      BleBondRecord signingAuthority{};
      const bool matchingAuthority =
          readFlashBondRecord(&signingAuthority) &&
          isBondRecordUsable(signingAuthority) &&
          loaded.peerAddressRandom == signingAuthority.peerAddressRandom &&
          memcmp(loaded.peerAddress, signingAuthority.peerAddress,
                 sizeof(loaded.peerAddress)) == 0 &&
          memcmp(loaded.ltk, signingAuthority.ltk, sizeof(loaded.ltk)) == 0;
      memset(loaded.peerCsrk, 0, sizeof(loaded.peerCsrk));
      memset(loaded.localCsrk, 0, sizeof(loaded.localCsrk));
      loaded.peerSignCounter = 0U;
      loaded.localSignCounter = 0U;
      loaded.peerCsrkValid = 0U;
      loaded.localCsrkValid = 0U;
      loaded.peerSignCounterValid = 0U;
      loaded.signingReserved = 0U;
      if (matchingAuthority) {
        memcpy(loaded.peerCsrk, signingAuthority.peerCsrk,
               sizeof(loaded.peerCsrk));
        memcpy(loaded.localCsrk, signingAuthority.localCsrk,
               sizeof(loaded.localCsrk));
        loaded.peerSignCounter = signingAuthority.peerSignCounter;
        loaded.localSignCounter = signingAuthority.localSignCounter;
        loaded.peerCsrkValid = signingAuthority.peerCsrkValid;
        loaded.localCsrkValid = signingAuthority.localCsrkValid;
        loaded.peerSignCounterValid =
            signingAuthority.peerSignCounterValid;
      }
    }
    if (!loadedFromStore) {
      loadedFromStore = readFlashBondRecord(&loaded) &&
                        isBondRecordUsable(loaded);
    }
    if (!loadedFromStore) {
      loadedFromStore = readRetainedBondRecord(&loaded) &&
                        isBondRecordUsable(loaded);
    }
    if (loadedFromStore) {
      memcpy(&bondRecord_, &loaded, sizeof(bondRecord_));
      bondRecordValid_ = true;
      activeBondId_ = 0U;
      emitBleTrace("BOND_CALLBACK_LOADED_CAPACITY_1");
    }
    bondStorageLoaded_ = true;
    return bondRecordValid_;
  }

  if (!migrateLegacyBondStorageToDatabase()) {
    emitBleTrace("BOND_DB_MIGRATION_FAILED");
    return false;
  }
  bondStorageLoaded_ = true;
  for (uint8_t bondId = 0U; bondId < kBleMaxBonds; ++bondId) {
    BleBondDbPayload payload{};
    if (!readActiveBleBondDbPayload(bondId, &payload)) {
      continue;
    }
    bondRuntimeLastUsed_[bondId] = payload.peerState.lastUsedGeneration;
    if (bleBondDbGenerationNewer(bondRuntimeLastUsed_[bondId],
                                 bondRuntimeUseCounter_)) {
      bondRuntimeUseCounter_ = bondRuntimeLastUsed_[bondId];
    }
  }
  const uint8_t newest = mostRecentBleBondDbId();
  if (newest != kBleInvalidBondId &&
      loadBondProjection(newest, BleConnectionRole::kNone, &bondRecord_,
                         &activeBondGeneration_)) {
    activeBondId_ = newest;
    bondRecordValid_ = true;
    emitBleTrace("BOND_DB_LOADED");
  }
  return bondRecordValid_;
}

bool BleRadio::loadBondProjection(uint8_t bondId, BleConnectionRole role,
                                  BleBondRecord* outRecord,
                                  uint32_t* outGeneration) const {
  BleBondDbPayload payload{};
  uint32_t generation = 0U;
  if (bondId >= kBleMaxBonds ||
      !readActiveBleBondDbPayload(bondId, &payload, &generation) ||
      !bleBondDbCoreToRecord(payload.core, role, outRecord)) {
    return false;
  }
  if (outGeneration != nullptr) {
    *outGeneration = generation;
  }
  return true;
}

bool BleRadio::persistActiveBondDatabaseRecord(bool includeCccdState) {
  if (activeBondId_ >= kBleMaxBonds || !bondRecordValid_ ||
      !isBondRecordUsable(bondRecord_) || bleRunningInIsr()) {
    return false;
  }
  BleBondDbPayload payload{};
  BleBondDbPayload existing{};
  const bool hasExisting =
      readActiveBleBondDbPayload(activeBondId_, &existing);
  const bool samePeer =
      hasExisting && bleBondDbIdentityMatches(existing.core, bondRecord_);
  bool sameSecurityMaterial = false;
  if (samePeer) {
    BleBondRecord projected{};
    sameSecurityMaterial =
        bleBondDbCoreToRecord(existing.core, connectionRole_, &projected) &&
        memcmp(projected.ltk, bondRecord_.ltk, sizeof(projected.ltk)) == 0 &&
        memcmp(projected.rand, bondRecord_.rand, sizeof(projected.rand)) == 0 &&
        projected.ediv == bondRecord_.ediv;
    if (sameSecurityMaterial) {
      memcpy(&payload, &existing, sizeof(payload));
    }
  }

  bleBondDbUpdateCoreFromRecord(bondRecord_, connectionRole_, &payload.core,
                                sameSecurityMaterial);
  const bool runtimeLegacyPairing =
      smpBondingRequested_ && !smpSecureConnectionsActive_;
  if (runtimeLegacyPairing && smpLocalLtkValid_) {
    memcpy(payload.core.localOrSharedKey.ltk, smpLocalLtk_,
           sizeof(payload.core.localOrSharedKey.ltk));
    memcpy(payload.core.localOrSharedKey.rand, smpLocalRand_,
           sizeof(payload.core.localOrSharedKey.rand));
    payload.core.localOrSharedKey.ediv = smpLocalEdiv_;
    payload.core.keyValidityFlags |= kBleBondDbKeyLocalOrSharedValid;
  }
  if (runtimeLegacyPairing && smpPeerLtkValid_ &&
      !smpPeerLtkAwaitMasterId_) {
    memcpy(payload.core.peerLegacyKey.ltk, smpPeerLtk_,
           sizeof(payload.core.peerLegacyKey.ltk));
    memcpy(payload.core.peerLegacyKey.rand, smpEncReqRand_,
           sizeof(payload.core.peerLegacyKey.rand));
    payload.core.peerLegacyKey.ediv = smpEncReqEdiv_;
    payload.core.keyValidityFlags |= kBleBondDbKeyPeerLegacyValid;
  }
  if (runtimeLegacyPairing) {
    payload.core.keySize =
        (smpKeySize_ >= 7U && smpKeySize_ <= 16U) ? smpKeySize_ : 16U;
    payload.core.keyValidityFlags &=
        static_cast<uint8_t>(~kBleBondDbKeySharedSecureConnections);
    if ((payload.core.keyValidityFlags &
         (kBleBondDbKeyLocalOrSharedValid |
          kBleBondDbKeyPeerLegacyValid)) ==
        (kBleBondDbKeyLocalOrSharedValid |
         kBleBondDbKeyPeerLegacyValid)) {
      payload.core.keyValidityFlags &= static_cast<uint8_t>(
          ~kBleBondDbKeyLegacyProjectionUnknown);
    }
  }
  if (!sameSecurityMaterial) {
    memset(&payload.peerState, 0, sizeof(payload.peerState));
    payload.peerState.schemaFingerprint = gattSchemaFingerprint_;
    payload.peerState.serviceChangedGeneration = 1U;
  }
  uint32_t lastUsed = bondRuntimeLastUsed_[activeBondId_];
  if (lastUsed == 0U) {
    lastUsed = nextBleBondDbGeneration();
    bondRuntimeLastUsed_[activeBondId_] = lastUsed;
  }
  payload.peerState.lastUsedGeneration = lastUsed;

  if (includeCccdState) {
    payload.peerState.schemaFingerprint = bondedGattSchemaFingerprint_;
    payload.peerState.serviceChangedGeneration =
        bondedServiceChangedGeneration_ != 0U
            ? bondedServiceChangedGeneration_
            : 1U;
    payload.peerState.serviceChangedStartHandle =
        bondedServiceChangedStartHandle_;
    payload.peerState.serviceChangedEndHandle =
        bondedServiceChangedEndHandle_;
    payload.peerState.fixedCccdFlags = 0U;
    if (connectionServiceChangedIndicationsEnabled_) {
      payload.peerState.fixedCccdFlags |= kBleCccdFixedServiceChanged;
    }
    if (connectionBatteryNotificationsEnabled_) {
      payload.peerState.fixedCccdFlags |= kBleCccdFixedBattery;
    }
    payload.peerState.fixedCccdFlags |= static_cast<uint8_t>(
        (connectionGattClientSupportedFeatures_ & 0x07U) << 2U);
    payload.peerState.cccdCount = 0U;
    memset(payload.peerState.cccdValues, 0,
           sizeof(payload.peerState.cccdValues));
    memset(payload.peerState.cccdHandles, 0,
           sizeof(payload.peerState.cccdHandles));
    for (uint8_t i = 0U; i < customGattCharacteristicCount_ &&
                         payload.peerState.cccdCount <
                             kCustomGattMaxCharacteristics;
         ++i) {
      const BleCustomCharacteristicState& ch = customGattCharacteristics_[i];
      if (ch.cccdHandle == 0U || ch.cccdValue == 0U) {
        continue;
      }
      const uint8_t index = payload.peerState.cccdCount++;
      payload.peerState.cccdHandles[index] = ch.cccdHandle;
      bleBondDbSetCccdValue(&payload.peerState, index,
                            static_cast<uint8_t>(ch.cccdValue));
    }
  }

  uint32_t generation = 0U;
  if (!writeBleBondDbReplica(activeBondId_, kBleBondDbActiveMagic, &payload,
                             &generation)) {
    return false;
  }
  activeBondGeneration_ = generation;
  for (uint8_t bondId = 0U; bondId < kBleMaxBonds; ++bondId) {
    if (bondId == activeBondId_) {
      continue;
    }
    BleBondDbPayload duplicate{};
    if (readActiveBleBondDbPayload(bondId, &duplicate) &&
        bleBondDbIdentityMatches(duplicate.core, bondRecord_)) {
      (void)eraseBleBondDbRecord(bondId);
      bondRuntimeLastUsed_[bondId] = 0U;
      emitBleTrace("BOND_DB_DUPLICATE_TOMBSTONED");
    }
  }
  clearRetainedBondBlob();
  clearRetainedCccdBondBlob();
  emitBleTrace("BOND_DB_COMMITTED");
  return true;
}

bool BleRadio::updateActiveBondLegacyKeyTuple(
    bool locallyDistributed, const uint8_t ltk[16], const uint8_t rand[8],
    uint16_t ediv, uint8_t keySize) {
  if (activeBondId_ >= kBleMaxBonds || ltk == nullptr || rand == nullptr ||
      keySize < 7U || keySize > 16U ||
      bleBondDbBytesAllZero(ltk, 16U)) {
    return false;
  }
  BleBondDbPayload payload{};
  if (!readActiveBleBondDbPayload(activeBondId_, &payload) ||
      (payload.core.securityFlags &
       kBleBondDbSecuritySecureConnections) != 0U) {
    return false;
  }
  BleBondDbLongTermKey& key = locallyDistributed
                                  ? payload.core.localOrSharedKey
                                  : payload.core.peerLegacyKey;
  memcpy(key.ltk, ltk, sizeof(key.ltk));
  memcpy(key.rand, rand, sizeof(key.rand));
  key.ediv = ediv;
  payload.core.keySize = keySize;
  payload.core.keyValidityFlags |=
      locallyDistributed ? kBleBondDbKeyLocalOrSharedValid
                         : kBleBondDbKeyPeerLegacyValid;
  if ((payload.core.keyValidityFlags &
       (kBleBondDbKeyLocalOrSharedValid |
        kBleBondDbKeyPeerLegacyValid)) ==
      (kBleBondDbKeyLocalOrSharedValid |
       kBleBondDbKeyPeerLegacyValid)) {
    payload.core.keyValidityFlags &=
        static_cast<uint8_t>(~kBleBondDbKeyLegacyProjectionUnknown);
  }
  uint32_t generation = 0U;
  if (!writeBleBondDbReplica(activeBondId_, kBleBondDbActiveMagic, &payload,
                             &generation)) {
    return false;
  }
  activeBondGeneration_ = generation;
  return loadBondProjection(activeBondId_, connectionRole_, &bondRecord_,
                            &activeBondGeneration_);
}

bool BleRadio::persistBondRecord(const BleBondRecord& record) {
  if (!isBondRecordUsable(record)) {
    return false;
  }

  const bool sameBond =
      bondRecordValid_ &&
      bondRecord_.peerAddressRandom == record.peerAddressRandom &&
      bondRecord_.peerIdentityAddressRandom ==
          record.peerIdentityAddressRandom &&
      memcmp(bondRecord_.peerAddress, record.peerAddress,
             sizeof(record.peerAddress)) == 0 &&
      memcmp(bondRecord_.peerIdentityAddress, record.peerIdentityAddress,
             sizeof(record.peerIdentityAddress)) == 0 &&
      memcmp(bondRecord_.ltk, record.ltk, sizeof(record.ltk)) == 0 &&
      memcmp(bondRecord_.rand, record.rand, sizeof(record.rand)) == 0 &&
      bondRecord_.ediv == record.ediv;
  if (!sameBond) {
    resetBondedServiceChangedState(gattSchemaFingerprint_);
  }

  const bool retentionOk = writeRetainedBondRecord(record);
  const bool deferStorage = connected_ || bleRunningInIsr();
  const bool callbackMode =
      bondLoadCallback_ != nullptr || bondSaveCallback_ != nullptr;
  bool callbackStored = false;
  if (callbackMode) {
    bool stored = retentionOk;
    if (!deferStorage) {
      const bool flashOk = writeFlashBondRecord(record);
      const bool callbackOk =
          bondSaveCallback_ == nullptr ||
          bondSaveCallback_(&record, bondCallbackContext_);
      stored = stored || (flashOk && callbackOk);
    } else {
      bondFlashPersistPending_ = true;
      bondSigningCounterPersistPending_ = bondSaveCallback_ != nullptr;
    }
    if (!stored && !deferStorage) {
      return false;
    }
    activeBondId_ = 0U;
    callbackStored = stored || deferStorage;
  } else {
    uint8_t selected = findBleBondDbIdForRecord(record);
    if (selected == kBleInvalidBondId && activeBondId_ < kBleMaxBonds) {
      BleBondDbPayload active{};
      if (!readActiveBleBondDbPayload(activeBondId_, &active) ||
          bleBondDbIdentityMatches(active.core, record)) {
        selected = activeBondId_;
      }
    }
    if (selected == kBleInvalidBondId) {
      for (uint8_t bondId = 0U; bondId < kBleMaxBonds; ++bondId) {
        if (!readActiveBleBondDbPayload(bondId, nullptr)) {
          selected = bondId;
          break;
        }
      }
    }
    if (selected == kBleInvalidBondId) {
      uint32_t oldestUse = 0U;
      for (uint8_t bondId = 0U; bondId < kBleMaxBonds; ++bondId) {
        if (connected_ && bondId == activeBondId_) {
          continue;
        }
        const uint32_t use = bondRuntimeLastUsed_[bondId];
        if (selected == kBleInvalidBondId || use == 0U ||
            (oldestUse != 0U && bleBondDbGenerationNewer(oldestUse, use))) {
          selected = bondId;
          oldestUse = use;
        }
      }
    }
    if (selected == kBleInvalidBondId) {
      return false;
    }
    if (selected != activeBondId_) {
      resetBondedServiceChangedState(gattSchemaFingerprint_);
    }
    activeBondId_ = selected;
    ++bondRuntimeUseCounter_;
    if (bondRuntimeUseCounter_ == 0U) {
      bondRuntimeUseCounter_ = 1U;
    }
    bondRuntimeLastUsed_[selected] = bondRuntimeUseCounter_;
  }

  memcpy(&bondRecord_, &record, sizeof(bondRecord_));
  bondRecordValid_ = true;
  bondStorageLoaded_ = true;
  if (deferStorage) {
    bondFlashPersistPending_ = true;
    cccdFlashPersistPending_ = true;
    emitBleTrace("BOND_DEFERRED");
    return retentionOk || bondFlashPersistPending_;
  }
  if (callbackMode) {
    if (callbackStored) {
      bondFlashPersistPending_ = false;
      emitBleTrace("BOND_CALLBACK_SAVED_CAPACITY_1");
    }
    return callbackStored;
  }
  const bool stored = persistActiveBondDatabaseRecord(true);
  if (stored) {
    bondFlashPersistPending_ = false;
    cccdFlashPersistPending_ = false;
    emitBleTrace("BOND_SAVED");
  }
  return stored;
}

bool BleRadio::flushDeferredBondStorage() {
  bool ok = true;
  if ((bondFlashPersistPending_ || cccdFlashPersistPending_) &&
      bondRecordValid_) {
    const bool callbackMode =
        bondLoadCallback_ != nullptr || bondSaveCallback_ != nullptr;
    const bool flashOk = callbackMode
                             ? writeFlashBondRecord(bondRecord_)
                             : persistActiveBondDatabaseRecord(true);
    ok = flashOk && ok;
    if (flashOk) {
      bondFlashPersistPending_ = false;
      if (!callbackMode) {
        cccdFlashPersistPending_ = false;
      }
      emitBleTrace("BOND_FLASH_FLUSHED");
    }
  }
  if (bondSigningCounterPersistPending_) {
    bool callbackOk = true;
    if (bondSaveCallback_ != nullptr && bondRecordValid_) {
      callbackOk = bondSaveCallback_(&bondRecord_, bondCallbackContext_);
    }
    ok = callbackOk && ok;
    if (callbackOk && !bondFlashPersistPending_) {
      bondSigningCounterPersistPending_ = false;
      emitBleTrace("BOND_SIGN_COUNTERS_FLUSHED");
    }
  }
  if (cccdFlashPersistPending_ && !bondFlashPersistPending_) {
    const bool cccdOk = persistBondedCccdState(true);
    ok = cccdOk && ok;
    if (cccdOk) {
      cccdFlashPersistPending_ = false;
      emitBleTrace("CCCD_FLASH_FLUSHED");
    }
  }
  return ok;
}

bool BleRadio::clearPersistentBondRecord() {
  clearRetainedBondBlob();
  bondKeyPrimedForConnection_ = false;
  bondIdentityMatchedForConnection_ = false;
  bondFlashPersistPending_ = false;
  cccdFlashPersistPending_ = false;
  bondLocalIdentityMigrationPending_ = false;
  bondLocalIdentityMigrationRetryAtUs_ = 0U;
  bondSigningCounterPersistPending_ = false;
  clearRetainedCccdBondBlob();
  resetBondedServiceChangedState();
  const bool callbackMode =
      bondLoadCallback_ != nullptr || bondSaveCallback_ != nullptr;
  const bool databaseOk = callbackMode ? true : clearBleBondDbRecords();
  const bool cccdOk = clearFlashCccdBondRecord();
  const bool flashOk = clearFlashBondRecord();
  bool callbackOk = true;
  if (bondClearCallback_ != nullptr) {
    callbackOk = bondClearCallback_(bondCallbackContext_);
  }
  emitBleTrace("BOND_CLEARED");
  return databaseOk && flashOk && cccdOk && callbackOk;
}

bool BleRadio::primeBondForCurrentPeer() {
  bondKeyPrimedForConnection_ = false;
  bondIdentityMatchedForConnection_ = false;
  activeBondMeetsSecurityPolicy_ = false;
  activeBondUseNotedForConnection_ = false;
  smpPeerCsrkValid_ = false;
  smpLocalCsrkValid_ = false;
  smpPeerSignCounterValid_ = false;
  memset(smpPeerCsrk_, 0, sizeof(smpPeerCsrk_));
  memset(smpLocalCsrk_, 0, sizeof(smpLocalCsrk_));
  smpPeerSignCounter_ = 0U;
  smpLocalSignCounter_ = 0U;
  smpPeerSignCounterReservedUntil_ = 0U;
  smpLocalSignCounterReservedUntil_ = 0U;
  if (!bondStorageLoaded_) {
    loadBondRecordFromPersistence();
  }
  uint8_t localIdentityAddress[6] = {0};
  BleAddressType localIdentityType = BleAddressType::kPublic;
  if (!getLocalIdentityAddress(localIdentityAddress, &localIdentityType)) {
    memcpy(localIdentityAddress, address_, sizeof(localIdentityAddress));
    localIdentityType = addressType_;
  }
  const uint8_t localIdentityRandom =
      (localIdentityType == BleAddressType::kRandomStatic) ? 0x01U : 0x00U;
  uint8_t currentLocalIrk[16] = {0};
  const bool currentLocalIrkValid = getLocalIdentityIrk(currentLocalIrk);
  uint8_t legacyLocalRoot[16] = {0};
  Ficr::identityRoot(legacyLocalRoot);
  uint8_t selectedBondId = kBleInvalidBondId;
  uint8_t directMatches = 0U;
  uint8_t identityMatches = 0U;
  uint8_t rpaMatches = 0U;
  const bool callbackMode =
      bondLoadCallback_ != nullptr || bondSaveCallback_ != nullptr;
  if (callbackMode && bondRecordValid_) {
    const bool peerRandom = (bondRecord_.peerAddressRandom & 0x01U) != 0U;
    const bool identityRandom =
        (bondRecord_.peerIdentityAddressRandom & 0x01U) != 0U;
    if (peerRandom == connectionPeerAddressRandom_ &&
        memcmp(bondRecord_.peerAddress, connectionPeerAddress_, 6U) == 0) {
      selectedBondId = 0U;
      directMatches = 1U;
    } else if (bondRecord_.peerIrkValid != 0U &&
               identityRandom == connectionPeerAddressRandom_ &&
               memcmp(bondRecord_.peerIdentityAddress,
                      connectionPeerAddress_, 6U) == 0) {
      selectedBondId = 0U;
      identityMatches = 1U;
    } else if (connectionPeerAddressRandom_ &&
               bondRecord_.peerIrkValid != 0U &&
               bleAddressLooksResolvablePrivate(connectionPeerAddress_)) {
      uint8_t hash[3] = {0};
      if (computeBleResolvablePrivateAddressHash(
              bondRecord_.peerIrk, &connectionPeerAddress_[3], hash) &&
          memcmp(hash, connectionPeerAddress_, sizeof(hash)) == 0) {
        selectedBondId = 0U;
        rpaMatches = 1U;
      }
    }
  } else if (!callbackMode) {
    // Examine every logical bond. Direct on-air address wins over stable
    // identity, which wins over resolving the peer's current RPA.
    for (uint8_t bondId = 0U; bondId < kBleMaxBonds; ++bondId) {
      BleBondDbPayload payload{};
      BleBondRecord projected{};
      if (!readActiveBleBondDbPayload(bondId, &payload) ||
          !bleBondDbCoreToRecord(payload.core, connectionRole_, &projected)) {
        continue;
      }
      bool candidateLocalIdentityMatches =
          payload.core.localAddressRandom == localIdentityRandom &&
          memcmp(payload.core.localAddress, localIdentityAddress, 6U) == 0;
      if (!candidateLocalIdentityMatches &&
          payload.core.localAddressRandom != 0U &&
          bleAddressLooksResolvablePrivate(payload.core.localAddress)) {
        uint8_t expectedHash[3] = {0};
        candidateLocalIdentityMatches =
            (currentLocalIrkValid &&
             computeBleResolvablePrivateAddressHash(
                 currentLocalIrk, &payload.core.localAddress[3],
                 expectedHash) &&
             memcmp(expectedHash, payload.core.localAddress, 3U) == 0) ||
            (computeBleResolvablePrivateAddressHash(
                 legacyLocalRoot, &payload.core.localAddress[3],
                 expectedHash) &&
             memcmp(expectedHash, payload.core.localAddress, 3U) == 0);
      }
      if (!candidateLocalIdentityMatches) {
        continue;
      }
      if (((payload.core.peerAddressRandom & 0x01U) != 0U) ==
              connectionPeerAddressRandom_ &&
          memcmp(payload.core.peerAddress, connectionPeerAddress_, 6U) == 0) {
        selectedBondId = bondId;
        ++directMatches;
        continue;
      }
      if (bleBondDbAddressNonZero(payload.core.peerIdentityAddress) &&
          ((payload.core.peerIdentityAddressRandom & 0x01U) != 0U) ==
              connectionPeerAddressRandom_ &&
          memcmp(payload.core.peerIdentityAddress, connectionPeerAddress_,
                 6U) == 0) {
        if (directMatches == 0U) {
          selectedBondId = bondId;
        }
        ++identityMatches;
        continue;
      }
      if (connectionPeerAddressRandom_ &&
          bleAddressLooksResolvablePrivate(connectionPeerAddress_) &&
          (payload.core.keyValidityFlags &
           kBleBondDbKeyPeerIrkValid) != 0U) {
        ++encDebug_.connAarResolveAttemptCount;
        uint8_t hash[3] = {0};
        if (computeBleResolvablePrivateAddressHash(
                payload.core.peerIrk, &connectionPeerAddress_[3], hash) &&
            memcmp(hash, connectionPeerAddress_, sizeof(hash)) == 0) {
          if (directMatches == 0U && identityMatches == 0U) {
            selectedBondId = bondId;
          }
          ++rpaMatches;
        }
      }
    }
    const bool ambiguous = directMatches > 1U ||
                           (directMatches == 0U && identityMatches > 1U) ||
                           (directMatches == 0U && identityMatches == 0U &&
                            rpaMatches > 1U);
    if (ambiguous) {
      selectedBondId = kBleInvalidBondId;
      emitBleTrace("BOND_PRIME_AMBIGUOUS");
    }
    if (selectedBondId != kBleInvalidBondId &&
        !loadBondProjection(selectedBondId, connectionRole_, &bondRecord_,
                            &activeBondGeneration_)) {
      selectedBondId = kBleInvalidBondId;
    }
  }
  if (selectedBondId == kBleInvalidBondId) {
    activeBondId_ = kBleInvalidBondId;
    activeBondGeneration_ = 0U;
    bondRecordValid_ = false;
    emitBleTrace("BOND_PRIME_PEER_UNRESOLVED");
    return false;
  }
  activeBondId_ = selectedBondId;
  bondRecordValid_ = true;
  if (directMatches != 0U) {
    emitBleTrace("BOND_DB_DIRECT_MATCH");
  } else if (identityMatches != 0U) {
    emitBleTrace("BOND_IDENTITY_ADDR_MATCH");
  } else if (rpaMatches != 0U) {
    ++encDebug_.connAarResolveSuccessCount;
    emitBleTrace("BOND_DB_RPA_RESOLVED");
  }
  emitBleTrace("BOND_DB_SELECTED");
  bool localIdentityMatches =
      ((bondRecord_.localAddressRandom & 0x01U) == localIdentityRandom) &&
      memcmp(bondRecord_.localAddress, localIdentityAddress,
             sizeof(localIdentityAddress)) == 0;
  bool normalizeLegacyLocalRpa = false;

  // Versions before bidirectional identity distribution persisted the active
  // RPA as the local identity. Resolve that record with both the corrected
  // d1-derived IRK and the formerly used raw FICR root, then migrate it.
  if (!localIdentityMatches &&
      (bondRecord_.localAddressRandom & 0x01U) != 0U &&
      bleAddressLooksResolvablePrivate(bondRecord_.localAddress)) {
    uint8_t candidateIrk[16] = {0};
    uint8_t expectedHash[3] = {0};
    if (getLocalIdentityIrk(candidateIrk) &&
        computeBleResolvablePrivateAddressHash(
            candidateIrk, &bondRecord_.localAddress[3], expectedHash) &&
        memcmp(expectedHash, &bondRecord_.localAddress[0],
               sizeof(expectedHash)) == 0) {
      normalizeLegacyLocalRpa = true;
    } else {
      Ficr::identityRoot(candidateIrk);
      if (computeBleResolvablePrivateAddressHash(
              candidateIrk, &bondRecord_.localAddress[3], expectedHash) &&
          memcmp(expectedHash, &bondRecord_.localAddress[0],
                 sizeof(expectedHash)) == 0) {
        normalizeLegacyLocalRpa = true;
      }
    }
    localIdentityMatches = normalizeLegacyLocalRpa;
  }
  if (!localIdentityMatches) {
    activeBondId_ = kBleInvalidBondId;
    activeBondGeneration_ = 0U;
    bondRecordValid_ = false;
    emitBleTrace("BOND_PRIME_LOCAL_ID_MISMATCH");
    return false;
  }
  bondIdentityMatchedForConnection_ = true;
  activeBondMeetsSecurityPolicy_ =
      bondMeetsSecurityPolicy(bondRecord_, securityPolicy_);
  memcpy(smpStk_, bondRecord_.ltk, sizeof(smpStk_));
  smpStkValid_ = true;
  memcpy(smpPeerLtk_, bondRecord_.ltk, sizeof(smpPeerLtk_));
  smpPeerLtkValid_ = true;
  smpPeerLtkAwaitMasterId_ = false;
  memcpy(smpEncReqRand_, bondRecord_.rand, sizeof(smpEncReqRand_));
  smpEncReqEdiv_ = bondRecord_.ediv;
  smpKeySize_ = (bondRecord_.keySize >= 7U && bondRecord_.keySize <= 16U)
                    ? bondRecord_.keySize
                    : 16U;
  reduceSmpEncryptionKey(smpStk_);
  reduceSmpEncryptionKey(smpPeerLtk_);
  if (bondRecord_.peerCsrkValid != 0U) {
    memcpy(smpPeerCsrk_, bondRecord_.peerCsrk, sizeof(smpPeerCsrk_));
    smpPeerCsrkValid_ = true;
    smpPeerSignCounter_ = bondRecord_.peerSignCounter;
    smpPeerSignCounterReservedUntil_ = bondRecord_.peerSignCounter;
    smpPeerSignCounterValid_ =
        bondRecord_.peerSignCounterValid != 0U;
  }
  if (bondRecord_.localCsrkValid != 0U) {
    memcpy(smpLocalCsrk_, bondRecord_.localCsrk, sizeof(smpLocalCsrk_));
    smpLocalCsrkValid_ = true;
    smpLocalSignCounter_ = bondRecord_.localSignCounter;
    smpLocalSignCounterReservedUntil_ = bondRecord_.localSignCounter;
  }
  connectionEncAuthenticated_ =
      (bondRecord_.reserved[0] & kBleBondRecordFlagAuthenticated) != 0U;
  connectionEncSecureConnections_ =
      (bondRecord_.reserved[0] & kBleBondRecordFlagSecureConnections) != 0U;
  smpBondingRequested_ = true;
  smpExpectPeerEncKey_ = false;
  bondKeyPrimedForConnection_ = true;
  connectionBondedEncryptionRequested_ = false;
  restoreBondedCccdState();
  if (gattSchemaFingerprint_ != 0U) {
    (void)commitGattSchemaFingerprint(gattSchemaFingerprint_);
  }
  if (normalizeLegacyLocalRpa) {
    memcpy(bondRecord_.localAddress, localIdentityAddress,
           sizeof(bondRecord_.localAddress));
    bondRecord_.localAddressRandom = localIdentityRandom;
    bondLocalIdentityMigrationPending_ = true;
    bondLocalIdentityMigrationRetryAtUs_ = 0U;
    emitBleTrace("BOND_LOCAL_RPA_MIGRATED");
  }
  emitBleTrace("BOND_PRIMED");
  return true;
}

void BleRadio::emitBleTrace(const char* message) const {
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
  if (message == nullptr) {
    return;
  }
  // These are emitted for ordinary idle connection events and can overwhelm
  // the serial transport before a pairing failure can be observed.
  if ((strcmp(message, "EVT_RX_TIMEOUT") == 0) ||
      (strcmp(message, "EVT_TX_TIMEOUT") == 0) ||
      (strcmp(message, "EVT_LATE_SKIP") == 0)) {
    return;
  }
  // Bluefruit polls the initial connection setup window in thread context.
  // Serial output there can still block past T_IFS or the next connection
  // anchor, so defer every connected-state trace just as we do in an ISR.
  if (bleRunningInIsr() || connected_) {
    const_cast<BleRadio*>(this)->enqueueDeferredTrace(message);
    return;
  }
  if (traceCallback_ != nullptr) {
    traceCallback_(message, traceCallbackContext_);
    return;
  }
  Serial.print(F("[BLE] "));
  Serial.println(message);
#else
  (void)message;
#endif
}

void BleRadio::emitBleTraceHexNibble(uint8_t value) const {
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
  switch (value & 0x0FU) {
    case 0x0U:
      emitBleTrace("HEX_0");
      break;
    case 0x1U:
      emitBleTrace("HEX_1");
      break;
    case 0x2U:
      emitBleTrace("HEX_2");
      break;
    case 0x3U:
      emitBleTrace("HEX_3");
      break;
    case 0x4U:
      emitBleTrace("HEX_4");
      break;
    case 0x5U:
      emitBleTrace("HEX_5");
      break;
    case 0x6U:
      emitBleTrace("HEX_6");
      break;
    case 0x7U:
      emitBleTrace("HEX_7");
      break;
    case 0x8U:
      emitBleTrace("HEX_8");
      break;
    case 0x9U:
      emitBleTrace("HEX_9");
      break;
    case 0xAU:
      emitBleTrace("HEX_A");
      break;
    case 0xBU:
      emitBleTrace("HEX_B");
      break;
    case 0xCU:
      emitBleTrace("HEX_C");
      break;
    case 0xDU:
      emitBleTrace("HEX_D");
      break;
    case 0xEU:
      emitBleTrace("HEX_E");
      break;
    default:
      emitBleTrace("HEX_F");
      break;
  }
#else
  (void)value;
#endif
}

void BleRadio::emitBleTraceHexByte(uint8_t value) const {
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
  emitBleTraceHexNibble(static_cast<uint8_t>(value >> 4U));
  emitBleTraceHexNibble(value);
#else
  (void)value;
#endif
}

void BleRadio::emitBleTraceHex16(uint16_t value) const {
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
  emitBleTraceHexByte(static_cast<uint8_t>((value >> 8U) & 0xFFU));
  emitBleTraceHexByte(static_cast<uint8_t>(value & 0xFFU));
#else
  (void)value;
#endif
}

void BleRadio::emitAttErrorTrace(uint8_t requestOpcode, uint16_t handle,
                                 uint8_t errorCode) const {
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
  emitBleTrace("ATT_ERROR_RSP");
  switch (requestOpcode) {
    case kAttOpExchangeMtuReq:
      emitBleTrace("ATT_ERROR_REQ_MTU");
      break;
    case kAttOpFindInfoReq:
      emitBleTrace("ATT_ERROR_REQ_FIND_INFO");
      break;
    case kAttOpFindByTypeValueReq:
      emitBleTrace("ATT_ERROR_REQ_FIND_TYPE");
      break;
    case kAttOpReadByTypeReq:
      emitBleTrace("ATT_ERROR_REQ_READ_TYPE");
      break;
    case kAttOpReadReq:
      emitBleTrace("ATT_ERROR_REQ_READ");
      break;
    case kAttOpReadBlobReq:
      emitBleTrace("ATT_ERROR_REQ_READ_BLOB");
      break;
    case kAttOpReadByGroupTypeReq:
      emitBleTrace("ATT_ERROR_REQ_READ_GROUP");
      break;
    case kAttOpPrepareWriteReq:
      emitBleTrace("ATT_ERROR_REQ_PREP_WRITE");
      break;
    case kAttOpExecuteWriteReq:
      emitBleTrace("ATT_ERROR_REQ_EXEC_WRITE");
      break;
    case kAttOpWriteReq:
      emitBleTrace("ATT_ERROR_REQ_WRITE");
      break;
    default:
      emitBleTrace("ATT_ERROR_REQ_OTHER");
      emitBleTraceHexByte(requestOpcode);
      break;
  }
  emitBleTrace("ATT_ERROR_HANDLE");
  emitBleTraceHex16(handle);
  switch (errorCode) {
    case kAttErrInvalidHandle:
      emitBleTrace("ATT_ERROR_INVALID_HANDLE");
      break;
    case kAttErrReadNotPermitted:
      emitBleTrace("ATT_ERROR_READ_NOT_PERMITTED");
      break;
    case kAttErrWriteNotPermitted:
      emitBleTrace("ATT_ERROR_WRITE_NOT_PERMITTED");
      break;
    case kAttErrInvalidPdu:
      emitBleTrace("ATT_ERROR_INVALID_PDU");
      break;
    case kAttErrInsufficientAuthentication:
      emitBleTrace("ATT_ERROR_INSUFF_AUTH");
      break;
    case kAttErrInvalidOffset:
      emitBleTrace("ATT_ERROR_INVALID_OFFSET");
      break;
    case kAttErrAttributeNotFound:
      emitBleTrace("ATT_ERROR_ATTR_NOT_FOUND");
      break;
    case kAttErrInvalidAttrValueLen:
      emitBleTrace("ATT_ERROR_BAD_VALUE_LEN");
      break;
    case kAttErrInsufficientEncryption:
      emitBleTrace("ATT_ERROR_INSUFF_ENC");
      break;
    default:
      emitBleTrace("ATT_ERROR_OTHER");
      emitBleTraceHexByte(errorCode);
      break;
  }
#else
  (void)requestOpcode;
  (void)handle;
  (void)errorCode;
#endif
}

void BleRadio::updateNextConnectionEventTime() {
  const uint32_t intervalUs = static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL;
  if (intervalUs == 0U) {
    connectionNextEventUs_ = bleTimingUs() + 1250UL;
    ++connectionEventCounter_;
    return;
  }

  connectionNextEventUs_ += intervalUs;
  ++connectionEventCounter_;

  // Catch up if caller serviced connection late — advance at most 1 event
  // per call so the resync loop can walk channels one-by-one and find the
  // correct one (unguarded loop skips past the right channel).
  const uint32_t nowUs = bleTimingUs();
  if (timeReachedUs(nowUs, connectionNextEventUs_)) {
    connectionNextEventUs_ += intervalUs;
    ++connectionEventCounter_;
  }
}

void BleRadio::resetConnectionMissedEventCount() {
  connectionMissedEventCount_ = 0;
}

// Advance counter/timer until caught up to now (no guard) so the channel
// selection aligns with the peripheral after a long callback stall.
void BleRadio::catchUpConnectionEventTime() {
  if (connectionIntervalUnits_ == 0U) {
    return;
  }
  const uint32_t nowUs = bleTimingUs();
  while (timeReachedUs(nowUs, connectionNextEventUs_)) {
    if (connectionUpdatePending_ &&
        llEventInstantReached(connectionEventCounter_,
                              connectionUpdateInstant_)) {
      const uint16_t skippedInstant = connectionEventCounter_;
      const uint32_t skippedAnchorUs = connectionNextEventUs_;
      applyPendingConnectionUpdateAtInstant(skippedInstant,
                                            skippedAnchorUs);
      emitBleTrace("LL_CONN_UPDATE_INSTANT_CATCHUP");
      continue;
    }
    const uint32_t intervalUs =
        static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL;
    if (intervalUs == 0U) {
      break;
    }
    connectionNextEventUs_ += intervalUs;
    ++connectionEventCounter_;
  }
}

void BleRadio::armConnectionResyncWindow(uint32_t extraPostAnchorUs,
                                         uint8_t attempts) {
  if ((attempts == 0U) || (connectionIntervalUnits_ == 0U)) {
    return;
  }

  uint32_t postAnchorListenUs =
      bleConnectionRxListenUs(connectionIntervalUnits_, connectionSca_);
  if (postAnchorListenUs > kBleConnRxStartLeadUs) {
    postAnchorListenUs -= kBleConnRxStartLeadUs;
  } else {
    postAnchorListenUs = 0U;
  }
  postAnchorListenUs += extraPostAnchorUs;
  if (postAnchorListenUs > kBleConnResyncPostAnchorMaxUs) {
    postAnchorListenUs = kBleConnResyncPostAnchorMaxUs;
  }

  if (postAnchorListenUs > connectionFirstEventListenUs_) {
    connectionFirstEventListenUs_ = postAnchorListenUs;
  }
  if (attempts > connectionSyncAttemptsRemaining_) {
    connectionSyncAttemptsRemaining_ = attempts;
  }
}

uint8_t BleRadio::selectNextDataChannel(bool useCurrentEventCounter) {
  if (connectionUseChSel2_) {
    // pollConnectionEvent() advances connectionEventCounter_ before selecting
    // the channel. Normally we map to counter-1 (the current event). When the
    // caller was late enough to miss the full event window, we map to the
    // already-advanced counter to re-synchronize with the next event.
    const uint16_t eventCounter = useCurrentEventCounter
                                      ? connectionEventCounter_
                                      : ((connectionEventCounter_ > 0U)
                                             ? static_cast<uint16_t>(
                                                   connectionEventCounter_ - 1U)
                                             : 0U);
    const uint16_t prnE = bleCsa2PrnE(eventCounter, connectionChannelId_);
    uint8_t chanNext = static_cast<uint8_t>(prnE % 37U);
    if ((connectionChannelMap_[chanNext >> 3U] & (1U << (chanNext & 0x07U))) != 0U) {
      return chanNext;
    }
    if (connectionChannelCount_ == 0U) {
      return 0U;
    }
    const uint8_t remapIndex =
        static_cast<uint8_t>((static_cast<uint32_t>(connectionChannelCount_) *
                              static_cast<uint32_t>(prnE)) >>
                             16U);
    chanNext = remapChannelByIndex(connectionChannelMap_, remapIndex);
    return chanNext;
  }

  const uint16_t eventCounter = useCurrentEventCounter
                                    ? connectionEventCounter_
                                    : ((connectionEventCounter_ > 0U)
                                           ? static_cast<uint16_t>(
                                                 connectionEventCounter_ - 1U)
                                           : 0U);
  uint8_t chanNext = static_cast<uint8_t>(
      (static_cast<uint32_t>(connectionHop_) *
       static_cast<uint32_t>(eventCounter + 1U)) % 37U);
  connectionChanUse_ = chanNext;
  if ((connectionChannelMap_[chanNext >> 3U] & (1U << (chanNext & 0x07U))) == 0U) {
    const uint8_t remapIndex = static_cast<uint8_t>(chanNext % connectionChannelCount_);
    chanNext = remapChannelByIndex(connectionChannelMap_, remapIndex);
  }
  return chanNext;
}

bool BleRadio::setDataChannel(uint8_t dataChannel) {
  if (radio_ == nullptr || dataChannel > 36U ||
      !nrf54l15_exclusive_radio_is_owned_by(
          Nrf54ExclusiveRadioOwner::kBle, radioOwnershipToken_)) {
    return false;
  }

  if (connected_) {
    radio_->BASE0 = bleAccessAddressBase(connectionAccessAddress_);
    radio_->PREFIX0 = (radio_->PREFIX0 & ~RADIO_PREFIX0_AP0_Msk) |
                      ((bleAccessAddressPrefix(connectionAccessAddress_)
                        << RADIO_PREFIX0_AP0_Pos) &
                       RADIO_PREFIX0_AP0_Msk);
    radio_->CRCINIT = (connectionCrcInit_ & RADIO_CRCINIT_CRCINIT_Msk);
  }

  const uint8_t freq = bleDataChannelToFrequency(dataChannel);
  radio_->FREQUENCY =
      ((static_cast<uint32_t>(freq) << RADIO_FREQUENCY_FREQUENCY_Pos) &
       RADIO_FREQUENCY_FREQUENCY_Msk) |
      (0UL << RADIO_FREQUENCY_MAP_Pos);
  radio_->DATAWHITE = bleDataWhiteValue(dataChannel);
  return true;
}

void BleRadio::restoreAdvertisingLinkDefaults() {
  if (radio_ == nullptr ||
      !nrf54l15_exclusive_radio_is_owned_by(
          Nrf54ExclusiveRadioOwner::kBle, radioOwnershipToken_)) {
    return;
  }

  radio_->BASE0 = bleAccessAddressBase(kBleAccessAddress);
  radio_->PREFIX0 = (radio_->PREFIX0 & ~RADIO_PREFIX0_AP0_Msk) |
                    ((bleAccessAddressPrefix(kBleAccessAddress)
                      << RADIO_PREFIX0_AP0_Pos) &
                     RADIO_PREFIX0_AP0_Msk);
  radio_->CRCINIT = (kBleAdvertisingCrcInit & RADIO_CRCINIT_CRCINIT_Msk);
  radio_->TXADDRESS =
      (0UL << RADIO_TXADDRESS_TXADDRESS_Pos) & RADIO_TXADDRESS_TXADDRESS_Msk;
  radio_->RXADDRESSES =
      (RADIO_RXADDRESSES_ADDR0_Enabled << RADIO_RXADDRESSES_ADDR0_Pos) &
      RADIO_RXADDRESSES_ADDR0_Msk;
  connectionCurrentTxPhy_ = kBlePhy1M;
  connectionCurrentRxPhy_ = kBlePhy1M;
  connectionPreferredTxPhyMask_ = kBlePhy1M;
  connectionPreferredRxPhyMask_ = kBlePhy1M;
  connectionPhyRequestPending_ = false;
  connectionPhyRequestInFlight_ = false;
  connectionPhyUpdatePending_ = false;
  connectionPhyUpdateInstant_ = 0U;
  connectionPendingTxPhy_ = kBlePhy1M;
  connectionPendingRxPhy_ = kBlePhy1M;
  connectionMaxTxPayloadLength_ = kBleDefaultDataPduMaxPayload;
  connectionMaxRxPayloadLength_ = kBleDefaultDataPduMaxPayload;
  connectionDataLengthUpdatePending_ = false;
  connectionDataLengthUpdateInFlight_ = false;
  connectionDataLengthUpdateComplete_ = false;
  connectionAttMtuExchangePending_ = false;
  connectionAttMtuExchangeInFlight_ = false;
  connectionAttMtuExchangeComplete_ = false;
  connectionRequestedAttMtu_ = localPreferredAttMtu();
  connectionAttMtu_ = kBleDefaultAttMtu;
  connectionLastTxLlid_ = 0x01U;
  connectionLastTxLength_ = 0U;
  connectionLastTxMoreData_ = false;
  connectionLastTxPlainLlid_ = 0x01U;
  connectionLastTxPlainLength_ = 0U;
  memset(connectionLastTxPlainPayload_, 0, sizeof(connectionLastTxPlainPayload_));
  connectionPendingTxLlid_ = 0x01U;
  connectionPendingTxLength_ = 0U;
  connectionPendingTxValid_ = false;
  connectionPendingTxSmpFragment_ = false;
  memset(connectionPendingTxPayload_, 0, sizeof(connectionPendingTxPayload_));
  clearPendingL2capTxFragment();
  connectionUpdatePending_ = false;
  connectionUpdateInstant_ = 0U;
  connectionPendingWinSize_ = 0U;
  connectionPendingWinOffset_ = 0U;
  connectionPendingIntervalUnits_ = 0U;
  connectionPendingLatency_ = 0U;
  connectionPendingTimeoutUnits_ = 0U;
  connectionCentralConnParamIndPending_ = false;
  connectionCentralConnParamIntervalUnits_ = 0U;
  connectionCentralConnParamLatency_ = 0U;
  connectionCentralConnParamTimeoutUnits_ = 0U;
  connectionChannelMapPending_ = false;
  connectionChannelMapInstant_ = 0U;
  memset(connectionPendingChannelMap_, 0, sizeof(connectionPendingChannelMap_));
  connectionPendingChannelCount_ = 0U;
  connectionServiceChangedIndicationsEnabled_ = false;
  connectionServiceChangedCccdRestored_ = false;
  connectionServiceChangedIndicationPending_ = false;
  connectionServiceChangedIndicationAwaitingConfirm_ = false;
  connectionServiceChangedIndicationStartHandle_ = 0U;
  connectionServiceChangedIndicationEndHandle_ = 0U;
  connectionServiceChangedIndicationGeneration_ = 0U;
  connectionBatteryNotificationsEnabled_ = false;
  connectionBatteryNotificationPending_ = false;
  connectionSmpSecurityRequestPending_ = false;
  clearPreparedWriteState();
  clearIncomingL2capReassembly();
  clearConnectionSecurityState();
  scanCycleStartIndex_ = 0U;
  buildAdvertisingPacket();
  buildScanResponsePacket();
  setAdvertisingChannel(BleAdvertisingChannel::k37);
}
