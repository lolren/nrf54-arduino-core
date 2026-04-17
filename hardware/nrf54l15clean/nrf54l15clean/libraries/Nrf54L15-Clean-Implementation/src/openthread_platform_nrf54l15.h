#pragma once

#include <stddef.h>
#include <stdint.h>

#include <openthread/error.h>
#include <openthread/platform/radio.h>

namespace xiao_nrf54l15 {

struct OpenThreadPlatformSkeletonSnapshot {
  bool initialized = false;
  bool settingsInitialized = false;
  bool eventPending = false;
  bool pseudoResetRequested = false;
  bool diagModeEnabled = false;

  bool alarmMilliRunning = false;
  bool alarmMicroRunning = false;
  uint32_t alarmMilliDeadline = 0;
  uint32_t alarmMicroDeadline = 0;
  uint32_t alarmMilliFires = 0;
  uint32_t alarmMicroFires = 0;

  otRadioCaps radioCaps = OT_RADIO_CAPS_NONE;
  otRadioState radioState = OT_RADIO_STATE_DISABLED;
  bool radioEnabled = false;
  bool radioPromiscuous = false;
  bool radioRxOnWhenIdle = false;
  uint8_t radioChannel = 0;
  uint8_t cslAccuracyPpm = 0;
  uint8_t cslUncertainty10us = 0;

  otPanId panId = 0;
  otShortAddress shortAddress = 0;
  otShortAddress alternateShortAddress = OT_RADIO_INVALID_SHORT_ADDR;
  otExtAddress extendedAddress = {};
  int8_t txPowerDbm = OT_RADIO_POWER_INVALID;
  int8_t ccaThresholdDbm = 0;
  int8_t femLnaGainDbm = 0;
  int8_t lastRssiDbm = OT_RADIO_RSSI_INVALID;
  int8_t receiveSensitivityDbm = -100;
  uint16_t regionCode = 0;

  uint16_t sensitiveKeyCount = 0;
  uint16_t settingsKeyCount = 0;
  uint16_t lastSettingsKey = 0;
  uint16_t lastSettingsLength = 0;
  bool cryptoInitialized = false;
  bool cryptoRandomHardware = false;
  bool cryptoAesReady = false;
  uint16_t cryptoKeyCount = 0;
  uint16_t cryptoLastKeyLength = 0;
  uint32_t cryptoRandomRequests = 0;
  uint32_t cryptoAesEncryptCount = 0;
  uint32_t cryptoUnsupportedCount = 0;
  uint32_t cryptoSupportMask = 0;

  uint32_t processCount = 0;
  uint32_t diagProcessCount = 0;
  uint32_t txRequestCount = 0;

  uint64_t radioNowUs = 0;
  char lastLogLine[96] = {0};
};

class OpenThreadPlatformSkeleton {
 public:
  static void begin();
  static void end();
  static void process(otInstance* instance = nullptr);
  static bool snapshot(OpenThreadPlatformSkeletonSnapshot* outSnapshot);

  static otError fillEntropy(uint8_t* output, uint16_t outputLength);
  static otError writeSetting(uint16_t key, const uint8_t* value, uint16_t valueLength);
  static otError addSetting(uint16_t key, const uint8_t* value, uint16_t valueLength);
  static otError readSetting(uint16_t key, int index, uint8_t* value, uint16_t* valueLength);
  static otError deleteSetting(uint16_t key, int index);
  static void wipeSettings();
};

}  // namespace xiao_nrf54l15
