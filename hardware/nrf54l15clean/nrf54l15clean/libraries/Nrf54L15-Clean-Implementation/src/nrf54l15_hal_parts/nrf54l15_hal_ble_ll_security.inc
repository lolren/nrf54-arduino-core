
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
      {
        const uint8_t winSize = payload[1];
        const uint16_t winOffset = readLe16(&payload[2]);
        const uint16_t interval = readLe16(&payload[4]);
        const uint16_t latency = readLe16(&payload[6]);
        const uint16_t timeout = readLe16(&payload[8]);
        const uint16_t instant = readLe16(&payload[10]);
        bool valid = true;
        if (winSize > 8U) {
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
        } else {
          return rejectMalformedRequest();
        }
      }
      return false;

    case kBleLlCtrlChannelMapInd:
      if (length != 8U) {
        return rejectMalformedRequest();
      }
      {
        uint8_t map[5] = {payload[1], payload[2], payload[3], payload[4], payload[5]};
        const uint16_t instant = readLe16(&payload[6]);
        map[4] &= 0x1FU;
        const uint8_t count = bitCount37(map);
        const uint16_t instantDelta =
            static_cast<uint16_t>(instant - currentEventCounter);
        const bool instantInFuture = (instantDelta != 0U) && (instantDelta < 0x8000U);
        if (count >= 2U && instantInFuture) {
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
        const uint16_t ediv = readLe16(&payload[9]);
        if ((ediv != bondRecord_.ediv) ||
            (memcmp(&payload[1], bondRecord_.rand, sizeof(bondRecord_.rand)) != 0)) {
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
          if (fillBleSecurityRandomBytes(&connectionEncSkd_[8], 8U, seed,
                                         kBleSecurityRandomSpinLimit)) {
            ++encDebug_.encRandomHardwareCount;
          } else {
            ++encDebug_.encRandomFallbackCount;
          }
          if (fillBleSecurityRandomBytes(&connectionEncIv_[4], 4U,
                                         seed ^ 0x6BA38B4FUL,
                                         kBleSecurityRandomSpinLimit)) {
            ++encDebug_.encRandomHardwareCount;
          } else {
            ++encDebug_.encRandomFallbackCount;
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
        // Peripheral role sequence per Core LLCP:
        // after LL_ENC_RSP, wait for initiator LL_START_ENC_REQ and answer with
        // LL_START_ENC_RSP.
        connectionEncStartReqPending_ = true;
        connectionEncStartReqTxPending_ = false;
        connectionEncAwaitingStartRsp_ = false;
        connectionEncEnableTxOnNextEvent_ = false;
        connectionEncRxCounter_ = 0ULL;
        connectionEncTxCounter_ = 0ULL;
        connectionPendingTxValid_ = false;
        connectionPendingTxLlid_ = 0x01U;
        connectionPendingTxLength_ = 0U;
        connectionLastTxWasEncrypted_ = false;
        connectionLastTxEncryptedLength_ = 0U;
        memset(connectionLastTxEncryptedPayload_, 0, sizeof(connectionLastTxEncryptedPayload_));
        connectionEncPrecomputedEmptyValid_ = false;
        connectionEncPrecomputedCounter_ = 0ULL;
        memset(connectionEncPrecomputedPayload_, 0, sizeof(connectionEncPrecomputedPayload_));
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
      connectionEncStartReqPending_ = false;
      connectionEncStartReqTxPending_ = false;
      connectionEncAwaitingStartRsp_ = false;
      connectionEncRxEnabled_ = true;
      connectionEncEnableTxOnNextEvent_ = true;
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
        bool valid = true;
        if (intervalMin < 6U || intervalMin > 3200U ||
            intervalMax < 6U || intervalMax > 3200U ||
            intervalMin > intervalMax) {
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
             static_cast<uint32_t>(intervalMax) * 5UL) /
            2UL;
        if (timeoutMs <= requiredMs) {
          valid = false;
        }
        if (!valid) {
          return rejectMalformedRequest();
        }

        outPayload[0] = kBleLlCtrlConnectionParamRsp;
        memcpy(&outPayload[1], &payload[1], 23U);
        *outLength = 24U;
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
        const uint8_t symmetricPhys = preferredSymmetricBlePhys();
        outPayload[0] = kBleLlCtrlPhyRsp;
        outPayload[1] = symmetricPhys;
        outPayload[2] = symmetricPhys;
        *outLength = 3U;
        emitBleTrace("LL_PHY_REQ_RX");
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
      return false;

    case kBleLlCtrlEncRsp:
      if (length != 13U) {
        return false;
      }
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
        // Interop fallback completion: transmit the terminal LL_START_ENC_RSP
        // with TX encryption active so host stacks that expect a symmetric
        // completion PDU accept the transition.
        outPayload[0] = kBleLlCtrlStartEncRsp;
        *outLength = 1U;
        emitBleTrace("LL_START_ENC_RSP_RX");
        return true;
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
      {
        const uint8_t txPhy = supportedBlePhys(payload[1]);
        const uint8_t rxPhy = supportedBlePhys(payload[2]);
        const uint16_t instant = readLe16(&payload[3]);
        const uint16_t instantDelta =
            static_cast<uint16_t>(instant - currentEventCounter);
        const bool instantInFuture =
            (instantDelta != 0U) && (instantDelta < 0x8000U);
        if (txPhy == kBlePhyNone || rxPhy == kBlePhyNone) {
          emitBleTrace("LL_PHY_UPDATE_BADPHY");
          return false;
        }
        if (!instantInFuture) {
          emitBleTrace("LL_PHY_UPDATE_PAST");
          return false;
        }
        if (txPhy != rxPhy) {
          emitBleTrace("LL_PHY_ASYM_UNSUPPORTED");
          return false;
        }
        connectionPendingTxPhy_ = txPhy;
        connectionPendingRxPhy_ = rxPhy;
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
  smpExpectInitiatorEncKey_ = false;
  smpExpectInitiatorIdKey_ = false;
  smpPeerLtkValid_ = false;
  smpPeerLtkAwaitMasterId_ = false;
  smpPeerIrkValid_ = false;
  smpPeerIdentityAddressValid_ = false;
  smpPeerIdentityAddressRandom_ = false;
  memset(smpPeerLtk_, 0, sizeof(smpPeerLtk_));
  memset(smpPeerIrk_, 0, sizeof(smpPeerIrk_));
  memset(smpPeerIdentityAddress_, 0, sizeof(smpPeerIdentityAddress_));
  memset(smpEncReqRand_, 0, sizeof(smpEncReqRand_));
  smpEncReqEdiv_ = 0U;
  smpKeySize_ = 16U;
  bondKeyPrimedForConnection_ = false;
}

void BleRadio::clearEncryptionState() {
  connectionEncSessionValid_ = false;
  connectionEncRxEnabled_ = false;
  connectionEncTxEnabled_ = false;
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
  connectionEncPrecomputedEmptyValid_ = false;
  connectionEncPrecomputedCounter_ = 0ULL;
  memset(connectionEncPrecomputedPayload_, 0, sizeof(connectionEncPrecomputedPayload_));
  connectionEncPrecomputedStartRspValid_ = false;
  memset(connectionEncPrecomputedStartRsp_, 0, sizeof(connectionEncPrecomputedStartRsp_));
  connectionEncPrecomputedStartRspTxValid_ = false;
  connectionEncPrecomputedStartRspTxCounter_ = 0ULL;
  memset(connectionEncPrecomputedStartRspTx_, 0, sizeof(connectionEncPrecomputedStartRspTx_));
}

void BleRadio::clearConnectionSecurityState() {
  clearSmpPairingState();
  clearEncryptionState();
}

bool BleRadio::buildCurrentBondRecord(BleBondRecord* outRecord) const {
  if (outRecord == nullptr || !smpPeerLtkValid_) {
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
  memcpy(record.localAddress, address_, sizeof(record.localAddress));
  record.localAddressRandom =
      (addressType_ == BleAddressType::kRandomStatic) ? 1U : 0U;
  memcpy(record.ltk, smpPeerLtk_, sizeof(record.ltk));
  memcpy(record.rand, smpEncReqRand_, sizeof(record.rand));
  if (smpPeerIrkValid_) {
    memcpy(record.peerIrk, smpPeerIrk_, sizeof(record.peerIrk));
    record.peerIrkValid = 1U;
  }
  record.ediv = smpEncReqEdiv_;
  record.keySize = (smpKeySize_ >= 7U && smpKeySize_ <= 16U) ? smpKeySize_ : 16U;
  memset(record.reserved, 0, sizeof(record.reserved));
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

  if (!smpPrefetchedLocalRandomValid_) {
    if (fillBleSecurityRandomBytes(smpPrefetchedLocalRandom_,
                                   sizeof(smpPrefetchedLocalRandom_),
                                   seedBase ^ 0x13579BDFUL, spinLimit)) {
      ++encDebug_.encRandomHardwareCount;
    } else {
      ++encDebug_.encRandomFallbackCount;
    }
    smpPrefetchedLocalRandomValid_ = true;
    ++encDebug_.encRandomPrefetchFillCount;
  }

  if (!connectionPrefetchedEncMaterialValid_) {
    if (fillBleSecurityRandomBytes(connectionPrefetchedSkds_,
                                   sizeof(connectionPrefetchedSkds_),
                                   seedBase ^ 0x2468ACE0UL, spinLimit)) {
      ++encDebug_.encRandomHardwareCount;
    } else {
      ++encDebug_.encRandomFallbackCount;
    }
    if (fillBleSecurityRandomBytes(connectionPrefetchedIvs_,
                                   sizeof(connectionPrefetchedIvs_),
                                   seedBase ^ 0x6BA38B4FUL, spinLimit)) {
      ++encDebug_.encRandomHardwareCount;
    } else {
      ++encDebug_.encRandomFallbackCount;
    }
    connectionPrefetchedEncMaterialValid_ = true;
    ++encDebug_.encRandomPrefetchFillCount;
  }
}

bool BleRadio::isBondRecordUsable(const BleBondRecord& record) const {
  return bleBondRecordLooksSane(record);
}

bool BleRadio::loadBondRecordFromPersistence() {
  if (bondStorageLoaded_) {
    return bondRecordValid_;
  }

  BleBondRecord loaded{};
  bool loadedFromStore = false;
  if (bondLoadCallback_ != nullptr) {
    loadedFromStore = bondLoadCallback_(&loaded, bondCallbackContext_);
    if (loadedFromStore && !isBondRecordUsable(loaded)) {
      loadedFromStore = false;
    }
  }
  if (!loadedFromStore) {
    loadedFromStore = readFlashBondRecord(&loaded);
  }
  if (!loadedFromStore) {
    loadedFromStore = readRetainedBondRecord(&loaded);
  }

  bondStorageLoaded_ = true;
  if (loadedFromStore && isBondRecordUsable(loaded)) {
    memcpy(&bondRecord_, &loaded, sizeof(bondRecord_));
    bondRecordValid_ = true;
    emitBleTrace("BOND_LOADED");
  } else {
    memset(&bondRecord_, 0, sizeof(bondRecord_));
    bondRecordValid_ = false;
  }

  return bondRecordValid_;
}

bool BleRadio::persistBondRecord(const BleBondRecord& record) {
  if (!isBondRecordUsable(record)) {
    return false;
  }

  const bool retentionOk = writeRetainedBondRecord(record);
  const bool flashOk = writeFlashBondRecord(record);
  bool callbackOk = true;
  if (bondSaveCallback_ != nullptr) {
    callbackOk = bondSaveCallback_(&record, bondCallbackContext_);
  }
  if (retentionOk || flashOk || callbackOk) {
    memcpy(&bondRecord_, &record, sizeof(bondRecord_));
    bondRecordValid_ = true;
    bondStorageLoaded_ = true;
    emitBleTrace("BOND_SAVED");
  }
  return (retentionOk || flashOk) && callbackOk;
}

bool BleRadio::clearPersistentBondRecord() {
  clearRetainedBondBlob();
  const bool flashOk = clearFlashBondRecord();
  bool callbackOk = true;
  if (bondClearCallback_ != nullptr) {
    callbackOk = bondClearCallback_(bondCallbackContext_);
  }
  emitBleTrace("BOND_CLEARED");
  return flashOk && callbackOk;
}

bool BleRadio::primeBondForCurrentPeer() {
  bondKeyPrimedForConnection_ = false;
  if (!bondStorageLoaded_) {
    loadBondRecordFromPersistence();
  }
  if (!bondRecordValid_) {
    return false;
  }

  // For random peer addresses (e.g. Android resolvable private addresses that
  // rotate between connections) prefer the silicon AAR resolver when an IRK
  // is available, then fall back to exact current-address match. If no peer
  // IRK was ever distributed, keep the old behavior and rely on EDIV/Rand in
  // the later LL_ENC_REQ handler.
  // For public peer addresses require an exact address and type match.
  const bool bondPeerIsRandom = (bondRecord_.peerAddressRandom & 0x01U) != 0U;
  if (!bondPeerIsRandom) {
    if (connectionPeerAddressRandom_) {
      return false;
    }
    if (memcmp(bondRecord_.peerAddress, connectionPeerAddress_,
               sizeof(connectionPeerAddress_)) != 0) {
      return false;
    }
  } else {
    if (!connectionPeerAddressRandom_) {
      return false;
    }
    const bool exactRandomAddressMatch =
        memcmp(bondRecord_.peerAddress, connectionPeerAddress_,
               sizeof(connectionPeerAddress_)) == 0;
    if (!exactRandomAddressMatch && (bondRecord_.peerIrkValid != 0U)) {
      ++encDebug_.connAarResolveAttemptCount;
      bool resolved = false;
      bool aarOk = false;
      if (bleAddressLooksResolvablePrivate(connectionPeerAddress_)) {
        Aar aar;
        aarOk = aar.resolveSingle(connectionPeerAddress_, bondRecord_.peerIrk,
                                  &resolved, kBleBondAarSpinLimit);
        encDebug_.connAarLastErrorStatus = aar.errorStatus();
      }
      if (aarOk && resolved) {
        ++encDebug_.connAarResolveSuccessCount;
        emitBleTrace("BOND_AAR_RESOLVED");
      } else {
        ++encDebug_.connAarResolveFailureCount;
        return false;
      }
    }
  }
  if (memcmp(bondRecord_.localAddress, address_, sizeof(address_)) != 0) {
    return false;
  }
  const uint8_t localAddressRandom =
      (addressType_ == BleAddressType::kRandomStatic) ? 0x01U : 0x00U;
  if ((bondRecord_.localAddressRandom & 0x01U) != localAddressRandom) {
    return false;
  }

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
  smpBondingRequested_ = true;
  smpExpectInitiatorEncKey_ = false;
  bondKeyPrimedForConnection_ = true;
  return true;
}

void BleRadio::emitBleTrace(const char* message) const {
#if defined(NRF54L15_CLEAN_BLE_TRACE) && (NRF54L15_CLEAN_BLE_TRACE == 1)
  if (message == nullptr) {
    return;
  }
  if (bleRunningInIsr()) {
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

void BleRadio::updateNextConnectionEventTime() {
  const uint32_t intervalUs = static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL;
  if (intervalUs == 0U) {
    connectionNextEventUs_ = bleTimingUs() + 1250UL;
    ++connectionEventCounter_;
    return;
  }

  connectionNextEventUs_ += intervalUs;
  ++connectionEventCounter_;

  // Catch up if caller serviced connection late.
  const uint32_t nowUs = bleTimingUs();
  uint8_t guard = 8U;
  while (guard-- > 0U && timeReachedUs(nowUs, connectionNextEventUs_)) {
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
  if (radio_ == nullptr || dataChannel > 36U) {
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
  if (radio_ == nullptr) {
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
  connectionPreferredTxPhyMask_ =
      static_cast<uint8_t>(kBlePhy1M | kBlePhy2M | kBlePhyCoded);
  connectionPreferredRxPhyMask_ =
      static_cast<uint8_t>(kBlePhy1M | kBlePhy2M | kBlePhyCoded);
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
  connectionLastTxPlainLlid_ = 0x01U;
  connectionLastTxPlainLength_ = 0U;
  memset(connectionLastTxPlainPayload_, 0, sizeof(connectionLastTxPlainPayload_));
  connectionPendingTxLlid_ = 0x01U;
  connectionPendingTxLength_ = 0U;
  connectionPendingTxValid_ = false;
  memset(connectionPendingTxPayload_, 0, sizeof(connectionPendingTxPayload_));
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
  connectionServiceChangedIndicationPending_ = false;
  connectionServiceChangedIndicationAwaitingConfirm_ = false;
  connectionBatteryNotificationsEnabled_ = false;
  connectionBatteryNotificationPending_ = false;
  connectionSmpSecurityRequestPending_ = false;
  connectionPreparedWriteActive_ = false;
  connectionPreparedWriteHandle_ = 0U;
  connectionPreparedWriteValue_[0] = 0U;
  connectionPreparedWriteValue_[1] = 0U;
  connectionPreparedWriteMask_ = 0U;
  clearConnectionSecurityState();
  scanCycleStartIndex_ = 0U;
  buildAdvertisingPacket();
  buildScanResponsePacket();
  setAdvertisingChannel(BleAdvertisingChannel::k37);
}
