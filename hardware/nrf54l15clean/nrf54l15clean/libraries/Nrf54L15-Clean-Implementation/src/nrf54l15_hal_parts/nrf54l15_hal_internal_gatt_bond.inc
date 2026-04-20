
struct BleBondRecordV1 {
  uint8_t peerAddress[6];
  uint8_t peerAddressRandom;
  uint8_t localAddress[6];
  uint8_t localAddressRandom;
  uint8_t ltk[16];
  uint8_t rand[8];
  uint16_t ediv;
  uint8_t keySize;
  uint8_t reserved[3];
};

struct BleBondRetentionBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t recordLength;
  uint32_t crc32;
  xiao_nrf54l15::BleBondRecord record;
};

__attribute__((section(".noinit"))) BleBondRetentionBlob g_bleBondRetention;
__attribute__((section(".bond_storage"), aligned(4)))
volatile BleBondRetentionBlob g_bleBondFlashStorage;

constexpr uint8_t kAttOpErrorRsp = 0x01U;
constexpr uint8_t kAttOpExchangeMtuReq = 0x02U;
constexpr uint8_t kAttOpExchangeMtuRsp = 0x03U;
constexpr uint8_t kAttOpFindInfoReq = 0x04U;
constexpr uint8_t kAttOpFindInfoRsp = 0x05U;
constexpr uint8_t kAttOpFindByTypeValueReq = 0x06U;
constexpr uint8_t kAttOpFindByTypeValueRsp = 0x07U;
constexpr uint8_t kAttOpReadByTypeReq = 0x08U;
constexpr uint8_t kAttOpReadByTypeRsp = 0x09U;
constexpr uint8_t kAttOpReadReq = 0x0AU;
constexpr uint8_t kAttOpReadRsp = 0x0BU;
constexpr uint8_t kAttOpReadBlobReq = 0x0CU;
constexpr uint8_t kAttOpReadBlobRsp = 0x0DU;
constexpr uint8_t kAttOpReadMultipleReq = 0x0EU;
constexpr uint8_t kAttOpReadMultipleRsp = 0x0FU;
constexpr uint8_t kAttOpReadMultipleVariableReq = 0x20U;
constexpr uint8_t kAttOpReadMultipleVariableRsp = 0x21U;
constexpr uint8_t kAttOpReadByGroupTypeReq = 0x10U;
constexpr uint8_t kAttOpReadByGroupTypeRsp = 0x11U;
constexpr uint8_t kAttOpWriteReq = 0x12U;
constexpr uint8_t kAttOpWriteRsp = 0x13U;
constexpr uint8_t kAttOpSignedWriteCmd = 0xD2U;
constexpr uint8_t kAttOpPrepareWriteReq = 0x16U;
constexpr uint8_t kAttOpPrepareWriteRsp = 0x17U;
constexpr uint8_t kAttOpExecuteWriteReq = 0x18U;
constexpr uint8_t kAttOpExecuteWriteRsp = 0x19U;
constexpr uint8_t kAttOpHandleValueNtf = 0x1BU;
constexpr uint8_t kAttOpHandleValueInd = 0x1DU;
constexpr uint8_t kAttOpHandleValueCfm = 0x1EU;
constexpr uint8_t kAttOpWriteCmd = 0x52U;

static inline bool attOpcodeIsClientProgressPdu(uint8_t opcode) {
  switch (opcode) {
    case kAttOpExchangeMtuReq:
    case kAttOpFindInfoReq:
    case kAttOpFindByTypeValueReq:
    case kAttOpReadByTypeReq:
    case kAttOpReadReq:
    case kAttOpReadBlobReq:
    case kAttOpReadMultipleReq:
    case kAttOpReadMultipleVariableReq:
    case kAttOpReadByGroupTypeReq:
    case kAttOpWriteReq:
    case kAttOpPrepareWriteReq:
    case kAttOpExecuteWriteReq:
    case kAttOpWriteCmd:
    case kAttOpSignedWriteCmd:
    case kAttOpHandleValueCfm:
      return true;
    default:
      return false;
  }
}

static inline bool attOpcodeIsServerResponsePdu(uint8_t opcode) {
  switch (opcode) {
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
      return true;
    default:
      return false;
  }
}

constexpr uint8_t kAttErrInvalidHandle = 0x01U;
constexpr uint8_t kAttErrReadNotPermitted = 0x02U;
constexpr uint8_t kAttErrWriteNotPermitted = 0x03U;
constexpr uint8_t kAttErrInvalidPdu = 0x04U;
constexpr uint8_t kAttErrRequestNotSupported = 0x06U;
constexpr uint8_t kAttErrInvalidOffset = 0x07U;
constexpr uint8_t kAttErrPrepareQueueFull = 0x09U;
constexpr uint8_t kAttErrInvalidAttrValueLen = 0x0DU;
constexpr uint8_t kAttErrUnsupportedGroupType = 0x10U;
constexpr uint8_t kAttErrAttributeNotFound = 0x0AU;

constexpr uint16_t kUuidPrimaryService = 0x2800U;
constexpr uint16_t kUuidCharacteristic = 0x2803U;
constexpr uint16_t kUuidClientCharacteristicConfig = 0x2902U;
constexpr uint16_t kUuidGapService = 0x1800U;
constexpr uint16_t kUuidGattService = 0x1801U;
constexpr uint16_t kUuidBatteryService = 0x180FU;
constexpr uint16_t kUuidDeviceName = 0x2A00U;
constexpr uint16_t kUuidAppearance = 0x2A01U;
constexpr uint16_t kUuidPpcp = 0x2A04U;
constexpr uint16_t kUuidServiceChanged = 0x2A05U;
constexpr uint16_t kUuidBatteryLevel = 0x2A19U;

constexpr uint16_t kHandleGapService = 0x0001U;
constexpr uint16_t kHandleGapDeviceNameDecl = 0x0002U;
constexpr uint16_t kHandleGapDeviceNameValue = 0x0003U;
constexpr uint16_t kHandleGapAppearanceDecl = 0x0004U;
constexpr uint16_t kHandleGapAppearanceValue = 0x0005U;
constexpr uint16_t kHandleGapPpcpDecl = 0x0006U;
constexpr uint16_t kHandleGapPpcpValue = 0x0007U;
constexpr uint16_t kHandleGattService = 0x0008U;
constexpr uint16_t kHandleGattServiceChangedDecl = 0x0009U;
constexpr uint16_t kHandleGattServiceChangedValue = 0x000AU;
constexpr uint16_t kHandleGattServiceChangedCccd = 0x000BU;
constexpr uint16_t kHandleBatteryService = 0x0010U;
constexpr uint16_t kHandleBatteryLevelDecl = 0x0011U;
constexpr uint16_t kHandleBatteryLevelValue = 0x0012U;
constexpr uint16_t kHandleBatteryLevelCccd = 0x0013U;

constexpr uint8_t kGattCharacteristicPropRead = 0x02U;
constexpr uint8_t kGattCharacteristicPropNotify = 0x10U;
constexpr uint8_t kGattCharacteristicPropIndicate = 0x20U;
constexpr uint8_t kBleUuid16Size = 2U;
constexpr uint8_t kBleUuid128Size = 16U;
constexpr uint8_t kAttrReadInvalidHandleLen = 0xFFU;
constexpr uint8_t kAttrReadInvalidOffsetLen = 0xFEU;
constexpr uint8_t kAttrReadNotPermittedLen = 0xFDU;
constexpr uint8_t kAttFindInfoFormat16Bit = 0x01U;
constexpr uint8_t kAttFindInfoFormat128Bit = 0x02U;
constexpr uint8_t kBleUuidLength16 = 2U;
constexpr uint8_t kBleUuidLength128 = 16U;

uint16_t readLe16(const uint8_t* p);
void writeLe16(uint8_t* p, uint16_t v);

struct BlePrimaryServiceRecord {
  uint16_t startHandle;
  uint16_t endHandle;
  uint16_t uuid16;
};

struct BleCharacteristicRecord {
  uint16_t declarationHandle;
  uint8_t properties;
  uint16_t valueHandle;
  uint16_t uuid16;
};

struct BleAttributeUuidRecord {
  uint16_t handle;
  uint16_t uuid16;
};

uint8_t customUuidLength(uint8_t uuidLength) {
  if (uuidLength == kBleUuidLength16 || uuidLength == kBleUuidLength128) {
    return uuidLength;
  }
  return 0U;
}

void setCustomUuid16(uint8_t* uuidLength, uint8_t* uuidBytes, uint16_t uuid16) {
  if (uuidLength == nullptr || uuidBytes == nullptr) {
    return;
  }

  memset(uuidBytes, 0, kBleUuidLength128);
  *uuidLength = kBleUuidLength16;
  writeLe16(uuidBytes, uuid16);
}

bool setCustomUuid128(uint8_t* uuidLength, uint8_t* uuidBytes,
                      const uint8_t* uuid128Canonical) {
  if (uuidLength == nullptr || uuidBytes == nullptr || uuid128Canonical == nullptr) {
    return false;
  }

  memset(uuidBytes, 0, kBleUuidLength128);
  *uuidLength = kBleUuidLength128;
  // Store 128-bit UUIDs in ATT/BLE little-endian order so wire encoding and
  // incoming ATT request matching do not need extra byte swapping later.
  for (uint8_t i = 0U; i < kBleUuidLength128; ++i) {
    uuidBytes[i] = uuid128Canonical[kBleUuidLength128 - 1U - i];
  }
  return true;
}

bool customUuidMatchesRaw(const uint8_t* uuidBytes,
                          uint8_t uuidLength,
                          const uint8_t* rawUuid,
                          uint8_t rawUuidLength) {
  uuidLength = customUuidLength(uuidLength);
  if (uuidLength == 0U || rawUuid == nullptr || rawUuidLength != uuidLength) {
    return false;
  }
  return memcmp(uuidBytes, rawUuid, uuidLength) == 0;
}

uint8_t customCharacteristicDeclarationValueLength(uint8_t uuidLength) {
  uuidLength = customUuidLength(uuidLength);
  return uuidLength == 0U ? 0U : static_cast<uint8_t>(3U + uuidLength);
}

constexpr BlePrimaryServiceRecord kBlePrimaryServices[] = {
    {kHandleGapService, kHandleGapPpcpValue, kUuidGapService},
    {kHandleGattService, kHandleGattServiceChangedCccd, kUuidGattService},
    {kHandleBatteryService, kHandleBatteryLevelCccd, kUuidBatteryService},
};

constexpr BleCharacteristicRecord kBleCharacteristics[] = {
    {kHandleGapDeviceNameDecl, kGattCharacteristicPropRead,
     kHandleGapDeviceNameValue, kUuidDeviceName},
    {kHandleGapAppearanceDecl, kGattCharacteristicPropRead,
     kHandleGapAppearanceValue, kUuidAppearance},
    {kHandleGapPpcpDecl, kGattCharacteristicPropRead,
     kHandleGapPpcpValue, kUuidPpcp},
    {kHandleGattServiceChangedDecl, kGattCharacteristicPropIndicate,
     kHandleGattServiceChangedValue, kUuidServiceChanged},
    {kHandleBatteryLevelDecl, static_cast<uint8_t>(kGattCharacteristicPropRead |
                                                   kGattCharacteristicPropNotify),
     kHandleBatteryLevelValue, kUuidBatteryLevel},
};

constexpr BleAttributeUuidRecord kBleAttributeUuids[] = {
    {kHandleGapService, kUuidPrimaryService},
    {kHandleGapDeviceNameDecl, kUuidCharacteristic},
    {kHandleGapDeviceNameValue, kUuidDeviceName},
    {kHandleGapAppearanceDecl, kUuidCharacteristic},
    {kHandleGapAppearanceValue, kUuidAppearance},
    {kHandleGapPpcpDecl, kUuidCharacteristic},
    {kHandleGapPpcpValue, kUuidPpcp},
    {kHandleGattService, kUuidPrimaryService},
    {kHandleGattServiceChangedDecl, kUuidCharacteristic},
    {kHandleGattServiceChangedValue, kUuidServiceChanged},
    {kHandleGattServiceChangedCccd, kUuidClientCharacteristicConfig},
    {kHandleBatteryService, kUuidPrimaryService},
    {kHandleBatteryLevelDecl, kUuidCharacteristic},
    {kHandleBatteryLevelValue, kUuidBatteryLevel},
    {kHandleBatteryLevelCccd, kUuidClientCharacteristicConfig},
};

uint8_t bleChannelToFrequency(xiao_nrf54l15::BleAdvertisingChannel channel) {
  switch (channel) {
    case xiao_nrf54l15::BleAdvertisingChannel::k37:
      return 2U;
    case xiao_nrf54l15::BleAdvertisingChannel::k38:
      return 26U;
    case xiao_nrf54l15::BleAdvertisingChannel::k39:
    default:
      return 80U;
  }
}

uint8_t bleChannelToIndex(xiao_nrf54l15::BleAdvertisingChannel channel) {
  switch (channel) {
    case xiao_nrf54l15::BleAdvertisingChannel::k37:
      return 37U;
    case xiao_nrf54l15::BleAdvertisingChannel::k38:
      return 38U;
    case xiao_nrf54l15::BleAdvertisingChannel::k39:
    default:
      return 39U;
  }
}

uint8_t bleDataChannelToFrequency(uint8_t dataChannel) {
  if (dataChannel > 36U) {
    return 4U;
  }
  if (dataChannel <= 10U) {
    return static_cast<uint8_t>(4U + (2U * dataChannel));
  }
  return static_cast<uint8_t>(6U + (2U * dataChannel));
}

bool ieee802154ChannelValid(uint8_t channel) {
  return (channel >= 11U) && (channel <= 26U);
}

uint8_t ieee802154ChannelToFrequency(uint8_t channel) {
  return static_cast<uint8_t>(5U + ((channel - 11U) * 5U));
}

uint32_t radioTxPowerRegFromDbm(int8_t dbm) {
  if (dbm >= 8) {
    return RADIO_TXPOWER_TXPOWER_Pos8dBm;
  }
  if (dbm >= 7) {
    return RADIO_TXPOWER_TXPOWER_Pos7dBm;
  }
  if (dbm >= 6) {
    return RADIO_TXPOWER_TXPOWER_Pos6dBm;
  }
  if (dbm >= 5) {
    return RADIO_TXPOWER_TXPOWER_Pos5dBm;
  }
  if (dbm >= 4) {
    return RADIO_TXPOWER_TXPOWER_Pos4dBm;
  }
  if (dbm >= 3) {
    return RADIO_TXPOWER_TXPOWER_Pos3dBm;
  }
  if (dbm >= 2) {
    return RADIO_TXPOWER_TXPOWER_Pos2dBm;
  }
  if (dbm >= 1) {
    return RADIO_TXPOWER_TXPOWER_Pos1dBm;
  }
  if (dbm >= 0) {
    return RADIO_TXPOWER_TXPOWER_0dBm;
  }
  if (dbm >= -1) {
    return RADIO_TXPOWER_TXPOWER_Neg1dBm;
  }
  if (dbm >= -2) {
    return RADIO_TXPOWER_TXPOWER_Neg2dBm;
  }
  if (dbm >= -3) {
    return RADIO_TXPOWER_TXPOWER_Neg3dBm;
  }
  if (dbm >= -4) {
    return RADIO_TXPOWER_TXPOWER_Neg4dBm;
  }
  if (dbm >= -5) {
    return RADIO_TXPOWER_TXPOWER_Neg5dBm;
  }
  if (dbm >= -6) {
    return RADIO_TXPOWER_TXPOWER_Neg6dBm;
  }
  if (dbm >= -7) {
    return RADIO_TXPOWER_TXPOWER_Neg7dBm;
  }
  if (dbm >= -8) {
    return RADIO_TXPOWER_TXPOWER_Neg8dBm;
  }
  if (dbm >= -9) {
    return RADIO_TXPOWER_TXPOWER_Neg9dBm;
  }
  if (dbm >= -10) {
    return RADIO_TXPOWER_TXPOWER_Neg10dBm;
  }
  if (dbm >= -12) {
    return RADIO_TXPOWER_TXPOWER_Neg12dBm;
  }
  if (dbm >= -14) {
    return RADIO_TXPOWER_TXPOWER_Neg14dBm;
  }
  if (dbm >= -16) {
    return RADIO_TXPOWER_TXPOWER_Neg16dBm;
  }
  if (dbm >= -18) {
    return RADIO_TXPOWER_TXPOWER_Neg18dBm;
  }
  if (dbm >= -20) {
    return RADIO_TXPOWER_TXPOWER_Neg20dBm;
  }
  if (dbm >= -28) {
    return RADIO_TXPOWER_TXPOWER_Neg28dBm;
  }
  if (dbm >= -40) {
    return RADIO_TXPOWER_TXPOWER_Neg40dBm;
  }
  return RADIO_TXPOWER_TXPOWER_Neg46dBm;
}

uint16_t readLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         (static_cast<uint16_t>(p[1]) << 8U);
}

uint32_t readLe24(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8U) |
         (static_cast<uint32_t>(p[2]) << 16U);
}

uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8U) |
         (static_cast<uint32_t>(p[2]) << 16U) |
         (static_cast<uint32_t>(p[3]) << 24U);
}

void writeLe16(uint8_t* p, uint16_t v) {
  if (p == nullptr) {
    return;
  }
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
}

uint8_t bleConnectionPayloadProtocolOpcode(uint8_t llid, const uint8_t* payload,
                                           uint8_t length) {
  if (payload == nullptr || length == 0U) {
    return 0U;
  }
  if (llid == kBlePduLlControl) {
    return payload[0];
  }
  if ((llid == kBlePduDataStartOrComplete) && (length >= 5U)) {
    const uint16_t l2capLen = readLe16(&payload[0]);
    const uint16_t cid = readLe16(&payload[2]);
    const uint8_t availableAttLen =
        (length > kBleL2capHeaderLen)
            ? static_cast<uint8_t>(length - kBleL2capHeaderLen)
            : 0U;
    if ((cid == kBleL2capCidAtt) && (l2capLen > 0U) && (availableAttLen > 0U)) {
      return payload[kBleL2capHeaderLen];
    }
  }
  return payload[0];
}

void writeLe24(uint8_t* p, uint32_t v) {
  if (p == nullptr) {
    return;
  }
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
  p[2] = static_cast<uint8_t>((v >> 16U) & 0xFFU);
}

void writeLe32(uint8_t* p, uint32_t v) {
  if (p == nullptr) {
    return;
  }
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
  p[2] = static_cast<uint8_t>((v >> 16U) & 0xFFU);
  p[3] = static_cast<uint8_t>((v >> 24U) & 0xFFU);
}

void fillPseudoRandomBytes(uint8_t* out, uint8_t len, uint32_t seed) {
  if (out == nullptr || len == 0U) {
    return;
  }

  uint32_t state = seed ^ 0xA55AA55AUL;
  if (state == 0U) {
    state = 0x1F123BB5UL;
  }
  for (uint8_t i = 0U; i < len; ++i) {
    // Xorshift32 for lightweight deterministic byte generation.
    state ^= (state << 13U);
    state ^= (state >> 17U);
    state ^= (state << 5U);
    out[i] = static_cast<uint8_t>(state & 0xFFU);
  }
}

uint32_t crc32(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0U) {
    return 0U;
  }

  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0U; i < length; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      const uint32_t mask = (crc & 1U) ? 0xFFFFFFFFUL : 0UL;
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

bool bleBondRecordLooksSane(const xiao_nrf54l15::BleBondRecord& record) {
  if (record.peerAddressRandom > 1U || record.peerIdentityAddressRandom > 1U ||
      record.localAddressRandom > 1U || record.peerIrkValid > 1U) {
    return false;
  }
  if (record.keySize < 7U || record.keySize > 16U) {
    return false;
  }
  bool keyNonZero = false;
  for (uint8_t i = 0U; i < 16U; ++i) {
    if (record.ltk[i] != 0U) {
      keyNonZero = true;
      break;
    }
  }
  return keyNonZero;
}

void clearBondRecordV2Tail(xiao_nrf54l15::BleBondRecord* record) {
  if (record == nullptr) {
    return;
  }
  memset(record->peerIdentityAddress, 0, sizeof(record->peerIdentityAddress));
  record->peerIdentityAddressRandom = 0U;
  memset(record->peerIrk, 0, sizeof(record->peerIrk));
  record->peerIrkValid = 0U;
  memset(record->reserved, 0, sizeof(record->reserved));
}

void convertLegacyBondRecord(const BleBondRecordV1& legacy,
                             xiao_nrf54l15::BleBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return;
  }
  memset(outRecord, 0, sizeof(*outRecord));
  memcpy(outRecord->peerAddress, legacy.peerAddress, sizeof(outRecord->peerAddress));
  outRecord->peerAddressRandom = legacy.peerAddressRandom;
  memcpy(outRecord->localAddress, legacy.localAddress, sizeof(outRecord->localAddress));
  outRecord->localAddressRandom = legacy.localAddressRandom;
  memcpy(outRecord->ltk, legacy.ltk, sizeof(outRecord->ltk));
  memcpy(outRecord->rand, legacy.rand, sizeof(outRecord->rand));
  outRecord->ediv = legacy.ediv;
  outRecord->keySize = legacy.keySize;
  clearBondRecordV2Tail(outRecord);
}

NRF_RRAMC_Type* bondRramc() {
  return reinterpret_cast<NRF_RRAMC_Type*>(kBleBondRramcBase);
}

bool waitForBondRramcReady(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
  if (rramc == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    if (((rramc->READY & RRAMC_READY_READY_Msk) >> RRAMC_READY_READY_Pos) ==
        RRAMC_READY_READY_Ready) {
      return true;
    }
  }
  return false;
}

bool waitForBondRramcReadyNext(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
  if (rramc == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    if (((rramc->READYNEXT & RRAMC_READYNEXT_READYNEXT_Msk) >>
         RRAMC_READYNEXT_READYNEXT_Pos) == RRAMC_READYNEXT_READYNEXT_Ready) {
      return true;
    }
  }
  return false;
}

void copyFromVolatileMemory(const volatile uint8_t* src, uint8_t* dst, size_t len) {
  if (src == nullptr || dst == nullptr) {
    return;
  }
  for (size_t i = 0U; i < len; ++i) {
    dst[i] = src[i];
  }
}

bool writeBondBlobToRram(const BleBondRetentionBlob& blob) {
  NRF_RRAMC_Type* const rramc = bondRramc();
  if (rramc == nullptr) {
    return false;
  }

  const uint32_t targetAddress =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_bleBondFlashStorage));
  const uint8_t* const src = reinterpret_cast<const uint8_t*>(&blob);
  const uint32_t prevConfig = rramc->CONFIG;
  const uint32_t writeConfig = prevConfig | RRAMC_CONFIG_WEN_Msk;
  bool writeOk = true;

  rramc->CONFIG = writeConfig;
  if (!waitForBondRramcReady(rramc, kBleBondRramcSpinLimit)) {
    writeOk = false;
  }

  if (writeOk) {
    rramc->EVENTS_ACCESSERROR = 0U;
    for (size_t i = 0U; i < sizeof(blob); ++i) {
      if (!waitForBondRramcReadyNext(rramc, kBleBondRramcSpinLimit)) {
        writeOk = false;
        break;
      }
      *reinterpret_cast<volatile uint8_t*>(targetAddress + static_cast<uint32_t>(i)) = src[i];
    }
    if (rramc->EVENTS_ACCESSERROR != 0U) {
      writeOk = false;
    }
  }

  if (writeOk) {
    rramc->EVENTS_READY = 0U;
    rramc->TASKS_COMMITWRITEBUF = 1U;
    writeOk = waitForBondRramcReady(rramc, kBleBondRramcSpinLimit);
  }

  rramc->CONFIG = prevConfig;
  if (!waitForBondRramcReady(rramc, kBleBondRramcSpinLimit)) {
    writeOk = false;
  }
  return writeOk;
}

void clearFlashBondBlobImage(BleBondRetentionBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  memset(blob, 0, sizeof(*blob));
}

void buildFlashBondBlob(const xiao_nrf54l15::BleBondRecord& record,
                        BleBondRetentionBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  memset(blob, 0, sizeof(*blob));
  blob->magic = kBleBondRetentionMagic;
  blob->version = kBleBondRetentionVersion;
  blob->recordLength = static_cast<uint16_t>(sizeof(record));
  memcpy(&blob->record, &record, sizeof(record));
  blob->crc32 = crc32(reinterpret_cast<const uint8_t*>(&blob->record), sizeof(blob->record));
}

bool readFlashBondBlob(BleBondRetentionBlob* outBlob) {
  if (outBlob == nullptr) {
    return false;
  }
  copyFromVolatileMemory(reinterpret_cast<const volatile uint8_t*>(&g_bleBondFlashStorage),
                         reinterpret_cast<uint8_t*>(outBlob), sizeof(*outBlob));
  return true;
}

bool writeFlashBondBlob(const BleBondRetentionBlob& blob) {
  if (!writeBondBlobToRram(blob)) {
    return false;
  }
  BleBondRetentionBlob verify{};
  if (!readFlashBondBlob(&verify)) {
    return false;
  }
  return (memcmp(&verify, &blob, sizeof(blob)) == 0);
}

bool clearFlashBondRecord() {
  BleBondRetentionBlob blob{};
  clearFlashBondBlobImage(&blob);
  return writeFlashBondBlob(blob);
}

bool writeFlashBondRecord(const xiao_nrf54l15::BleBondRecord& record) {
  if (!bleBondRecordLooksSane(record)) {
    return false;
  }
  BleBondRetentionBlob blob{};
  buildFlashBondBlob(record, &blob);
  return writeFlashBondBlob(blob);
}

bool readFlashBondRecord(xiao_nrf54l15::BleBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return false;
  }

  BleBondRetentionBlob blob{};
  if (!readFlashBondBlob(&blob)) {
    return false;
  }
  if (blob.magic != kBleBondRetentionMagic) {
    return false;
  }
  if (blob.version == kBleBondRetentionVersion &&
      blob.recordLength == sizeof(blob.record)) {
    const uint32_t computedCrc =
        crc32(reinterpret_cast<const uint8_t*>(&blob.record), sizeof(blob.record));
    if (computedCrc != blob.crc32) {
      return false;
    }
    if (!bleBondRecordLooksSane(blob.record)) {
      return false;
    }
    memcpy(outRecord, &blob.record, sizeof(*outRecord));
    return true;
  }
  if (blob.version == 1U && blob.recordLength == sizeof(BleBondRecordV1)) {
    BleBondRecordV1 legacy{};
    memcpy(&legacy, &blob.record, sizeof(legacy));
    const uint32_t computedCrc =
        crc32(reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy));
    if (computedCrc != blob.crc32) {
      return false;
    }
    convertLegacyBondRecord(legacy, outRecord);
    return bleBondRecordLooksSane(*outRecord);
  }
  return false;
}

void clearRetainedBondBlob() {
  memset(&g_bleBondRetention, 0, sizeof(g_bleBondRetention));
}

bool writeRetainedBondRecord(const xiao_nrf54l15::BleBondRecord& record) {
  if (!bleBondRecordLooksSane(record)) {
    return false;
  }
  g_bleBondRetention.magic = kBleBondRetentionMagic;
  g_bleBondRetention.version = kBleBondRetentionVersion;
  g_bleBondRetention.recordLength = static_cast<uint16_t>(sizeof(record));
  memcpy(&g_bleBondRetention.record, &record, sizeof(record));
  g_bleBondRetention.crc32 =
      crc32(reinterpret_cast<const uint8_t*>(&g_bleBondRetention.record),
            sizeof(g_bleBondRetention.record));
  return true;
}

bool readRetainedBondRecord(xiao_nrf54l15::BleBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return false;
  }
  if (g_bleBondRetention.magic != kBleBondRetentionMagic) {
    return false;
  }
  if (g_bleBondRetention.version == kBleBondRetentionVersion &&
      g_bleBondRetention.recordLength == sizeof(g_bleBondRetention.record)) {
    const uint32_t computedCrc =
        crc32(reinterpret_cast<const uint8_t*>(&g_bleBondRetention.record),
              sizeof(g_bleBondRetention.record));
    if (computedCrc != g_bleBondRetention.crc32) {
      return false;
    }
    if (!bleBondRecordLooksSane(g_bleBondRetention.record)) {
      return false;
    }
    memcpy(outRecord, &g_bleBondRetention.record, sizeof(*outRecord));
    return true;
  }
  if (g_bleBondRetention.version == 1U &&
      g_bleBondRetention.recordLength == sizeof(BleBondRecordV1)) {
    BleBondRecordV1 legacy{};
    memcpy(&legacy, &g_bleBondRetention.record, sizeof(legacy));
    const uint32_t computedCrc =
        crc32(reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy));
    if (computedCrc != g_bleBondRetention.crc32) {
      return false;
    }
    convertLegacyBondRecord(legacy, outRecord);
    return bleBondRecordLooksSane(*outRecord);
  }
  return false;
}

constexpr uint8_t kAesSbox[256] = {
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B,
    0xFE, 0xD7, 0xAB, 0x76, 0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0,
    0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0, 0xB7, 0xFD, 0x93, 0x26,
    0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2,
    0xEB, 0x27, 0xB2, 0x75, 0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0,
    0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84, 0x53, 0xD1, 0x00, 0xED,
    0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F,
    0x50, 0x3C, 0x9F, 0xA8, 0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5,
    0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2, 0xCD, 0x0C, 0x13, 0xEC,
    0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14,
    0xDE, 0x5E, 0x0B, 0xDB, 0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C,
    0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79, 0xE7, 0xC8, 0x37, 0x6D,
    0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F,
    0x4B, 0xBD, 0x8B, 0x8A, 0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E,
    0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E, 0xE1, 0xF8, 0x98, 0x11,
    0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F,
    0xB0, 0x54, 0xBB, 0x16};

constexpr uint8_t kAesRcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10,
                                   0x20, 0x40, 0x80, 0x1B, 0x36};

using AesState = uint8_t[4][4];

uint8_t aesXtime(uint8_t x) {
  return static_cast<uint8_t>((x << 1U) ^ (((x >> 7U) & 0x01U) * 0x1BU));
}

void aesAddRoundKey(uint8_t round, AesState* state, const uint8_t* roundKey) {
  for (uint8_t i = 0U; i < 4U; ++i) {
    for (uint8_t j = 0U; j < 4U; ++j) {
      (*state)[i][j] ^= roundKey[(round * 16U) + (i * 4U) + j];
    }
  }
}

void aesSubBytes(AesState* state) {
  for (uint8_t i = 0U; i < 4U; ++i) {
    for (uint8_t j = 0U; j < 4U; ++j) {
      (*state)[j][i] = kAesSbox[(*state)[j][i]];
    }
  }
}

void aesShiftRows(AesState* state) {
  uint8_t temp = (*state)[0][1];
  (*state)[0][1] = (*state)[1][1];
  (*state)[1][1] = (*state)[2][1];
  (*state)[2][1] = (*state)[3][1];
  (*state)[3][1] = temp;

  temp = (*state)[0][2];
  (*state)[0][2] = (*state)[2][2];
  (*state)[2][2] = temp;
  temp = (*state)[1][2];
  (*state)[1][2] = (*state)[3][2];
  (*state)[3][2] = temp;

  temp = (*state)[0][3];
  (*state)[0][3] = (*state)[3][3];
  (*state)[3][3] = (*state)[2][3];
  (*state)[2][3] = (*state)[1][3];
  (*state)[1][3] = temp;
}

void aesMixColumns(AesState* state) {
  for (uint8_t i = 0U; i < 4U; ++i) {
    uint8_t t = (*state)[i][0];
    uint8_t tmp = static_cast<uint8_t>((*state)[i][0] ^ (*state)[i][1] ^
                                       (*state)[i][2] ^ (*state)[i][3]);
    uint8_t tm = static_cast<uint8_t>((*state)[i][0] ^ (*state)[i][1]);
    tm = aesXtime(tm);
    (*state)[i][0] ^= static_cast<uint8_t>(tm ^ tmp);
    tm = static_cast<uint8_t>((*state)[i][1] ^ (*state)[i][2]);
    tm = aesXtime(tm);
    (*state)[i][1] ^= static_cast<uint8_t>(tm ^ tmp);
    tm = static_cast<uint8_t>((*state)[i][2] ^ (*state)[i][3]);
    tm = aesXtime(tm);
    (*state)[i][2] ^= static_cast<uint8_t>(tm ^ tmp);
    tm = static_cast<uint8_t>((*state)[i][3] ^ t);
    tm = aesXtime(tm);
    (*state)[i][3] ^= static_cast<uint8_t>(tm ^ tmp);
  }
}

void aesKeyExpansion128(const uint8_t key[16], uint8_t roundKey[176]) {
  for (uint8_t i = 0U; i < 16U; ++i) {
    roundKey[i] = key[i];
  }

  uint8_t temp[4] = {0U, 0U, 0U, 0U};
  uint16_t bytesGenerated = 16U;
  uint8_t rconIndex = 1U;
  while (bytesGenerated < 176U) {
    for (uint8_t i = 0U; i < 4U; ++i) {
      temp[i] = roundKey[static_cast<uint16_t>(bytesGenerated - 4U + i)];
    }

    if ((bytesGenerated % 16U) == 0U) {
      const uint8_t rot = temp[0];
      temp[0] = temp[1];
      temp[1] = temp[2];
      temp[2] = temp[3];
      temp[3] = rot;
      temp[0] = kAesSbox[temp[0]];
      temp[1] = kAesSbox[temp[1]];
      temp[2] = kAesSbox[temp[2]];
      temp[3] = kAesSbox[temp[3]];
      temp[0] ^= kAesRcon[rconIndex];
      ++rconIndex;
    }

    for (uint8_t i = 0U; i < 4U; ++i) {
      roundKey[bytesGenerated] =
          static_cast<uint8_t>(roundKey[bytesGenerated - 16U] ^ temp[i]);
      ++bytesGenerated;
    }
  }
}

// AES-128 encryption path adapted from tiny-AES-c (Unlicense), reduced to ECB block encrypt.
void aesEncryptBlockBe(const uint8_t key[16], const uint8_t plaintext[16], uint8_t out[16]) {
  uint8_t roundKey[176] = {0};
  uint8_t stateBuffer[16] = {0};
  aesKeyExpansion128(key, roundKey);
  memcpy(stateBuffer, plaintext, 16U);

  auto* state = reinterpret_cast<AesState*>(&stateBuffer[0]);
  aesAddRoundKey(0U, state, roundKey);
  for (uint8_t round = 1U; round < 10U; ++round) {
    aesSubBytes(state);
    aesShiftRows(state);
    aesMixColumns(state);
    aesAddRoundKey(round, state, roundKey);
  }
  aesSubBytes(state);
  aesShiftRows(state);
  aesAddRoundKey(10U, state, roundKey);
  memcpy(out, stateBuffer, 16U);
}

void reverseBytes(const uint8_t* in, uint8_t* out, uint8_t len) {
  if (in == nullptr || out == nullptr) {
    return;
  }
  for (uint8_t i = 0U; i < len; ++i) {
    out[i] = in[static_cast<uint8_t>(len - 1U - i)];
  }
}

void xor16(const uint8_t* a, const uint8_t* b, uint8_t* out) {
  if (a == nullptr || b == nullptr || out == nullptr) {
    return;
  }
  for (uint8_t i = 0U; i < 16U; ++i) {
    out[i] = static_cast<uint8_t>(a[i] ^ b[i]);
  }
}

bool aesEncryptLeSoftware(const uint8_t keyLe[16], const uint8_t plaintextLe[16],
                         uint8_t outLe[16]) {
  if (keyLe == nullptr || plaintextLe == nullptr || outLe == nullptr) {
    return false;
  }

  uint8_t keyBe[16] = {0};
  uint8_t plainBe[16] = {0};
  uint8_t encBe[16] = {0};
  reverseBytes(keyLe, keyBe, 16U);
  reverseBytes(plaintextLe, plainBe, 16U);
  aesEncryptBlockBe(keyBe, plainBe, encBe);
  reverseBytes(encBe, outLe, 16U);
  return true;
}

bool aesEncryptCcmBlockSoftware(const uint8_t keyLe[16], const uint8_t plaintext[16],
                                uint8_t out[16]) {
  if (keyLe == nullptr || plaintext == nullptr || out == nullptr) {
    return false;
  }

  // Link-layer CCM operates on byte strings as-is (AES big-endian block
  // convention). Session keys are held in Bluetooth little-endian order, so
  // only the key bytes are swapped before the block encrypt.
  uint8_t keyBe[16] = {0};
  reverseBytes(keyLe, keyBe, 16U);
  aesEncryptBlockBe(keyBe, plaintext, out);
  return true;
}

bool aesEncryptLe(const uint8_t keyLe[16], const uint8_t plaintextLe[16],
                  uint8_t outLe[16]);
bool bleCcmEncryptPayload(const uint8_t key[16], const uint8_t iv[8],
                          uint64_t counter, uint8_t direction, uint8_t header,
                          const uint8_t* plaintext, uint8_t plaintextLen,
                          uint8_t* outCipherWithMic, uint8_t* outCipherWithMicLen);
bool bleCcmDecryptPayload(const uint8_t key[16], const uint8_t iv[8],
                          uint64_t counter, uint8_t direction, uint8_t header,
                          const uint8_t* cipherWithMic, uint8_t cipherWithMicLen,
                          uint8_t* outPlaintext, uint8_t* outPlaintextLen);
bool bleCcmEncryptPayloadSoftware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* plaintext,
                                  uint8_t plaintextLen, uint8_t* outCipherWithMic,
                                  uint8_t* outCipherWithMicLen);
bool bleCcmDecryptPayloadSoftware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* cipherWithMic,
                                  uint8_t cipherWithMicLen, uint8_t* outPlaintext,
                                  uint8_t* outPlaintextLen);

#if !defined(NRF54L15_CLEAN_BLE_HARDWARE_CRYPTO)
// Default the live BLE path to the silicon ECB/CCM blocks. The runtime still
// self-tests the hardware backend once and falls back to the software path if
// the peripheral bring-up or packet formatting does not match the software
// reference implementation on this board/package.
#define NRF54L15_CLEAN_BLE_HARDWARE_CRYPTO 1
#endif

constexpr uint32_t kBleHardwareCryptoEcbSpinLimit = 200000UL;
constexpr uint32_t kBleHardwareCryptoCcmSpinLimit = 200000UL;
constexpr uint32_t kBleSecurityRandomSpinLimit = 400000UL;
