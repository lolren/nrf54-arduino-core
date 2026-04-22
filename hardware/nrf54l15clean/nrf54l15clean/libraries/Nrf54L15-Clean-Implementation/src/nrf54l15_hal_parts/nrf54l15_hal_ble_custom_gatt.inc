
bool BleRadio::clearCustomGatt() {
  if (connected_) {
    return false;
  }
  memset(customGattServices_, 0, sizeof(customGattServices_));
  memset(customGattCharacteristics_, 0, sizeof(customGattCharacteristics_));
  memset(customGattWriteHandlers_, 0, sizeof(customGattWriteHandlers_));
  customGattServiceCount_ = 0U;
  customGattCharacteristicCount_ = 0U;
  customGattNextHandle_ = kCustomGattHandleStart;
  clearCustomGattConnectionState();
  return true;
}

BleRadio::BleCustomServiceState* BleRadio::findCustomServiceByHandle(
    uint16_t serviceHandle) {
  for (uint8_t i = 0U; i < customGattServiceCount_; ++i) {
    if (customGattServices_[i].serviceHandle == serviceHandle) {
      return &customGattServices_[i];
    }
  }
  return nullptr;
}

const BleRadio::BleCustomServiceState* BleRadio::findCustomServiceByHandle(
    uint16_t serviceHandle) const {
  for (uint8_t i = 0U; i < customGattServiceCount_; ++i) {
    if (customGattServices_[i].serviceHandle == serviceHandle) {
      return &customGattServices_[i];
    }
  }
  return nullptr;
}

BleRadio::BleCustomCharacteristicState*
BleRadio::findCustomCharacteristicByValueHandle(uint16_t valueHandle) {
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattCharacteristics_[i].valueHandle == valueHandle) {
      return &customGattCharacteristics_[i];
    }
  }
  return nullptr;
}

const BleRadio::BleCustomCharacteristicState*
BleRadio::findCustomCharacteristicByValueHandle(uint16_t valueHandle) const {
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattCharacteristics_[i].valueHandle == valueHandle) {
      return &customGattCharacteristics_[i];
    }
  }
  return nullptr;
}

BleRadio::BleCustomWriteHandlerState*
BleRadio::findCustomGattWriteHandler(uint16_t valueHandle) {
  if (valueHandle == 0U) {
    return nullptr;
  }
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattWriteHandlers_[i].valueHandle == valueHandle) {
      return &customGattWriteHandlers_[i];
    }
  }
  return nullptr;
}

const BleRadio::BleCustomWriteHandlerState*
BleRadio::findCustomGattWriteHandler(uint16_t valueHandle) const {
  if (valueHandle == 0U) {
    return nullptr;
  }
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattWriteHandlers_[i].valueHandle == valueHandle) {
      return &customGattWriteHandlers_[i];
    }
  }
  return nullptr;
}

BleRadio::BleCustomCharacteristicState*
BleRadio::findCustomCharacteristicByCccdHandle(uint16_t cccdHandle) {
  if (cccdHandle == 0U) {
    return nullptr;
  }
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattCharacteristics_[i].cccdHandle == cccdHandle) {
      return &customGattCharacteristics_[i];
    }
  }
  return nullptr;
}

const BleRadio::BleCustomCharacteristicState*
BleRadio::findCustomCharacteristicByCccdHandle(uint16_t cccdHandle) const {
  if (cccdHandle == 0U) {
    return nullptr;
  }
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattCharacteristics_[i].cccdHandle == cccdHandle) {
      return &customGattCharacteristics_[i];
    }
  }
  return nullptr;
}

bool BleRadio::addCustomGattService(uint16_t uuid16, uint16_t* outServiceHandle) {
  if (uuid16 == 0U) {
    return false;
  }
  uint8_t uuid[kBleUuid128Size] = {0};
  bleUuid16ToBytes(uuid16, uuid);
  return addCustomGattServiceCommon(uuid, kBleUuid16Size, outServiceHandle);
}

bool BleRadio::addCustomGattService128(const uint8_t uuid128[16],
                                       uint16_t* outServiceHandle) {
  return addCustomGattServiceCommon(uuid128, kBleUuid128Size, outServiceHandle);
}

bool BleRadio::addCustomGattServiceCommon(const uint8_t* uuid, uint8_t uuidSize,
                                          uint16_t* outServiceHandle) {
  if (connected_ || uuid == nullptr || customGattServiceCount_ >= kCustomGattMaxServices ||
      (uuidSize != kBleUuid16Size && uuidSize != kBleUuid128Size)) {
    return false;
  }
  if (customGattNextHandle_ > kCustomGattHandleEnd) {
    return false;
  }

  BleCustomServiceState& service = customGattServices_[customGattServiceCount_];
  memset(&service, 0, sizeof(service));
  service.uuidSize = uuidSize;
  memcpy(service.uuid, uuid, uuidSize);
  service.serviceHandle = customGattNextHandle_;
  service.endHandle = customGattNextHandle_;
  ++customGattNextHandle_;
  ++customGattServiceCount_;

  if (outServiceHandle != nullptr) {
    *outServiceHandle = service.serviceHandle;
  }
  return true;
}

bool BleRadio::addCustomGattCharacteristic(uint16_t serviceHandle, uint16_t uuid16,
                                           uint8_t properties,
                                           const uint8_t* initialValue,
                                           uint8_t initialValueLength,
                                           uint16_t* outValueHandle,
                                           uint16_t* outCccdHandle) {
  if (uuid16 == 0U) {
    return false;
  }
  uint8_t uuid[kBleUuid128Size] = {0};
  bleUuid16ToBytes(uuid16, uuid);
  return addCustomGattCharacteristicCommon(serviceHandle, uuid, kBleUuid16Size,
                                           properties, initialValue,
                                           initialValueLength, outValueHandle,
                                           outCccdHandle);
}

bool BleRadio::addCustomGattCharacteristic128(uint16_t serviceHandle,
                                              const uint8_t uuid128[16],
                                              uint8_t properties,
                                              const uint8_t* initialValue,
                                              uint8_t initialValueLength,
                                              uint16_t* outValueHandle,
                                              uint16_t* outCccdHandle) {
  return addCustomGattCharacteristicCommon(serviceHandle, uuid128,
                                           kBleUuid128Size, properties,
                                           initialValue, initialValueLength,
                                           outValueHandle, outCccdHandle);
}

bool BleRadio::addCustomGattCharacteristicCommon(uint16_t serviceHandle,
                                                 const uint8_t* uuid,
                                                 uint8_t uuidSize,
                                                 uint8_t properties,
                                                 const uint8_t* initialValue,
                                                 uint8_t initialValueLength,
                                                 uint16_t* outValueHandle,
                                                 uint16_t* outCccdHandle) {
  constexpr uint8_t kAllowedProps = static_cast<uint8_t>(
      kBleGattPropRead | kBleGattPropWriteNoRsp | kBleGattPropWrite |
      kBleGattPropNotify | kBleGattPropIndicate);
  if (connected_ || uuid == nullptr ||
      customGattCharacteristicCount_ >= kCustomGattMaxCharacteristics ||
      (properties == 0U) || ((properties & ~kAllowedProps) != 0U) ||
      (uuidSize != kBleUuid16Size && uuidSize != kBleUuid128Size)) {
    return false;
  }
  if (uuidSize == kBleUuid16Size && readLe16(uuid) == 0U) {
    return false;
  }
  if (initialValueLength > kCustomGattMaxValueLength ||
      (initialValueLength > 0U && initialValue == nullptr)) {
    return false;
  }

  uint8_t serviceIndex = 0xFFU;
  for (uint8_t i = 0U; i < customGattServiceCount_; ++i) {
    if (customGattServices_[i].serviceHandle == serviceHandle) {
      serviceIndex = i;
      break;
    }
  }
  if (serviceIndex == 0xFFU) {
    return false;
  }
  // Keep service ranges monotonic by only appending to the newest service.
  if (serviceIndex != static_cast<uint8_t>(customGattServiceCount_ - 1U)) {
    return false;
  }

  const bool hasCccd = ((properties & (kBleGattPropNotify | kBleGattPropIndicate)) != 0U);
  const uint16_t requiredHandles = hasCccd ? 3U : 2U;
  if (customGattNextHandle_ > kCustomGattHandleEnd ||
      (static_cast<uint16_t>(kCustomGattHandleEnd - customGattNextHandle_ + 1U) <
       requiredHandles)) {
    return false;
  }

  BleCustomCharacteristicState& characteristic =
      customGattCharacteristics_[customGattCharacteristicCount_];
  memset(&characteristic, 0, sizeof(characteristic));
  characteristic.serviceHandle = serviceHandle;
  characteristic.uuidSize = uuidSize;
  memcpy(characteristic.uuid, uuid, uuidSize);
  characteristic.properties = properties;
  characteristic.declarationHandle = customGattNextHandle_++;
  characteristic.valueHandle = customGattNextHandle_++;
  characteristic.cccdHandle = hasCccd ? customGattNextHandle_++ : 0U;
  characteristic.cccdValue = 0U;
  characteristic.valueLength = initialValueLength;
  if (initialValueLength > 0U) {
    memcpy(characteristic.value, initialValue, initialValueLength);
  }
  ++customGattCharacteristicCount_;

  BleCustomServiceState& service = customGattServices_[serviceIndex];
  service.endHandle = (characteristic.cccdHandle != 0U) ? characteristic.cccdHandle
                                                         : characteristic.valueHandle;

  if (outValueHandle != nullptr) {
    *outValueHandle = characteristic.valueHandle;
  }
  if (outCccdHandle != nullptr) {
    *outCccdHandle = characteristic.cccdHandle;
  }
  return true;
}

bool BleRadio::setCustomGattCharacteristicValue(uint16_t valueHandle,
                                                const uint8_t* value,
                                                uint8_t valueLength) {
  if (valueLength > kCustomGattMaxValueLength ||
      (valueLength > 0U && value == nullptr)) {
    return false;
  }

  const uint32_t irqState = bleEnterCritical();
  BleCustomCharacteristicState* characteristic =
      findCustomCharacteristicByValueHandle(valueHandle);
  if (characteristic == nullptr) {
    bleExitCritical(irqState);
    return false;
  }
  characteristic->valueLength = valueLength;
  memset(characteristic->value, 0, sizeof(characteristic->value));
  if (valueLength > 0U) {
    memcpy(characteristic->value, value, valueLength);
  }
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::getCustomGattCharacteristicValue(uint16_t valueHandle, uint8_t* outValue,
                                                uint8_t* inOutValueLength) const {
  if (outValue == nullptr || inOutValueLength == nullptr) {
    return false;
  }
  const uint32_t irqState = bleEnterCritical();
  const BleCustomCharacteristicState* characteristic =
      findCustomCharacteristicByValueHandle(valueHandle);
  if (characteristic == nullptr) {
    bleExitCritical(irqState);
    return false;
  }
  if (*inOutValueLength < characteristic->valueLength) {
    *inOutValueLength = characteristic->valueLength;
    bleExitCritical(irqState);
    return false;
  }
  if (characteristic->valueLength > 0U) {
    memcpy(outValue, characteristic->value, characteristic->valueLength);
  }
  *inOutValueLength = characteristic->valueLength;
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::enqueueCustomGattNotification(uint8_t characteristicIndex,
                                             bool indicate,
                                             const uint8_t* value,
                                             uint8_t valueLength) {
  if (connectionCustomNotificationQueueCount_ >=
      kCustomGattNotificationQueueDepth) {
    return false;
  }

  BleQueuedCustomNotificationState& slot =
      connectionCustomNotificationQueue_[connectionCustomNotificationQueueHead_];
  slot.valid = true;
  slot.characteristicIndex = characteristicIndex;
  slot.indication = indicate;
  slot.valueLength = valueLength;
  if (valueLength > 0U && value != nullptr) {
    memcpy(slot.value, value, valueLength);
  }

  connectionCustomNotificationQueueHead_ =
      static_cast<uint8_t>((connectionCustomNotificationQueueHead_ + 1U) %
                           kCustomGattNotificationQueueDepth);
  ++connectionCustomNotificationQueueCount_;
  return true;
}

bool BleRadio::peekCustomGattNotification(
    BleQueuedCustomNotificationState* outNotification) const {
  if (outNotification == nullptr ||
      connectionCustomNotificationQueueCount_ == 0U) {
    return false;
  }

  const BleQueuedCustomNotificationState& slot =
      connectionCustomNotificationQueue_[connectionCustomNotificationQueueTail_];
  if (!slot.valid) {
    return false;
  }

  *outNotification = slot;
  return true;
}

void BleRadio::popCustomGattNotification() {
  if (connectionCustomNotificationQueueCount_ == 0U) {
    return;
  }

  BleQueuedCustomNotificationState& slot =
      connectionCustomNotificationQueue_[connectionCustomNotificationQueueTail_];
  slot.valid = false;
  slot.characteristicIndex = 0xFFU;
  slot.indication = false;
  slot.valueLength = 0U;
  memset(slot.value, 0, sizeof(slot.value));

  connectionCustomNotificationQueueTail_ =
      static_cast<uint8_t>((connectionCustomNotificationQueueTail_ + 1U) %
                           kCustomGattNotificationQueueDepth);
  --connectionCustomNotificationQueueCount_;
  if (connectionCustomNotificationQueueCount_ == 0U) {
    connectionCustomNotificationQueueHead_ = 0U;
    connectionCustomNotificationQueueTail_ = 0U;
  }
}

void BleRadio::filterCustomGattNotificationQueue(uint8_t characteristicIndex,
                                                 bool notificationsEnabled,
                                                 bool indicationsEnabled) {
  if (connectionCustomNotificationQueueCount_ == 0U) {
    return;
  }

  BleQueuedCustomNotificationState
      kept[kCustomGattNotificationQueueDepth] = {};
  uint8_t keptCount = 0U;
  for (uint8_t i = 0U; i < connectionCustomNotificationQueueCount_; ++i) {
    const uint8_t slotIndex =
        static_cast<uint8_t>((connectionCustomNotificationQueueTail_ + i) %
                             kCustomGattNotificationQueueDepth);
    const BleQueuedCustomNotificationState& slot =
        connectionCustomNotificationQueue_[slotIndex];
    if (!slot.valid) {
      continue;
    }

    bool keep = true;
    if (slot.characteristicIndex == characteristicIndex) {
      keep = slot.indication ? indicationsEnabled : notificationsEnabled;
    }
    if (keep) {
      kept[keptCount++] = slot;
    }
  }

  memset(connectionCustomNotificationQueue_, 0,
         sizeof(connectionCustomNotificationQueue_));
  connectionCustomNotificationQueueHead_ = 0U;
  connectionCustomNotificationQueueTail_ = 0U;
  connectionCustomNotificationQueueCount_ = 0U;
  for (uint8_t i = 0U; i < keptCount; ++i) {
    connectionCustomNotificationQueue_[i] = kept[i];
    connectionCustomNotificationQueue_[i].valid = true;
  }
  connectionCustomNotificationQueueHead_ = keptCount %
                                          kCustomGattNotificationQueueDepth;
  connectionCustomNotificationQueueCount_ = keptCount;
}

bool BleRadio::notifyCustomGattCharacteristic(uint16_t valueHandle, bool indicate) {
  const uint32_t irqState = bleEnterCritical();
  if (!connected_) {
    bleExitCritical(irqState);
    return false;
  }
  BleCustomCharacteristicState* characteristic =
      findCustomCharacteristicByValueHandle(valueHandle);
  if (characteristic == nullptr) {
    bleExitCritical(irqState);
    return false;
  }

  const bool notifyEnabled =
      ((characteristic->properties & kBleGattPropNotify) != 0U) &&
      ((characteristic->cccdValue & 0x0001U) != 0U);
  const bool indicateEnabled =
      ((characteristic->properties & kBleGattPropIndicate) != 0U) &&
      ((characteristic->cccdValue & 0x0002U) != 0U);
  if (indicate) {
    if (!indicateEnabled || connectionCustomIndicationAwaitingHandle_ != 0U ||
        connectionCustomNotificationQueueCount_ != 0U) {
      bleExitCritical(irqState);
      return false;
    }
  } else if (!notifyEnabled) {
    bleExitCritical(irqState);
    return false;
  }

  const ptrdiff_t idx = (characteristic - &customGattCharacteristics_[0]);
  if (idx < 0 ||
      static_cast<uint8_t>(idx) >= customGattCharacteristicCount_) {
    bleExitCritical(irqState);
    return false;
  }

  const bool queued = enqueueCustomGattNotification(
      static_cast<uint8_t>(idx), indicate, characteristic->value,
      characteristic->valueLength);
  if (queued &&
      !connectionPendingTxValid_ &&
      !connectionEncStartReqTxPending_ &&
      !connectionEncAwaitingStartRsp_ &&
      !connectionEncStartReqPending_) {
    BleQueuedCustomNotificationState queuedNow{};
    if (peekCustomGattNotification(&queuedNow) &&
        queuedNow.characteristicIndex < customGattCharacteristicCount_) {
      BleCustomCharacteristicState& pendingCharacteristic =
          customGattCharacteristics_[queuedNow.characteristicIndex];
      const bool pendingModeEnabled = queuedNow.indication
                                          ? (((pendingCharacteristic.properties &
                                               kBleGattPropIndicate) != 0U) &&
                                             ((pendingCharacteristic.cccdValue & 0x0002U) !=
                                              0U))
                                          : (((pendingCharacteristic.properties &
                                               kBleGattPropNotify) != 0U) &&
                                             ((pendingCharacteristic.cccdValue & 0x0001U) !=
                                              0U));
      const bool indicationFlowReady =
          !connectionServiceChangedIndicationAwaitingConfirm_ &&
          (connectionCustomIndicationAwaitingHandle_ == 0U);
      if (pendingModeEnabled &&
          (!queuedNow.indication || indicationFlowReady)) {
        writeLe16(&connectionPendingTxPayload_[0],
                  static_cast<uint16_t>(3U + queuedNow.valueLength));
        writeLe16(&connectionPendingTxPayload_[2], kBleL2capCidAtt);
        connectionPendingTxPayload_[4] = queuedNow.indication
                                             ? kAttOpHandleValueInd
                                             : kAttOpHandleValueNtf;
        writeLe16(&connectionPendingTxPayload_[5],
                  pendingCharacteristic.valueHandle);
        if (queuedNow.valueLength > 0U) {
          memcpy(&connectionPendingTxPayload_[7], queuedNow.value,
                 queuedNow.valueLength);
        }
        connectionPendingTxLlid_ = kBlePduDataStartOrComplete;
        connectionPendingTxLength_ =
            static_cast<uint8_t>(7U + queuedNow.valueLength);
        connectionPendingTxValid_ = true;
        if (queuedNow.indication) {
          connectionCustomIndicationAwaitingHandle_ =
              pendingCharacteristic.valueHandle;
        }
        popCustomGattNotification();
      }
    }
  }
  bleExitCritical(irqState);
  return queued;
}

bool BleRadio::sendSmpSecurityRequest() {
  const uint32_t irqState = bleEnterCritical();
  // No-op if already bonded/keyed, encryption is up, or not connected.
  if (!connected_ || smpStkValid_ || connectionEncSessionValid_) {
    bleExitCritical(irqState);
    return false;
  }
  connectionSmpSecurityRequestPending_ = true;
  bleExitCritical(irqState);
  return true;
}

bool BleRadio::isCustomGattNotificationQueued(uint16_t valueHandle, bool indicate) const {
  const uint32_t irqState = bleEnterCritical();
  const BleCustomCharacteristicState* characteristic =
      findCustomCharacteristicByValueHandle(valueHandle);
  if (characteristic == nullptr) {
    bleExitCritical(irqState);
    return false;
  }

  bool queued = false;
  const ptrdiff_t idx = (characteristic - &customGattCharacteristics_[0]);
  if (idx >= 0 &&
      static_cast<uint8_t>(idx) < customGattCharacteristicCount_) {
    for (uint8_t i = 0U; i < connectionCustomNotificationQueueCount_; ++i) {
      const uint8_t slotIndex =
          static_cast<uint8_t>((connectionCustomNotificationQueueTail_ + i) %
                               kCustomGattNotificationQueueDepth);
      const BleQueuedCustomNotificationState& slot =
          connectionCustomNotificationQueue_[slotIndex];
      if (slot.valid &&
          (slot.characteristicIndex == static_cast<uint8_t>(idx)) &&
          (slot.indication == indicate)) {
        queued = true;
        break;
      }
    }
  }

  if (!queued &&
      connectionPendingTxValid_ &&
      (connectionPendingTxLlid_ == kBlePduDataStartOrComplete) &&
      (connectionPendingTxLength_ >= 7U) &&
      (readLe16(&connectionPendingTxPayload_[2]) == kBleL2capCidAtt)) {
    const uint8_t expectedOpcode =
        indicate ? kAttOpHandleValueInd : kAttOpHandleValueNtf;
    queued = (connectionPendingTxPayload_[4] == expectedOpcode) &&
             (readLe16(&connectionPendingTxPayload_[5]) == valueHandle);
  }

  if (!queued && indicate &&
      (connectionCustomIndicationAwaitingHandle_ == valueHandle)) {
    queued = true;
  }

  bleExitCritical(irqState);
  return queued;
}

uint16_t BleRadio::currentDataLength() const {
  return currentTxDataPduPayloadLength();
}

uint16_t BleRadio::currentAttMtu() const {
  return (connected_ && connectionAttMtu_ >= kBleDefaultAttMtu)
             ? connectionAttMtu_
             : kBleDefaultAttMtu;
}

uint16_t BleRadio::currentDataLengthMaxTimeUs() const {
  const uint8_t phy = connected_ ? connectionCurrentTxPhy_ : kBlePhy1M;
  return maximumDataLengthTimeUsForPhy(phy);
}

uint16_t BleRadio::localMaxDataPduPayloadLength() const {
  return static_cast<uint16_t>(kBleDataPduMaxPayload);
}

uint8_t BleRadio::supportedBlePhys(uint8_t phys) const {
  return static_cast<uint8_t>(phys &
                              static_cast<uint8_t>(kBlePhy1M | kBlePhy2M | kBlePhyCoded));
}

uint8_t BleRadio::symmetricBlePhys(uint8_t txPhys, uint8_t rxPhys) const {
  return supportedBlePhys(static_cast<uint8_t>(txPhys & rxPhys));
}

uint8_t BleRadio::preferredSymmetricBlePhys() const {
  const uint8_t symmetric =
      symmetricBlePhys(connectionPreferredTxPhyMask_, connectionPreferredRxPhyMask_);
  return (symmetric != kBlePhyNone) ? symmetric : kBlePhy1M;
}

uint8_t BleRadio::normalizedLocalBlePhys(uint8_t phys) const {
  const uint8_t supported = supportedBlePhys(phys);
  return (supported != kBlePhyNone) ? supported : kBlePhy1M;
}

uint8_t BleRadio::selectPreferredBlePhy(uint8_t phys) const {
  const uint8_t supported = supportedBlePhys(phys);
  if ((supported & kBlePhy2M) != 0U) {
    return kBlePhy2M;
  }
  if ((supported & kBlePhyCoded) != 0U) {
    return kBlePhyCoded;
  }
  if ((supported & kBlePhy1M) != 0U) {
    return kBlePhy1M;
  }
  return kBlePhyNone;
}

bool BleRadio::connectionUsesCodedPhy() const {
  return connected_ &&
         (connectionCurrentTxPhy_ == kBlePhyCoded) &&
         (connectionCurrentRxPhy_ == kBlePhyCoded);
}

uint16_t BleRadio::minimumDataLengthTimeUsForPhy(uint8_t phy) const {
  if ((phy & kBlePhyCoded) != 0U) {
    return kBleCodedDataLengthMaxTimeUs;
  }
  if ((phy & kBlePhy2M) != 0U) {
    return static_cast<uint16_t>(168U);
  }
  return static_cast<uint16_t>(328U);
}

uint16_t BleRadio::maximumDataLengthTimeUsForPhy(uint8_t phy) const {
  if ((phy & kBlePhyCoded) != 0U) {
    return kBleCodedDataLengthMaxTimeUs + static_cast<uint16_t>(
        (static_cast<uint16_t>(kBleDataPduMaxPayload - kBleDefaultDataPduMaxPayload) * 64U));
  }
  if ((phy & kBlePhy2M) != 0U) {
    return static_cast<uint16_t>((static_cast<uint16_t>(kBleDataPduMaxPayload) + 15U) * 4U);
  }
  return kBleDataLengthMaxTimeUs;
}

uint16_t BleRadio::payloadOctetsForDataLengthTimeUs(uint16_t maxTimeUs,
                                                    uint8_t phy) const {
  auto clampPayload = [](uint16_t payload) -> uint16_t {
    if (payload < static_cast<uint16_t>(kBleDefaultDataPduMaxPayload)) {
      return static_cast<uint16_t>(kBleDefaultDataPduMaxPayload);
    }
    return minU16(static_cast<uint16_t>(kBleDataPduMaxPayload), payload);
  };
  if ((phy & kBlePhyCoded) != 0U) {
    if (maxTimeUs <= 976U) {
      return kBleDefaultDataPduMaxPayload;
    }
    const uint16_t payload =
        static_cast<uint16_t>((maxTimeUs - 976U) >> 6U);
    return clampPayload(payload);
  }
  if ((phy & kBlePhy2M) != 0U) {
    if (maxTimeUs <= 168U) {
      return kBleDefaultDataPduMaxPayload;
    }
    const uint16_t payload =
        static_cast<uint16_t>((maxTimeUs >> 2U) - 15U);
    return clampPayload(payload);
  }
  if (maxTimeUs <= 112U) {
    return kBleDefaultDataPduMaxPayload;
  }
  const uint16_t payload =
      static_cast<uint16_t>((maxTimeUs >> 3U) - 14U);
  return clampPayload(payload);
}

uint16_t BleRadio::currentTxDataPduPayloadLength() const {
  uint16_t effective = connected_ ? connectionMaxTxPayloadLength_
                                  : static_cast<uint16_t>(kBleDefaultDataPduMaxPayload);
  if (effective < kBleDefaultDataPduMaxPayload) {
    effective = kBleDefaultDataPduMaxPayload;
  } else if (effective > localMaxDataPduPayloadLength()) {
    effective = localMaxDataPduPayloadLength();
  }
  return effective;
}

uint16_t BleRadio::currentRxDataPduPayloadLength() const {
  uint16_t effective = connected_ ? connectionMaxRxPayloadLength_
                                  : static_cast<uint16_t>(kBleDefaultDataPduMaxPayload);
  if (effective < kBleDefaultDataPduMaxPayload) {
    effective = kBleDefaultDataPduMaxPayload;
  } else if (effective > localMaxDataPduPayloadLength()) {
    effective = localMaxDataPduPayloadLength();
  }
  return effective;
}

uint16_t BleRadio::currentMaxAttPayloadLength() const {
  const uint16_t txPayloadLength = currentTxDataPduPayloadLength();
  return (txPayloadLength > kBleL2capHeaderLen)
             ? static_cast<uint16_t>(txPayloadLength - kBleL2capHeaderLen)
             : 0U;
}

uint16_t BleRadio::localPreferredAttMtu() const {
  return kBleServerPreferredMtu;
}

void BleRadio::updateConnectionDataLengthFromPeer(uint16_t peerMaxRxOctets,
                                                  uint16_t peerMaxRxTimeUs,
                                                  uint16_t peerMaxTxOctets,
                                                  uint16_t peerMaxTxTimeUs) {
  if (peerMaxRxOctets < kBleDefaultDataPduMaxPayload) {
    peerMaxRxOctets = kBleDefaultDataPduMaxPayload;
  } else if (peerMaxRxOctets > kBleDataPduMaxPayload) {
    peerMaxRxOctets = kBleDataPduMaxPayload;
  }
  if (peerMaxTxOctets < kBleDefaultDataPduMaxPayload) {
    peerMaxTxOctets = kBleDefaultDataPduMaxPayload;
  } else if (peerMaxTxOctets > kBleDataPduMaxPayload) {
    peerMaxTxOctets = kBleDataPduMaxPayload;
  }

  const uint8_t txPhy = connected_ ? connectionCurrentTxPhy_ : kBlePhy1M;
  const uint8_t rxPhy = connected_ ? connectionCurrentRxPhy_ : kBlePhy1M;
  const uint16_t minTxTimeUs = minimumDataLengthTimeUsForPhy(txPhy);
  const uint16_t minRxTimeUs = minimumDataLengthTimeUsForPhy(rxPhy);
  const uint16_t maxTxTimeUs = maximumDataLengthTimeUsForPhy(txPhy);
  const uint16_t maxRxTimeUs = maximumDataLengthTimeUsForPhy(rxPhy);
  if (peerMaxRxTimeUs < minTxTimeUs) {
    peerMaxRxTimeUs = minTxTimeUs;
  } else if (peerMaxRxTimeUs > maxTxTimeUs) {
    peerMaxRxTimeUs = maxTxTimeUs;
  }
  if (peerMaxTxTimeUs < minRxTimeUs) {
    peerMaxTxTimeUs = minRxTimeUs;
  } else if (peerMaxTxTimeUs > maxRxTimeUs) {
    peerMaxTxTimeUs = maxRxTimeUs;
  }

  connectionMaxTxPayloadLength_ =
      minU16(minU16(localMaxDataPduPayloadLength(), peerMaxRxOctets),
             payloadOctetsForDataLengthTimeUs(peerMaxRxTimeUs, txPhy));
  connectionMaxRxPayloadLength_ =
      minU16(minU16(localMaxDataPduPayloadLength(), peerMaxTxOctets),
             payloadOctetsForDataLengthTimeUs(peerMaxTxTimeUs, rxPhy));
}

bool BleRadio::queuePhyUpdateRequest() {
  if (!connected_ || connectionPendingTxValid_ ||
      !connectionPhyRequestPending_ || connectionPhyRequestInFlight_) {
    return false;
  }

  const uint8_t symmetricPhys = preferredSymmetricBlePhys();
  connectionPendingTxLlid_ = kBlePduLlControl;
  connectionPendingTxLength_ = 3U;
  connectionPendingTxPayload_[0] = kBleLlCtrlPhyReq;
  connectionPendingTxPayload_[1] = symmetricPhys;
  connectionPendingTxPayload_[2] = symmetricPhys;
  connectionPendingTxValid_ = true;
  connectionPhyRequestInFlight_ = true;
  emitBleTrace("LL_PHY_REQ_TX");
  return true;
}

bool BleRadio::queueCentralDataLengthRequest() {
  if (!connected_ || connectionPendingTxValid_ ||
      !connectionDataLengthUpdatePending_ ||
      connectionDataLengthUpdateInFlight_) {
    return false;
  }

  const uint16_t maxPayload = localMaxDataPduPayloadLength();
  const uint16_t maxTimeUs = currentDataLengthMaxTimeUs();
  connectionPendingTxLlid_ = kBlePduLlControl;
  connectionPendingTxLength_ = 9U;
  connectionPendingTxPayload_[0] = kBleLlCtrlLengthReq;
  writeLe16(&connectionPendingTxPayload_[1], maxPayload);
  writeLe16(&connectionPendingTxPayload_[3], maxTimeUs);
  writeLe16(&connectionPendingTxPayload_[5], maxPayload);
  writeLe16(&connectionPendingTxPayload_[7], maxTimeUs);
  connectionPendingTxValid_ = true;
  connectionDataLengthUpdateInFlight_ = true;
  emitBleTrace("LL_LENGTH_REQ_TX");
  return true;
}

bool BleRadio::queueCentralAttMtuRequest() {
  if (!connected_ || connectionPendingTxValid_ ||
      !connectionAttMtuExchangePending_ ||
      connectionAttMtuExchangeInFlight_) {
    return false;
  }

  uint16_t mtu = connectionRequestedAttMtu_;
  if (mtu < kBleDefaultAttMtu) {
    mtu = kBleDefaultAttMtu;
  }
  if (mtu > localPreferredAttMtu()) {
    mtu = localPreferredAttMtu();
  }
  connectionPendingTxLlid_ = kBlePduDataStartOrComplete;
  connectionPendingTxLength_ = 7U;
  writeLe16(&connectionPendingTxPayload_[0], 3U);
  writeLe16(&connectionPendingTxPayload_[2], kBleL2capCidAtt);
  connectionPendingTxPayload_[4] = kAttOpExchangeMtuReq;
  writeLe16(&connectionPendingTxPayload_[5], mtu);
  connectionPendingTxValid_ = true;
  connectionAttMtuExchangeInFlight_ = true;
  emitBleTrace("ATT_MTU_REQ_TX");
  return true;
}

bool BleRadio::connParamsAreValid(uint16_t intervalMin, uint16_t intervalMax,
                                  uint16_t latency, uint16_t timeout) const {
  if (intervalMin < 6U || intervalMin > 3200U || intervalMax < 6U ||
      intervalMax > 3200U || intervalMin > intervalMax) {
    return false;
  }
  if (latency > 499U) {
    return false;
  }
  if (timeout < 10U || timeout > 3200U) {
    return false;
  }
  const uint32_t timeoutMs = static_cast<uint32_t>(timeout) * 10UL;
  const uint32_t requiredMs =
      ((1UL + static_cast<uint32_t>(latency)) *
       static_cast<uint32_t>(intervalMax) * 5UL) /
      2UL;
  return timeoutMs > requiredMs;
}

uint16_t BleRadio::chooseAcceptedConnIntervalUnits(uint16_t intervalMin,
                                                   uint16_t intervalMax) const {
  if (intervalMax == 0U || intervalMax < intervalMin) {
    return intervalMin;
  }
  if (intervalMin == intervalMax) {
    return intervalMin;
  }
  // Prefer the slower edge of the requested range so an explicit peripheral
  // preference like 6-12 actually moves away from the central's faster default.
  return intervalMax;
}

bool BleRadio::queuePeripheralConnParamUpdateRequest() {
  if (!connected_ || connectionPendingTxValid_ ||
      !connectionConnParamUpdatePending_ ||
      connectionConnParamUpdateInFlight_) {
    return false;
  }

  uint16_t intervalMin = gapPpcpIntervalMin_;
  uint16_t intervalMax = (gapPpcpIntervalMax_ != 0U)
                             ? gapPpcpIntervalMax_
                             : gapPpcpIntervalMin_;
  uint16_t latency = gapPpcpLatency_;
  uint16_t timeout = gapPpcpTimeout_;
  if (intervalMin < 6U) {
    intervalMin = 6U;
  }
  if (intervalMax < intervalMin) {
    intervalMax = intervalMin;
  } else if (intervalMax > 3200U) {
    intervalMax = 3200U;
  }
  if (latency > 499U) {
    latency = 499U;
  }
  if (timeout < 10U) {
    timeout = 10U;
  } else if (timeout > 3200U) {
    timeout = 3200U;
  }

  uint8_t identifier = connectionConnParamUpdateIdentifier_;
  if (identifier == 0U) {
    identifier = 1U;
  }
  connectionConnParamUpdateIdentifier_ =
      static_cast<uint8_t>(identifier + 1U);
  if (connectionConnParamUpdateIdentifier_ == 0U) {
    connectionConnParamUpdateIdentifier_ = 1U;
  }

  writeLe16(&connectionPendingTxPayload_[0], 12U);
  writeLe16(&connectionPendingTxPayload_[2], kBleL2capCidLeSignaling);
  connectionPendingTxPayload_[4] = kL2capSigCodeConnParamUpdateReq;
  connectionPendingTxPayload_[5] = identifier;
  writeLe16(&connectionPendingTxPayload_[6], 8U);
  writeLe16(&connectionPendingTxPayload_[8], intervalMin);
  writeLe16(&connectionPendingTxPayload_[10], intervalMax);
  writeLe16(&connectionPendingTxPayload_[12], latency);
  writeLe16(&connectionPendingTxPayload_[14], timeout);
  connectionPendingTxLlid_ = kBlePduDataStartOrComplete;
  connectionPendingTxLength_ = 16U;
  connectionPendingTxValid_ = true;
  connectionConnParamUpdateInFlight_ = true;
  emitBleTrace("CONN_PARAM_REQ_TX");
  return true;
}

bool BleRadio::queueCentralConnParamUpdateInd() {
  if (!connected_ || (connectionRole_ != BleConnectionRole::kCentral) ||
      connectionPendingTxValid_ || !connectionCentralConnParamIndPending_) {
    return false;
  }

  const uint16_t interval = connectionCentralConnParamIntervalUnits_;
  const uint16_t latency = connectionCentralConnParamLatency_;
  const uint16_t timeout = connectionCentralConnParamTimeoutUnits_;
  if (!connParamsAreValid(interval, interval, latency, timeout)) {
    connectionCentralConnParamIndPending_ = false;
    return false;
  }

  static constexpr uint8_t kConnUpdateWinSize = 1U;
  static constexpr uint16_t kConnUpdateWinOffset = 0U;
  static constexpr uint16_t kConnUpdateInstantDeltaEvents = 6U;
  const uint16_t instant =
      static_cast<uint16_t>(connectionEventCounter_ + kConnUpdateInstantDeltaEvents);

  connectionPendingTxPayload_[0] = kBleLlCtrlConnectionUpdateInd;
  connectionPendingTxPayload_[1] = kConnUpdateWinSize;
  writeLe16(&connectionPendingTxPayload_[2], kConnUpdateWinOffset);
  writeLe16(&connectionPendingTxPayload_[4], interval);
  writeLe16(&connectionPendingTxPayload_[6], latency);
  writeLe16(&connectionPendingTxPayload_[8], timeout);
  writeLe16(&connectionPendingTxPayload_[10], instant);
  connectionPendingTxLlid_ = kBlePduLlControl;
  connectionPendingTxLength_ = 12U;
  connectionPendingTxValid_ = true;

  connectionPendingWinSize_ = kConnUpdateWinSize;
  connectionPendingWinOffset_ = kConnUpdateWinOffset;
  connectionPendingIntervalUnits_ = interval;
  connectionPendingLatency_ = latency;
  connectionPendingTimeoutUnits_ = timeout;
  connectionUpdateInstant_ = instant;
  connectionUpdatePending_ = true;
  connectionCentralConnParamIndPending_ = false;
  emitBleTrace("CONN_PARAM_IND_TX");
  return true;
}

void BleRadio::maybeQueueCentralLinkSetupRequest() {
  if (!connected_ || connectionPendingTxValid_) {
    return;
  }
  const uint32_t nowMs = millis();
  const bool setupDelayElapsed =
      (connectionCentralLinkSetupNotBeforeMs_ == 0U) ||
      (static_cast<int32_t>(nowMs - connectionCentralLinkSetupNotBeforeMs_) >= 0);

  if (connectionPhyRequestPending_ &&
      !connectionPhyRequestInFlight_) {
    (void)queuePhyUpdateRequest();
    return;
  }

  if (connectionDataLengthUpdatePending_ &&
      !connectionDataLengthUpdateInFlight_) {
    (void)queueCentralDataLengthRequest();
    return;
  }

  if (connectionAttMtuExchangePending_ &&
      !connectionAttMtuExchangeInFlight_) {
    (void)queueCentralAttMtuRequest();
    return;
  }

  if (connectionRole_ == BleConnectionRole::kPeripheral) {
    if (connectionConnParamUpdatePending_ &&
        !connectionConnParamUpdateInFlight_ &&
        setupDelayElapsed) {
      // Delay the first peripheral-side parameter request a little after the
      // link comes up, similar to other stacks, but do not block forever on
      // an ATT_MTU exchange that may never happen.
      (void)queuePeripheralConnParamUpdateRequest();
    }
    return;
  }

  if (connectionCentralConnParamIndPending_) {
    (void)queueCentralConnParamUpdateInd();
  }
}

bool BleRadio::applyPendingConnectionPhyUpdateAtInstant(uint16_t currentEventCounter) {
  if (!connectionPhyUpdatePending_ ||
      !llEventInstantReached(currentEventCounter, connectionPhyUpdateInstant_)) {
    return false;
  }

  const uint8_t txPhy = selectPreferredBlePhy(connectionPendingTxPhy_);
  const uint8_t rxPhy = selectPreferredBlePhy(connectionPendingRxPhy_);
  const bool canApplySymmetric =
      (txPhy != kBlePhyNone) &&
      (txPhy == rxPhy) &&
      configureBlePhy(txPhy);

  if (canApplySymmetric) {
    const bool changed =
        (connectionCurrentTxPhy_ != txPhy) || (connectionCurrentRxPhy_ != rxPhy);
    connectionCurrentTxPhy_ = txPhy;
    connectionCurrentRxPhy_ = rxPhy;
    if (changed) {
      connectionMaxTxPayloadLength_ = kBleDefaultDataPduMaxPayload;
      connectionMaxRxPayloadLength_ = kBleDefaultDataPduMaxPayload;
      connectionDataLengthUpdatePending_ = true;
      connectionDataLengthUpdateInFlight_ = false;
      connectionDataLengthUpdateComplete_ = false;
      emitBleTrace((txPhy == kBlePhyCoded) ? "LL_PHY_CODED_APPLIED"
                   : ((txPhy == kBlePhy2M) ? "LL_PHY_2M_APPLIED"
                                           : "LL_PHY_1M_APPLIED"));
    }
  } else {
    emitBleTrace((txPhy != rxPhy) ? "LL_PHY_ASYM_UNSUPPORTED"
                                  : "LL_PHY_APPLY_SKIPPED");
  }

  connectionPhyUpdatePending_ = false;
  connectionPhyUpdateInstant_ = 0U;
  connectionPendingTxPhy_ = connectionCurrentTxPhy_;
  connectionPendingRxPhy_ = connectionCurrentRxPhy_;
  connectionPhyRequestPending_ = false;
  connectionPhyRequestInFlight_ = false;
  return canApplySymmetric;
}

void BleRadio::applyPendingConnectionUpdateAtInstant(
    uint16_t currentEventCounter, uint32_t currentEventAnchorUs) {
  if (!connectionUpdatePending_ ||
      !llEventInstantReached(currentEventCounter, connectionUpdateInstant_)) {
    return;
  }

  if ((connectionPendingIntervalUnits_ >= 6U) &&
      (connectionPendingIntervalUnits_ <= 3200U) &&
      (connectionPendingTimeoutUnits_ >= 10U)) {
    connectionIntervalUnits_ = connectionPendingIntervalUnits_;
    connectionLatency_ = connectionPendingLatency_;
    connectionTimeoutUnits_ = connectionPendingTimeoutUnits_;
    const uint32_t newIntervalUs =
        static_cast<uint32_t>(connectionIntervalUnits_) * 1250UL;
    if (newIntervalUs > 0U) {
      const uint32_t winOffsetUs =
          static_cast<uint32_t>(connectionPendingWinOffset_) * 1250UL;
      const uint32_t winSizeUs =
          static_cast<uint32_t>((connectionPendingWinSize_ > 0U)
                                    ? connectionPendingWinSize_
                                    : 1U) * 1250UL;
      connectionNextEventUs_ = currentEventAnchorUs + winOffsetUs;
      if (connectionRole_ == BleConnectionRole::kPeripheral) {
        // When the peer central updates the anchor, keep RX open across the full
        // window so the peripheral can re-sync to the new cadence.
        connectionFirstEventListenUs_ = winOffsetUs + winSizeUs;
        connectionSyncAttemptsRemaining_ = 4U;
      } else {
        connectionFirstEventListenUs_ = 0U;
        connectionSyncAttemptsRemaining_ = 0U;
      }
      uint32_t nowUsAfterUpdate = bleTimingUs();
      uint8_t guard = 8U;
      while (guard-- > 0U &&
             timeReachedUs(nowUsAfterUpdate, connectionNextEventUs_)) {
        connectionNextEventUs_ += newIntervalUs;
        ++connectionEventCounter_;
        nowUsAfterUpdate = bleTimingUs();
      }
    }
  }
  connectionUpdatePending_ = false;
  connectionPendingWinSize_ = 0U;
  connectionPendingWinOffset_ = 0U;
  connectionPendingIntervalUnits_ = 0U;
  connectionPendingLatency_ = 0U;
  connectionPendingTimeoutUnits_ = 0U;
  connectionUpdateInstant_ = 0U;
}

uint8_t BleRadio::maxNotificationValueLength() const {
  const uint16_t mtu = currentAttMtu();
  if (mtu <= 3U) {
    return 1U;
  }
  const uint16_t txPayloadLength = currentTxDataPduPayloadLength();
  const uint16_t linkValueLimit =
      (txPayloadLength > 7U) ? static_cast<uint16_t>(txPayloadLength - 7U) : 1U;
  const uint16_t maxValueLength =
      minU16(minU16(static_cast<uint16_t>(kCustomGattMaxValueLength),
                    static_cast<uint16_t>(mtu - 3U)),
             linkValueLimit);
  return static_cast<uint8_t>((maxValueLength > 0U) ? maxValueLength : 1U);
}

bool BleRadio::isCustomGattCccdEnabled(uint16_t valueHandle, bool indication) const {
  const BleCustomCharacteristicState* characteristic =
      findCustomCharacteristicByValueHandle(valueHandle);
  if (characteristic == nullptr || characteristic->cccdHandle == 0U) {
    return false;
  }
  const uint16_t mask = indication ? 0x0002U : 0x0001U;
  return ((characteristic->cccdValue & mask) != 0U);
}

bool BleRadio::setCustomGattWriteHandler(uint16_t valueHandle,
                                         BleGattWriteCallback callback,
                                         void* context) {
  const uint32_t irqState = bleEnterCritical();
  if (findCustomCharacteristicByValueHandle(valueHandle) == nullptr) {
    bleExitCritical(irqState);
    return false;
  }

  BleCustomWriteHandlerState* existing = findCustomGattWriteHandler(valueHandle);
  if (existing != nullptr) {
    if (callback == nullptr) {
      memset(existing, 0, sizeof(*existing));
    } else {
      existing->callback = callback;
      existing->context = context;
    }
    bleExitCritical(irqState);
    return true;
  }

  if (callback == nullptr) {
    bleExitCritical(irqState);
    return true;
  }

  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    if (customGattWriteHandlers_[i].valueHandle == 0U) {
      customGattWriteHandlers_[i].valueHandle = valueHandle;
      customGattWriteHandlers_[i].callback = callback;
      customGattWriteHandlers_[i].context = context;
      bleExitCritical(irqState);
      return true;
    }
  }
  bleExitCritical(irqState);
  return false;
}

void BleRadio::setCustomGattWriteCallback(BleGattWriteCallback callback, void* context) {
  const uint32_t irqState = bleEnterCritical();
  customGattWriteCallback_ = callback;
  customGattWriteContext_ = context;
  bleExitCritical(irqState);
}

bool BleRadio::writeCustomGattCharacteristic(uint16_t handle, const uint8_t* value,
                                             uint16_t valueLength, bool withResponse,
                                             uint8_t* outErrorCode) {
  if (outErrorCode != nullptr) {
    *outErrorCode = kAttErrWriteNotPermitted;
  }

  BleCustomCharacteristicState* valueTarget =
      findCustomCharacteristicByValueHandle(handle);
  if (valueTarget != nullptr) {
    if ((valueLength > kCustomGattMaxValueLength) ||
        (valueLength > 0U && value == nullptr)) {
      if (outErrorCode != nullptr) {
        *outErrorCode = kAttErrInvalidAttrValueLen;
      }
      return false;
    }
    const bool allowWriteReq = ((valueTarget->properties & kBleGattPropWrite) != 0U);
    const bool allowWriteCmd =
        ((valueTarget->properties & kBleGattPropWriteNoRsp) != 0U);
    if ((withResponse && !allowWriteReq) || (!withResponse && !allowWriteCmd)) {
      if (outErrorCode != nullptr) {
        *outErrorCode = kAttErrWriteNotPermitted;
      }
      return false;
    }

    valueTarget->valueLength = static_cast<uint8_t>(valueLength);
    memset(valueTarget->value, 0, sizeof(valueTarget->value));
    if (valueLength > 0U) {
      memcpy(valueTarget->value, value, valueLength);
    }
    BleCustomWriteHandlerState* handler =
        findCustomGattWriteHandler(valueTarget->valueHandle);
    if (handler != nullptr && handler->callback != nullptr) {
      if (bleRunningInIsr()) {
        enqueueDeferredGattWrite(handler->callback, handler->context,
                                 valueTarget->valueHandle, valueTarget->value,
                                 valueTarget->valueLength, withResponse);
      } else {
        handler->callback(valueTarget->valueHandle, valueTarget->value,
                          valueTarget->valueLength, withResponse,
                          handler->context);
      }
    } else if (customGattWriteCallback_ != nullptr) {
      if (bleRunningInIsr()) {
        enqueueDeferredGattWrite(customGattWriteCallback_, customGattWriteContext_,
                                 valueTarget->valueHandle, valueTarget->value,
                                 valueTarget->valueLength, withResponse);
      } else {
        customGattWriteCallback_(valueTarget->valueHandle, valueTarget->value,
                                 valueTarget->valueLength, withResponse,
                                 customGattWriteContext_);
      }
    }
    return true;
  }

  BleCustomCharacteristicState* cccdTarget =
      findCustomCharacteristicByCccdHandle(handle);
  if (cccdTarget == nullptr) {
    if (outErrorCode != nullptr) {
      *outErrorCode = kAttErrWriteNotPermitted;
    }
    return false;
  }
  if (valueLength != 2U || value == nullptr) {
    if (outErrorCode != nullptr) {
      *outErrorCode = kAttErrInvalidAttrValueLen;
    }
    return false;
  }

  uint16_t allowedMask = 0U;
  if ((cccdTarget->properties & kBleGattPropNotify) != 0U) {
    allowedMask |= 0x0001U;
  }
  if ((cccdTarget->properties & kBleGattPropIndicate) != 0U) {
    allowedMask |= 0x0002U;
  }
  if (allowedMask == 0U) {
    if (outErrorCode != nullptr) {
      *outErrorCode = kAttErrWriteNotPermitted;
    }
    return false;
  }

  const uint16_t cccd = readLe16(value);
  if ((cccd & ~allowedMask) != 0U) {
    if (outErrorCode != nullptr) {
      *outErrorCode = kAttErrWriteNotPermitted;
    }
    return false;
  }
  cccdTarget->cccdValue = cccd;
  if ((cccdTarget->cccdValue & 0x0002U) == 0U &&
      connectionCustomIndicationAwaitingHandle_ == cccdTarget->valueHandle) {
    connectionCustomIndicationAwaitingHandle_ = 0U;
  }
  const ptrdiff_t idx = (cccdTarget - &customGattCharacteristics_[0]);
  if (idx >= 0 &&
      static_cast<uint8_t>(idx) < customGattCharacteristicCount_) {
    filterCustomGattNotificationQueue(static_cast<uint8_t>(idx),
                                      (cccdTarget->cccdValue & 0x0001U) != 0U,
                                      (cccdTarget->cccdValue & 0x0002U) != 0U);
  }
  return true;
}

void BleRadio::clearCustomGattConnectionState() {
  memset(connectionCustomNotificationQueue_, 0,
         sizeof(connectionCustomNotificationQueue_));
  connectionCustomNotificationQueueHead_ = 0U;
  connectionCustomNotificationQueueTail_ = 0U;
  connectionCustomNotificationQueueCount_ = 0U;
  connectionCustomIndicationAwaitingHandle_ = 0U;
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    customGattCharacteristics_[i].cccdValue = 0U;
  }
}

bool BleRadio::buildAdvertisingPacket() {
  const size_t payloadLen = sizeof(address_) + advDataLen_;
  if (payloadLen > kBleLegacyRawPayloadMaxLength) {
    return false;
  }

  uint8_t header = static_cast<uint8_t>(pduType_) & 0x0FU;
  // ChSel#2 is only defined on legacy ADV_IND and ADV_DIRECT_IND PDUs.
  const bool pduAllowsChSel2 =
      (pduType_ == BleAdvPduType::kAdvInd) ||
      (pduType_ == BleAdvPduType::kAdvDirectInd);
  if (useChSel2_ && pduAllowsChSel2) {
    header |= (1U << 5U);
  }
  if (addressType_ == BleAddressType::kRandomStatic) {
    header |= (1U << 6U);
  }

  txPacket_[0] = header;
  txPacket_[1] = static_cast<uint8_t>(payloadLen & 0x3FU);

  memcpy(&txPacket_[2], address_, sizeof(address_));
  if (advDataLen_ > 0U) {
    memcpy(&txPacket_[2 + sizeof(address_)], advData_, advDataLen_);
  }

  return true;
}

bool BleRadio::setScanResponseData(const uint8_t* data, size_t len) {
  if (len > sizeof(scanRspData_)) {
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
    memcpy(scanRspData_, data, len);
  }
  scanRspDataLen_ = len;
  scanRspDataAutoDefault_ = false;
  if (!buildScanResponsePacket()) {
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

bool BleRadio::setScanResponseName(const char* name) {
  if (name == nullptr) {
    return false;
  }

  uint8_t payload[kBleLegacyAdDataMaxLength];
  size_t used = 0;

  const size_t nameLen = strlen(name);
  if (nameLen + 2U > sizeof(payload)) {
    payload[used++] = static_cast<uint8_t>(sizeof(payload) - 1U);
    payload[used++] = 0x08U;  // Shortened local name.
    memcpy(&payload[used], name, sizeof(payload) - used);
    used = sizeof(payload);
  } else {
    payload[used++] = static_cast<uint8_t>(nameLen + 1U);
    payload[used++] = 0x09U;  // Complete local name.
    memcpy(&payload[used], name, nameLen);
    used += nameLen;
  }

  return setScanResponseData(payload, used);
}

bool BleRadio::buildScanResponsePacket() {
  const size_t payloadLen = sizeof(address_) + scanRspDataLen_;
  if (payloadLen > kBleLegacyRawPayloadMaxLength) {
    return false;
  }

  uint8_t header = kBlePduScanRsp;
  if (addressType_ == BleAddressType::kRandomStatic) {
    header |= (1U << 6U);
  }

  scanRspPacket_[0] = header;
  scanRspPacket_[1] = static_cast<uint8_t>(payloadLen & 0x3FU);
  memcpy(&scanRspPacket_[2], address_, sizeof(address_));
  if (scanRspDataLen_ > 0U) {
    memcpy(&scanRspPacket_[2 + sizeof(address_)], scanRspData_, scanRspDataLen_);
  }

  return true;
}

bool BleRadio::setAdvertisingServiceUuid128(
    const uint8_t uuid128[kCustomGattUuid128Length]) {
  if (uuid128 == nullptr) {
    return false;
  }

  // Build advertising payload: Flags (3 bytes) + Complete 128-bit UUID list (18 bytes).
  // Total = 21 bytes, well within the 31-byte limit.
  uint8_t payload[kBleLegacyAdDataMaxLength];
  size_t used = 0U;
  payload[used++] = 2U;
  payload[used++] = 0x01U;
  payload[used++] = 0x06U;

  // AD type 0x07: Complete List of 128-bit Service UUIDs.
  // BLE advertising encodes 128-bit UUIDs in little-endian (LSB first), which is
  // the byte-reversal of the canonical big-endian representation passed in by the caller.
  payload[used++] = 1U + kCustomGattUuid128Length;
  payload[used++] = 0x07U;
  for (uint8_t i = 0U; i < kCustomGattUuid128Length; ++i) {
    payload[used++] = uuid128[kCustomGattUuid128Length - 1U - i];
  }

  // After flags(3) + UUID(18) = 21 bytes, up to 10 bytes remain in the 31-byte ad payload.
  // A name ≤ 8 chars fits (2 bytes overhead + N name bytes ≤ 10).
  // Embedding the name in the ADV payload makes it visible to passive scanners
  // (e.g. Windows Bluetooth settings) that never send a SCAN_REQ.
  // Also populate the SCAN_RSP with the name when no explicit scan response has
  // been set — some active-scan-only BLE stacks (Qualcomm/Sony) will not surface
  // an ADV_IND device in the scan list unless the SCAN_RSP contains AD data.
  bool updateScanResponseName = false;
  uint8_t scanRspPayload[kBleLegacyAdDataMaxLength];
  size_t scanRspUsed = 0U;
  if (gapDeviceNameLen_ > 0U) {
    const size_t remaining = sizeof(payload) - used;
    if (gapDeviceNameLen_ + 2U <= remaining) {
      payload[used++] = static_cast<uint8_t>(gapDeviceNameLen_ + 1U);
      payload[used++] = 0x09U;  // Complete local name.
      memcpy(&payload[used], gapDeviceName_, gapDeviceNameLen_);
      used += gapDeviceNameLen_;
      // Also mirror the name into SCAN_RSP when no explicit scan response data
      // has been set, so active scanners get a non-empty SCAN_RSP reply.
      if (scanRspDataLen_ == 0U || scanRspDataAutoDefault_) {
        updateScanResponseName = true;
        size_t copyLen = gapDeviceNameLen_;
        uint8_t adType = 0x09U;  // Complete local name.
        if (copyLen + 2U > sizeof(scanRspPayload)) {
          copyLen = sizeof(scanRspPayload) - 2U;
          adType = 0x08U;  // Shortened local name.
        }
        scanRspPayload[scanRspUsed++] = static_cast<uint8_t>(copyLen + 1U);
        scanRspPayload[scanRspUsed++] = adType;
        memcpy(&scanRspPayload[scanRspUsed], gapDeviceName_, copyLen);
        scanRspUsed += copyLen;
      }
    } else if (scanRspDataLen_ == 0U || scanRspDataAutoDefault_) {
      updateScanResponseName = true;
      size_t copyLen = gapDeviceNameLen_;
      uint8_t adType = 0x09U;  // Complete local name.
      if (copyLen + 2U > sizeof(scanRspPayload)) {
        copyLen = sizeof(scanRspPayload) - 2U;
        adType = 0x08U;  // Shortened local name.
      }
      scanRspPayload[scanRspUsed++] = static_cast<uint8_t>(copyLen + 1U);
      scanRspPayload[scanRspUsed++] = adType;
      memcpy(&scanRspPayload[scanRspUsed], gapDeviceName_, copyLen);
      scanRspUsed += copyLen;
    }
  }

  BleLegacyAdvertisingConfigSnapshot configSnapshot{};
  BleBackgroundAdvertisingRestartState restartState{};
  captureLegacyAdvertisingConfigSnapshot(&configSnapshot);
  captureBackgroundAdvertisingRestartState(&restartState);
  if (restartState.active) {
    stopBackgroundAdvertising();
  }

  memcpy(advData_, payload, used);
  advDataLen_ = used;
  if (updateScanResponseName) {
    memcpy(scanRspData_, scanRspPayload, scanRspUsed);
    scanRspDataLen_ = scanRspUsed;
    scanRspDataAutoDefault_ = true;
  }
  const bool packetsOk =
      buildAdvertisingPacket() &&
      (!updateScanResponseName || buildScanResponsePacket());
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

bool BleRadio::buildExtendedAdvertisingPackets(uint32_t auxOffsetUs,
                                               uint32_t* actualAuxOffsetUs) {
  uint32_t auxOffsetUnits =
      (auxOffsetUs + kBleExtendedAuxOffsetUnit30Us - 1U) /
      kBleExtendedAuxOffsetUnit30Us;
  if (auxOffsetUnits == 0U) {
    auxOffsetUnits = 1U;
  }
  if (auxOffsetUnits > kBleExtendedAuxOffsetMaxUnits) {
    return false;
  }

  const uint32_t encodedAuxOffsetUs =
      auxOffsetUnits * kBleExtendedAuxOffsetUnit30Us;
  if (actualAuxOffsetUs != nullptr) {
    *actualAuxOffsetUs = encodedAuxOffsetUs;
  }

  const uint8_t txAdd =
      (addressType_ == BleAddressType::kRandomStatic) ? (1U << 6U) : 0U;

  constexpr uint8_t kPrimaryExtHdrLen =
      static_cast<uint8_t>(1U + kBleLegacyAddressLength + 2U + 3U);
  constexpr uint8_t kPrimaryPayloadLen =
      static_cast<uint8_t>(1U + kPrimaryExtHdrLen);
  constexpr uint8_t kAuxExtHdrLenNoChain = static_cast<uint8_t>(1U + 2U);
  constexpr uint8_t kAuxExtHdrLenWithChain = static_cast<uint8_t>(1U + 2U + 3U);
  constexpr size_t kSecondaryDataCapacityNoChain = 255U - (1U + kAuxExtHdrLenNoChain);
  constexpr size_t kSecondaryDataCapacityWithChain = 255U - (1U + kAuxExtHdrLenWithChain);

  uint8_t secondaryPacketCount = 1U;
  size_t remainingForCount = extendedAdvDataLen_;
  while (remainingForCount > kSecondaryDataCapacityNoChain) {
    if (secondaryPacketCount >= kBleExtendedSecondaryPacketCountMax) {
      return false;
    }
    remainingForCount -= kSecondaryDataCapacityWithChain;
    ++secondaryPacketCount;
  }
  extendedSecondaryPacketCount_ = secondaryPacketCount;

  size_t offset = 0U;
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(kBlePduAdvExtInd | txAdd);
  extendedPrimaryPacket_[offset++] = kPrimaryPayloadLen;
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      (kBleExtendedAdvModeNonConnNonScan << 6U) | kPrimaryExtHdrLen);
  extendedPrimaryPacket_[offset++] = kBleExtendedPrimaryHeaderFlags;
  memcpy(&extendedPrimaryPacket_[offset], address_, sizeof(address_));
  offset += sizeof(address_);
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(extendedAdvDid_ & 0xFFU);
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      ((extendedAdvDid_ >> 8U) & 0x0FU) | (static_cast<uint16_t>(extendedAdvSid_) << 4U));
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      (extendedAdvAuxChannel_ & 0x3FU) |
      (kBleExtendedClockAccuracy500Ppm << 6U));
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(auxOffsetUnits & 0xFFU);
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      ((auxOffsetUnits >> 8U) & 0x1FU) | (kBleExtendedAuxPhy1M << 5U));

  size_t dataCursor = 0U;
  size_t remaining = extendedAdvDataLen_;
  for (uint8_t packetIndex = 0U; packetIndex < secondaryPacketCount; ++packetIndex) {
    const bool hasFollowingPacket = ((packetIndex + 1U) < secondaryPacketCount);
    const size_t dataLen = hasFollowingPacket ? kSecondaryDataCapacityWithChain : remaining;
    const uint8_t extHdrLen =
        hasFollowingPacket ? kAuxExtHdrLenWithChain : kAuxExtHdrLenNoChain;
    const uint8_t headerFlags =
        hasFollowingPacket ? static_cast<uint8_t>(kBleExtendedAuxHeaderFlags | 0x10U)
                           : kBleExtendedAuxHeaderFlags;
    const size_t payloadLen = 1U + extHdrLen + dataLen;
    uint8_t* packet = &extendedSecondaryPackets_[packetIndex][0];

    offset = 0U;
    packet[offset++] = kBlePduAdvExtInd;
    packet[offset++] = static_cast<uint8_t>(payloadLen);
    packet[offset++] = static_cast<uint8_t>(
        (kBleExtendedAdvModeNonConnNonScan << 6U) | extHdrLen);
    packet[offset++] = headerFlags;
    packet[offset++] = static_cast<uint8_t>(extendedAdvDid_ & 0xFFU);
    packet[offset++] = static_cast<uint8_t>(
        ((extendedAdvDid_ >> 8U) & 0x0FU) | (static_cast<uint16_t>(extendedAdvSid_) << 4U));

    if (hasFollowingPacket) {
      packet[offset++] = static_cast<uint8_t>(
          (extendedAdvAuxChannel_ & 0x3FU) |
          (kBleExtendedClockAccuracy500Ppm << 6U));
      packet[offset++] = static_cast<uint8_t>(auxOffsetUnits & 0xFFU);
      packet[offset++] = static_cast<uint8_t>(
          ((auxOffsetUnits >> 8U) & 0x1FU) | (kBleExtendedAuxPhy1M << 5U));
    }

    if (dataLen > 0U) {
      memcpy(&packet[offset], &extendedAdvData_[dataCursor], dataLen);
    }
    dataCursor += dataLen;
    remaining -= dataLen;
  }

  return true;
}

bool BleRadio::buildExtendedScannableAdvertisingPackets(
    uint32_t auxOffsetUs,
    uint32_t* actualAuxOffsetUs,
    uint32_t* actualScanRspChainOffsetUs) {
  if (extendedScanRspDataLen_ == 0U) {
    return false;
  }

  uint32_t auxOffsetUnits =
      (auxOffsetUs + kBleExtendedAuxOffsetUnit30Us - 1U) /
      kBleExtendedAuxOffsetUnit30Us;
  if (auxOffsetUnits == 0U) {
    auxOffsetUnits = 1U;
  }
  if (auxOffsetUnits > kBleExtendedAuxOffsetMaxUnits) {
    return false;
  }

  const uint32_t encodedAuxOffsetUs =
      auxOffsetUnits * kBleExtendedAuxOffsetUnit30Us;
  if (actualAuxOffsetUs != nullptr) {
    *actualAuxOffsetUs = encodedAuxOffsetUs;
  }
  if (actualScanRspChainOffsetUs != nullptr) {
    *actualScanRspChainOffsetUs = encodedAuxOffsetUs;
  }

  const uint8_t txAdd =
      (addressType_ == BleAddressType::kRandomStatic) ? (1U << 6U) : 0U;

  constexpr uint8_t kScannablePrimaryHeaderFlags = 0x18U;   // ADI + AuxPtr
  constexpr uint8_t kScannableAuxAdvHeaderFlags = 0x09U;    // AdvA + ADI
  constexpr uint8_t kScannableScanRspHeaderFlags = 0x09U;   // AdvA + ADI
  constexpr uint8_t kChainHeaderFlags = kBleExtendedAuxHeaderFlags;
  constexpr uint8_t kPrimaryExtHdrLen = static_cast<uint8_t>(1U + 2U + 3U);
  constexpr uint8_t kPrimaryPayloadLen = static_cast<uint8_t>(1U + kPrimaryExtHdrLen);
  constexpr uint8_t kAuxAdvExtHdrLen = static_cast<uint8_t>(1U + 6U + 2U);
  constexpr uint8_t kAuxAdvPayloadLen = static_cast<uint8_t>(1U + kAuxAdvExtHdrLen);
  constexpr uint8_t kScanRspExtHdrLenNoChain = static_cast<uint8_t>(1U + 6U + 2U);
  constexpr uint8_t kScanRspExtHdrLenWithChain =
      static_cast<uint8_t>(1U + 6U + 2U + 3U);
  constexpr size_t kScanRspDataCapacityNoChain =
      255U - (1U + kScanRspExtHdrLenNoChain);
  constexpr size_t kScanRspDataCapacityWithChain =
      255U - (1U + kScanRspExtHdrLenWithChain);
  constexpr uint8_t kChainExtHdrLenNoChain = static_cast<uint8_t>(1U + 2U);
  constexpr uint8_t kChainExtHdrLenWithChain = static_cast<uint8_t>(1U + 2U + 3U);
  constexpr size_t kChainDataCapacityNoChain =
      255U - (1U + kChainExtHdrLenNoChain);
  constexpr size_t kChainDataCapacityWithChain =
      255U - (1U + kChainExtHdrLenWithChain);

  size_t offset = 0U;
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(kBlePduAdvExtInd | txAdd);
  extendedPrimaryPacket_[offset++] = kPrimaryPayloadLen;
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      (kBleExtendedAdvModeNonConnScan << 6U) | kPrimaryExtHdrLen);
  extendedPrimaryPacket_[offset++] = kScannablePrimaryHeaderFlags;
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(extendedAdvDid_ & 0xFFU);
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      ((extendedAdvDid_ >> 8U) & 0x0FU) | (static_cast<uint16_t>(extendedAdvSid_) << 4U));
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      (extendedAdvAuxChannel_ & 0x3FU) |
      (kBleExtendedClockAccuracy500Ppm << 6U));
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(auxOffsetUnits & 0xFFU);
  extendedPrimaryPacket_[offset++] = static_cast<uint8_t>(
      ((auxOffsetUnits >> 8U) & 0x1FU) | (kBleExtendedAuxPhy1M << 5U));

  offset = 0U;
  extendedAuxAdvPacket_[offset++] = static_cast<uint8_t>(kBlePduAdvExtInd | txAdd);
  extendedAuxAdvPacket_[offset++] = kAuxAdvPayloadLen;
  extendedAuxAdvPacket_[offset++] = static_cast<uint8_t>(
      (kBleExtendedAdvModeNonConnScan << 6U) | kAuxAdvExtHdrLen);
  extendedAuxAdvPacket_[offset++] = kScannableAuxAdvHeaderFlags;
  memcpy(&extendedAuxAdvPacket_[offset], address_, sizeof(address_));
  offset += sizeof(address_);
  extendedAuxAdvPacket_[offset++] = static_cast<uint8_t>(extendedAdvDid_ & 0xFFU);
  extendedAuxAdvPacket_[offset++] = static_cast<uint8_t>(
      ((extendedAdvDid_ >> 8U) & 0x0FU) | (static_cast<uint16_t>(extendedAdvSid_) << 4U));

  extendedScanRspPacketCount_ = 0U;
  size_t remaining = extendedScanRspDataLen_;
  size_t dataCursor = 0U;
  bool firstPacket = true;
  while (remaining > 0U) {
    if (extendedScanRspPacketCount_ >= kBleExtendedSecondaryPacketCountMax) {
      return false;
    }

    const bool useFirstPacket = firstPacket;
    const size_t capacityNoChain =
        useFirstPacket ? kScanRspDataCapacityNoChain : kChainDataCapacityNoChain;
    const size_t capacityWithChain =
        useFirstPacket ? kScanRspDataCapacityWithChain : kChainDataCapacityWithChain;
    const bool hasFollowingPacket = (remaining > capacityNoChain);
    const size_t dataLen = hasFollowingPacket ? capacityWithChain : remaining;
    uint8_t* packet =
        &extendedScanRspPackets_[extendedScanRspPacketCount_][0];

    offset = 0U;
    packet[offset++] =
        useFirstPacket ? static_cast<uint8_t>(kBlePduAdvExtInd | txAdd) : kBlePduAdvExtInd;
    if (useFirstPacket) {
      const uint8_t extHdrLen = hasFollowingPacket ? kScanRspExtHdrLenWithChain
                                                   : kScanRspExtHdrLenNoChain;
      const uint8_t headerFlags = hasFollowingPacket
                                      ? static_cast<uint8_t>(kScannableScanRspHeaderFlags |
                                                             0x10U)
                                      : kScannableScanRspHeaderFlags;
      packet[offset++] = static_cast<uint8_t>(1U + extHdrLen + dataLen);
      packet[offset++] = static_cast<uint8_t>(
          (kBleExtendedAdvModeNonConnNonScan << 6U) | extHdrLen);
      packet[offset++] = headerFlags;
      memcpy(&packet[offset], address_, sizeof(address_));
      offset += sizeof(address_);
      packet[offset++] = static_cast<uint8_t>(extendedAdvDid_ & 0xFFU);
      packet[offset++] = static_cast<uint8_t>(
          ((extendedAdvDid_ >> 8U) & 0x0FU) |
          (static_cast<uint16_t>(extendedAdvSid_) << 4U));
      if (hasFollowingPacket) {
        packet[offset++] = static_cast<uint8_t>(
            (extendedAdvAuxChannel_ & 0x3FU) |
            (kBleExtendedClockAccuracy500Ppm << 6U));
        packet[offset++] = static_cast<uint8_t>(auxOffsetUnits & 0xFFU);
        packet[offset++] = static_cast<uint8_t>(
            ((auxOffsetUnits >> 8U) & 0x1FU) | (kBleExtendedAuxPhy1M << 5U));
      }
    } else {
      const uint8_t extHdrLen =
          hasFollowingPacket ? kChainExtHdrLenWithChain : kChainExtHdrLenNoChain;
      const uint8_t headerFlags = hasFollowingPacket
                                      ? static_cast<uint8_t>(kChainHeaderFlags | 0x10U)
                                      : kChainHeaderFlags;
      packet[offset++] = static_cast<uint8_t>(1U + extHdrLen + dataLen);
      packet[offset++] = static_cast<uint8_t>(
          (kBleExtendedAdvModeNonConnNonScan << 6U) | extHdrLen);
      packet[offset++] = headerFlags;
      packet[offset++] = static_cast<uint8_t>(extendedAdvDid_ & 0xFFU);
      packet[offset++] = static_cast<uint8_t>(
          ((extendedAdvDid_ >> 8U) & 0x0FU) |
          (static_cast<uint16_t>(extendedAdvSid_) << 4U));
      if (hasFollowingPacket) {
        packet[offset++] = static_cast<uint8_t>(
            (extendedAdvAuxChannel_ & 0x3FU) |
            (kBleExtendedClockAccuracy500Ppm << 6U));
        packet[offset++] = static_cast<uint8_t>(auxOffsetUnits & 0xFFU);
        packet[offset++] = static_cast<uint8_t>(
            ((auxOffsetUnits >> 8U) & 0x1FU) | (kBleExtendedAuxPhy1M << 5U));
      }
    }

    memcpy(&packet[offset], &extendedScanRspData_[dataCursor], dataLen);
    dataCursor += dataLen;
    remaining -= dataLen;
    ++extendedScanRspPacketCount_;
    firstPacket = false;
  }

  return (extendedScanRspPacketCount_ > 0U);
}
