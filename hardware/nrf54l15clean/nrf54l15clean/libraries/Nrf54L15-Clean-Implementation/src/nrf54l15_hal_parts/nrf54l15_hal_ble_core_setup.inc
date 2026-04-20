BleRadio::BleRadio(uint32_t radioBase, uint32_t ficrBase)
    : radio_(reinterpret_cast<NRF_RADIO_Type*>(static_cast<uintptr_t>(radioBase))),
      ficr_(reinterpret_cast<NRF_FICR_Type*>(static_cast<uintptr_t>(ficrBase))),
      initialized_(false),
      addressType_(BleAddressType::kRandomStatic),
      pduType_(BleAdvPduType::kAdvInd),
      useChSel2_(true),
      externalAntenna_(false),
      address_{0},
      advData_{0},
      advDataLen_(0),
      extendedAdvData_{0},
      extendedAdvDataLen_(0),
      extendedScanRspData_{0},
      extendedScanRspDataLen_(0),
      extendedAdvSid_(0U),
      extendedAdvDid_(0U),
      extendedAdvAuxChannel_(20U),
      extendedSecondaryPacketCount_(0U),
      extendedScanRspPacketCount_(0U),
      scanRspDataAutoDefault_(false),
      scanRspData_{0},
      scanRspDataLen_(0),
      txPacket_{0},
      extendedPrimaryPacket_{0},
      extendedAuxAdvPacket_{0},
      extendedSecondaryPackets_{0},
      extendedScanRspPackets_{0},
      scanRspPacket_{0},
      rxPacket_{0},
      connectionTxPayload_{0},
      connected_(false),
      connectionRole_(BleConnectionRole::kNone),
      connectionPeerAddress_{0},
      connectionPeerAddressRandom_(false),
      connectionAccessAddress_(0),
      connectionCrcInit_(0),
      connectionIntervalUnits_(0),
      connectionLatency_(0),
      connectionTimeoutUnits_(0),
      connectionChannelMap_{0},
      connectionChannelCount_(0),
      connectionHop_(0),
      connectionUseChSel2_(false),
      connectionChannelId_(0U),
      connectionSca_(0),
      connectionChanUse_(0),
      connectionExpectedRxSn_(0),
      connectionTxSn_(0),
      connectionLastTxSn_(0),
      connectionTxHistoryValid_(false),
      connectionFreshTxAllowed_(true),
      connectionEventCounter_(0),
      connectionMissedEventCount_(0U),
      connectionNextEventUs_(0),
      connectionFirstEventListenUs_(0),
      connectionSyncAttemptsRemaining_(0U),
      connectionMaxTxPayloadLength_(kBleDefaultDataPduMaxPayload),
      connectionMaxRxPayloadLength_(kBleDefaultDataPduMaxPayload),
      connectionDataLengthUpdatePending_(false),
      connectionDataLengthUpdateInFlight_(false),
      connectionDataLengthUpdateComplete_(false),
      connectionAttMtuExchangePending_(false),
      connectionAttMtuExchangeInFlight_(false),
      connectionAttMtuExchangeComplete_(false),
      connectionConnParamUpdatePending_(false),
      connectionConnParamUpdateInFlight_(false),
      connectionConnParamUpdateIdentifier_(1U),
      connectionCentralConnParamIndPending_(false),
      connectionCentralConnParamIntervalUnits_(0U),
      connectionCentralConnParamLatency_(0U),
      connectionCentralConnParamTimeoutUnits_(0U),
      connectionRequestedAttMtu_(kBleDefaultAttMtu),
      connectionAttMtu_(kBleDefaultAttMtu),
      connectionCentralLinkSetupNotBeforeMs_(0UL),
      connectionLastTxLlid_(0x01U),
      connectionLastTxLength_(0),
      connectionLastTxPlainLlid_(0x01U),
      connectionLastTxPlainLength_(0U),
      connectionLastTxPlainPayload_{0},
      connectionPendingTxLlid_(0x01U),
      connectionPendingTxLength_(0U),
      connectionPendingTxValid_(false),
      connectionPendingTxPayload_{0},
      connectionUpdatePending_(false),
      connectionUpdateInstant_(0U),
      connectionPendingWinSize_(0U),
      connectionPendingWinOffset_(0U),
      connectionPendingIntervalUnits_(0U),
      connectionPendingLatency_(0U),
      connectionPendingTimeoutUnits_(0U),
      connectionChannelMapPending_(false),
      connectionChannelMapInstant_(0U),
      connectionPendingChannelMap_{0},
      connectionPendingChannelCount_(0U),
      connectionServiceChangedIndicationsEnabled_(false),
      connectionServiceChangedIndicationPending_(false),
      connectionServiceChangedIndicationAwaitingConfirm_(false),
      connectionBatteryNotificationsEnabled_(false),
      connectionBatteryNotificationPending_(false),
      connectionPreparedWriteActive_(false),
      connectionPreparedWriteHandle_(0U),
      connectionPreparedWriteValue_{0},
      connectionPreparedWriteMask_(0U),
      backgroundAdvertisingEnabled_(false),
      backgroundAdvertisingArmed_(false),
      backgroundAdvertisingInProgress_(false),
      backgroundAdvertisingDue_(false),
      backgroundAdvertisingThreeChannel_(false),
      backgroundAdvertisingRandomDelay_(false),
      backgroundAdvertisingManageRfPath_(false),
      backgroundAdvertisingRfPathCollapsed_(false),
      backgroundAdvertisingUseIrqTxKick_(bleBackgroundAdvertisingInitialUseIrqTxKick()),
      backgroundAdvertisingManageLatency_(false),
      backgroundAdvertisingConstlatHardwareVerified_(false),
      backgroundAdvertisingAwaitingHfxoTuned_(false),
      backgroundAdvertisingChannel_(BleAdvertisingChannel::k37),
      backgroundAdvertisingIntervalUs_(0U),
      backgroundAdvertisingInterChannelDelayUs_(0U),
      backgroundAdvertisingHfxoLeadUs_(kBleBackgroundAdvDefaultHfxoLeadUs),
      backgroundAdvertisingNextTxUs_(0U),
      backgroundAdvertisingServiceUs_(0U),
      backgroundAdvertisingEventStartTxUs_(0U),
      backgroundAdvertisingRandomState_(0U),
      backgroundAdvertisingPrimaryStage_(0U),
      backgroundAdvertisingLastStopReason_(kBleBackgroundAdvStopReasonNone),
      backgroundAdvertisingTxKickRetryCountCurrent_(0U),
      backgroundAdvertisingPendingTxCompareChannel_(kBleBackgroundInvalidCompareChannel),
      backgroundAdvertisingPendingServiceCompareChannel_(kBleBackgroundInvalidCompareChannel),
      backgroundAdvertisingDebug_{0},
      backgroundConnectionServiceEnabled_(false),
      backgroundConnectionServiceArmed_(false),
      backgroundConnectionServiceInProgress_(false),
      backgroundConnectionServiceDue_(false),
      backgroundConnectionRxPrepared_(false),
      deferredConnectionEventHead_(0U),
      deferredConnectionEventTail_(0U),
      deferredConnectionEventCount_(0U),
      backgroundConnectionPreparedDataChannel_(0U),
      backgroundConnectionWakeUs_(0U),
      deferredConnectionPayload_{0},
      deferredConnectionTxPayload_{0},
      consumedDeferredConnectionPayload_{0},
      consumedDeferredConnectionTxPayload_{0},
      deferredConnectionEvents_{},
      deferredGattWrites_{},
      deferredTraces_{},
      deferredGattWriteHead_(0U),
      deferredGattWriteTail_(0U),
      deferredGattWriteCount_(0U),
      deferredTraceHead_(0U),
      deferredTraceTail_(0U),
      deferredTraceCount_(0U),
      lastDisconnectReason_(0U),
      lastDisconnectReasonValid_(false),
      lastDisconnectReasonRemote_(false),
      smpPairingState_(kSmpPairingStateIdle),
      smpPairingReq_{0},
      smpPairingRsp_{0},
      smpPeerConfirm_{0},
      smpPeerRandom_{0},
      smpLocalRandom_{0},
      smpPrefetchedLocalRandomValid_(false),
      smpPrefetchedLocalRandom_{0},
      smpLocalConfirm_{0},
      smpStk_{0},
      smpStkValid_(false),
      connectionEncSessionValid_(false),
      connectionEncRxEnabled_(false),
      connectionEncTxEnabled_(false),
      connectionEncStartReqPending_(false),
      connectionEncStartReqTxPending_(false),
      connectionEncAwaitingStartRsp_(false),
      connectionEncEnableTxOnNextEvent_(false),
      connectionEncRxCounter_(0ULL),
      connectionEncTxCounter_(0ULL),
      connectionEncKeyDerivationPending_(false),
      connectionEncSkd_{0},
      connectionEncSessionKey_{0},
      connectionEncSessionKeyAlt_{0},
      connectionEncAltKeyValid_(false),
      connectionEncRxDirection_(1U),
      connectionEncTxDirection_(0U),
      connectionEncIv_{0},
      connectionPrefetchedEncMaterialValid_(false),
      connectionPrefetchedSkds_{0},
      connectionPrefetchedIvs_{0},
      connectionLastTxWasEncrypted_(false),
      connectionLastTxEncryptedLength_(0U),
      connectionLastTxEncryptedPayload_{0},
      connectionEncPrecomputedEmptyValid_(false),
      connectionEncPrecomputedCounter_(0ULL),
      connectionEncPrecomputedPayload_{0},
      connectionEncPrecomputedStartRspValid_(false),
      connectionEncPrecomputedStartRsp_{0},
      connectionEncPrecomputedStartRspTxValid_(false),
      connectionEncPrecomputedStartRspTxCounter_(0ULL),
      connectionEncPrecomputedStartRspTx_{0},
      bondLoadCallback_(nullptr),
      bondSaveCallback_(nullptr),
      bondClearCallback_(nullptr),
      bondCallbackContext_(nullptr),
      bondRecord_{},
      bondRecordValid_(false),
      bondStorageLoaded_(false),
      bondKeyPrimedForConnection_(false),
      traceCallback_(nullptr),
      traceCallbackContext_(nullptr),
      smpBondingRequested_(false),
      smpExpectInitiatorEncKey_(false),
      smpPeerLtkValid_(false),
      smpPeerLtkAwaitMasterId_(false),
      smpPeerLtk_{0},
      smpEncReqRand_{0},
      smpEncReqEdiv_(0U),
      smpKeySize_(16U),
      advCycleStartIndex_(0U),
      scanCycleStartIndex_(0U),
      gapDeviceName_{0},
      gapDeviceNameLen_(0),
      gapAppearance_(0),
      gapPpcpIntervalMin_(kBlePreferredConnIntervalMinUnits),
      gapPpcpIntervalMax_(kBlePreferredConnIntervalMaxUnits),
      gapPpcpLatency_(kBlePreferredConnLatency),
      gapPpcpTimeout_(kBlePreferredConnTimeoutUnits),
      gapBatteryLevel_(100U),
      customGattServices_{},
      customGattCharacteristics_{},
      customGattWriteHandlers_{},
      customGattServiceCount_(0U),
      customGattCharacteristicCount_(0U),
      customGattNextHandle_(kCustomGattHandleStart),
      connectionCustomNotificationQueue_{},
      connectionCustomNotificationQueueHead_(0U),
      connectionCustomNotificationQueueTail_(0U),
      connectionCustomNotificationQueueCount_(0U),
      connectionCustomIndicationAwaitingHandle_(0U),
      connectionSmpSecurityRequestPending_(false),
      customGattWriteCallback_(nullptr),
      customGattWriteContext_(nullptr),
      encDebug_{},
      queuedConnectionEvents_{},
      queuedConnectionEventPayloadScratch_{0},
      queuedConnectionEventTxPayloadScratch_{0},
      queuedConnectionEventHead_(0U),
      queuedConnectionEventTail_(0U),
      queuedConnectionEventCount_(0U),
      latestConnectionRssiDbm_(0),
      latestConnectionRssiValid_(false),
      connectionServiceBusy_(false),
      disconnectDebug_{},
      lastObservedRxLlid_(0U),
      lastObservedRxLength_(0U),
      lastObservedRxOpcode_(0U),
      lastObservedRxNesn_(0U),
      lastObservedRxSn_(0U),
      lastObservedPacketIsNew_(false),
      lastObservedPeerAckedLastTx_(false) {}

bool BleRadio::ensureRfPathActiveForBle() {
  const bool rfPowerEnabled = BoardControl::rfSwitchPowerEnabled();
  const BoardAntennaPath currentPath = BoardControl::antennaPath();
  if (rfPowerEnabled && currentPath == boardDesiredAntennaPath()) {
    return true;
  }
  if (!BoardControl::enableRfPath(boardDesiredAntennaPath())) {
    return false;
  }
  if (!rfPowerEnabled) {
    rfPathOwnedByBle_ = true;
  }
  return true;
}

void BleRadio::captureLegacyAdvertisingConfigSnapshot(
    BleLegacyAdvertisingConfigSnapshot* out) const {
  if (out == nullptr) {
    return;
  }

  out->addressType = addressType_;
  out->pduType = pduType_;
  out->useChSel2 = useChSel2_;
  out->scanRspDataAutoDefault = scanRspDataAutoDefault_;
  memcpy(out->address, address_, sizeof(address_));
  out->advDataLen = advDataLen_;
  if (advDataLen_ > 0U) {
    memcpy(out->advData, advData_, advDataLen_);
  }
  out->scanRspDataLen = scanRspDataLen_;
  if (scanRspDataLen_ > 0U) {
    memcpy(out->scanRspData, scanRspData_, scanRspDataLen_);
  }
}

void BleRadio::restoreLegacyAdvertisingConfigSnapshot(
    const BleLegacyAdvertisingConfigSnapshot& snapshot) {
  addressType_ = snapshot.addressType;
  pduType_ = snapshot.pduType;
  useChSel2_ = snapshot.useChSel2;
  scanRspDataAutoDefault_ = snapshot.scanRspDataAutoDefault;
  memcpy(address_, snapshot.address, sizeof(address_));
  advDataLen_ = snapshot.advDataLen;
  if (snapshot.advDataLen > 0U) {
    memcpy(advData_, snapshot.advData, snapshot.advDataLen);
  }
  scanRspDataLen_ = snapshot.scanRspDataLen;
  if (snapshot.scanRspDataLen > 0U) {
    memcpy(scanRspData_, snapshot.scanRspData, snapshot.scanRspDataLen);
  }
  (void)buildAdvertisingPacket();
  (void)buildScanResponsePacket();
}

void BleRadio::captureBackgroundAdvertisingRestartState(
    BleBackgroundAdvertisingRestartState* out) const {
  if (out == nullptr) {
    return;
  }

  out->active = backgroundAdvertisingEnabled_;
  out->threeChannel = backgroundAdvertisingThreeChannel_;
  out->randomDelay = backgroundAdvertisingRandomDelay_;
  out->channel = backgroundAdvertisingChannel_;
  out->intervalMs = backgroundAdvertisingIntervalUs_ / 1000UL;
  out->interChannelDelayUs = backgroundAdvertisingInterChannelDelayUs_;
  out->hfxoLeadUs = backgroundAdvertisingHfxoLeadUs_;
}

bool BleRadio::restartBackgroundAdvertisingFromState(
    const BleBackgroundAdvertisingRestartState& state) {
  if (!state.active) {
    return true;
  }
  if (state.threeChannel) {
    return beginBackgroundAdvertising3Channel(state.intervalMs,
                                              state.interChannelDelayUs,
                                              state.hfxoLeadUs,
                                              state.randomDelay);
  }
  return beginBackgroundAdvertising(state.intervalMs, state.channel,
                                    state.hfxoLeadUs,
                                    state.randomDelay);
}

void BleRadio::snapshotBackgroundAdvertisingRfPathRestoreState() {
  backgroundAdvertisingRestoreRfPathValid_ = true;
  backgroundAdvertisingRestoreRfPathPowerEnabled_ =
      BoardControl::rfSwitchPowerEnabled();
  backgroundAdvertisingRestoreRfPath_ = BoardControl::antennaPath();
}

void BleRadio::restoreBackgroundAdvertisingRfPathRestoreState() {
  if (!backgroundAdvertisingRestoreRfPathValid_) {
    return;
  }

  if (backgroundAdvertisingRestoreRfPathPowerEnabled_) {
    if (backgroundAdvertisingRestoreRfPath_ ==
        BoardAntennaPath::kControlHighImpedance) {
      (void)BoardControl::collapseRfPathIdle(
          backgroundAdvertisingRestoreRfPath_, false);
    } else {
      (void)BoardControl::enableRfPath(backgroundAdvertisingRestoreRfPath_);
    }
  } else {
    (void)BoardControl::collapseRfPathIdle(backgroundAdvertisingRestoreRfPath_,
                                           true);
  }

  setBoardDesiredAntennaPath(backgroundAdvertisingRestoreRfPath_);
  rfPathOwnedByBle_ = false;
  backgroundAdvertisingRestoreRfPathValid_ = false;
}

void BleRadio::releaseRfPathForBle() {
  if (!rfPathOwnedByBle_) {
    return;
  }
  BoardControl::collapseRfPathIdle();
  rfPathOwnedByBle_ = false;
}

bool BleRadio::beginUnconnectedRadioActivity(uint32_t spinLimit) {
  if (connected_) {
    return true;
  }
  if (!ClockControl::startHfxo(true, spinLimit)) {
    return false;
  }
  // Match the per-event RF ownership model used by the phone-visible
  // non-connectable advertiser: enable the RF path for this event, then
  // collapse it again once the device returns to the disconnected idle state.
  return ensureRfPathActiveForBle();
}

void BleRadio::endUnconnectedRadioActivity() {
  if (!connected_) {
    ClockControl::stopHfxo();
    releaseRfPathForBle();
  }
}

bool BleRadio::begin(int8_t txPowerDbm) {
  connected_ = false;
  connectionRole_ = BleConnectionRole::kNone;
  connectionFreshTxAllowed_ = true;
  lastObservedRxLlid_ = 0U;
  lastObservedRxLength_ = 0U;
  lastObservedRxOpcode_ = 0U;
  lastObservedRxNesn_ = 0U;
  lastObservedRxSn_ = 0U;
  lastObservedPacketIsNew_ = false;
  lastObservedPeerAckedLastTx_ = false;
  advCycleStartIndex_ = 0U;
  scanCycleStartIndex_ = 0U;
  connectionServiceBusy_ = false;
  latestConnectionRssiDbm_ = 0;
  latestConnectionRssiValid_ = false;
  clearQueuedConnectionEvents();
  clearCustomGattConnectionState();

#if defined(NRF54L15_CLEAN_BLE_ENABLED) && (NRF54L15_CLEAN_BLE_ENABLED == 0)
  (void)txPowerDbm;
  initialized_ = false;
  return false;
#endif

  // Observe the current RF switch routing without overriding sketch-managed
  // GPIO state. Tools > Antenna still sets the startup default in initVariant().
  setBoardDesiredAntennaPath(BoardControl::antennaPath());
  externalAntenna_ =
      (boardDesiredAntennaPath() == BoardAntennaPath::kExternal);
  initBleGrtc();
  // Re-assert GRTC ACTIVE on every begin(); initBleGrtc() is one-shot but
  // end() clears it so subsequent begin() calls must restore it explicitly.
  if (NRF_GRTC != nullptr) {
    NRF54L15_GRTC_SYSCOUNTER(NRF_GRTC).ACTIVE =
        (GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Active
         << GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Pos);
  }

  if (!beginUnconnectedRadioActivity(1500000UL)) {
    return false;
  }

  if (!configureBle1M()) {
    endUnconnectedRadioActivity();
    return false;
  }
  if (!setTxPowerDbm(txPowerDbm)) {
    endUnconnectedRadioActivity();
    return false;
  }

  uint8_t fallbackAddress[6] = {0xC0, 0x54, 0x15, 0x00, 0x00, 0x00};
  if (!loadAddressFromFicr(false)) {
    if (!setDeviceAddress(fallbackAddress, BleAddressType::kRandomStatic)) {
      endUnconnectedRadioActivity();
      return false;
    }
  }

  if (advDataLen_ == 0U) {
    if (!setAdvertisingName("XIAO-nRF54L15", true)) {
      endUnconnectedRadioActivity();
      return false;
    }
  } else if (!buildAdvertisingPacket()) {
    endUnconnectedRadioActivity();
    return false;
  }

  if (scanRspDataLen_ == 0U) {
    if (!setScanResponseName("XIAO-nRF54L15")) {
      endUnconnectedRadioActivity();
      return false;
    }
    scanRspDataAutoDefault_ = true;
  } else if (!buildScanResponsePacket()) {
    endUnconnectedRadioActivity();
    return false;
  }

  if (gapDeviceNameLen_ == 0U) {
    if (!setGattDeviceName("XIAO-nRF54L15")) {
      endUnconnectedRadioActivity();
      return false;
    }
  }

  loadBondRecordFromPersistence();
  initialized_ = true;
  g_activeBleRadio = this;
  return true;
}

void BleRadio::end() {
  if (radio_ == nullptr) {
    connected_ = false;
    stopBackgroundAdvertising();
    stopBackgroundConnectionService();
    initialized_ = false;
    latestConnectionRssiDbm_ = 0;
    latestConnectionRssiValid_ = false;
    connectionServiceBusy_ = false;
    clearQueuedConnectionEvents();
    clearCustomGattConnectionState();
    if (g_activeBleRadio == this) {
      g_activeBleRadio = nullptr;
    }
    return;
  }

  radio_->SHORTS = 0U;
  radio_->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  waitDisabled(120000UL);
  clearRadioCoreEvents(radio_);
  connected_ = false;
  stopBackgroundAdvertising();
  stopBackgroundConnectionService();
  initialized_ = false;
  latestConnectionRssiDbm_ = 0;
  latestConnectionRssiValid_ = false;
  connectionServiceBusy_ = false;
  clearQueuedConnectionEvents();
  clearCustomGattConnectionState();
  if (g_activeBleRadio == this) {
    g_activeBleRadio = nullptr;
  }
}

bool BleRadio::setTxPowerDbm(int8_t dbm) {
  if (radio_ == nullptr) {
    return false;
  }
  const uint32_t regValue = txPowerRegFromDbm(dbm);
  radio_->TXPOWER = ((regValue << RADIO_TXPOWER_TXPOWER_Pos) &
                     RADIO_TXPOWER_TXPOWER_Msk);
  return true;
}

bool BleRadio::selectExternalAntenna(bool external) {
  if (!Gpio::configure(kPinRfSwitchCtl, GpioDirection::kOutput,
                       GpioPull::kDisabled)) {
    return false;
  }
  if (!Gpio::write(kPinRfSwitchCtl, external)) {
    return false;
  }

  externalAntenna_ = external;
  return true;
}

uint64_t hardwareUniqueId64() {
  const NRF_FICR_Type* const ficr =
      reinterpret_cast<const NRF_FICR_Type*>(static_cast<uintptr_t>(nrf54l15::FICR_BASE));
  if (ficr == nullptr) {
    return 0U;
  }
  return static_cast<uint64_t>(ficr->INFO.DEVICEID[0]) |
         (static_cast<uint64_t>(ficr->INFO.DEVICEID[1]) << 32U);
}

uint64_t zigbeeFactoryEui64() {
  const NRF_FICR_Type* const ficr =
      reinterpret_cast<const NRF_FICR_Type*>(static_cast<uintptr_t>(nrf54l15::FICR_BASE));
  if (ficr == nullptr) {
    return 0U;
  }

  const uint32_t lo = ficr->DEVICEADDR[0];
  const uint32_t hi = ficr->DEVICEADDR[1];
  const uint8_t address[6] = {
      static_cast<uint8_t>(lo & 0xFFU),
      static_cast<uint8_t>((lo >> 8U) & 0xFFU),
      static_cast<uint8_t>((lo >> 16U) & 0xFFU),
      static_cast<uint8_t>((lo >> 24U) & 0xFFU),
      static_cast<uint8_t>(hi & 0xFFU),
      static_cast<uint8_t>((hi >> 8U) & 0xFFU),
  };

  if (!isAllBytes(address, sizeof(address), 0x00U) &&
      !isAllBytes(address, sizeof(address), 0xFFU)) {
    const uint8_t eui[8] = {
        address[0], address[1], address[2], 0xFFU,
        0xFEU,      address[3], address[4], address[5],
    };
    return readLittleEndian64(eui, sizeof(eui));
  }

  return hardwareUniqueId64();
}

bool BleRadio::loadAddressFromFicr(bool forceRandomStatic) {
  if (ficr_ == nullptr) {
    return false;
  }

  const uint32_t lo = ficr_->DEVICEADDR[0];
  const uint32_t hi = ficr_->DEVICEADDR[1];

  uint8_t address[6];
  address[0] = static_cast<uint8_t>(lo & 0xFFU);
  address[1] = static_cast<uint8_t>((lo >> 8U) & 0xFFU);
  address[2] = static_cast<uint8_t>((lo >> 16U) & 0xFFU);
  address[3] = static_cast<uint8_t>((lo >> 24U) & 0xFFU);
  address[4] = static_cast<uint8_t>(hi & 0xFFU);
  address[5] = static_cast<uint8_t>((hi >> 8U) & 0xFFU);

  BleAddressType type = ((ficr_->DEVICEADDRTYPE & 0x1UL) != 0U)
                            ? BleAddressType::kRandomStatic
                            : BleAddressType::kPublic;
  if (forceRandomStatic) {
    type = BleAddressType::kRandomStatic;
  }

  return setDeviceAddress(address, type);
}

bool BleRadio::setDeviceAddress(const uint8_t address[6], BleAddressType type) {
  if (address == nullptr) {
    return false;
  }

  BleLegacyAdvertisingConfigSnapshot configSnapshot{};
  BleBackgroundAdvertisingRestartState restartState{};
  captureLegacyAdvertisingConfigSnapshot(&configSnapshot);
  captureBackgroundAdvertisingRestartState(&restartState);
  if (restartState.active) {
    stopBackgroundAdvertising();
  }

  memcpy(address_, address, sizeof(address_));
  if (type == BleAddressType::kRandomStatic) {
    address_[5] = static_cast<uint8_t>((address_[5] & 0x3FU) | 0xC0U);
  }
  addressType_ = type;
  const bool packetsOk = buildAdvertisingPacket() && buildScanResponsePacket();
  if (!packetsOk) {
    restoreLegacyAdvertisingConfigSnapshot(configSnapshot);
    (void)restartBackgroundAdvertisingFromState(restartState);
    return false;
  }
  if (restartBackgroundAdvertisingFromState(restartState)) {
    return true;
  }

  restoreLegacyAdvertisingConfigSnapshot(configSnapshot);
  (void)restartBackgroundAdvertisingFromState(restartState);
  return false;
}

bool BleRadio::setDeviceAddressString(const char* addressText, BleAddressType type) {
  uint8_t address[kBleLegacyAddressLength];
  if (!parseBleAddressString(addressText, address)) {
    return false;
  }
  return setDeviceAddress(address, type);
}

bool BleRadio::getDeviceAddress(uint8_t addressOut[6], BleAddressType* typeOut) const {
  if (addressOut == nullptr) {
    return false;
  }
  memcpy(addressOut, address_, sizeof(address_));
  if (typeOut != nullptr) {
    *typeOut = addressType_;
  }
  return true;
}

bool BleRadio::getDeviceAddressString(char* out, size_t outSize,
                                      BleAddressType* typeOut) const {
  if (typeOut != nullptr) {
    *typeOut = addressType_;
  }
  return formatBleAddressString(address_, out, outSize) > 0U;
}

bool BleRadio::setAdvertisingPduType(BleAdvPduType type) {
  const uint8_t raw = static_cast<uint8_t>(type);
  if (raw > 0x0FU) {
    return false;
  }

  BleLegacyAdvertisingConfigSnapshot configSnapshot{};
  BleBackgroundAdvertisingRestartState restartState{};
  captureLegacyAdvertisingConfigSnapshot(&configSnapshot);
  captureBackgroundAdvertisingRestartState(&restartState);
  if (restartState.active) {
    stopBackgroundAdvertising();
  }

  pduType_ = type;
  const bool packetOk = buildAdvertisingPacket();
  if (!packetOk) {
    restoreLegacyAdvertisingConfigSnapshot(configSnapshot);
    (void)restartBackgroundAdvertisingFromState(restartState);
    return false;
  }
  if (restartBackgroundAdvertisingFromState(restartState)) {
    return true;
  }

  restoreLegacyAdvertisingConfigSnapshot(configSnapshot);
  (void)restartBackgroundAdvertisingFromState(restartState);
  return false;
}

bool BleRadio::setAdvertisingChannelSelectionAlgorithm2(bool enabled) {
  BleLegacyAdvertisingConfigSnapshot configSnapshot{};
  BleBackgroundAdvertisingRestartState restartState{};
  captureLegacyAdvertisingConfigSnapshot(&configSnapshot);
  captureBackgroundAdvertisingRestartState(&restartState);
  if (restartState.active) {
    stopBackgroundAdvertising();
  }

  useChSel2_ = enabled;
  const bool packetOk = buildAdvertisingPacket();
  if (!packetOk) {
    restoreLegacyAdvertisingConfigSnapshot(configSnapshot);
    (void)restartBackgroundAdvertisingFromState(restartState);
    return false;
  }
  if (restartBackgroundAdvertisingFromState(restartState)) {
    return true;
  }

  restoreLegacyAdvertisingConfigSnapshot(configSnapshot);
  (void)restartBackgroundAdvertisingFromState(restartState);
  return false;
}

bool BleRadio::setAdvertisingData(const uint8_t* data, size_t len) {
  if (len > sizeof(advData_)) {
    return false;
  }
  if (len > 0U && data == nullptr) {
    return false;
  }

  BleLegacyAdvertisingConfigSnapshot configSnapshot{};
  BleBackgroundAdvertisingRestartState restartState{};
  captureLegacyAdvertisingConfigSnapshot(&configSnapshot);
  captureBackgroundAdvertisingRestartState(&restartState);
  if (restartState.active) {
    stopBackgroundAdvertising();
  }

  if (len > 0U) {
    memcpy(advData_, data, len);
  }
  advDataLen_ = len;
  const bool packetOk = buildAdvertisingPacket();
  if (!packetOk) {
    restoreLegacyAdvertisingConfigSnapshot(configSnapshot);
    (void)restartBackgroundAdvertisingFromState(restartState);
    return false;
  }
  if (restartBackgroundAdvertisingFromState(restartState)) {
    return true;
  }

  restoreLegacyAdvertisingConfigSnapshot(configSnapshot);
  (void)restartBackgroundAdvertisingFromState(restartState);
  return false;
}

bool BleRadio::setAdvertisingName(const char* name, bool includeFlags) {
  if (name == nullptr) {
    return false;
  }

  uint8_t payload[kBleLegacyAdDataMaxLength];
  size_t used = 0;

  if (includeFlags) {
    payload[used++] = 2U;
    payload[used++] = 0x01U;
    payload[used++] = 0x06U;
  }

  const size_t nameLen = strlen(name);
  if (used >= sizeof(payload)) {
    return false;
  }

  const size_t remaining = sizeof(payload) - used;
  if (remaining < 2U) {
    return false;
  }

  size_t copyLen = nameLen;
  uint8_t adType = 0x09U;  // Complete local name.
  if ((copyLen + 2U) > remaining) {
    copyLen = remaining - 2U;
    adType = 0x08U;  // Shortened local name.
  }

  payload[used++] = static_cast<uint8_t>(copyLen + 1U);
  payload[used++] = adType;
  if (copyLen > 0U) {
    memcpy(&payload[used], name, copyLen);
    used += copyLen;
  }
  const bool ok = setAdvertisingData(payload, used);
  if (ok && gapDeviceNameLen_ == 0U) {
    setGattDeviceName(name);
  }
  return ok;
}

bool BleRadio::setExtendedAdvertisingSid(uint8_t sid) {
  if (sid >= 16U) {
    return false;
  }
  extendedAdvSid_ = sid;
  return true;
}

bool BleRadio::setExtendedAdvertisingAuxChannel(uint8_t dataChannel) {
  if (dataChannel > 36U) {
    return false;
  }
  extendedAdvAuxChannel_ = dataChannel;
  return true;
}

bool BleRadio::setExtendedAdvertisingData(const uint8_t* data, size_t len) {
  if (len > sizeof(extendedAdvData_)) {
    return false;
  }
  if ((len > 0U) && (data == nullptr)) {
    return false;
  }

  if (len > 0U) {
    memcpy(extendedAdvData_, data, len);
  }
  extendedAdvDataLen_ = len;
  extendedAdvDid_ = static_cast<uint16_t>((extendedAdvDid_ + 1U) & 0x0FFFU);
  return true;
}

bool BleRadio::setExtendedAdvertisingName(const char* name, bool includeFlags) {
  if (name == nullptr) {
    return false;
  }

  uint8_t payload[kBleExtendedAdvDataMaxLength];
  size_t used = 0U;

  if (includeFlags) {
    payload[used++] = 2U;
    payload[used++] = 0x01U;
    payload[used++] = 0x06U;
  }

  const size_t nameLen = strlen(name);
  if (used >= sizeof(payload)) {
    return false;
  }

  const size_t remaining = sizeof(payload) - used;
  if (remaining < 2U) {
    return false;
  }

  size_t copyLen = nameLen;
  uint8_t adType = 0x09U;
  if ((copyLen + 2U) > remaining) {
    copyLen = remaining - 2U;
    adType = 0x08U;
  }

  payload[used++] = static_cast<uint8_t>(copyLen + 1U);
  payload[used++] = adType;
  if (copyLen > 0U) {
    memcpy(&payload[used], name, copyLen);
    used += copyLen;
  }

  const bool ok = setExtendedAdvertisingData(payload, used);
  if (ok && (gapDeviceNameLen_ == 0U)) {
    setGattDeviceName(name);
  }
  return ok;
}

bool BleRadio::setExtendedScanResponseData(const uint8_t* data, size_t len) {
  if (len > sizeof(extendedScanRspData_)) {
    return false;
  }
  if ((len > 0U) && (data == nullptr)) {
    return false;
  }

  if (len > 0U) {
    memcpy(extendedScanRspData_, data, len);
  }
  extendedScanRspDataLen_ = len;
  extendedAdvDid_ = static_cast<uint16_t>((extendedAdvDid_ + 1U) & 0x0FFFU);
  return true;
}

bool BleRadio::setExtendedScanResponseName(const char* name) {
  if (name == nullptr) {
    return false;
  }

  uint8_t payload[kBleExtendedAdvDataMaxLength];
  size_t used = 0U;
  size_t copyLen = strlen(name);
  uint8_t adType = 0x09U;
  if (copyLen > 254U) {
    copyLen = 254U;
    adType = 0x08U;
  }

  payload[used++] = static_cast<uint8_t>(copyLen + 1U);
  payload[used++] = adType;
  if (copyLen > 0U) {
    memcpy(&payload[used], name, copyLen);
    used += copyLen;
  }
  return setExtendedScanResponseData(payload, used);
}

bool BleRadio::setGattDeviceName(const char* name) {
  if (name == nullptr) {
    return false;
  }

  const size_t len = strlen(name);
  if (len > sizeof(gapDeviceName_)) {
    return false;
  }
  if (len > 0U) {
    memcpy(gapDeviceName_, name, len);
  }
  gapDeviceNameLen_ = static_cast<uint8_t>(len);
  if (scanRspDataAutoDefault_) {
    if (!setScanResponseName(name)) {
      return false;
    }
    scanRspDataAutoDefault_ = true;
  }
  return true;
}

bool BleRadio::setGattAppearance(uint16_t appearance) {
  gapAppearance_ = appearance;
  return true;
}

bool BleRadio::setGattBatteryLevel(uint8_t percent) {
  if (percent > 100U) {
    return false;
  }
  const uint32_t irqState = bleEnterCritical();
  if (gapBatteryLevel_ != percent) {
    gapBatteryLevel_ = percent;
    if (connected_ && connectionBatteryNotificationsEnabled_) {
      connectionBatteryNotificationPending_ = true;
    }
    bleExitCritical(irqState);
    return true;
  }
  gapBatteryLevel_ = percent;
  bleExitCritical(irqState);
  return true;
}
