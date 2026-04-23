#include "openthread_platform_nrf54l15.h"

#include <openthread/error.h>
#include <openthread/instance.h>
#include <openthread/platform/alarm-micro.h>
#include <openthread/platform/alarm-milli.h>
#include <openthread/platform/crypto.h>
#include <openthread/platform/entropy.h>
#include <openthread/platform/logging.h>
#include <openthread/platform/memory.h>
#include <openthread/platform/radio.h>
#include <openthread/platform/settings.h>
#include <openthread-system.h>

using xiao_nrf54l15::OpenThreadPlatformSkeleton;
using xiao_nrf54l15::OpenThreadPlatformSkeletonSnapshot;
using xiao_nrf54l15::OpenThreadRuntimeOwnership;

namespace {

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
extern "C" size_t nrf54l15OpenThreadStageInstanceSize(void);
constexpr uint32_t kStageInitDelayMs = 4000UL;
otInstance* gStageInstance = nullptr;
bool gStageInstanceCreated = false;
bool gStageInstanceInitialized = false;
bool gStageInitAttempted = false;
#endif

uint32_t gRadioTxStartedCount = 0;
uint32_t gRadioTxDoneCount = 0;
uint32_t gRadioRxDoneCount = 0;
uint32_t gRadioEnergyScanDoneCount = 0;
otError gRadioLastTxError = OT_ERROR_NONE;
otError gRadioLastRxError = OT_ERROR_NONE;
uint16_t gRadioLastTxLength = 0;
uint8_t gRadioLastTxChannel = 0;
uint16_t gRadioLastRxLength = 0;
uint8_t gRadioLastRxChannel = 0;
uint8_t gRadioLastRxLqi = OT_RADIO_LQI_NONE;
int8_t gRadioLastEnergyScanDbm = OT_RADIO_RSSI_INVALID;
int8_t gRadioLastRxRssi = OT_RADIO_RSSI_INVALID;
uint64_t gRadioLastRxTimestampUs = 0;
uint8_t gRadioLastRxPsdu[16] = {0};
uint8_t gRadioLastRxPsduLength = 0;
uint32_t gLastRadioReportMs = 0;

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

void printU64(uint64_t value) {
  char buffer[24] = {0};
  size_t index = sizeof(buffer) - 1U;
  buffer[index] = '\0';
  do {
    const uint64_t digit = value % 10ULL;
    value /= 10ULL;
    buffer[--index] = static_cast<char>('0' + digit);
  } while (value != 0ULL && index != 0U);
  Serial.print(&buffer[index]);
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

void printRadioSummary(const OpenThreadPlatformSkeletonSnapshot& snapshot) {
  Serial.print("ot_radio ");
  printRadioState(snapshot.radioState);
  Serial.print("@ch");
  Serial.print(snapshot.radioChannel);
  Serial.print(" sm=");
  Serial.print(snapshot.radioSrcMatchEnabled ? 1 : 0);
  Serial.print("/");
  Serial.print(snapshot.radioSrcMatchShortCount);
  Serial.print("/");
  Serial.print(snapshot.radioSrcMatchExtCount);
  Serial.print(" rx=");
  Serial.print(snapshot.radioRxDoneCount);
  Serial.print("/");
  Serial.print(snapshot.radioReceivePollCount);
  Serial.print(" rxmeta=");
  Serial.print(snapshot.radioChannel);
  Serial.print("/");
  Serial.print(snapshot.radioLastRxLength);
  Serial.print("/");
  Serial.print(snapshot.lastRssiDbm);
  Serial.print(" cb=");
  Serial.print(gRadioRxDoneCount);
  Serial.print("/");
  Serial.print(gRadioLastRxChannel);
  Serial.print("/");
  Serial.print(gRadioLastRxLength);
  Serial.print("/");
  Serial.print(gRadioLastRxLqi);
  Serial.print("/");
  Serial.print(gRadioLastRxRssi);
  Serial.print("/");
  Serial.print(static_cast<int>(gRadioLastRxError));
  Serial.print("/");
  printU64(gRadioLastRxTimestampUs);
  Serial.print("/");
  printHexBuffer(gRadioLastRxPsdu, gRadioLastRxPsduLength);
  Serial.println();
}

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
void printStageCoreSummary() {
  Serial.print("ot_core stage=");
  Serial.print(OpenThreadRuntimeOwnership::kCoreBuildSeamAvailable ? 1 : 0);
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kCoreBuildSeamCurrentEnabled ? 1 : 0);
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kCoreCryptoFallbackCurrentEnabled
                   ? 1
                   : 0);
  Serial.print("/");
  Serial.print(otGetVersionString());
  Serial.print("/");
  Serial.print(nrf54l15OpenThreadStageInstanceSize());
  Serial.print("/");
  Serial.print(gStageInstanceCreated ? 1 : 0);
  Serial.print("/");
  Serial.print(gStageInstanceInitialized ? 1 : 0);
  Serial.print("/");
  Serial.print(gStageInitAttempted ? 1 : 0);
  Serial.println();
}
#endif

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

extern "C" void otPlatRadioReceiveDone(otInstance*, otRadioFrame* frame,
                                       otError error) {
  ++gRadioRxDoneCount;
  gRadioLastRxError = error;
  if (frame != nullptr) {
    gRadioLastRxLength = frame->mLength;
    gRadioLastRxChannel = frame->mChannel;
    gRadioLastRxLqi = frame->mInfo.mRxInfo.mLqi;
    gRadioLastRxRssi = frame->mInfo.mRxInfo.mRssi;
    gRadioLastRxTimestampUs = frame->mInfo.mRxInfo.mTimestamp;
    gRadioLastRxPsduLength = static_cast<uint8_t>(
        (frame->mLength < sizeof(gRadioLastRxPsdu))
            ? frame->mLength
            : sizeof(gRadioLastRxPsdu));
    if (frame->mPsdu != nullptr && gRadioLastRxPsduLength != 0U) {
      memcpy(gRadioLastRxPsdu, frame->mPsdu, gRadioLastRxPsduLength);
    }
  }
}

extern "C" void otPlatRadioEnergyScanDone(otInstance*, int8_t maxRssi) {
  ++gRadioEnergyScanDoneCount;
  gRadioLastEnergyScanDbm = maxRssi;
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
  const otError entropyError =
      OpenThreadPlatformSkeleton::fillEntropy(entropy, sizeof(entropy));

  otPlatCryptoInit();
  otPlatCryptoRandomInit();
  uint8_t cryptoRandom[8] = {0};
  const otError cryptoRandomError =
      otPlatCryptoRandomGet(cryptoRandom, sizeof(cryptoRandom));

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
  void* memoryBlock = otPlatCAlloc(2U, 16U);
  const bool memoryAllocOk = memoryBlock != nullptr;
  if (memoryBlock != nullptr) {
    memset(memoryBlock, 0xA5, 32U);
    otPlatFree(memoryBlock);
  }

  const otError sleepWhileDisabledError = otPlatRadioSleep(nullptr);
  const otError radioEnableError = otPlatRadioEnable(nullptr);
  otPlatRadioSetPanId(nullptr, 0x1234);
  otExtAddress extAddress = {};
  otPlatRadioGetIeeeEui64(nullptr, extAddress.m8);
  otPlatRadioSetExtendedAddress(nullptr, &extAddress);
  otPlatRadioSetShortAddress(nullptr, 0x2345);
  otPlatRadioSetAlternateShortAddress(nullptr, 0x3456);
  otPlatRadioSetTransmitPower(nullptr, 8);
  otRadioFrame* txFrame = otPlatRadioGetTransmitBuffer(nullptr);
  bool txFramePrepared = false;
  if (txFrame != nullptr && txFrame->mPsdu != nullptr) {
    txFramePrepared = true;
    txFrame->mLength = 3U;
    txFrame->mChannel = 15U;
    txFrame->mInfo.mTxInfo.mTxPower = 8;
    txFrame->mInfo.mTxInfo.mCsmaCaEnabled = false;
    txFrame->mPsdu[0] = 0x02U;
    txFrame->mPsdu[1] = 0x00U;
    txFrame->mPsdu[2] = 0x5AU;
  }
  const otError txFromSleepError =
      txFramePrepared ? otPlatRadioTransmit(nullptr, txFrame)
                      : OT_ERROR_INVALID_ARGS;
  const otError receiveEnterError = otPlatRadioReceive(nullptr, 15);
  const otError energyScanError = otPlatRadioEnergyScan(nullptr, 15, 1);
  const otError txDuringEnergyScanError =
      txFramePrepared ? otPlatRadioTransmit(nullptr, txFrame)
                      : OT_ERROR_INVALID_ARGS;

  delay(300);
  OpenThreadPlatformSkeleton::process();

  otPlatRadioSetRxOnWhenIdle(nullptr, true);
  otPlatRadioEnableSrcMatch(nullptr, true);
  (void)otPlatRadioAddSrcMatchShortEntry(nullptr, 0x4567);
  otExtAddress childExtAddress = {};
  for (uint8_t i = 0; i < OT_EXT_ADDRESS_SIZE; ++i) {
    childExtAddress.m8[i] = static_cast<uint8_t>(0xA0U + i);
  }
  (void)otPlatRadioAddSrcMatchExtEntry(nullptr, &childExtAddress);
  (void)otPlatRadioClearSrcMatchShortEntry(nullptr, 0x4567);

  const otError sleepFromReceiveError = otPlatRadioSleep(nullptr);
  const otError receiveAfterSleepError = otPlatRadioReceive(nullptr, 15);
  const otError finalTxError =
      txFramePrepared ? otPlatRadioTransmit(nullptr, txFrame)
                      : OT_ERROR_INVALID_ARGS;
  otPlatLog(OT_LOG_LEVEL_NOTE, OT_LOG_REGION_PLATFORM,
            "OpenThread skeleton ready");

  delay(300);
  OpenThreadPlatformSkeleton::process();
  const otError finalReceiveError = otPlatRadioReceive(nullptr, 15);

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  gStageInstance = nullptr;
  gStageInstanceCreated = false;
  gStageInstanceInitialized = false;
  gStageInitAttempted = false;
#endif

  OpenThreadPlatformSkeletonSnapshot snapshot;
  OpenThreadPlatformSkeleton::snapshot(&snapshot);

  Serial.print("ot_platform ok=1 api=");
  Serial.print(OPENTHREAD_API_VERSION);
  Serial.print(" policy=");
  Serial.print(OpenThreadRuntimeOwnership::kCpuAppHostsCore ? "cpuapp" : "other");
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kUsesZigbeeRadioBackend
                   ? "zigbee-radio"
                   : "other-radio");
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kUsesVprRadioOffload ? "vpr" : "no-vpr");
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kHeadersOnlyImportActive
                   ? "headers-only"
                   : "full-core");
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kFullCoreIntegrated ? "core-live" : "core-staged");
  Serial.print(" corebuild=");
  Serial.print(OpenThreadRuntimeOwnership::kCoreBuildSeamAvailable ? 1 : 0);
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kCoreBuildSeamCurrentEnabled ? 1 : 0);
  Serial.print("/");
  Serial.print(OpenThreadRuntimeOwnership::kCoreCryptoFallbackCurrentEnabled
                   ? 1
                   : 0);
  Serial.print("/");
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  Serial.print(otGetVersionString());
  Serial.print("/");
  Serial.print(nrf54l15OpenThreadStageInstanceSize());
  Serial.print("/");
  Serial.print(gStageInstanceCreated ? 1 : 0);
  Serial.print("/");
  Serial.print(gStageInstanceInitialized ? 1 : 0);
  Serial.print("/");
  Serial.print(gStageInitAttempted ? 1 : 0);
#else
  Serial.print("disabled");
#endif
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
  Serial.print(" mem=");
  Serial.print(memoryAllocOk ? 1 : 0);
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
  Serial.print(" sm=");
  Serial.print(snapshot.radioSrcMatchEnabled ? 1 : 0);
  Serial.print("/");
  Serial.print(snapshot.radioSrcMatchShortCount);
  Serial.print("/");
  Serial.print(snapshot.radioSrcMatchExtCount);
  Serial.print(" rxmeta=");
  Serial.print(snapshot.radioChannel);
  Serial.print("/");
  Serial.print(snapshot.radioLastRxLength);
  Serial.print("/");
  Serial.print(snapshot.lastRssiDbm);
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
  Serial.print("/");
  Serial.print(snapshot.radioLastTxAckFramePending ? 1 : 0);
  Serial.print(" xstate=");
  Serial.print(static_cast<int>(sleepWhileDisabledError));
  Serial.print("/");
  Serial.print(static_cast<int>(radioEnableError));
  Serial.print("/");
  Serial.print(static_cast<int>(txFromSleepError));
  Serial.print("/");
  Serial.print(static_cast<int>(receiveEnterError));
  Serial.print("/");
  Serial.print(static_cast<int>(energyScanError));
  Serial.print("/");
  Serial.print(static_cast<int>(txDuringEnergyScanError));
  Serial.print("/");
  Serial.print(static_cast<int>(sleepFromReceiveError));
  Serial.print("/");
  Serial.print(static_cast<int>(receiveAfterSleepError));
  Serial.print("/");
  Serial.print(static_cast<int>(finalTxError));
  Serial.print("/");
  Serial.print(static_cast<int>(finalReceiveError));
  Serial.print(" escan=");
  Serial.print(static_cast<int>(energyScanError));
  Serial.print("/");
  Serial.print(snapshot.radioEnergyScanCount);
  Serial.print("/");
  Serial.print(snapshot.radioLastEdLevel);
  Serial.print("/");
  Serial.print(snapshot.lastRssiDbm);
  Serial.print("/");
  Serial.print(gRadioEnergyScanDoneCount);
  Serial.print("/");
  Serial.print(gRadioLastEnergyScanDbm);
  Serial.print(" cb=");
  Serial.print(gRadioTxStartedCount);
  Serial.print("/");
  Serial.print(gRadioTxDoneCount);
  Serial.print("/");
  Serial.print(gRadioRxDoneCount);
  Serial.print("/");
  Serial.print(gRadioEnergyScanDoneCount);
  Serial.print("/");
  Serial.print(gRadioLastTxLength);
  Serial.print("@");
  Serial.print(gRadioLastTxChannel);
  Serial.print("/");
  Serial.print(static_cast<int>(gRadioLastTxError));
  Serial.print(" rxcb=");
  Serial.print(gRadioLastRxChannel);
  Serial.print("/");
  Serial.print(gRadioLastRxLength);
  Serial.print("/");
  Serial.print(gRadioLastRxLqi);
  Serial.print("/");
  Serial.print(gRadioLastRxRssi);
  Serial.print("/");
  Serial.print(static_cast<int>(gRadioLastRxError));
  Serial.print("/");
  printU64(gRadioLastRxTimestampUs);
  Serial.print("/");
  printHexBuffer(gRadioLastRxPsdu, gRadioLastRxPsduLength);
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
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  if (!gStageInitAttempted && millis() >= kStageInitDelayMs) {
    gStageInitAttempted = true;
    gStageInstance = otInstanceInitSingle();
    gStageInstanceCreated = gStageInstance != nullptr;
    gStageInstanceInitialized =
        gStageInstanceCreated && otInstanceIsInitialized(gStageInstance);
  }
#endif
  OpenThreadPlatformSkeletonSnapshot snapshot;
#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  if ((millis() - gLastRadioReportMs) >= 1000UL) {
    printStageCoreSummary();
  }
#endif
  if ((millis() - gLastRadioReportMs) >= 1000UL &&
      OpenThreadPlatformSkeleton::snapshot(&snapshot)) {
    gLastRadioReportMs = millis();
    printRadioSummary(snapshot);
  }
  delay(1000);
}
