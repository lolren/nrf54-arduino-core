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
      memset(&outPayload[2], 0, 7U);
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
                                         readLe16(&payload[5]));
      connectionDataLengthUpdatePending_ = false;
      connectionDataLengthUpdateInFlight_ = false;
      connectionDataLengthUpdateComplete_ = true;
      // LL_LENGTH_RSP: MaxRxOctets, MaxRxTime, MaxTxOctets, MaxTxTime.
      outPayload[0] = kBleLlCtrlLengthRsp;
      writeLe16(&outPayload[1], kBleDataPduMaxPayload);
      writeLe16(&outPayload[3], kBleDataLengthMaxTimeUs);
      writeLe16(&outPayload[5], kBleDataPduMaxPayload);
      writeLe16(&outPayload[7], kBleDataLengthMaxTimeUs);
      *outLength = 9U;
      return true;

    case kBleLlCtrlPhyReq:
      if (length != 3U) {
        return rejectMalformedRequest();
      }
      outPayload[0] = kBleLlCtrlPhyRsp;
      outPayload[1] = 0x01U;  // LE 1M TX.
      outPayload[2] = 0x01U;  // LE 1M RX.
      *outLength = 3U;
      return true;

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
                                         readLe16(&payload[5]));
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
        return false;
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
      return false;

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
