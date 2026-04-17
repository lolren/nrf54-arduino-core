#include "openthread_platform_nrf54l15.h"

#include <openthread/error.h>
#include <openthread/instance.h>
#include <openthread/platform/alarm-micro.h>
#include <openthread/platform/alarm-milli.h>
#include <openthread/platform/entropy.h>
#include <openthread/platform/logging.h>
#include <openthread/platform/radio.h>
#include <openthread/platform/settings.h>
#include <openthread-system.h>

using xiao_nrf54l15::OpenThreadPlatformSkeleton;
using xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot;

namespace {

void printHexByte(uint8_t value) {
  if (value < 16) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

void printHexBuffer(const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    printHexByte(data[i]);
  }
}

void printRadioState(otRadioState state) {
  switch (state) {
    case OT_RADIO_STATE_DISABLED:
      Serial.print("disabled");
      break;
    case OT_RADIO_STATE_SLEEP:
      Serial.print("sleep");
      break;
    case OT_RADIO_STATE_RECEIVE:
      Serial.print("receive");
      break;
    case OT_RADIO_STATE_TRANSMIT:
      Serial.print("transmit");
      break;
    default:
      Serial.print("unknown");
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

  OpenThreadPlatformSkeleton::begin();
  otPlatSettingsInit(nullptr, nullptr, 0);

  const uint32_t nowMs = otPlatAlarmMilliGetNow();
  const uint32_t nowUs = otPlatAlarmMicroGetNow();
  otPlatAlarmMilliStartAt(nullptr, nowMs, 250);
  otPlatAlarmMicroStartAt(nullptr, nowUs, 500);

  uint8_t entropy[8] = {0};
  const otError entropyError = OpenThreadPlatformSkeleton::fillEntropy(entropy, sizeof(entropy));

  const uint8_t settingValueA[4] = {'T', 'H', 'R', 'D'};
  const uint8_t settingValueB[4] = {'N', 'R', 'F', '4'};
  const uint16_t settingKey = OT_SETTINGS_KEY_VENDOR_RESERVED_MIN;

  const otError setError = OpenThreadPlatformSkeleton::writeSetting(
      settingKey, settingValueA, sizeof(settingValueA));
  const otError addError = OpenThreadPlatformSkeleton::addSetting(
      settingKey, settingValueB, sizeof(settingValueB));

  uint8_t settingRead[8] = {0};
  uint16_t settingReadLength = sizeof(settingRead);
  const otError getError = OpenThreadPlatformSkeleton::readSetting(
      settingKey, 1, settingRead, &settingReadLength);

  otPlatRadioEnable(nullptr);
  otPlatRadioSetPanId(nullptr, 0x1234);
  otExtAddress extAddress = {};
  otPlatRadioGetIeeeEui64(nullptr, extAddress.m8);
  otPlatRadioSetExtendedAddress(nullptr, &extAddress);
  otPlatRadioSetShortAddress(nullptr, 0x2345);
  otPlatRadioSetAlternateShortAddress(nullptr, 0x3456);
  otPlatRadioSetTransmitPower(nullptr, 8);
  otPlatRadioReceive(nullptr, 15);
  otPlatLog(OT_LOG_LEVEL_NOTE, OT_LOG_REGION_PLATFORM, "OpenThread skeleton ready");

  delay(300);
  OpenThreadPlatformSkeleton::process();

  OpenThreadPlatformSkeletonSnapshot snapshot;
  OpenThreadPlatformSkeleton::snapshot(&snapshot);

  Serial.print("ot_platform ok=1 api=");
  Serial.print(OPENTHREAD_API_VERSION);
  Serial.print(" entropy=");
  Serial.print(static_cast<int>(entropyError));
  Serial.print("/");
  printHexBuffer(entropy, sizeof(entropy));
  Serial.print(" settings=");
  Serial.print(static_cast<int>(setError));
  Serial.print("/");
  Serial.print(static_cast<int>(addError));
  Serial.print("/");
  Serial.print(static_cast<int>(getError));
  Serial.print("/");
  Serial.print(settingReadLength);
  Serial.print("/");
  printHexBuffer(settingRead, settingReadLength);
  Serial.print(" radio=");
  printRadioState(snapshot.radioState);
  Serial.print("@ch");
  Serial.print(snapshot.radioChannel);
  Serial.print(" pan=0x");
  Serial.print(snapshot.panId, HEX);
  Serial.print(" short=0x");
  Serial.print(snapshot.shortAddress, HEX);
  Serial.print(" alt=0x");
  Serial.print(snapshot.alternateShortAddress, HEX);
  Serial.print(" tx=");
  Serial.print(snapshot.txPowerDbm);
  Serial.print(" fires=");
  Serial.print(snapshot.alarmMilliFires);
  Serial.print("/");
  Serial.print(snapshot.alarmMicroFires);
  Serial.print(" proc=");
  Serial.print(snapshot.processCount);
  Serial.print(" log=\"");
  Serial.print(snapshot.lastLogLine);
  Serial.println("\"");
}

void loop() {
  OpenThreadPlatformSkeleton::process();
  delay(1000);
}
