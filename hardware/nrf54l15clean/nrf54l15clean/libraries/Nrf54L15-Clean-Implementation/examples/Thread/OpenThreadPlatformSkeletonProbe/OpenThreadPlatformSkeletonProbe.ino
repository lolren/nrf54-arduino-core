#include "openthread_platform_nrf54l15.h"

#include <openthread/error.h>
#include <openthread/instance.h>
#include <openthread/platform/alarm-micro.h>
#include <openthread/platform/alarm-milli.h>
#include <openthread/platform/crypto.h>
#include <openthread/platform/entropy.h>
#include <openthread/platform/logging.h>
#include <openthread/platform/radio.h>
#include <openthread/platform/settings.h>
#include <openthread-system.h>

using xiao_nrf54l15::OpenThreadPlatformSkeleton;
using xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot;

namespace {

uint32_t gRadioTxStartedCount = 0;
uint32_t gRadioTxDoneCount = 0;
uint32_t gRadioRxDoneCount = 0;
otError gRadioLastTxError = OT_ERROR_NONE;
uint16_t gRadioLastTxLength = 0;
uint8_t gRadioLastTxChannel = 0;

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

extern "C" void otPlatRadioTxStarted(otInstance*, otRadioFrame* frame) {
  ++gRadioTxStartedCount;
  if (frame != nullptr) {
    gRadioLastTxLength = frame->mLength;
    gRadioLastTxChannel = frame->mChannel;
  }
}

extern "C" void otPlatRadioTxDone(otInstance*, otRadioFrame*, otRadioFrame*,
                                  otError error) {
  ++gRadioTxDoneCount;
  gRadioLastTxError = error;
}

extern "C" void otPlatRadioReceiveDone(otInstance*, otRadioFrame*, otError) {
  ++gRadioRxDoneCount;
}

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

  otPlatCryptoInit();
  otPlatCryptoRandomInit();
  uint8_t cryptoRandom[8] = {0};
  const otError cryptoRandomError = otPlatCryptoRandomGet(
      cryptoRandom, sizeof(cryptoRandom));

  const uint8_t aesKey[16] = {
      0x00, 0x01, 0x02, 0x03,
      0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0A, 0x0B,
      0x0C, 0x0D, 0x0E, 0x0F,
  };
  const uint8_t aesInput[16] = {
      0x00, 0x11, 0x22, 0x33,
      0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xAA, 0xBB,
      0xCC, 0xDD, 0xEE, 0xFF,
  };
  uint8_t aesDirectOutput[16] = {0};
  uint8_t aesRefOutput[16] = {0};
  uint8_t exportedKey[16] = {0};
  size_t exportedKeyLength = sizeof(exportedKey);

  otCryptoContext aesDirect = {};
  const otError aesInitError = otPlatCryptoAesInit(&aesDirect);
  const otCryptoKey aesDirectKey = {aesKey, sizeof(aesKey), 0};
  const otError aesSetError = otPlatCryptoAesSetKey(&aesDirect, &aesDirectKey);
  const otError aesEncryptError =
      otPlatCryptoAesEncrypt(&aesDirect, aesInput, aesDirectOutput);
  const otError aesFreeError = otPlatCryptoAesFree(&aesDirect);

  otCryptoKeyRef keyRef = 0;
  const otError keyImportError =
      otPlatCryptoImportKey(&keyRef, OT_CRYPTO_KEY_TYPE_AES,
                            OT_CRYPTO_KEY_ALG_AES_ECB,
                            OT_CRYPTO_KEY_USAGE_EXPORT |
                                OT_CRYPTO_KEY_USAGE_ENCRYPT |
                                OT_CRYPTO_KEY_USAGE_DECRYPT,
                            OT_CRYPTO_KEY_STORAGE_VOLATILE, aesKey,
                            sizeof(aesKey));
  const bool keyHasBeforeDestroy = otPlatCryptoHasKey(keyRef);
  const otError keyExportError =
      otPlatCryptoExportKey(keyRef, exportedKey, sizeof(exportedKey),
                            &exportedKeyLength);

  otCryptoContext aesRef = {};
  const otError aesRefInitError = otPlatCryptoAesInit(&aesRef);
  const otCryptoKey aesRefKey = {nullptr, 0, keyRef};
  const otError aesRefSetError = otPlatCryptoAesSetKey(&aesRef, &aesRefKey);
  const otError aesRefEncryptError =
      otPlatCryptoAesEncrypt(&aesRef, aesInput, aesRefOutput);
  const otError aesRefFreeError = otPlatCryptoAesFree(&aesRef);
  const otError keyDestroyError = otPlatCryptoDestroyKey(keyRef);
  const bool keyHasAfterDestroy = otPlatCryptoHasKey(keyRef);

  otCryptoContext shaContext = {};
  const otError shaInitError = otPlatCryptoSha256Init(&shaContext);

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
  otRadioFrame* txFrame = otPlatRadioGetTransmitBuffer(nullptr);
  if (txFrame != nullptr && txFrame->mPsdu != nullptr) {
    txFrame->mLength = 3U;
    txFrame->mChannel = 15U;
    txFrame->mInfo.mTxInfo.mTxPower = 8;
    txFrame->mInfo.mTxInfo.mCsmaCaEnabled = false;
    txFrame->mPsdu[0] = 0x02U;
    txFrame->mPsdu[1] = 0x00U;
    txFrame->mPsdu[2] = 0x5AU;
    (void)otPlatRadioTransmit(nullptr, txFrame);
  }
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
  Serial.print(" crypto=");
  Serial.print(static_cast<int>(cryptoRandomError));
  Serial.print("/");
  printHexBuffer(cryptoRandom, sizeof(cryptoRandom));
  Serial.print(" aes=");
  Serial.print(static_cast<int>(aesInitError));
  Serial.print("/");
  Serial.print(static_cast<int>(aesSetError));
  Serial.print("/");
  Serial.print(static_cast<int>(aesEncryptError));
  Serial.print("/");
  printHexBuffer(aesDirectOutput, sizeof(aesDirectOutput));
  Serial.print("/");
  Serial.print(static_cast<int>(aesFreeError));
  Serial.print(" keyref=");
  Serial.print(static_cast<int>(keyImportError));
  Serial.print("/");
  Serial.print(keyRef);
  Serial.print("/");
  Serial.print(static_cast<int>(keyExportError));
  Serial.print("/");
  Serial.print(exportedKeyLength);
  Serial.print("/");
  printHexBuffer(exportedKey, exportedKeyLength);
  Serial.print("/");
  Serial.print(keyHasBeforeDestroy ? 1 : 0);
  Serial.print("/");
  Serial.print(static_cast<int>(aesRefInitError));
  Serial.print("/");
  Serial.print(static_cast<int>(aesRefSetError));
  Serial.print("/");
  Serial.print(static_cast<int>(aesRefEncryptError));
  Serial.print("/");
  printHexBuffer(aesRefOutput, sizeof(aesRefOutput));
  Serial.print("/");
  Serial.print(static_cast<int>(aesRefFreeError));
  Serial.print("/");
  Serial.print(static_cast<int>(keyDestroyError));
  Serial.print("/");
  Serial.print(keyHasAfterDestroy ? 1 : 0);
  Serial.print(" sha=");
  Serial.print(static_cast<int>(shaInitError));
  Serial.print(" cstats=");
  Serial.print(snapshot.cryptoInitialized ? 1 : 0);
  Serial.print("/");
  Serial.print(snapshot.cryptoRandomHardware ? 1 : 0);
  Serial.print("/");
  Serial.print(snapshot.cryptoAesReady ? 1 : 0);
  Serial.print("/");
  Serial.print(snapshot.cryptoKeyCount);
  Serial.print("/");
  Serial.print(snapshot.cryptoRandomRequests);
  Serial.print("/");
  Serial.print(snapshot.cryptoAesEncryptCount);
  Serial.print("/");
  Serial.print(snapshot.cryptoUnsupportedCount);
  Serial.print("/0x");
  Serial.print(snapshot.cryptoSupportMask, HEX);
  Serial.print(" radio=");
  printRadioState(snapshot.radioState);
  Serial.print("@ch");
  Serial.print(snapshot.radioChannel);
  Serial.print(" backend=");
  Serial.print(snapshot.radioBackendWrappedDirect ? 1 : 0);
  Serial.print("/");
  Serial.print(snapshot.radioBackendReady ? 1 : 0);
  Serial.print(" pan=0x");
  Serial.print(snapshot.panId, HEX);
  Serial.print(" short=0x");
  Serial.print(snapshot.shortAddress, HEX);
  Serial.print(" alt=0x");
  Serial.print(snapshot.alternateShortAddress, HEX);
  Serial.print(" tx=");
  Serial.print(snapshot.txPowerDbm);
  Serial.print(" txpath=");
  Serial.print(snapshot.txRequestCount);
  Serial.print("/");
  Serial.print(snapshot.radioTxDoneCount);
  Serial.print("/");
  Serial.print(snapshot.radioLastTxLength);
  Serial.print("/");
  Serial.print(static_cast<int>(snapshot.radioLastError));
  Serial.print(" cb=");
  Serial.print(gRadioTxStartedCount);
  Serial.print("/");
  Serial.print(gRadioTxDoneCount);
  Serial.print("/");
  Serial.print(gRadioRxDoneCount);
  Serial.print("/");
  Serial.print(gRadioLastTxLength);
  Serial.print("@");
  Serial.print(gRadioLastTxChannel);
  Serial.print("/");
  Serial.print(static_cast<int>(gRadioLastTxError));
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
