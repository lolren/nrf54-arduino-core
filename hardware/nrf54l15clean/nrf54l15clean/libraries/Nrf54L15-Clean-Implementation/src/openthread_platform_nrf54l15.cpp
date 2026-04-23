#include "openthread_platform_nrf54l15.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nrf54l15_hal.h"
#include <openthread/platform/alarm-micro.h>
#include <openthread/platform/alarm-milli.h>
#include <openthread/platform/crypto.h>
#include <openthread/platform/diag.h>
#include <openthread/platform/entropy.h>
#include <openthread/platform/logging.h>
#include <openthread/platform/memory.h>
#include <openthread/platform/settings.h>
#include <openthread/platform/radio.h>
#include <openthread/tasklet.h>
#include <openthread-system.h>

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
#include "common/as_core_type.hpp"
#include "instance/instance.hpp"
#include "thread/mle.hpp"
#endif

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_CRYPTO_FALLBACK_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_CRYPTO_FALLBACK_ENABLE != 0)
#define NRF54L15_CLEAN_OPENTHREAD_PAL_UNSUPPORTED_CRYPTO_STUBS 0
#else
#define NRF54L15_CLEAN_OPENTHREAD_PAL_UNSUPPORTED_CRYPTO_STUBS 1
#endif

namespace xiao_nrf54l15 {
namespace {

static const char* kSettingsNamespace = "otplat";
static const char* kVersionString = "nrf54l15-thread-skel-0";
constexpr uint32_t kCryptoSupportRandom = 1UL << 0U;
constexpr uint32_t kCryptoSupportAesEcb = 1UL << 1U;
constexpr uint32_t kCryptoSupportKeyRefs = 1UL << 2U;
constexpr uint32_t kCryptoSupportPbkdf2Cmac = 1UL << 3U;
// Current PAL receive is polling-based rather than interrupt-driven, so Thread
// attach needs a much wider idle RX window than the packet probes did.
// OpenThread expects frequent driver/tasklet/alarm servicing. Keep the PAL
// receive window short so two-node attach does not stall behind a blocking RX.
constexpr uint32_t kThreadRadioPollWindowUs = 10000U;
constexpr uint32_t kThreadRadioPollSpinLimit = 250000UL;
constexpr int8_t kThreadEdRssiOffsetDbm = -92;
constexpr int8_t kThreadEdRssiMaxDbm = 20;
constexpr size_t kCryptoMaxKeyBytes = OT_CRYPTO_ECDSA_MAX_DER_SIZE;
constexpr size_t kCryptoKeySlotCount = 8U;
constexpr size_t kThreadSrcMatchShortCapacity = 16U;
constexpr size_t kThreadSrcMatchExtCapacity = 16U;
constexpr otRadioCaps kThreadRadioCaps =
    static_cast<otRadioCaps>(OT_RADIO_CAPS_ENERGY_SCAN |
                             OT_RADIO_CAPS_CSMA_BACKOFF |
                             OT_RADIO_CAPS_RX_ON_WHEN_IDLE |
                             OT_RADIO_CAPS_TRANSMIT_FRAME_POWER |
                             OT_RADIO_CAPS_ALT_SHORT_ADDR);
constexpr uint8_t kIeee802154FrameTypeCommand = 0x03U;
constexpr uint8_t kIeee802154MacCommandDataRequest = 0x04U;
constexpr otPanId kDiagDefaultPanId = 0x1234U;
constexpr otShortAddress kDiagBroadcastShort = 0xFFFFU;
constexpr otShortAddress kDiagInvalidShort = OT_RADIO_INVALID_SHORT_ADDR;
constexpr uint8_t kDiagDefaultPayloadPattern = 0xA0U;
constexpr uint8_t kDiagMaxPayloadLength = 100U;

enum class CryptoContextKind : uint8_t {
  kNone = 0U,
  kAes = 1U,
  kSha256 = 2U,
  kHmacSha256 = 3U,
  kHkdfSha256 = 4U,
};

struct CryptoContextHeader {
  CryptoContextKind kind = CryptoContextKind::kNone;
};

struct CryptoAesContext {
  CryptoContextHeader header = {};
  bool hasKey = false;
  uint8_t key[16] = {0};
};

constexpr size_t kCryptoAesBlockSize = 16U;
constexpr size_t kCryptoSha256BlockSize = 64U;
constexpr size_t kCryptoSha256HashSize = OT_CRYPTO_SHA256_HASH_SIZE;

struct CryptoSha256Context {
  CryptoContextHeader header = {};
  uint32_t state[8] = {0};
  uint64_t totalBytes = 0U;
  uint8_t buffer[kCryptoSha256BlockSize] = {0};
  uint8_t bufferLength = 0U;
  bool started = false;
};

struct CryptoHmacSha256Context {
  CryptoContextHeader header = {};
  CryptoSha256Context inner = {};
  uint8_t opad[kCryptoSha256BlockSize] = {0};
  bool started = false;
};

struct CryptoHkdfContext {
  CryptoContextHeader header = {};
  uint8_t prk[kCryptoSha256HashSize] = {0};
  bool hasPrk = false;
};

struct CryptoKeySlot {
  bool occupied = false;
  otCryptoKeyRef ref = 0;
  otCryptoKeyType type = OT_CRYPTO_KEY_TYPE_RAW;
  otCryptoKeyAlgorithm algorithm = OT_CRYPTO_KEY_ALG_VENDOR;
  int usage = OT_CRYPTO_KEY_USAGE_NONE;
  otCryptoKeyStorage storage = OT_CRYPTO_KEY_STORAGE_VOLATILE;
  uint16_t length = 0;
  uint8_t bytes[kCryptoMaxKeyBytes] = {0};
};

struct ThreadSrcMatchShortEntry {
  bool occupied = false;
  otShortAddress address = OT_RADIO_INVALID_SHORT_ADDR;
};

struct ThreadSrcMatchExtEntry {
  bool occupied = false;
  otExtAddress address = {};
};

struct ThreadMacDataRequestSource {
  bool valid = false;
  bool shortAddress = false;
  otShortAddress shortValue = OT_RADIO_INVALID_SHORT_ADDR;
  otExtAddress extValue = {};
};

struct ThreadMacDestination {
  bool valid = false;
  bool present = false;
  bool shortAddress = false;
  otPanId panId = OT_PANID_BROADCAST;
  otShortAddress shortValue = OT_RADIO_INVALID_SHORT_ADDR;
  otExtAddress extValue = {};
};

struct OpenThreadPlatformState {
  OpenThreadPlatformSkeletonSnapshot snapshot;
  Preferences settings;
  bool settingsOpen = false;
  const uint16_t* sensitiveKeys = nullptr;

  uint32_t lastRadioNowLow = 0;
  uint64_t radioNowHigh = 0;

  ZigbeeRadio radio;
  uint8_t txPsdu[OT_RADIO_FRAME_MAX_SIZE] = {0};
  otRadioFrame txFrame = {};
  uint8_t txAckPsdu[OT_RADIO_FRAME_MAX_SIZE] = {0};
  otRadioFrame txAckFrame = {};
  uint8_t rxPsdu[OT_RADIO_FRAME_MAX_SIZE] = {0};
  otRadioFrame rxFrame = {};
  otInstance* radioCallbackInstance = nullptr;
  bool radioTxDonePending = false;
  bool radioRxDonePending = false;
  otError radioTxDoneError = OT_ERROR_NONE;
  bool radioTxAckFrameValid = false;
  bool radioEnergyScanDonePending = false;
  int8_t radioEnergyScanDoneDbm = OT_RADIO_RSSI_INVALID;
  bool radioSrcMatchEnabled = false;
  ThreadSrcMatchShortEntry srcMatchShort[kThreadSrcMatchShortCapacity] = {};
  ThreadSrcMatchExtEntry srcMatchExt[kThreadSrcMatchExtCapacity] = {};

  otPlatDiagOutputCallback diagCallback = nullptr;
  void* diagCallbackContext = nullptr;
  CracenRng cryptoRng;
  bool cryptoRngReady = false;
  otCryptoKeyRef nextVolatileKeyRef = 1;
  CryptoKeySlot cryptoKeys[kCryptoKeySlotCount] = {};
} gOpenThreadPlatformState;

void ensureTxFrameInitialized() {
  gOpenThreadPlatformState.txFrame.mPsdu = gOpenThreadPlatformState.txPsdu;
}

void ensureTxAckFrameInitialized() {
  gOpenThreadPlatformState.txAckFrame.mPsdu = gOpenThreadPlatformState.txAckPsdu;
}

void ensureRxFrameInitialized() {
  gOpenThreadPlatformState.rxFrame.mPsdu = gOpenThreadPlatformState.rxPsdu;
}

uint16_t readLe16(const uint8_t* src) {
  return static_cast<uint16_t>(src[0]) |
         (static_cast<uint16_t>(src[1]) << 8U);
}

void captureThreadMacFrameSummary(OpenThreadPlatformSkeletonSnapshot& snapshot,
                                  const uint8_t* psdu, uint8_t length,
                                  bool txFrame) {
  uint8_t* sequenceOut =
      txFrame ? &snapshot.radioLastTxSequence : &snapshot.radioLastRxSequence;
  uint8_t* frameTypeOut = txFrame ? &snapshot.radioLastTxFrameType
                                  : &snapshot.radioLastRxFrameType;
  uint8_t* dstAddrModeOut = txFrame ? &snapshot.radioLastTxDstAddrMode
                                    : &snapshot.radioLastRxDstAddrMode;
  otShortAddress* dstShortOut =
      txFrame ? &snapshot.radioLastTxDestinationShort
              : &snapshot.radioLastRxDestinationShort;
  uint8_t* headerOut =
      txFrame ? snapshot.radioLastTxHeader : snapshot.radioLastRxHeader;

  *sequenceOut = 0U;
  *frameTypeOut = 0U;
  *dstAddrModeOut = 0U;
  *dstShortOut = OT_RADIO_INVALID_SHORT_ADDR;
  memset(headerOut, 0, 10U);

  if (psdu == nullptr || length < 2U) {
    return;
  }

  memcpy(headerOut, psdu, (length < 10U) ? length : 10U);

  const uint16_t frameControl = readLe16(psdu);
  const bool sequenceSuppressed = ((frameControl >> 8U) & 0x1U) != 0U;
  const uint8_t destinationMode =
      static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  size_t index = sequenceSuppressed ? 2U : 3U;

  *frameTypeOut = static_cast<uint8_t>(frameControl & 0x07U);
  *dstAddrModeOut = destinationMode;
  if (!sequenceSuppressed && length >= 3U) {
    *sequenceOut = psdu[2];
  }
  if (destinationMode == 0x02U && length >= (index + 2U + 2U)) {
    index += 2U;
    *dstShortOut = readLe16(&psdu[index]);
  }
}

void ensureSettingsOpen() {
  if (!gOpenThreadPlatformState.settingsOpen) {
    gOpenThreadPlatformState.settings.begin(kSettingsNamespace, false);
    gOpenThreadPlatformState.settingsOpen = true;
  }
}

void closeSettings() {
  if (gOpenThreadPlatformState.settingsOpen) {
    gOpenThreadPlatformState.settings.end();
    gOpenThreadPlatformState.settingsOpen = false;
  }
}

void makeCountKey(uint16_t key, char* outKey, size_t outLen) {
  snprintf(outKey, outLen, "k%04X.c", static_cast<unsigned>(key));
}

void makeLengthKey(uint16_t key, int index, char* outKey, size_t outLen) {
  snprintf(outKey, outLen, "k%04X.%02d.l", static_cast<unsigned>(key), index);
}

void makeDataKey(uint16_t key, int index, char* outKey, size_t outLen) {
  snprintf(outKey, outLen, "k%04X.%02d.d", static_cast<unsigned>(key), index);
}

void makeDataChunkKey(uint16_t key,
                      int index,
                      uint16_t chunkIndex,
                      char* outKey,
                      size_t outLen) {
  snprintf(outKey,
           outLen,
           "k%04X.%02d.d%02u",
           static_cast<unsigned>(key),
           index,
           static_cast<unsigned>(chunkIndex));
}

constexpr uint16_t kSettingChunkLength = 48U;

uint16_t getSettingChunkCount(uint16_t valueLength) {
  if (valueLength == 0U) {
    return 0U;
  }
  return static_cast<uint16_t>((valueLength + kSettingChunkLength - 1U) /
                               kSettingChunkLength);
}

void removeSettingDataKeys(uint16_t key, int index, uint16_t valueLength) {
  ensureSettingsOpen();

  char dataKey[20];
  makeDataKey(key, index, dataKey, sizeof(dataKey));
  gOpenThreadPlatformState.settings.remove(dataKey);

  const uint16_t chunkCount = getSettingChunkCount(valueLength);
  for (uint16_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
    char chunkKey[20];
    makeDataChunkKey(key, index, chunkIndex, chunkKey, sizeof(chunkKey));
    gOpenThreadPlatformState.settings.remove(chunkKey);
  }
}

uint16_t getSettingCount(uint16_t key) {
  ensureSettingsOpen();
  char countKey[16];
  makeCountKey(key, countKey, sizeof(countKey));
  return gOpenThreadPlatformState.settings.getUShort(countKey, 0);
}

bool setSettingCount(uint16_t key, uint16_t count) {
  ensureSettingsOpen();
  char countKey[16];
  makeCountKey(key, countKey, sizeof(countKey));
  return gOpenThreadPlatformState.settings.putUShort(countKey, count) == sizeof(uint16_t);
}

bool readSettingItem(uint16_t key, int index, uint8_t* value, uint16_t* valueLength) {
  if (valueLength == nullptr) {
    return false;
  }

  ensureSettingsOpen();

  char lengthKey[20];
  makeLengthKey(key, index, lengthKey, sizeof(lengthKey));

  if (!gOpenThreadPlatformState.settings.isKey(lengthKey)) {
    return false;
  }

  const uint16_t actualLength = gOpenThreadPlatformState.settings.getUShort(lengthKey, 0);
  const uint16_t requestedLength = *valueLength;
  *valueLength = actualLength;

  if (value == nullptr || actualLength == 0) {
    return true;
  }

  const uint16_t copyLength = (requestedLength < actualLength) ? requestedLength : actualLength;
  if (actualLength <= kSettingChunkLength) {
    char dataKey[20];
    makeDataKey(key, index, dataKey, sizeof(dataKey));
    if (!gOpenThreadPlatformState.settings.isKey(dataKey)) {
      return false;
    }
    gOpenThreadPlatformState.settings.getBytes(dataKey, value, copyLength);
    return true;
  }

  uint16_t offset = 0U;
  uint16_t remaining = actualLength;
  uint16_t chunkIndex = 0U;
  while (remaining > 0U) {
    const uint16_t chunkLength =
        (remaining < kSettingChunkLength) ? remaining : kSettingChunkLength;
    const uint16_t copyChunkLength =
        (offset < copyLength)
            ? ((copyLength - offset) < chunkLength ? (copyLength - offset) : chunkLength)
            : 0U;
    char chunkKey[20];
    makeDataChunkKey(key, index, chunkIndex, chunkKey, sizeof(chunkKey));
    if (!gOpenThreadPlatformState.settings.isKey(chunkKey)) {
      return false;
    }
    if (copyChunkLength != 0U) {
      gOpenThreadPlatformState.settings.getBytes(chunkKey,
                                                 value + offset,
                                                 copyChunkLength);
      offset = static_cast<uint16_t>(offset + copyChunkLength);
    }
    remaining = static_cast<uint16_t>(remaining - chunkLength);
    ++chunkIndex;
  }
  return true;
}

bool writeSettingItem(uint16_t key, int index, const uint8_t* value, uint16_t valueLength) {
  ensureSettingsOpen();

  char lengthKey[20];
  makeLengthKey(key, index, lengthKey, sizeof(lengthKey));
  const uint16_t previousLength =
      gOpenThreadPlatformState.settings.isKey(lengthKey)
          ? gOpenThreadPlatformState.settings.getUShort(lengthKey, 0)
          : 0U;

  if (valueLength == 0) {
    if (gOpenThreadPlatformState.settings.putUShort(lengthKey, valueLength) !=
        sizeof(uint16_t)) {
      return false;
    }
    removeSettingDataKeys(key, index, previousLength);
    return true;
  }

  if (valueLength <= kSettingChunkLength) {
    char dataKey[20];
    makeDataKey(key, index, dataKey, sizeof(dataKey));
    if (gOpenThreadPlatformState.settings.putBytes(dataKey, value, valueLength) !=
        valueLength) {
      return false;
    }
  } else {
    uint16_t offset = 0U;
    uint16_t chunkIndex = 0U;
    while (offset < valueLength) {
      const uint16_t chunkLength =
          ((valueLength - offset) < kSettingChunkLength) ? (valueLength - offset)
                                                         : kSettingChunkLength;
      char chunkKey[20];
      makeDataChunkKey(key, index, chunkIndex, chunkKey, sizeof(chunkKey));
      if (gOpenThreadPlatformState.settings.putBytes(chunkKey,
                                                     value + offset,
                                                     chunkLength) !=
          chunkLength) {
        return false;
      }
      offset = static_cast<uint16_t>(offset + chunkLength);
      ++chunkIndex;
    }
  }

  if (gOpenThreadPlatformState.settings.putUShort(lengthKey, valueLength) !=
      sizeof(uint16_t)) {
    return false;
  }

  if (previousLength != valueLength) {
    if (previousLength <= kSettingChunkLength && valueLength > kSettingChunkLength) {
      char previousDataKey[20];
      makeDataKey(key, index, previousDataKey, sizeof(previousDataKey));
      gOpenThreadPlatformState.settings.remove(previousDataKey);
    } else if (previousLength > kSettingChunkLength &&
               valueLength <= kSettingChunkLength) {
      const uint16_t previousChunkCount = getSettingChunkCount(previousLength);
      for (uint16_t chunkIndex = 0; chunkIndex < previousChunkCount; ++chunkIndex) {
        char chunkKey[20];
        makeDataChunkKey(key, index, chunkIndex, chunkKey, sizeof(chunkKey));
        gOpenThreadPlatformState.settings.remove(chunkKey);
      }
    } else if (previousLength > valueLength && previousLength > kSettingChunkLength) {
      const uint16_t previousChunkCount = getSettingChunkCount(previousLength);
      const uint16_t currentChunkCount = getSettingChunkCount(valueLength);
      for (uint16_t chunkIndex = currentChunkCount; chunkIndex < previousChunkCount;
           ++chunkIndex) {
        char chunkKey[20];
        makeDataChunkKey(key, index, chunkIndex, chunkKey, sizeof(chunkKey));
        gOpenThreadPlatformState.settings.remove(chunkKey);
      }
    }
  }

  return true;
}

void removeSettingItemKeys(uint16_t key, int index) {
  ensureSettingsOpen();

  char lengthKey[20];
  makeLengthKey(key, index, lengthKey, sizeof(lengthKey));
  const uint16_t valueLength =
      gOpenThreadPlatformState.settings.isKey(lengthKey)
          ? gOpenThreadPlatformState.settings.getUShort(lengthKey, 0)
          : 0U;
  gOpenThreadPlatformState.settings.remove(lengthKey);
  removeSettingDataKeys(key, index, valueLength);
}

bool shiftSettingItem(uint16_t key, int fromIndex, int toIndex) {
  uint16_t length = 0;
  if (!readSettingItem(key, fromIndex, nullptr, &length)) {
    return false;
  }

  uint8_t* buffer = nullptr;
  if (length != 0) {
    buffer = static_cast<uint8_t*>(malloc(length));
    if (buffer == nullptr) {
      return false;
    }
    uint16_t copyLength = length;
    if (!readSettingItem(key, fromIndex, buffer, &copyLength)) {
      free(buffer);
      return false;
    }
  }

  const bool ok = writeSettingItem(key, toIndex, buffer, length);
  free(buffer);
  return ok;
}

void secureZero(void* ptr, size_t length) {
  if (ptr == nullptr) {
    return;
  }
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(ptr);
  while (length-- > 0U) {
    *bytes++ = 0U;
  }
}

void refreshThreadSrcMatchSnapshot(OpenThreadPlatformState& state) {
  uint8_t shortCount = 0U;
  uint8_t extCount = 0U;
  for (const ThreadSrcMatchShortEntry& entry : state.srcMatchShort) {
    if (entry.occupied) {
      ++shortCount;
    }
  }
  for (const ThreadSrcMatchExtEntry& entry : state.srcMatchExt) {
    if (entry.occupied) {
      ++extCount;
    }
  }
  state.snapshot.radioSrcMatchEnabled = state.radioSrcMatchEnabled;
  state.snapshot.radioSrcMatchShortCount = shortCount;
  state.snapshot.radioSrcMatchExtCount = extCount;
}

void clearThreadSrcMatchTables(OpenThreadPlatformState& state) {
  memset(state.srcMatchShort, 0, sizeof(state.srcMatchShort));
  memset(state.srcMatchExt, 0, sizeof(state.srcMatchExt));
  refreshThreadSrcMatchSnapshot(state);
}

bool hasThreadSrcMatchShort(const OpenThreadPlatformState& state,
                            otShortAddress address) {
  for (const ThreadSrcMatchShortEntry& entry : state.srcMatchShort) {
    if (entry.occupied && entry.address == address) {
      return true;
    }
  }
  return false;
}

bool hasThreadSrcMatchExt(const OpenThreadPlatformState& state,
                          const otExtAddress* address) {
  if (address == nullptr) {
    return false;
  }
  for (const ThreadSrcMatchExtEntry& entry : state.srcMatchExt) {
    if (entry.occupied &&
        memcmp(entry.address.m8, address->m8, sizeof(entry.address.m8)) == 0) {
      return true;
    }
  }
  return false;
}

otError addThreadSrcMatchShort(OpenThreadPlatformState& state,
                               otShortAddress address) {
  if (hasThreadSrcMatchShort(state, address)) {
    return OT_ERROR_NONE;
  }
  for (ThreadSrcMatchShortEntry& entry : state.srcMatchShort) {
    if (!entry.occupied) {
      entry.occupied = true;
      entry.address = address;
      refreshThreadSrcMatchSnapshot(state);
      return OT_ERROR_NONE;
    }
  }
  return OT_ERROR_NO_BUFS;
}

otError addThreadSrcMatchExt(OpenThreadPlatformState& state,
                             const otExtAddress* address) {
  if (address == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (hasThreadSrcMatchExt(state, address)) {
    return OT_ERROR_NONE;
  }
  for (ThreadSrcMatchExtEntry& entry : state.srcMatchExt) {
    if (!entry.occupied) {
      entry.occupied = true;
      entry.address = *address;
      refreshThreadSrcMatchSnapshot(state);
      return OT_ERROR_NONE;
    }
  }
  return OT_ERROR_NO_BUFS;
}

otError clearThreadSrcMatchShort(OpenThreadPlatformState& state,
                                 otShortAddress address) {
  for (ThreadSrcMatchShortEntry& entry : state.srcMatchShort) {
    if (entry.occupied && entry.address == address) {
      entry.occupied = false;
      entry.address = OT_RADIO_INVALID_SHORT_ADDR;
      refreshThreadSrcMatchSnapshot(state);
      return OT_ERROR_NONE;
    }
  }
  return OT_ERROR_NO_ADDRESS;
}

otError clearThreadSrcMatchExt(OpenThreadPlatformState& state,
                               const otExtAddress* address) {
  if (address == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  for (ThreadSrcMatchExtEntry& entry : state.srcMatchExt) {
    if (entry.occupied &&
        memcmp(entry.address.m8, address->m8, sizeof(entry.address.m8)) == 0) {
      entry.occupied = false;
      memset(entry.address.m8, 0, sizeof(entry.address.m8));
      refreshThreadSrcMatchSnapshot(state);
      return OT_ERROR_NONE;
    }
  }
  return OT_ERROR_NO_ADDRESS;
}

bool parseThreadMacDataRequestSource(const uint8_t* psdu, uint8_t length,
                                     ThreadMacDataRequestSource* outSource) {
  if (outSource == nullptr) {
    return false;
  }
  *outSource = {};
  if (psdu == nullptr || length < 3U) {
    return false;
  }

  const uint16_t frameControl = readLe16(psdu);
  if ((frameControl & 0x0007U) != kIeee802154FrameTypeCommand) {
    return false;
  }

  const bool panCompression = ((frameControl >> 6U) & 0x1U) != 0U;
  const bool sequenceSuppressed = ((frameControl >> 8U) & 0x1U) != 0U;
  const uint8_t destinationMode =
      static_cast<uint8_t>((frameControl >> 10U) & 0x03U);
  const uint8_t sourceMode =
      static_cast<uint8_t>((frameControl >> 14U) & 0x03U);

  size_t index = sequenceSuppressed ? 2U : 3U;
  if (destinationMode != 0U) {
    if (length < (index + 2U)) {
      return false;
    }
    index += 2U;
    if (destinationMode == 0x02U) {
      index += 2U;
    } else if (destinationMode == 0x03U) {
      index += 8U;
    } else {
      return false;
    }
  }

  if (sourceMode != 0U && (!panCompression || destinationMode == 0U)) {
    index += 2U;
  }
  if (length < index) {
    return false;
  }

  if (sourceMode == 0x02U) {
    if (length < (index + 2U + 1U)) {
      return false;
    }
    outSource->shortAddress = true;
    outSource->shortValue = readLe16(&psdu[index]);
    index += 2U;
  } else if (sourceMode == 0x03U) {
    if (length < (index + sizeof(outSource->extValue.m8) + 1U)) {
      return false;
    }
    memcpy(outSource->extValue.m8, &psdu[index], sizeof(outSource->extValue.m8));
    index += sizeof(outSource->extValue.m8);
  } else {
    return false;
  }

  if (psdu[index] != kIeee802154MacCommandDataRequest) {
    return false;
  }

  outSource->valid = true;
  return true;
}

bool threadMacDataRequestPendingCallback(const uint8_t* psdu, uint8_t length,
                                         void* context) {
  OpenThreadPlatformState* state =
      static_cast<OpenThreadPlatformState*>(context);
  if (state == nullptr) {
    return true;
  }

  ThreadMacDataRequestSource source = {};
  if (!parseThreadMacDataRequestSource(psdu, length, &source)) {
    return true;
  }

  bool match = true;
  if (state->radioSrcMatchEnabled) {
    match = source.shortAddress
                ? hasThreadSrcMatchShort(*state, source.shortValue)
                : hasThreadSrcMatchExt(*state, &source.extValue);
  }

  state->snapshot.radioLastSrcMatchMatched = match;
  state->snapshot.radioLastSrcMatchWasShort = source.shortAddress;
  state->snapshot.radioLastSrcMatchShortAddress =
      source.shortAddress ? source.shortValue : OT_RADIO_INVALID_SHORT_ADDR;
  if (match) {
    state->snapshot.radioSrcMatchAckSetCount++;
  } else {
    state->snapshot.radioSrcMatchAckClearCount++;
  }
  return match;
}

bool parseThreadMacDestination(const uint8_t* psdu, uint8_t length,
                               ThreadMacDestination* outDestination) {
  if (outDestination == nullptr) {
    return false;
  }

  *outDestination = {};
  if (psdu == nullptr || length < 2U) {
    return false;
  }

  const uint16_t frameControl = readLe16(psdu);
  const bool sequenceSuppressed = ((frameControl >> 8U) & 0x1U) != 0U;
  const uint8_t destinationMode =
      static_cast<uint8_t>((frameControl >> 10U) & 0x03U);

  size_t index = sequenceSuppressed ? 2U : 3U;
  if (destinationMode == 0U) {
    outDestination->valid = true;
    return true;
  }

  if (length < (index + 2U)) {
    return false;
  }
  outDestination->panId = readLe16(&psdu[index]);
  index += 2U;

  if (destinationMode == 0x02U) {
    if (length < (index + 2U)) {
      return false;
    }
    outDestination->present = true;
    outDestination->shortAddress = true;
    outDestination->shortValue = readLe16(&psdu[index]);
    outDestination->valid = true;
    return true;
  }

  if (destinationMode == 0x03U) {
    if (length < (index + sizeof(outDestination->extValue.m8))) {
      return false;
    }
    outDestination->present = true;
    memcpy(outDestination->extValue.m8, &psdu[index],
           sizeof(outDestination->extValue.m8));
    outDestination->valid = true;
    return true;
  }

  return false;
}

bool threadMacDestinationMatchesLocal(const OpenThreadPlatformState& state,
                                      const ThreadMacDestination& destination) {
  if (state.snapshot.diagModeEnabled || state.snapshot.radioPromiscuous) {
    return true;
  }
  if (!destination.valid || !destination.present) {
    return false;
  }

  if (state.snapshot.panId != 0U && destination.panId != OT_PANID_BROADCAST &&
      destination.panId != state.snapshot.panId) {
    return false;
  }

  if (destination.shortAddress) {
    if (destination.shortValue == OT_RADIO_BROADCAST_SHORT_ADDR) {
      return true;
    }
    if (state.snapshot.shortAddress != OT_RADIO_INVALID_SHORT_ADDR &&
        destination.shortValue == state.snapshot.shortAddress) {
      return true;
    }
    if (state.snapshot.alternateShortAddress != OT_RADIO_INVALID_SHORT_ADDR &&
        destination.shortValue == state.snapshot.alternateShortAddress) {
      return true;
    }
    return false;
  }

  return memcmp(destination.extValue.m8, state.snapshot.extendedAddress.m8,
                sizeof(destination.extValue.m8)) == 0;
}

bool threadMacReceiveFilterCallback(const uint8_t* psdu, uint8_t length,
                                    void* context) {
  OpenThreadPlatformState* state =
      static_cast<OpenThreadPlatformState*>(context);
  if (state == nullptr) {
    return true;
  }

  ThreadMacDestination destination = {};
  if (!parseThreadMacDestination(psdu, length, &destination)) {
    state->snapshot.radioFilteredCount++;
    return false;
  }

  const bool match = threadMacDestinationMatchesLocal(*state, destination);
  if (!match) {
    state->snapshot.radioFilteredCount++;
  }
  return match;
}

uint64_t nextEntropyWord();

void fillPseudoEntropy(uint8_t* output, uint16_t outputLength) {
  uint16_t offset = 0;
  while (offset < outputLength) {
    const uint64_t word = nextEntropyWord();
    for (uint8_t i = 0; (i < sizeof(word)) && (offset < outputLength); ++i) {
      output[offset++] = static_cast<uint8_t>((word >> (i * 8U)) & 0xFFU);
    }
  }
}

uint64_t nextEntropyWord() {
  const uint64_t uniqueId = hardwareUniqueId64();
  const uint64_t now = static_cast<uint64_t>(micros());
  static uint64_t counter = 0xA5C39E27D4B1826FULL;

  counter ^= uniqueId + 0x9E3779B97F4A7C15ULL + (counter << 6U) + (counter >> 2U);
  counter ^= (now << 17U) ^ (now >> 7U);
  counter ^= static_cast<uint64_t>(millis()) << 32U;

  uint64_t x = counter;
  x ^= x >> 12U;
  x ^= x << 25U;
  x ^= x >> 27U;
  return x * 2685821657736338717ULL;
}

void updateRadioTime() {
  const uint32_t nowLow = micros();
  if (nowLow < gOpenThreadPlatformState.lastRadioNowLow) {
    gOpenThreadPlatformState.radioNowHigh += (1ULL << 32U);
  }
  gOpenThreadPlatformState.lastRadioNowLow = nowLow;
  gOpenThreadPlatformState.snapshot.radioNowUs = gOpenThreadPlatformState.radioNowHigh | nowLow;
}

bool isThreadAttachLogLine(const char* line) {
  if (line == nullptr || *line == '\0') {
    return false;
  }

  static const char* const kAttachPrefixes[] = {
      "Mle",
      "MeshForwarder",
      "ParentSearch",
      "NeighborTable",
      "RouterTable",
      "MeshCop",
  };

  for (const char* prefix : kAttachPrefixes) {
    if (strstr(line, prefix) != nullptr) {
      return true;
    }
  }

  return false;
}

bool rememberRecentLogLine(
    char lines[OpenThreadPlatformSkeletonSnapshot::kRecentLogLineCount]
              [OpenThreadPlatformSkeletonSnapshot::kRecentLogLineLength],
    const char* line) {
  if (line == nullptr || *line == '\0') {
    return false;
  }

  if (strncmp(lines[0], line, sizeof(lines[0])) == 0) {
    return false;
  }

  for (size_t index = OpenThreadPlatformSkeletonSnapshot::kRecentLogLineCount - 1U;
       index > 0U; --index) {
    memcpy(lines[index], lines[index - 1U], sizeof(lines[index]));
  }

  strncpy(lines[0], line, sizeof(lines[0]) - 1U);
  lines[0][sizeof(lines[0]) - 1U] = '\0';
  return true;
}

void rememberPlatformLogLine(const char* line) {
  OpenThreadPlatformSkeletonSnapshot& snapshot = gOpenThreadPlatformState.snapshot;

  if (line == nullptr) {
    snapshot.lastLogLine[0] = '\0';
    return;
  }

  strncpy(snapshot.lastLogLine, line, sizeof(snapshot.lastLogLine) - 1U);
  snapshot.lastLogLine[sizeof(snapshot.lastLogLine) - 1U] = '\0';

  if (rememberRecentLogLine(snapshot.recentLogLines, snapshot.lastLogLine)) {
    snapshot.recentLogCount++;
  }

  if (isThreadAttachLogLine(snapshot.lastLogLine) &&
      rememberRecentLogLine(snapshot.recentMleLogLines, snapshot.lastLogLine)) {
    snapshot.recentMleLogCount++;
  }
}

void setLastLogLineV(const char* format, va_list args) {
  char line[sizeof(gOpenThreadPlatformState.snapshot.lastLogLine)] = {0};
  vsnprintf(line, sizeof(line), format, args);
  rememberPlatformLogLine(line);
}

void setLastLogLine(const char* format, ...) {
  va_list args;
  va_start(args, format);
  setLastLogLineV(format, args);
  va_end(args);
}

void emitDiagOutput(const char* format, ...) {
  char line[sizeof(gOpenThreadPlatformState.snapshot.lastLogLine)] = {0};
  va_list args;
  va_start(args, format);
  va_list argsCopy;
  va_copy(argsCopy, args);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  setLastLogLine("%s", line);
  if (gOpenThreadPlatformState.diagCallback != nullptr) {
    gOpenThreadPlatformState.diagCallback(format, argsCopy,
                                          gOpenThreadPlatformState.diagCallbackContext);
  }
  va_end(argsCopy);
}

bool parseLongArg(const char* text, long* outValue) {
  if (text == nullptr || outValue == nullptr || *text == '\0') {
    return false;
  }
  char* end = nullptr;
  const long value = strtol(text, &end, 0);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  *outValue = value;
  return true;
}

otShortAddress defaultDiagShortAddress() {
  uint16_t value = static_cast<uint16_t>(hardwareUniqueId64() & 0xFFFFU);
  if (value == 0U || value == kDiagBroadcastShort || value == kDiagInvalidShort) {
    value ^= 0x5A5AU;
    value |= 0x0001U;
  }
  if (value == kDiagBroadcastShort || value == kDiagInvalidShort) {
    value = 0x7A11U;
  }
  return value;
}

void resetDiagSnapshot(OpenThreadPlatformState& state) {
  state.snapshot.diagRadioActive = false;
  state.snapshot.diagLastTxLength = 0U;
  state.snapshot.diagLastTxSequence = 0U;
  state.snapshot.diagLastRxLength = 0U;
  state.snapshot.diagLastRxChannel = 0U;
  state.snapshot.diagLastRxSequence = 0U;
  state.snapshot.diagLastTxError = OT_ERROR_NONE;
  state.snapshot.diagLastRxRssi = OT_RADIO_RSSI_INVALID;
  state.snapshot.diagTxCount = 0U;
  state.snapshot.diagRxCount = 0U;
}

void ensureDiagIdentity(otInstance* instance) {
  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  if (state.snapshot.panId == 0U) {
    otPlatRadioSetPanId(instance, kDiagDefaultPanId);
  }

  const otShortAddress shortAddress = state.snapshot.shortAddress;
  if (shortAddress == 0U || shortAddress == kDiagBroadcastShort ||
      shortAddress == kDiagInvalidShort) {
    otPlatRadioSetShortAddress(instance, defaultDiagShortAddress());
  }
}

otError startDiagRadio(otInstance* instance) {
  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  if (!otPlatRadioIsEnabled(instance)) {
    const otError enableError = otPlatRadioEnable(instance);
    if (enableError != OT_ERROR_NONE) {
      return enableError;
    }
  }

  ensureDiagIdentity(instance);
  resetDiagSnapshot(state);
  state.snapshot.diagModeEnabled = true;
  state.snapshot.diagRadioActive = true;
  otPlatRadioSetRxOnWhenIdle(instance, true);
  const otError powerError =
      otPlatRadioSetTransmitPower(instance, state.snapshot.txPowerDbm);
  if (powerError != OT_ERROR_NONE) {
    return powerError;
  }

  uint8_t channel = state.snapshot.radioChannel;
  if (channel < OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MIN ||
      channel > OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MAX) {
    channel = OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MIN;
  }
  return otPlatRadioReceive(instance, channel);
}

void stopDiagRadio(otInstance* instance) {
  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  state.snapshot.diagModeEnabled = false;
  state.snapshot.diagRadioActive = false;
  otPlatRadioSetRxOnWhenIdle(instance, false);
  if (state.snapshot.radioEnabled) {
    (void)otPlatRadioSleep(instance);
  }
}

otError sendDiagFrame(otInstance* instance, uint8_t payloadLength,
                      uint8_t pattern) {
  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  if (!state.snapshot.diagModeEnabled) {
    return OT_ERROR_INVALID_STATE;
  }
  if (payloadLength == 0U || payloadLength > kDiagMaxPayloadLength) {
    return OT_ERROR_INVALID_ARGS;
  }

  ensureDiagIdentity(instance);
  otRadioFrame* frame = otPlatRadioGetTransmitBuffer(instance);
  if (frame == nullptr || frame->mPsdu == nullptr) {
    return OT_ERROR_FAILED;
  }

  uint8_t payload[kDiagMaxPayloadLength] = {0};
  for (uint8_t i = 0; i < payloadLength; ++i) {
    payload[i] = static_cast<uint8_t>(pattern + i);
  }

  const uint8_t sequence = static_cast<uint8_t>(state.snapshot.diagTxCount + 1U);
  uint8_t length = 0U;
  if (!ZigbeeRadio::buildDataFrameShort(sequence, state.snapshot.panId,
                                        kDiagBroadcastShort,
                                        state.snapshot.shortAddress, payload,
                                        payloadLength, frame->mPsdu, &length,
                                        false)) {
    return OT_ERROR_FAILED;
  }

  frame->mLength = length;
  frame->mChannel = state.snapshot.radioChannel;
  frame->mInfo.mTxInfo.mCsmaCaEnabled = false;
  frame->mInfo.mTxInfo.mTxPower = state.snapshot.txPowerDbm;
  state.snapshot.diagLastTxLength = length;
  state.snapshot.diagLastTxSequence = sequence;
  return otPlatRadioTransmit(instance, frame);
}

void emitDiagStats(void) {
  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  emitDiagOutput("diag stats mode=%u active=%u ch=%u power=%d tx=%lu rx=%lu txlen=%u rxlen=%u rxrssi=%d\n",
                 state.snapshot.diagModeEnabled ? 1U : 0U,
                 state.snapshot.diagRadioActive ? 1U : 0U,
                 state.snapshot.radioChannel, state.snapshot.txPowerDbm,
                 static_cast<unsigned long>(state.snapshot.diagTxCount),
                 static_cast<unsigned long>(state.snapshot.diagRxCount),
                 state.snapshot.diagLastTxLength,
                 state.snapshot.diagLastRxLength,
                 state.snapshot.diagLastRxRssi);
}

void clearCryptoKeySlot(CryptoKeySlot& slot) {
  secureZero(slot.bytes, sizeof(slot.bytes));
  slot = {};
}

void resetCryptoState() {
  for (size_t i = 0; i < kCryptoKeySlotCount; ++i) {
    clearCryptoKeySlot(gOpenThreadPlatformState.cryptoKeys[i]);
  }
  if (gOpenThreadPlatformState.cryptoRngReady) {
    gOpenThreadPlatformState.cryptoRng.end();
  }
  gOpenThreadPlatformState.cryptoRngReady = false;
  gOpenThreadPlatformState.nextVolatileKeyRef = 1;
  gOpenThreadPlatformState.snapshot.cryptoInitialized = false;
  gOpenThreadPlatformState.snapshot.cryptoRandomHardware = false;
  gOpenThreadPlatformState.snapshot.cryptoAesReady = false;
  gOpenThreadPlatformState.snapshot.cryptoKeyCount = 0;
  gOpenThreadPlatformState.snapshot.cryptoLastKeyLength = 0;
  gOpenThreadPlatformState.snapshot.cryptoRandomRequests = 0;
  gOpenThreadPlatformState.snapshot.cryptoAesEncryptCount = 0;
  gOpenThreadPlatformState.snapshot.cryptoUnsupportedCount = 0;
  gOpenThreadPlatformState.snapshot.cryptoSupportMask = 0;
}

void refreshCryptoKeySnapshot() {
  uint16_t count = 0;
  for (const CryptoKeySlot& slot : gOpenThreadPlatformState.cryptoKeys) {
    if (slot.occupied) {
      ++count;
    }
  }
  gOpenThreadPlatformState.snapshot.cryptoKeyCount = count;
}

CryptoKeySlot* findCryptoKeySlot(otCryptoKeyRef ref) {
  if (ref == 0U) {
    return nullptr;
  }
  for (CryptoKeySlot& slot : gOpenThreadPlatformState.cryptoKeys) {
    if (slot.occupied && slot.ref == ref) {
      return &slot;
    }
  }
  return nullptr;
}

CryptoKeySlot* allocateCryptoKeySlot(otCryptoKeyRef ref) {
  if (ref == 0U) {
    return nullptr;
  }

  if (CryptoKeySlot* existing = findCryptoKeySlot(ref)) {
    return existing;
  }

  for (CryptoKeySlot& slot : gOpenThreadPlatformState.cryptoKeys) {
    if (!slot.occupied) {
      slot.occupied = true;
      slot.ref = ref;
      return &slot;
    }
  }
  return nullptr;
}

otCryptoKeyRef allocateVolatileKeyRef() {
  for (size_t attempt = 0; attempt < kCryptoKeySlotCount; ++attempt) {
    otCryptoKeyRef candidate = gOpenThreadPlatformState.nextVolatileKeyRef++;
    if (candidate == 0U) {
      candidate = gOpenThreadPlatformState.nextVolatileKeyRef++;
    }
    if (findCryptoKeySlot(candidate) == nullptr) {
      return candidate;
    }
  }
  return 0U;
}

otError resolveKeyMaterial(const otCryptoKey* key,
                           uint8_t* outKey,
                           size_t outKeyCapacity,
                           size_t* outKeyLength) {
  if (key == nullptr || outKeyLength == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }

  const uint8_t* source = key->mKey;
  size_t sourceLength = key->mKeyLength;

  if (source == nullptr) {
    CryptoKeySlot* slot = findCryptoKeySlot(key->mKeyRef);
    if (slot == nullptr) {
      return OT_ERROR_NOT_FOUND;
    }
    source = slot->bytes;
    sourceLength = slot->length;
  }

  if (sourceLength > outKeyCapacity) {
    return OT_ERROR_NO_BUFS;
  }

  if (source != nullptr && sourceLength != 0U) {
    memcpy(outKey, source, sourceLength);
  }
  *outKeyLength = sourceLength;
  return OT_ERROR_NONE;
}

CryptoAesContext* getAesContext(otCryptoContext* context) {
  if (context == nullptr || context->mContext == nullptr) {
    return nullptr;
  }
  CryptoContextHeader* header =
      static_cast<CryptoContextHeader*>(context->mContext);
  if (header->kind != CryptoContextKind::kAes) {
    return nullptr;
  }
  return static_cast<CryptoAesContext*>(context->mContext);
}

CryptoSha256Context* getSha256Context(otCryptoContext* context) {
  if (context == nullptr || context->mContext == nullptr) {
    return nullptr;
  }
  CryptoContextHeader* header =
      static_cast<CryptoContextHeader*>(context->mContext);
  if (header->kind != CryptoContextKind::kSha256) {
    return nullptr;
  }
  return static_cast<CryptoSha256Context*>(context->mContext);
}

CryptoHmacSha256Context* getHmacSha256Context(otCryptoContext* context) {
  if (context == nullptr || context->mContext == nullptr) {
    return nullptr;
  }
  CryptoContextHeader* header =
      static_cast<CryptoContextHeader*>(context->mContext);
  if (header->kind != CryptoContextKind::kHmacSha256) {
    return nullptr;
  }
  return static_cast<CryptoHmacSha256Context*>(context->mContext);
}

CryptoHkdfContext* getHkdfContext(otCryptoContext* context) {
  if (context == nullptr || context->mContext == nullptr) {
    return nullptr;
  }
  CryptoContextHeader* header =
      static_cast<CryptoContextHeader*>(context->mContext);
  if (header->kind != CryptoContextKind::kHkdfSha256) {
    return nullptr;
  }
  return static_cast<CryptoHkdfContext*>(context->mContext);
}

uint32_t loadBe32(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24U) |
         (static_cast<uint32_t>(data[1]) << 16U) |
         (static_cast<uint32_t>(data[2]) << 8U) |
         static_cast<uint32_t>(data[3]);
}

void storeBe32(uint32_t value, uint8_t* out) {
  out[0] = static_cast<uint8_t>(value >> 24U);
  out[1] = static_cast<uint8_t>(value >> 16U);
  out[2] = static_cast<uint8_t>(value >> 8U);
  out[3] = static_cast<uint8_t>(value);
}

void storeBe64(uint64_t value, uint8_t* out) {
  for (int i = 7; i >= 0; --i) {
    out[i] = static_cast<uint8_t>(value & 0xFFU);
    value >>= 8U;
  }
}

uint32_t rotateRight(uint32_t value, uint8_t shift) {
  return (value >> shift) | (value << (32U - shift));
}

void sha256Reset(CryptoSha256Context& context) {
  context.header.kind = CryptoContextKind::kSha256;
  context.state[0] = 0x6A09E667U;
  context.state[1] = 0xBB67AE85U;
  context.state[2] = 0x3C6EF372U;
  context.state[3] = 0xA54FF53AU;
  context.state[4] = 0x510E527FU;
  context.state[5] = 0x9B05688CU;
  context.state[6] = 0x1F83D9ABU;
  context.state[7] = 0x5BE0CD19U;
  context.totalBytes = 0U;
  context.bufferLength = 0U;
  context.started = true;
  memset(context.buffer, 0, sizeof(context.buffer));
}

void sha256Transform(CryptoSha256Context& context, const uint8_t* block) {
  static const uint32_t kRoundConstants[64] = {
      0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
      0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
      0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
      0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
      0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
      0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
      0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
      0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
      0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
      0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
      0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
      0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
      0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
      0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
      0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
      0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
  };

  uint32_t w[64] = {0};
  for (size_t i = 0; i < 16U; ++i) {
    w[i] = loadBe32(block + (i * 4U));
  }
  for (size_t i = 16U; i < 64U; ++i) {
    const uint32_t s0 =
        rotateRight(w[i - 15U], 7U) ^ rotateRight(w[i - 15U], 18U) ^
        (w[i - 15U] >> 3U);
    const uint32_t s1 =
        rotateRight(w[i - 2U], 17U) ^ rotateRight(w[i - 2U], 19U) ^
        (w[i - 2U] >> 10U);
    w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
  }

  uint32_t a = context.state[0];
  uint32_t b = context.state[1];
  uint32_t c = context.state[2];
  uint32_t d = context.state[3];
  uint32_t e = context.state[4];
  uint32_t f = context.state[5];
  uint32_t g = context.state[6];
  uint32_t h = context.state[7];

  for (size_t i = 0; i < 64U; ++i) {
    const uint32_t s1 =
        rotateRight(e, 6U) ^ rotateRight(e, 11U) ^ rotateRight(e, 25U);
    const uint32_t ch = (e & f) ^ ((~e) & g);
    const uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
    const uint32_t s0 =
        rotateRight(a, 2U) ^ rotateRight(a, 13U) ^ rotateRight(a, 22U);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = s0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  context.state[0] += a;
  context.state[1] += b;
  context.state[2] += c;
  context.state[3] += d;
  context.state[4] += e;
  context.state[5] += f;
  context.state[6] += g;
  context.state[7] += h;
}

otError sha256UpdateBytes(CryptoSha256Context& context,
                          const uint8_t* data,
                          size_t dataLength) {
  if (!context.started || (data == nullptr && dataLength != 0U)) {
    return OT_ERROR_INVALID_ARGS;
  }

  context.totalBytes += static_cast<uint64_t>(dataLength);
  while (dataLength != 0U) {
    const size_t space = kCryptoSha256BlockSize - context.bufferLength;
    const size_t chunk = (dataLength < space) ? dataLength : space;
    memcpy(context.buffer + context.bufferLength, data, chunk);
    context.bufferLength = static_cast<uint8_t>(context.bufferLength + chunk);
    data += chunk;
    dataLength -= chunk;
    if (context.bufferLength == kCryptoSha256BlockSize) {
      sha256Transform(context, context.buffer);
      context.bufferLength = 0U;
    }
  }

  return OT_ERROR_NONE;
}

otError sha256FinishBytes(CryptoSha256Context& context,
                          uint8_t* hash,
                          size_t hashSize) {
  if (!context.started || hash == nullptr || hashSize < kCryptoSha256HashSize) {
    return OT_ERROR_INVALID_ARGS;
  }

  const uint64_t totalBits = context.totalBytes * 8ULL;
  context.buffer[context.bufferLength++] = 0x80U;

  if (context.bufferLength > 56U) {
    while (context.bufferLength < kCryptoSha256BlockSize) {
      context.buffer[context.bufferLength++] = 0U;
    }
    sha256Transform(context, context.buffer);
    context.bufferLength = 0U;
  }

  while (context.bufferLength < 56U) {
    context.buffer[context.bufferLength++] = 0U;
  }
  storeBe64(totalBits, context.buffer + 56U);
  sha256Transform(context, context.buffer);
  context.bufferLength = 0U;

  for (size_t i = 0; i < 8U; ++i) {
    storeBe32(context.state[i], hash + (i * 4U));
  }
  context.started = false;
  return OT_ERROR_NONE;
}

otError sha256Compute(const uint8_t* data, size_t dataLength, uint8_t* hash) {
  CryptoSha256Context context = {};
  sha256Reset(context);
  otError error = sha256UpdateBytes(context, data, dataLength);
  if (error != OT_ERROR_NONE) {
    secureZero(&context, sizeof(context));
    return error;
  }
  error = sha256FinishBytes(context, hash, kCryptoSha256HashSize);
  secureZero(&context, sizeof(context));
  return error;
}

otError startHmacSha256(CryptoHmacSha256Context& context,
                        const uint8_t* key,
                        size_t keyLength) {
  if (key == nullptr && keyLength != 0U) {
    return OT_ERROR_INVALID_ARGS;
  }

  uint8_t keyBlock[kCryptoSha256BlockSize] = {0};
  uint8_t ipad[kCryptoSha256BlockSize] = {0};

  if (keyLength > kCryptoSha256BlockSize) {
    otError error = sha256Compute(key, keyLength, keyBlock);
    if (error != OT_ERROR_NONE) {
      secureZero(keyBlock, sizeof(keyBlock));
      secureZero(ipad, sizeof(ipad));
      return error;
    }
    keyLength = kCryptoSha256HashSize;
  } else if (keyLength != 0U) {
    memcpy(keyBlock, key, keyLength);
  }

  context.header.kind = CryptoContextKind::kHmacSha256;
  memset(context.opad, 0, sizeof(context.opad));
  for (size_t i = 0; i < kCryptoSha256BlockSize; ++i) {
    ipad[i] = static_cast<uint8_t>(keyBlock[i] ^ 0x36U);
    context.opad[i] = static_cast<uint8_t>(keyBlock[i] ^ 0x5CU);
  }

  sha256Reset(context.inner);
  otError error = sha256UpdateBytes(context.inner, ipad, sizeof(ipad));
  if (error == OT_ERROR_NONE) {
    context.started = true;
  }

  secureZero(keyBlock, sizeof(keyBlock));
  secureZero(ipad, sizeof(ipad));
  return error;
}

otError finishHmacSha256(CryptoHmacSha256Context& context,
                         uint8_t* hash,
                         size_t hashSize) {
  if (!context.started || hash == nullptr || hashSize < kCryptoSha256HashSize) {
    return OT_ERROR_INVALID_ARGS;
  }

  uint8_t innerHash[kCryptoSha256HashSize] = {0};
  otError error = sha256FinishBytes(context.inner, innerHash, sizeof(innerHash));
  if (error != OT_ERROR_NONE) {
    secureZero(innerHash, sizeof(innerHash));
    return error;
  }

  CryptoSha256Context outer = {};
  sha256Reset(outer);
  error = sha256UpdateBytes(outer, context.opad, sizeof(context.opad));
  if (error == OT_ERROR_NONE) {
    error = sha256UpdateBytes(outer, innerHash, sizeof(innerHash));
  }
  if (error == OT_ERROR_NONE) {
    error = sha256FinishBytes(outer, hash, hashSize);
  }

  secureZero(innerHash, sizeof(innerHash));
  secureZero(&outer, sizeof(outer));
  context.started = false;
  return error;
}

otError hmacSha256Compute(const uint8_t* key,
                         size_t keyLength,
                         const uint8_t* inputA,
                         size_t inputALength,
                         const uint8_t* inputB,
                         size_t inputBLength,
                         const uint8_t* inputC,
                         size_t inputCLength,
                         uint8_t* hash) {
  CryptoHmacSha256Context context = {};
  otError error = startHmacSha256(context, key, keyLength);
  if (error == OT_ERROR_NONE && inputALength != 0U) {
    error = sha256UpdateBytes(context.inner, inputA, inputALength);
  }
  if (error == OT_ERROR_NONE && inputBLength != 0U) {
    error = sha256UpdateBytes(context.inner, inputB, inputBLength);
  }
  if (error == OT_ERROR_NONE && inputCLength != 0U) {
    error = sha256UpdateBytes(context.inner, inputC, inputCLength);
  }
  if (error == OT_ERROR_NONE) {
    error = finishHmacSha256(context, hash, kCryptoSha256HashSize);
  }
  secureZero(&context, sizeof(context));
  return error;
}

otError aesEcbEncryptBlockRaw(const uint8_t key[kCryptoAesBlockSize],
                              const uint8_t input[kCryptoAesBlockSize],
                              uint8_t output[kCryptoAesBlockSize]) {
  Ecb ecb;
  return ecb.encryptBlock(key, input, output) ? OT_ERROR_NONE : OT_ERROR_FAILED;
}

void xorAesBlock(const uint8_t lhs[kCryptoAesBlockSize],
                 const uint8_t rhs[kCryptoAesBlockSize],
                 uint8_t output[kCryptoAesBlockSize]) {
  for (size_t i = 0; i < kCryptoAesBlockSize; ++i) {
    output[i] = static_cast<uint8_t>(lhs[i] ^ rhs[i]);
  }
}

void leftShiftAesBlock(const uint8_t input[kCryptoAesBlockSize],
                       uint8_t output[kCryptoAesBlockSize]) {
  uint8_t carry = 0U;
  for (size_t i = kCryptoAesBlockSize; i > 0U; --i) {
    const uint8_t current = input[i - 1U];
    output[i - 1U] =
        static_cast<uint8_t>((current << 1U) | carry);
    carry = static_cast<uint8_t>((current & 0x80U) != 0U);
  }
}

void doubleAesCmacSubkey(uint8_t block[kCryptoAesBlockSize]) {
  uint8_t shifted[kCryptoAesBlockSize] = {0};
  const bool msbSet = (block[0] & 0x80U) != 0U;
  leftShiftAesBlock(block, shifted);
  if (msbSet) {
    shifted[kCryptoAesBlockSize - 1U] ^= 0x87U;
  }
  memcpy(block, shifted, sizeof(shifted));
  secureZero(shifted, sizeof(shifted));
}

otError aesCmacCompute128(const uint8_t key[kCryptoAesBlockSize],
                          const uint8_t* message,
                          size_t messageLength,
                          uint8_t mac[kCryptoAesBlockSize]) {
  if ((message == nullptr && messageLength != 0U) || mac == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }

  uint8_t zero[kCryptoAesBlockSize] = {0};
  uint8_t subkey1[kCryptoAesBlockSize] = {0};
  uint8_t subkey2[kCryptoAesBlockSize] = {0};
  uint8_t state[kCryptoAesBlockSize] = {0};
  uint8_t block[kCryptoAesBlockSize] = {0};
  uint8_t work[kCryptoAesBlockSize] = {0};

  otError error = aesEcbEncryptBlockRaw(key, zero, subkey1);
  if (error != OT_ERROR_NONE) {
    secureZero(zero, sizeof(zero));
    return error;
  }
  doubleAesCmacSubkey(subkey1);
  memcpy(subkey2, subkey1, sizeof(subkey2));
  doubleAesCmacSubkey(subkey2);

  const size_t blockCount =
      (messageLength == 0U) ? 1U
                            : ((messageLength + kCryptoAesBlockSize - 1U) /
                               kCryptoAesBlockSize);
  const bool lastBlockComplete =
      (messageLength != 0U) &&
      ((messageLength % kCryptoAesBlockSize) == 0U);
  const size_t fullBlockCount =
      (blockCount > 0U) ? (blockCount - 1U) : 0U;

  for (size_t index = 0; index < fullBlockCount; ++index) {
    const uint8_t* messageBlock = message + (index * kCryptoAesBlockSize);
    xorAesBlock(state, messageBlock, work);
    error = aesEcbEncryptBlockRaw(key, work, state);
    if (error != OT_ERROR_NONE) {
      goto exit;
    }
  }

  memset(block, 0, sizeof(block));
  if (lastBlockComplete) {
    memcpy(block, message + (fullBlockCount * kCryptoAesBlockSize),
           kCryptoAesBlockSize);
    xorAesBlock(block, subkey1, block);
  } else {
    const size_t tailLength =
        (messageLength == 0U)
            ? 0U
            : (messageLength - (fullBlockCount * kCryptoAesBlockSize));
    if (tailLength != 0U) {
      memcpy(block, message + (fullBlockCount * kCryptoAesBlockSize),
             tailLength);
    }
    block[tailLength] = 0x80U;
    xorAesBlock(block, subkey2, block);
  }

  xorAesBlock(state, block, work);
  error = aesEcbEncryptBlockRaw(key, work, mac);

exit:
  secureZero(zero, sizeof(zero));
  secureZero(subkey1, sizeof(subkey1));
  secureZero(subkey2, sizeof(subkey2));
  secureZero(state, sizeof(state));
  secureZero(block, sizeof(block));
  secureZero(work, sizeof(work));
  return error;
}

otError aesCmacPrf128(const uint8_t* key,
                      size_t keyLength,
                      const uint8_t* message,
                      size_t messageLength,
                      uint8_t mac[kCryptoAesBlockSize]) {
  if ((key == nullptr && keyLength != 0U) ||
      (message == nullptr && messageLength != 0U) || mac == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }

  uint8_t normalizedKey[kCryptoAesBlockSize] = {0};
  otError error = OT_ERROR_NONE;

  if (keyLength == kCryptoAesBlockSize) {
    memcpy(normalizedKey, key, sizeof(normalizedKey));
  } else {
    const uint8_t zeroKey[kCryptoAesBlockSize] = {0};
    error = aesCmacCompute128(zeroKey, key, keyLength, normalizedKey);
    if (error != OT_ERROR_NONE) {
      secureZero(normalizedKey, sizeof(normalizedKey));
      return error;
    }
  }

  error = aesCmacCompute128(normalizedKey, message, messageLength, mac);
  secureZero(normalizedKey, sizeof(normalizedKey));
  return error;
}

otError pbkdf2AesCmacPrf128(const uint8_t* password,
                            uint16_t passwordLength,
                            const uint8_t* salt,
                            uint16_t saltLength,
                            uint32_t iterationCounter,
                            uint16_t keyLength,
                            uint8_t* key) {
  if ((password == nullptr && passwordLength != 0U) ||
      (salt == nullptr && saltLength != 0U) ||
      (key == nullptr && keyLength != 0U) || iterationCounter == 0U ||
      saltLength > OT_CRYPTO_PBDKF2_MAX_SALT_SIZE) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (keyLength == 0U) {
    return OT_ERROR_NONE;
  }

  uint8_t prfInput[OT_CRYPTO_PBDKF2_MAX_SALT_SIZE + 4U] = {0};
  uint8_t u[kCryptoAesBlockSize] = {0};
  uint8_t t[kCryptoAesBlockSize] = {0};
  uint16_t produced = 0U;
  uint32_t blockIndex = 1U;

  if (saltLength != 0U) {
    memcpy(prfInput, salt, saltLength);
  }

  while (produced < keyLength) {
    prfInput[saltLength + 0U] = static_cast<uint8_t>(blockIndex >> 24U);
    prfInput[saltLength + 1U] = static_cast<uint8_t>(blockIndex >> 16U);
    prfInput[saltLength + 2U] = static_cast<uint8_t>(blockIndex >> 8U);
    prfInput[saltLength + 3U] = static_cast<uint8_t>(blockIndex);

    otError error = aesCmacPrf128(password, passwordLength, prfInput,
                                  saltLength + 4U, u);
    if (error != OT_ERROR_NONE) {
      secureZero(prfInput, sizeof(prfInput));
      secureZero(u, sizeof(u));
      secureZero(t, sizeof(t));
      return error;
    }
    memcpy(t, u, sizeof(t));

    for (uint32_t iteration = 1U; iteration < iterationCounter; ++iteration) {
      error = aesCmacPrf128(password, passwordLength, u, sizeof(u), u);
      if (error != OT_ERROR_NONE) {
        secureZero(prfInput, sizeof(prfInput));
        secureZero(u, sizeof(u));
        secureZero(t, sizeof(t));
        return error;
      }
      for (size_t i = 0; i < sizeof(t); ++i) {
        t[i] ^= u[i];
      }
    }

    const uint16_t chunkLength =
        ((keyLength - produced) < kCryptoAesBlockSize)
            ? (keyLength - produced)
            : static_cast<uint16_t>(kCryptoAesBlockSize);
    memcpy(key + produced, t, chunkLength);
    produced += chunkLength;
    ++blockIndex;
  }

  secureZero(prfInput, sizeof(prfInput));
  secureZero(u, sizeof(u));
  secureZero(t, sizeof(t));
  return OT_ERROR_NONE;
}

void recordUnsupportedCrypto(void) {
  ++gOpenThreadPlatformState.snapshot.cryptoUnsupportedCount;
}

bool ensureThreadRadioReady() {
  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  if (state.snapshot.radioBackendReady) {
    return true;
  }

  if (!state.snapshot.radioEnabled) {
    return false;
  }

  if (!state.radio.begin(state.snapshot.radioChannel, state.snapshot.txPowerDbm)) {
    state.snapshot.radioBackendReady = false;
    state.snapshot.radioLastError = OT_ERROR_FAILED;
    setLastLogLine("thread-radio-begin-failed");
    return false;
  }

  state.snapshot.radioBackendReady = true;
  state.snapshot.radioBackendWrappedDirect = true;
  state.radio.setMacDataRequestPendingCallback(threadMacDataRequestPendingCallback,
                                               &state);
  state.radio.setMacFrameReceiveFilterCallback(threadMacReceiveFilterCallback,
                                               &state);
  return true;
}

otRadioState threadRadioIdleState() {
  return gOpenThreadPlatformState.snapshot.radioRxOnWhenIdle
             ? OT_RADIO_STATE_RECEIVE
             : OT_RADIO_STATE_SLEEP;
}

bool threadRadioTransmitBusy() {
  return gOpenThreadPlatformState.radioTxDonePending;
}

bool threadRadioEnergyScanBusy() {
  const OpenThreadPlatformState& state = gOpenThreadPlatformState;
  return state.snapshot.radioEnergyScanPending ||
         state.radioEnergyScanDonePending;
}

bool threadRadioOperationBusy() {
  return threadRadioTransmitBusy() || threadRadioEnergyScanBusy();
}

void applyThreadRadioIdleState() {
  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  if (!state.snapshot.radioEnabled) {
    state.snapshot.radioState = OT_RADIO_STATE_DISABLED;
    return;
  }
  if (threadRadioOperationBusy()) {
    return;
  }
  state.snapshot.radioState = threadRadioIdleState();
}

void clearThreadRadioPendingAsyncState() {
  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  state.radioTxDonePending = false;
  state.radioRxDonePending = false;
  state.radioTxDoneError = OT_ERROR_NONE;
  state.radioTxAckFrameValid = false;
  state.radioEnergyScanDonePending = false;
  state.radioEnergyScanDoneDbm = OT_RADIO_RSSI_INVALID;
  state.snapshot.radioEnergyScanPending = false;
}

void captureThreadRadioReceivedFrame(otInstance* instance,
                                     const ZigbeeFrame& frame) {
  OpenThreadPlatformState& state = gOpenThreadPlatformState;

  ensureRxFrameInitialized();
  state.rxFrame.mLength = frame.length;
  state.rxFrame.mChannel = frame.channel;
  state.rxFrame.mInfo.mRxInfo.mRssi = frame.rssiDbm;
  state.rxFrame.mInfo.mRxInfo.mLqi = 0U;
  state.rxFrame.mInfo.mRxInfo.mAckedWithFramePending = false;
  state.rxFrame.mInfo.mRxInfo.mAckedWithSecEnhAck = false;
  state.rxFrame.mInfo.mRxInfo.mAckFrameCounter = 0U;
  state.rxFrame.mInfo.mRxInfo.mAckKeyId = 0U;
  state.rxFrame.mInfo.mRxInfo.mTimestamp = otPlatRadioGetNow(instance);
  memcpy(state.rxPsdu, frame.psdu, frame.length);

  state.snapshot.radioLastRxLength = frame.length;
  state.snapshot.lastRssiDbm = frame.rssiDbm;
  state.snapshot.radioRxDoneCount++;
  captureThreadMacFrameSummary(state.snapshot, frame.psdu, frame.length, false);
  state.snapshot.radioLastError = OT_ERROR_NONE;
}

void finishThreadRadioRx(otInstance* instance) {
  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  if (!state.radioRxDonePending) {
    return;
  }

  state.radioRxDonePending = false;
  if (state.snapshot.diagModeEnabled) {
    state.snapshot.diagRadioActive = true;
    state.snapshot.diagRxCount++;
    state.snapshot.diagLastRxLength = state.rxFrame.mLength;
    state.snapshot.diagLastRxChannel = state.rxFrame.mChannel;
    state.snapshot.diagLastRxSequence =
        (state.rxFrame.mLength >= 3U && state.rxFrame.mPsdu != nullptr)
            ? state.rxFrame.mPsdu[2]
            : 0U;
    state.snapshot.diagLastRxRssi = state.rxFrame.mInfo.mRxInfo.mRssi;
    otPlatDiagRadioReceived(instance, &state.rxFrame, OT_ERROR_NONE);
    return;
  }

  otPlatRadioReceiveDone(instance, &state.rxFrame, OT_ERROR_NONE);
}

void finishThreadRadioTx(otInstance* instance) {
  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  if (!state.radioTxDonePending) {
    return;
  }

  state.radioTxDonePending = false;
  state.snapshot.radioTxDoneCount++;
  state.snapshot.radioLastError = static_cast<uint8_t>(state.radioTxDoneError);
  if (state.snapshot.diagModeEnabled) {
    state.snapshot.diagRadioActive = true;
    state.snapshot.diagTxCount++;
    state.snapshot.diagLastTxLength = state.txFrame.mLength;
    state.snapshot.diagLastTxSequence =
        (state.txFrame.mLength >= 3U && state.txFrame.mPsdu != nullptr)
            ? state.txFrame.mPsdu[2]
            : 0U;
    state.snapshot.diagLastTxError =
        static_cast<uint8_t>(state.radioTxDoneError);
  }
  applyThreadRadioIdleState();
  otPlatRadioTxDone(instance, &state.txFrame,
                    state.radioTxAckFrameValid ? &state.txAckFrame : nullptr,
                    state.radioTxDoneError);
  state.radioTxAckFrameValid = false;
}

void pollThreadRadioReceive(otInstance* instance) {
  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  if (!state.snapshot.radioEnabled || !state.snapshot.radioBackendReady ||
      state.snapshot.radioState != OT_RADIO_STATE_RECEIVE) {
    return;
  }

  if (!state.radio.receiverArmed() &&
      !state.radio.beginReceive(kThreadRadioPollSpinLimit)) {
    return;
  }

  ZigbeeFrame frame = {};
  state.snapshot.radioReceivePollCount++;
  if (!state.radio.pollReceive(&frame, kThreadRadioPollSpinLimit)) {
    const ZigbeeReceiveDebug rxDebug = state.radio.lastReceiveDebug();
    state.snapshot.radioFilteredCount = rxDebug.filteredCount;
    state.snapshot.radioRxCrcErrorCount = rxDebug.crcErrorCount;
    state.snapshot.radioRxInvalidLengthCount = rxDebug.invalidLengthCount;
    state.snapshot.radioLastRxPhr = rxDebug.lastPhr;
    state.snapshot.radioLastRejectedLength = rxDebug.lastLength;
    return;
  }

  const ZigbeeReceiveDebug rxDebug = state.radio.lastReceiveDebug();
  state.snapshot.radioFilteredCount = rxDebug.filteredCount;
  state.snapshot.radioRxCrcErrorCount = rxDebug.crcErrorCount;
  state.snapshot.radioRxInvalidLengthCount = rxDebug.invalidLengthCount;
  state.snapshot.radioLastRxPhr = rxDebug.lastPhr;
  state.snapshot.radioLastRejectedLength = rxDebug.lastLength;
  captureThreadRadioReceivedFrame(instance, frame);
  state.radioRxDonePending = true;
  finishThreadRadioRx(instance);
}

int8_t convertThreadEnergyScanToDbm(uint8_t edLevel) {
  int16_t dbm = static_cast<int16_t>(kThreadEdRssiOffsetDbm) +
                static_cast<int16_t>(edLevel);
  if (dbm > kThreadEdRssiMaxDbm) {
    dbm = kThreadEdRssiMaxDbm;
  }
  if (dbm < INT8_MIN) {
    dbm = INT8_MIN;
  }
  return static_cast<int8_t>(dbm);
}

void finishThreadRadioEnergyScan(otInstance* instance) {
  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  if (!state.radioEnergyScanDonePending) {
    return;
  }

  state.radioEnergyScanDonePending = false;
  state.snapshot.radioEnergyScanPending = false;
  state.snapshot.radioEnergyScanCount++;
  state.snapshot.lastRssiDbm = state.radioEnergyScanDoneDbm;
  state.snapshot.radioLastError = OT_ERROR_NONE;
  otPlatRadioEnergyScanDone(instance, state.radioEnergyScanDoneDbm);
  applyThreadRadioIdleState();
}

void clearThreadCoreDebugSnapshot(OpenThreadPlatformSkeletonSnapshot& snapshot) {
  snapshot.threadCoreDebugValid = false;
  snapshot.threadAttachInProgress = false;
  snapshot.threadAttachTimerRunning = false;
  snapshot.threadReceivedResponseFromParent = false;
  snapshot.threadAttachState = 0U;
  snapshot.threadAttachMode = 0U;
  snapshot.threadReattachMode = 0U;
  snapshot.threadParentRequestCounter = 0U;
  snapshot.threadChildIdRequestsRemaining = 0U;
  snapshot.threadParentCandidateState = 0U;
  snapshot.threadAttachCounter = 0U;
  snapshot.threadParentCandidateRloc16 = OT_RADIO_INVALID_SHORT_ADDR;
  snapshot.threadAttachTimerRemainingMs = 0U;
  snapshot.threadAttachStateName[0] = '\0';
  snapshot.threadAttachModeName[0] = '\0';
  snapshot.threadReattachModeName[0] = '\0';
  snapshot.threadParentCandidateStateName[0] = '\0';
}

void copyThreadCoreDebugString(char* destination, size_t destinationLength,
                               const char* source) {
  if (destination == nullptr || destinationLength == 0U) {
    return;
  }

  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }

  strncpy(destination, source, destinationLength - 1U);
  destination[destinationLength - 1U] = '\0';
}

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
const char* threadAttachStateToString(uint8_t state) {
  using Mle = ot::Mle::Mle;

  switch (static_cast<Mle::AttachState>(state)) {
    case Mle::kAttachStateIdle:
      return "Idle";
    case Mle::kAttachStateStart:
      return "Start";
    case Mle::kAttachStateParentRequest:
      return "ParentReq";
    case Mle::kAttachStateAnnounce:
      return "Announce";
    case Mle::kAttachStateChildIdRequest:
      return "ChildIdReq";
    default:
      return "Unknown";
  }
}

const char* threadAttachModeToString(uint8_t mode) {
  using Mle = ot::Mle::Mle;

  switch (static_cast<Mle::AttachMode>(mode)) {
    case Mle::kAnyPartition:
      return "AnyPartition";
    case Mle::kSamePartition:
      return "SamePartition";
    case Mle::kBetterPartition:
      return "BetterPartition";
    case Mle::kDowngradeToReed:
      return "DowngradeToReed";
    case Mle::kBetterParent:
      return "BetterParent";
    case Mle::kSelectedParent:
      return "SelectedParent";
    default:
      return "Unknown";
  }
}

const char* threadReattachModeToString(uint8_t mode) {
  using Mle = ot::Mle::Mle;

  switch (static_cast<Mle::ReattachState>(mode)) {
    case Mle::kReattachStop:
      return "Stop";
    case Mle::kReattachActive:
      return "Active";
    case Mle::kReattachPending:
      return "Pending";
    default:
      return "Unknown";
  }
}

void captureThreadCoreDebugSnapshot(otInstance* instance) {
  OpenThreadPlatformSkeletonSnapshot& snapshot = gOpenThreadPlatformState.snapshot;
  clearThreadCoreDebugSnapshot(snapshot);

  if (instance == nullptr) {
    return;
  }

  ot::Instance& instanceRef = ot::AsCoreType(instance);
  ot::Mle::Mle& mle = instanceRef.Get<ot::Mle::Mle>();
  ot::Mle::Mle::AttachDebugInfo debugInfo;
  mle.GetAttachDebugInfo(debugInfo);

  snapshot.threadCoreDebugValid = true;
  snapshot.threadAttachInProgress = debugInfo.mAttaching;
  snapshot.threadAttachTimerRunning = debugInfo.mTimerRunning;
  snapshot.threadReceivedResponseFromParent =
      debugInfo.mReceivedResponseFromParent;
  snapshot.threadAttachState = static_cast<uint8_t>(debugInfo.mAttachState);
  snapshot.threadAttachMode = static_cast<uint8_t>(debugInfo.mAttachMode);
  snapshot.threadReattachMode = static_cast<uint8_t>(debugInfo.mReattachState);
  snapshot.threadParentRequestCounter = debugInfo.mParentRequestCounter;
  snapshot.threadChildIdRequestsRemaining =
      debugInfo.mChildIdRequestsRemaining;
  snapshot.threadParentCandidateState =
      static_cast<uint8_t>(debugInfo.mParentCandidateState);
  snapshot.threadAttachCounter = debugInfo.mAttachCounter;
  snapshot.threadParentCandidateRloc16 = debugInfo.mParentCandidateRloc16;
  snapshot.threadAttachTimerRemainingMs = debugInfo.mTimerRemainingMs;

  copyThreadCoreDebugString(snapshot.threadAttachStateName,
                            sizeof(snapshot.threadAttachStateName),
                            threadAttachStateToString(
                                snapshot.threadAttachState));
  copyThreadCoreDebugString(snapshot.threadAttachModeName,
                            sizeof(snapshot.threadAttachModeName),
                            threadAttachModeToString(
                                snapshot.threadAttachMode));
  copyThreadCoreDebugString(snapshot.threadReattachModeName,
                            sizeof(snapshot.threadReattachModeName),
                            threadReattachModeToString(
                                snapshot.threadReattachMode));
  copyThreadCoreDebugString(snapshot.threadParentCandidateStateName,
                            sizeof(snapshot.threadParentCandidateStateName),
                            ot::Neighbor::StateToString(
                                debugInfo.mParentCandidateState));
}
#else
void captureThreadCoreDebugSnapshot(otInstance*) {
  clearThreadCoreDebugSnapshot(gOpenThreadPlatformState.snapshot);
}
#endif

}  // namespace

void OpenThreadPlatformSkeleton::begin() { otSysInit(0, nullptr); }

void OpenThreadPlatformSkeleton::end() { otSysDeinit(); }

void OpenThreadPlatformSkeleton::process(otInstance* instance) {
  otSysProcessDrivers(instance);
  captureThreadCoreDebugSnapshot(instance);
}

bool OpenThreadPlatformSkeleton::snapshot(OpenThreadPlatformSkeletonSnapshot* outSnapshot) {
  if (outSnapshot == nullptr) {
    return false;
  }
  updateRadioTime();
  *outSnapshot = gOpenThreadPlatformState.snapshot;
  return true;
}

otError OpenThreadPlatformSkeleton::fillEntropy(uint8_t* output, uint16_t outputLength) {
  return otPlatEntropyGet(output, outputLength);
}

otError OpenThreadPlatformSkeleton::writeSetting(uint16_t key,
                                                 const uint8_t* value,
                                                 uint16_t valueLength) {
  return otPlatSettingsSet(nullptr, key, value, valueLength);
}

otError OpenThreadPlatformSkeleton::addSetting(uint16_t key,
                                               const uint8_t* value,
                                               uint16_t valueLength) {
  return otPlatSettingsAdd(nullptr, key, value, valueLength);
}

otError OpenThreadPlatformSkeleton::readSetting(uint16_t key,
                                                int index,
                                                uint8_t* value,
                                                uint16_t* valueLength) {
  return otPlatSettingsGet(nullptr, key, index, value, valueLength);
}

otError OpenThreadPlatformSkeleton::deleteSetting(uint16_t key, int index) {
  return otPlatSettingsDelete(nullptr, key, index);
}

void OpenThreadPlatformSkeleton::wipeSettings() { otPlatSettingsWipe(nullptr); }

}  // namespace xiao_nrf54l15

extern "C" {

void otSysInit(int, char**) {
  using namespace xiao_nrf54l15;

  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  memset(&state.snapshot, 0, sizeof(state.snapshot));
  memset(state.txPsdu, 0, sizeof(state.txPsdu));
  memset(state.txAckPsdu, 0, sizeof(state.txAckPsdu));
  memset(state.rxPsdu, 0, sizeof(state.rxPsdu));
  state.txFrame = {};
  state.txAckFrame = {};
  state.rxFrame = {};
  ensureTxFrameInitialized();
  ensureTxAckFrameInitialized();
  ensureRxFrameInitialized();
  state.radioCallbackInstance = nullptr;
  state.radioTxDonePending = false;
  state.radioTxDoneError = OT_ERROR_NONE;
  state.radioTxAckFrameValid = false;
  state.radioEnergyScanDonePending = false;
  state.radioEnergyScanDoneDbm = OT_RADIO_RSSI_INVALID;
  state.radioSrcMatchEnabled = false;
  clearThreadSrcMatchTables(state);
  resetDiagSnapshot(state);

  state.snapshot.initialized = true;
  state.snapshot.settingsInitialized = true;
  state.snapshot.radioCaps = kThreadRadioCaps;
  state.snapshot.radioState = OT_RADIO_STATE_DISABLED;
  state.snapshot.radioBackendWrappedDirect = true;
  state.snapshot.radioBackendReady = false;
  state.snapshot.radioEnergyScanPending = false;
  state.snapshot.radioLastEdLevel = 0U;
  state.snapshot.receiveSensitivityDbm = -100;
  state.snapshot.txPowerDbm = 0;
  state.snapshot.ccaThresholdDbm = -75;
  state.snapshot.femLnaGainDbm = 0;
  state.snapshot.regionCode = static_cast<uint16_t>('W' << 8U) | static_cast<uint16_t>('W');
  state.snapshot.cslAccuracyPpm = 100;
  state.snapshot.cslUncertainty10us = 4;
  state.snapshot.alternateShortAddress = OT_RADIO_INVALID_SHORT_ADDR;
  state.snapshot.lastRssiDbm = OT_RADIO_RSSI_INVALID;
  state.snapshot.diagLastRxRssi = OT_RADIO_RSSI_INVALID;
  state.snapshot.radioChannel = OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MIN;
  state.cryptoRngReady = false;
  state.nextVolatileKeyRef = 1U;
  state.lastRadioNowLow = micros();
  state.radioNowHigh = 0;
  updateRadioTime();
  resetCryptoState();

  otPlatRadioGetIeeeEui64(nullptr, state.snapshot.extendedAddress.m8);
  closeSettings();
  ensureSettingsOpen();
}

void otSysDeinit(void) {
  using namespace xiao_nrf54l15;

  closeSettings();
  resetCryptoState();
  if (gOpenThreadPlatformState.snapshot.radioBackendReady) {
    gOpenThreadPlatformState.radio.end();
  }
  gOpenThreadPlatformState.snapshot = {};
  gOpenThreadPlatformState.settingsOpen = false;
  gOpenThreadPlatformState.sensitiveKeys = nullptr;
  gOpenThreadPlatformState.lastRadioNowLow = 0;
  gOpenThreadPlatformState.radioNowHigh = 0;
  memset(gOpenThreadPlatformState.txPsdu, 0, sizeof(gOpenThreadPlatformState.txPsdu));
  memset(gOpenThreadPlatformState.txAckPsdu, 0,
         sizeof(gOpenThreadPlatformState.txAckPsdu));
  memset(gOpenThreadPlatformState.rxPsdu, 0, sizeof(gOpenThreadPlatformState.rxPsdu));
  gOpenThreadPlatformState.txFrame = {};
  gOpenThreadPlatformState.txAckFrame = {};
  gOpenThreadPlatformState.rxFrame = {};
  ensureTxFrameInitialized();
  ensureTxAckFrameInitialized();
  ensureRxFrameInitialized();
  gOpenThreadPlatformState.radioCallbackInstance = nullptr;
  gOpenThreadPlatformState.radioTxDonePending = false;
  gOpenThreadPlatformState.radioTxDoneError = OT_ERROR_NONE;
  gOpenThreadPlatformState.radioTxAckFrameValid = false;
  gOpenThreadPlatformState.radioEnergyScanDonePending = false;
  gOpenThreadPlatformState.radioEnergyScanDoneDbm = OT_RADIO_RSSI_INVALID;
  gOpenThreadPlatformState.radioSrcMatchEnabled = false;
  clearThreadSrcMatchTables(gOpenThreadPlatformState);
  gOpenThreadPlatformState.diagCallback = nullptr;
  gOpenThreadPlatformState.diagCallbackContext = nullptr;
  resetDiagSnapshot(gOpenThreadPlatformState);
}

bool otSysPseudoResetWasRequested(void) {
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.pseudoResetRequested;
}

void otSysProcessDrivers(otInstance* instance) {
  using namespace xiao_nrf54l15;

  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  if (instance != nullptr) {
    state.radioCallbackInstance = instance;
  }
  state.snapshot.processCount++;

  bool milliFired = false;
  const uint32_t nowMs = otPlatAlarmMilliGetNow();
  if (state.snapshot.alarmMilliRunning &&
      static_cast<int32_t>(nowMs - state.snapshot.alarmMilliDeadline) >= 0) {
    state.snapshot.alarmMilliRunning = false;
    state.snapshot.alarmMilliFires++;
    milliFired = true;
  }

  bool microFired = false;
  const uint32_t nowUs = otPlatAlarmMicroGetNow();
  if (state.snapshot.alarmMicroRunning &&
      static_cast<int32_t>(nowUs - state.snapshot.alarmMicroDeadline) >= 0) {
    state.snapshot.alarmMicroRunning = false;
    state.snapshot.alarmMicroFires++;
    microFired = true;
  }

  if (state.radioCallbackInstance != nullptr) {
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
    if (milliFired) {
      otPlatAlarmMilliFired(state.radioCallbackInstance);
    }
#if OPENTHREAD_CONFIG_PLATFORM_USEC_TIMER_ENABLE
    if (microFired) {
      otPlatAlarmMicroFired(state.radioCallbackInstance);
    }
#endif
#else
    (void)milliFired;
    (void)microFired;
#endif
  }

  updateRadioTime();
  finishThreadRadioTx(state.radioCallbackInstance);
  finishThreadRadioRx(state.radioCallbackInstance);
  finishThreadRadioEnergyScan(state.radioCallbackInstance);
  pollThreadRadioReceive(state.radioCallbackInstance);

  if (state.radioCallbackInstance != nullptr) {
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
    while (otTaskletsArePending(state.radioCallbackInstance)) {
      state.snapshot.eventPending = false;
      otTaskletsProcess(state.radioCallbackInstance);
      updateRadioTime();
      finishThreadRadioTx(state.radioCallbackInstance);
      finishThreadRadioRx(state.radioCallbackInstance);
      finishThreadRadioEnergyScan(state.radioCallbackInstance);
      pollThreadRadioReceive(state.radioCallbackInstance);
    }
#endif
  }
}

__attribute__((weak)) void otPlatRadioTxStarted(otInstance*, otRadioFrame*) {}

__attribute__((weak)) void otPlatRadioTxDone(otInstance*, otRadioFrame*,
                                             otRadioFrame*, otError) {}

__attribute__((weak)) void otPlatRadioReceiveDone(otInstance*, otRadioFrame*,
                                                  otError) {}

__attribute__((weak)) void otPlatRadioEnergyScanDone(otInstance*, int8_t) {}

void otSysEventSignalPending(void) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.eventPending = true;
}

void otTaskletsSignalPending(otInstance*) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.eventPending = true;
}

void otPlatAlarmMilliStartAt(otInstance*, uint32_t t0, uint32_t dt) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.alarmMilliRunning = true;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.alarmMilliDeadline = t0 + dt;
}

void otPlatAlarmMilliStop(otInstance*) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.alarmMilliRunning = false;
}

uint32_t otPlatAlarmMilliGetNow(void) { return millis(); }

void otPlatAlarmMicroStartAt(otInstance*, uint32_t t0, uint32_t dt) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.alarmMicroRunning = true;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.alarmMicroDeadline = t0 + dt;
}

void otPlatAlarmMicroStop(otInstance*) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.alarmMicroRunning = false;
}

uint32_t otPlatAlarmMicroGetNow(void) { return micros(); }

void* otPlatCAlloc(size_t aNum, size_t aSize) {
  if (aSize != 0U && aNum > (SIZE_MAX / aSize)) {
    return nullptr;
  }
  return calloc(aNum, aSize);
}

void otPlatFree(void* aPtr) { free(aPtr); }

otError otPlatEntropyGet(uint8_t* output, uint16_t outputLength) {
  if ((output == nullptr) && (outputLength != 0)) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::fillPseudoEntropy(output, outputLength);
  return OT_ERROR_NONE;
}

void otPlatLog(otLogLevel, otLogRegion, const char* format, ...) {
  va_list args;
  va_start(args, format);
  xiao_nrf54l15::setLastLogLineV(format, args);
  va_end(args);
}

void otPlatLogOutput(otInstance*, otLogLevel, const char* logLine) {
  xiao_nrf54l15::rememberPlatformLogLine(logLine);
}

void otPlatLogHandleLevelChanged(otLogLevel) {}

void otPlatLogHandleLogLevelChanged(otInstance*, otLogLevel) {}

void otPlatSettingsInit(otInstance*, const uint16_t* sensitiveKeys, uint16_t sensitiveKeysLength) {
  xiao_nrf54l15::ensureSettingsOpen();
  xiao_nrf54l15::gOpenThreadPlatformState.sensitiveKeys = sensitiveKeys;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.settingsInitialized = true;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.sensitiveKeyCount = sensitiveKeysLength;
}

void otPlatSettingsDeinit(otInstance*) {
  xiao_nrf54l15::closeSettings();
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.settingsInitialized = false;
}

otError otPlatSettingsGet(otInstance*, uint16_t key, int index, uint8_t* value, uint16_t* valueLength) {
  if ((index < 0) || (valueLength == nullptr && value != nullptr)) {
    return OT_ERROR_INVALID_ARGS;
  }

  const uint16_t count = xiao_nrf54l15::getSettingCount(key);
  if (static_cast<uint16_t>(index) >= count) {
    return OT_ERROR_NOT_FOUND;
  }

  uint16_t actualLength = (valueLength == nullptr) ? 0 : *valueLength;
  if (!xiao_nrf54l15::readSettingItem(key, index, value, &actualLength)) {
    return OT_ERROR_NOT_FOUND;
  }

  if (valueLength != nullptr) {
    *valueLength = actualLength;
  }

  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.lastSettingsKey = key;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.lastSettingsLength = actualLength;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.settingsKeyCount = count;
  return OT_ERROR_NONE;
}

otError otPlatSettingsSet(otInstance*, uint16_t key, const uint8_t* value, uint16_t valueLength) {
  if ((value == nullptr) && (valueLength != 0)) {
    return OT_ERROR_INVALID_ARGS;
  }

  otPlatSettingsDelete(nullptr, key, -1);
  if (!xiao_nrf54l15::writeSettingItem(key, 0, value, valueLength) ||
      !xiao_nrf54l15::setSettingCount(key, 1)) {
    return OT_ERROR_NO_BUFS;
  }

  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.lastSettingsKey = key;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.lastSettingsLength = valueLength;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.settingsKeyCount = 1;
  return OT_ERROR_NONE;
}

otError otPlatSettingsAdd(otInstance*, uint16_t key, const uint8_t* value, uint16_t valueLength) {
  if ((value == nullptr) && (valueLength != 0)) {
    return OT_ERROR_INVALID_ARGS;
  }

  const uint16_t count = xiao_nrf54l15::getSettingCount(key);
  if (!xiao_nrf54l15::writeSettingItem(key, count, value, valueLength) ||
      !xiao_nrf54l15::setSettingCount(key, count + 1U)) {
    return OT_ERROR_NO_BUFS;
  }

  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.lastSettingsKey = key;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.lastSettingsLength = valueLength;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.settingsKeyCount = count + 1U;
  return OT_ERROR_NONE;
}

otError otPlatSettingsDelete(otInstance*, uint16_t key, int index) {
  const uint16_t count = xiao_nrf54l15::getSettingCount(key);
  if (count == 0) {
    return OT_ERROR_NOT_FOUND;
  }

  if (index < 0) {
    for (uint16_t i = 0; i < count; ++i) {
      xiao_nrf54l15::removeSettingItemKeys(key, i);
    }
    xiao_nrf54l15::setSettingCount(key, 0);
    xiao_nrf54l15::gOpenThreadPlatformState.snapshot.settingsKeyCount = 0;
    return OT_ERROR_NONE;
  }

  if (static_cast<uint16_t>(index) >= count) {
    return OT_ERROR_NOT_FOUND;
  }

  for (uint16_t i = static_cast<uint16_t>(index); i + 1U < count; ++i) {
    if (!xiao_nrf54l15::shiftSettingItem(key, i + 1U, i)) {
      return OT_ERROR_NO_BUFS;
    }
  }

  xiao_nrf54l15::removeSettingItemKeys(key, count - 1U);
  xiao_nrf54l15::setSettingCount(key, count - 1U);
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.settingsKeyCount = count - 1U;
  return OT_ERROR_NONE;
}

void otPlatSettingsWipe(otInstance*) {
  xiao_nrf54l15::ensureSettingsOpen();
  xiao_nrf54l15::gOpenThreadPlatformState.settings.clear();
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.settingsKeyCount = 0;
}

void otPlatCryptoInit(void) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cryptoInitialized = true;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cryptoSupportMask =
      xiao_nrf54l15::kCryptoSupportRandom |
      xiao_nrf54l15::kCryptoSupportAesEcb |
      xiao_nrf54l15::kCryptoSupportKeyRefs |
      xiao_nrf54l15::kCryptoSupportPbkdf2Cmac;
}

otError otPlatCryptoImportKey(otCryptoKeyRef* keyRef,
                              otCryptoKeyType keyType,
                              otCryptoKeyAlgorithm keyAlgorithm,
                              int keyUsage,
                              otCryptoKeyStorage keyPersistence,
                              const uint8_t* key,
                              size_t keyLen) {
  if ((key == nullptr && keyLen != 0U) || keyRef == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (keyLen > xiao_nrf54l15::kCryptoMaxKeyBytes) {
    return OT_ERROR_NO_BUFS;
  }

  otPlatCryptoInit();

  otCryptoKeyRef resolvedRef = 0U;
  if (keyPersistence == OT_CRYPTO_KEY_STORAGE_PERSISTENT) {
    if (*keyRef == 0U) {
      return OT_ERROR_INVALID_ARGS;
    }
    resolvedRef = *keyRef;
  } else {
    resolvedRef = xiao_nrf54l15::allocateVolatileKeyRef();
    if (resolvedRef == 0U) {
      return OT_ERROR_NO_BUFS;
    }
  }

  xiao_nrf54l15::CryptoKeySlot* slot =
      xiao_nrf54l15::allocateCryptoKeySlot(resolvedRef);
  if (slot == nullptr) {
    return OT_ERROR_NO_BUFS;
  }

  xiao_nrf54l15::clearCryptoKeySlot(*slot);
  slot->occupied = true;
  slot->ref = resolvedRef;
  slot->type = keyType;
  slot->algorithm = keyAlgorithm;
  slot->usage = keyUsage;
  slot->storage = keyPersistence;
  slot->length = static_cast<uint16_t>(keyLen);
  if (keyLen != 0U) {
    memcpy(slot->bytes, key, keyLen);
  }

  *keyRef = resolvedRef;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cryptoLastKeyLength =
      static_cast<uint16_t>(keyLen);
  xiao_nrf54l15::refreshCryptoKeySnapshot();
  return OT_ERROR_NONE;
}

otError otPlatCryptoExportKey(otCryptoKeyRef keyRef,
                              uint8_t* buffer,
                              size_t bufferLen,
                              size_t* keyLen) {
  xiao_nrf54l15::CryptoKeySlot* slot =
      xiao_nrf54l15::findCryptoKeySlot(keyRef);
  if (slot == nullptr) {
    return OT_ERROR_NOT_FOUND;
  }
  if (keyLen == nullptr || (buffer == nullptr && slot->length != 0U)) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (bufferLen < slot->length) {
    return OT_ERROR_NO_BUFS;
  }

  if (slot->length != 0U) {
    memcpy(buffer, slot->bytes, slot->length);
  }
  *keyLen = slot->length;
  return OT_ERROR_NONE;
}

otError otPlatCryptoDestroyKey(otCryptoKeyRef keyRef) {
  xiao_nrf54l15::CryptoKeySlot* slot =
      xiao_nrf54l15::findCryptoKeySlot(keyRef);
  if (slot == nullptr) {
    return OT_ERROR_NOT_FOUND;
  }
  xiao_nrf54l15::clearCryptoKeySlot(*slot);
  xiao_nrf54l15::refreshCryptoKeySnapshot();
  return OT_ERROR_NONE;
}

bool otPlatCryptoHasKey(otCryptoKeyRef keyRef) {
  return xiao_nrf54l15::findCryptoKeySlot(keyRef) != nullptr;
}

void* otPlatCryptoCAlloc(size_t num, size_t size) {
  return calloc(num, size);
}

void otPlatCryptoFree(void* ptr) {
  free(ptr);
}

otError otPlatCryptoHmacSha256Init(otCryptoContext* context) {
  if (context == nullptr || context->mContext == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (context->mContextSize < sizeof(xiao_nrf54l15::CryptoHmacSha256Context)) {
    return OT_ERROR_NO_BUFS;
  }
  xiao_nrf54l15::secureZero(context->mContext, context->mContextSize);
  static_cast<xiao_nrf54l15::CryptoHmacSha256Context*>(context->mContext)
      ->header.kind = xiao_nrf54l15::CryptoContextKind::kHmacSha256;
  return OT_ERROR_NONE;
}

otError otPlatCryptoHmacSha256Deinit(otCryptoContext* context) {
  if (context == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (context->mContext != nullptr && context->mContextSize != 0U) {
    xiao_nrf54l15::secureZero(context->mContext, context->mContextSize);
  }
  return OT_ERROR_NONE;
}

otError otPlatCryptoHmacSha256Start(otCryptoContext* context,
                                    const otCryptoKey* key) {
  xiao_nrf54l15::CryptoHmacSha256Context* hmacContext =
      xiao_nrf54l15::getHmacSha256Context(context);
  if (hmacContext == nullptr || key == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }

  uint8_t resolvedKey[xiao_nrf54l15::kCryptoMaxKeyBytes] = {0};
  size_t resolvedKeyLength = 0U;
  otError error = xiao_nrf54l15::resolveKeyMaterial(
      key, resolvedKey, sizeof(resolvedKey), &resolvedKeyLength);
  if (error == OT_ERROR_NONE) {
    error = xiao_nrf54l15::startHmacSha256(*hmacContext, resolvedKey,
                                           resolvedKeyLength);
  }
  xiao_nrf54l15::secureZero(resolvedKey, sizeof(resolvedKey));
  return error;
}

otError otPlatCryptoHmacSha256Update(otCryptoContext* context,
                                     const void* buffer,
                                     uint16_t bufferLength) {
  xiao_nrf54l15::CryptoHmacSha256Context* hmacContext =
      xiao_nrf54l15::getHmacSha256Context(context);
  if (hmacContext == nullptr || (buffer == nullptr && bufferLength != 0U)) {
    return OT_ERROR_INVALID_ARGS;
  }
  return xiao_nrf54l15::sha256UpdateBytes(
      hmacContext->inner, static_cast<const uint8_t*>(buffer), bufferLength);
}

otError otPlatCryptoHmacSha256Finish(otCryptoContext* context,
                                     uint8_t* buffer,
                                     size_t bufferLength) {
  xiao_nrf54l15::CryptoHmacSha256Context* hmacContext =
      xiao_nrf54l15::getHmacSha256Context(context);
  if (hmacContext == nullptr || (buffer == nullptr && bufferLength != 0U)) {
    return OT_ERROR_INVALID_ARGS;
  }
  return xiao_nrf54l15::finishHmacSha256(*hmacContext, buffer, bufferLength);
}

otError otPlatCryptoAesInit(otCryptoContext* context) {
  if (context == nullptr || context->mContext == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (context->mContextSize < sizeof(xiao_nrf54l15::CryptoAesContext)) {
    return OT_ERROR_NO_BUFS;
  }
  xiao_nrf54l15::secureZero(context->mContext, context->mContextSize);
  xiao_nrf54l15::CryptoAesContext* aesContext =
      static_cast<xiao_nrf54l15::CryptoAesContext*>(context->mContext);
  aesContext->header.kind = xiao_nrf54l15::CryptoContextKind::kAes;
  otPlatCryptoInit();
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cryptoAesReady = true;
  return OT_ERROR_NONE;
}

otError otPlatCryptoAesSetKey(otCryptoContext* context,
                              const otCryptoKey* key) {
  xiao_nrf54l15::CryptoAesContext* aesContext =
      xiao_nrf54l15::getAesContext(context);
  if (aesContext == nullptr || key == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }

  uint8_t resolvedKey[16] = {0};
  size_t resolvedKeyLength = 0;
  otError error = xiao_nrf54l15::resolveKeyMaterial(
      key, resolvedKey, sizeof(resolvedKey), &resolvedKeyLength);
  if (error != OT_ERROR_NONE) {
    return error;
  }
  if (resolvedKeyLength != sizeof(aesContext->key)) {
    return OT_ERROR_INVALID_ARGS;
  }

  memcpy(aesContext->key, resolvedKey, sizeof(aesContext->key));
  aesContext->hasKey = true;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cryptoLastKeyLength =
      static_cast<uint16_t>(resolvedKeyLength);
  return OT_ERROR_NONE;
}

otError otPlatCryptoAesEncrypt(otCryptoContext* context,
                               const uint8_t* input,
                               uint8_t* output) {
  xiao_nrf54l15::CryptoAesContext* aesContext =
      xiao_nrf54l15::getAesContext(context);
  if (aesContext == nullptr || !aesContext->hasKey || input == nullptr ||
      output == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }

  xiao_nrf54l15::Ecb ecb;
  if (!ecb.encryptBlock(aesContext->key, input, output)) {
    return OT_ERROR_FAILED;
  }

  ++xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cryptoAesEncryptCount;
  return OT_ERROR_NONE;
}

otError otPlatCryptoAesFree(otCryptoContext* context) {
  if (context == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (context->mContext != nullptr && context->mContextSize != 0U) {
    xiao_nrf54l15::secureZero(context->mContext, context->mContextSize);
  }
  return OT_ERROR_NONE;
}

otError otPlatCryptoHkdfInit(otCryptoContext* context) {
  if (context == nullptr || context->mContext == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (context->mContextSize < sizeof(xiao_nrf54l15::CryptoHkdfContext)) {
    return OT_ERROR_NO_BUFS;
  }
  xiao_nrf54l15::secureZero(context->mContext, context->mContextSize);
  static_cast<xiao_nrf54l15::CryptoHkdfContext*>(context->mContext)
      ->header.kind = xiao_nrf54l15::CryptoContextKind::kHkdfSha256;
  return OT_ERROR_NONE;
}

otError otPlatCryptoHkdfExpand(otCryptoContext* context,
                               const uint8_t* info,
                               uint16_t infoLength,
                               uint8_t* outputKey,
                               uint16_t outputKeyLength) {
  xiao_nrf54l15::CryptoHkdfContext* hkdfContext =
      xiao_nrf54l15::getHkdfContext(context);
  if (hkdfContext == nullptr ||
      (info == nullptr && infoLength != 0U) ||
      (outputKey == nullptr && outputKeyLength != 0U)) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (!hkdfContext->hasPrk) {
    return OT_ERROR_INVALID_STATE;
  }
  if (outputKeyLength > (255U * xiao_nrf54l15::kCryptoSha256HashSize)) {
    return OT_ERROR_INVALID_ARGS;
  }

  uint8_t previous[xiao_nrf54l15::kCryptoSha256HashSize] = {0};
  uint8_t block[xiao_nrf54l15::kCryptoSha256HashSize] = {0};
  size_t generated = 0U;
  size_t previousLength = 0U;
  uint8_t counter = 1U;

  while (generated < outputKeyLength) {
    const otError error = xiao_nrf54l15::hmacSha256Compute(
        hkdfContext->prk, sizeof(hkdfContext->prk),
        previous, previousLength,
        info, infoLength,
        &counter, sizeof(counter),
        block);
    if (error != OT_ERROR_NONE) {
      xiao_nrf54l15::secureZero(previous, sizeof(previous));
      xiao_nrf54l15::secureZero(block, sizeof(block));
      return error;
    }

    const size_t remaining = outputKeyLength - generated;
    const size_t copyLength =
        (remaining < sizeof(block)) ? remaining : sizeof(block);
    memcpy(outputKey + generated, block, copyLength);
    memcpy(previous, block, sizeof(previous));
    previousLength = sizeof(previous);
    generated += copyLength;
    ++counter;
  }

  xiao_nrf54l15::secureZero(previous, sizeof(previous));
  xiao_nrf54l15::secureZero(block, sizeof(block));
  return OT_ERROR_NONE;
}

otError otPlatCryptoHkdfExtract(otCryptoContext* context,
                                const uint8_t* salt,
                                uint16_t saltLength,
                                const otCryptoKey* inputKey) {
  xiao_nrf54l15::CryptoHkdfContext* hkdfContext =
      xiao_nrf54l15::getHkdfContext(context);
  if (hkdfContext == nullptr ||
      (salt == nullptr && saltLength != 0U) ||
      inputKey == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }

  uint8_t resolvedKey[xiao_nrf54l15::kCryptoMaxKeyBytes] = {0};
  size_t resolvedKeyLength = 0U;
  otError error = xiao_nrf54l15::resolveKeyMaterial(
      inputKey, resolvedKey, sizeof(resolvedKey), &resolvedKeyLength);
  if (error != OT_ERROR_NONE) {
    xiao_nrf54l15::secureZero(resolvedKey, sizeof(resolvedKey));
    return error;
  }

  uint8_t zeroSalt[xiao_nrf54l15::kCryptoSha256HashSize] = {0};
  const uint8_t* effectiveSalt = salt;
  size_t effectiveSaltLength = saltLength;
  if (effectiveSalt == nullptr || effectiveSaltLength == 0U) {
    effectiveSalt = zeroSalt;
    effectiveSaltLength = sizeof(zeroSalt);
  }

  error = xiao_nrf54l15::hmacSha256Compute(
      effectiveSalt, effectiveSaltLength,
      resolvedKey, resolvedKeyLength,
      nullptr, 0U,
      nullptr, 0U,
      hkdfContext->prk);
  hkdfContext->hasPrk = (error == OT_ERROR_NONE);
  xiao_nrf54l15::secureZero(resolvedKey, sizeof(resolvedKey));
  xiao_nrf54l15::secureZero(zeroSalt, sizeof(zeroSalt));
  return error;
}

otError otPlatCryptoHkdfDeinit(otCryptoContext* context) {
  if (context == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (context->mContext != nullptr && context->mContextSize != 0U) {
    xiao_nrf54l15::secureZero(context->mContext, context->mContextSize);
  }
  return OT_ERROR_NONE;
}

otError otPlatCryptoSha256Init(otCryptoContext* context) {
  if (context == nullptr || context->mContext == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (context->mContextSize < sizeof(xiao_nrf54l15::CryptoSha256Context)) {
    return OT_ERROR_NO_BUFS;
  }
  xiao_nrf54l15::secureZero(context->mContext, context->mContextSize);
  static_cast<xiao_nrf54l15::CryptoSha256Context*>(context->mContext)
      ->header.kind = xiao_nrf54l15::CryptoContextKind::kSha256;
  return OT_ERROR_NONE;
}

otError otPlatCryptoSha256Deinit(otCryptoContext* context) {
  if (context == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (context->mContext != nullptr && context->mContextSize != 0U) {
    xiao_nrf54l15::secureZero(context->mContext, context->mContextSize);
  }
  return OT_ERROR_NONE;
}

otError otPlatCryptoSha256Start(otCryptoContext* context) {
  xiao_nrf54l15::CryptoSha256Context* shaContext =
      xiao_nrf54l15::getSha256Context(context);
  if (shaContext == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::sha256Reset(*shaContext);
  return OT_ERROR_NONE;
}

otError otPlatCryptoSha256Update(otCryptoContext* context,
                                 const void* buffer,
                                 uint16_t bufferLength) {
  xiao_nrf54l15::CryptoSha256Context* shaContext =
      xiao_nrf54l15::getSha256Context(context);
  if (shaContext == nullptr || (buffer == nullptr && bufferLength != 0U)) {
    return OT_ERROR_INVALID_ARGS;
  }
  return xiao_nrf54l15::sha256UpdateBytes(
      *shaContext, static_cast<const uint8_t*>(buffer), bufferLength);
}

otError otPlatCryptoSha256Finish(otCryptoContext* context,
                                 uint8_t* hash,
                                 uint16_t hashSize) {
  xiao_nrf54l15::CryptoSha256Context* shaContext =
      xiao_nrf54l15::getSha256Context(context);
  if (shaContext == nullptr || (hash == nullptr && hashSize != 0U)) {
    return OT_ERROR_INVALID_ARGS;
  }
  return xiao_nrf54l15::sha256FinishBytes(*shaContext, hash, hashSize);
}

void otPlatCryptoRandomInit(void) {
  otPlatCryptoInit();
  xiao_nrf54l15::gOpenThreadPlatformState.cryptoRngReady =
      xiao_nrf54l15::gOpenThreadPlatformState.cryptoRng.begin();
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cryptoRandomHardware =
      xiao_nrf54l15::gOpenThreadPlatformState.cryptoRngReady;
}

void otPlatCryptoRandomDeinit(void) {
  if (xiao_nrf54l15::gOpenThreadPlatformState.cryptoRngReady) {
    xiao_nrf54l15::gOpenThreadPlatformState.cryptoRng.end();
  }
  xiao_nrf54l15::gOpenThreadPlatformState.cryptoRngReady = false;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cryptoRandomHardware = false;
}

otError otPlatCryptoRandomGet(uint8_t* buffer, uint16_t size) {
  if ((buffer == nullptr) && (size != 0U)) {
    return OT_ERROR_INVALID_ARGS;
  }

  ++xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cryptoRandomRequests;
  if (size == 0U) {
    return OT_ERROR_NONE;
  }

  if (!xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cryptoInitialized) {
    otPlatCryptoInit();
  }

  if (!xiao_nrf54l15::gOpenThreadPlatformState.cryptoRngReady) {
    xiao_nrf54l15::gOpenThreadPlatformState.cryptoRngReady =
        xiao_nrf54l15::gOpenThreadPlatformState.cryptoRng.begin();
  }

  if (xiao_nrf54l15::gOpenThreadPlatformState.cryptoRngReady &&
      xiao_nrf54l15::gOpenThreadPlatformState.cryptoRng.fill(buffer, size)) {
    xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cryptoRandomHardware = true;
    return OT_ERROR_NONE;
  }

  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cryptoRandomHardware = false;
  xiao_nrf54l15::fillPseudoEntropy(buffer, size);
  return OT_ERROR_NONE;
}

#if NRF54L15_CLEAN_OPENTHREAD_PAL_UNSUPPORTED_CRYPTO_STUBS
otError otPlatCryptoEcdsaGenerateKey(otPlatCryptoEcdsaKeyPair* keyPair) {
  if (keyPair == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoEcdsaGetPublicKey(
    const otPlatCryptoEcdsaKeyPair* keyPair,
    otPlatCryptoEcdsaPublicKey* publicKey) {
  if (keyPair == nullptr || publicKey == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoEcdsaSign(const otPlatCryptoEcdsaKeyPair* keyPair,
                              const otPlatCryptoSha256Hash* hash,
                              otPlatCryptoEcdsaSignature* signature) {
  if (keyPair == nullptr || hash == nullptr || signature == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoEcdsaVerify(
    const otPlatCryptoEcdsaPublicKey* publicKey,
    const otPlatCryptoSha256Hash* hash,
    const otPlatCryptoEcdsaSignature* signature) {
  if (publicKey == nullptr || hash == nullptr || signature == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoEcdsaSignUsingKeyRef(otCryptoKeyRef keyRef,
                                         const otPlatCryptoSha256Hash* hash,
                                         otPlatCryptoEcdsaSignature* signature) {
  if (keyRef == 0U || hash == nullptr || signature == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoEcdsaExportPublicKey(otCryptoKeyRef keyRef,
                                         otPlatCryptoEcdsaPublicKey* publicKey) {
  if (keyRef == 0U || publicKey == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoEcdsaGenerateAndImportKey(otCryptoKeyRef keyRef) {
  if (keyRef == 0U) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoEcdsaVerifyUsingKeyRef(
    otCryptoKeyRef keyRef,
    const otPlatCryptoSha256Hash* hash,
    const otPlatCryptoEcdsaSignature* signature) {
  if (keyRef == 0U || hash == nullptr || signature == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoPbkdf2GenerateKey(const uint8_t* password,
                                      uint16_t passwordLen,
                                      const uint8_t* salt,
                                      uint16_t saltLen,
                                      uint32_t iterationCounter,
                                      uint16_t keyLen,
                                      uint8_t* key) {
  return xiao_nrf54l15::pbkdf2AesCmacPrf128(password, passwordLen, salt,
                                            saltLen, iterationCounter, keyLen,
                                            key);
}
#endif

otRadioCaps otPlatRadioGetCaps(otInstance*) {
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioCaps;
}

const char* otPlatRadioGetVersionString(otInstance*) { return xiao_nrf54l15::kVersionString; }

int8_t otPlatRadioGetReceiveSensitivity(otInstance*) {
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.receiveSensitivityDbm;
}

void otPlatRadioGetIeeeEui64(otInstance*, uint8_t* ieeeEui64) {
  if (ieeeEui64 == nullptr) {
    return;
  }

  const uint64_t eui = xiao_nrf54l15::zigbeeFactoryEui64();
  for (uint8_t i = 0; i < OT_EXT_ADDRESS_SIZE; ++i) {
    ieeeEui64[i] = static_cast<uint8_t>((eui >> (i * 8U)) & 0xFFU);
  }
}

void otPlatRadioSetPanId(otInstance*, otPanId panId) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.panId = panId;
}

void otPlatRadioSetExtendedAddress(otInstance*, const otExtAddress* extAddress) {
  if (extAddress != nullptr) {
    xiao_nrf54l15::gOpenThreadPlatformState.snapshot.extendedAddress = *extAddress;
  }
}

void otPlatRadioSetShortAddress(otInstance*, otShortAddress shortAddress) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.shortAddress = shortAddress;
}

void otPlatRadioSetAlternateShortAddress(otInstance*, otShortAddress shortAddress) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.alternateShortAddress = shortAddress;
}

otError otPlatRadioGetTransmitPower(otInstance*, int8_t* power) {
  if (power == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  *power = xiao_nrf54l15::gOpenThreadPlatformState.snapshot.txPowerDbm;
  return OT_ERROR_NONE;
}

otError otPlatRadioSetTransmitPower(otInstance*, int8_t power) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.txPowerDbm = power;
  if (xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioBackendReady &&
      !xiao_nrf54l15::gOpenThreadPlatformState.radio.setTxPowerDbm(power)) {
    return OT_ERROR_FAILED;
  }
  return OT_ERROR_NONE;
}

otError otPlatRadioGetCcaEnergyDetectThreshold(otInstance*, int8_t* threshold) {
  if (threshold == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  *threshold = xiao_nrf54l15::gOpenThreadPlatformState.snapshot.ccaThresholdDbm;
  return OT_ERROR_NONE;
}

otError otPlatRadioSetCcaEnergyDetectThreshold(otInstance*, int8_t threshold) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.ccaThresholdDbm = threshold;
  return OT_ERROR_NONE;
}

otError otPlatRadioGetFemLnaGain(otInstance*, int8_t* gain) {
  if (gain == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  *gain = xiao_nrf54l15::gOpenThreadPlatformState.snapshot.femLnaGainDbm;
  return OT_ERROR_NONE;
}

otError otPlatRadioSetFemLnaGain(otInstance*, int8_t gain) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.femLnaGainDbm = gain;
  return OT_ERROR_NONE;
}

bool otPlatRadioGetPromiscuous(otInstance*) {
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioPromiscuous;
}

void otPlatRadioSetPromiscuous(otInstance*, bool enable) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioPromiscuous = enable;
}

void otPlatRadioSetRxOnWhenIdle(otInstance*, bool enable) {
  xiao_nrf54l15::OpenThreadPlatformState& state =
      xiao_nrf54l15::gOpenThreadPlatformState;
  state.snapshot.radioRxOnWhenIdle = enable;
  if (state.snapshot.radioEnabled &&
      state.snapshot.radioState != OT_RADIO_STATE_DISABLED) {
    xiao_nrf54l15::applyThreadRadioIdleState();
  }
}

void otPlatRadioSetMacKey(otInstance*, uint8_t, uint8_t,
                          const otMacKeyMaterial*, const otMacKeyMaterial*,
                          const otMacKeyMaterial*, otRadioKeyType) {}

void otPlatRadioSetMacFrameCounter(otInstance*, uint32_t) {}

void otPlatRadioSetMacFrameCounterIfLarger(otInstance*, uint32_t) {}

uint64_t otPlatRadioGetNow(otInstance*) {
  xiao_nrf54l15::updateRadioTime();
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioNowUs;
}

uint32_t otPlatRadioGetBusSpeed(otInstance*) { return 0; }

uint32_t otPlatRadioGetBusLatency(otInstance*) { return 0; }

otRadioState otPlatRadioGetState(otInstance*) {
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioState;
}

otError otPlatRadioEnable(otInstance*) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioEnabled = true;
  if (!xiao_nrf54l15::ensureThreadRadioReady()) {
    xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioEnabled = false;
    xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioState =
        OT_RADIO_STATE_DISABLED;
    return OT_ERROR_FAILED;
  }
  xiao_nrf54l15::applyThreadRadioIdleState();
  return OT_ERROR_NONE;
}

otError otPlatRadioDisable(otInstance*) {
  xiao_nrf54l15::OpenThreadPlatformState& state =
      xiao_nrf54l15::gOpenThreadPlatformState;
  if (!state.snapshot.radioEnabled) {
    return OT_ERROR_NONE;
  }
  if (state.snapshot.radioState != OT_RADIO_STATE_SLEEP ||
      xiao_nrf54l15::threadRadioOperationBusy()) {
    return OT_ERROR_INVALID_STATE;
  }
  if (state.snapshot.radioBackendReady) {
    state.radio.cancelReceive();
    state.radio.end();
  }
  xiao_nrf54l15::clearThreadRadioPendingAsyncState();
  state.snapshot.radioBackendReady = false;
  state.snapshot.radioEnabled = false;
  state.snapshot.radioState = OT_RADIO_STATE_DISABLED;
  return OT_ERROR_NONE;
}

bool otPlatRadioIsEnabled(otInstance*) {
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioEnabled;
}

otError otPlatRadioSleep(otInstance*) {
  xiao_nrf54l15::OpenThreadPlatformState& state =
      xiao_nrf54l15::gOpenThreadPlatformState;
  if (!state.snapshot.radioEnabled) {
    return OT_ERROR_INVALID_STATE;
  }
  if (state.snapshot.radioState == OT_RADIO_STATE_TRANSMIT ||
      xiao_nrf54l15::threadRadioOperationBusy()) {
    return OT_ERROR_BUSY;
  }
  state.radio.cancelReceive();
  state.snapshot.radioState = OT_RADIO_STATE_SLEEP;
  return OT_ERROR_NONE;
}

otError otPlatRadioReceive(otInstance*, uint8_t channel) {
  xiao_nrf54l15::OpenThreadPlatformState& state =
      xiao_nrf54l15::gOpenThreadPlatformState;
  if (!state.snapshot.radioEnabled || !xiao_nrf54l15::ensureThreadRadioReady()) {
    return OT_ERROR_INVALID_STATE;
  }
  if (state.snapshot.radioState == OT_RADIO_STATE_TRANSMIT ||
      xiao_nrf54l15::threadRadioOperationBusy()) {
    return OT_ERROR_INVALID_STATE;
  }
  if (!state.radio.setChannel(channel)) {
    return OT_ERROR_INVALID_ARGS;
  }
  state.snapshot.radioChannel = channel;
  state.snapshot.radioState = OT_RADIO_STATE_RECEIVE;
  state.snapshot.radioLastError = OT_ERROR_NONE;
  if (!state.radio.beginReceive(xiao_nrf54l15::kThreadRadioPollSpinLimit)) {
    state.snapshot.radioState = xiao_nrf54l15::threadRadioIdleState();
    state.snapshot.radioLastError = OT_ERROR_FAILED;
    return OT_ERROR_FAILED;
  }
  return OT_ERROR_NONE;
}

otError otPlatRadioReceiveAt(otInstance*, uint8_t channel, uint32_t, uint32_t) {
  return otPlatRadioReceive(nullptr, channel);
}

otRadioFrame* otPlatRadioGetTransmitBuffer(otInstance*) {
  xiao_nrf54l15::ensureTxFrameInitialized();
  return &xiao_nrf54l15::gOpenThreadPlatformState.txFrame;
}

otError otPlatRadioTransmit(otInstance* instance, otRadioFrame* frame) {
  xiao_nrf54l15::OpenThreadPlatformState& state =
      xiao_nrf54l15::gOpenThreadPlatformState;
  if (!state.snapshot.radioEnabled || !xiao_nrf54l15::ensureThreadRadioReady()) {
    return OT_ERROR_INVALID_STATE;
  }
  if (frame == nullptr || frame->mPsdu == nullptr || frame->mLength == 0U ||
      frame->mLength > 125U) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (state.snapshot.radioState != OT_RADIO_STATE_RECEIVE ||
      xiao_nrf54l15::threadRadioOperationBusy()) {
    return OT_ERROR_INVALID_STATE;
  }

  if (frame->mChannel < OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MIN ||
      frame->mChannel > OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MAX ||
      !state.radio.setChannel(frame->mChannel)) {
    return OT_ERROR_INVALID_ARGS;
  }

  if (frame->mInfo.mTxInfo.mTxPower != OT_RADIO_POWER_INVALID) {
    state.snapshot.txPowerDbm = frame->mInfo.mTxInfo.mTxPower;
    if (!state.radio.setTxPowerDbm(frame->mInfo.mTxInfo.mTxPower)) {
      return OT_ERROR_FAILED;
    }
  } else if (!state.radio.setTxPowerDbm(state.snapshot.txPowerDbm)) {
    return OT_ERROR_FAILED;
  }

  state.radioCallbackInstance = instance;
  frame->mInfo.mTxInfo.mTimestamp = otPlatRadioGetNow(instance);
  otPlatRadioTxStarted(instance, frame);
  state.snapshot.radioState = OT_RADIO_STATE_TRANSMIT;
  state.snapshot.radioChannel = frame->mChannel;
  state.snapshot.txRequestCount++;
  state.snapshot.radioLastTxLength = static_cast<uint8_t>(frame->mLength);
  xiao_nrf54l15::captureThreadMacFrameSummary(
      state.snapshot, frame->mPsdu, static_cast<uint8_t>(frame->mLength), true);
  state.snapshot.radioLastError = OT_ERROR_NONE;
  xiao_nrf54l15::ZigbeeFrame followupFrame = {};
  bool followupFrameValid = false;
  bool txOk = false;
  if (state.snapshot.radioRxOnWhenIdle) {
    followupFrameValid = state.radio.transmitThenReceive(
        frame->mPsdu, static_cast<uint8_t>(frame->mLength), &followupFrame,
        xiao_nrf54l15::kThreadRadioPollWindowUs,
        frame->mInfo.mTxInfo.mCsmaCaEnabled,
        xiao_nrf54l15::kThreadRadioPollSpinLimit);
    const xiao_nrf54l15::ZigbeeTransmitDebug txDebug =
        state.radio.lastTransmitDebug();
    txOk = txDebug.endSeen && txDebug.disabledSeen &&
           (!txDebug.ackRequested || txDebug.ackReceived);
  } else {
    txOk = state.radio.transmit(
        frame->mPsdu, static_cast<uint8_t>(frame->mLength),
        frame->mInfo.mTxInfo.mCsmaCaEnabled);
  }
  const xiao_nrf54l15::ZigbeeTransmitDebug txDebug = state.radio.lastTransmitDebug();
  state.snapshot.radioLastTxAcked =
      txDebug.ackRequested && txDebug.ackReceived;
  state.snapshot.radioLastTxAckFramePending =
      txDebug.ackRequested && txDebug.ackReceived && txDebug.ackFramePending;
  state.snapshot.radioLastTxAckLength =
      (txDebug.ackRequested && txDebug.ackReceived) ? txDebug.rxLength : 0U;
  state.radioTxAckFrameValid = false;
  if (txDebug.ackRequested && txDebug.ackReceived) {
    uint8_t ackLength = 0U;
    const uint8_t ackSequence = txDebug.ackSequence;
    if (xiao_nrf54l15::ZigbeeRadio::buildMacAcknowledgement(
            ackSequence, state.txAckPsdu,
            &ackLength, txDebug.ackFramePending)) {
      state.txAckFrame = {};
      xiao_nrf54l15::ensureTxAckFrameInitialized();
      state.txAckFrame.mLength = ackLength;
      state.txAckFrame.mChannel = frame->mChannel;
      state.txAckFrame.mInfo.mRxInfo.mTimestamp = otPlatRadioGetNow(instance);
      state.txAckFrame.mInfo.mRxInfo.mRssi = OT_RADIO_RSSI_INVALID;
      state.txAckFrame.mInfo.mRxInfo.mLqi = 0U;
      state.txAckFrame.mInfo.mRxInfo.mAckedWithFramePending = false;
      state.txAckFrame.mInfo.mRxInfo.mAckedWithSecEnhAck = false;
      state.txAckFrame.mInfo.mRxInfo.mAckFrameCounter = 0U;
      state.txAckFrame.mInfo.mRxInfo.mAckKeyId = 0U;
      state.radioTxAckFrameValid = true;
    }
  }
  if (followupFrameValid) {
    xiao_nrf54l15::captureThreadRadioReceivedFrame(instance, followupFrame);
    state.radioRxDonePending = true;
  }
  if (state.snapshot.radioRxOnWhenIdle) {
    (void)state.radio.beginReceive(xiao_nrf54l15::kThreadRadioPollSpinLimit);
  }
  state.radioTxDoneError =
      txOk ? OT_ERROR_NONE
           : (txDebug.ccaBusy
                  ? OT_ERROR_CHANNEL_ACCESS_FAILURE
                  : ((txDebug.ackRequested && !txDebug.ackReceived &&
                      txDebug.endSeen && txDebug.disabledSeen)
                         ? OT_ERROR_NO_ACK
                         : OT_ERROR_ABORT));
  state.radioTxDonePending = true;
  return OT_ERROR_NONE;
}

int8_t otPlatRadioGetRssi(otInstance*) {
  const xiao_nrf54l15::OpenThreadPlatformState& state =
      xiao_nrf54l15::gOpenThreadPlatformState;
  if (state.snapshot.radioState != OT_RADIO_STATE_RECEIVE ||
      xiao_nrf54l15::threadRadioOperationBusy()) {
    return OT_RADIO_RSSI_INVALID;
  }
  return state.snapshot.lastRssiDbm;
}

otError otPlatRadioEnergyScan(otInstance*, uint8_t channel, uint16_t) {
  using namespace xiao_nrf54l15;

  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  if (!state.snapshot.radioEnabled) {
    return OT_ERROR_INVALID_STATE;
  }
  if (state.snapshot.radioState == OT_RADIO_STATE_TRANSMIT ||
      threadRadioEnergyScanBusy() || threadRadioTransmitBusy()) {
    return OT_ERROR_BUSY;
  }
  if (channel < OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MIN ||
      channel > OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MAX) {
    return OT_ERROR_INVALID_ARGS;
  }
  if (!ensureThreadRadioReady()) {
    return OT_ERROR_FAILED;
  }
  if (!state.radio.setChannel(channel)) {
    state.snapshot.radioLastError = OT_ERROR_INVALID_ARGS;
    return OT_ERROR_INVALID_ARGS;
  }

  uint8_t edLevel = 0U;
  if (!state.radio.sampleEnergyDetect(&edLevel, kThreadRadioPollSpinLimit)) {
    state.snapshot.radioLastError = OT_ERROR_FAILED;
    state.snapshot.radioEnergyScanPending = false;
    return OT_ERROR_FAILED;
  }

  state.snapshot.radioChannel = channel;
  state.snapshot.radioLastEdLevel = edLevel;
  state.snapshot.radioEnergyScanPending = true;
  state.radioEnergyScanDoneDbm = convertThreadEnergyScanToDbm(edLevel);
  state.radioEnergyScanDonePending = true;
  return OT_ERROR_NONE;
}

void otPlatRadioEnableSrcMatch(otInstance*, bool enable) {
  xiao_nrf54l15::gOpenThreadPlatformState.radioSrcMatchEnabled = enable;
  xiao_nrf54l15::refreshThreadSrcMatchSnapshot(
      xiao_nrf54l15::gOpenThreadPlatformState);
}

otError otPlatRadioAddSrcMatchShortEntry(otInstance*, otShortAddress shortAddress) {
  return xiao_nrf54l15::addThreadSrcMatchShort(
      xiao_nrf54l15::gOpenThreadPlatformState, shortAddress);
}

otError otPlatRadioAddSrcMatchExtEntry(otInstance*, const otExtAddress* extAddress) {
  return xiao_nrf54l15::addThreadSrcMatchExt(
      xiao_nrf54l15::gOpenThreadPlatformState, extAddress);
}

otError otPlatRadioClearSrcMatchShortEntry(otInstance*, otShortAddress shortAddress) {
  return xiao_nrf54l15::clearThreadSrcMatchShort(
      xiao_nrf54l15::gOpenThreadPlatformState, shortAddress);
}

otError otPlatRadioClearSrcMatchExtEntry(otInstance*, const otExtAddress* extAddress) {
  return xiao_nrf54l15::clearThreadSrcMatchExt(
      xiao_nrf54l15::gOpenThreadPlatformState, extAddress);
}

void otPlatRadioClearSrcMatchShortEntries(otInstance*) {
  for (xiao_nrf54l15::ThreadSrcMatchShortEntry& entry :
       xiao_nrf54l15::gOpenThreadPlatformState.srcMatchShort) {
    entry.occupied = false;
    entry.address = OT_RADIO_INVALID_SHORT_ADDR;
  }
  xiao_nrf54l15::refreshThreadSrcMatchSnapshot(
      xiao_nrf54l15::gOpenThreadPlatformState);
}

void otPlatRadioClearSrcMatchExtEntries(otInstance*) {
  for (xiao_nrf54l15::ThreadSrcMatchExtEntry& entry :
       xiao_nrf54l15::gOpenThreadPlatformState.srcMatchExt) {
    entry.occupied = false;
    memset(entry.address.m8, 0, sizeof(entry.address.m8));
  }
  xiao_nrf54l15::refreshThreadSrcMatchSnapshot(
      xiao_nrf54l15::gOpenThreadPlatformState);
}

uint32_t otPlatRadioGetSupportedChannelMask(otInstance*) { return OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MASK; }

uint32_t otPlatRadioGetPreferredChannelMask(otInstance*) { return OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MASK; }

otError otPlatRadioSetCoexEnabled(otInstance*, bool) { return OT_ERROR_NOT_IMPLEMENTED; }

bool otPlatRadioIsCoexEnabled(otInstance*) { return false; }

otError otPlatRadioGetCoexMetrics(otInstance*, otRadioCoexMetrics* coexMetrics) {
  if (coexMetrics == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  memset(coexMetrics, 0, sizeof(*coexMetrics));
  return OT_ERROR_NONE;
}

otError otPlatRadioEnableCsl(otInstance*, uint32_t, otShortAddress, const otExtAddress*) {
  return OT_ERROR_NOT_IMPLEMENTED;
}

otError otPlatRadioResetCsl(otInstance*) { return OT_ERROR_NOT_IMPLEMENTED; }

void otPlatRadioUpdateCslSampleTime(otInstance*, uint32_t) {}

uint8_t otPlatRadioGetCslAccuracy(otInstance*) {
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cslAccuracyPpm;
}

uint8_t otPlatRadioGetCslUncertainty(otInstance*) {
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.cslUncertainty10us;
}

otError otPlatRadioSetChannelMaxTransmitPower(otInstance*, uint8_t, int8_t) {
  return OT_ERROR_NOT_IMPLEMENTED;
}

otError otPlatRadioSetRegion(otInstance*, uint16_t regionCode) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.regionCode = regionCode;
  return OT_ERROR_NONE;
}

otError otPlatRadioGetRegion(otInstance*, uint16_t* regionCode) {
  if (regionCode == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  *regionCode = xiao_nrf54l15::gOpenThreadPlatformState.snapshot.regionCode;
  return OT_ERROR_NONE;
}

otError otPlatRadioConfigureEnhAckProbing(otInstance*, otLinkMetrics, otShortAddress, const otExtAddress*) {
  return OT_ERROR_NOT_IMPLEMENTED;
}

otError otPlatRadioAddCalibratedPower(otInstance*, uint8_t, int16_t, const uint8_t*, uint16_t) {
  return OT_ERROR_NOT_IMPLEMENTED;
}

otError otPlatRadioClearCalibratedPowers(otInstance*) { return OT_ERROR_NOT_IMPLEMENTED; }

otError otPlatRadioSetChannelTargetPower(otInstance*, uint8_t, int16_t) {
  return OT_ERROR_NOT_IMPLEMENTED;
}

otError otPlatRadioGetRawPowerSetting(otInstance*, uint8_t, uint8_t*, uint16_t*) {
  return OT_ERROR_NOT_FOUND;
}

void otPlatDiagSetOutputCallback(otInstance*, otPlatDiagOutputCallback callback, void* context) {
  xiao_nrf54l15::gOpenThreadPlatformState.diagCallback = callback;
  xiao_nrf54l15::gOpenThreadPlatformState.diagCallbackContext = context;
}

otError otPlatDiagProcess(otInstance* instance, uint8_t argc, char** argv) {
  using namespace xiao_nrf54l15;

  gOpenThreadPlatformState.snapshot.diagProcessCount++;
  if (argc == 0U || argv == nullptr || argv[0] == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }

  const char* command = argv[0];
  if (strcmp(command, "start") == 0) {
    const otError error = startDiagRadio(instance);
    if (error == OT_ERROR_NONE) {
      emitDiagOutput("diag start ch=%u power=%d short=0x%04x pan=0x%04x\n",
                     gOpenThreadPlatformState.snapshot.radioChannel,
                     gOpenThreadPlatformState.snapshot.txPowerDbm,
                     gOpenThreadPlatformState.snapshot.shortAddress,
                     gOpenThreadPlatformState.snapshot.panId);
    }
    return error;
  }

  if (strcmp(command, "stop") == 0) {
    stopDiagRadio(instance);
    emitDiagOutput("diag stop\n");
    return OT_ERROR_NONE;
  }

  if (strcmp(command, "stats") == 0) {
    emitDiagStats();
    return OT_ERROR_NONE;
  }

  if (strcmp(command, "version") == 0) {
    emitDiagOutput("diag version %s\n", kVersionString);
    return OT_ERROR_NONE;
  }

  if (strcmp(command, "channel") == 0) {
    long channelValue = 0;
    if (argc < 2U || !parseLongArg(argv[1], &channelValue) ||
        channelValue < OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MIN ||
        channelValue > OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MAX) {
      return OT_ERROR_INVALID_ARGS;
    }

    otPlatDiagChannelSet(static_cast<uint8_t>(channelValue));
    if (gOpenThreadPlatformState.snapshot.diagModeEnabled) {
      const otError error =
          otPlatRadioReceive(instance, static_cast<uint8_t>(channelValue));
      if (error != OT_ERROR_NONE) {
        return error;
      }
    }
    emitDiagOutput("diag channel %ld\n", channelValue);
    return OT_ERROR_NONE;
  }

  if (strcmp(command, "power") == 0) {
    long powerValue = 0;
    if (argc < 2U || !parseLongArg(argv[1], &powerValue) ||
        powerValue < -128L || powerValue > 127L) {
      return OT_ERROR_INVALID_ARGS;
    }

    otPlatDiagTxPowerSet(static_cast<int8_t>(powerValue));
    emitDiagOutput("diag power %ld\n", powerValue);
    return OT_ERROR_NONE;
  }

  if (strcmp(command, "send") == 0) {
    long payloadLength = 0;
    long pattern = kDiagDefaultPayloadPattern;
    if (argc < 2U || !parseLongArg(argv[1], &payloadLength) ||
        payloadLength <= 0L || payloadLength > kDiagMaxPayloadLength) {
      return OT_ERROR_INVALID_ARGS;
    }
    if (argc >= 3U) {
      if (!parseLongArg(argv[2], &pattern) || pattern < 0L || pattern > 255L) {
        return OT_ERROR_INVALID_ARGS;
      }
    }

    const otError error = sendDiagFrame(
        instance, static_cast<uint8_t>(payloadLength),
        static_cast<uint8_t>(pattern));
    if (error == OT_ERROR_NONE) {
      emitDiagOutput("diag send len=%ld pattern=0x%02lx\n", payloadLength,
                     pattern & 0xFFL);
    }
    return error;
  }

  return OT_ERROR_INVALID_COMMAND;
}

void otPlatDiagModeSet(bool mode) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.diagModeEnabled = mode;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.diagRadioActive = mode;
}

bool otPlatDiagModeGet(void) {
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.diagModeEnabled;
}

void otPlatDiagChannelSet(uint8_t channel) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioChannel = channel;
  if (xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioBackendReady) {
    (void)xiao_nrf54l15::gOpenThreadPlatformState.radio.setChannel(channel);
  }
}

void otPlatDiagTxPowerSet(int8_t txPower) {
  (void)otPlatRadioSetTransmitPower(nullptr, txPower);
}

__attribute__((weak)) void otPlatDiagRadioReceived(otInstance*, otRadioFrame*,
                                                   otError) {}

__attribute__((weak)) void otPlatDiagAlarmCallback(otInstance*) {}

otError otPlatDiagGpioSet(uint32_t, bool) { return OT_ERROR_NOT_IMPLEMENTED; }

otError otPlatDiagGpioGet(uint32_t, bool*) { return OT_ERROR_NOT_IMPLEMENTED; }

otError otPlatDiagGpioSetMode(uint32_t, otGpioMode) { return OT_ERROR_NOT_IMPLEMENTED; }

otError otPlatDiagGpioGetMode(uint32_t, otGpioMode*) { return OT_ERROR_NOT_IMPLEMENTED; }

otError otPlatDiagRadioSetRawPowerSetting(otInstance*, const uint8_t*, uint16_t) {
  return OT_ERROR_NOT_IMPLEMENTED;
}

otError otPlatDiagRadioGetRawPowerSetting(otInstance*, uint8_t*, uint16_t*) {
  return OT_ERROR_NOT_IMPLEMENTED;
}

otError otPlatDiagRadioRawPowerSettingEnable(otInstance*, bool) {
  return OT_ERROR_NOT_IMPLEMENTED;
}

}  // extern "C"
