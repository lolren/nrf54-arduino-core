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
  state.lastRadioNowLow = micros();
  state.radioNowHigh = 0;
  updateRadioTime();

  otPlatRadioGetIeeeEui64(nullptr, state.snapshot.extendedAddress.m8);
  closeSettings();
  ensureSettingsOpen();
}

void otSysDeinit(void) {
  using namespace xiao_nrf54l15;

  closeSettings();
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

  uint16_t offset = 0;
  while (offset < outputLength) {
    const uint64_t word = xiao_nrf54l15::nextEntropyWord();
    for (uint8_t i = 0; (i < sizeof(word)) && (offset < outputLength); ++i) {
      output[offset++] = static_cast<uint8_t>((word >> (i * 8U)) & 0xFFU);
    }
  }
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
