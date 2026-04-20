
struct BleHardwareCryptoState {
  bool checked;
  bool usable;
};

BleHardwareCryptoState& bleHardwareCryptoState() {
  static BleHardwareCryptoState state = {false, false};
  return state;
}

Ecb& bleHardwareEcb() {
  static Ecb ecb;
  return ecb;
}

Ccm& bleHardwareCcm() {
  static Ccm ccm;
  return ccm;
}

bool aesEncryptLeHardware(const uint8_t keyLe[16], const uint8_t plaintextLe[16],
                          uint8_t outLe[16]) {
  return bleHardwareEcb().encryptBlock(keyLe, plaintextLe, outLe,
                                       kBleHardwareCryptoEcbSpinLimit);
}

bool bleCcmEncryptPayloadHardware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* plaintext,
                                  uint8_t plaintextLen, uint8_t* outCipherWithMic,
                                  uint8_t* outCipherWithMicLen) {
  return bleHardwareCcm().encryptBlePacket(
      key, iv, counter, direction, header, plaintext, plaintextLen,
      outCipherWithMic, outCipherWithMicLen, CcmBleDataRate::k1Mbit, 0xE3U,
      kBleHardwareCryptoCcmSpinLimit);
}

bool bleCcmDecryptPayloadHardware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* cipherWithMic,
                                  uint8_t cipherWithMicLen, uint8_t* outPlaintext,
                                  uint8_t* outPlaintextLen) {
  bool macValid = false;
  return bleHardwareCcm().decryptBlePacket(
             key, iv, counter, direction, header, cipherWithMic,
             cipherWithMicLen, outPlaintext, outPlaintextLen, &macValid,
             CcmBleDataRate::k1Mbit, 0xE3U, kBleHardwareCryptoCcmSpinLimit) &&
         macValid;
}

bool bleHardwareCryptoSelfTest() {
  const uint8_t key[16] = {0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xABU, 0xCDU, 0xEFU,
                           0x10U, 0x32U, 0x54U, 0x76U, 0x98U, 0xBAU, 0xDCU, 0xFEU};
  const uint8_t plaintextLe[16] = {0xFEU, 0xDCU, 0xBAU, 0x98U, 0x76U, 0x54U, 0x32U, 0x10U,
                                   0xEFU, 0xCDU, 0xABU, 0x89U, 0x67U, 0x45U, 0x23U, 0x01U};
  uint8_t swEcb[16] = {0};
  uint8_t hwEcb[16] = {0};
  if (!aesEncryptLeSoftware(key, plaintextLe, swEcb) ||
      !aesEncryptLeHardware(key, plaintextLe, hwEcb) ||
      (memcmp(swEcb, hwEcb, sizeof(swEcb)) != 0)) {
    return false;
  }

  const uint8_t iv[8] = {0x34U, 0x12U, 0x78U, 0x56U, 0xBCU, 0x9AU, 0xF0U, 0xDEU};
  const uint8_t header = 0x0BU;
  const uint64_t counter = 0x12345ULL;
  const uint8_t direction = 1U;
  const uint8_t payload[] = {
      0x03U, 0x07U, 0x00U, 0x06U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U,
      0x77U, 0x88U, 0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xF0U};
  uint8_t swCipher[kBleDataPduMaxPayload + kBleMicLen] = {0};
  uint8_t hwCipher[kBleDataPduMaxPayload + kBleMicLen] = {0};
  uint8_t swCipherLen = 0U;
  uint8_t hwCipherLen = 0U;
  if (!bleCcmEncryptPayloadSoftware(key, iv, counter, direction, header, payload,
                                    sizeof(payload), swCipher, &swCipherLen) ||
      !bleCcmEncryptPayloadHardware(key, iv, counter, direction, header, payload,
                                    sizeof(payload), hwCipher, &hwCipherLen) ||
      (swCipherLen != hwCipherLen) ||
      (memcmp(swCipher, hwCipher, swCipherLen) != 0)) {
    return false;
  }

  uint8_t swPlain[kBleDataPduMaxPayload] = {0};
  uint8_t hwPlain[kBleDataPduMaxPayload] = {0};
  uint8_t swPlainLen = 0U;
  uint8_t hwPlainLen = 0U;
  if (!bleCcmDecryptPayloadSoftware(key, iv, counter, direction, header, swCipher,
                                    swCipherLen, swPlain, &swPlainLen) ||
      !bleCcmDecryptPayloadHardware(key, iv, counter, direction, header, swCipher,
                                    swCipherLen, hwPlain, &hwPlainLen) ||
      (swPlainLen != sizeof(payload)) || (hwPlainLen != sizeof(payload)) ||
      (memcmp(swPlain, payload, sizeof(payload)) != 0) ||
      (memcmp(hwPlain, payload, sizeof(payload)) != 0)) {
    return false;
  }

  return true;
}

bool bleHardwareCryptoAvailable() {
#if NRF54L15_CLEAN_BLE_HARDWARE_CRYPTO == 1
  BleHardwareCryptoState& state = bleHardwareCryptoState();
  if (!state.checked) {
    state.checked = true;
    state.usable = bleHardwareCryptoSelfTest();
  }
  return state.usable;
#else
  return false;
#endif
}

void bleHardwareCryptoDisable() {
  BleHardwareCryptoState& state = bleHardwareCryptoState();
  state.checked = true;
  state.usable = false;
}

bool fillBleSecurityRandomBytes(uint8_t* out, uint8_t len, uint32_t seed,
                                uint32_t spinLimit) {
  if (out == nullptr || len == 0U) {
    return false;
  }

  CracenRng rng;
  if (rng.fill(out, len, spinLimit)) {
    return true;
  }

  fillPseudoRandomBytes(out, len, seed);
  return false;
}

bool smpC1(const uint8_t tk[16], const uint8_t r[16], const uint8_t preq[7],
           const uint8_t pres[7], uint8_t iat, const uint8_t ia[6], uint8_t rat,
           const uint8_t ra[6], uint8_t out[16]) {
  if (tk == nullptr || r == nullptr || preq == nullptr || pres == nullptr ||
      ia == nullptr || ra == nullptr || out == nullptr) {
    return false;
  }

  uint8_t p1[16] = {0};
  uint8_t p2[16] = {0};
  uint8_t tmp[16] = {0};
  p1[0] = iat;
  p1[1] = rat;
  memcpy(&p1[2], preq, 7U);
  memcpy(&p1[9], pres, 7U);

  xor16(r, p1, tmp);
  if (!aesEncryptLe(tk, tmp, tmp)) {
    return false;
  }

  memcpy(&p2[0], ra, 6U);
  memcpy(&p2[6], ia, 6U);
  memset(&p2[12], 0, 4U);
  xor16(tmp, p2, tmp);
  return aesEncryptLe(tk, tmp, out);
}

bool smpS1(const uint8_t tk[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t out[16]) {
  if (tk == nullptr || r1 == nullptr || r2 == nullptr || out == nullptr) {
    return false;
  }
  uint8_t plaintext[16] = {0};
  // s1(k, r1, r2) per BLE spec Vol 3 Part H §2.2.4:
  //   AES input (big-endian) = r1[63:0] || r2[63:0]
  //                          = Srand_BE || Mrand_BE (with r1=Srand, r2=Mrand)
  // aesEncryptLe byte-reverses the plaintext before AES, so we must supply
  // the LE buffer in reverse order: [r2 LSBs, r1 LSBs] to get [r1_BE, r2_BE]
  // after the reversal.
  memcpy(&plaintext[0], r2, 8U);
  memcpy(&plaintext[8], r1, 8U);
  return aesEncryptLe(tk, plaintext, out);
}

void buildBleCcmNonce(const uint8_t iv[8], uint64_t counter, uint8_t direction,
                      uint8_t nonce[13]) {
  if (iv == nullptr || nonce == nullptr) {
    return;
  }

  const uint64_t ctr = counter & 0x7FFFFFFFFFULL;
  nonce[0] = static_cast<uint8_t>(ctr & 0xFFU);
  nonce[1] = static_cast<uint8_t>((ctr >> 8U) & 0xFFU);
  nonce[2] = static_cast<uint8_t>((ctr >> 16U) & 0xFFU);
  nonce[3] = static_cast<uint8_t>((ctr >> 24U) & 0xFFU);
  nonce[4] = static_cast<uint8_t>(((ctr >> 32U) & 0x7FU) | ((direction & 0x01U) << 7U));
  // BLE sample data and controller interop both expect the nonce IV bytes in
  // the same little-endian order they are carried over LL control PDUs:
  // IVm first, then IVs.
  memcpy(&nonce[5], iv, 8U);
}

void buildCcmCtrBlock(const uint8_t nonce[13], uint16_t ctr, uint8_t out[16]) {
  if (nonce == nullptr || out == nullptr) {
    return;
  }

  memset(out, 0, 16U);
  out[0] = 0x01U;  // L = 2 bytes for BLE CCM.
  memcpy(&out[1], nonce, 13U);
  out[14] = static_cast<uint8_t>((ctr >> 8U) & 0xFFU);
  out[15] = static_cast<uint8_t>(ctr & 0xFFU);
}

bool bleComputeCcmMicSoftware(const uint8_t key[16], const uint8_t nonce[13],
                              uint8_t header, const uint8_t* payload,
                              uint8_t payloadLen, uint8_t outMic[4]) {
  if (key == nullptr || nonce == nullptr || outMic == nullptr) {
    return false;
  }
  if (payloadLen > 0U && payload == nullptr) {
    return false;
  }

  uint8_t x[16] = {0};
  uint8_t block[16] = {0};
  uint8_t tmp[16] = {0};

  // B0 flags: Adata present + M=4 + L=2.
  block[0] = 0x49U;
  memcpy(&block[1], nonce, 13U);
  block[14] = 0x00U;
  block[15] = payloadLen;
  xor16(x, block, tmp);
  if (!aesEncryptCcmBlockSoftware(key, tmp, x)) {
    return false;
  }

  // AAD is one-byte header with SN/NESN/MD masked to zero (0xE3).
  memset(block, 0, sizeof(block));
  block[0] = 0x00U;
  block[1] = 0x01U;
  block[2] = static_cast<uint8_t>(header & 0xE3U);
  xor16(x, block, tmp);
  if (!aesEncryptCcmBlockSoftware(key, tmp, x)) {
    return false;
  }

  uint8_t offset = 0U;
  while (offset < payloadLen) {
    memset(block, 0, sizeof(block));
    const uint8_t remaining = static_cast<uint8_t>(payloadLen - offset);
    const uint8_t chunk = (remaining < 16U) ? remaining : 16U;
    memcpy(block, &payload[offset], chunk);
    xor16(x, block, tmp);
    if (!aesEncryptCcmBlockSoftware(key, tmp, x)) {
      return false;
    }
    offset = static_cast<uint8_t>(offset + chunk);
  }

  uint8_t s0[16] = {0};
  buildCcmCtrBlock(nonce, 0U, block);
  if (!aesEncryptCcmBlockSoftware(key, block, s0)) {
    return false;
  }

  for (uint8_t i = 0U; i < 4U; ++i) {
    outMic[i] = static_cast<uint8_t>(x[i] ^ s0[i]);
  }
  return true;
}

bool bleCcmEncryptPayloadSoftware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* plaintext,
                                  uint8_t plaintextLen, uint8_t* outCipherWithMic,
                                  uint8_t* outCipherWithMicLen) {
  if (key == nullptr || iv == nullptr || outCipherWithMic == nullptr ||
      outCipherWithMicLen == nullptr) {
    return false;
  }
  if (plaintextLen > 0U && plaintext == nullptr) {
    return false;
  }
  if (plaintextLen > kBleDataPduMaxPayload) {
    return false;
  }

  uint8_t nonce[13] = {0};
  buildBleCcmNonce(iv, counter, direction, nonce);

  uint8_t offset = 0U;
  uint16_t ctr = 1U;
  while (offset < plaintextLen) {
    uint8_t stream[16] = {0};
    uint8_t ctrBlock[16] = {0};
    buildCcmCtrBlock(nonce, ctr, ctrBlock);
    if (!aesEncryptCcmBlockSoftware(key, ctrBlock, stream)) {
      return false;
    }

    const uint8_t remaining = static_cast<uint8_t>(plaintextLen - offset);
    const uint8_t chunk = (remaining < 16U) ? remaining : 16U;
    for (uint8_t i = 0U; i < chunk; ++i) {
      outCipherWithMic[offset + i] =
          static_cast<uint8_t>(plaintext[offset + i] ^ stream[i]);
    }
    offset = static_cast<uint8_t>(offset + chunk);
    ++ctr;
  }

  uint8_t mic[4] = {0};
  if (!bleComputeCcmMicSoftware(key, nonce, header, plaintext, plaintextLen, mic)) {
    return false;
  }
  memcpy(&outCipherWithMic[plaintextLen], mic, sizeof(mic));
  *outCipherWithMicLen = static_cast<uint8_t>(plaintextLen + kBleMicLen);
  return true;
}

bool bleCcmDecryptPayloadSoftware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* cipherWithMic,
                                  uint8_t cipherWithMicLen, uint8_t* outPlaintext,
                                  uint8_t* outPlaintextLen) {
  if (key == nullptr || iv == nullptr || cipherWithMic == nullptr ||
      outPlaintext == nullptr || outPlaintextLen == nullptr) {
    return false;
  }
  if (cipherWithMicLen < kBleMicLen) {
    return false;
  }

  const uint8_t payloadLen = static_cast<uint8_t>(cipherWithMicLen - kBleMicLen);
  if (payloadLen > kBleDataPduMaxPayload) {
    return false;
  }

  uint8_t nonce[13] = {0};
  buildBleCcmNonce(iv, counter, direction, nonce);

  uint8_t offset = 0U;
  uint16_t ctr = 1U;
  while (offset < payloadLen) {
    uint8_t stream[16] = {0};
    uint8_t ctrBlock[16] = {0};
    buildCcmCtrBlock(nonce, ctr, ctrBlock);
    if (!aesEncryptCcmBlockSoftware(key, ctrBlock, stream)) {
      return false;
    }

    const uint8_t remaining = static_cast<uint8_t>(payloadLen - offset);
    const uint8_t chunk = (remaining < 16U) ? remaining : 16U;
    for (uint8_t i = 0U; i < chunk; ++i) {
      outPlaintext[offset + i] =
          static_cast<uint8_t>(cipherWithMic[offset + i] ^ stream[i]);
    }
    offset = static_cast<uint8_t>(offset + chunk);
    ++ctr;
  }

  uint8_t expectedMic[4] = {0};
  if (!bleComputeCcmMicSoftware(key, nonce, header, outPlaintext, payloadLen,
                                expectedMic)) {
    return false;
  }

  uint8_t diff = 0U;
  for (uint8_t i = 0U; i < 4U; ++i) {
    diff |= static_cast<uint8_t>(expectedMic[i] ^ cipherWithMic[payloadLen + i]);
  }
  if (diff != 0U) {
    return false;
  }

  *outPlaintextLen = payloadLen;
  return true;
}

bool aesEncryptLe(const uint8_t keyLe[16], const uint8_t plaintextLe[16],
                  uint8_t outLe[16]) {
  if (bleHardwareCryptoAvailable() &&
      aesEncryptLeHardware(keyLe, plaintextLe, outLe)) {
    return true;
  }
  if (bleHardwareCryptoAvailable()) {
    bleHardwareCryptoDisable();
  }
  return aesEncryptLeSoftware(keyLe, plaintextLe, outLe);
}

bool bleCcmEncryptPayload(const uint8_t key[16], const uint8_t iv[8],
                          uint64_t counter, uint8_t direction, uint8_t header,
                          const uint8_t* plaintext, uint8_t plaintextLen,
                          uint8_t* outCipherWithMic, uint8_t* outCipherWithMicLen) {
  if (bleHardwareCryptoAvailable() &&
      bleCcmEncryptPayloadHardware(key, iv, counter, direction, header, plaintext,
                                   plaintextLen, outCipherWithMic,
                                   outCipherWithMicLen)) {
    return true;
  }
  if (bleHardwareCryptoAvailable()) {
    bleHardwareCryptoDisable();
  }
  return bleCcmEncryptPayloadSoftware(key, iv, counter, direction, header,
                                      plaintext, plaintextLen, outCipherWithMic,
                                      outCipherWithMicLen);
}

bool bleCcmDecryptPayload(const uint8_t key[16], const uint8_t iv[8],
                          uint64_t counter, uint8_t direction, uint8_t header,
                          const uint8_t* cipherWithMic, uint8_t cipherWithMicLen,
                          uint8_t* outPlaintext, uint8_t* outPlaintextLen) {
  if (bleHardwareCryptoAvailable() &&
      bleCcmDecryptPayloadHardware(key, iv, counter, direction, header,
                                   cipherWithMic, cipherWithMicLen, outPlaintext,
                                   outPlaintextLen)) {
    return true;
  }
  if (bleHardwareCryptoAvailable()) {
    bleHardwareCryptoDisable();
  }
  return bleCcmDecryptPayloadSoftware(key, iv, counter, direction, header,
                                      cipherWithMic, cipherWithMicLen,
                                      outPlaintext, outPlaintextLen);
}

uint8_t bitCount37(const uint8_t map[5]) {
  if (map == nullptr) {
    return 0U;
  }

  uint8_t count = 0;
  for (uint8_t ch = 0; ch < 37U; ++ch) {
    if ((map[ch >> 3U] & (1U << (ch & 7U))) != 0U) {
      ++count;
    }
  }
  return count;
}

uint8_t remapChannelByIndex(const uint8_t map[5], uint8_t usedIndex) {
  uint8_t channel = 0;
  for (uint8_t byte = 0; byte < 5U; ++byte) {
    uint8_t bits = map[byte];
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      if (channel >= 37U) {
        break;
      }
      if ((bits & 0x01U) != 0U) {
        if (usedIndex == 0U) {
          return channel;
        }
        --usedIndex;
      }
      bits >>= 1U;
      ++channel;
    }
  }
  return 0U;
}

void blePeriphConnTraceReset() {
  g_ble_periph_conn_trace_count = 0U;
  for (uint8_t i = 0U; i < 16U; ++i) {
    g_ble_periph_conn_trace_stage[i] = 0U;
    g_ble_periph_conn_trace_channel[i] = 0U;
    g_ble_periph_conn_trace_event[i] = 0U;
    g_ble_periph_conn_trace_value[i] = 0U;
  }
}

void blePeriphConnTracePush(uint16_t eventCounter, uint8_t dataChannel,
                            uint8_t stage, uint8_t value) {
  const uint32_t count = g_ble_periph_conn_trace_count;
  const uint8_t slot = static_cast<uint8_t>(count & 0x0FU);
  g_ble_periph_conn_trace_stage[slot] = stage;
  g_ble_periph_conn_trace_channel[slot] = dataChannel;
  g_ble_periph_conn_trace_event[slot] = eventCounter;
  g_ble_periph_conn_trace_value[slot] = value;
  g_ble_periph_conn_trace_count = count + 1U;
}

void blePeriphRxTraceReset() {
  g_ble_periph_rx_trace_count = 0U;
  for (uint8_t i = 0U; i < 32U; ++i) {
    g_ble_periph_rx_trace_llid[i] = 0U;
    g_ble_periph_rx_trace_len[i] = 0U;
    g_ble_periph_rx_trace_flags[i] = 0U;
    g_ble_periph_rx_trace_opcode[i] = 0U;
    g_ble_periph_rx_trace_cid[i] = 0U;
  }
}

void blePeriphRxTracePush(uint8_t llid, uint8_t length, bool packetIsNew,
                          bool peerAckedLastTx, uint16_t l2capCid,
                          uint8_t opcode) {
  const uint32_t count = g_ble_periph_rx_trace_count;
  const uint8_t slot = static_cast<uint8_t>(count & 0x1FU);
  uint8_t flags = 0U;
  if (packetIsNew) {
    flags |= 0x01U;
  }
  if (peerAckedLastTx) {
    flags |= 0x02U;
  }
  if (l2capCid != 0U) {
    flags |= 0x04U;
  }
  g_ble_periph_rx_trace_llid[slot] = llid;
  g_ble_periph_rx_trace_len[slot] = length;
  g_ble_periph_rx_trace_flags[slot] = flags;
  g_ble_periph_rx_trace_opcode[slot] = opcode;
  g_ble_periph_rx_trace_cid[slot] = l2capCid;
  g_ble_periph_rx_trace_count = count + 1U;
}

void blePeriphTxTraceReset() {
  g_ble_periph_tx_trace_count = 0U;
  for (uint8_t i = 0U; i < 32U; ++i) {
    g_ble_periph_tx_trace_llid[i] = 0U;
    g_ble_periph_tx_trace_len[i] = 0U;
    g_ble_periph_tx_trace_opcode[i] = 0U;
    g_ble_periph_tx_trace_cid[i] = 0U;
  }
}

void blePeriphTxTracePush(uint8_t llid, uint8_t length, uint16_t l2capCid,
                          uint8_t opcode) {
  const uint32_t count = g_ble_periph_tx_trace_count;
  const uint8_t slot = static_cast<uint8_t>(count & 0x1FU);
  g_ble_periph_tx_trace_llid[slot] = llid;
  g_ble_periph_tx_trace_len[slot] = length;
  g_ble_periph_tx_trace_opcode[slot] = opcode;
  g_ble_periph_tx_trace_cid[slot] = l2capCid;
  g_ble_periph_tx_trace_count = count + 1U;
}

void blePeriphTxTracePushPacket(uint8_t llid, uint8_t length,
                                const uint8_t* payload) {
  if (length == 0U || payload == nullptr) {
    return;
  }
  uint16_t l2capCid = 0U;
  uint8_t opcode = 0U;
  if (llid == kBlePduLlControl) {
    opcode = payload[0];
  } else if (llid == kBlePduDataStartOrComplete) {
    if (length >= kBleL2capHeaderLen) {
      l2capCid = readLe16(&payload[2]);
    }
    if (length >= (kBleL2capHeaderLen + 1U)) {
      opcode = payload[4];
    }
  }
  blePeriphTxTracePush(llid, length, l2capCid, opcode);
}

bool timeReachedUs(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

bool isValidBleConnectionAccessAddress(uint32_t accessAddress) {
  if (accessAddress == kBleAccessAddress ||
      accessAddress == 0U ||
      accessAddress == 0xFFFFFFFFUL) {
    return false;
  }

  const uint32_t diffBits =
      static_cast<uint32_t>(__builtin_popcount(accessAddress ^ kBleAccessAddress));
  if (diffBits <= 1U) {
    return false;
  }

  uint8_t transitions = 0U;
  uint8_t runLength = 1U;
  uint8_t maxRunLength = 1U;
  bool lastBit = (accessAddress & 0x1U) != 0U;
  for (uint8_t bit = 1U; bit < 32U; ++bit) {
    const bool currentBit = ((accessAddress >> bit) & 0x1U) != 0U;
    if (currentBit != lastBit) {
      ++transitions;
      runLength = 1U;
    } else {
      ++runLength;
      if (runLength > maxRunLength) {
        maxRunLength = runLength;
      }
    }
    lastBit = currentBit;
  }

  if (maxRunLength > 6U || transitions > 24U) {
    return false;
  }

  uint8_t topTransitions = 0U;
  bool lastTopBit = ((accessAddress >> 31U) & 0x1U) != 0U;
  for (int8_t bit = 30; bit >= 26; --bit) {
    const bool currentBit = ((accessAddress >> bit) & 0x1U) != 0U;
    if (currentBit != lastTopBit) {
      ++topTransitions;
    }
    lastTopBit = currentBit;
  }
  return topTransitions >= 2U;
}

uint32_t nextPseudoRandomWord(uint32_t state) {
  uint32_t x = state;
  if (x == 0U) {
    x = 0xA5A55A5AUL;
  }
  x ^= (x << 13U);
  x ^= (x >> 17U);
  x ^= (x << 5U);
  return x;
}

uint32_t generateBleConnectionAccessAddress(uint32_t seed) {
  uint32_t state = seed ^ 0xC3A55A3CUL;
  for (uint8_t attempt = 0U; attempt < 64U; ++attempt) {
    state = nextPseudoRandomWord(state);
    if (isValidBleConnectionAccessAddress(state)) {
      return state;
    }
  }

  static constexpr uint32_t kFallbackCandidates[] = {
      0xAF9A8C69UL,
      0xD7A46C5BUL,
      0xC59A3B6DUL,
  };
  for (uint32_t candidate : kFallbackCandidates) {
    if (isValidBleConnectionAccessAddress(candidate)) {
      return candidate;
    }
  }

  return 0xAF9A8C69UL;
}

uint32_t bleMasterScaPpmUpperBound(uint8_t scaCode) {
  switch (scaCode & 0x07U) {
    case 0U:
      return 500UL;
    case 1U:
      return 250UL;
    case 2U:
      return 150UL;
    case 3U:
      return 100UL;
    case 4U:
      return 75UL;
    case 5U:
      return 50UL;
    case 6U:
      return 30UL;
    case 7U:
    default:
      return 20UL;
  }
}

uint32_t bleConnectionWindowWideningUs(uint16_t intervalUnits,
                                       uint8_t masterScaCode,
                                       uint16_t unsyncedEventCount = 1U) {
  if (intervalUnits == 0U || unsyncedEventCount == 0U) {
    return 0U;
  }

  const uint32_t intervalUs = static_cast<uint32_t>(intervalUnits) * 1250UL;
  const uint32_t totalPpm =
      bleMasterScaPpmUpperBound(masterScaCode) + kBleConnSlaveScaPpm;
  const uint64_t wideningProduct =
      static_cast<uint64_t>(intervalUs) * static_cast<uint64_t>(totalPpm) *
      static_cast<uint64_t>(unsyncedEventCount);
  return static_cast<uint32_t>((wideningProduct + 999999ULL) / 1000000ULL);
}

uint32_t bleConnectionRxListenUs(uint16_t intervalUnits, uint8_t masterScaCode,
                                 uint16_t unsyncedEventCount = 1U) {
  const uint32_t wideningUs =
      bleConnectionWindowWideningUs(intervalUnits, masterScaCode,
                                    unsyncedEventCount);
  uint32_t postAnchorListenUs = kBleConnRxListenBaseUs + wideningUs;
  if (postAnchorListenUs > kBleConnRxListenMaxUs) {
    postAnchorListenUs = kBleConnRxListenMaxUs;
  }
  // RX is armed kBleConnRxStartLeadUs before the nominal anchor. The radio
  // budget must include this lead, otherwise the listen window can expire
  // before the expected packet arrival.
  return kBleConnRxStartLeadUs + postAnchorListenUs;
}

uint32_t bleDataPduAirTimeUs(uint8_t payloadLength) {
  return static_cast<uint32_t>(10U + payloadLength) * 8U;
}

uint32_t bleConnectionTxEndBudgetUs(uint8_t payloadLength) {
  return bleDataPduAirTimeUs(payloadLength) + kBleConnTxDisableWaitUs + 400U;
}

uint8_t bleReverse8(uint8_t v) {
  v = static_cast<uint8_t>(((v & 0xF0U) >> 4U) | ((v & 0x0FU) << 4U));
  v = static_cast<uint8_t>(((v & 0xCCU) >> 2U) | ((v & 0x33U) << 2U));
  v = static_cast<uint8_t>(((v & 0xAAU) >> 1U) | ((v & 0x55U) << 1U));
  return v;
}

uint16_t bleCsa2Permute(uint16_t v) {
  const uint8_t msb = bleReverse8(static_cast<uint8_t>((v >> 8U) & 0xFFU));
  const uint8_t lsb = bleReverse8(static_cast<uint8_t>(v & 0xFFU));
  return static_cast<uint16_t>((static_cast<uint16_t>(msb) << 8U) | lsb);
}

uint16_t bleCsa2Mam(uint16_t a, uint16_t b) {
  return static_cast<uint16_t>((static_cast<uint32_t>(17U) * a + b) & 0xFFFFU);
}

uint16_t bleCsa2PrnE(uint16_t eventCounter, uint16_t channelId) {
  uint16_t prn = static_cast<uint16_t>(eventCounter ^ channelId);
  for (uint8_t i = 0U; i < 3U; ++i) {
    prn = bleCsa2Permute(prn);
    prn = bleCsa2Mam(prn, channelId);
  }
  return static_cast<uint16_t>(prn ^ channelId);
}

bool waitRadioEndBudgeted(NRF_RADIO_Type* radio, uint32_t budgetUs, uint32_t spinLimit,
                          uint32_t* endTimestampUs = nullptr) {
  if (radio == nullptr || (spinLimit == 0U && budgetUs == 0U)) {
    return false;
  }

  const uint32_t startUs = bleTimingUs();
  uint8_t divider = kBleConnMicrosPollDivider;
  while (true) {
    if (radio->EVENTS_PHYEND != 0U || radio->EVENTS_END != 0U) {
      if (endTimestampUs != nullptr) {
        *endTimestampUs = bleTimingUs();
      }
      return true;
    }

    if (--divider == 0U) {
      divider = kBleConnMicrosPollDivider;
      const uint32_t elapsedUs = static_cast<uint32_t>(bleTimingUs() - startUs);
      if ((budgetUs > 0U) && (elapsedUs >= budgetUs)) {
        break;
      }
      if ((budgetUs == 0U) && (spinLimit == 0U)) {
        break;
      }
    }

    if (spinLimit > 0U) {
      --spinLimit;
      if (spinLimit == 0U) {
        break;
      }
    }
  }

  const bool endSeen = (radio->EVENTS_PHYEND != 0U || radio->EVENTS_END != 0U);
  if (endSeen && endTimestampUs != nullptr) {
    *endTimestampUs = bleTimingUs();
  }
  return endSeen;
}

bool waitRadioDisabledBudgeted(NRF_RADIO_Type* radio, uint32_t budgetUs, uint32_t spinLimit) {
  if (radio == nullptr || (spinLimit == 0U && budgetUs == 0U)) {
    return false;
  }

  const uint32_t startUs = bleTimingUs();
  uint8_t divider = kBleConnMicrosPollDivider;
  while (true) {
    if (radio->EVENTS_DISABLED != 0U) {
      return true;
    }

    const uint32_t state =
        (radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
    if (state == RADIO_STATE_STATE_Disabled) {
      return true;
    }

    if (--divider == 0U) {
      divider = kBleConnMicrosPollDivider;
      const uint32_t elapsedUs = static_cast<uint32_t>(bleTimingUs() - startUs);
      if ((budgetUs > 0U) && (elapsedUs >= budgetUs)) {
        break;
      }
      if (spinLimit == 0U) {
        break;
      }
    }

    if (spinLimit > 0U) {
      --spinLimit;
    }
  }

  if (radio->EVENTS_DISABLED != 0U) {
    return true;
  }
  const uint32_t state =
      (radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  return (state == RADIO_STATE_STATE_Disabled);
}

bool waitRadioRxDoneBudgeted(NRF_RADIO_Type* radio, uint32_t budgetUs, uint32_t spinLimit,
                             uint32_t* outDoneTimestampUs = nullptr) {
  if (radio == nullptr || (spinLimit == 0U && budgetUs == 0U)) {
    return false;
  }

  const uint32_t startUs = bleTimingUs();
  uint8_t divider = kBleConnMicrosPollDivider;
  while (true) {
    if ((radio->EVENTS_CRCOK != 0U) || (radio->EVENTS_CRCERROR != 0U)) {
      if (outDoneTimestampUs != nullptr) {
        *outDoneTimestampUs = bleTimingUs();
      }
      return true;
    }

    if (--divider == 0U) {
      divider = kBleConnMicrosPollDivider;
      const uint32_t elapsedUs = static_cast<uint32_t>(bleTimingUs() - startUs);
      if ((budgetUs > 0U) && (elapsedUs >= budgetUs)) {
        break;
      }
      if ((budgetUs == 0U) && (spinLimit == 0U)) {
        break;
      }
    }

    if (spinLimit > 0U) {
      --spinLimit;
      if (spinLimit == 0U) {
        break;
      }
    }
  }

  const bool doneSeen = ((radio->EVENTS_CRCOK != 0U) || (radio->EVENTS_CRCERROR != 0U));
  if (doneSeen && outDoneTimestampUs != nullptr) {
    *outDoneTimestampUs = bleTimingUs();
  }
  return doneSeen;
}

void forceRadioDisabled(NRF_RADIO_Type* radio, uint32_t spinLimit) {
  if (radio == nullptr) {
    return;
  }

  radio->SHORTS = 0U;
  radio->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
  (void)waitRadioDisabledBudgeted(radio, 3000U, spinLimit);
  radio->EVENTS_READY = 0U;
  radio->EVENTS_TXREADY = 0U;
  radio->EVENTS_RXREADY = 0U;
  radio->EVENTS_ADDRESS = 0U;
  radio->EVENTS_PAYLOAD = 0U;
  radio->EVENTS_END = 0U;
  radio->EVENTS_DISABLED = 0U;
  radio->EVENTS_PHYEND = 0U;
  radio->EVENTS_PLLREADY = 0U;
  radio->EVENTS_CRCOK = 0U;
  radio->EVENTS_CRCERROR = 0U;
  radio->EVENTS_RXADDRESS = 0U;
  radio->EVENTS_DEVMATCH = 0U;
  radio->EVENTS_DEVMISS = 0U;
  radio->EVENTS_MHRMATCH = 0U;
  radio->EVENTS_SYNC = 0U;
}

uint8_t ieee802154MacPayloadLengthFromPhr(uint8_t phrLength) {
  // The IEEE 802.15.4 PHR length counts the on-air FCS as part of the PSDU.
  // The stack uses MAC payload lengths without the trailing two-byte FCS.
  return (phrLength >= 2U) ? static_cast<uint8_t>(phrLength - 2U) : 0U;
}

uint8_t ieee802154PhrLengthFromMacPayloadLength(uint8_t macLength) {
  return (macLength <= 125U) ? static_cast<uint8_t>(macLength + 2U) : 0U;
}

void setIeee802154CrcLengthMode(NRF_RADIO_Type* radio, bool includeFcsInLength) {
  if (radio == nullptr) {
    return;
  }
  uint32_t pcnf0 = radio->PCNF0 & ~RADIO_PCNF0_CRCINC_Msk;
  pcnf0 |= ((includeFcsInLength ? RADIO_PCNF0_CRCINC_Include
                                : RADIO_PCNF0_CRCINC_Exclude)
            << RADIO_PCNF0_CRCINC_Pos) & RADIO_PCNF0_CRCINC_Msk;
  radio->PCNF0 = pcnf0;
}

void snapshotZigbeeReceiveEvents(NRF_RADIO_Type* radio,
                                 ZigbeeTransmitDebug* debug) {
  if (radio == nullptr || debug == nullptr) {
    return;
  }

  debug->syncSeen = (radio->EVENTS_SYNC != 0U);
  debug->rxReadySeen = (radio->EVENTS_RXREADY != 0U);
  debug->frameStartSeen = (radio->EVENTS_FRAMESTART != 0U);
  debug->addressSeen = (radio->EVENTS_ADDRESS != 0U);
  debug->rxAddressSeen = (radio->EVENTS_RXADDRESS != 0U);
  debug->endEventSeen = (radio->EVENTS_END != 0U);
  debug->devMatchSeen = (radio->EVENTS_DEVMATCH != 0U);
  debug->devMissSeen = (radio->EVENTS_DEVMISS != 0U);
  debug->mhrMatchSeen = (radio->EVENTS_MHRMATCH != 0U);
}

bool llEventInstantReached(uint16_t currentEventCounter, uint16_t instant) {
  return static_cast<uint16_t>(currentEventCounter - instant) < 0x8000U;
}

uint32_t bleDataWhiteValue(uint8_t channelIndex) {
  const uint32_t iv = static_cast<uint32_t>(0x40U | (channelIndex & 0x3FU));
  const uint32_t poly = 0x89UL;  // x^7 + x^4 + 1
  return ((poly << RADIO_DATAWHITE_POLY_Pos) & RADIO_DATAWHITE_POLY_Msk) |
         ((iv << RADIO_DATAWHITE_IV_Pos) & RADIO_DATAWHITE_IV_Msk);
}

uint32_t bleAccessAddressBase(uint32_t accessAddress) {
  return (accessAddress << 8U);
}

uint32_t bleAccessAddressPrefix(uint32_t accessAddress) {
  return ((accessAddress >> 24U) & 0xFFU);
}

int8_t radioRssiDbm(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return 0;
  }
  const uint8_t raw =
      static_cast<uint8_t>((radio->RSSISAMPLE & RADIO_RSSISAMPLE_RSSISAMPLE_Msk) >>
                           RADIO_RSSISAMPLE_RSSISAMPLE_Pos);
  return -static_cast<int8_t>(raw);
}

void clearRadioCoreEvents(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return;
  }

  radio->EVENTS_READY = 0U;
  radio->EVENTS_TXREADY = 0U;
  radio->EVENTS_RXREADY = 0U;
  radio->EVENTS_ADDRESS = 0U;
  radio->EVENTS_PAYLOAD = 0U;
  radio->EVENTS_END = 0U;
  radio->EVENTS_PHYEND = 0U;
  radio->EVENTS_DISABLED = 0U;
  radio->EVENTS_PLLREADY = 0U;
  radio->EVENTS_CRCOK = 0U;
  radio->EVENTS_CRCERROR = 0U;
  radio->EVENTS_RXADDRESS = 0U;
  radio->EVENTS_DEVMATCH = 0U;
  radio->EVENTS_DEVMISS = 0U;
  radio->EVENTS_MHRMATCH = 0U;
  radio->EVENTS_SYNC = 0U;
}

bool bleAddressEqual(const uint8_t* a, const uint8_t* b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  return memcmp(a, b, 6U) == 0;
}

bool bleAddressLooksResolvablePrivate(const uint8_t* address) {
  if (address == nullptr) {
    return false;
  }
  return (address[5] & 0xC0U) == 0x40U;
}

uint8_t minU8(uint8_t a, uint8_t b) { return (a < b) ? a : b; }

uint16_t minU16(uint16_t a, uint16_t b) { return (a < b) ? a : b; }

bool inHandleRange(uint16_t handle, uint16_t start, uint16_t end) {
  return (handle >= start) && (handle <= end);
}

void bleUuid16ToBytes(uint16_t uuid16, uint8_t out[16]) {
  if (out == nullptr) {
    return;
  }
  memset(out, 0, 16U);
  writeLe16(out, uuid16);
}

bool bleUuidIs16(const uint8_t* uuid, uint8_t uuidSize) {
  return (uuid != nullptr) && (uuidSize == kBleUuid16Size);
}

uint16_t bleUuidRead16(const uint8_t* uuid, uint8_t uuidSize) {
  return bleUuidIs16(uuid, uuidSize) ? readLe16(uuid) : 0U;
}

bool bleUuidMatches16(const uint8_t* uuid, uint8_t uuidSize, uint16_t uuid16) {
  return bleUuidRead16(uuid, uuidSize) == uuid16;
}

bool bleUuidEquals(const uint8_t* lhs, uint8_t lhsSize, const uint8_t* rhs,
                   uint8_t rhsSize) {
  if (lhs == nullptr || rhs == nullptr || lhsSize != rhsSize) {
    return false;
  }
  return memcmp(lhs, rhs, lhsSize) == 0;
}

bool bleUuidEqualsAttWire(const uint8_t* storedUuid, uint8_t storedUuidSize,
                          const uint8_t* attWireUuid, uint8_t attWireUuidSize) {
  if (storedUuid == nullptr || attWireUuid == nullptr ||
      storedUuidSize != attWireUuidSize) {
    return false;
  }
  if (storedUuidSize == kBleUuid128Size) {
    for (uint8_t i = 0U; i < kBleUuid128Size; ++i) {
      if (storedUuid[i] != attWireUuid[kBleUuid128Size - 1U - i]) {
        return false;
      }
    }
    return true;
  }
  return memcmp(storedUuid, attWireUuid, storedUuidSize) == 0;
}

void writeUuidToAttWire(uint8_t* out, const uint8_t* uuid, uint8_t uuidSize) {
  if (out == nullptr || uuid == nullptr) {
    return;
  }
  if (uuidSize == kBleUuid128Size) {
    for (uint8_t i = 0U; i < kBleUuid128Size; ++i) {
      out[i] = uuid[kBleUuid128Size - 1U - i];
    }
    return;
  }
  memcpy(out, uuid, uuidSize);
}

uint8_t bleCharacteristicDeclarationValueLen(uint8_t uuidSize) {
  return (uuidSize == kBleUuid128Size) ? 19U : 5U;
}

void writeCharacteristicDeclarationValue(uint8_t* out,
                                         uint8_t properties,
                                         uint16_t valueHandle,
                                         const uint8_t* uuid,
                                         uint8_t uuidSize) {
  if (out == nullptr || uuid == nullptr) {
    return;
  }

  out[0] = properties;
  writeLe16(&out[1], valueHandle);
  if (uuidSize == kBleUuid128Size) {
    writeUuidToAttWire(&out[3], uuid, kBleUuid128Size);
  } else {
    writeLe16(&out[3], bleUuidRead16(uuid, uuidSize));
  }
}

extern "C" void nrf54l15_bluefruit_compat_idle_service(void)
    __attribute__((weak));
extern "C" uint32_t nrf54l15_bluefruit_compat_idle_sleep_cap_us(void)
    __attribute__((weak));

bool g_bleIdleServiceActive = false;

}  // namespace
