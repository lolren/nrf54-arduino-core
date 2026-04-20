bool BleRadio::buildAttErrorResponse(uint8_t requestOpcode, uint16_t handle,
                                     uint8_t errorCode, uint8_t* outAttResponse,
                                     uint16_t* outAttResponseLength) const {
  if (outAttResponse == nullptr || outAttResponseLength == nullptr) {
    return false;
  }

  outAttResponse[0] = kAttOpErrorRsp;
  outAttResponse[1] = requestOpcode;
  writeLe16(&outAttResponse[2], handle);
  outAttResponse[4] = errorCode;
  *outAttResponseLength = 5U;
  return true;
}

void BleRadio::clearPreparedWriteState() {
  connectionPreparedWriteActive_ = false;
  connectionPreparedWriteHandle_ = 0U;
  connectionPreparedWriteValue_[0] = 0U;
  connectionPreparedWriteValue_[1] = 0U;
  connectionPreparedWriteMask_ = 0U;
}

bool BleRadio::applyCccdState(uint16_t handle, uint16_t cccd) {
  if (handle == kHandleGattServiceChangedCccd) {
    if ((cccd & ~0x0002U) != 0U) {
      return false;
    }
    const bool enableIndication = ((cccd & 0x0002U) != 0U);
    if (!enableIndication) {
      connectionServiceChangedIndicationPending_ = false;
      connectionServiceChangedIndicationAwaitingConfirm_ = false;
    }
    connectionServiceChangedIndicationsEnabled_ = enableIndication;
    return true;
  }
  if (handle == kHandleBatteryLevelCccd) {
    if ((cccd & ~0x0001U) != 0U) {
      return false;
    }
    const bool enableNotify = ((cccd & 0x0001U) != 0U);
    connectionBatteryNotificationsEnabled_ = enableNotify;
    connectionBatteryNotificationPending_ = enableNotify;
    return true;
  }

  BleCustomCharacteristicState* custom = findCustomCharacteristicByCccdHandle(handle);
  if (custom == nullptr) {
    return false;
  }

  uint16_t allowedMask = 0U;
  if ((custom->properties & kBleGattPropNotify) != 0U) {
    allowedMask |= 0x0001U;
  }
  if ((custom->properties & kBleGattPropIndicate) != 0U) {
    allowedMask |= 0x0002U;
  }
  if (allowedMask == 0U || ((cccd & ~allowedMask) != 0U)) {
    return false;
  }

  custom->cccdValue = cccd;
  if (((custom->cccdValue & 0x0002U) == 0U) &&
      (connectionCustomIndicationAwaitingHandle_ == custom->valueHandle)) {
    connectionCustomIndicationAwaitingHandle_ = 0U;
  }
  const ptrdiff_t idx = (custom - &customGattCharacteristics_[0]);
  if (idx >= 0 &&
      static_cast<uint8_t>(idx) < customGattCharacteristicCount_) {
    filterCustomGattNotificationQueue(static_cast<uint8_t>(idx),
                                      (custom->cccdValue & 0x0001U) != 0U,
                                      (custom->cccdValue & 0x0002U) != 0U);
  }
  return true;
}

uint8_t BleRadio::readAttributeValue(uint16_t handle, uint16_t offset,
                                     uint8_t* outValue, uint8_t maxLen) const {
  if (outValue == nullptr) {
    return kAttrReadInvalidHandleLen;
  }

  uint8_t fullValue[31] = {0};
  uint8_t fullLen = 0U;

  switch (handle) {
    case kHandleGapService:
      writeLe16(&fullValue[0], kUuidGapService);
      fullLen = 2U;
      break;
    case kHandleGapDeviceNameDecl:
      fullValue[0] = kGattCharacteristicPropRead;
      writeLe16(&fullValue[1], kHandleGapDeviceNameValue);
      writeLe16(&fullValue[3], kUuidDeviceName);
      fullLen = 5U;
      break;
    case kHandleGapDeviceNameValue:
      if (gapDeviceNameLen_ > 0U) {
        memcpy(fullValue, gapDeviceName_, gapDeviceNameLen_);
      }
      fullLen = gapDeviceNameLen_;
      break;
    case kHandleGapAppearanceDecl:
      fullValue[0] = kGattCharacteristicPropRead;
      writeLe16(&fullValue[1], kHandleGapAppearanceValue);
      writeLe16(&fullValue[3], kUuidAppearance);
      fullLen = 5U;
      break;
    case kHandleGapAppearanceValue:
      writeLe16(&fullValue[0], gapAppearance_);
      fullLen = 2U;
      break;
    case kHandleGapPpcpDecl:
      fullValue[0] = kGattCharacteristicPropRead;
      writeLe16(&fullValue[1], kHandleGapPpcpValue);
      writeLe16(&fullValue[3], kUuidPpcp);
      fullLen = 5U;
      break;
    case kHandleGapPpcpValue:
      writeLe16(&fullValue[0], gapPpcpIntervalMin_);
      writeLe16(&fullValue[2], gapPpcpIntervalMax_);
      writeLe16(&fullValue[4], gapPpcpLatency_);
      writeLe16(&fullValue[6], gapPpcpTimeout_);
      fullLen = 8U;
      break;
    case kHandleGattService:
      writeLe16(&fullValue[0], kUuidGattService);
      fullLen = 2U;
      break;
    case kHandleGattServiceChangedDecl:
      fullValue[0] = kGattCharacteristicPropIndicate;
      writeLe16(&fullValue[1], kHandleGattServiceChangedValue);
      writeLe16(&fullValue[3], kUuidServiceChanged);
      fullLen = 5U;
      break;
    case kHandleGattServiceChangedValue:
      writeLe16(&fullValue[0], 0x0001U);
      writeLe16(&fullValue[2], 0xFFFFU);
      fullLen = 4U;
      break;
    case kHandleGattServiceChangedCccd:
      writeLe16(&fullValue[0],
                connectionServiceChangedIndicationsEnabled_ ? 0x0002U : 0x0000U);
      fullLen = 2U;
      break;
    case kHandleBatteryService:
      writeLe16(&fullValue[0], kUuidBatteryService);
      fullLen = 2U;
      break;
    case kHandleBatteryLevelDecl:
      fullValue[0] = static_cast<uint8_t>(kGattCharacteristicPropRead |
                                          kGattCharacteristicPropNotify);
      writeLe16(&fullValue[1], kHandleBatteryLevelValue);
      writeLe16(&fullValue[3], kUuidBatteryLevel);
      fullLen = 5U;
      break;
    case kHandleBatteryLevelValue:
      fullValue[0] = gapBatteryLevel_;
      fullLen = 1U;
      break;
    case kHandleBatteryLevelCccd:
      writeLe16(&fullValue[0], connectionBatteryNotificationsEnabled_ ? 0x0001U : 0x0000U);
      fullLen = 2U;
      break;
    default: {
      const BleCustomServiceState* service = findCustomServiceByHandle(handle);
      if (service != nullptr) {
        if (service->uuidSize == kBleUuid128Size) {
          memcpy(&fullValue[0], service->uuid, kBleUuid128Size);
          fullLen = kBleUuid128Size;
        } else {
          writeLe16(&fullValue[0], bleUuidRead16(service->uuid, service->uuidSize));
          fullLen = kBleUuid16Size;
        }
        break;
      }

      const BleCustomCharacteristicState* characteristic =
          findCustomCharacteristicByValueHandle(handle);
      if (characteristic != nullptr) {
        if ((characteristic->properties & kBleGattPropRead) == 0U) {
          return kAttrReadNotPermittedLen;
        }
        if (characteristic->valueLength > 0U) {
          memcpy(&fullValue[0], characteristic->value, characteristic->valueLength);
        }
        fullLen = characteristic->valueLength;
        break;
      }

      characteristic = findCustomCharacteristicByCccdHandle(handle);
      if (characteristic != nullptr) {
        writeLe16(&fullValue[0], characteristic->cccdValue);
        fullLen = 2U;
        break;
      }

      for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
        const BleCustomCharacteristicState& candidate = customGattCharacteristics_[i];
        if (candidate.declarationHandle != handle) {
          continue;
        }
        writeCharacteristicDeclarationValue(fullValue, candidate.properties,
                                            candidate.valueHandle, candidate.uuid,
                                            candidate.uuidSize);
        fullLen = bleCharacteristicDeclarationValueLen(candidate.uuidSize);
        break;
      }
      if (fullLen == 0U) {
        return kAttrReadInvalidHandleLen;
      }
      break;
    }
  }

  if (offset > fullLen) {
    return kAttrReadInvalidOffsetLen;
  }
  const uint8_t remaining = static_cast<uint8_t>(fullLen - offset);
  const uint8_t toCopy = minU8(remaining, maxLen);
  if (toCopy > 0U) {
    memcpy(outValue, &fullValue[offset], toCopy);
  }
  return toCopy;
}

bool BleRadio::buildAttResponse(const uint8_t* attRequest, uint16_t requestLength,
                                uint8_t* outAttResponse,
                                uint16_t* outAttResponseLength) {
  if (attRequest == nullptr || outAttResponse == nullptr ||
      outAttResponseLength == nullptr || requestLength == 0U) {
    return false;
  }

  const uint16_t maxAttResponseLen =
      minU16(connectionAttMtu_, currentMaxAttPayloadLength());
  if (maxAttResponseLen < 1U) {
    return false;
  }
  const uint8_t maxAttValueLen =
      static_cast<uint8_t>((maxAttResponseLen > 1U) ? (maxAttResponseLen - 1U) : 0U);

  const uint8_t opcode = attRequest[0];
  *outAttResponseLength = 0U;

  switch (opcode) {
    case kAttOpExchangeMtuReq: {
      if (requestLength < 3U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t clientRxMtu = readLe16(&attRequest[1]);
      const uint16_t serverRxMtu = localPreferredAttMtu();
      const uint16_t negotiatedMtu = minU16(clientRxMtu, serverRxMtu);
      connectionAttMtu_ = (negotiatedMtu >= 23U) ? negotiatedMtu : 23U;
      connectionAttMtuExchangePending_ = false;
      connectionAttMtuExchangeInFlight_ = false;
      connectionAttMtuExchangeComplete_ = true;

      outAttResponse[0] = kAttOpExchangeMtuRsp;
      writeLe16(&outAttResponse[1], serverRxMtu);
      *outAttResponseLength = 3U;
      return true;
    }

    case kAttOpFindInfoReq: {
      if (requestLength < 5U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t rawStart = readLe16(&attRequest[1]);
      const uint16_t end = readLe16(&attRequest[3]);
      if (rawStart == 0U || rawStart > end) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidHandle, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t start = rawStart;

      outAttResponse[0] = kAttOpFindInfoRsp;
      outAttResponse[1] = 0U;
      uint16_t used = 2U;
      uint8_t responseFormat = 0U;
      uint16_t recordCount = 0U;
      bool stopFindInfo = false;
      for (size_t i = 0; i < (sizeof(kBleAttributeUuids) / sizeof(kBleAttributeUuids[0]));
           ++i) {
        const BleAttributeUuidRecord& record = kBleAttributeUuids[i];
        if (!inHandleRange(record.handle, start, end)) {
          continue;
        }
        if (responseFormat == kAttFindInfoFormat128Bit) {
          break;
        }
        if ((used + 4U) > maxAttResponseLen) {
          break;
        }
        responseFormat = kAttFindInfoFormat16Bit;
        outAttResponse[1] = responseFormat;
        writeLe16(&outAttResponse[used], record.handle);
        writeLe16(&outAttResponse[used + 2U], record.uuid16);
        used += 4U;
        ++recordCount;
      }
      for (uint8_t i = 0U;
           i < customGattServiceCount_; ++i) {
        const BleCustomServiceState& service = customGattServices_[i];
        if (inHandleRange(service.serviceHandle, start, end)) {
          if (recordCount == 0U) {
            outAttResponse[1] = 0x01U;
          }
          if (outAttResponse[1] != 0x01U || (used + 4U) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], service.serviceHandle);
          writeLe16(&outAttResponse[used + 2U], kUuidPrimaryService);
          used += 4U;
          ++recordCount;
        }
        for (uint8_t j = 0U;
             j < customGattCharacteristicCount_; ++j) {
          const BleCustomCharacteristicState& ch = customGattCharacteristics_[j];
          if (ch.serviceHandle != service.serviceHandle) {
            continue;
          }
          if (inHandleRange(ch.declarationHandle, start, end)) {
            if (recordCount == 0U) {
              outAttResponse[1] = 0x01U;
            }
            if (outAttResponse[1] != 0x01U || (used + 4U) > maxAttResponseLen) {
              break;
            }
            writeLe16(&outAttResponse[used], ch.declarationHandle);
            writeLe16(&outAttResponse[used + 2U], kUuidCharacteristic);
            used += 4U;
            ++recordCount;
          }
          if (inHandleRange(ch.valueHandle, start, end)) {
            if (recordCount == 0U) {
              outAttResponse[1] = (ch.uuidSize == kBleUuid128Size) ? 0x02U : 0x01U;
            }
            if ((outAttResponse[1] == 0x01U && ch.uuidSize != kBleUuid16Size) ||
                (outAttResponse[1] == 0x02U && ch.uuidSize != kBleUuid128Size)) {
              break;
            }
            const uint16_t entrySize =
                (outAttResponse[1] == 0x02U) ? 18U : 4U;
            if ((used + entrySize) > maxAttResponseLen) {
              break;
            }
            writeLe16(&outAttResponse[used], ch.valueHandle);
            if (outAttResponse[1] == 0x02U) {
              writeUuidToAttWire(&outAttResponse[used + 2U], ch.uuid, kBleUuid128Size);
              used += 18U;
            } else {
              writeLe16(&outAttResponse[used + 2U], bleUuidRead16(ch.uuid, ch.uuidSize));
              used += 4U;
            }
            ++recordCount;
          }
          if (ch.cccdHandle != 0U &&
              inHandleRange(ch.cccdHandle, start, end)) {
            if (recordCount == 0U) {
              outAttResponse[1] = 0x01U;
            }
            if (outAttResponse[1] != 0x01U || (used + 4U) > maxAttResponseLen) {
              break;
            }
            writeLe16(&outAttResponse[used], ch.cccdHandle);
            writeLe16(&outAttResponse[used + 2U], kUuidClientCharacteristicConfig);
            used += 4U;
            ++recordCount;
          }
        }
      }

      if (recordCount == 0U) {
        return buildAttErrorResponse(opcode, start, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpFindByTypeValueReq: {
      if (requestLength < 9U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t rawStart = readLe16(&attRequest[1]);
      const uint16_t end = readLe16(&attRequest[3]);
      const uint16_t attrType = readLe16(&attRequest[5]);
      if (rawStart == 0U || rawStart > end) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }

      const uint16_t valueLen = static_cast<uint16_t>(requestLength - 7U);
      if (attrType != kUuidPrimaryService ||
          (valueLen != kBleUuid16Size && valueLen != kBleUuid128Size)) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }

      const uint8_t* requestedServiceUuid = &attRequest[7];
      const uint16_t serviceUuid16 =
          (valueLen == kBleUuid16Size) ? readLe16(requestedServiceUuid) : 0U;
      outAttResponse[0] = kAttOpFindByTypeValueRsp;
      uint16_t used = 1U;
      const uint16_t maxRecords = static_cast<uint16_t>((maxAttResponseLen - 1U) / 4U);
      uint16_t recordCount = 0U;
      if (valueLen == kBleUuid16Size) {
        for (size_t i = 0; i < (sizeof(kBlePrimaryServices) / sizeof(kBlePrimaryServices[0]));
             ++i) {
          const BlePrimaryServiceRecord& service = kBlePrimaryServices[i];
          if (service.uuid16 != serviceUuid16) {
            continue;
          }
          if (!inHandleRange(service.startHandle, rawStart, end)) {
            continue;
          }
          if (recordCount >= maxRecords) {
            break;
          }
          writeLe16(&outAttResponse[used], service.startHandle);
          writeLe16(&outAttResponse[used + 2U], service.endHandle);
          used += 4U;
          ++recordCount;
        }
      }
      for (uint8_t i = 0U;
           (i < customGattServiceCount_) && (recordCount < maxRecords); ++i) {
        const BleCustomServiceState& service = customGattServices_[i];
        if (!bleUuidEqualsAttWire(service.uuid, service.uuidSize, requestedServiceUuid,
                                  static_cast<uint8_t>(valueLen))) {
          continue;
        }
        if (!inHandleRange(service.serviceHandle, rawStart, end)) {
          continue;
        }
        writeLe16(&outAttResponse[used], service.serviceHandle);
        writeLe16(&outAttResponse[used + 2U], service.endHandle);
        used += 4U;
        ++recordCount;
      }

      if (recordCount == 0U) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpReadByGroupTypeReq: {
      if (requestLength < 7U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t rawStart = readLe16(&attRequest[1]);
      const uint16_t end = readLe16(&attRequest[3]);
      if (rawStart == 0U || rawStart > end) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }

      const uint16_t groupTypeLen = static_cast<uint16_t>(requestLength - 5U);
      if (groupTypeLen != 2U) {
        if (groupTypeLen == 16U) {
          return buildAttErrorResponse(opcode, rawStart, kAttErrUnsupportedGroupType,
                                       outAttResponse, outAttResponseLength);
        }
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidPdu,
                                     outAttResponse, outAttResponseLength);
      }

      const uint16_t start = rawStart;
      const uint16_t groupType = readLe16(&attRequest[5]);
      if (groupType != kUuidPrimaryService) {
        return buildAttErrorResponse(opcode, start, kAttErrUnsupportedGroupType,
                                     outAttResponse, outAttResponseLength);
      }

      outAttResponse[0] = kAttOpReadByGroupTypeRsp;
      outAttResponse[1] = 0U;
      uint16_t used = 2U;
      uint8_t entryLen = 0U;
      uint16_t recordCount = 0U;
      for (size_t i = 0; i < (sizeof(kBlePrimaryServices) / sizeof(kBlePrimaryServices[0]));
           ++i) {
        const BlePrimaryServiceRecord& service = kBlePrimaryServices[i];
        if (!inHandleRange(service.startHandle, start, end)) {
          continue;
        }
        if (entryLen == 0U) {
          entryLen = 6U;
          outAttResponse[1] = entryLen;
        } else if (entryLen != 6U) {
          break;
        }
        if ((used + entryLen) > maxAttResponseLen) {
          break;
        }
        writeLe16(&outAttResponse[used], service.startHandle);
        writeLe16(&outAttResponse[used + 2U], service.endHandle);
        writeLe16(&outAttResponse[used + 4U], service.uuid16);
        used += 6U;
        ++recordCount;
      }
      for (uint8_t i = 0U;
           i < customGattServiceCount_; ++i) {
        const BleCustomServiceState& service = customGattServices_[i];
        if (!inHandleRange(service.serviceHandle, start, end)) {
          continue;
        }
        const uint8_t candidateEntryLen =
            (service.uuidSize == kBleUuid128Size) ? 20U : 6U;
        if (recordCount == 0U) {
          outAttResponse[1] = candidateEntryLen;
        }
        if (candidateEntryLen != outAttResponse[1] ||
            (used + candidateEntryLen) > maxAttResponseLen) {
          break;
        }
        writeLe16(&outAttResponse[used], service.serviceHandle);
        writeLe16(&outAttResponse[used + 2U], service.endHandle);
        if (candidateEntryLen == 20U) {
          writeUuidToAttWire(&outAttResponse[used + 4U], service.uuid, kBleUuid128Size);
        } else {
          writeLe16(&outAttResponse[used + 4U],
                    bleUuidRead16(service.uuid, service.uuidSize));
        }
        used += candidateEntryLen;
        ++recordCount;
      }

      if (recordCount == 0U) {
        return buildAttErrorResponse(opcode, start, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpReadByTypeReq: {
      if (requestLength < 7U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t rawStart = readLe16(&attRequest[1]);
      const uint16_t end = readLe16(&attRequest[3]);
      const uint16_t typeLen = static_cast<uint16_t>(requestLength - 5U);
      if (typeLen != kBleUuid16Size && typeLen != kBleUuid128Size) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint8_t* requestedType = &attRequest[5];
      const uint16_t type =
          (typeLen == kBleUuid16Size) ? readLe16(requestedType) : 0U;
      if (rawStart == 0U || rawStart > end) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidHandle, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t start = rawStart;
      const uint8_t typeLength = static_cast<uint8_t>(requestLength - 5U);
      if (typeLength != kBleUuidLength16 && typeLength != kBleUuidLength128) {
        return buildAttErrorResponse(opcode, rawStart, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t type16 =
          (typeLength == kBleUuidLength16) ? readLe16(&attRequest[5]) : 0U;

      outAttResponse[0] = kAttOpReadByTypeRsp;
      uint16_t used = 2U;
      uint8_t entryLen = 0U;
      uint16_t recordCount = 0U;

      if (typeLen == kBleUuid16Size && type == kUuidCharacteristic) {
        entryLen = 7U;
        outAttResponse[1] = entryLen;
        const uint16_t maxRecords =
            static_cast<uint16_t>((maxAttResponseLen - 2U) / entryLen);
        for (size_t i = 0; i < (sizeof(kBleCharacteristics) / sizeof(kBleCharacteristics[0]));
             ++i) {
          const BleCharacteristicRecord& ch = kBleCharacteristics[i];
          if (!inHandleRange(ch.declarationHandle, start, end)) {
            continue;
          }
          const uint8_t candidateEntryLen = 7U;
          if (entryLen == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          } else if (candidateEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], ch.declarationHandle);
          outAttResponse[used + 2U] = ch.properties;
          writeLe16(&outAttResponse[used + 3U], ch.valueHandle);
          writeLe16(&outAttResponse[used + 5U], ch.uuid16);
          used += entryLen;
          ++recordCount;
        }
        for (uint8_t i = 0U;
             i < customGattCharacteristicCount_; ++i) {
          const BleCustomCharacteristicState& ch = customGattCharacteristics_[i];
          if (!inHandleRange(ch.declarationHandle, start, end)) {
            continue;
          }
          const uint8_t candidateEntryLen =
              static_cast<uint8_t>(2U + bleCharacteristicDeclarationValueLen(ch.uuidSize));
          if (candidateEntryLen == 2U) {
            break;
          }
          if (recordCount == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          }
          if (candidateEntryLen != entryLen ||
              (used + candidateEntryLen) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], ch.declarationHandle);
          writeCharacteristicDeclarationValue(&outAttResponse[used + 2U], ch.properties,
                                              ch.valueHandle, ch.uuid, ch.uuidSize);
          used += candidateEntryLen;
          ++recordCount;
        }
      } else if (typeLen == kBleUuid16Size && type == kUuidPrimaryService) {
        entryLen = 4U;
        outAttResponse[1] = entryLen;
        const uint16_t maxRecords =
            static_cast<uint16_t>((maxAttResponseLen - 2U) / entryLen);
        for (size_t i = 0; i < (sizeof(kBlePrimaryServices) / sizeof(kBlePrimaryServices[0]));
             ++i) {
          const BlePrimaryServiceRecord& service = kBlePrimaryServices[i];
          if (!inHandleRange(service.startHandle, start, end)) {
            continue;
          }
          const uint8_t candidateEntryLen = 4U;
          if (entryLen == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          } else if (candidateEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], service.startHandle);
          writeLe16(&outAttResponse[used + 2U], service.uuid16);
          used += entryLen;
          ++recordCount;
        }
        for (uint8_t i = 0U;
             i < customGattServiceCount_; ++i) {
          const BleCustomServiceState& service = customGattServices_[i];
          if (!inHandleRange(service.serviceHandle, start, end)) {
            continue;
          }
          const uint8_t candidateEntryLen =
              (service.uuidSize == kBleUuid128Size) ? 18U : 4U;
          if (recordCount == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          }
          if (candidateEntryLen != entryLen ||
              (used + candidateEntryLen) > maxAttResponseLen) {
            break;
          }
          writeLe16(&outAttResponse[used], service.serviceHandle);
          if (candidateEntryLen == 18U) {
            writeUuidToAttWire(&outAttResponse[used + 2U], service.uuid, kBleUuid128Size);
          } else {
            writeLe16(&outAttResponse[used + 2U],
                      bleUuidRead16(service.uuid, service.uuidSize));
          }
          used += candidateEntryLen;
          ++recordCount;
        }
      } else {
        uint16_t handles[6] = {0U, 0U, 0U, 0U, 0U, 0U};
        uint8_t handleCount = 0U;
        if (typeLength == kBleUuidLength16 && type16 == kUuidDeviceName) {
          handles[handleCount++] = kHandleGapDeviceNameValue;
        } else if (typeLength == kBleUuidLength16 && type16 == kUuidAppearance) {
          handles[handleCount++] = kHandleGapAppearanceValue;
        } else if (typeLength == kBleUuidLength16 && type16 == kUuidPpcp) {
          handles[handleCount++] = kHandleGapPpcpValue;
        } else if (typeLength == kBleUuidLength16 && type16 == kUuidServiceChanged) {
          handles[handleCount++] = kHandleGattServiceChangedValue;
        } else if (typeLength == kBleUuidLength16 &&
                   type16 == kUuidClientCharacteristicConfig) {
          handles[handleCount++] = kHandleGattServiceChangedCccd;
          handles[handleCount++] = kHandleBatteryLevelCccd;
        } else if (typeLength == kBleUuidLength16 && type16 == kUuidBatteryLevel) {
          handles[handleCount++] = kHandleBatteryLevelValue;
        }

        for (uint8_t i = 0U; i < handleCount; ++i) {
          const uint16_t handle = handles[i];
          if (!inHandleRange(handle, start, end)) {
            continue;
          }
          uint8_t temp[31] = {0};
          const uint8_t vlen = readAttributeValue(handle, 0U, temp, maxAttValueLen);
          if (vlen == kAttrReadInvalidHandleLen || vlen == kAttrReadInvalidOffsetLen) {
            continue;
          }

          const uint8_t candidateEntryLen = static_cast<uint8_t>(2U + vlen);
          if (candidateEntryLen == 2U) {
            continue;
          }
          if (entryLen == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          } else if (candidateEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }

          writeLe16(&outAttResponse[used], handle);
          if (vlen > 0U) {
            memcpy(&outAttResponse[used + 2U], temp, vlen);
          }
          used += entryLen;
          ++recordCount;
        }

        for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
          const BleCustomCharacteristicState& ch = customGattCharacteristics_[i];
          uint16_t handle = 0U;
          if (bleUuidEqualsAttWire(ch.uuid, ch.uuidSize, requestedType,
                                   static_cast<uint8_t>(typeLen))) {
            handle = ch.valueHandle;
          } else if (typeLen == kBleUuid16Size &&
                     type == kUuidClientCharacteristicConfig) {
            handle = ch.cccdHandle;
          }
          if (handle == 0U || !inHandleRange(handle, start, end)) {
            continue;
          }

          uint8_t temp[31] = {0};
          const uint8_t vlen = readAttributeValue(handle, 0U, temp, maxAttValueLen);
          if (vlen == kAttrReadInvalidHandleLen ||
              vlen == kAttrReadInvalidOffsetLen ||
              vlen == kAttrReadNotPermittedLen) {
            continue;
          }

          const uint8_t candidateEntryLen = static_cast<uint8_t>(2U + vlen);
          if (candidateEntryLen == 2U) {
            continue;
          }
          if (entryLen == 0U) {
            entryLen = candidateEntryLen;
            outAttResponse[1] = entryLen;
          } else if (candidateEntryLen != entryLen) {
            break;
          }
          if ((used + entryLen) > maxAttResponseLen) {
            break;
          }

          writeLe16(&outAttResponse[used], handle);
          if (vlen > 0U) {
            memcpy(&outAttResponse[used + 2U], temp, vlen);
          }
          used += entryLen;
          ++recordCount;
        }
      }

      if (recordCount == 0U) {
        return buildAttErrorResponse(opcode, start, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpReadReq: {
      if (requestLength < 3U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t handle = readLe16(&attRequest[1]);
      if (handle == 0U) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }
      outAttResponse[0] = kAttOpReadRsp;
      const uint8_t vlen =
          readAttributeValue(handle, 0U, &outAttResponse[1], maxAttValueLen);
      if (vlen == kAttrReadInvalidHandleLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }
      if (vlen == kAttrReadNotPermittedLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrReadNotPermitted,
                                     outAttResponse, outAttResponseLength);
      }
      if (vlen == kAttrReadInvalidOffsetLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidOffset,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = static_cast<uint16_t>(1U + vlen);
      return true;
    }

    case kAttOpReadBlobReq: {
      if (requestLength < 5U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t handle = readLe16(&attRequest[1]);
      const uint16_t offset = readLe16(&attRequest[3]);
      if (handle == 0U) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }

      outAttResponse[0] = kAttOpReadBlobRsp;
      const uint8_t vlen =
          readAttributeValue(handle, offset, &outAttResponse[1], maxAttValueLen);
      if (vlen == kAttrReadInvalidHandleLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                     outAttResponse, outAttResponseLength);
      }
      if (vlen == kAttrReadNotPermittedLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrReadNotPermitted,
                                     outAttResponse, outAttResponseLength);
      }
      if (vlen == kAttrReadInvalidOffsetLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidOffset,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = static_cast<uint16_t>(1U + vlen);
      return true;
    }

    case kAttOpReadMultipleReq: {
      if (requestLength < 5U || ((requestLength & 0x01U) == 0U)) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      outAttResponse[0] = kAttOpReadMultipleRsp;
      uint16_t used = 1U;
      const uint16_t handleCount = static_cast<uint16_t>((requestLength - 1U) / 2U);
      for (uint16_t i = 0U; i < handleCount; ++i) {
        const uint16_t handle = readLe16(&attRequest[1U + (i * 2U)]);
        if (handle == 0U) {
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                       outAttResponse, outAttResponseLength);
        }

        if (used >= maxAttResponseLen) {
          break;
        }

        const uint8_t chunkMax = static_cast<uint8_t>(maxAttResponseLen - used);
        const uint8_t vlen = readAttributeValue(handle, 0U, &outAttResponse[used], chunkMax);
        if (vlen == kAttrReadInvalidHandleLen) {
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidHandle,
                                       outAttResponse, outAttResponseLength);
        }
        if (vlen == kAttrReadNotPermittedLen) {
          return buildAttErrorResponse(opcode, handle, kAttErrReadNotPermitted,
                                       outAttResponse, outAttResponseLength);
        }
        if (vlen == kAttrReadInvalidOffsetLen) {
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidOffset,
                                       outAttResponse, outAttResponseLength);
        }
        used = static_cast<uint16_t>(used + vlen);
      }

      if (used <= 1U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrAttributeNotFound,
                                     outAttResponse, outAttResponseLength);
      }
      *outAttResponseLength = used;
      return true;
    }

    case kAttOpPrepareWriteReq: {
      if (requestLength < 5U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint16_t handle = readLe16(&attRequest[1]);
      if ((handle != kHandleGattServiceChangedCccd) &&
          (handle != kHandleBatteryLevelCccd) &&
          (findCustomCharacteristicByCccdHandle(handle) == nullptr)) {
        return buildAttErrorResponse(opcode, handle, kAttErrWriteNotPermitted,
                                     outAttResponse, outAttResponseLength);
      }

      const uint16_t offset = readLe16(&attRequest[3]);
      const uint16_t valueLen = static_cast<uint16_t>(requestLength - 5U);
      if (offset > 1U) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidOffset,
                                     outAttResponse, outAttResponseLength);
      }
      if ((offset + valueLen) > 2U) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidAttrValueLen,
                                     outAttResponse, outAttResponseLength);
      }

      if (!connectionPreparedWriteActive_) {
        clearPreparedWriteState();
        connectionPreparedWriteActive_ = true;
        connectionPreparedWriteHandle_ = handle;
      } else if (connectionPreparedWriteHandle_ != handle) {
        return buildAttErrorResponse(opcode, handle, kAttErrPrepareQueueFull,
                                     outAttResponse, outAttResponseLength);
      }

      for (uint16_t i = 0U; i < valueLen; ++i) {
        const uint16_t pos = static_cast<uint16_t>(offset + i);
        connectionPreparedWriteValue_[pos] = attRequest[5U + i];
        connectionPreparedWriteMask_ |= static_cast<uint8_t>(1U << pos);
      }

      if (requestLength > maxAttResponseLen) {
        return buildAttErrorResponse(opcode, handle, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      memcpy(outAttResponse, attRequest, requestLength);
      outAttResponse[0] = kAttOpPrepareWriteRsp;
      *outAttResponseLength = requestLength;
      return true;
    }

    case kAttOpExecuteWriteReq: {
      if (requestLength != 2U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      const uint8_t flags = attRequest[1];
      if (flags == 0x00U) {
        clearPreparedWriteState();
        outAttResponse[0] = kAttOpExecuteWriteRsp;
        *outAttResponseLength = 1U;
        return true;
      }
      if (flags != 0x01U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }

      if (connectionPreparedWriteActive_) {
        const uint16_t handle = connectionPreparedWriteHandle_;
        if ((handle != kHandleGattServiceChangedCccd) &&
            (handle != kHandleBatteryLevelCccd) &&
            (findCustomCharacteristicByCccdHandle(handle) == nullptr)) {
          clearPreparedWriteState();
          return buildAttErrorResponse(opcode, handle, kAttErrWriteNotPermitted,
                                       outAttResponse, outAttResponseLength);
        }
        if ((connectionPreparedWriteMask_ & 0x03U) != 0x03U) {
          clearPreparedWriteState();
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidAttrValueLen,
                                       outAttResponse, outAttResponseLength);
        }
        const uint16_t cccd = readLe16(&connectionPreparedWriteValue_[0]);
        if (!applyCccdState(handle, cccd)) {
          clearPreparedWriteState();
          return buildAttErrorResponse(opcode, handle, kAttErrWriteNotPermitted,
                                       outAttResponse, outAttResponseLength);
        }
        clearPreparedWriteState();
      }

      outAttResponse[0] = kAttOpExecuteWriteRsp;
      *outAttResponseLength = 1U;
      return true;
    }

    case kAttOpHandleValueCfm: {
      // Confirmation for ATT Handle Value Indication. This opcode has no response.
      if (requestLength == 1U) {
        connectionServiceChangedIndicationAwaitingConfirm_ = false;
        connectionCustomIndicationAwaitingHandle_ = 0U;
      }
      *outAttResponseLength = 0U;
      return false;
    }

    case kAttOpWriteReq: {
      if (requestLength < 3U) {
        return buildAttErrorResponse(opcode, 0U, kAttErrInvalidPdu, outAttResponse,
                                     outAttResponseLength);
      }
      const uint16_t handle = readLe16(&attRequest[1]);
      const uint16_t valueLen = static_cast<uint16_t>(requestLength - 3U);
      const uint8_t* value = (valueLen > 0U) ? &attRequest[3] : nullptr;

      const bool isCccdHandle =
          (handle == kHandleGattServiceChangedCccd) ||
          (handle == kHandleBatteryLevelCccd) ||
          (findCustomCharacteristicByCccdHandle(handle) != nullptr);
      if (isCccdHandle) {
        if (valueLen != 2U) {
          return buildAttErrorResponse(opcode, handle, kAttErrInvalidAttrValueLen,
                                       outAttResponse, outAttResponseLength);
        }
        const uint16_t cccd = readLe16(&attRequest[3]);
        if (!applyCccdState(handle, cccd)) {
          return buildAttErrorResponse(opcode, handle, kAttErrWriteNotPermitted,
                                       outAttResponse, outAttResponseLength);
        }
        if (connectionPreparedWriteActive_ &&
            connectionPreparedWriteHandle_ == handle) {
          clearPreparedWriteState();
        }
        outAttResponse[0] = kAttOpWriteRsp;
        *outAttResponseLength = 1U;
        return true;
      }
      uint8_t errorCode = kAttErrWriteNotPermitted;
      if (writeCustomGattCharacteristic(handle, value, valueLen, true, &errorCode)) {
        outAttResponse[0] = kAttOpWriteRsp;
        *outAttResponseLength = 1U;
        return true;
      }
      return buildAttErrorResponse(opcode, handle, errorCode, outAttResponse,
                                   outAttResponseLength);
    }

    case kAttOpWriteCmd: {
      if (requestLength >= 3U) {
        const uint16_t handle = readLe16(&attRequest[1]);
        const uint16_t valueLen = static_cast<uint16_t>(requestLength - 3U);
        const uint8_t* value = (valueLen > 0U) ? &attRequest[3] : nullptr;
        const bool isCccdHandle =
            (handle == kHandleGattServiceChangedCccd) ||
            (handle == kHandleBatteryLevelCccd) ||
            (findCustomCharacteristicByCccdHandle(handle) != nullptr);
        if (isCccdHandle && valueLen == 2U) {
          const uint16_t cccd = readLe16(&attRequest[3]);
          if (applyCccdState(handle, cccd)) {
            if (connectionPreparedWriteActive_ &&
                connectionPreparedWriteHandle_ == handle) {
              clearPreparedWriteState();
            }
          }
        } else {
          uint8_t ignoredError = kAttErrWriteNotPermitted;
          writeCustomGattCharacteristic(handle, value, valueLen, false, &ignoredError);
        }
      }
      // Write command has no ATT response.
      *outAttResponseLength = 0U;
      return false;
    }

    case kAttOpErrorRsp:
    case kAttOpExchangeMtuRsp:
    case kAttOpFindInfoRsp:
    case kAttOpFindByTypeValueRsp:
    case kAttOpReadByTypeRsp:
    case kAttOpReadRsp:
    case kAttOpReadBlobRsp:
    case kAttOpReadMultipleRsp:
    case kAttOpReadMultipleVariableRsp:
    case kAttOpReadByGroupTypeRsp:
    case kAttOpWriteRsp:
    case kAttOpPrepareWriteRsp:
    case kAttOpExecuteWriteRsp:
    case kAttOpSignedWriteCmd:
    case kAttOpHandleValueNtf:
    case kAttOpHandleValueInd:
      *outAttResponseLength = 0U;
      return false;

    default:
      return buildAttErrorResponse(opcode, 0U, kAttErrRequestNotSupported,
                                   outAttResponse, outAttResponseLength);
  }
}

bool BleRadio::buildL2capResponse(const uint8_t* l2capPayload,
                                  uint8_t l2capPayloadLength,
                                  uint8_t* outPayload,
                                  uint8_t* outPayloadLength) {
  if (l2capPayload == nullptr || l2capPayloadLength < kBleL2capHeaderLen) {
    return false;
  }

  const uint16_t cid = readLe16(&l2capPayload[2]);
  const bool smpPairingHandshakeActive =
      (smpPairingState_ == kSmpPairingStateRspSent) ||
      (smpPairingState_ == kSmpPairingStateConfirmSent);
  if (cid == kBleL2capCidAtt) {
    // During SMP confirm exchange, prioritize security-channel traffic and keep
    // ATT off the data path to reduce retransmission churn.
    if (smpPairingHandshakeActive) {
      return false;
    }
    return buildL2capAttResponse(l2capPayload, l2capPayloadLength, outPayload,
                                 outPayloadLength);
  }
  if (cid == kBleL2capCidLeSignaling) {
    return buildL2capSignalingResponse(l2capPayload, l2capPayloadLength,
                                       outPayload, outPayloadLength);
  }
  if (cid == kBleL2capCidSmp) {
    return buildL2capSmpResponse(l2capPayload, l2capPayloadLength,
                                 outPayload, outPayloadLength);
  }
  return false;
}

bool BleRadio::buildL2capSignalingResponse(const uint8_t* l2capPayload,
                                           uint8_t l2capPayloadLength,
                                           uint8_t* outPayload,
                                           uint8_t* outPayloadLength) {
  if (l2capPayload == nullptr || outPayload == nullptr || outPayloadLength == nullptr ||
      l2capPayloadLength < kBleL2capHeaderLen) {
    return false;
  }

  const uint16_t declaredL2capLength = readLe16(&l2capPayload[0]);
  const uint16_t cid = readLe16(&l2capPayload[2]);
  if (cid != kBleL2capCidLeSignaling) {
    return false;
  }

  const uint16_t available = static_cast<uint16_t>(l2capPayloadLength - kBleL2capHeaderLen);
  const uint16_t sigLength = minU16(declaredL2capLength, available);
  if (sigLength < 4U) {
    return false;
  }

  const uint8_t code = l2capPayload[kBleL2capHeaderLen + 0U];
  const uint8_t identifier = l2capPayload[kBleL2capHeaderLen + 1U];
  const uint16_t commandLength = readLe16(&l2capPayload[kBleL2capHeaderLen + 2U]);

  uint8_t outSigLen = 0U;
  auto writeCommandReject = [&](uint16_t reason) {
    outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeCommandRejectRsp;
    outPayload[kBleL2capHeaderLen + 1U] = identifier;
    writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 2U);
    writeLe16(&outPayload[kBleL2capHeaderLen + 4U], reason);
    outSigLen = 6U;
  };
  auto writeCommandRejectMtu = [&]() {
    outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeCommandRejectRsp;
    outPayload[kBleL2capHeaderLen + 1U] = identifier;
    writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 4U);
    writeLe16(&outPayload[kBleL2capHeaderLen + 4U],
              kL2capCmdRejectReasonSignalingMtuExceeded);
    writeLe16(&outPayload[kBleL2capHeaderLen + 6U], kBleL2capLeSignalingMtu);
    outSigLen = 8U;
  };
  auto writeCommandRejectInvalidCid = [&](uint16_t localCid, uint16_t remoteCid) {
    outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeCommandRejectRsp;
    outPayload[kBleL2capHeaderLen + 1U] = identifier;
    writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 6U);
    writeLe16(&outPayload[kBleL2capHeaderLen + 4U], kL2capCmdRejectReasonInvalidCid);
    writeLe16(&outPayload[kBleL2capHeaderLen + 6U], localCid);
    writeLe16(&outPayload[kBleL2capHeaderLen + 8U], remoteCid);
    outSigLen = 10U;
  };

  if ((4U + commandLength) > sigLength) {
    // Truncated signaling command payload; reject with signaling MTU hint.
    writeCommandRejectMtu();
  } else if (code == kL2capSigCodeCommandRejectRsp ||
             code == kL2capSigCodeConnParamUpdateRsp ||
             code == kL2capSigCodeLeCreditConnRsp) {
    // Response opcodes do not require another response.
    return false;
  } else if (code == kL2capSigCodeConnParamUpdateReq) {
    if (commandLength != 8U) {
      writeCommandReject(kL2capCmdRejectReasonCmdNotUnderstood);
    } else {
      const uint8_t* req = &l2capPayload[kBleL2capHeaderLen + 4U];
      const uint16_t intervalMin = readLe16(&req[0]);
      const uint16_t intervalMax = readLe16(&req[2]);
      const uint16_t latency = readLe16(&req[4]);
      const uint16_t timeout = readLe16(&req[6]);
      outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeConnParamUpdateRsp;
      outPayload[kBleL2capHeaderLen + 1U] = identifier;
      writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 2U);
      if ((connectionRole_ == BleConnectionRole::kCentral) &&
          !connectionCentralConnParamIndPending_ &&
          !connectionUpdatePending_ &&
          connParamsAreValid(intervalMin, intervalMax, latency, timeout)) {
        connectionCentralConnParamIntervalUnits_ =
            chooseAcceptedConnIntervalUnits(intervalMin, intervalMax);
        connectionCentralConnParamLatency_ = latency;
        connectionCentralConnParamTimeoutUnits_ = timeout;
        connectionCentralConnParamIndPending_ = true;
        writeLe16(&outPayload[kBleL2capHeaderLen + 4U],
                  kL2capConnParamResultAccepted);
      } else {
        writeLe16(&outPayload[kBleL2capHeaderLen + 4U],
                  kL2capConnParamResultRejected);
      }
      outSigLen = 6U;
    }
  } else if (code == kL2capSigCodeLeCreditConnReq) {
    if (commandLength != 10U) {
      writeCommandReject(kL2capCmdRejectReasonCmdNotUnderstood);
    } else {
      // LE Credit Based Connection Request:
      //   [PSM(2), SCID(2), MTU(2), MPS(2), InitialCredits(2)]
      // Peripheral path currently does not expose dynamic LE CoC channels.
      // Reply deterministically with "PSM not supported".
      outPayload[kBleL2capHeaderLen + 0U] = kL2capSigCodeLeCreditConnRsp;
      outPayload[kBleL2capHeaderLen + 1U] = identifier;
      writeLe16(&outPayload[kBleL2capHeaderLen + 2U], 10U);
      writeLe16(&outPayload[kBleL2capHeaderLen + 4U], 0x0000U);  // DCID
      writeLe16(&outPayload[kBleL2capHeaderLen + 6U], kBleL2capLeSignalingMtu);
      writeLe16(&outPayload[kBleL2capHeaderLen + 8U], kBleL2capLeSignalingMtu);
      writeLe16(&outPayload[kBleL2capHeaderLen + 10U], 0x0000U);  // Credits
      writeLe16(&outPayload[kBleL2capHeaderLen + 12U],
                kL2capLeCreditConnResultPsmNotSupported);
      outSigLen = 14U;
    }
  } else if (code == kL2capSigCodeLeFlowControlCredit) {
    if (commandLength != 4U) {
      writeCommandReject(kL2capCmdRejectReasonCmdNotUnderstood);
    } else {
      // No LE credit channels are established in this clean peripheral path.
      // Reject with Invalid CID and include received CID for diagnostics.
      const uint16_t cidFromPeer = readLe16(&l2capPayload[kBleL2capHeaderLen + 4U]);
      writeCommandRejectInvalidCid(0x0000U, cidFromPeer);
    }
  } else {
    writeCommandReject(kL2capCmdRejectReasonCmdNotUnderstood);
  }

  if ((kBleL2capHeaderLen + outSigLen) > kBleDataPduMaxPayload) {
    return false;
  }

  writeLe16(&outPayload[0], outSigLen);
  writeLe16(&outPayload[2], kBleL2capCidLeSignaling);
  *outPayloadLength = static_cast<uint8_t>(kBleL2capHeaderLen + outSigLen);
  return true;
}

bool BleRadio::buildL2capSmpResponse(const uint8_t* l2capPayload,
                                     uint8_t l2capPayloadLength,
                                     uint8_t* outPayload,
                                     uint8_t* outPayloadLength) {
  if (l2capPayload == nullptr || outPayload == nullptr || outPayloadLength == nullptr ||
      l2capPayloadLength < kBleL2capHeaderLen) {
    return false;
  }

  const uint16_t declaredL2capLength = readLe16(&l2capPayload[0]);
  const uint16_t cid = readLe16(&l2capPayload[2]);
  if (cid != kBleL2capCidSmp) {
    return false;
  }

  const uint16_t available = static_cast<uint16_t>(l2capPayloadLength - kBleL2capHeaderLen);
  const uint16_t smpLength = minU16(declaredL2capLength, available);
  if (smpLength < 1U) {
    return false;
  }

  const uint8_t* smp = &l2capPayload[kBleL2capHeaderLen];
  const uint8_t smpCode = smp[0];

  auto buildSmpResponse = [&](const uint8_t* smpData, uint8_t smpDataLen) -> bool {
    if (smpData == nullptr || smpDataLen == 0U) {
      return false;
    }
    if ((kBleL2capHeaderLen + smpDataLen) > kBleDataPduMaxPayload) {
      return false;
    }
    writeLe16(&outPayload[0], smpDataLen);
    writeLe16(&outPayload[2], kBleL2capCidSmp);
    memcpy(&outPayload[kBleL2capHeaderLen], smpData, smpDataLen);
    *outPayloadLength = static_cast<uint8_t>(kBleL2capHeaderLen + smpDataLen);
    return true;
  };
  auto buildSmpPairingFailed = [&](uint8_t reason) -> bool {
    clearConnectionSecurityState();
    uint8_t failed[2] = {kSmpCodePairingFailed, reason};
    emitBleTrace("SMP_PAIRING_FAILED");
    return buildSmpResponse(failed, sizeof(failed));
  };

  if (smpCode == kSmpCodePairingFailed) {
    clearConnectionSecurityState();
    emitBleTrace("SMP_PAIRING_FAILED_RX");
    return false;
  }

  switch (smpCode) {
    case kSmpCodePairingRequest: {
      if (smpLength != kSmpPairingRequestLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }

      const uint8_t oobFlag = smp[2];
      const uint8_t maxKeySize = smp[4];
      if (oobFlag != 0x00U) {
        return buildSmpPairingFailed(kSmpReasonOobNotAvailable);
      }
      if (maxKeySize < 7U || maxKeySize > 16U) {
        return buildSmpPairingFailed(kSmpReasonEncryptionKeySize);
      }

      const bool replacingPrimedBond = bondKeyPrimedForConnection_;
      clearConnectionSecurityState();
      if (replacingPrimedBond && bondRecordValid_) {
        // Peer explicitly started a new pairing procedure; drop the prior
        // bonded key so this connection re-establishes security state cleanly.
        clearBondRecord(true);
        emitBleTrace("BOND_REPLACED");
      }
      memcpy(smpPairingReq_, smp, kSmpPairingRequestLen);
      smpPairingRsp_[0] = kSmpCodePairingResponse;
      smpPairingRsp_[1] = kSmpIoCapNoInputNoOutput;
      smpPairingRsp_[2] = 0x00U;
      const uint8_t peerBonding = smp[3] & kSmpAuthReqBondingMask;
      smpPairingRsp_[3] = (peerBonding != 0U) ? 0x01U : 0x00U;
      smpBondingRequested_ = (peerBonding != 0U);
      smpPairingRsp_[4] = maxKeySize;
      smpPairingRsp_[5] = smpBondingRequested_
                              ? static_cast<uint8_t>(
                                    smp[5] &
                                    static_cast<uint8_t>(kSmpKeyDistEncKeyMask |
                                                         kSmpKeyDistIdKeyMask))
                              : 0x00U;
      smpPairingRsp_[6] = 0x00U;
      smpExpectInitiatorEncKey_ = ((smpPairingRsp_[5] & kSmpKeyDistEncKeyMask) != 0U);
      smpExpectInitiatorIdKey_ = ((smpPairingRsp_[5] & kSmpKeyDistIdKeyMask) != 0U);
      smpKeySize_ = maxKeySize;

      // Precompute local confirm material here so confirm/random stages avoid
      // repeated AES work while servicing tight connection events.
      const uint32_t seed = micros() ^ connectionAccessAddress_ ^
                            static_cast<uint32_t>(connectionEventCounter_ << 16U);
      if (smpPrefetchedLocalRandomValid_) {
        memcpy(smpLocalRandom_, smpPrefetchedLocalRandom_, sizeof(smpLocalRandom_));
        smpPrefetchedLocalRandomValid_ = false;
        ++encDebug_.encRandomPrefetchUseCount;
      } else if (fillBleSecurityRandomBytes(smpLocalRandom_,
                                            sizeof(smpLocalRandom_), seed,
                                            kBleSecurityRandomSpinLimit)) {
        ++encDebug_.encRandomHardwareCount;
      } else {
        ++encDebug_.encRandomFallbackCount;
      }
      const uint8_t tk[16] = {0};
      const uint8_t iat = connectionPeerAddressRandom_ ? 0x01U : 0x00U;
      const uint8_t rat =
          (addressType_ == BleAddressType::kRandomStatic) ? 0x01U : 0x00U;
      if (!smpC1(tk, smpLocalRandom_, smpPairingReq_, smpPairingRsp_, iat,
                 connectionPeerAddress_, rat, address_, smpLocalConfirm_)) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }

      smpPairingState_ = kSmpPairingStateRspSent;
      emitBleTrace("SMP_PAIRING_REQUEST_RX");
      return buildSmpResponse(smpPairingRsp_, kSmpPairingResponseLen);
    }

    case kSmpCodePairingConfirm: {
      if (smpLength != kSmpPairingConfirmLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smpPairingState_ != kSmpPairingStateRspSent) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }

      memcpy(smpPeerConfirm_, &smp[1], sizeof(smpPeerConfirm_));
      uint8_t localConfirm[17] = {0};
      localConfirm[0] = kSmpCodePairingConfirm;
      memcpy(&localConfirm[1], smpLocalConfirm_, sizeof(smpLocalConfirm_));
      smpPairingState_ = kSmpPairingStateConfirmSent;
      emitBleTrace("SMP_CONFIRM_RX");
      return buildSmpResponse(localConfirm, sizeof(localConfirm));
    }

    case kSmpCodePairingRandom: {
      if (smpLength != kSmpPairingRandomLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (smpPairingState_ != kSmpPairingStateConfirmSent) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }

      memcpy(smpPeerRandom_, &smp[1], sizeof(smpPeerRandom_));
      const uint8_t tk[16] = {0};
      const uint8_t iat = connectionPeerAddressRandom_ ? 0x01U : 0x00U;
      const uint8_t rat =
          (addressType_ == BleAddressType::kRandomStatic) ? 0x01U : 0x00U;
      uint8_t expectedRemoteConfirm[16] = {0};
      if (!smpC1(tk, smpPeerRandom_, smpPairingReq_, smpPairingRsp_, iat,
                 connectionPeerAddress_, rat, address_, expectedRemoteConfirm)) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }

      if (memcmp(expectedRemoteConfirm, smpPeerConfirm_, sizeof(smpPeerConfirm_)) != 0) {
        return buildSmpPairingFailed(kSmpReasonConfirmValueFailed);
      }

      // STK = s1(TK, Srand, Mrand) per BLE spec Vol 3 Part H §2.2.4.
      // s1 r1=Srand (responder = local), r2=Mrand (initiator = peer).
      if (!smpS1(tk, smpLocalRandom_, smpPeerRandom_, smpStk_)) {
        return buildSmpPairingFailed(kSmpReasonUnspecified);
      }
      smpStkValid_ = true;
      memcpy(encDebug_.encLastStk, smpStk_, sizeof(encDebug_.encLastStk));

      uint8_t localRandom[17] = {0};
      localRandom[0] = kSmpCodePairingRandom;
      memcpy(&localRandom[1], smpLocalRandom_, sizeof(smpLocalRandom_));
      smpPairingState_ = kSmpPairingStateRandomSent;
      emitBleTrace("SMP_RANDOM_RX");
      return buildSmpResponse(localRandom, sizeof(localRandom));
    }

    case kSmpCodeEncryptionInformation:
      if (smpLength != kSmpEncryptionInformationLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpBondingRequested_ || !smpExpectInitiatorEncKey_ ||
          !connectionEncSessionValid_ || !connectionEncRxEnabled_) {
        return false;
      }
      memcpy(smpPeerLtk_, &smp[1], sizeof(smpPeerLtk_));
      smpPeerLtkValid_ = true;
      smpPeerLtkAwaitMasterId_ = true;
      emitBleTrace("SMP_ENC_INFO_RX");
      return false;

    case kSmpCodeMasterIdentification:
      if (smpLength != kSmpMasterIdentificationLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpBondingRequested_ || !smpPeerLtkValid_ || !smpPeerLtkAwaitMasterId_) {
        return false;
      }
      smpEncReqEdiv_ = readLe16(&smp[1]);
      memcpy(smpEncReqRand_, &smp[3], sizeof(smpEncReqRand_));
      smpPeerLtkAwaitMasterId_ = false;
      {
        BleBondRecord record{};
        if (buildCurrentBondRecord(&record)) {
          persistBondRecord(record);
        }
      }
      emitBleTrace("SMP_MASTER_ID_RX");
      return false;

    case kSmpCodeIdentityInformation:
      if (smpLength != kSmpIdentityInformationLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpBondingRequested_ || !smpExpectInitiatorIdKey_ ||
          !connectionEncSessionValid_ || !connectionEncRxEnabled_) {
        return false;
      }
      memcpy(smpPeerIrk_, &smp[1], sizeof(smpPeerIrk_));
      smpPeerIrkValid_ = true;
      {
        BleBondRecord record{};
        if (buildCurrentBondRecord(&record)) {
          persistBondRecord(record);
        }
      }
      emitBleTrace("SMP_ID_INFO_RX");
      return false;

    case kSmpCodeIdentityAddressInformation:
      if (smpLength != kSmpIdentityAddressInformationLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      if (!smpBondingRequested_ || !smpExpectInitiatorIdKey_ ||
          !connectionEncSessionValid_ || !connectionEncRxEnabled_) {
        return false;
      }
      if (smp[1] > 0x01U) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      memcpy(smpPeerIdentityAddress_, &smp[2], sizeof(smpPeerIdentityAddress_));
      smpPeerIdentityAddressRandom_ = (smp[1] != 0U);
      smpPeerIdentityAddressValid_ = true;
      {
        BleBondRecord record{};
        if (buildCurrentBondRecord(&record)) {
          persistBondRecord(record);
        }
      }
      emitBleTrace("SMP_ID_ADDR_RX");
      return false;

    case kSmpCodePairingResponse:
      if (smpLength != kSmpPairingResponseLen) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      return buildSmpPairingFailed(kSmpReasonCommandNotSupported);

    case kSmpCodeSecurityRequest:
      if (smpLength != 2U) {
        return buildSmpPairingFailed(kSmpReasonInvalidParameters);
      }
      return buildSmpPairingFailed(kSmpReasonCommandNotSupported);

    default:
      return buildSmpPairingFailed(kSmpReasonPairingNotSupported);
  }
}

bool BleRadio::buildL2capAttResponse(const uint8_t* l2capPayload,
                                     uint8_t l2capPayloadLength,
                                     uint8_t* outPayload,
                                     uint8_t* outPayloadLength) {
  if (l2capPayload == nullptr || outPayload == nullptr || outPayloadLength == nullptr ||
      l2capPayloadLength < kBleL2capHeaderLen) {
    return false;
  }

  const uint16_t declaredL2capLength = readLe16(&l2capPayload[0]);
  const uint16_t cid = readLe16(&l2capPayload[2]);
  if (cid != kBleL2capCidAtt) {
    return false;
  }

  const uint16_t available = static_cast<uint16_t>(l2capPayloadLength - kBleL2capHeaderLen);
  const uint16_t attRequestLength = minU16(declaredL2capLength, available);
  if (attRequestLength == 0U) {
    return false;
  }

  g_ble_periph_last_att_req_opcode = l2capPayload[kBleL2capHeaderLen];
  ++g_ble_periph_att_req_count;

  uint16_t attResponseLength = 0U;
  if (!buildAttResponse(&l2capPayload[kBleL2capHeaderLen], attRequestLength,
                        &outPayload[kBleL2capHeaderLen], &attResponseLength) ||
      attResponseLength == 0U) {
    return false;
  }

  if (attResponseLength > currentMaxAttPayloadLength()) {
    return false;
  }

  writeLe16(&outPayload[0], attResponseLength);
  writeLe16(&outPayload[2], kBleL2capCidAtt);
  g_ble_periph_last_att_rsp_opcode = outPayload[kBleL2capHeaderLen];
  ++g_ble_periph_att_rsp_count;
  *outPayloadLength =
      static_cast<uint8_t>(kBleL2capHeaderLen + static_cast<uint8_t>(attResponseLength));
  return true;
}
