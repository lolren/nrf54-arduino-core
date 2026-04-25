#pragma once

#include <Preferences.h>
#include <stddef.h>
#include <stdint.h>

#include "matter_foundation_target.h"
#include "matter_onoff_light.h"
#include "matter_onoff_light_endpoint.h"

namespace xiao_nrf54l15 {

enum class MatterOnNetworkDatasetSource : uint8_t {
  kNone = 0U,
  kDemo = 1U,
  kPassphrase = 2U,
  kExplicit = 3U,
};

enum class MatterCommissioningWindowState : uint8_t {
  kClosed = 0U,
  kPendingReadiness = 1U,
  kOpen = 2U,
  kExpired = 3U,
};

struct MatterOnNetworkIdentity {
  uint32_t setupPinCode = 20202021UL;
  uint16_t discriminator = 3840U;
  uint16_t vendorId = 12U;
  uint16_t productId = 1U;
  MatterCommissioningFlow commissioningFlow =
      MatterCommissioningFlow::kStandard;
};

struct MatterOnNetworkPersistentState {
  uint32_t magic = 0U;
  uint16_t version = 0U;
  uint32_t setupPinCode = 0U;
  uint16_t discriminator = 0U;
  uint16_t vendorId = 0U;
  uint16_t productId = 0U;
  uint8_t commissioningFlow = 0U;
  uint8_t reserved = 0U;
};

struct MatterOnNetworkOnOffLightConfig {
  const char* storageNamespace = "matter_node";
  const char* lightStorageNamespace = "matter_onoff";
  bool restorePersistentState = true;
  bool wipeThreadSettings = false;
  bool autoStartThread = true;
  bool autoRequestRouterRole = false;
  bool useDemoDataset = false;
  MatterOnNetworkIdentity identity = {};
  const otOperationalDataset* explicitThreadDataset = nullptr;
  const char* threadPassPhrase = nullptr;
  const char* threadNetworkName = nullptr;
  const uint8_t* threadExtPanId = nullptr;
};

struct MatterOnNetworkOnOffLightStatus {
  bool storageOpen = false;
  bool lightReady = false;
  bool threadStarted = false;
  bool threadAttached = false;
  bool threadDatasetConfigured = false;
  bool threadDatasetExportable = false;
  bool manualCodeReady = false;
  bool qrCodeReady = false;
  bool readyForOnNetworkCommissioning = false;
  bool buildSeamsAligned = false;
  bool commissioningWindowPending = false;
  MatterOnNetworkDatasetSource datasetSource =
      MatterOnNetworkDatasetSource::kNone;
  MatterCommissioningWindowState commissioningWindowState =
      MatterCommissioningWindowState::kClosed;
  uint16_t commissioningWindowSecondsRemaining = 0U;
  Nrf54ThreadExperimental::Role threadRole =
      Nrf54ThreadExperimental::Role::kUnknown;
  uint16_t rloc16 = 0xFFFFU;
  MatterOnNetworkIdentity identity = {};
  MatterOnOffLightDeviceState light = {};
};

struct MatterOnNetworkCommissioningBundle {
  static constexpr size_t kOpenThreadDatasetHexCapacity =
      (OT_OPERATIONAL_DATASET_MAX_LENGTH * 2U) + 1U;

  bool ready = false;
  bool manualCodeReady = false;
  bool qrCodeReady = false;
  bool openThreadDatasetReady = false;
#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
  static constexpr size_t kMatterThreadDatasetHexCapacity =
      (chip::Thread::kSizeOperationalDataset * 2U) + 1U;
  bool matterThreadDatasetReady = false;
  size_t matterThreadDatasetHexLength = 0U;
  char matterThreadDatasetHex[kMatterThreadDatasetHexCapacity] = {0};
#endif
  MatterOnNetworkDatasetSource datasetSource =
      MatterOnNetworkDatasetSource::kNone;
  MatterCommissioningWindowState commissioningWindowState =
      MatterCommissioningWindowState::kClosed;
  uint16_t commissioningWindowSecondsRemaining = 0U;
  size_t openThreadDatasetHexLength = 0U;
  char manualCode[kMatterManualPairingLongCodeLength + 1U] = {0};
  char qrCode[kMatterQrCodeTextLength + 1U] = {0};
  char openThreadDatasetHex[kOpenThreadDatasetHexCapacity] = {0};
};

class Nrf54MatterOnNetworkOnOffLightNode {
 public:
  Nrf54MatterOnNetworkOnOffLightNode() = default;

  bool begin(const MatterOnNetworkOnOffLightConfig* config = nullptr);
  void end();
  void process();

  bool snapshot(MatterOnNetworkOnOffLightStatus* outStatus) const;

  bool setIdentity(const MatterOnNetworkIdentity& identity, bool persist = true);
  const MatterOnNetworkIdentity& identity() const;
  bool savePersistentIdentity();
  bool clearPersistentIdentity();

  bool useDemoThreadDataset();
  bool useThreadDatasetFromPassphrase(
      const char* passPhrase,
      const char* networkName,
      const uint8_t extPanId[OT_EXT_PAN_ID_SIZE]);
  bool useThreadDataset(const otOperationalDataset& dataset);

  bool openCommissioningWindow(uint16_t seconds);
  void closeCommissioningWindow();
  MatterCommissioningWindowState commissioningWindowState() const;
  bool commissioningWindowOpen() const;
  uint16_t commissioningWindowSecondsRemaining() const;
  bool buildCommissioningBundle(
      MatterOnNetworkCommissioningBundle* outBundle) const;
  bool exportOpenThreadDatasetTlvs(otOperationalDatasetTlvs* outTlvs) const;
  bool exportOpenThreadDatasetHex(char* outBuffer, size_t outBufferSize,
                                  size_t* outHexLength = nullptr) const;

  bool manualPairingCode(char* outBuffer, size_t outBufferSize) const;
  bool qrCode(char* outBuffer, size_t outBufferSize) const;
  bool readyForOnNetworkCommissioning() const;

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
  bool exportThreadDataset(chip::Thread::OperationalDataset* outDataset,
                           CHIP_ERROR* outError = nullptr) const;
  bool exportMatterThreadDatasetHex(char* outBuffer, size_t outBufferSize,
                                    size_t* outHexLength = nullptr,
                                    CHIP_ERROR* outError = nullptr) const;
#endif

  Nrf54MatterOnOffLightDevice& light();
  const Nrf54MatterOnOffLightDevice& light() const;
  Nrf54MatterOnOffLightEndpoint& endpoint();
  const Nrf54MatterOnOffLightEndpoint& endpoint() const;
  Nrf54ThreadExperimental& thread();
  const Nrf54ThreadExperimental& thread() const;

  static void buildDefaultIdentity(MatterOnNetworkIdentity* outIdentity);
  static bool identityValid(const MatterOnNetworkIdentity& identity);
  static const char* datasetSourceName(MatterOnNetworkDatasetSource source);
  static const char* commissioningWindowStateName(
      MatterCommissioningWindowState state);

 private:
  static constexpr uint32_t kPersistentStateMagic = 0x4D4E4554UL;
  static constexpr uint16_t kPersistentStateVersion = 1U;
  static constexpr char kPersistentStateKey[] = "setup";

  static uint16_t remainingWindowSeconds(uint32_t endMs);
  static bool bytesToUpperHex(const uint8_t* data, size_t length,
                              char* outBuffer, size_t outBufferSize,
                              size_t* outHexLength = nullptr);

  bool loadPersistentIdentity(MatterOnNetworkIdentity* outIdentity) const;
  void buildManualPayload(MatterManualPairingPayload* outPayload) const;
  void buildQrPayload(MatterQrCodePayload* outPayload) const;
  bool threadDatasetExportable() const;

  Preferences prefs_;
  bool storageOpen_ = false;
  bool lightReady_ = false;
  bool autoRequestRouterRole_ = false;
  bool routerRoleRequested_ = false;
  bool commissioningWindowPending_ = false;
  bool commissioningWindowExpired_ = false;
  uint16_t commissioningWindowDurationSeconds_ = 0U;
  uint32_t commissioningWindowEndMs_ = 0U;
  MatterOnNetworkDatasetSource datasetSource_ =
      MatterOnNetworkDatasetSource::kNone;
  MatterOnNetworkIdentity identity_ = {};
  Nrf54MatterOnOffLightFoundation foundation_;
  Nrf54MatterOnOffLightDevice light_;
  Nrf54MatterOnOffLightEndpoint endpoint_;
  Nrf54ThreadExperimental thread_;
};

}  // namespace xiao_nrf54l15
