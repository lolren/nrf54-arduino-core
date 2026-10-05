namespace {

constexpr uint32_t kVdmaDescriptorAttrEcbData =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_EcbData
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrAarHash =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_AarHash
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrAarPrand =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_AarPrand
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrAarIrk =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_AarIrk
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
// Nordic confirmed the output job list needs a dedicated 0x11 descriptor byte,
// not the generic AarHash value that the datasheet implies.
constexpr uint32_t kVdmaDescriptorAttrAarResolvedIndex =
    (0x11UL << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrCcmAlen =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_CcmAlen
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrCcmMlen =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_CcmMlen
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrCcmAdata =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_CcmAdata
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr uint32_t kVdmaDescriptorAttrCcmMdata =
    (VDMADESCRIPTOR_CONFIG_ATTRIBUTE_CcmMdata
     << VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Pos) &
    VDMADESCRIPTOR_CONFIG_ATTRIBUTE_Msk;
constexpr size_t kAarMaxIrksScratch = 64U;
alignas(4) uint8_t g_aarAddressScratch[6] = {};
alignas(4) uint8_t g_aarIrkScratch[kAarMaxIrksScratch * 16U] = {};
alignas(4) uint16_t g_aarResolvedIndexScratch = 0xFFFFU;
alignas(4) NRF_VDMADESCRIPTOR_Type g_aarInJobs[kAarMaxIrksScratch + 3U] = {};
alignas(4) NRF_VDMADESCRIPTOR_Type g_aarOutJobs[2] = {};
alignas(4) volatile int16_t g_saadcSampleScratch = 0;
volatile bool g_cracenRngOwned = false;

void clearPdmEvent(volatile uint32_t* event) {
  *event = 0U;
  // Nordic requires an event-register readback before a following task can
  // safely rely on the cleared state.
  (void)*event;
}

bool waitForPdmEvent(volatile uint32_t* event, volatile uint32_t* error,
                     uint32_t timeoutUs) {
  const uint32_t startedUs = micros();
  do {
    if (*event != 0U) {
      return true;
    }
    if (error != nullptr && *error != 0U) {
      return false;
    }
  } while (static_cast<uint32_t>(micros() - startedUs) < timeoutUs);
  return *event != 0U;
}

uint16_t pdmRatioDivisor(uint8_t ratio) {
  switch (ratio) {
#if !defined(NRF54LM20A_XXAA) && !defined(NRF54LM20B_XXAA)
    case PDM_RATIO_RATIO_Ratio32:
      return 32U;
#endif
    case PDM_RATIO_RATIO_Ratio48:
      return 48U;
    case PDM_RATIO_RATIO_Ratio50:
      return 50U;
    case PDM_RATIO_RATIO_Ratio64:
      return 64U;
    case PDM_RATIO_RATIO_Ratio80:
      return 80U;
    case PDM_RATIO_RATIO_Ratio96:
      return 96U;
#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
    case PDM_RATIO_RATIO_Ratio150:
      return 150U;
    case PDM_RATIO_RATIO_Ratio192:
      return 192U;
    case PDM_RATIO_RATIO_Custom:
      return 0U;
#else
    case PDM_RATIO_RATIO_Ratio100:
      return 100U;
    case PDM_RATIO_RATIO_Ratio128:
      return 128U;
#endif
    default:
      return 0U;
  }
}

uint32_t pdmCaptureTimeoutUs(size_t sampleCount, uint32_t sampleRateHz) {
  if (sampleRateHz == 0U) {
    return 250000UL;
  }
  const uint64_t expectedUs =
      (static_cast<uint64_t>(sampleCount) * 1000000ULL + sampleRateHz - 1U) /
      sampleRateHz;
  const uint64_t budgetUs = expectedUs * 2ULL + 250000ULL;
  return (budgetUs > UINT32_MAX) ? UINT32_MAX
                                 : static_cast<uint32_t>(budgetUs);
}

bool tryAcquireCracenRng() {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const bool acquired = !g_cracenRngOwned;
  if (acquired) {
    g_cracenRngOwned = true;
  }
  if ((primask & 1U) == 0U) {
    __enable_irq();
  }
  return acquired;
}

void releaseCracenRng() {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  g_cracenRngOwned = false;
  if ((primask & 1U) == 0U) {
    __enable_irq();
  }
}

bool isQdecPortSupported(const Pin& pin) {
#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
  return pin.port == 1U || pin.port == 3U;
#else
  return pin.port == 1U;
#endif
}

bool isPdmPortSupported(const Pin& pin) {
#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
  return pin.port == 1U || pin.port == 3U;
#else
  return pin.port == 1U;
#endif
}

uint32_t packBigEndianWord(const uint8_t* bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24U) |
         (static_cast<uint32_t>(bytes[1]) << 16U) |
         (static_cast<uint32_t>(bytes[2]) << 8U) |
         static_cast<uint32_t>(bytes[3]);
}

void configureVdmaDescriptor(NRF_VDMADESCRIPTOR_Type* descriptor,
                             const uint8_t* data,
                             size_t length,
                             uint32_t attrBits) {
  if (descriptor == nullptr) {
    return;
  }

  descriptor->PTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
  descriptor->CONFIG =
      (static_cast<uint32_t>(length) & VDMADESCRIPTOR_CONFIG_CNT_Msk) | attrBits;
}

void zeroVdmaDescriptor(NRF_VDMADESCRIPTOR_Type* descriptor) {
  if (descriptor == nullptr) {
    return;
  }
  descriptor->PTR = 0U;
  descriptor->CONFIG = 0U;
}

void stageBleCcmNonceWords(const uint8_t iv[8], uint64_t counter,
                           uint8_t direction, uint32_t outWords[4]) {
  if (iv == nullptr || outWords == nullptr) {
    return;
  }

  uint8_t nonce[13] = {};
  uint8_t nonceRegisterBytes[16] = {};

  buildBleCcmNonce(iv, counter, direction, nonce);
  memcpy(&nonceRegisterBytes[3], nonce, sizeof(nonce));

  for (uint8_t i = 0U; i < 4U; ++i) {
    outWords[i] = packBigEndianWord(&nonceRegisterBytes[(3U - i) * 4U]);
  }
}

}  // namespace

CracenRng::CracenRng(uint32_t controlBase, uint32_t coreBase)
    : cracen_(reinterpret_cast<NRF_CRACEN_Type*>(
          static_cast<uintptr_t>(controlBase))),
      core_(reinterpret_cast<NRF_CRACENCORE_Type*>(
          static_cast<uintptr_t>(coreBase))),
      active_(false),
      conditioningKeySet_(false) {}

bool CracenRng::beginNonBlocking() {
  if (cracen_ == nullptr || core_ == nullptr) {
    return false;
  }
  if (active_) {
    return true;
  }
  if (!tryAcquireCracenRng()) {
    return false;
  }

  const uint32_t nextEnable = cracen_->ENABLE | CRACEN_ENABLE_RNG_Msk;
  cracen_->ENABLE = nextEnable;
  clearEvent();

  // Match Nordic's TRNG acquisition sequence: reset sticky FSM/FIFO state,
  // apply the SoC sampling parameters, then seed the conditioning key from
  // the first four entropy words before exposing any data to the caller.
  core_->RNGCONTROL.CONTROL = CRACENCORE_RNGCONTROL_CONTROL_SOFTRST_Msk;
  __asm volatile("dsb 0xF" ::: "memory");
#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
  core_->RNGCONTROL.WARMUPPERIOD = 512U;
  core_->RNGCONTROL.SAMPLINGPERIOD = 1U;
#else
  core_->RNGCONTROL.INITWAITVAL = 512U;
  core_->RNGCONTROL.SWOFFTMRVAL = 0U;
  core_->RNGCONTROL.CLKDIV = 0U;
#endif
  core_->RNGCONTROL.CONTROL =
      CRACENCORE_RNGCONTROL_CONTROL_ENABLE_Msk |
      ((4UL << CRACENCORE_RNGCONTROL_CONTROL_NB128BITBLOCKS_Pos) &
       CRACENCORE_RNGCONTROL_CONTROL_NB128BITBLOCKS_Msk);
  __asm volatile("dsb 0xF" ::: "memory");

  active_ = true;
  conditioningKeySet_ = false;
  if (!healthy()) {
    end();
    return false;
  }
  return true;
}

bool CracenRng::begin(uint32_t spinLimit) {
  if (active_) {
    return ensureDataAvailable(spinLimit);
  }
  for (uint8_t attempt = 0U; attempt < 2U; ++attempt) {
    if (beginNonBlocking() && ensureDataAvailable(spinLimit)) {
      return true;
    }
    end();
  }
  return false;
}

void CracenRng::end() {
  if (!active_) {
    return;
  }
  if (cracen_ == nullptr || core_ == nullptr) {
    active_ = false;
    conditioningKeySet_ = false;
    releaseCracenRng();
    return;
  }

  core_->RNGCONTROL.CONTROL = CRACENCORE_RNGCONTROL_CONTROL_SOFTRST_Msk;
  __asm volatile("dsb 0xF" ::: "memory");

  cracen_->ENABLE &= ~CRACEN_ENABLE_RNG_Msk;
  clearEvent();
  active_ = false;
  conditioningKeySet_ = false;
  releaseCracenRng();
}

bool CracenRng::fill(void* data, size_t length, uint32_t spinLimit) {
  if (data == nullptr) {
    return false;
  }
  memset(data, 0, length);

  const bool temporaryBegin = !active_;
  if (temporaryBegin && !begin(spinLimit)) {
    return false;
  }

  uint8_t* out = static_cast<uint8_t*>(data);
  size_t remaining = length;
  bool ok = true;
  while (remaining > 0U) {
    uint32_t word = 0U;
    if (!randomWord(&word, spinLimit)) {
      ok = false;
      break;
    }

    const size_t chunk = (remaining < sizeof(word)) ? remaining : sizeof(word);
    memcpy(out, &word, chunk);
    out += chunk;
    remaining -= chunk;
  }

  if (temporaryBegin) {
    end();
  }
  if (!ok) {
    memset(data, 0, length);
  }
  return ok;
}

bool CracenRng::randomWord(uint32_t* outWord, uint32_t spinLimit) {
  if (outWord == nullptr) {
    return false;
  }
  *outWord = 0U;

  const bool temporaryBegin = !active_;
  if (temporaryBegin && !begin(spinLimit)) {
    return false;
  }

  const bool ready = ensureDataAvailable(spinLimit);
  if (ready) {
    *outWord = core_->RNGCONTROL.FIFO[0];
  }

  if (temporaryBegin) {
    end();
  }
  return ready;
}

bool CracenRng::tryRandomWord(uint32_t* outWord) {
  if (outWord == nullptr) {
    return false;
  }
  *outWord = 0U;

  const bool temporaryBegin = !active_;
  if (temporaryBegin && !beginNonBlocking()) {
    return false;
  }

  const bool ready = healthy() && setupConditioningKeyIfAvailable() &&
                     (availableWords() != 0U);
  if (ready) {
    *outWord = core_->RNGCONTROL.FIFO[0];
  }

  if (temporaryBegin) {
    end();
  }
  return ready;
}

uint32_t CracenRng::availableWords() const {
  if (core_ == nullptr) {
    return 0U;
  }
  return core_->RNGCONTROL.FIFOLEVEL &
         CRACENCORE_RNGCONTROL_FIFOLEVEL_FIFOLEVEL_Msk;
}

uint32_t CracenRng::status() const {
  if (core_ == nullptr) {
    return 0xFFFFFFFFUL;
  }
  return core_->RNGCONTROL.STATUS;
}

bool CracenRng::healthy() const {
  const uint32_t currentStatus = status();
  const uint32_t state =
      (currentStatus & CRACENCORE_RNGCONTROL_STATUS_STATE_Msk) >>
      CRACENCORE_RNGCONTROL_STATUS_STATE_Pos;
  return (currentStatus != 0xFFFFFFFFUL) &&
         (state != CRACENCORE_RNGCONTROL_STATUS_STATE_ERROR);
}

bool CracenRng::active() const { return active_; }

void CracenRng::clearEvent() {
  if (cracen_ == nullptr) {
    return;
  }
  cracen_->EVENTS_RNG = 0U;
}

bool CracenRng::setupConditioningKeyIfAvailable() {
  if (conditioningKeySet_) {
    return true;
  }
  if (!active_ || !healthy() || availableWords() < 4U) {
    return false;
  }
  for (uint8_t i = 0U; i < 4U; ++i) {
    core_->RNGCONTROL.KEY[i] = core_->RNGCONTROL.FIFO[0];
  }
  conditioningKeySet_ = true;
  return true;
}

bool CracenRng::ensureDataAvailable(uint32_t spinLimit) {
  if (!active_ || core_ == nullptr) {
    return false;
  }

  while (spinLimit-- > 0U) {
    if (!healthy()) {
      return false;
    }
    if (setupConditioningKeyIfAvailable() && availableWords() != 0U) {
      return true;
    }
  }
  return false;
}

Aar::Aar(uint32_t base)
    : aar_(reinterpret_cast<NRF_AAR_Type*>(static_cast<uintptr_t>(base))) {}

bool Aar::resolveFirst(const uint8_t address[6], const uint8_t* irks,
                       size_t irkCount, bool* outResolved, uint16_t* outIndex,
                       uint32_t spinLimit) {
  if (aar_ == nullptr || address == nullptr || irks == nullptr ||
      outResolved == nullptr || irkCount == 0U ||
      irkCount > kAarMaxIrksScratch) {
    return false;
  }

  *outResolved = false;
  if (outIndex != nullptr) {
    *outIndex = 0xFFFFU;
  }

  uint8_t* const irkRam = g_aarIrkScratch;
  NRF_VDMADESCRIPTOR_Type* const inJobs = g_aarInJobs;
  NRF_VDMADESCRIPTOR_Type* const outJobs = g_aarOutJobs;

  memset(g_aarAddressScratch, 0, sizeof(g_aarAddressScratch));
  memset(g_aarIrkScratch, 0, irkCount * 16U);
  memset(g_aarInJobs, 0, (irkCount + 3U) * sizeof(NRF_VDMADESCRIPTOR_Type));
  memset(g_aarOutJobs, 0, sizeof(g_aarOutJobs));
  g_aarResolvedIndexScratch = 0xFFFFU;

  memcpy(g_aarAddressScratch, address, sizeof(g_aarAddressScratch));
  // Zephyr pre-swaps nRF54L resolving-list IRKs before feeding AAR.
  for (size_t irkIndex = 0; irkIndex < irkCount; ++irkIndex) {
    const uint8_t* src = irks + (irkIndex * 16U);
    uint8_t* dst = irkRam + (irkIndex * 16U);
    for (size_t byteIndex = 0; byteIndex < 16U; ++byteIndex) {
      dst[byteIndex] = src[15U - byteIndex];
    }
  }

  configureVdmaDescriptor(&inJobs[0], &g_aarAddressScratch[0], 3U,
                          kVdmaDescriptorAttrAarHash);
  configureVdmaDescriptor(&inJobs[1], &g_aarAddressScratch[3], 3U,
                          kVdmaDescriptorAttrAarPrand);
  for (size_t irkIndex = 0; irkIndex < irkCount; ++irkIndex) {
    configureVdmaDescriptor(&inJobs[2U + irkIndex], irkRam + (irkIndex * 16U),
                            16U, kVdmaDescriptorAttrAarIrk);
  }
  zeroVdmaDescriptor(&inJobs[2U + irkCount]);

  configureVdmaDescriptor(&outJobs[0],
                          reinterpret_cast<const uint8_t*>(&g_aarResolvedIndexScratch),
                          sizeof(g_aarResolvedIndexScratch),
                          kVdmaDescriptorAttrAarResolvedIndex);
  zeroVdmaDescriptor(&outJobs[1]);

  aar_->ENABLE = AAR_ENABLE_ENABLE_Disabled;
  aar_->INTENCLR = 0xFFFFFFFFUL;
  clearEvents();
  NVIC_ClearPendingIRQ(AAR00_IRQn);
  aar_->ENABLE = AAR_ENABLE_ENABLE_Enabled;
  aar_->MAXRESOLVED = 1U;
  aar_->IN.PTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&inJobs[0]));
  aar_->OUT.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&outJobs[0]));
  __asm volatile("dsb 0xF" ::: "memory");

  aar_->TASKS_START = AAR_TASKS_START_TASKS_START_Trigger;

  bool completed = false;
  while (spinLimit-- > 0U) {
    if (aar_->EVENTS_ERROR != 0U) {
      completed = false;
      break;
    }
    if (aar_->EVENTS_END != 0U) {
      completed = true;
      break;
    }
  }

  const uint32_t finalAmount = aar_->OUT.AMOUNT & AAR_OUT_AMOUNT_AMOUNT_Msk;
  const bool resolved = (aar_->EVENTS_RESOLVED != 0U) &&
                        (aar_->EVENTS_NOTRESOLVED == 0U);
  const uint16_t resolvedIndex = g_aarResolvedIndexScratch;

  if (!completed) {
    aar_->TASKS_STOP = AAR_TASKS_STOP_TASKS_STOP_Trigger;
  }
  *outResolved = completed && resolved;

  aar_->ENABLE = AAR_ENABLE_ENABLE_Disabled;
  NVIC_DisableIRQ(AAR00_IRQn);
  NVIC_ClearPendingIRQ(AAR00_IRQn);

  if (completed && resolved && outIndex != nullptr && finalAmount >= 2U) {
    *outIndex = resolvedIndex;
  }

  return completed && errorStatus() == AAR_ERRORSTATUS_ERRORSTATUS_NoError;
}

bool Aar::resolveSingle(const uint8_t address[6], const uint8_t irk[16],
                        bool* outResolved, uint32_t spinLimit) {
  return resolveFirst(address, irk, 1U, outResolved, nullptr, spinLimit);
}

uint32_t Aar::errorStatus() const {
  if (aar_ == nullptr) {
    return AAR_ERRORSTATUS_ERRORSTATUS_DmaError;
  }
  return aar_->ERRORSTATUS & AAR_ERRORSTATUS_ERRORSTATUS_Msk;
}

uint32_t Aar::resolvedAmountBytes() const {
  if (aar_ == nullptr) {
    return 0U;
  }
  return aar_->OUT.AMOUNT & AAR_OUT_AMOUNT_AMOUNT_Msk;
}

void Aar::clearEvents() {
  if (aar_ == nullptr) {
    return;
  }
  aar_->EVENTS_END = 0U;
  aar_->EVENTS_RESOLVED = 0U;
  aar_->EVENTS_NOTRESOLVED = 0U;
  aar_->EVENTS_ERROR = 0U;
}

Ecb::Ecb(uint32_t base)
    : ecb_(reinterpret_cast<NRF_ECB_Type*>(static_cast<uintptr_t>(base))) {}

bool Ecb::encryptBlock(const uint8_t key[16], const uint8_t plaintext[16],
                       uint8_t ciphertext[16], uint32_t spinLimit) {
  if (ecb_ == nullptr || key == nullptr || plaintext == nullptr ||
      ciphertext == nullptr) {
    return false;
  }

  alignas(4) uint8_t inputBlock[16];
  alignas(4) NRF_VDMADESCRIPTOR_Type inJobs[2] = {};
  alignas(4) NRF_VDMADESCRIPTOR_Type outJobs[2] = {};

  memcpy(inputBlock, plaintext, sizeof(inputBlock));
  configureVdmaDescriptor(&inJobs[0], inputBlock, sizeof(inputBlock),
                          kVdmaDescriptorAttrEcbData);
  configureVdmaDescriptor(&outJobs[0], ciphertext, 16U,
                          kVdmaDescriptorAttrEcbData);
  zeroVdmaDescriptor(&inJobs[1]);
  zeroVdmaDescriptor(&outJobs[1]);

  clearEvents();

  for (uint8_t i = 0U; i < 4U; ++i) {
    ecb_->KEY.VALUE[i] = packBigEndianWord(&key[(3U - i) * 4U]);
  }

  ecb_->IN.PTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&inJobs[0]));
  ecb_->OUT.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&outJobs[0]));
  __asm volatile("dsb 0xF" ::: "memory");

  ecb_->TASKS_START = ECB_TASKS_START_TASKS_START_Trigger;
  while (spinLimit-- > 0U) {
    if (ecb_->EVENTS_END != 0U) {
      ecb_->EVENTS_END = 0U;
      return (ecb_->ERRORSTATUS & ECB_ERRORSTATUS_ERRORSTATUS_Msk) ==
             ECB_ERRORSTATUS_ERRORSTATUS_NoError;
    }
    if (ecb_->EVENTS_ERROR != 0U) {
      ecb_->EVENTS_ERROR = 0U;
      return false;
    }
  }

  ecb_->TASKS_STOP = ECB_TASKS_STOP_TASKS_STOP_Trigger;
  return false;
}

bool Ecb::encryptBlockInPlace(const uint8_t key[16], uint8_t block[16],
                              uint32_t spinLimit) {
  return encryptBlock(key, block, block, spinLimit);
}

uint32_t Ecb::errorStatus() const {
  if (ecb_ == nullptr) {
    return ECB_ERRORSTATUS_ERRORSTATUS_DmaError;
  }
  return ecb_->ERRORSTATUS & ECB_ERRORSTATUS_ERRORSTATUS_Msk;
}

void Ecb::clearEvents() {
  if (ecb_ == nullptr) {
    return;
  }
  ecb_->EVENTS_END = 0U;
  ecb_->EVENTS_ERROR = 0U;
}

Ccm::Ccm(uint32_t base)
    : ccm_(reinterpret_cast<NRF_CCM_Type*>(static_cast<uintptr_t>(base))) {}

bool Ccm::encryptBlePacket(const uint8_t key[16], const uint8_t iv[8],
                           uint64_t counter, uint8_t direction, uint8_t header,
                           const uint8_t* plaintext, uint8_t plaintextLen,
                           uint8_t* outCipherWithMic,
                           uint8_t* outCipherWithMicLen,
                           CcmBleDataRate dataRate, uint8_t adataMask,
                           uint32_t spinLimit) {
  if (ccm_ == nullptr || key == nullptr || iv == nullptr ||
      outCipherWithMic == nullptr || outCipherWithMicLen == nullptr) {
    return false;
  }
  if (plaintextLen > 0U && plaintext == nullptr) {
    return false;
  }
  if (plaintextLen > kBleDataPduMaxPayload) {
    return false;
  }

  *outCipherWithMicLen = 0U;

  alignas(4) uint8_t inputPayloadScratch[kBleDataPduMaxPayload + kBleMicLen] = {};
  alignas(4) uint8_t outputPayloadScratch[kBleDataPduMaxPayload + kBleMicLen] = {};
  alignas(4) uint8_t adataScratch[1] = {
      static_cast<uint8_t>(header & 0xE3U)};
  alignas(4) uint8_t adataOutScratch[1] = {};
  alignas(4) NRF_VDMADESCRIPTOR_Type inJobs[5] = {};
  alignas(4) NRF_VDMADESCRIPTOR_Type outJobs[5] = {};
  uint16_t inAlen = 1U;
  uint16_t inMlen = plaintextLen;
  uint16_t outAlen = 0xFFFFU;
  uint16_t outMlen = 0xFFFFU;
  uint32_t nonceWords[4] = {};

  if (plaintextLen > 0U) {
    memcpy(inputPayloadScratch, plaintext, plaintextLen);
  }

  configureVdmaDescriptor(
      &inJobs[0], reinterpret_cast<const uint8_t*>(&inAlen), sizeof(inAlen),
      kVdmaDescriptorAttrCcmAlen);
  configureVdmaDescriptor(
      &inJobs[1], reinterpret_cast<const uint8_t*>(&inMlen), sizeof(inMlen),
      kVdmaDescriptorAttrCcmMlen);
  configureVdmaDescriptor(&inJobs[2], adataScratch, sizeof(adataScratch),
                          kVdmaDescriptorAttrCcmAdata);
  configureVdmaDescriptor(&inJobs[3], inputPayloadScratch, plaintextLen,
                          kVdmaDescriptorAttrCcmMdata);
  zeroVdmaDescriptor(&inJobs[4]);

  configureVdmaDescriptor(
      &outJobs[0], reinterpret_cast<const uint8_t*>(&outAlen), sizeof(outAlen),
      kVdmaDescriptorAttrCcmAlen);
  configureVdmaDescriptor(
      &outJobs[1], reinterpret_cast<const uint8_t*>(&outMlen), sizeof(outMlen),
      kVdmaDescriptorAttrCcmMlen);
  configureVdmaDescriptor(&outJobs[2], adataOutScratch, sizeof(adataOutScratch),
                          kVdmaDescriptorAttrCcmAdata);
  configureVdmaDescriptor(&outJobs[3], outputPayloadScratch,
                          static_cast<size_t>(plaintextLen + kBleMicLen),
                          kVdmaDescriptorAttrCcmMdata);
  zeroVdmaDescriptor(&outJobs[4]);

  stageBleCcmNonceWords(iv, counter, direction, nonceWords);

  ccm_->ENABLE = CCM_ENABLE_ENABLE_Disabled;
  ccm_->INTENCLR = 0xFFFFFFFFUL;
  clearEvents();
  NVIC_ClearPendingIRQ(CCM00_IRQn);
  ccm_->ENABLE = CCM_ENABLE_ENABLE_Enabled;
  ccm_->MODE = (CCM_MODE_MODE_Encryption << CCM_MODE_MODE_Pos) |
               (CCM_MODE_PROTOCOL_Ble << CCM_MODE_PROTOCOL_Pos) |
               (static_cast<uint32_t>(dataRate) << CCM_MODE_DATARATE_Pos) |
               (CCM_MODE_MACLEN_M4 << CCM_MODE_MACLEN_Pos);
  ccm_->RATEOVERRIDE = static_cast<uint32_t>(dataRate);
  ccm_->ADATAMASK = static_cast<uint32_t>(adataMask);
  for (uint8_t i = 0U; i < 4U; ++i) {
    ccm_->KEY.VALUE[i] = packBigEndianWord(&key[(3U - i) * 4U]);
    ccm_->NONCE.VALUE[i] = nonceWords[i];
  }
  ccm_->IN.PTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&inJobs[0]));
  ccm_->OUT.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&outJobs[0]));
  __asm volatile("dsb 0xF" ::: "memory");

  bool finished = false;
  ccm_->TASKS_START = CCM_TASKS_START_TASKS_START_Trigger;
  while (spinLimit-- > 0U) {
    if (ccm_->EVENTS_ERROR != 0U) {
      break;
    }
    if (ccm_->EVENTS_END != 0U) {
      finished = true;
      break;
    }
  }

  const uint32_t error = errorStatus();
  if (!finished) {
    ccm_->TASKS_STOP = CCM_TASKS_STOP_TASKS_STOP_Trigger;
  }
  const uint16_t actualOutMlen =
      (outMlen != 0U && outMlen != 0xFFFFU)
          ? outMlen
          : static_cast<uint16_t>(plaintextLen + kBleMicLen);
  const bool ok = finished &&
                  ((ccm_->EVENTS_ERROR & CCM_EVENTS_ERROR_EVENTS_ERROR_Msk) == 0U) &&
                  (error == CCM_ERRORSTATUS_ERRORSTATUS_NoError) &&
                  (actualOutMlen <= sizeof(outputPayloadScratch));

  if (ok) {
    memcpy(outCipherWithMic, outputPayloadScratch, actualOutMlen);
    *outCipherWithMicLen = static_cast<uint8_t>(actualOutMlen);
  }

  ccm_->ENABLE = CCM_ENABLE_ENABLE_Disabled;
  NVIC_ClearPendingIRQ(CCM00_IRQn);
  return ok;
}

bool Ccm::decryptBlePacket(const uint8_t key[16], const uint8_t iv[8],
                           uint64_t counter, uint8_t direction, uint8_t header,
                           const uint8_t* cipherWithMic, uint8_t cipherWithMicLen,
                           uint8_t* outPlaintext, uint8_t* outPlaintextLen,
                           bool* outMacValid, CcmBleDataRate dataRate,
                           uint8_t adataMask, uint32_t spinLimit) {
  if (ccm_ == nullptr || key == nullptr || iv == nullptr ||
      cipherWithMic == nullptr || outPlaintext == nullptr ||
      outPlaintextLen == nullptr) {
    return false;
  }
  if (outMacValid != nullptr) {
    *outMacValid = false;
  }
  *outPlaintextLen = 0U;

  if (cipherWithMicLen < kBleMicLen) {
    return false;
  }

  const uint8_t payloadLen = static_cast<uint8_t>(cipherWithMicLen - kBleMicLen);
  if (payloadLen > kBleDataPduMaxPayload) {
    return false;
  }

  alignas(4) uint8_t inputPayloadScratch[kBleDataPduMaxPayload + kBleMicLen] = {};
  alignas(4) uint8_t outputPayloadScratch[kBleDataPduMaxPayload + kBleMicLen] = {};
  alignas(4) uint8_t adataScratch[1] = {
      static_cast<uint8_t>(header & 0xE3U)};
  alignas(4) uint8_t adataOutScratch[1] = {};
  alignas(4) NRF_VDMADESCRIPTOR_Type inJobs[5] = {};
  alignas(4) NRF_VDMADESCRIPTOR_Type outJobs[5] = {};
  uint16_t inAlen = 1U;
  uint16_t inMlen = cipherWithMicLen;
  uint16_t outAlen = 0xFFFFU;
  uint16_t outMlen = 0xFFFFU;
  uint32_t nonceWords[4] = {};

  memcpy(inputPayloadScratch, cipherWithMic, cipherWithMicLen);

  configureVdmaDescriptor(
      &inJobs[0], reinterpret_cast<const uint8_t*>(&inAlen), sizeof(inAlen),
      kVdmaDescriptorAttrCcmAlen);
  configureVdmaDescriptor(
      &inJobs[1], reinterpret_cast<const uint8_t*>(&inMlen), sizeof(inMlen),
      kVdmaDescriptorAttrCcmMlen);
  configureVdmaDescriptor(&inJobs[2], adataScratch, sizeof(adataScratch),
                          kVdmaDescriptorAttrCcmAdata);
  configureVdmaDescriptor(&inJobs[3], inputPayloadScratch, cipherWithMicLen,
                          kVdmaDescriptorAttrCcmMdata);
  zeroVdmaDescriptor(&inJobs[4]);

  configureVdmaDescriptor(
      &outJobs[0], reinterpret_cast<const uint8_t*>(&outAlen), sizeof(outAlen),
      kVdmaDescriptorAttrCcmAlen);
  configureVdmaDescriptor(
      &outJobs[1], reinterpret_cast<const uint8_t*>(&outMlen), sizeof(outMlen),
      kVdmaDescriptorAttrCcmMlen);
  configureVdmaDescriptor(&outJobs[2], adataOutScratch, sizeof(adataOutScratch),
                          kVdmaDescriptorAttrCcmAdata);
  configureVdmaDescriptor(&outJobs[3], outputPayloadScratch, payloadLen,
                          kVdmaDescriptorAttrCcmMdata);
  zeroVdmaDescriptor(&outJobs[4]);

  stageBleCcmNonceWords(iv, counter, direction, nonceWords);

  ccm_->ENABLE = CCM_ENABLE_ENABLE_Disabled;
  ccm_->INTENCLR = 0xFFFFFFFFUL;
  clearEvents();
  NVIC_ClearPendingIRQ(CCM00_IRQn);
  ccm_->ENABLE = CCM_ENABLE_ENABLE_Enabled;
  ccm_->MODE = (CCM_MODE_MODE_Decryption << CCM_MODE_MODE_Pos) |
               (CCM_MODE_PROTOCOL_Ble << CCM_MODE_PROTOCOL_Pos) |
               (static_cast<uint32_t>(dataRate) << CCM_MODE_DATARATE_Pos) |
               (CCM_MODE_MACLEN_M4 << CCM_MODE_MACLEN_Pos);
  ccm_->RATEOVERRIDE = static_cast<uint32_t>(dataRate);
  ccm_->ADATAMASK = static_cast<uint32_t>(adataMask);
  for (uint8_t i = 0U; i < 4U; ++i) {
    ccm_->KEY.VALUE[i] = packBigEndianWord(&key[(3U - i) * 4U]);
    ccm_->NONCE.VALUE[i] = nonceWords[i];
  }
  ccm_->IN.PTR = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&inJobs[0]));
  ccm_->OUT.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&outJobs[0]));
  __asm volatile("dsb 0xF" ::: "memory");

  bool finished = false;
  ccm_->TASKS_START = CCM_TASKS_START_TASKS_START_Trigger;
  while (spinLimit-- > 0U) {
    if (ccm_->EVENTS_ERROR != 0U) {
      break;
    }
    if (ccm_->EVENTS_END != 0U) {
      finished = true;
      break;
    }
  }

  const uint32_t error = errorStatus();
  const bool macValid = macStatus();
  if (outMacValid != nullptr) {
    *outMacValid = macValid;
  }
  if (!finished) {
    ccm_->TASKS_STOP = CCM_TASKS_STOP_TASKS_STOP_Trigger;
  }
  const bool noError = finished &&
                       ((ccm_->EVENTS_ERROR & CCM_EVENTS_ERROR_EVENTS_ERROR_Msk) == 0U) &&
                       (error == CCM_ERRORSTATUS_ERRORSTATUS_NoError) &&
                       (outMlen <= payloadLen);

  if (noError) {
    memcpy(outPlaintext, outputPayloadScratch, outMlen);
    *outPlaintextLen = static_cast<uint8_t>(outMlen);
  }

  ccm_->ENABLE = CCM_ENABLE_ENABLE_Disabled;
  NVIC_ClearPendingIRQ(CCM00_IRQn);
  return noError && macValid;
}

uint32_t Ccm::errorStatus() const {
  if (ccm_ == nullptr) {
    return CCM_ERRORSTATUS_ERRORSTATUS_DmaError;
  }
  return ccm_->ERRORSTATUS & CCM_ERRORSTATUS_ERRORSTATUS_Msk;
}

bool Ccm::macStatus() const {
  if (ccm_ == nullptr) {
    return false;
  }
  return (ccm_->MACSTATUS & CCM_MACSTATUS_MACSTATUS_Msk) ==
         CCM_MACSTATUS_MACSTATUS_CheckPassed;
}

void Ccm::clearEvents() {
  if (ccm_ == nullptr) {
    return;
  }
  ccm_->EVENTS_END = 0U;
  ccm_->EVENTS_ERROR = 0U;
}

Comp::Comp(uint32_t base)
    : comp_(reinterpret_cast<NRF_COMP_Type*>(static_cast<uintptr_t>(base))),
      active_(false) {}

bool Comp::beginThreshold(const Pin& inputPin, uint16_t thresholdPermille,
                          uint16_t hysteresisPermille, CompReference reference,
                          CompSpeedMode speed, const Pin& externalReferencePin,
                          CompCurrentSource currentSource, uint32_t spinLimit) {
  return beginSingleEnded(inputPin, reference, thresholdPermille,
                          hysteresisPermille, speed, externalReferencePin,
                          currentSource, spinLimit);
}

bool Comp::beginSingleEnded(const Pin& inputPin, CompReference reference,
                            uint16_t thresholdPermille,
                            uint16_t hysteresisPermille, CompSpeedMode speed,
                            const Pin& externalReferencePin,
                            CompCurrentSource currentSource,
                            uint32_t spinLimit) {
  if (active_) {
    end();
  }

  if (!pinSupportsAnalogInput(inputPin)) {
    return false;
  }
  if (reference == CompReference::kExternalAref &&
      !pinSupportsAnalogInput(externalReferencePin)) {
    return false;
  }
  if (!claimComparator(ComparatorOwner::kComp)) {
    return false;
  }
  if (!configureAnalogPeripheralPin(inputPin) ||
      (reference == CompReference::kExternalAref &&
       !configureAnalogPeripheralPin(externalReferencePin))) {
    releaseComparator(ComparatorOwner::kComp);
    return false;
  }

  thresholdPermille = clampPermille(thresholdPermille);
  hysteresisPermille = clampPermille(hysteresisPermille);
  const uint16_t halfHysteresis = hysteresisPermille / 2U;
  uint16_t lowPermille =
      (thresholdPermille > halfHysteresis) ? (thresholdPermille - halfHysteresis) : 0U;
  uint16_t highPermille =
      thresholdPermille + static_cast<uint16_t>(hysteresisPermille - halfHysteresis);
  if (highPermille > 1000U) {
    highPermille = 1000U;
  }
  if (lowPermille > highPermille) {
    lowPermille = highPermille;
  }

  comp_->TASKS_STOP = COMP_TASKS_STOP_TASKS_STOP_Trigger;
  comp_->ENABLE = (COMP_ENABLE_ENABLE_Disabled << COMP_ENABLE_ENABLE_Pos);
  comp_->INTENCLR = 0xFFFFFFFFUL;
  comp_->SHORTS = 0U;
  clearEvents();

  comp_->PSEL = makeCompPinSelect(inputPin);
  comp_->REFSEL = ((static_cast<uint32_t>(reference) << COMP_REFSEL_REFSEL_Pos) &
                   COMP_REFSEL_REFSEL_Msk);
  comp_->EXTREFSEL = (reference == CompReference::kExternalAref)
                         ? makeCompPinSelect(externalReferencePin)
                         : 0U;
  comp_->TH = compThresholdRegValue(lowPermille, highPermille);
  comp_->MODE = ((static_cast<uint32_t>(speed) << COMP_MODE_SP_Pos) &
                 COMP_MODE_SP_Msk) |
                ((COMP_MODE_MAIN_SE << COMP_MODE_MAIN_Pos) &
                 COMP_MODE_MAIN_Msk);
  comp_->HYST = ((COMP_HYST_HYST_NoHyst << COMP_HYST_HYST_Pos) &
                 COMP_HYST_HYST_Msk);
  setCurrentSource(currentSource);

  comp_->ENABLE = ((COMP_ENABLE_ENABLE_Enabled << COMP_ENABLE_ENABLE_Pos) &
                   COMP_ENABLE_ENABLE_Msk);
  clearEvents();
  comp_->TASKS_START = COMP_TASKS_START_TASKS_START_Trigger;
  if (!waitForCompReady(&comp_->EVENTS_READY, spinLimit)) {
    comp_->TASKS_STOP = COMP_TASKS_STOP_TASKS_STOP_Trigger;
    comp_->ENABLE = (COMP_ENABLE_ENABLE_Disabled << COMP_ENABLE_ENABLE_Pos);
    releaseComparator(ComparatorOwner::kComp);
    return false;
  }

  active_ = true;
  return true;
}

bool Comp::beginDifferential(const Pin& positivePin, const Pin& negativePin,
                             CompSpeedMode speed, bool hysteresis,
                             CompCurrentSource currentSource,
                             uint32_t spinLimit) {
  if (active_) {
    end();
  }

  if (!pinSupportsAnalogInput(positivePin) ||
      !pinSupportsAnalogInput(negativePin)) {
    return false;
  }
  if ((positivePin.port == negativePin.port) &&
      (positivePin.pin == negativePin.pin)) {
    return false;
  }
  if (!claimComparator(ComparatorOwner::kComp)) {
    return false;
  }
  if (!configureAnalogPeripheralPin(positivePin) ||
      !configureAnalogPeripheralPin(negativePin)) {
    releaseComparator(ComparatorOwner::kComp);
    return false;
  }

  comp_->TASKS_STOP = COMP_TASKS_STOP_TASKS_STOP_Trigger;
  comp_->ENABLE = (COMP_ENABLE_ENABLE_Disabled << COMP_ENABLE_ENABLE_Pos);
  comp_->INTENCLR = 0xFFFFFFFFUL;
  comp_->SHORTS = 0U;
  clearEvents();

  comp_->PSEL = makeCompPinSelect(positivePin);
  comp_->EXTREFSEL = makeCompPinSelect(negativePin);
  comp_->REFSEL = 0U;
  comp_->TH = compThresholdRegValue(500U, 500U);
  comp_->MODE = ((static_cast<uint32_t>(speed) << COMP_MODE_SP_Pos) &
                 COMP_MODE_SP_Msk) |
                ((COMP_MODE_MAIN_Diff << COMP_MODE_MAIN_Pos) &
                 COMP_MODE_MAIN_Msk);
  comp_->HYST = (((hysteresis ? COMP_HYST_HYST_Hyst40mV
                              : COMP_HYST_HYST_NoHyst)
                  << COMP_HYST_HYST_Pos) &
                 COMP_HYST_HYST_Msk);
  setCurrentSource(currentSource);

  comp_->ENABLE = ((COMP_ENABLE_ENABLE_Enabled << COMP_ENABLE_ENABLE_Pos) &
                   COMP_ENABLE_ENABLE_Msk);
  clearEvents();
  comp_->TASKS_START = COMP_TASKS_START_TASKS_START_Trigger;
  if (!waitForCompReady(&comp_->EVENTS_READY, spinLimit)) {
    comp_->TASKS_STOP = COMP_TASKS_STOP_TASKS_STOP_Trigger;
    comp_->ENABLE = (COMP_ENABLE_ENABLE_Disabled << COMP_ENABLE_ENABLE_Pos);
    releaseComparator(ComparatorOwner::kComp);
    return false;
  }

  active_ = true;
  return true;
}

bool Comp::setThresholdWindowPermille(uint16_t lowPermille,
                                      uint16_t highPermille) {
  lowPermille = clampPermille(lowPermille);
  highPermille = clampPermille(highPermille);
  if (lowPermille > highPermille) {
    return false;
  }
  comp_->TH = compThresholdRegValue(lowPermille, highPermille);
  return true;
}

void Comp::setCurrentSource(CompCurrentSource currentSource) {
  comp_->ISOURCE = ((static_cast<uint32_t>(currentSource)
                     << COMP_ISOURCE_ISOURCE_Pos) &
                    COMP_ISOURCE_ISOURCE_Msk);
}

bool Comp::sample(uint32_t spinLimit) const {
  (void)spinLimit;
  if (!active_) {
    return false;
  }
  comp_->TASKS_SAMPLE = COMP_TASKS_SAMPLE_TASKS_SAMPLE_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  return true;
}

bool Comp::resultAbove() const {
  return (comp_->RESULT & COMP_RESULT_RESULT_Msk) != 0U;
}

bool Comp::pollReady(bool clearEventFlag) {
  const bool fired = (comp_->EVENTS_READY != 0U);
  if (fired && clearEventFlag) {
    comp_->EVENTS_READY = 0U;
  }
  return fired;
}

bool Comp::pollUp(bool clearEventFlag) {
  const bool fired = (comp_->EVENTS_UP != 0U);
  if (fired && clearEventFlag) {
    comp_->EVENTS_UP = 0U;
  }
  return fired;
}

bool Comp::pollDown(bool clearEventFlag) {
  const bool fired = (comp_->EVENTS_DOWN != 0U);
  if (fired && clearEventFlag) {
    comp_->EVENTS_DOWN = 0U;
  }
  return fired;
}

bool Comp::pollCross(bool clearEventFlag) {
  const bool fired = (comp_->EVENTS_CROSS != 0U);
  if (fired && clearEventFlag) {
    comp_->EVENTS_CROSS = 0U;
  }
  return fired;
}

void Comp::clearEvents() {
  comp_->EVENTS_READY = 0U;
  comp_->EVENTS_UP = 0U;
  comp_->EVENTS_DOWN = 0U;
  comp_->EVENTS_CROSS = 0U;
}

void Comp::end() {
  comp_->SHORTS = 0U;
  comp_->INTENCLR = 0xFFFFFFFFUL;
  clearEvents();
  comp_->TASKS_STOP = COMP_TASKS_STOP_TASKS_STOP_Trigger;
  comp_->ENABLE = ((COMP_ENABLE_ENABLE_Disabled << COMP_ENABLE_ENABLE_Pos) &
                   COMP_ENABLE_ENABLE_Msk);
  active_ = false;
  releaseComparator(ComparatorOwner::kComp);
}

Lpcomp::Lpcomp(uint32_t base)
    : lpcomp_(reinterpret_cast<NRF_LPCOMP_Type*>(static_cast<uintptr_t>(base))),
      active_(false) {}

bool Lpcomp::begin(const Pin& inputPin, LpcompReference reference, bool hysteresis,
                   LpcompDetect detect, const Pin& externalReferencePin,
                   uint32_t spinLimit) {
  if (active_) {
    end();
  }

  if (!pinSupportsAnalogInput(inputPin)) {
    return false;
  }
  if (reference == LpcompReference::kExternalAref &&
      !pinSupportsAnalogInput(externalReferencePin)) {
    return false;
  }
  if (!claimComparator(ComparatorOwner::kLpcomp)) {
    return false;
  }
  if (!configureAnalogPeripheralPin(inputPin) ||
      (reference == LpcompReference::kExternalAref &&
       !configureAnalogPeripheralPin(externalReferencePin))) {
    releaseComparator(ComparatorOwner::kLpcomp);
    return false;
  }

  lpcomp_->TASKS_STOP = LPCOMP_TASKS_STOP_TASKS_STOP_Trigger;
  lpcomp_->ENABLE = ((LPCOMP_ENABLE_ENABLE_Disabled << LPCOMP_ENABLE_ENABLE_Pos) &
                     LPCOMP_ENABLE_ENABLE_Msk);
  lpcomp_->INTENCLR = 0xFFFFFFFFUL;
  lpcomp_->SHORTS = 0U;
  clearEvents();

  lpcomp_->PSEL = makeLpcompPinSelect(inputPin);
  lpcomp_->REFSEL = ((static_cast<uint32_t>(reference) << LPCOMP_REFSEL_REFSEL_Pos) &
                     LPCOMP_REFSEL_REFSEL_Msk);
  lpcomp_->EXTREFSEL = (reference == LpcompReference::kExternalAref)
                           ? makeLpcompPinSelect(externalReferencePin)
                           : 0U;
  configureAnalogDetect(detect);
  lpcomp_->HYST = (((hysteresis ? LPCOMP_HYST_HYST_Enabled
                                : LPCOMP_HYST_HYST_Disabled)
                    << LPCOMP_HYST_HYST_Pos) &
                   LPCOMP_HYST_HYST_Msk);

  lpcomp_->ENABLE = ((LPCOMP_ENABLE_ENABLE_Enabled << LPCOMP_ENABLE_ENABLE_Pos) &
                     LPCOMP_ENABLE_ENABLE_Msk);
  clearEvents();
  lpcomp_->TASKS_START = LPCOMP_TASKS_START_TASKS_START_Trigger;
  if (!waitForCompReady(&lpcomp_->EVENTS_READY, spinLimit)) {
    lpcomp_->TASKS_STOP = LPCOMP_TASKS_STOP_TASKS_STOP_Trigger;
    lpcomp_->ENABLE =
        ((LPCOMP_ENABLE_ENABLE_Disabled << LPCOMP_ENABLE_ENABLE_Pos) &
         LPCOMP_ENABLE_ENABLE_Msk);
    releaseComparator(ComparatorOwner::kLpcomp);
    return false;
  }

  active_ = true;
  return true;
}

bool Lpcomp::beginThreshold(const Pin& inputPin, uint16_t thresholdPermille,
                            bool hysteresis, LpcompDetect detect,
                            const Pin& externalReferencePin,
                            uint32_t spinLimit) {
  thresholdPermille = clampPermille(thresholdPermille);
  if (isConnected(externalReferencePin)) {
    if (thresholdPermille != 1000U) {
      return false;
    }
    return begin(inputPin, LpcompReference::kExternalAref, hysteresis, detect,
                 externalReferencePin, spinLimit);
  }
  return begin(inputPin, nearestLpcompReference(thresholdPermille), hysteresis,
               detect, kPinDisconnected, spinLimit);
}

bool Lpcomp::beginThresholdMv(const Pin& inputPin, uint16_t vddMv,
                               uint16_t thresholdMv, bool hysteresis,
                               LpcompDetect detect, uint32_t spinLimit) {
  if (vddMv == 0U) {
    return false;
  }
  // Round-to-nearest: add vddMv/2 before integer division.
  const uint32_t permille =
      (static_cast<uint32_t>(thresholdMv) * 1000U + vddMv / 2U) / vddMv;
  return beginThreshold(
      inputPin,
      static_cast<uint16_t>(permille > 1000U ? 1000U : permille),
      hysteresis, detect, kPinDisconnected, spinLimit);
}

void Lpcomp::configureAnalogDetect(LpcompDetect detect) {
  lpcomp_->ANADETECT =
      ((static_cast<uint32_t>(detect) << LPCOMP_ANADETECT_ANADETECT_Pos) &
       LPCOMP_ANADETECT_ANADETECT_Msk);
}

bool Lpcomp::sample(uint32_t spinLimit) const {
  (void)spinLimit;
  if (!active_) {
    return false;
  }
  lpcomp_->TASKS_SAMPLE = LPCOMP_TASKS_SAMPLE_TASKS_SAMPLE_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  return true;
}

bool Lpcomp::resultAbove() const {
  return (lpcomp_->RESULT & LPCOMP_RESULT_RESULT_Msk) != 0U;
}

bool Lpcomp::pollReady(bool clearEventFlag) {
  const bool fired = (lpcomp_->EVENTS_READY != 0U);
  if (fired && clearEventFlag) {
    lpcomp_->EVENTS_READY = 0U;
  }
  return fired;
}

bool Lpcomp::pollUp(bool clearEventFlag) {
  const bool fired = (lpcomp_->EVENTS_UP != 0U);
  if (fired && clearEventFlag) {
    lpcomp_->EVENTS_UP = 0U;
  }
  return fired;
}

bool Lpcomp::pollDown(bool clearEventFlag) {
  const bool fired = (lpcomp_->EVENTS_DOWN != 0U);
  if (fired && clearEventFlag) {
    lpcomp_->EVENTS_DOWN = 0U;
  }
  return fired;
}

bool Lpcomp::pollCross(bool clearEventFlag) {
  const bool fired = (lpcomp_->EVENTS_CROSS != 0U);
  if (fired && clearEventFlag) {
    lpcomp_->EVENTS_CROSS = 0U;
  }
  return fired;
}

void Lpcomp::clearEvents() {
  lpcomp_->EVENTS_READY = 0U;
  lpcomp_->EVENTS_UP = 0U;
  lpcomp_->EVENTS_DOWN = 0U;
  lpcomp_->EVENTS_CROSS = 0U;
}

void Lpcomp::end() {
  lpcomp_->SHORTS = 0U;
  lpcomp_->INTENCLR = 0xFFFFFFFFUL;
  clearEvents();
  lpcomp_->TASKS_STOP = LPCOMP_TASKS_STOP_TASKS_STOP_Trigger;
  lpcomp_->ENABLE =
      ((LPCOMP_ENABLE_ENABLE_Disabled << LPCOMP_ENABLE_ENABLE_Pos) &
       LPCOMP_ENABLE_ENABLE_Msk);
  active_ = false;
  releaseComparator(ComparatorOwner::kLpcomp);
}

Qdec::Qdec(uint32_t base)
    : qdec_(reinterpret_cast<NRF_QDEC_Type*>(static_cast<uintptr_t>(base))),
      configured_(false) {}

bool Qdec::begin(const Pin& pinA, const Pin& pinB, QdecSamplePeriod samplePeriod,
                 QdecReportPeriod reportPeriod, bool debounce,
                 QdecInputPull inputPull, const Pin& ledPin,
                 QdecLedPolarity ledPolarity, uint16_t ledPreUs) {
  if (configured_) {
    end();
  }

  if (qdec_ == nullptr || !isConnected(pinA) || !isConnected(pinB) ||
      !isQdecPortSupported(pinA) || !isQdecPortSupported(pinB) ||
      (isConnected(ledPin) && !isQdecPortSupported(ledPin))) {
    return false;
  }
  if ((pinA.port == pinB.port) && (pinA.pin == pinB.pin)) {
    return false;
  }

  const GpioPull pull = gpioPullFromQdecInputPull(inputPull);
  if (!Gpio::configure(pinA, GpioDirection::kInput, pull) ||
      !Gpio::configure(pinB, GpioDirection::kInput, pull)) {
    return false;
  }
  if (isConnected(ledPin) &&
      !Gpio::configure(ledPin, GpioDirection::kOutput, GpioPull::kDisabled)) {
    return false;
  }

  if (ledPreUs < QDEC_LEDPRE_LEDPRE_Min) {
    ledPreUs = QDEC_LEDPRE_LEDPRE_Min;
  }
  if (ledPreUs > QDEC_LEDPRE_LEDPRE_Max) {
    ledPreUs = QDEC_LEDPRE_LEDPRE_Max;
  }

  qdec_->TASKS_STOP = QDEC_TASKS_STOP_TASKS_STOP_Trigger;
  qdec_->ENABLE = QDEC_ENABLE_ENABLE_Disabled;
  qdec_->INTENCLR = 0xFFFFFFFFUL;
  qdec_->SHORTS = 0U;
  qdec_->EVENTS_SAMPLERDY = 0U;
  qdec_->EVENTS_REPORTRDY = 0U;
  qdec_->EVENTS_ACCOF = 0U;
  qdec_->EVENTS_DBLRDY = 0U;
  qdec_->EVENTS_STOPPED = 0U;

  qdec_->PSEL.A = makeQdecConnectedPinSelect(pinA);
  qdec_->PSEL.B = makeQdecConnectedPinSelect(pinB);
  qdec_->PSEL.LED =
      isConnected(ledPin) ? makeQdecConnectedPinSelect(ledPin) : PSEL_DISCONNECTED;
  qdec_->LEDPOL = ((static_cast<uint32_t>(ledPolarity) << QDEC_LEDPOL_LEDPOL_Pos) &
                   QDEC_LEDPOL_LEDPOL_Msk);
  qdec_->SAMPLEPER =
      ((static_cast<uint32_t>(samplePeriod) << QDEC_SAMPLEPER_SAMPLEPER_Pos) &
       QDEC_SAMPLEPER_SAMPLEPER_Msk);
  qdec_->REPORTPER =
      ((static_cast<uint32_t>(reportPeriod) << QDEC_REPORTPER_REPORTPER_Pos) &
       QDEC_REPORTPER_REPORTPER_Msk);
  qdec_->DBFEN = (((debounce ? QDEC_DBFEN_DBFEN_Enabled
                             : QDEC_DBFEN_DBFEN_Disabled)
                   << QDEC_DBFEN_DBFEN_Pos) &
                  QDEC_DBFEN_DBFEN_Msk);
  qdec_->LEDPRE = ((static_cast<uint32_t>(ledPreUs) << QDEC_LEDPRE_LEDPRE_Pos) &
                   QDEC_LEDPRE_LEDPRE_Msk);

  qdec_->ENABLE = ((QDEC_ENABLE_ENABLE_Enabled << QDEC_ENABLE_ENABLE_Pos) &
                   QDEC_ENABLE_ENABLE_Msk);
  configured_ = true;
  return true;
}

void Qdec::end() {
  qdec_->TASKS_STOP = QDEC_TASKS_STOP_TASKS_STOP_Trigger;
  qdec_->ENABLE = QDEC_ENABLE_ENABLE_Disabled;
  qdec_->PSEL.A = PSEL_DISCONNECTED;
  qdec_->PSEL.B = PSEL_DISCONNECTED;
  qdec_->PSEL.LED = PSEL_DISCONNECTED;
  qdec_->EVENTS_SAMPLERDY = 0U;
  qdec_->EVENTS_REPORTRDY = 0U;
  qdec_->EVENTS_ACCOF = 0U;
  qdec_->EVENTS_DBLRDY = 0U;
  qdec_->EVENTS_STOPPED = 0U;
  configured_ = false;
}

void Qdec::start() {
  if (configured_) {
    qdec_->TASKS_START = QDEC_TASKS_START_TASKS_START_Trigger;
  }
}

void Qdec::stop() {
  if (configured_) {
    qdec_->TASKS_STOP = QDEC_TASKS_STOP_TASKS_STOP_Trigger;
  }
}

int32_t Qdec::sampleValue() const { return qdec_->SAMPLE; }

int32_t Qdec::accumulator() const { return qdec_->ACC; }

int32_t Qdec::readAndClearAccumulator() {
  if (!configured_) {
    return 0;
  }
  qdec_->TASKS_RDCLRACC = QDEC_TASKS_RDCLRACC_TASKS_RDCLRACC_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  return qdec_->ACCREAD;
}

uint32_t Qdec::doubleTransitions() const { return qdec_->ACCDBL; }

uint32_t Qdec::readAndClearDoubleTransitions() {
  if (!configured_) {
    return 0U;
  }
  qdec_->TASKS_RDCLRDBL = QDEC_TASKS_RDCLRDBL_TASKS_RDCLRDBL_Trigger;
  __asm volatile("dsb 0xF" ::: "memory");
  return qdec_->ACCDBLREAD;
}

bool Qdec::pollSampleReady(bool clearEventFlag) {
  const bool fired = (qdec_->EVENTS_SAMPLERDY != 0U);
  if (fired && clearEventFlag) {
    qdec_->EVENTS_SAMPLERDY = 0U;
  }
  return fired;
}

bool Qdec::pollReportReady(bool clearEventFlag) {
  const bool fired = (qdec_->EVENTS_REPORTRDY != 0U);
  if (fired && clearEventFlag) {
    qdec_->EVENTS_REPORTRDY = 0U;
  }
  return fired;
}

bool Qdec::pollOverflow(bool clearEventFlag) {
  const bool fired = (qdec_->EVENTS_ACCOF != 0U);
  if (fired && clearEventFlag) {
    qdec_->EVENTS_ACCOF = 0U;
  }
  return fired;
}

bool Qdec::pollDoubleReady(bool clearEventFlag) {
  const bool fired = (qdec_->EVENTS_DBLRDY != 0U);
  if (fired && clearEventFlag) {
    qdec_->EVENTS_DBLRDY = 0U;
  }
  return fired;
}

bool Qdec::pollStopped(bool clearEventFlag) {
  const bool fired = (qdec_->EVENTS_STOPPED != 0U);
  if (fired && clearEventFlag) {
    qdec_->EVENTS_STOPPED = 0U;
  }
  return fired;
}

Saadc::Saadc(uint32_t base)
    : base_(base),
      resolution_(AdcResolution::k12bit),
      gain_(AdcGain::k2over8),
      oversample_(AdcOversample::kBypass),
      differential_(false),
      configured_(false) {}

namespace {
void saadcDisconnectAllChannels(uint32_t base) {
  for (uint8_t channel = 0U; channel < 8U; ++channel) {
    const uint32_t offset = static_cast<uint32_t>(channel) * saadc::CH_STRIDE;
    reg32(base + saadc::CH_PSELP + offset) =
        (saadc::CH_PSEL_CONNECT_NC << saadc::CH_PSEL_CONNECT_Pos);
    reg32(base + saadc::CH_PSELN + offset) =
        (saadc::CH_PSEL_CONNECT_NC << saadc::CH_PSEL_CONNECT_Pos);
    reg32(base + saadc::CH_CONFIG + offset) = 0U;
#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
    reg32(base + saadc::CH_BURST + offset) = 0U;
#endif  	
  }
}
}  // namespace

bool Saadc::begin(AdcResolution resolution, uint32_t spinLimit) {
  return begin(resolution, AdcOversample::kBypass, spinLimit);
}

bool Saadc::begin(AdcResolution resolution, AdcOversample oversample,
                  uint32_t spinLimit) {
  resolution_ = resolution;
  oversample_ = oversample;
  differential_ = false;
  configured_ = false;

  reg32(base_ + saadc::ENABLE) = saadc::ENABLE_DISABLED;
  saadcDisconnectAllChannels(base_);

  reg32(base_ + saadc::RESOLUTION) = static_cast<uint32_t>(resolution_);
  reg32(base_ + saadc::OVERSAMPLE) = static_cast<uint32_t>(oversample_);
  reg32(base_ + saadc::SAMPLERATE) =
      (saadc::SAMPLERATE_MODE_TASK << saadc::SAMPLERATE_MODE_Pos);
  reg32(base_ + saadc::NOISESHAPE) = saadc::NOISESHAPE_DISABLED;

  reg32(base_ + saadc::ENABLE) = saadc::ENABLE_ENABLED;

  return calibrate(spinLimit);
}

bool Saadc::calibrate(uint32_t spinLimit) {
  clearEvent(base_, saadc::EVENTS_STARTED);
  clearEvent(base_, saadc::EVENTS_END);
  clearEvent(base_, saadc::EVENTS_STOPPED);

  clearEvent(base_, saadc::EVENTS_CALIBRATEDONE);
  reg32(base_ + saadc::TASKS_CALIBRATEOFFSET) = 1;

  return waitForEvent(base_, saadc::EVENTS_CALIBRATEDONE, spinLimit);
}

void Saadc::end() {
  clearEvent(base_, saadc::EVENTS_STOPPED);
  reg32(base_ + saadc::TASKS_STOP) = 1;
  (void)waitForEvent(base_, saadc::EVENTS_STOPPED, 300000UL);
  reg32(base_ + saadc::ENABLE) = saadc::ENABLE_DISABLED;
  saadcDisconnectAllChannels(base_);
  differential_ = false;
  configured_ = false;
}

bool Saadc::configureSingleEnded(uint8_t channel, const Pin& pin, AdcGain gain,
                                 uint16_t tacq, uint8_t tconv, bool burst) {
  if (channel > 7U || !isConnected(pin) || saadcInputForPin(pin) < 0) {
    return false;
  }

  if (tacq < 1U) {
    tacq = 1U;
  }
  if (tacq > 319U) {
    tacq = 319U;
  }
  if (tconv < 1U) {
    tconv = 1U;
  }
  if (tconv > 7U) {
    tconv = 7U;
  }

  // Keep this HAL simple and deterministic: one active channel at a time.
  saadcDisconnectAllChannels(base_);

  reg32(base_ + saadc::CH_PSELP +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) =
      saadcPselValue(pin);
  reg32(base_ + saadc::CH_PSELN +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) =
      (saadc::CH_PSEL_CONNECT_NC << saadc::CH_PSEL_CONNECT_Pos);

  uint32_t config = 0;
  config |= (static_cast<uint32_t>(gain) << saadc::CH_CONFIG_GAIN_Pos);
#if !(defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA))
  config |= ((burst ? saadc::CH_CONFIG_BURST_ENABLED
                    : saadc::CH_CONFIG_BURST_DISABLED)
             << saadc::CH_CONFIG_BURST_Pos);
#endif			 
  config |= (saadc::REFSEL_INTERNAL << saadc::CH_CONFIG_REFSEL_Pos);
  config |= (saadc::MODE_SINGLE_ENDED << saadc::CH_CONFIG_MODE_Pos);
  config |= (static_cast<uint32_t>(tacq) << saadc::CH_CONFIG_TACQ_Pos);
  config |= (static_cast<uint32_t>(tconv) << saadc::CH_CONFIG_TCONV_Pos);

  reg32(base_ + saadc::CH_CONFIG +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) = config;
		
#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
  config = 0;
  config |= ((burst ? saadc::CH_CONFIG_BURST_ENABLED
                    : saadc::CH_CONFIG_BURST_DISABLED)
             << saadc::CH_CONFIG_BURST_Pos);  
			 
  reg32(base_ + saadc::CH_BURST +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) = config;
#endif  

  gain_ = gain;
  differential_ = false;
  configured_ = true;
  return true;
}

bool Saadc::configureSingleEnded(uint8_t channel, AdcInternalInput input,
                                 AdcGain gain, uint16_t tacq, uint8_t tconv,
                                 bool burst) {
  if (channel > 7U) {
    return false;
  }

  if (tacq < 1U) {
    tacq = 1U;
  }
  if (tacq > 319U) {
    tacq = 319U;
  }
  if (tconv < 1U) {
    tconv = 1U;
  }
  if (tconv > 7U) {
    tconv = 7U;
  }

  // Keep this HAL simple and deterministic: one active channel at a time.
  saadcDisconnectAllChannels(base_);

  reg32(base_ + saadc::CH_PSELP +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) =
      (static_cast<uint32_t>(input) << saadc::CH_PSEL_INTERNAL_Pos) |
      (saadc::CH_PSEL_CONNECT_INTERNAL << saadc::CH_PSEL_CONNECT_Pos);
  reg32(base_ + saadc::CH_PSELN +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) =
      (saadc::CH_PSEL_CONNECT_NC << saadc::CH_PSEL_CONNECT_Pos);

  uint32_t config = 0;
  config |= (static_cast<uint32_t>(gain) << saadc::CH_CONFIG_GAIN_Pos);
#if !(defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA))
  config |= ((burst ? saadc::CH_CONFIG_BURST_ENABLED
                    : saadc::CH_CONFIG_BURST_DISABLED)
             << saadc::CH_CONFIG_BURST_Pos);
#endif			 
  config |= (saadc::REFSEL_INTERNAL << saadc::CH_CONFIG_REFSEL_Pos);
  config |= (saadc::MODE_SINGLE_ENDED << saadc::CH_CONFIG_MODE_Pos);
  config |= (static_cast<uint32_t>(tacq) << saadc::CH_CONFIG_TACQ_Pos);
  config |= (static_cast<uint32_t>(tconv) << saadc::CH_CONFIG_TCONV_Pos);

  reg32(base_ + saadc::CH_CONFIG +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) = config;

#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
  config = 0;
  config |= ((burst ? saadc::CH_CONFIG_BURST_ENABLED
                    : saadc::CH_CONFIG_BURST_DISABLED)
             << saadc::CH_CONFIG_BURST_Pos);  
			 
  reg32(base_ + saadc::CH_BURST +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) = config;
#endif  

  gain_ = gain;
  differential_ = false;
  configured_ = true;
  return true;
}

bool Saadc::configureDifferential(uint8_t channel, const Pin& positivePin,
                                  const Pin& negativePin, AdcGain gain,
                                  uint16_t tacq, uint8_t tconv, bool burst) {
  if (channel > 7U || !isConnected(positivePin) ||
      !isConnected(negativePin) || saadcInputForPin(positivePin) < 0 ||
      saadcInputForPin(negativePin) < 0 ||
      (positivePin.port == negativePin.port &&
       positivePin.pin == negativePin.pin)) {
    return false;
  }

  if (tacq < 1U) {
    tacq = 1U;
  }
  if (tacq > 319U) {
    tacq = 319U;
  }
  if (tconv < 1U) {
    tconv = 1U;
  }
  if (tconv > 7U) {
    tconv = 7U;
  }

  saadcDisconnectAllChannels(base_);

  reg32(base_ + saadc::CH_PSELP +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) =
      saadcPselValue(positivePin);
  reg32(base_ + saadc::CH_PSELN +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) =
      saadcPselValue(negativePin);

  uint32_t config = 0;
  config |= (static_cast<uint32_t>(gain) << saadc::CH_CONFIG_GAIN_Pos);
#if !(defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA))
  config |= ((burst ? saadc::CH_CONFIG_BURST_ENABLED
                    : saadc::CH_CONFIG_BURST_DISABLED)
             << saadc::CH_CONFIG_BURST_Pos);
#endif			 
  config |= (saadc::REFSEL_INTERNAL << saadc::CH_CONFIG_REFSEL_Pos);
  config |= (saadc::MODE_DIFFERENTIAL << saadc::CH_CONFIG_MODE_Pos);
  config |= (static_cast<uint32_t>(tacq) << saadc::CH_CONFIG_TACQ_Pos);
  config |= (static_cast<uint32_t>(tconv) << saadc::CH_CONFIG_TCONV_Pos);

  reg32(base_ + saadc::CH_CONFIG +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) = config;

#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
  config = 0;
  config |= ((burst ? saadc::CH_CONFIG_BURST_ENABLED
                    : saadc::CH_CONFIG_BURST_DISABLED)
             << saadc::CH_CONFIG_BURST_Pos);  
			 
  reg32(base_ + saadc::CH_BURST +
        (static_cast<uint32_t>(channel) * saadc::CH_STRIDE)) = config;
#endif  

  gain_ = gain;
  differential_ = true;
  configured_ = true;
  return true;
}

bool Saadc::sampleRaw(int16_t* outRaw, uint32_t spinLimit) const {
  if (!configured_ || base_ == 0U || outRaw == nullptr) {
    return false;
  }

  // Keep the DMA target alive even if STOPPED never arrives. A stack target
  // would become a use-after-return destination on a timeout path.
  g_saadcSampleScratch = 0;

  clearEvent(base_, saadc::EVENTS_STARTED);
  clearEvent(base_, saadc::EVENTS_END);
  clearEvent(base_, saadc::EVENTS_STOPPED);

  reg32(base_ + saadc::RESULT_PTR) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_saadcSampleScratch));
  // On nRF54 SAADC this counter is in bytes.
  reg32(base_ + saadc::RESULT_MAXCNT) = sizeof(g_saadcSampleScratch);

  reg32(base_ + saadc::TASKS_START) = 1;
  const bool started = waitForEvent(base_, saadc::EVENTS_STARTED, spinLimit);
  bool ended = false;
  if (started) {
    reg32(base_ + saadc::TASKS_SAMPLE) = 1;
    ended = waitForEvent(base_, saadc::EVENTS_END, spinLimit);
  }

  reg32(base_ + saadc::TASKS_STOP) = 1;
  const bool stopped = waitForEvent(base_, saadc::EVENTS_STOPPED, spinLimit);
  if (stopped) {
    reg32(base_ + saadc::RESULT_MAXCNT) = 0U;
    reg32(base_ + saadc::RESULT_PTR) = 0U;
  }

  if (!started || !ended || !stopped ||
      reg32(base_ + saadc::RESULT_AMOUNT) < sizeof(g_saadcSampleScratch)) {
    return false;
  }

  *outRaw = g_saadcSampleScratch;
  return true;
}

bool Saadc::sampleMilliVolts(int32_t* outMilliVolts, uint32_t spinLimit) const {
  if (outMilliVolts == nullptr) {
    return false;
  }

  int16_t raw = 0;
  if (!sampleRaw(&raw, spinLimit)) {
    return false;
  }

  if (!differential_ && raw < 0) {
    raw = 0;
  }

  *outMilliVolts = adcRawToMilliVolts(raw, resolution_, gain_, differential_);
  return true;
}

bool Saadc::sampleMilliVoltsSigned(int32_t* outMilliVolts,
                                   uint32_t spinLimit) const {
  if (outMilliVolts == nullptr) {
    return false;
  }

  int16_t raw = 0;
  if (!sampleRaw(&raw, spinLimit)) {
    return false;
  }

  *outMilliVolts = adcRawToMilliVolts(raw, resolution_, gain_, differential_);
  return true;
}

PowerManager::PowerManager(uint32_t powerBase, uint32_t resetBase,
                           uint32_t regulatorsBase)
    : power_(reinterpret_cast<NRF_POWER_Type*>(static_cast<uintptr_t>(powerBase))),
      reset_(reinterpret_cast<NRF_RESET_Type*>(static_cast<uintptr_t>(resetBase))),
      regulators_(reinterpret_cast<NRF_REGULATORS_Type*>(
          static_cast<uintptr_t>(regulatorsBase))),
      constantLatencyOwned_(false) {}

PowerManager::~PowerManager() {
  if (constantLatencyOwned_) {
    nrf54l15_constlat_release();
    constantLatencyOwned_ = false;
  }
}

void PowerManager::quarantineConstantLatencyLease() {
  constantLatencyOwned_ = false;
}

bool PowerManager::setLatencyMode(PowerLatencyMode mode) {
  if (mode == PowerLatencyMode::kConstantLatency) {
    if (constantLatencyOwned_) {
      return true;
    }
    if (nrf54l15_constlat_acquire() == 0U) {
      return false;
    }
    constantLatencyOwned_ = true;
  } else if (constantLatencyOwned_) {
    nrf54l15_constlat_release();
    constantLatencyOwned_ = false;
  }
  return true;
}

bool PowerManager::isConstantLatency() const {
  return (power_->CONSTLATSTAT & 0x1UL) != 0U;
}

bool PowerManager::setRetention(uint8_t index, uint8_t value) {
  if (index >= 2U) {
    return false;
  }
  power_->GPREGRET[index] = static_cast<uint32_t>(value);
  return true;
}

bool PowerManager::getRetention(uint8_t index, uint8_t* value) const {
  if (index >= 2U || value == nullptr) {
    return false;
  }
  *value = static_cast<uint8_t>(power_->GPREGRET[index] & POWER_GPREGRET_GPREGRET_Msk);
  return true;
}

uint32_t PowerManager::resetReason() const { return reset_->RESETREAS; }

void PowerManager::clearResetReason(uint32_t mask) { reset_->RESETREAS = mask; }

bool PowerManager::enableMainDcdc(bool enable) {
  regulators_->VREGMAIN.DCDCEN =
      enable ? REGULATORS_VREGMAIN_DCDCEN_VAL_Enabled
             : REGULATORS_VREGMAIN_DCDCEN_VAL_Disabled;
  const bool isEnabled = (regulators_->VREGMAIN.DCDCEN &
                          REGULATORS_VREGMAIN_DCDCEN_VAL_Msk) != 0U;
  return (isEnabled == enable);
}

bool PowerManager::prepareLowPowerIdle(bool collapseBoardLoads,
                                       bool enableIdleCpuScaling,
                                       bool stopHfxoIfIdle) {
  setLatencyMode(PowerLatencyMode::kLowPower);
  bool ok = enableMainDcdc(true);

  if (enableIdleCpuScaling) {
    ok = ClockControl::enableIdleCpuScaling(CpuFrequency::k64MHz) && ok;
  }

  if (stopHfxoIfIdle) {
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (nrf54l15_exclusive_radio_owner() ==
        Nrf54ExclusiveRadioOwner::kNone) {
      ClockControl::stopHfxo();
    } else {
      ok = false;
    }
    __set_PRIMASK(primask);
  }

  if (collapseBoardLoads) {
    if (BoardControl::hasSwitchedBatterySense()) {
      ok = BoardControl::setBatterySenseEnabled(false) && ok;
    }
    if (BoardControl::hasImuMicPowerGate()) {
      ok = BoardControl::setImuMicEnabled(false) && ok;
    }
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (nrf54l15_exclusive_radio_owner() ==
        Nrf54ExclusiveRadioOwner::kNone) {
      ok = BoardControl::collapseRfPathIdle() && ok;
    } else {
      ok = false;
    }
    __set_PRIMASK(primask);
  }

  return ok;
}

bool PowerManager::configurePowerFailComparator(PowerFailThreshold threshold,
                                                bool enableWarningEvent) {
  uint32_t config = regulators_->POFCON;
  config &= ~(REGULATORS_POFCON_POF_Msk | REGULATORS_POFCON_THRESHOLD_Msk |
              REGULATORS_POFCON_EVENTDISABLE_Msk);
  config |= (REGULATORS_POFCON_POF_Enabled << REGULATORS_POFCON_POF_Pos) &
            REGULATORS_POFCON_POF_Msk;
  config |= ((static_cast<uint32_t>(threshold)
              << REGULATORS_POFCON_THRESHOLD_Pos) &
             REGULATORS_POFCON_THRESHOLD_Msk);
  config |= (((enableWarningEvent ? REGULATORS_POFCON_EVENTDISABLE_Enabled
                                  : REGULATORS_POFCON_EVENTDISABLE_Disabled)
              << REGULATORS_POFCON_EVENTDISABLE_Pos) &
             REGULATORS_POFCON_EVENTDISABLE_Msk);
  regulators_->POFCON = config;
  power_->EVENTS_POFWARN = POWER_EVENTS_POFWARN_EVENTS_POFWARN_NotGenerated;
  return powerFailComparatorEnabled() &&
         (powerFailThreshold() == threshold) &&
         (powerFailWarningEventEnabled() == enableWarningEvent);
}

void PowerManager::disablePowerFailComparator() {
  regulators_->POFCON &= ~REGULATORS_POFCON_POF_Msk;
  clearPowerFailWarning();
}

bool PowerManager::powerFailComparatorEnabled() const {
  return (regulators_->POFCON & REGULATORS_POFCON_POF_Msk) != 0U;
}

PowerFailThreshold PowerManager::powerFailThreshold() const {
  return static_cast<PowerFailThreshold>(
      (regulators_->POFCON & REGULATORS_POFCON_THRESHOLD_Msk) >>
      REGULATORS_POFCON_THRESHOLD_Pos);
}

bool PowerManager::powerBelowPowerFailThreshold() const {
  return (regulators_->POFSTAT & REGULATORS_POFSTAT_COMPARATOR_Msk) != 0U;
}

bool PowerManager::powerFailWarningEventEnabled() const {
  return (regulators_->POFCON & REGULATORS_POFCON_EVENTDISABLE_Msk) == 0U;
}

bool PowerManager::pollPowerFailWarning(bool clearEvent) {
  const bool fired =
      (power_->EVENTS_POFWARN & POWER_EVENTS_POFWARN_EVENTS_POFWARN_Msk) != 0U;
  if (fired && clearEvent) {
    clearPowerFailWarning();
  }
  return fired;
}

void PowerManager::clearPowerFailWarning() {
  power_->EVENTS_POFWARN = POWER_EVENTS_POFWARN_EVENTS_POFWARN_NotGenerated;
}

TempSensor::TempSensor(uint32_t base)
    : temp_(reinterpret_cast<NRF_TEMP_Type*>(static_cast<uintptr_t>(base))) {}

bool TempSensor::sampleQuarterDegreesC(int32_t* outQuarterDegreesC,
                                       uint32_t spinLimit) const {
  if (outQuarterDegreesC == nullptr) {
    return false;
  }

  temp_->EVENTS_DATARDY = 0U;
  temp_->TASKS_START = TEMP_TASKS_START_TASKS_START_Trigger;
  const bool ready = waitForNonZero(&temp_->EVENTS_DATARDY, spinLimit);
  temp_->TASKS_STOP = TEMP_TASKS_STOP_TASKS_STOP_Trigger;
  if (!ready) {
    return false;
  }

  *outQuarterDegreesC = temp_->TEMP;
  temp_->EVENTS_DATARDY = 0U;
  return true;
}

bool TempSensor::sampleMilliDegreesC(int32_t* outMilliDegreesC,
                                     uint32_t spinLimit) const {
  if (outMilliDegreesC == nullptr) {
    return false;
  }

  int32_t quarter = 0;
  if (!sampleQuarterDegreesC(&quarter, spinLimit)) {
    return false;
  }

  *outMilliDegreesC = quarter * 250;
  return true;
}

Watchdog::Watchdog(uint32_t base)
    : wdt_(reinterpret_cast<NRF_WDT_Type*>(static_cast<uintptr_t>(base))),
      defaultReloadRegister_(0),
      allowStop_(false) {}

bool Watchdog::configure(uint32_t timeoutMs, uint8_t reloadRegister, bool runInSleep,
                         bool runInDebugHalt, bool allowStop) {
  if (wdt_ == nullptr || reloadRegister > 7U || timeoutMs == 0U) {
    return false;
  }
  if (isRunning()) {
    return false;
  }

  const uint64_t requestedTicks =
      (static_cast<uint64_t>(timeoutMs) * 32768ULL + 999ULL) / 1000ULL;
  // Timeout is (CRV + 1) / 32768 seconds.
  uint64_t crv = requestedTicks - 1ULL;
  if (crv < WDT_CRV_CRV_Min) {
    crv = WDT_CRV_CRV_Min;
  }
  if (crv > WDT_CRV_CRV_Max) {
    crv = WDT_CRV_CRV_Max;
  }

  uint32_t cfg = 0;
  cfg |= ((runInSleep ? WDT_CONFIG_SLEEP_Run : WDT_CONFIG_SLEEP_Pause)
          << WDT_CONFIG_SLEEP_Pos);
  cfg |= ((runInDebugHalt ? WDT_CONFIG_HALT_Run : WDT_CONFIG_HALT_Pause)
          << WDT_CONFIG_HALT_Pos);
  cfg |= ((allowStop ? WDT_CONFIG_STOPEN_Enable : WDT_CONFIG_STOPEN_Disable)
          << WDT_CONFIG_STOPEN_Pos);

  wdt_->CRV = static_cast<uint32_t>(crv);
  wdt_->RREN = (1UL << reloadRegister);
  wdt_->CONFIG = cfg;
  wdt_->TSEN = allowStop ? WDT_TSEN_TSEN_Enable : 0U;

  defaultReloadRegister_ = reloadRegister;
  allowStop_ = allowStop;
  return true;
}

void Watchdog::start() { wdt_->TASKS_START = WDT_TASKS_START_TASKS_START_Trigger; }

bool Watchdog::stop(uint32_t spinLimit) {
  if (!allowStop_) {
    return false;
  }

  wdt_->EVENTS_STOPPED = 0U;
  wdt_->TASKS_STOP = WDT_TASKS_STOP_TASKS_STOP_Trigger;
  if (!waitForNonZero(&wdt_->EVENTS_STOPPED, spinLimit)) {
    return false;
  }
  wdt_->TSEN = 0U;
  allowStop_ = false;
  return true;
}

bool Watchdog::feed(uint8_t reloadRegister) {
  if (reloadRegister > 7U) {
    reloadRegister = defaultReloadRegister_;
  }
  wdt_->RR[reloadRegister] = WDT_RR_RR_Reload;
  return true;
}

bool Watchdog::isRunning() const {
  return (wdt_->RUNSTATUS & WDT_RUNSTATUS_RUNSTATUSWDT_Msk) != 0U;
}

uint32_t Watchdog::requestStatus() const { return wdt_->REQSTATUS; }

Pdm::Pdm(uint32_t base)
    : pdm_(reinterpret_cast<NRF_PDM_Type*>(static_cast<uintptr_t>(base))),
      configured_(false),
      clkPin_(kPinDisconnected),
      dinPin_(kPinDisconnected),
      modeConfig_(0U),
      ratioConfig_(PDM_RATIO_RATIO_Ratio80),
      prescalerConfig_(25U),
      sampleRateHz_(16000UL),
      streaming_(false),
      streamStarting_(false),
      streamActiveBuffer_(0U),
      streamSampleCount_(0U),
      streamBuffers_{nullptr, nullptr} {}

void Pdm::applyConfiguration() {
  pdm_->PSEL.CLK = make_psel(clkPin_.port, clkPin_.pin);
  pdm_->PSEL.DIN = make_psel(dinPin_.port, dinPin_.pin);
  pdm_->MODE = modeConfig_;
  pdm_->GAINL = PDM_GAINL_GAINL_DefaultGain;
  pdm_->GAINR = PDM_GAINR_GAINR_DefaultGain;
  pdm_->RATIO = ratioConfig_;
#if defined(NRF54LM20A_XXAA) || defined(NRF54LM20B_XXAA)
  reg32(reinterpret_cast<uintptr_t>(pdm_) + 0x54CUL) = 0U;
#endif
  pdm_->PRESCALER = prescalerConfig_;
  pdm_->DMA.TERMINATEONBUSERROR =
      PDM_DMA_TERMINATEONBUSERROR_ENABLE_Enabled;
  __DSB();
}

bool Pdm::begin(const Pin& clk, const Pin& din, bool mono, uint8_t prescalerDiv,
                uint8_t ratio, PdmEdge edge) {
  if (!isConnected(clk) || !isConnected(din) ||
      !isPdmPortSupported(clk) || !isPdmPortSupported(din) ||
      (clk.port == din.port && clk.pin == din.pin)) {
    return false;
  }
  if (prescalerDiv < PDM_PRESCALER_DIVISOR_Min ||
      prescalerDiv > PDM_PRESCALER_DIVISOR_Max ||
      ratio > PDM_RATIO_RATIO_Max ||
      static_cast<uint32_t>(edge) > PDM_MODE_EDGE_Max) {
    return false;
  }
  const uint16_t ratioDivisor = pdmRatioDivisor(ratio);
  if (ratioDivisor == 0U) {
    return false;
  }

  // Nordic's PDM setup requires CLK to idle low before output ownership is
  // handed to the peripheral.
  if (!Gpio::write(clk, false) ||
      !Gpio::configure(clk, GpioDirection::kOutput, GpioPull::kDisabled) ||
      !Gpio::configure(din, GpioDirection::kInput, GpioPull::kDisabled)) {
    return false;
  }

  if (configured_) {
    end();
  }

  pdm_->ENABLE = PDM_ENABLE_ENABLE_Disabled;
  __DSB();
  (void)pdm_->ENABLE;
  clearPdmEvent(&pdm_->EVENTS_STARTED);
  clearPdmEvent(&pdm_->EVENTS_END);
  clearPdmEvent(&pdm_->EVENTS_STOPPED);
  clearPdmEvent(&pdm_->EVENTS_DMA.BUSERROR);

  uint32_t mode = 0;
  mode |= ((mono ? PDM_MODE_OPERATION_Mono : PDM_MODE_OPERATION_Stereo)
           << PDM_MODE_OPERATION_Pos);
  mode |= ((static_cast<uint32_t>(edge) << PDM_MODE_EDGE_Pos) & PDM_MODE_EDGE_Msk);
  clkPin_ = clk;
  dinPin_ = din;
  modeConfig_ = mode;
  ratioConfig_ =
      ((static_cast<uint32_t>(ratio) << PDM_RATIO_RATIO_Pos) &
       PDM_RATIO_RATIO_Msk);
  prescalerConfig_ =
      (static_cast<uint32_t>(prescalerDiv) << PDM_PRESCALER_DIVISOR_Pos);
  sampleRateHz_ =
      32000000UL / static_cast<uint32_t>(prescalerDiv) / ratioDivisor;
  applyConfiguration();

  // Match nrfx: configuration does not power the PDM until a buffer is armed.
  __DSB();
  configured_ = true;
  return true;
}

void Pdm::end() {
  if (!configured_) {
    return;
  }

  (void)stopAndReleaseDma(100000UL);
  streaming_ = false;
  streamStarting_ = false;
  streamSampleCount_ = 0U;
  streamBuffers_[0] = nullptr;
  streamBuffers_[1] = nullptr;
  pdm_->ENABLE = PDM_ENABLE_ENABLE_Disabled;
  __DSB();
  (void)pdm_->ENABLE;
  __ISB();
  pdm_->SAMPLE.MAXCNT = 0U;
  pdm_->SAMPLE.PTR = 0U;
  __DSB();
  pdm_->PSEL.CLK = PSEL_DISCONNECTED;
  pdm_->PSEL.DIN = PSEL_DISCONNECTED;
  configured_ = false;
}

bool Pdm::stopAndReleaseDma(uint32_t timeoutUs) {
  bool stoppedBeforeDeadline = true;
  if (pdm_->ENABLE != PDM_ENABLE_ENABLE_Disabled) {
    clearPdmEvent(&pdm_->EVENTS_STOPPED);
    __DSB();
    pdm_->TASKS_STOP = PDM_TASKS_STOP_TASKS_STOP_Trigger;
    stoppedBeforeDeadline =
        waitForPdmEvent(&pdm_->EVENTS_STOPPED, nullptr, timeoutUs);
    if (!stoppedBeforeDeadline) {
      // STOPPED is the product-specification fence that proves EasyDMA has
      // released caller memory. A forced ENABLE/PSEL clear is not equivalent.
      // Preserve memory safety even if a hardware fault makes this wait
      // unbounded; the caller must never regain a still-owned stack buffer.
      while (pdm_->EVENTS_STOPPED == 0U) {
        __NOP();
      }
    }
  }

  // STOPPED guarantees that PDM and EasyDMA no longer access caller memory.
  pdm_->ENABLE = PDM_ENABLE_ENABLE_Disabled;
  __DSB();
  (void)pdm_->ENABLE;
  clearPdmEvent(&pdm_->EVENTS_STARTED);
  clearPdmEvent(&pdm_->EVENTS_END);
  clearPdmEvent(&pdm_->EVENTS_STOPPED);
  clearPdmEvent(&pdm_->EVENTS_DMA.BUSERROR);
  __DSB();
  return stoppedBeforeDeadline;
}

bool Pdm::capture(int16_t* samples, size_t sampleCount, uint32_t timeoutUs) {
  if (!configured_ || streaming_ || samples == nullptr ||
      sampleCount < kDrainSampleCount) {
    return false;
  }

  const uintptr_t ptr = reinterpret_cast<uintptr_t>(samples);
  if ((ptr & 0x3U) != 0U) {
    return false;
  }

  if (sampleCount >
      (PDM_SAMPLE_MAXCNT_BUFFSIZE_Max / sizeof(int16_t))) {
    return false;
  }
  const uint32_t dmaByteCount =
      static_cast<uint32_t>(sampleCount * sizeof(int16_t));
  const uint32_t captureTimeoutUs =
      (timeoutUs == 0U) ? pdmCaptureTimeoutUs(sampleCount, sampleRateHz_)
                        : timeoutUs;

  clearPdmEvent(&pdm_->EVENTS_STARTED);
  clearPdmEvent(&pdm_->EVENTS_END);
  clearPdmEvent(&pdm_->EVENTS_STOPPED);
  clearPdmEvent(&pdm_->EVENTS_DMA.BUSERROR);
  __DSB();

  pdm_->SAMPLE.PTR = static_cast<uint32_t>(ptr);
  pdm_->SAMPLE.MAXCNT = dmaByteCount;
  __DSB();

  // LM20 STOP resets internal PDM clock/filter latches while their register
  // image remains readable. Reapply the stored configuration before ENABLE.
  applyConfiguration();
  pdm_->ENABLE = PDM_ENABLE_ENABLE_Enabled;
  __DSB();
  (void)pdm_->ENABLE;
  pdm_->TASKS_START = PDM_TASKS_START_TASKS_START_Trigger;
  if (!waitForPdmEvent(&pdm_->EVENTS_STARTED, &pdm_->EVENTS_DMA.BUSERROR,
                       captureTimeoutUs)) {
    (void)stopAndReleaseDma(100000UL);
    Cache::invalidateForDma(samples, dmaByteCount);
    return false;
  }

  // SAMPLE is double-buffered. Queue an owned drain buffer as soon as the
  // caller's buffer is latched so END cannot advance into a zero-length or
  // stale bank before the polling code issues STOP.
  clearPdmEvent(&pdm_->EVENTS_STARTED);
  pdm_->SAMPLE.PTR =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(drainSamples_));
  pdm_->SAMPLE.MAXCNT =
      static_cast<uint32_t>(sizeof(drainSamples_));
  __DSB();

  const bool endSeen =
      waitForPdmEvent(&pdm_->EVENTS_END, &pdm_->EVENTS_DMA.BUSERROR,
                      captureTimeoutUs);

  const bool busError = (pdm_->EVENTS_DMA.BUSERROR != 0U);
  const bool stoppedBeforeDeadline = stopAndReleaseDma(100000UL);
  // stopAndReleaseDma never returns before STOPPED, including its late-fence
  // failure path. Discard any CPU cache lines populated before/during RX so
  // callers also observe coherent partial data after a false return.
  Cache::invalidateForDma(samples, dmaByteCount);

  return endSeen && stoppedBeforeDeadline && !busError;
}

bool Pdm::startStream(int16_t* firstBuffer, size_t sampleCount) {
  if (!configured_ || streaming_ || firstBuffer == nullptr ||
      sampleCount < kDrainSampleCount ||
      sampleCount > (PDM_SAMPLE_MAXCNT_BUFFSIZE_Max / sizeof(int16_t))) {
    return false;
  }
  const uintptr_t ptr = reinterpret_cast<uintptr_t>(firstBuffer);
  if ((ptr & 0x3U) != 0U) {
    return false;
  }

  clearPdmEvent(&pdm_->EVENTS_STARTED);
  clearPdmEvent(&pdm_->EVENTS_END);
  clearPdmEvent(&pdm_->EVENTS_STOPPED);
  clearPdmEvent(&pdm_->EVENTS_DMA.BUSERROR);
  streamBuffers_[0] = firstBuffer;
  streamBuffers_[1] = nullptr;
  streamActiveBuffer_ = 0U;
  streamSampleCount_ = sampleCount;
  streamStarting_ = true;
  streaming_ = true;

  pdm_->SAMPLE.PTR = static_cast<uint32_t>(ptr);
  pdm_->SAMPLE.MAXCNT =
      static_cast<uint32_t>(sampleCount * sizeof(int16_t));
  applyConfiguration();
  pdm_->ENABLE = PDM_ENABLE_ENABLE_Enabled;
  __DSB();
  (void)pdm_->ENABLE;
  pdm_->TASKS_START = PDM_TASKS_START_TASKS_START_Trigger;
  return true;
}

bool Pdm::queueStreamBuffer(int16_t* buffer) {
  if (!streaming_ || streamStarting_ || buffer == nullptr) {
    return false;
  }
  const uintptr_t ptr = reinterpret_cast<uintptr_t>(buffer);
  if ((ptr & 0x3U) != 0U) {
    return false;
  }
  if (buffer == streamBuffers_[streamActiveBuffer_]) {
    return false;
  }
  const uint8_t nextBuffer = static_cast<uint8_t>(streamActiveBuffer_ ^ 1U);
  if (streamBuffers_[nextBuffer] != nullptr) {
    return false;
  }

  streamBuffers_[nextBuffer] = buffer;
  pdm_->SAMPLE.PTR = static_cast<uint32_t>(ptr);
  pdm_->SAMPLE.MAXCNT =
      static_cast<uint32_t>(streamSampleCount_ * sizeof(int16_t));
  __DSB();
  return true;
}

bool Pdm::pollStream(StreamEvent* event) {
  if (event == nullptr) {
    return false;
  }
  event->releasedBuffer = nullptr;
  event->bufferRequested = false;
  event->overflow = false;
  event->busError = false;
  if (!streaming_) {
    return false;
  }

  if (pdm_->EVENTS_DMA.BUSERROR != 0U) {
    event->busError = true;
    (void)stopStream();
    return true;
  }
  if (pdm_->EVENTS_STARTED == 0U) {
    return false;
  }

  clearPdmEvent(&pdm_->EVENTS_STARTED);
  clearPdmEvent(&pdm_->EVENTS_END);
  if (streamStarting_) {
    streamStarting_ = false;
    event->bufferRequested = true;
    return true;
  }

  const uint8_t nextBuffer = static_cast<uint8_t>(streamActiveBuffer_ ^ 1U);
  if (streamBuffers_[nextBuffer] == nullptr) {
    event->overflow = true;
    (void)stopStream();
    return true;
  }

  event->releasedBuffer = streamBuffers_[streamActiveBuffer_];
  streamBuffers_[streamActiveBuffer_] = nullptr;
  streamActiveBuffer_ = nextBuffer;
  event->bufferRequested = true;
  Cache::invalidateForDma(
      event->releasedBuffer,
      static_cast<uint32_t>(streamSampleCount_ * sizeof(int16_t)));
  return true;
}

bool Pdm::stopStream(uint32_t timeoutUs) {
  if (!streaming_) {
    return true;
  }
  const bool stopped = stopAndReleaseDma(timeoutUs);
  const uint32_t byteCount =
      static_cast<uint32_t>(streamSampleCount_ * sizeof(int16_t));
  for (uint8_t i = 0U; i < 2U; ++i) {
    if (streamBuffers_[i] != nullptr) {
      Cache::invalidateForDma(streamBuffers_[i], byteCount);
    }
    streamBuffers_[i] = nullptr;
  }
  streaming_ = false;
  streamStarting_ = false;
  streamActiveBuffer_ = 0U;
  streamSampleCount_ = 0U;
  return stopped;
}

bool Pdm::isStreaming() const { return streaming_; }
