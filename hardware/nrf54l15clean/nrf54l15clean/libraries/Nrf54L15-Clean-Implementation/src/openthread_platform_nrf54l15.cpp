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
#include <openthread/platform/settings.h>
#include <openthread/platform/radio.h>
#include <openthread-system.h>

namespace xiao_nrf54l15 {
namespace {

static const char* kSettingsNamespace = "otplat";
static const char* kVersionString = "nrf54l15-thread-skel-0";
constexpr uint32_t kCryptoSupportRandom = 1UL << 0U;
constexpr uint32_t kCryptoSupportAesEcb = 1UL << 1U;
constexpr uint32_t kCryptoSupportKeyRefs = 1UL << 2U;
constexpr size_t kCryptoMaxKeyBytes = OT_CRYPTO_ECDSA_MAX_DER_SIZE;
constexpr size_t kCryptoKeySlotCount = 8U;

enum class CryptoContextKind : uint8_t {
  kNone = 0U,
  kAes = 1U,
};

struct CryptoContextHeader {
  CryptoContextKind kind = CryptoContextKind::kNone;
};

struct CryptoAesContext {
  CryptoContextHeader header = {};
  bool hasKey = false;
  uint8_t key[16] = {0};
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

struct OpenThreadPlatformState {
  OpenThreadPlatformSkeletonSnapshot snapshot;
  Preferences settings;
  bool settingsOpen = false;
  const uint16_t* sensitiveKeys = nullptr;

  uint32_t lastRadioNowLow = 0;
  uint64_t radioNowHigh = 0;

  uint8_t txPsdu[OT_RADIO_FRAME_MAX_SIZE] = {0};
  otRadioFrame txFrame = {};

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
  char dataKey[20];
  makeLengthKey(key, index, lengthKey, sizeof(lengthKey));
  makeDataKey(key, index, dataKey, sizeof(dataKey));

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
  gOpenThreadPlatformState.settings.getBytes(dataKey, value, copyLength);
  return true;
}

bool writeSettingItem(uint16_t key, int index, const uint8_t* value, uint16_t valueLength) {
  ensureSettingsOpen();

  char lengthKey[20];
  char dataKey[20];
  makeLengthKey(key, index, lengthKey, sizeof(lengthKey));
  makeDataKey(key, index, dataKey, sizeof(dataKey));

  if (gOpenThreadPlatformState.settings.putUShort(lengthKey, valueLength) != sizeof(uint16_t)) {
    return false;
  }

  if (valueLength == 0) {
    gOpenThreadPlatformState.settings.remove(dataKey);
    return true;
  }

  return gOpenThreadPlatformState.settings.putBytes(dataKey, value, valueLength) == valueLength;
}

void removeSettingItemKeys(uint16_t key, int index) {
  ensureSettingsOpen();

  char lengthKey[20];
  char dataKey[20];
  makeLengthKey(key, index, lengthKey, sizeof(lengthKey));
  makeDataKey(key, index, dataKey, sizeof(dataKey));
  gOpenThreadPlatformState.settings.remove(lengthKey);
  gOpenThreadPlatformState.settings.remove(dataKey);
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

void setLastLogLineV(const char* format, va_list args) {
  vsnprintf(gOpenThreadPlatformState.snapshot.lastLogLine,
            sizeof(gOpenThreadPlatformState.snapshot.lastLogLine), format, args);
}

void setLastLogLine(const char* format, ...) {
  va_list args;
  va_start(args, format);
  setLastLogLineV(format, args);
  va_end(args);
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

void recordUnsupportedCrypto(void) {
  ++gOpenThreadPlatformState.snapshot.cryptoUnsupportedCount;
}

}  // namespace

void OpenThreadPlatformSkeleton::begin() { otSysInit(0, nullptr); }

void OpenThreadPlatformSkeleton::end() { otSysDeinit(); }

void OpenThreadPlatformSkeleton::process(otInstance* instance) {
  otSysProcessDrivers(instance);
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
  state.txFrame = {};
  ensureTxFrameInitialized();

  state.snapshot.initialized = true;
  state.snapshot.settingsInitialized = true;
  state.snapshot.radioCaps = OT_RADIO_CAPS_NONE;
  state.snapshot.radioState = OT_RADIO_STATE_DISABLED;
  state.snapshot.receiveSensitivityDbm = -100;
  state.snapshot.txPowerDbm = 0;
  state.snapshot.ccaThresholdDbm = -75;
  state.snapshot.femLnaGainDbm = 0;
  state.snapshot.regionCode = static_cast<uint16_t>('W' << 8U) | static_cast<uint16_t>('W');
  state.snapshot.cslAccuracyPpm = 100;
  state.snapshot.cslUncertainty10us = 4;
  state.snapshot.alternateShortAddress = OT_RADIO_INVALID_SHORT_ADDR;
  state.snapshot.lastRssiDbm = OT_RADIO_RSSI_INVALID;
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
  gOpenThreadPlatformState.snapshot = {};
  gOpenThreadPlatformState.settingsOpen = false;
  gOpenThreadPlatformState.sensitiveKeys = nullptr;
  gOpenThreadPlatformState.lastRadioNowLow = 0;
  gOpenThreadPlatformState.radioNowHigh = 0;
  memset(gOpenThreadPlatformState.txPsdu, 0, sizeof(gOpenThreadPlatformState.txPsdu));
  gOpenThreadPlatformState.txFrame = {};
  ensureTxFrameInitialized();
  gOpenThreadPlatformState.diagCallback = nullptr;
  gOpenThreadPlatformState.diagCallbackContext = nullptr;
}

bool otSysPseudoResetWasRequested(void) {
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.pseudoResetRequested;
}

void otSysProcessDrivers(otInstance*) {
  using namespace xiao_nrf54l15;

  OpenThreadPlatformState& state = gOpenThreadPlatformState;
  state.snapshot.processCount++;

  const uint32_t nowMs = otPlatAlarmMilliGetNow();
  if (state.snapshot.alarmMilliRunning &&
      static_cast<int32_t>(nowMs - state.snapshot.alarmMilliDeadline) >= 0) {
    state.snapshot.alarmMilliRunning = false;
    state.snapshot.alarmMilliFires++;
  }

  const uint32_t nowUs = otPlatAlarmMicroGetNow();
  if (state.snapshot.alarmMicroRunning &&
      static_cast<int32_t>(nowUs - state.snapshot.alarmMicroDeadline) >= 0) {
    state.snapshot.alarmMicroRunning = false;
    state.snapshot.alarmMicroFires++;
  }

  updateRadioTime();
}

void otSysEventSignalPending(void) {
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
  if (logLine == nullptr) {
    xiao_nrf54l15::gOpenThreadPlatformState.snapshot.lastLogLine[0] = '\0';
    return;
  }
  strncpy(xiao_nrf54l15::gOpenThreadPlatformState.snapshot.lastLogLine, logLine,
          sizeof(xiao_nrf54l15::gOpenThreadPlatformState.snapshot.lastLogLine) - 1U);
  xiao_nrf54l15::gOpenThreadPlatformState
      .snapshot.lastLogLine[sizeof(xiao_nrf54l15::gOpenThreadPlatformState.snapshot.lastLogLine) - 1U] = '\0';
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
      xiao_nrf54l15::kCryptoSupportKeyRefs;
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
  if (context == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoHmacSha256Deinit(otCryptoContext* context) {
  if (context == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  context->mContext = nullptr;
  context->mContextSize = 0U;
  return OT_ERROR_NONE;
}

otError otPlatCryptoHmacSha256Start(otCryptoContext* context,
                                    const otCryptoKey* key) {
  if (context == nullptr || key == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoHmacSha256Update(otCryptoContext* context,
                                     const void* buffer,
                                     uint16_t bufferLength) {
  if (context == nullptr || (buffer == nullptr && bufferLength != 0U)) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoHmacSha256Finish(otCryptoContext* context,
                                     uint8_t* buffer,
                                     size_t bufferLength) {
  if (context == nullptr || (buffer == nullptr && bufferLength != 0U)) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoAesInit(otCryptoContext* context) {
  if (context == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }

  if (context->mContext != nullptr) {
    xiao_nrf54l15::secureZero(context->mContext, context->mContextSize);
    free(context->mContext);
    context->mContext = nullptr;
    context->mContextSize = 0U;
  }

  xiao_nrf54l15::CryptoAesContext* aesContext =
      static_cast<xiao_nrf54l15::CryptoAesContext*>(
          calloc(1U, sizeof(xiao_nrf54l15::CryptoAesContext)));
  if (aesContext == nullptr) {
    return OT_ERROR_NO_BUFS;
  }
  aesContext->header.kind = xiao_nrf54l15::CryptoContextKind::kAes;
  context->mContext = aesContext;
  context->mContextSize = sizeof(*aesContext);
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
  if (context->mContext != nullptr) {
    xiao_nrf54l15::secureZero(context->mContext, context->mContextSize);
    free(context->mContext);
  }
  context->mContext = nullptr;
  context->mContextSize = 0U;
  return OT_ERROR_NONE;
}

otError otPlatCryptoHkdfInit(otCryptoContext* context) {
  if (context == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoHkdfExpand(otCryptoContext* context,
                               const uint8_t* info,
                               uint16_t infoLength,
                               uint8_t* outputKey,
                               uint16_t outputKeyLength) {
  if (context == nullptr ||
      (info == nullptr && infoLength != 0U) ||
      (outputKey == nullptr && outputKeyLength != 0U)) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoHkdfExtract(otCryptoContext* context,
                                const uint8_t* salt,
                                uint16_t saltLength,
                                const otCryptoKey* inputKey) {
  if (context == nullptr ||
      (salt == nullptr && saltLength != 0U) ||
      inputKey == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoHkdfDeinit(otCryptoContext* context) {
  if (context == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  context->mContext = nullptr;
  context->mContextSize = 0U;
  return OT_ERROR_NONE;
}

otError otPlatCryptoSha256Init(otCryptoContext* context) {
  if (context == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoSha256Deinit(otCryptoContext* context) {
  if (context == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  context->mContext = nullptr;
  context->mContextSize = 0U;
  return OT_ERROR_NONE;
}

otError otPlatCryptoSha256Start(otCryptoContext* context) {
  if (context == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoSha256Update(otCryptoContext* context,
                                 const void* buffer,
                                 uint16_t bufferLength) {
  if (context == nullptr || (buffer == nullptr && bufferLength != 0U)) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

otError otPlatCryptoSha256Finish(otCryptoContext* context,
                                 uint8_t* hash,
                                 uint16_t hashSize) {
  if (context == nullptr || (hash == nullptr && hashSize != 0U)) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
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
  if ((password == nullptr && passwordLen != 0U) ||
      (salt == nullptr && saltLen != 0U) ||
      (key == nullptr && keyLen != 0U) ||
      iterationCounter == 0U) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::recordUnsupportedCrypto();
  return OT_ERROR_NOT_CAPABLE;
}

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
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioRxOnWhenIdle = enable;
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
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioState = OT_RADIO_STATE_SLEEP;
  return OT_ERROR_NONE;
}

otError otPlatRadioDisable(otInstance*) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioEnabled = false;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioState = OT_RADIO_STATE_DISABLED;
  return OT_ERROR_NONE;
}

bool otPlatRadioIsEnabled(otInstance*) {
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioEnabled;
}

otError otPlatRadioSleep(otInstance*) {
  if (!xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioEnabled) {
    return OT_ERROR_INVALID_STATE;
  }
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioState = OT_RADIO_STATE_SLEEP;
  return OT_ERROR_NONE;
}

otError otPlatRadioReceive(otInstance*, uint8_t channel) {
  if (!xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioEnabled) {
    return OT_ERROR_INVALID_STATE;
  }
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioChannel = channel;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioState = OT_RADIO_STATE_RECEIVE;
  return OT_ERROR_NONE;
}

otError otPlatRadioReceiveAt(otInstance*, uint8_t channel, uint32_t, uint32_t) {
  return otPlatRadioReceive(nullptr, channel);
}

otRadioFrame* otPlatRadioGetTransmitBuffer(otInstance*) {
  xiao_nrf54l15::ensureTxFrameInitialized();
  return &xiao_nrf54l15::gOpenThreadPlatformState.txFrame;
}

otError otPlatRadioTransmit(otInstance*, otRadioFrame* frame) {
  if (!xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioEnabled) {
    return OT_ERROR_INVALID_STATE;
  }
  if (frame == nullptr) {
    return OT_ERROR_INVALID_ARGS;
  }
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioState = OT_RADIO_STATE_TRANSMIT;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.txRequestCount++;
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioState =
      xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioRxOnWhenIdle ? OT_RADIO_STATE_RECEIVE
                                                                         : OT_RADIO_STATE_SLEEP;
  return OT_ERROR_NONE;
}

int8_t otPlatRadioGetRssi(otInstance*) {
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.lastRssiDbm;
}

otError otPlatRadioEnergyScan(otInstance*, uint8_t, uint16_t) { return OT_ERROR_NOT_IMPLEMENTED; }

void otPlatRadioEnableSrcMatch(otInstance*, bool) {}

otError otPlatRadioAddSrcMatchShortEntry(otInstance*, otShortAddress) { return OT_ERROR_NOT_IMPLEMENTED; }

otError otPlatRadioAddSrcMatchExtEntry(otInstance*, const otExtAddress*) {
  return OT_ERROR_NOT_IMPLEMENTED;
}

otError otPlatRadioClearSrcMatchShortEntry(otInstance*, otShortAddress) {
  return OT_ERROR_NOT_IMPLEMENTED;
}

otError otPlatRadioClearSrcMatchExtEntry(otInstance*, const otExtAddress*) {
  return OT_ERROR_NOT_IMPLEMENTED;
}

void otPlatRadioClearSrcMatchShortEntries(otInstance*) {}

void otPlatRadioClearSrcMatchExtEntries(otInstance*) {}

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

otError otPlatDiagProcess(otInstance*, uint8_t, char**) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.diagProcessCount++;
  return OT_ERROR_INVALID_COMMAND;
}

void otPlatDiagModeSet(bool mode) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.diagModeEnabled = mode;
}

bool otPlatDiagModeGet(void) {
  return xiao_nrf54l15::gOpenThreadPlatformState.snapshot.diagModeEnabled;
}

void otPlatDiagChannelSet(uint8_t channel) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.radioChannel = channel;
}

void otPlatDiagTxPowerSet(int8_t txPower) {
  xiao_nrf54l15::gOpenThreadPlatformState.snapshot.txPowerDbm = txPower;
}

void otPlatDiagRadioReceived(otInstance*, otRadioFrame*, otError) {}

void otPlatDiagAlarmCallback(otInstance*) {}

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
